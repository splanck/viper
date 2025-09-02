// File: src/il/transform/Peephole.cpp
// Purpose: Implements local IL peephole optimizations.
// Key invariants: Transformations preserve program semantics.
// Ownership/Lifetime: Operates in place on the module.
// Links: docs/class-catalog.md

#include "il/transform/Peephole.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"

using namespace il::core;

namespace il::transform
{

namespace
{

static bool isConstInt(const Value &v, long long &out)
{
    if (v.kind == Value::Kind::ConstInt)
    {
        out = v.i64;
        return true;
    }
    return false;
}

static bool isConstEq(const Value &v, long long target)
{
    long long c;
    return isConstInt(v, c) && c == target;
}

static size_t countUses(const Function &f, unsigned id)
{
    size_t uses = 0;
    for (const auto &b : f.blocks)
        for (const auto &in : b.instructions)
            for (const auto &op : in.operands)
                if (op.kind == Value::Kind::Temp && op.id == id)
                    ++uses;
    return uses;
}

static void replaceAll(Function &f, unsigned id, const Value &v)
{
    for (auto &b : f.blocks)
        for (auto &in : b.instructions)
            for (auto &op : in.operands)
                if (op.kind == Value::Kind::Temp && op.id == id)
                    op = v;
}

} // namespace

void peephole(Module &m)
{
    for (auto &f : m.functions)
    {
        for (auto &b : f.blocks)
        {
            for (size_t i = 0; i < b.instructions.size(); ++i)
            {
                Instr &in = b.instructions[i];
                if (in.op == Opcode::CBr)
                {
                    if (in.labels.size() == 2 && in.labels[0] == in.labels[1])
                    {
                        in.op = Opcode::Br;
                        in.labels = {in.labels[0]};
                        in.operands.clear();
                        continue;
                    }
                    long long v;
                    bool known = false;
                    size_t defIdx = static_cast<size_t>(-1);
                    size_t uses = 0;
                    if (isConstInt(in.operands[0], v))
                    {
                        known = true;
                    }
                    else if (in.operands[0].kind == Value::Kind::Temp)
                    {
                        unsigned id = in.operands[0].id;
                        uses = countUses(f, id);
                        for (size_t j = 0; j < i; ++j)
                        {
                            Instr &def = b.instructions[j];
                            if (def.result && *def.result == id && def.operands.size() == 2)
                            {
                                long long l, r;
                                if (isConstInt(def.operands[0], l) &&
                                    isConstInt(def.operands[1], r))
                                {
                                    switch (def.op)
                                    {
                                        case Opcode::ICmpEq:
                                            v = (l == r);
                                            known = true;
                                            break;
                                        case Opcode::ICmpNe:
                                            v = (l != r);
                                            known = true;
                                            break;
                                        case Opcode::SCmpLT:
                                            v = (l < r);
                                            known = true;
                                            break;
                                        case Opcode::SCmpLE:
                                            v = (l <= r);
                                            known = true;
                                            break;
                                        case Opcode::SCmpGT:
                                            v = (l > r);
                                            known = true;
                                            break;
                                        case Opcode::SCmpGE:
                                            v = (l >= r);
                                            known = true;
                                            break;
                                        default:
                                            break;
                                    }
                                    if (known)
                                        defIdx = j;
                                }
                            }
                            if (known)
                                break;
                        }
                    }
                    if (known)
                    {
                        in.op = Opcode::Br;
                        in.labels = {v ? in.labels[0] : in.labels[1]};
                        in.operands.clear();
                        if (defIdx != static_cast<size_t>(-1) && uses == 1)
                        {
                            b.instructions.erase(b.instructions.begin() + defIdx);
                            --i;
                        }
                    }
                    continue;
                }
                if (!in.result || in.operands.size() != 2)
                    continue;
                Value repl{};
                bool match = false;
                switch (in.op)
                {
                    case Opcode::Add:
                        if (isConstEq(in.operands[0], 0))
                        {
                            repl = in.operands[1];
                            match = true;
                        }
                        else if (isConstEq(in.operands[1], 0))
                        {
                            repl = in.operands[0];
                            match = true;
                        }
                        break;
                    case Opcode::Sub:
                        if (isConstEq(in.operands[1], 0))
                        {
                            repl = in.operands[0];
                            match = true;
                        }
                        break;
                    case Opcode::Mul:
                        if (isConstEq(in.operands[0], 1))
                        {
                            repl = in.operands[1];
                            match = true;
                        }
                        else if (isConstEq(in.operands[1], 1))
                        {
                            repl = in.operands[0];
                            match = true;
                        }
                        break;
                    case Opcode::And:
                        if (isConstEq(in.operands[0], -1))
                        {
                            repl = in.operands[1];
                            match = true;
                        }
                        else if (isConstEq(in.operands[1], -1))
                        {
                            repl = in.operands[0];
                            match = true;
                        }
                        break;
                    case Opcode::Or:
                        if (isConstEq(in.operands[0], 0))
                        {
                            repl = in.operands[1];
                            match = true;
                        }
                        else if (isConstEq(in.operands[1], 0))
                        {
                            repl = in.operands[0];
                            match = true;
                        }
                        break;
                    case Opcode::Xor:
                        if (isConstEq(in.operands[0], 0))
                        {
                            repl = in.operands[1];
                            match = true;
                        }
                        else if (isConstEq(in.operands[1], 0))
                        {
                            repl = in.operands[0];
                            match = true;
                        }
                        break;
                    case Opcode::Shl:
                    case Opcode::LShr:
                    case Opcode::AShr:
                        if (isConstEq(in.operands[1], 0))
                        {
                            repl = in.operands[0];
                            match = true;
                        }
                        break;
                    default:
                        break;
                }
                if (match)
                {
                    replaceAll(f, *in.result, repl);
                    b.instructions.erase(b.instructions.begin() + i);
                    --i;
                }
            }
        }
    }
}

} // namespace il::transform
