// File: src/vm/VM.cpp
// Purpose: Implements stack-based virtual machine for IL.
// Key invariants: None.
// Ownership/Lifetime: VM references module owned externally.
// Links: docs/il-spec.md

#include "vm/VM.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Opcode.hpp"
#include "vm/RuntimeBridge.hpp"
#include <cassert>
#include <cstring>
#include <iostream>

using namespace il::core;

namespace il::vm
{

VM::VM(const Module &m, bool tr, uint64_t ms) : mod(m), trace(tr), maxSteps(ms)
{
    for (const auto &f : m.functions)
        fnMap[f.name] = &f;
    for (const auto &g : m.globals)
        strMap[g.name] = rt_const_cstr(g.init.c_str());
}

int64_t VM::run()
{
    auto it = fnMap.find("main");
    assert(it != fnMap.end());
    return execFunction(*it->second);
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

int64_t VM::execFunction(const Function &fn)
{
    Frame fr;
    fr.func = &fn;
    fr.regs.resize(64);
    std::unordered_map<std::string, const BasicBlock *> blocks;
    for (const auto &b : fn.blocks)
        blocks[b.label] = &b;
    const BasicBlock *bb = fn.blocks.empty() ? nullptr : &fn.blocks.front();
    size_t ip = 0;
    while (bb && ip < bb->instructions.size())
    {
        if (maxSteps && steps >= maxSteps)
        {
            std::cerr << "VM: step limit exceeded (" << maxSteps << "); aborting.\n";
            return 1;
        }
        const Instr &in = bb->instructions[ip];
        if (trace)
            std::cerr << fn.name << ":" << bb->label << ":" << toString(in.op) << "\n";
        ++steps;
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
                else if (in.type.kind == Type::Kind::Str)
                    *reinterpret_cast<rt_str *>(ptr) = val.str;
                else if (in.type.kind == Type::Kind::Ptr)
                    *reinterpret_cast<void **>(ptr) = val.ptr;
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
            case Opcode::CBr:
            {
                Slot c = eval(fr, in.operands[0]);
                const std::string &lbl = c.i64 ? in.labels[0] : in.labels[1];
                bb = blocks[lbl];
                ip = 0;
                continue;
            }
            case Opcode::Br:
            {
                bb = blocks[in.labels[0]];
                ip = 0;
                continue;
            }
            case Opcode::Ret:
            {
                if (!in.operands.empty())
                    return eval(fr, in.operands[0]).i64;
                return 0;
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
                std::vector<Slot> args;
                for (const auto &op : in.operands)
                    args.push_back(eval(fr, op));
                Slot res = RuntimeBridge::call(in.callee, args, in.loc, fr.func->name, bb->label);
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
                return 0; // unreachable
            }
            default:
                assert(false && "unimplemented opcode");
        }
        ++ip;
    }
    return 0;
}

} // namespace il::vm
