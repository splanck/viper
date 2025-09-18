// File: src/vm/OpHandlers.cpp
// Purpose: Implements opcode handlers and dispatch table for the VM interpreter.
// Key invariants: Handlers conform to IL opcode semantics and preserve frame integrity.
// Ownership/Lifetime: Operates on VM-managed frames without persisting references.
// Links: docs/il-spec.md

#include "vm/OpHandlers.hpp"

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Opcode.hpp"
#include "il/core/OpcodeInfo.hpp"
#include "il/core/Value.hpp"
#include "vm/RuntimeBridge.hpp"
#include <cstdint>
#include <cassert>
#include <cstring>
#include <utility>
#include <vector>

using namespace il::core;

namespace il::vm::detail
{
namespace
{
/// Store an opcode result into the destination register when one is provided.
inline void storeResult(Frame &fr, const Instr &in, const Slot &val)
{
    if (in.result)
    {
        if (fr.regs.size() <= *in.result)
            fr.regs.resize(*in.result + 1);
        fr.regs[*in.result] = val;
    }
}

#define VM_BIN_INT_OPS(OP)                                                                         \
    OP(Add, +)                                                                                     \
    OP(Sub, -)                                                                                     \
    OP(Mul, *)                                                                                     \
    OP(Xor, ^)                                                                                     \
    OP(Shl, <<)

#define VM_BIN_FLOAT_OPS(OP)                                                                       \
    OP(FAdd, +)                                                                                    \
    OP(FSub, -)                                                                                    \
    OP(FMul, *)                                                                                    \
    OP(FDiv, /)

#define VM_INT_CMP_OPS(OP)                                                                         \
    OP(ICmpEq, ==)                                                                                 \
    OP(ICmpNe, !=)                                                                                 \
    OP(SCmpGT, >)                                                                                  \
    OP(SCmpLT, <)                                                                                  \
    OP(SCmpLE, <=)                                                                                 \
    OP(SCmpGE, >=)

#define VM_FLOAT_CMP_OPS(OP)                                                                       \
    OP(FCmpEQ, ==)                                                                                 \
    OP(FCmpNE, !=)                                                                                 \
    OP(FCmpGT, >)                                                                                  \
    OP(FCmpLT, <)                                                                                  \
    OP(FCmpLE, <=)                                                                                 \
    OP(FCmpGE, >=)

#define DEFINE_BIN_INT_OP(NAME, OP)                                                                \
    VM::ExecResult OpHandlers::handle##NAME(VM &vm,                                                \
                                            Frame &fr,                                             \
                                            const Instr &in,                                       \
                                            const VM::BlockMap &blocks,                            \
                                            const BasicBlock *&bb,                                 \
                                            size_t &ip)                                            \
    {                                                                                              \
        (void)blocks;                                                                              \
        (void)bb;                                                                                  \
        (void)ip;                                                                                  \
        Slot a = vm.eval(fr, in.operands[0]);                                                      \
        Slot b = vm.eval(fr, in.operands[1]);                                                      \
        Slot res{};                                                                                \
        res.i64 = a.i64 OP b.i64;                                                                  \
        storeResult(fr, in, res);                                                                  \
        return {};                                                                                 \
    }

#define DEFINE_BIN_FLOAT_OP(NAME, OP)                                                              \
    VM::ExecResult OpHandlers::handle##NAME(VM &vm,                                                \
                                            Frame &fr,                                             \
                                            const Instr &in,                                       \
                                            const VM::BlockMap &blocks,                            \
                                            const BasicBlock *&bb,                                 \
                                            size_t &ip)                                            \
    {                                                                                              \
        (void)blocks;                                                                              \
        (void)bb;                                                                                  \
        (void)ip;                                                                                  \
        Slot a = vm.eval(fr, in.operands[0]);                                                      \
        Slot b = vm.eval(fr, in.operands[1]);                                                      \
        Slot res{};                                                                                \
        res.f64 = a.f64 OP b.f64;                                                                  \
        storeResult(fr, in, res);                                                                  \
        return {};                                                                                 \
    }

#define DEFINE_INT_CMP(NAME, CMP)                                                                  \
    VM::ExecResult OpHandlers::handle##NAME(VM &vm,                                                \
                                            Frame &fr,                                             \
                                            const Instr &in,                                       \
                                            const VM::BlockMap &blocks,                            \
                                            const BasicBlock *&bb,                                 \
                                            size_t &ip)                                            \
    {                                                                                              \
        (void)blocks;                                                                              \
        (void)bb;                                                                                  \
        (void)ip;                                                                                  \
        Slot a = vm.eval(fr, in.operands[0]);                                                      \
        Slot b = vm.eval(fr, in.operands[1]);                                                      \
        Slot res{};                                                                                \
        res.i64 = (a.i64 CMP b.i64) ? 1 : 0;                                                       \
        storeResult(fr, in, res);                                                                  \
        return {};                                                                                 \
    }

#define DEFINE_FLOAT_CMP(NAME, CMP)                                                                \
    VM::ExecResult OpHandlers::handle##NAME(VM &vm,                                                \
                                            Frame &fr,                                             \
                                            const Instr &in,                                       \
                                            const VM::BlockMap &blocks,                            \
                                            const BasicBlock *&bb,                                 \
                                            size_t &ip)                                            \
    {                                                                                              \
        (void)blocks;                                                                              \
        (void)bb;                                                                                  \
        (void)ip;                                                                                  \
        Slot a = vm.eval(fr, in.operands[0]);                                                      \
        Slot b = vm.eval(fr, in.operands[1]);                                                      \
        Slot res{};                                                                                \
        res.i64 = (a.f64 CMP b.f64) ? 1 : 0;                                                       \
        storeResult(fr, in, res);                                                                  \
        return {};                                                                                 \
    }
} // namespace

/// Common branch implementation that transfers control and block parameters.
VM::ExecResult OpHandlers::branchToTarget(VM &vm,
                                          Frame &fr,
                                          const Instr &in,
                                          size_t idx,
                                          const VM::BlockMap &blocks,
                                          const BasicBlock *&bb,
                                          size_t &ip)
{
    const auto &targetLabel = in.labels[idx];
    auto itBlk = blocks.find(targetLabel);
    assert(itBlk != blocks.end() && "invalid block");
    const BasicBlock *target = itBlk->second;
    const auto &args = in.brArgs.size() > idx ? in.brArgs[idx] : std::vector<Value>{};
    for (size_t i = 0; i < args.size() && i < target->params.size(); ++i)
        fr.params[target->params[i].id] = vm.eval(fr, args[i]);
    bb = target;
    ip = 0;
    VM::ExecResult r{};
    r.jumped = true;
    return r;
}

/// Allocate a block of zeroed stack memory.
VM::ExecResult OpHandlers::handleAlloca(VM &vm,
                                        Frame &fr,
                                        const Instr &in,
                                        const VM::BlockMap &blocks,
                                        const BasicBlock *&bb,
                                        size_t &ip)
{
    (void)blocks;
    (void)bb;
    (void)ip;
    if (in.operands.size() < 1)
    {
        RuntimeBridge::trap("missing allocation size", in.loc, fr.func->name, "");
        VM::ExecResult r{};
        r.returned = true;
        return r;
    }
    int64_t bytes = vm.eval(fr, in.operands[0]).i64;
    if (bytes < 0)
    {
        RuntimeBridge::trap("negative allocation", in.loc, fr.func->name, "");
        VM::ExecResult r{};
        r.value.i64 = 0;
        r.returned = true;
        return r;
    }
    size_t sz = static_cast<size_t>(bytes);
    size_t addr = fr.sp;
    assert(addr + sz <= fr.stack.size());
    std::memset(fr.stack.data() + addr, 0, sz);
    Slot res{};
    res.ptr = fr.stack.data() + addr;
    fr.sp += sz;
    storeResult(fr, in, res);
    return {};
}

/// Load a typed value from memory.
VM::ExecResult OpHandlers::handleLoad(VM &vm,
                                      Frame &fr,
                                      const Instr &in,
                                      const VM::BlockMap &blocks,
                                      const BasicBlock *&bb,
                                      size_t &ip)
{
    (void)blocks;
    (void)bb;
    (void)ip;
    void *ptr = vm.eval(fr, in.operands[0]).ptr;
    assert(ptr && "null load");
    Slot res{};
    if (in.type.kind == Type::Kind::I64)
        res.i64 = *reinterpret_cast<int64_t *>(ptr);
    else if (in.type.kind == Type::Kind::I1)
        res.i64 = static_cast<int64_t>(*reinterpret_cast<uint8_t *>(ptr) & 1);
    else if (in.type.kind == Type::Kind::F64)
        res.f64 = *reinterpret_cast<double *>(ptr);
    else if (in.type.kind == Type::Kind::Str)
        res.str = *reinterpret_cast<rt_string *>(ptr);
    else if (in.type.kind == Type::Kind::Ptr)
        res.ptr = *reinterpret_cast<void **>(ptr);
    storeResult(fr, in, res);
    return {};
}

/// Store a typed value to memory and emit debug events.
VM::ExecResult OpHandlers::handleStore(VM &vm,
                                       Frame &fr,
                                       const Instr &in,
                                       const VM::BlockMap &blocks,
                                       const BasicBlock *&bb,
                                       size_t &ip)
{
    (void)blocks;
    void *ptr = vm.eval(fr, in.operands[0]).ptr;
    assert(ptr && "null store");
    Slot val = vm.eval(fr, in.operands[1]);
    if (in.type.kind == Type::Kind::I64)
        *reinterpret_cast<int64_t *>(ptr) = val.i64;
    else if (in.type.kind == Type::Kind::I1)
        *reinterpret_cast<uint8_t *>(ptr) = static_cast<uint8_t>(val.i64 != 0);
    else if (in.type.kind == Type::Kind::F64)
        *reinterpret_cast<double *>(ptr) = val.f64;
    else if (in.type.kind == Type::Kind::Str)
        *reinterpret_cast<rt_string *>(ptr) = val.str;
    else if (in.type.kind == Type::Kind::Ptr)
        *reinterpret_cast<void **>(ptr) = val.ptr;
    if (in.operands[0].kind == Value::Kind::Temp)
    {
        unsigned id = in.operands[0].id;
        if (id < fr.func->valueNames.size())
        {
            const std::string &nm = fr.func->valueNames[id];
            if (!nm.empty())
                vm.debug.onStore(nm, in.type.kind, val.i64, val.f64, fr.func->name, bb->label, ip);
        }
    }
    return {};
}

VM_BIN_INT_OPS(DEFINE_BIN_INT_OP)
VM_BIN_FLOAT_OPS(DEFINE_BIN_FLOAT_OP)

/// Compute pointer address using getelementptr-style offsetting.
VM::ExecResult OpHandlers::handleGEP(VM &vm,
                                     Frame &fr,
                                     const Instr &in,
                                     const VM::BlockMap &blocks,
                                     const BasicBlock *&bb,
                                     size_t &ip)
{
    (void)blocks;
    (void)bb;
    (void)ip;
    Slot base = vm.eval(fr, in.operands[0]);
    Slot off = vm.eval(fr, in.operands[1]);
    Slot res{};
    res.ptr = static_cast<char *>(base.ptr) + off.i64;
    storeResult(fr, in, res);
    return {};
}

VM_INT_CMP_OPS(DEFINE_INT_CMP)
VM_FLOAT_CMP_OPS(DEFINE_FLOAT_CMP)

#undef DEFINE_FLOAT_CMP
#undef DEFINE_INT_CMP
#undef DEFINE_BIN_FLOAT_OP
#undef DEFINE_BIN_INT_OP
#undef VM_FLOAT_CMP_OPS
#undef VM_INT_CMP_OPS
#undef VM_BIN_FLOAT_OPS
#undef VM_BIN_INT_OPS

/// Unconditionally branch to another basic block, transferring arguments.
VM::ExecResult OpHandlers::handleBr(VM &vm,
                                    Frame &fr,
                                    const Instr &in,
                                    const VM::BlockMap &blocks,
                                    const BasicBlock *&bb,
                                    size_t &ip)
{
    return branchToTarget(vm, fr, in, 0, blocks, bb, ip);
}

/// Conditionally branch based on evaluated operand.
VM::ExecResult OpHandlers::handleCBr(VM &vm,
                                     Frame &fr,
                                     const Instr &in,
                                     const VM::BlockMap &blocks,
                                     const BasicBlock *&bb,
                                     size_t &ip)
{
    Slot cond = vm.eval(fr, in.operands[0]);
    size_t idx = (cond.i64 != 0) ? 0 : 1;
    return branchToTarget(vm, fr, in, idx, blocks, bb, ip);
}

/// Return from the current function.
VM::ExecResult OpHandlers::handleRet(VM &vm,
                                     Frame &fr,
                                     const Instr &in,
                                     const VM::BlockMap &blocks,
                                     const BasicBlock *&bb,
                                     size_t &ip)
{
    (void)blocks;
    (void)bb;
    (void)ip;
    VM::ExecResult r{};
    r.value.i64 = 0;
    if (!in.operands.empty())
        r.value = vm.eval(fr, in.operands[0]);
    r.returned = true;
    return r;
}

/// Load the address of a global string into the destination register.
VM::ExecResult OpHandlers::handleAddrOf(VM &vm,
                                        Frame &fr,
                                        const Instr &in,
                                        const VM::BlockMap &blocks,
                                        const BasicBlock *&bb,
                                        size_t &ip)
{
    (void)blocks;
    (void)bb;
    (void)ip;
    Slot tmp = vm.eval(fr, in.operands[0]);
    Slot res{};
    res.ptr = tmp.str;
    storeResult(fr, in, res);
    return {};
}

/// Load a constant string from the global table.
VM::ExecResult OpHandlers::handleConstStr(VM &vm,
                                          Frame &fr,
                                          const Instr &in,
                                          const VM::BlockMap &blocks,
                                          const BasicBlock *&bb,
                                          size_t &ip)
{
    (void)blocks;
    (void)bb;
    (void)ip;
    Slot res = vm.eval(fr, in.operands[0]);
    storeResult(fr, in, res);
    return {};
}

/// Invoke a function or runtime bridge call.
VM::ExecResult OpHandlers::handleCall(VM &vm,
                                      Frame &fr,
                                      const Instr &in,
                                      const VM::BlockMap &blocks,
                                      const BasicBlock *&bb,
                                      size_t &ip)
{
    (void)blocks;
    (void)ip;
    std::vector<Slot> callArgs;
    for (const auto &op : in.operands)
        callArgs.push_back(vm.eval(fr, op));
    Slot res{};
    auto itFn = vm.fnMap.find(in.callee);
    if (itFn != vm.fnMap.end())
        res = vm.execFunction(*itFn->second, callArgs);
    else
        res = RuntimeBridge::call(in.callee, callArgs, in.loc, fr.func->name, bb->label);
    storeResult(fr, in, res);
    return {};
}

/// Convert a signed integer slot to floating point.
VM::ExecResult OpHandlers::handleSitofp(VM &vm,
                                        Frame &fr,
                                        const Instr &in,
                                        const VM::BlockMap &blocks,
                                        const BasicBlock *&bb,
                                        size_t &ip)
{
    (void)blocks;
    (void)bb;
    (void)ip;
    Slot v = vm.eval(fr, in.operands[0]);
    Slot res{};
    res.f64 = static_cast<double>(v.i64);
    storeResult(fr, in, res);
    return {};
}

/// Convert a floating-point slot to signed integer.
VM::ExecResult OpHandlers::handleFptosi(VM &vm,
                                        Frame &fr,
                                        const Instr &in,
                                        const VM::BlockMap &blocks,
                                        const BasicBlock *&bb,
                                        size_t &ip)
{
    (void)blocks;
    (void)bb;
    (void)ip;
    Slot v = vm.eval(fr, in.operands[0]);
    Slot res{};
    res.i64 = static_cast<int64_t>(v.f64);
    storeResult(fr, in, res);
    return {};
}

/// Truncate or zero-extend an integer to 1 bit.
VM::ExecResult OpHandlers::handleTruncOrZext1(VM &vm,
                                              Frame &fr,
                                              const Instr &in,
                                              const VM::BlockMap &blocks,
                                              const BasicBlock *&bb,
                                              size_t &ip)
{
    (void)blocks;
    (void)bb;
    (void)ip;
    Slot v = vm.eval(fr, in.operands[0]);
    v.i64 &= 1;
    storeResult(fr, in, v);
    return {};
}

/// Signal an unrecoverable trap to the runtime bridge.
VM::ExecResult OpHandlers::handleTrap(VM &vm,
                                      Frame &fr,
                                      const Instr &in,
                                      const VM::BlockMap &blocks,
                                      const BasicBlock *&bb,
                                      size_t &ip)
{
    (void)vm;
    (void)blocks;
    (void)ip;
    RuntimeBridge::trap("trap", in.loc, fr.func->name, bb->label);
    VM::ExecResult r{};
    r.value.i64 = 0;
    r.returned = true;
    return r;
}

} // namespace il::vm::detail

namespace il::vm
{
/// Return the opcode handler dispatch table.
const VM::OpcodeHandlerTable &VM::getOpcodeHandlers()
{
    return detail::getOpcodeHandlers();
}
} // namespace il::vm

namespace il::vm::detail
{
/// Access the lazily initialised opcode handler table.
const VM::OpcodeHandlerTable &getOpcodeHandlers()
{
    static const VM::OpcodeHandlerTable table = []
    {
        VM::OpcodeHandlerTable t{};
        for (size_t idx = 0; idx < kOpcodeTable.size(); ++idx)
        {
            switch (kOpcodeTable[idx].vmDispatch)
            {
                case VMDispatch::None:
                    break;
                case VMDispatch::Alloca:
                    t[idx] = &OpHandlers::handleAlloca;
                    break;
                case VMDispatch::Load:
                    t[idx] = &OpHandlers::handleLoad;
                    break;
                case VMDispatch::Store:
                    t[idx] = &OpHandlers::handleStore;
                    break;
                case VMDispatch::GEP:
                    t[idx] = &OpHandlers::handleGEP;
                    break;
                case VMDispatch::Add:
                    t[idx] = &OpHandlers::handleAdd;
                    break;
                case VMDispatch::Sub:
                    t[idx] = &OpHandlers::handleSub;
                    break;
                case VMDispatch::Mul:
                    t[idx] = &OpHandlers::handleMul;
                    break;
                case VMDispatch::Xor:
                    t[idx] = &OpHandlers::handleXor;
                    break;
                case VMDispatch::Shl:
                    t[idx] = &OpHandlers::handleShl;
                    break;
                case VMDispatch::FAdd:
                    t[idx] = &OpHandlers::handleFAdd;
                    break;
                case VMDispatch::FSub:
                    t[idx] = &OpHandlers::handleFSub;
                    break;
                case VMDispatch::FMul:
                    t[idx] = &OpHandlers::handleFMul;
                    break;
                case VMDispatch::FDiv:
                    t[idx] = &OpHandlers::handleFDiv;
                    break;
                case VMDispatch::ICmpEq:
                    t[idx] = &OpHandlers::handleICmpEq;
                    break;
                case VMDispatch::ICmpNe:
                    t[idx] = &OpHandlers::handleICmpNe;
                    break;
                case VMDispatch::SCmpGT:
                    t[idx] = &OpHandlers::handleSCmpGT;
                    break;
                case VMDispatch::SCmpLT:
                    t[idx] = &OpHandlers::handleSCmpLT;
                    break;
                case VMDispatch::SCmpLE:
                    t[idx] = &OpHandlers::handleSCmpLE;
                    break;
                case VMDispatch::SCmpGE:
                    t[idx] = &OpHandlers::handleSCmpGE;
                    break;
                case VMDispatch::FCmpEQ:
                    t[idx] = &OpHandlers::handleFCmpEQ;
                    break;
                case VMDispatch::FCmpNE:
                    t[idx] = &OpHandlers::handleFCmpNE;
                    break;
                case VMDispatch::FCmpGT:
                    t[idx] = &OpHandlers::handleFCmpGT;
                    break;
                case VMDispatch::FCmpLT:
                    t[idx] = &OpHandlers::handleFCmpLT;
                    break;
                case VMDispatch::FCmpLE:
                    t[idx] = &OpHandlers::handleFCmpLE;
                    break;
                case VMDispatch::FCmpGE:
                    t[idx] = &OpHandlers::handleFCmpGE;
                    break;
                case VMDispatch::Br:
                    t[idx] = &OpHandlers::handleBr;
                    break;
                case VMDispatch::CBr:
                    t[idx] = &OpHandlers::handleCBr;
                    break;
                case VMDispatch::Ret:
                    t[idx] = &OpHandlers::handleRet;
                    break;
                case VMDispatch::AddrOf:
                    t[idx] = &OpHandlers::handleAddrOf;
                    break;
                case VMDispatch::ConstStr:
                    t[idx] = &OpHandlers::handleConstStr;
                    break;
                case VMDispatch::Call:
                    t[idx] = &OpHandlers::handleCall;
                    break;
                case VMDispatch::Sitofp:
                    t[idx] = &OpHandlers::handleSitofp;
                    break;
                case VMDispatch::Fptosi:
                    t[idx] = &OpHandlers::handleFptosi;
                    break;
                case VMDispatch::TruncOrZext1:
                    t[idx] = &OpHandlers::handleTruncOrZext1;
                    break;
                case VMDispatch::Trap:
                    t[idx] = &OpHandlers::handleTrap;
                    break;
            }
        }
        return t;
    }();
    return table;
}

} // namespace il::vm::detail
