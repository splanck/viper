// File: src/vm/mem_ops.cpp
// Purpose: Implement VM handlers for memory and pointer operations.
// Key invariants: Operations respect frame stack bounds and type semantics.
// Ownership/Lifetime: Handlers mutate the active frame without retaining state.
// Links: docs/il-spec.md

#include "vm/OpHandlers.hpp"

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Type.hpp"
#include "vm/OpHandlerUtils.hpp"
#include "vm/RuntimeBridge.hpp"
#include <cassert>
#include <cstring>

using namespace il::core;

namespace il::vm::detail
{

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
    if (in.operands.empty())
    {
        RuntimeBridge::trap("missing allocation size", in.loc, fr.func->name, "");
        VM::ExecResult result{};
        result.returned = true;
        return result;
    }

    int64_t bytes = vm.eval(fr, in.operands[0]).i64;
    if (bytes < 0)
    {
        RuntimeBridge::trap("negative allocation", in.loc, fr.func->name, "");
        VM::ExecResult result{};
        result.returned = true;
        return result;
    }

    const size_t size = static_cast<size_t>(bytes);
    const size_t addr = fr.sp;
    assert(addr + size <= fr.stack.size() && "stack overflow in alloca");
    std::memset(fr.stack.data() + addr, 0, size);

    Slot out{};
    out.ptr = fr.stack.data() + addr;
    fr.sp += size;
    ops::storeResult(fr, in, out);
    return {};
}

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

    Slot out{};
    switch (in.type.kind)
    {
        case Type::Kind::I64:
            out.i64 = *reinterpret_cast<int64_t *>(ptr);
            break;
        case Type::Kind::I1:
            out.i64 = static_cast<int64_t>(*reinterpret_cast<uint8_t *>(ptr) & 1);
            break;
        case Type::Kind::F64:
            out.f64 = *reinterpret_cast<double *>(ptr);
            break;
        case Type::Kind::Str:
            out.str = *reinterpret_cast<rt_string *>(ptr);
            break;
        case Type::Kind::Ptr:
            out.ptr = *reinterpret_cast<void **>(ptr);
            break;
        case Type::Kind::Void:
            out.i64 = 0;
            break;
    }

    ops::storeResult(fr, in, out);
    return {};
}

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
    Slot value = vm.eval(fr, in.operands[1]);

    switch (in.type.kind)
    {
        case Type::Kind::I64:
            *reinterpret_cast<int64_t *>(ptr) = value.i64;
            break;
        case Type::Kind::I1:
            *reinterpret_cast<uint8_t *>(ptr) = static_cast<uint8_t>(value.i64 != 0);
            break;
        case Type::Kind::F64:
            *reinterpret_cast<double *>(ptr) = value.f64;
            break;
        case Type::Kind::Str:
            *reinterpret_cast<rt_string *>(ptr) = value.str;
            break;
        case Type::Kind::Ptr:
            *reinterpret_cast<void **>(ptr) = value.ptr;
            break;
        case Type::Kind::Void:
            break;
    }

    if (in.operands[0].kind == Value::Kind::Temp)
    {
        const unsigned id = in.operands[0].id;
        if (id < fr.func->valueNames.size())
        {
            const std::string &nm = fr.func->valueNames[id];
            if (!nm.empty())
                vm.debug.onStore(nm, in.type.kind, value.i64, value.f64, fr.func->name, bb->label, ip);
        }
    }

    return {};
}

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
    Slot offset = vm.eval(fr, in.operands[1]);
    Slot out{};
    out.ptr = static_cast<char *>(base.ptr) + offset.i64;
    ops::storeResult(fr, in, out);
    return {};
}

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
    Slot out{};
    out.ptr = tmp.str;
    ops::storeResult(fr, in, out);
    return {};
}

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
    Slot out = vm.eval(fr, in.operands[0]);
    ops::storeResult(fr, in, out);
    return {};
}

} // namespace il::vm::detail

