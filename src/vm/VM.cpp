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
#include <cassert>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <utility>

using namespace il::core;

namespace il::vm
{

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

VM::ExecResult VM::executeOpcode(Frame &fr,
                                 const Instr &in,
                                 const std::unordered_map<std::string, const BasicBlock *> &blocks,
                                 const BasicBlock *&bb,
                                 size_t &ip)
{
    ExecResult r{};
    switch (in.op)
    {
        case Opcode::Alloca:
        {
            size_t sz = (size_t)eval(fr, in.operands[0]).i64;
            size_t addr = fr.sp;
            assert(addr + sz <= fr.stack.size());
            std::memset(fr.stack.data() + addr, 0, sz);
            Slot res{};
            res.ptr = fr.stack.data() + addr;
            fr.sp += sz;
            if (in.result)
            {
                if (fr.regs.size() <= *in.result)
                    fr.regs.resize(*in.result + 1);
                fr.regs[*in.result] = res;
            }
            break;
        }
        case Opcode::Load:
        {
            void *ptr = eval(fr, in.operands[0]).ptr;
            assert(ptr && "null load");
            Slot res{};
            if (in.type.kind == Type::Kind::I64)
                res.i64 = *reinterpret_cast<int64_t *>(ptr);
            else if (in.type.kind == Type::Kind::F64)
                res.f64 = *reinterpret_cast<double *>(ptr);
            else if (in.type.kind == Type::Kind::Str)
                res.str = *reinterpret_cast<rt_str *>(ptr);
            else if (in.type.kind == Type::Kind::Ptr)
                res.ptr = *reinterpret_cast<void **>(ptr);
            if (in.result)
            {
                if (fr.regs.size() <= *in.result)
                    fr.regs.resize(*in.result + 1);
                fr.regs[*in.result] = res;
            }
            break;
        }
        case Opcode::Store:
        {
            void *ptr = eval(fr, in.operands[0]).ptr;
            assert(ptr && "null store");
            Slot val = eval(fr, in.operands[1]);
            if (in.type.kind == Type::Kind::I64)
                *reinterpret_cast<int64_t *>(ptr) = val.i64;
            else if (in.type.kind == Type::Kind::F64)
                *reinterpret_cast<double *>(ptr) = val.f64;
            else if (in.type.kind == Type::Kind::Str)
                *reinterpret_cast<rt_str *>(ptr) = val.str;
            else if (in.type.kind == Type::Kind::Ptr)
                *reinterpret_cast<void **>(ptr) = val.ptr;
            if (in.operands[0].kind == Value::Kind::Temp)
            {
                unsigned id = in.operands[0].id;
                if (id < fr.func->valueNames.size())
                {
                    const std::string &nm = fr.func->valueNames[id];
                    if (!nm.empty())
                        debug.onStore(
                            nm, in.type.kind, val.i64, val.f64, fr.func->name, bb->label, ip);
                }
            }
            break;
        }
        case Opcode::Add:
        {
            Slot a = eval(fr, in.operands[0]);
            Slot b = eval(fr, in.operands[1]);
            Slot res{};
            res.i64 = a.i64 + b.i64;
            if (in.result)
            {
                if (fr.regs.size() <= *in.result)
                    fr.regs.resize(*in.result + 1);
                fr.regs[*in.result] = res;
            }
            break;
        }
        case Opcode::Sub:
        {
            Slot a = eval(fr, in.operands[0]);
            Slot b = eval(fr, in.operands[1]);
            Slot res{};
            res.i64 = a.i64 - b.i64;
            if (in.result)
            {
                if (fr.regs.size() <= *in.result)
                    fr.regs.resize(*in.result + 1);
                fr.regs[*in.result] = res;
            }
            break;
        }
        case Opcode::Mul:
        {
            Slot a = eval(fr, in.operands[0]);
            Slot b = eval(fr, in.operands[1]);
            Slot res{};
            res.i64 = a.i64 * b.i64;
            if (in.result)
            {
                if (fr.regs.size() <= *in.result)
                    fr.regs.resize(*in.result + 1);
                fr.regs[*in.result] = res;
            }
            break;
        }
        case Opcode::FAdd:
        {
            Slot a = eval(fr, in.operands[0]);
            Slot b = eval(fr, in.operands[1]);
            Slot res{};
            res.f64 = a.f64 + b.f64;
            if (in.result)
            {
                if (fr.regs.size() <= *in.result)
                    fr.regs.resize(*in.result + 1);
                fr.regs[*in.result] = res;
            }
            break;
        }
        case Opcode::FSub:
        {
            Slot a = eval(fr, in.operands[0]);
            Slot b = eval(fr, in.operands[1]);
            Slot res{};
            res.f64 = a.f64 - b.f64;
            if (in.result)
            {
                if (fr.regs.size() <= *in.result)
                    fr.regs.resize(*in.result + 1);
                fr.regs[*in.result] = res;
            }
            break;
        }
        case Opcode::FMul:
        {
            Slot a = eval(fr, in.operands[0]);
            Slot b = eval(fr, in.operands[1]);
            Slot res{};
            res.f64 = a.f64 * b.f64;
            if (in.result)
            {
                if (fr.regs.size() <= *in.result)
                    fr.regs.resize(*in.result + 1);
                fr.regs[*in.result] = res;
            }
            break;
        }
        case Opcode::FDiv:
        {
            Slot a = eval(fr, in.operands[0]);
            Slot b = eval(fr, in.operands[1]);
            Slot res{};
            res.f64 = a.f64 / b.f64;
            if (in.result)
            {
                if (fr.regs.size() <= *in.result)
                    fr.regs.resize(*in.result + 1);
                fr.regs[*in.result] = res;
            }
            break;
        }
        case Opcode::Xor:
        {
            Slot a = eval(fr, in.operands[0]);
            Slot b = eval(fr, in.operands[1]);
            Slot res{};
            res.i64 = a.i64 ^ b.i64;
            if (in.result)
            {
                if (fr.regs.size() <= *in.result)
                    fr.regs.resize(*in.result + 1);
                fr.regs[*in.result] = res;
            }
            break;
        }
        case Opcode::Shl:
        {
            Slot a = eval(fr, in.operands[0]);
            Slot b = eval(fr, in.operands[1]);
            Slot res{};
            res.i64 = a.i64 << b.i64;
            if (in.result)
            {
                if (fr.regs.size() <= *in.result)
                    fr.regs.resize(*in.result + 1);
                fr.regs[*in.result] = res;
            }
            break;
        }
        case Opcode::GEP:
        {
            Slot base = eval(fr, in.operands[0]);
            Slot off = eval(fr, in.operands[1]);
            Slot res{};
            res.ptr = static_cast<char *>(base.ptr) + off.i64;
            if (in.result)
            {
                if (fr.regs.size() <= *in.result)
                    fr.regs.resize(*in.result + 1);
                fr.regs[*in.result] = res;
            }
            break;
        }
        case Opcode::ICmpEq:
        {
            Slot a = eval(fr, in.operands[0]);
            Slot b = eval(fr, in.operands[1]);
            Slot res{};
            res.i64 = (a.i64 == b.i64) ? 1 : 0;
            if (in.result)
            {
                if (fr.regs.size() <= *in.result)
                    fr.regs.resize(*in.result + 1);
                fr.regs[*in.result] = res;
            }
            break;
        }
        case Opcode::ICmpNe:
        {
            Slot a = eval(fr, in.operands[0]);
            Slot b = eval(fr, in.operands[1]);
            Slot res{};
            res.i64 = (a.i64 != b.i64) ? 1 : 0;
            if (in.result)
            {
                if (fr.regs.size() <= *in.result)
                    fr.regs.resize(*in.result + 1);
                fr.regs[*in.result] = res;
            }
            break;
        }
        case Opcode::SCmpGT:
        {
            Slot a = eval(fr, in.operands[0]);
            Slot b = eval(fr, in.operands[1]);
            Slot res{};
            res.i64 = (a.i64 > b.i64) ? 1 : 0;
            if (in.result)
            {
                if (fr.regs.size() <= *in.result)
                    fr.regs.resize(*in.result + 1);
                fr.regs[*in.result] = res;
            }
            break;
        }
        case Opcode::SCmpLT:
        {
            Slot a = eval(fr, in.operands[0]);
            Slot b = eval(fr, in.operands[1]);
            Slot res{};
            res.i64 = (a.i64 < b.i64) ? 1 : 0;
            if (in.result)
            {
                if (fr.regs.size() <= *in.result)
                    fr.regs.resize(*in.result + 1);
                fr.regs[*in.result] = res;
            }
            break;
        }
        case Opcode::SCmpLE:
        {
            Slot a = eval(fr, in.operands[0]);
            Slot b = eval(fr, in.operands[1]);
            Slot res{};
            res.i64 = (a.i64 <= b.i64) ? 1 : 0;
            if (in.result)
            {
                if (fr.regs.size() <= *in.result)
                    fr.regs.resize(*in.result + 1);
                fr.regs[*in.result] = res;
            }
            break;
        }
        case Opcode::SCmpGE:
        {
            Slot a = eval(fr, in.operands[0]);
            Slot b = eval(fr, in.operands[1]);
            Slot res{};
            res.i64 = (a.i64 >= b.i64) ? 1 : 0;
            if (in.result)
            {
                if (fr.regs.size() <= *in.result)
                    fr.regs.resize(*in.result + 1);
                fr.regs[*in.result] = res;
            }
            break;
        }
        case Opcode::FCmpEQ:
        {
            Slot a = eval(fr, in.operands[0]);
            Slot b = eval(fr, in.operands[1]);
            Slot res{};
            res.i64 = (a.f64 == b.f64) ? 1 : 0;
            if (in.result)
            {
                if (fr.regs.size() <= *in.result)
                    fr.regs.resize(*in.result + 1);
                fr.regs[*in.result] = res;
            }
            break;
        }
        case Opcode::FCmpNE:
        {
            Slot a = eval(fr, in.operands[0]);
            Slot b = eval(fr, in.operands[1]);
            Slot res{};
            res.i64 = (a.f64 != b.f64) ? 1 : 0;
            if (in.result)
            {
                if (fr.regs.size() <= *in.result)
                    fr.regs.resize(*in.result + 1);
                fr.regs[*in.result] = res;
            }
            break;
        }
        case Opcode::FCmpGT:
        {
            Slot a = eval(fr, in.operands[0]);
            Slot b = eval(fr, in.operands[1]);
            Slot res{};
            res.i64 = (a.f64 > b.f64) ? 1 : 0;
            if (in.result)
            {
                if (fr.regs.size() <= *in.result)
                    fr.regs.resize(*in.result + 1);
                fr.regs[*in.result] = res;
            }
            break;
        }
        case Opcode::FCmpLT:
        {
            Slot a = eval(fr, in.operands[0]);
            Slot b = eval(fr, in.operands[1]);
            Slot res{};
            res.i64 = (a.f64 < b.f64) ? 1 : 0;
            if (in.result)
            {
                if (fr.regs.size() <= *in.result)
                    fr.regs.resize(*in.result + 1);
                fr.regs[*in.result] = res;
            }
            break;
        }
        case Opcode::FCmpLE:
        {
            Slot a = eval(fr, in.operands[0]);
            Slot b = eval(fr, in.operands[1]);
            Slot res{};
            res.i64 = (a.f64 <= b.f64) ? 1 : 0;
            if (in.result)
            {
                if (fr.regs.size() <= *in.result)
                    fr.regs.resize(*in.result + 1);
                fr.regs[*in.result] = res;
            }
            break;
        }
        case Opcode::FCmpGE:
        {
            Slot a = eval(fr, in.operands[0]);
            Slot b = eval(fr, in.operands[1]);
            Slot res{};
            res.i64 = (a.f64 >= b.f64) ? 1 : 0;
            if (in.result)
            {
                if (fr.regs.size() <= *in.result)
                    fr.regs.resize(*in.result + 1);
                fr.regs[*in.result] = res;
            }
            break;
        }
        case Opcode::Br:
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
            r.jumped = true;
            break;
        }
        case Opcode::CBr:
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
            r.jumped = true;
            break;
        }
        case Opcode::Ret:
        {
            r.value.i64 = 0;
            if (!in.operands.empty())
                r.value = eval(fr, in.operands[0]);
            r.returned = true;
            break;
        }
        case Opcode::ConstStr:
        {
            Slot res{};
            res.str = strMap[in.operands[0].str];
            if (in.result)
            {
                if (fr.regs.size() <= *in.result)
                    fr.regs.resize(*in.result + 1);
                fr.regs[*in.result] = res;
            }
            break;
        }
        case Opcode::Call:
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
            if (in.result)
            {
                if (fr.regs.size() <= *in.result)
                    fr.regs.resize(*in.result + 1);
                fr.regs[*in.result] = res;
            }
            break;
        }
        case Opcode::Sitofp:
        {
            Slot v = eval(fr, in.operands[0]);
            Slot res{};
            res.f64 = static_cast<double>(v.i64);
            if (in.result)
            {
                if (fr.regs.size() <= *in.result)
                    fr.regs.resize(*in.result + 1);
                fr.regs[*in.result] = res;
            }
            break;
        }
        case Opcode::Fptosi:
        {
            Slot v = eval(fr, in.operands[0]);
            Slot res{};
            res.i64 = static_cast<int64_t>(v.f64);
            if (in.result)
            {
                if (fr.regs.size() <= *in.result)
                    fr.regs.resize(*in.result + 1);
                fr.regs[*in.result] = res;
            }
            break;
        }
        case Opcode::Trunc1:
        case Opcode::Zext1:
        {
            Slot v = eval(fr, in.operands[0]);
            v.i64 &= 1;
            if (in.result)
            {
                if (fr.regs.size() <= *in.result)
                    fr.regs.resize(*in.result + 1);
                fr.regs[*in.result] = v;
            }
            break;
        }
        case Opcode::Trap:
        {
            RuntimeBridge::trap("trap", in.loc, fr.func->name, bb->label);
            r.value.i64 = 0; // unreachable
            r.returned = true;
            break;
        }
        default:
            assert(false && "unimplemented opcode");
    }
    return r;
}

Slot VM::execFunction(const Function &fn, const std::vector<Slot> &args)
{
    std::unordered_map<std::string, const BasicBlock *> blocks;
    const BasicBlock *bb = nullptr;
    Frame fr = setupFrame(fn, args, blocks, bb);
    debug.resetLastHit();
    size_t ip = 0;
    bool skipBreakOnce = false;
    while (bb && ip < bb->instructions.size())
    {
        if (maxSteps && instrCount >= maxSteps)
        {
            std::cerr << "VM: step limit exceeded (" << maxSteps << "); aborting.\n";
            Slot s{};
            s.i64 = 1;
            return s;
        }
        if (ip == 0 && stepBudget == 0 && !skipBreakOnce)
            if (auto br = handleDebugBreak(fr, *bb, ip, skipBreakOnce, nullptr))
                return *br;
        skipBreakOnce = false;
        const Instr &in = bb->instructions[ip];
        if (auto br = handleDebugBreak(fr, *bb, ip, skipBreakOnce, &in))
            return *br;
        tracer.onStep(in, fr);
        ++instrCount;
        auto res = executeOpcode(fr, in, blocks, bb, ip);
        if (res.returned)
            return res.value;
        if (res.jumped)
            debug.resetLastHit();
        else
            ++ip;
        if (stepBudget > 0)
        {
            --stepBudget;
            if (stepBudget == 0)
            {
                std::cerr << "[BREAK] fn=@" << fr.func->name << " blk=" << bb->label
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
                skipBreakOnce = true;
            }
        }
    }
    Slot s{};
    s.i64 = 0;
    return s;
}

} // namespace il::vm
