// File: src/vm/VM.cpp
// Purpose: Implements stack-based virtual machine for IL.
// Key invariants: None.
// Ownership/Lifetime: VM references module owned externally.
// Links: docs/il-spec.md

#include "vm/VM.hpp"
#include "VM/DebugScript.h"
#include "il/core/Instr.hpp"
#include "il/core/Opcode.hpp"
#include "vm/RuntimeBridge.hpp"
#include <array>
#include <cassert>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <utility>

using namespace il::core;

namespace il::vm
{

namespace
{
/// @brief Store opcode result @p val into destination register if present.
inline void storeResult(Frame &fr, const il::core::Instr &in, const Slot &val)
{
    if (in.result)
    {
        if (fr.regs.size() <= *in.result)
            fr.regs.resize(*in.result + 1);
        fr.regs[*in.result] = val;
    }
}
} // namespace

VM::VM(const Module &m, TraceConfig tc, uint64_t ms, DebugCtrl dbg, DebugScript *script)
    : mod(m), tracer(tc), debug(std::move(dbg)), script(script), maxSteps(ms)
{
    debug.setSourceManager(tc.sm);
    for (const auto &f : m.functions)
        fnMap[f.name] = &f;
    for (const auto &g : m.globals)
        strMap[g.name] = rt_const_cstr(g.init.c_str());
}

int64_t VM::run()
{
    auto it = fnMap.find("main");
    assert(it != fnMap.end());
    return execFunction(*it->second, {}).i64;
}

Slot VM::eval(Frame &fr, const Value &v)
{
    Slot s{};
    switch (v.kind)
    {
        case Value::Kind::Temp:
            if (v.id < fr.regs.size())
                return fr.regs[v.id];
            return s;
        case Value::Kind::ConstInt:
            s.i64 = v.i64;
            return s;
        case Value::Kind::ConstFloat:
            s.f64 = v.f64;
            return s;
        case Value::Kind::ConstStr:
            s.str = rt_const_cstr(v.str.c_str());
            return s;
        case Value::Kind::GlobalAddr:
            s.str = strMap[v.str];
            return s;
        case Value::Kind::NullPtr:
            s.ptr = nullptr;
            return s;
    }
    return s;
}

Frame VM::setupFrame(const Function &fn,
                     const std::vector<Slot> &args,
                     std::unordered_map<std::string, const BasicBlock *> &blocks,
                     const BasicBlock *&bb)
{
    Frame fr;
    fr.func = &fn;
    fr.regs.resize(64);
    for (const auto &b : fn.blocks)
        blocks[b.label] = &b;
    bb = fn.blocks.empty() ? nullptr : &fn.blocks.front();
    if (bb)
    {
        const auto &params = bb->params;
        for (size_t i = 0; i < params.size() && i < args.size(); ++i)
            fr.params[params[i].id] = args[i];
    }
    return fr;
}

std::optional<Slot> VM::handleDebugBreak(
    Frame &fr, const BasicBlock &bb, size_t ip, bool &skipBreakOnce, const Instr *in)
{
    if (!in)
    {
        if (debug.shouldBreak(bb))
        {
            std::cerr << "[BREAK] fn=@" << fr.func->name << " blk=" << bb.label
                      << " reason=label\n";
            if (!script || script->empty())
            {
                Slot s{};
                s.i64 = 10;
                return s;
            }
            auto act = script->nextAction();
            if (act.kind == DebugActionKind::Step)
                stepBudget = act.count;
            skipBreakOnce = true;
        }
        for (const auto &p : bb.params)
        {
            auto it = fr.params.find(p.id);
            if (it != fr.params.end())
            {
                if (fr.regs.size() <= p.id)
                    fr.regs.resize(p.id + 1);
                fr.regs[p.id] = it->second;
                debug.onStore(p.name,
                              p.type.kind,
                              fr.regs[p.id].i64,
                              fr.regs[p.id].f64,
                              fr.func->name,
                              bb.label,
                              0);
            }
        }
        fr.params.clear();
        return std::nullopt;
    }
    if (debug.hasSrcLineBPs() && debug.shouldBreakOn(*in))
    {
        const auto *sm = debug.getSourceManager();
        std::string path;
        if (sm && in->loc.isValid())
            path = std::filesystem::path(sm->getPath(in->loc.file_id)).filename().string();
        std::cerr << "[BREAK] src=" << path << ':' << in->loc.line << " fn=@" << fr.func->name
                  << " blk=" << bb.label << " ip=#" << ip << "\n";
        Slot s{};
        s.i64 = 10;
        return s;
    }
    return std::nullopt;
}

//===----------------------------------------------------------------------===//
// Opcode handlers
//===----------------------------------------------------------------------===//

#define DEFINE_BIN_INT_OP(NAME, OP)                                                                \
    VM::ExecResult VM::handle##NAME(Frame &fr, const Instr &in)                                    \
    {                                                                                              \
        Slot a = eval(fr, in.operands[0]);                                                         \
        Slot b = eval(fr, in.operands[1]);                                                         \
        Slot res{};                                                                                \
        res.i64 = a.i64 OP b.i64;                                                                  \
        storeResult(fr, in, res);                                                                  \
        return {};                                                                                 \
    }

#define DEFINE_BIN_FLOAT_OP(NAME, OP)                                                              \
    VM::ExecResult VM::handle##NAME(Frame &fr, const Instr &in)                                    \
    {                                                                                              \
        Slot a = eval(fr, in.operands[0]);                                                         \
        Slot b = eval(fr, in.operands[1]);                                                         \
        Slot res{};                                                                                \
        res.f64 = a.f64 OP b.f64;                                                                  \
        storeResult(fr, in, res);                                                                  \
        return {};                                                                                 \
    }

#define DEFINE_INT_CMP(NAME, CMP)                                                                  \
    VM::ExecResult VM::handle##NAME(Frame &fr, const Instr &in)                                    \
    {                                                                                              \
        Slot a = eval(fr, in.operands[0]);                                                         \
        Slot b = eval(fr, in.operands[1]);                                                         \
        Slot res{};                                                                                \
        res.i64 = (a.i64 CMP b.i64) ? 1 : 0;                                                       \
        storeResult(fr, in, res);                                                                  \
        return {};                                                                                 \
    }

#define DEFINE_FLOAT_CMP(NAME, CMP)                                                                \
    VM::ExecResult VM::handle##NAME(Frame &fr, const Instr &in)                                    \
    {                                                                                              \
        Slot a = eval(fr, in.operands[0]);                                                         \
        Slot b = eval(fr, in.operands[1]);                                                         \
        Slot res{};                                                                                \
        res.i64 = (a.f64 CMP b.f64) ? 1 : 0;                                                       \
        storeResult(fr, in, res);                                                                  \
        return {};                                                                                 \
    }

VM::ExecResult VM::handleAlloca(Frame &fr, const Instr &in)
{
    size_t sz = (size_t)eval(fr, in.operands[0]).i64;
    size_t addr = fr.sp;
    assert(addr + sz <= fr.stack.size());
    std::memset(fr.stack.data() + addr, 0, sz);
    Slot res{};
    res.ptr = fr.stack.data() + addr;
    fr.sp += sz;
    storeResult(fr, in, res);
    return {};
}

VM::ExecResult VM::handleLoad(Frame &fr, const Instr &in)
{
    void *ptr = eval(fr, in.operands[0]).ptr;
    assert(ptr && "null load");
    Slot res{};
    if (in.type.kind == Type::Kind::I64)
        res.i64 = *reinterpret_cast<int64_t *>(ptr);
    else if (in.type.kind == Type::Kind::F64)
        res.f64 = *reinterpret_cast<double *>(ptr);
    else if (in.type.kind == Type::Kind::Str)
        res.str = *reinterpret_cast<rt_string *>(ptr);
    else if (in.type.kind == Type::Kind::Ptr)
        res.ptr = *reinterpret_cast<void **>(ptr);
    storeResult(fr, in, res);
    return {};
}

VM::ExecResult VM::handleStore(Frame &fr, const Instr &in, const BasicBlock *bb, size_t ip)
{
    void *ptr = eval(fr, in.operands[0]).ptr;
    assert(ptr && "null store");
    Slot val = eval(fr, in.operands[1]);
    if (in.type.kind == Type::Kind::I64)
        *reinterpret_cast<int64_t *>(ptr) = val.i64;
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
                debug.onStore(nm, in.type.kind, val.i64, val.f64, fr.func->name, bb->label, ip);
        }
    }
    return {};
}

DEFINE_BIN_INT_OP(Add, +)
DEFINE_BIN_INT_OP(Sub, -)
DEFINE_BIN_INT_OP(Mul, *)
DEFINE_BIN_FLOAT_OP(FAdd, +)
DEFINE_BIN_FLOAT_OP(FSub, -)
DEFINE_BIN_FLOAT_OP(FMul, *)
DEFINE_BIN_FLOAT_OP(FDiv, /)
DEFINE_BIN_INT_OP(Xor, ^)
DEFINE_BIN_INT_OP(Shl, <<)

VM::ExecResult VM::handleGEP(Frame &fr, const Instr &in)
{
    Slot base = eval(fr, in.operands[0]);
    Slot off = eval(fr, in.operands[1]);
    Slot res{};
    res.ptr = static_cast<char *>(base.ptr) + off.i64;
    storeResult(fr, in, res);
    return {};
}

DEFINE_INT_CMP(ICmpEq, ==)
DEFINE_INT_CMP(ICmpNe, !=)
DEFINE_INT_CMP(SCmpGT, >)
DEFINE_INT_CMP(SCmpLT, <)
DEFINE_INT_CMP(SCmpLE, <=)
DEFINE_INT_CMP(SCmpGE, >=)
DEFINE_FLOAT_CMP(FCmpEQ, ==)
DEFINE_FLOAT_CMP(FCmpNE, !=)
DEFINE_FLOAT_CMP(FCmpGT, >)
DEFINE_FLOAT_CMP(FCmpLT, <)
DEFINE_FLOAT_CMP(FCmpLE, <=)
DEFINE_FLOAT_CMP(FCmpGE, >=)

VM::ExecResult VM::handleBr(
    Frame &fr, const Instr &in, const BlockMap &blocks, const BasicBlock *&bb, size_t &ip)
{
    const auto &targetLabel = in.labels[0];
    auto itBlk = blocks.find(targetLabel);
    assert(itBlk != blocks.end() && "invalid block");
    const BasicBlock *target = itBlk->second;
    const auto &args = in.brArgs.empty() ? std::vector<Value>{} : in.brArgs[0];
    for (size_t i = 0; i < args.size() && i < target->params.size(); ++i)
        fr.params[target->params[i].id] = eval(fr, args[i]);
    bb = target;
    ip = 0;
    ExecResult r{};
    r.jumped = true;
    return r;
}

VM::ExecResult VM::handleCBr(
    Frame &fr, const Instr &in, const BlockMap &blocks, const BasicBlock *&bb, size_t &ip)
{
    Slot cond = eval(fr, in.operands[0]);
    size_t idx = cond.i64 ? 0 : 1;
    const auto &targetLabel = in.labels[idx];
    auto itBlk = blocks.find(targetLabel);
    assert(itBlk != blocks.end() && "invalid block");
    const BasicBlock *target = itBlk->second;
    const auto &args = in.brArgs.size() > idx ? in.brArgs[idx] : std::vector<Value>{};
    for (size_t i = 0; i < args.size() && i < target->params.size(); ++i)
        fr.params[target->params[i].id] = eval(fr, args[i]);
    bb = target;
    ip = 0;
    ExecResult r{};
    r.jumped = true;
    return r;
}

VM::ExecResult VM::handleRet(Frame &fr, const Instr &in)
{
    ExecResult r{};
    r.value.i64 = 0;
    if (!in.operands.empty())
        r.value = eval(fr, in.operands[0]);
    r.returned = true;
    return r;
}

VM::ExecResult VM::handleConstStr(Frame &fr, const Instr &in)
{
    Slot res{};
    res.str = strMap[in.operands[0].str];
    storeResult(fr, in, res);
    return {};
}

VM::ExecResult VM::handleCall(Frame &fr, const Instr &in, const BasicBlock *bb)
{
    std::vector<Slot> callArgs;
    for (const auto &op : in.operands)
        callArgs.push_back(eval(fr, op));
    Slot res{};
    auto itFn = fnMap.find(in.callee);
    if (itFn != fnMap.end())
        res = execFunction(*itFn->second, callArgs);
    else
        res = RuntimeBridge::call(in.callee, callArgs, in.loc, fr.func->name, bb->label);
    storeResult(fr, in, res);
    return {};
}

VM::ExecResult VM::handleSitofp(Frame &fr, const Instr &in)
{
    Slot v = eval(fr, in.operands[0]);
    Slot res{};
    res.f64 = static_cast<double>(v.i64);
    storeResult(fr, in, res);
    return {};
}

VM::ExecResult VM::handleFptosi(Frame &fr, const Instr &in)
{
    Slot v = eval(fr, in.operands[0]);
    Slot res{};
    res.i64 = static_cast<int64_t>(v.f64);
    storeResult(fr, in, res);
    return {};
}

VM::ExecResult VM::handleTruncOrZext1(Frame &fr, const Instr &in)
{
    Slot v = eval(fr, in.operands[0]);
    v.i64 &= 1;
    storeResult(fr, in, v);
    return {};
}

VM::ExecResult VM::handleTrap(Frame &fr, const Instr &in, const BasicBlock *bb)
{
    RuntimeBridge::trap("trap", in.loc, fr.func->name, bb->label);
    ExecResult r{};
    r.value.i64 = 0; // unreachable
    r.returned = true;
    return r;
}

VM::ExecResult VM::executeOpcode(Frame &fr,
                                 const Instr &in,
                                 const std::unordered_map<std::string, const BasicBlock *> &blocks,
                                 const BasicBlock *&bb,
                                 size_t &ip)
{
    using Handler = ExecResult (*)(
        VM &, Frame &, const Instr &, const BlockMap &, const BasicBlock *&, size_t &);
    static const std::array<Handler, static_cast<size_t>(Opcode::Trap) + 1> table = []
    {
        std::array<Handler, static_cast<size_t>(Opcode::Trap) + 1> t{};
        t[static_cast<size_t>(Opcode::Alloca)] =
            [](VM &vm, Frame &fr, const Instr &in, const BlockMap &, const BasicBlock *&, size_t &)
        { return vm.handleAlloca(fr, in); };
        t[static_cast<size_t>(Opcode::Load)] =
            [](VM &vm, Frame &fr, const Instr &in, const BlockMap &, const BasicBlock *&, size_t &)
        { return vm.handleLoad(fr, in); };
        t[static_cast<size_t>(Opcode::Store)] = [](VM &vm,
                                                   Frame &fr,
                                                   const Instr &in,
                                                   const BlockMap &,
                                                   const BasicBlock *&b,
                                                   size_t &i)
        { return vm.handleStore(fr, in, b, i); };
        t[static_cast<size_t>(Opcode::Add)] =
            [](VM &vm, Frame &fr, const Instr &in, const BlockMap &, const BasicBlock *&, size_t &)
        { return vm.handleAdd(fr, in); };
        t[static_cast<size_t>(Opcode::Sub)] =
            [](VM &vm, Frame &fr, const Instr &in, const BlockMap &, const BasicBlock *&, size_t &)
        { return vm.handleSub(fr, in); };
        t[static_cast<size_t>(Opcode::Mul)] =
            [](VM &vm, Frame &fr, const Instr &in, const BlockMap &, const BasicBlock *&, size_t &)
        { return vm.handleMul(fr, in); };
        t[static_cast<size_t>(Opcode::FAdd)] =
            [](VM &vm, Frame &fr, const Instr &in, const BlockMap &, const BasicBlock *&, size_t &)
        { return vm.handleFAdd(fr, in); };
        t[static_cast<size_t>(Opcode::FSub)] =
            [](VM &vm, Frame &fr, const Instr &in, const BlockMap &, const BasicBlock *&, size_t &)
        { return vm.handleFSub(fr, in); };
        t[static_cast<size_t>(Opcode::FMul)] =
            [](VM &vm, Frame &fr, const Instr &in, const BlockMap &, const BasicBlock *&, size_t &)
        { return vm.handleFMul(fr, in); };
        t[static_cast<size_t>(Opcode::FDiv)] =
            [](VM &vm, Frame &fr, const Instr &in, const BlockMap &, const BasicBlock *&, size_t &)
        { return vm.handleFDiv(fr, in); };
        t[static_cast<size_t>(Opcode::Xor)] =
            [](VM &vm, Frame &fr, const Instr &in, const BlockMap &, const BasicBlock *&, size_t &)
        { return vm.handleXor(fr, in); };
        t[static_cast<size_t>(Opcode::Shl)] =
            [](VM &vm, Frame &fr, const Instr &in, const BlockMap &, const BasicBlock *&, size_t &)
        { return vm.handleShl(fr, in); };
        t[static_cast<size_t>(Opcode::GEP)] =
            [](VM &vm, Frame &fr, const Instr &in, const BlockMap &, const BasicBlock *&, size_t &)
        { return vm.handleGEP(fr, in); };
        t[static_cast<size_t>(Opcode::ICmpEq)] =
            [](VM &vm, Frame &fr, const Instr &in, const BlockMap &, const BasicBlock *&, size_t &)
        { return vm.handleICmpEq(fr, in); };
        t[static_cast<size_t>(Opcode::ICmpNe)] =
            [](VM &vm, Frame &fr, const Instr &in, const BlockMap &, const BasicBlock *&, size_t &)
        { return vm.handleICmpNe(fr, in); };
        t[static_cast<size_t>(Opcode::SCmpGT)] =
            [](VM &vm, Frame &fr, const Instr &in, const BlockMap &, const BasicBlock *&, size_t &)
        { return vm.handleSCmpGT(fr, in); };
        t[static_cast<size_t>(Opcode::SCmpLT)] =
            [](VM &vm, Frame &fr, const Instr &in, const BlockMap &, const BasicBlock *&, size_t &)
        { return vm.handleSCmpLT(fr, in); };
        t[static_cast<size_t>(Opcode::SCmpLE)] =
            [](VM &vm, Frame &fr, const Instr &in, const BlockMap &, const BasicBlock *&, size_t &)
        { return vm.handleSCmpLE(fr, in); };
        t[static_cast<size_t>(Opcode::SCmpGE)] =
            [](VM &vm, Frame &fr, const Instr &in, const BlockMap &, const BasicBlock *&, size_t &)
        { return vm.handleSCmpGE(fr, in); };
        t[static_cast<size_t>(Opcode::FCmpEQ)] =
            [](VM &vm, Frame &fr, const Instr &in, const BlockMap &, const BasicBlock *&, size_t &)
        { return vm.handleFCmpEQ(fr, in); };
        t[static_cast<size_t>(Opcode::FCmpNE)] =
            [](VM &vm, Frame &fr, const Instr &in, const BlockMap &, const BasicBlock *&, size_t &)
        { return vm.handleFCmpNE(fr, in); };
        t[static_cast<size_t>(Opcode::FCmpGT)] =
            [](VM &vm, Frame &fr, const Instr &in, const BlockMap &, const BasicBlock *&, size_t &)
        { return vm.handleFCmpGT(fr, in); };
        t[static_cast<size_t>(Opcode::FCmpLT)] =
            [](VM &vm, Frame &fr, const Instr &in, const BlockMap &, const BasicBlock *&, size_t &)
        { return vm.handleFCmpLT(fr, in); };
        t[static_cast<size_t>(Opcode::FCmpLE)] =
            [](VM &vm, Frame &fr, const Instr &in, const BlockMap &, const BasicBlock *&, size_t &)
        { return vm.handleFCmpLE(fr, in); };
        t[static_cast<size_t>(Opcode::FCmpGE)] =
            [](VM &vm, Frame &fr, const Instr &in, const BlockMap &, const BasicBlock *&, size_t &)
        { return vm.handleFCmpGE(fr, in); };
        t[static_cast<size_t>(Opcode::Br)] = [](VM &vm,
                                                Frame &fr,
                                                const Instr &in,
                                                const BlockMap &blk,
                                                const BasicBlock *&b,
                                                size_t &i)
        { return vm.handleBr(fr, in, blk, b, i); };
        t[static_cast<size_t>(Opcode::CBr)] = [](VM &vm,
                                                 Frame &fr,
                                                 const Instr &in,
                                                 const BlockMap &blk,
                                                 const BasicBlock *&b,
                                                 size_t &i)
        { return vm.handleCBr(fr, in, blk, b, i); };
        t[static_cast<size_t>(Opcode::Ret)] =
            [](VM &vm, Frame &fr, const Instr &in, const BlockMap &, const BasicBlock *&, size_t &)
        { return vm.handleRet(fr, in); };
        t[static_cast<size_t>(Opcode::ConstStr)] =
            [](VM &vm, Frame &fr, const Instr &in, const BlockMap &, const BasicBlock *&, size_t &)
        { return vm.handleConstStr(fr, in); };
        t[static_cast<size_t>(Opcode::Call)] =
            [](VM &vm, Frame &fr, const Instr &in, const BlockMap &, const BasicBlock *&b, size_t &)
        { return vm.handleCall(fr, in, b); };
        t[static_cast<size_t>(Opcode::Sitofp)] =
            [](VM &vm, Frame &fr, const Instr &in, const BlockMap &, const BasicBlock *&, size_t &)
        { return vm.handleSitofp(fr, in); };
        t[static_cast<size_t>(Opcode::Fptosi)] =
            [](VM &vm, Frame &fr, const Instr &in, const BlockMap &, const BasicBlock *&, size_t &)
        { return vm.handleFptosi(fr, in); };
        t[static_cast<size_t>(Opcode::Trunc1)] =
            [](VM &vm, Frame &fr, const Instr &in, const BlockMap &, const BasicBlock *&, size_t &)
        { return vm.handleTruncOrZext1(fr, in); };
        t[static_cast<size_t>(Opcode::Zext1)] =
            [](VM &vm, Frame &fr, const Instr &in, const BlockMap &, const BasicBlock *&, size_t &)
        { return vm.handleTruncOrZext1(fr, in); };
        t[static_cast<size_t>(Opcode::Trap)] =
            [](VM &vm, Frame &fr, const Instr &in, const BlockMap &, const BasicBlock *&b, size_t &)
        { return vm.handleTrap(fr, in, b); };
        return t;
    }();

    Handler handler = table[static_cast<size_t>(in.op)];
    if (!handler)
    {
        assert(false && "unimplemented opcode");
        return {};
    }
    return handler(*this, fr, in, blocks, bb, ip);
}

VM::ExecState VM::prepareExecution(const Function &fn, const std::vector<Slot> &args)
{
    ExecState st;
    st.fr = setupFrame(fn, args, st.blocks, st.bb);
    debug.resetLastHit();
    st.ip = 0;
    st.skipBreakOnce = false;
    return st;
}

std::optional<Slot> VM::processDebugControl(ExecState &st, const Instr *in, bool postExec)
{
    if (!postExec)
    {
        if (maxSteps && instrCount >= maxSteps)
        {
            std::cerr << "VM: step limit exceeded (" << maxSteps << "); aborting.\n";
            Slot s{};
            s.i64 = 1;
            return s;
        }
        if (st.ip == 0 && stepBudget == 0 && !st.skipBreakOnce)
            if (auto br = handleDebugBreak(st.fr, *st.bb, st.ip, st.skipBreakOnce, nullptr))
                return br;
        st.skipBreakOnce = false;
        if (in)
            if (auto br = handleDebugBreak(st.fr, *st.bb, st.ip, st.skipBreakOnce, in))
                return br;
        return std::nullopt;
    }
    if (stepBudget > 0)
    {
        --stepBudget;
        if (stepBudget == 0)
        {
            std::cerr << "[BREAK] fn=@" << st.fr.func->name << " blk=" << st.bb->label
                      << " reason=step\n";
            if (!script || script->empty())
            {
                Slot s{};
                s.i64 = 10;
                return s;
            }
            auto act = script->nextAction();
            if (act.kind == DebugActionKind::Step)
                stepBudget = act.count;
            st.skipBreakOnce = true;
        }
    }
    return std::nullopt;
}

Slot VM::runFunctionLoop(ExecState &st)
{
    while (st.bb && st.ip < st.bb->instructions.size())
    {
        const Instr &in = st.bb->instructions[st.ip];
        if (auto br = processDebugControl(st, &in, false))
            return *br;
        tracer.onStep(in, st.fr);
        ++instrCount;
        auto res = executeOpcode(st.fr, in, st.blocks, st.bb, st.ip);
        if (res.returned)
            return res.value;
        if (res.jumped)
            debug.resetLastHit();
        else
            ++st.ip;
        if (auto br = processDebugControl(st, nullptr, true))
            return *br;
    }
    Slot s{};
    s.i64 = 0;
    return s;
}

Slot VM::execFunction(const Function &fn, const std::vector<Slot> &args)
{
    auto st = prepareExecution(fn, args);
    return runFunctionLoop(st);
}

} // namespace il::vm
