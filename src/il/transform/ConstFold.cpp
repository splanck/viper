// File: src/il/transform/ConstFold.cpp
// Purpose: Implements constant folding for simple IL integer operations.
// Key invariants: Uses wraparound semantics for 64-bit integer arithmetic.
// Ownership/Lifetime: Operates in place on the module.
// Links: docs/class-catalog.md

#include "il/transform/ConstFold.hpp"
#include "il/core/Instr.hpp"
#include <cstdint>

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

static long long wrapAdd(long long a, long long b)
{
    return static_cast<long long>(static_cast<uint64_t>(a) + static_cast<uint64_t>(b));
}

static long long wrapSub(long long a, long long b)
{
    return static_cast<long long>(static_cast<uint64_t>(a) - static_cast<uint64_t>(b));
}

static long long wrapMul(long long a, long long b)
{
    return static_cast<long long>(static_cast<uint64_t>(a) * static_cast<uint64_t>(b));
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

void constFold(Module &m)
{
    for (auto &f : m.functions)
    {
        for (auto &b : f.blocks)
        {
            for (size_t i = 0; i < b.instructions.size(); ++i)
            {
                Instr &in = b.instructions[i];
                if (!in.result || in.operands.size() != 2)
                    continue;
                long long lhs, rhs;
                if (!isConstInt(in.operands[0], lhs) || !isConstInt(in.operands[1], rhs))
                    continue;
                long long res = 0;
                bool folded = true;
                switch (in.op)
                {
                    case Opcode::Add:
                        res = wrapAdd(lhs, rhs);
                        break;
                    case Opcode::Sub:
                        res = wrapSub(lhs, rhs);
                        break;
                    case Opcode::Mul:
                        res = wrapMul(lhs, rhs);
                        break;
                    case Opcode::And:
                        res = lhs & rhs;
                        break;
                    case Opcode::Or:
                        res = lhs | rhs;
                        break;
                    case Opcode::Xor:
                        res = lhs ^ rhs;
                        break;
                    default:
                        folded = false;
                        break;
                }
                if (folded)
                {
                    replaceAll(f, *in.result, Value::constInt(res));
                    b.instructions.erase(b.instructions.begin() + i);
                    --i;
                }
            }
        }
    }
}

} // namespace il::transform
