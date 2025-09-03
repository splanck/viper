// File: src/il/transform/ConstFold.cpp
// Purpose: Implements constant folding for simple IL integer operations and
//          selected math intrinsics.
// Key invariants: Uses wraparound semantics for 64-bit integer arithmetic and
//                 follows C math library semantics for floats.
// Ownership/Lifetime: Operates in place on the module.
// Links: docs/class-catalog.md

#include "il/transform/ConstFold.hpp"
#include "il/core/Instr.hpp"
#include <cmath>
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

static bool isConstFloat(const Value &v, double &out)
{
    if (v.kind == Value::Kind::ConstFloat)
    {
        out = v.f64;
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

                if (in.op == Opcode::Call)
                {
                    if (!in.result)
                        continue;
                    Value v;
                    bool folded = false;
                    const std::string &c = in.callee;
                    if (c == "rt_abs_i64" && in.operands.size() == 1)
                    {
                        long long x;
                        if (isConstInt(in.operands[0], x))
                        {
                            v = Value::constInt(x < 0 ? -x : x);
                            folded = true;
                        }
                    }
                    else if (c == "rt_abs_f64" && in.operands.size() == 1)
                    {
                        double x;
                        if (isConstFloat(in.operands[0], x))
                        {
                            v = Value::constFloat(std::fabs(x));
                            folded = true;
                        }
                    }
                    else if (c == "rt_floor" && in.operands.size() == 1)
                    {
                        double x;
                        if (isConstFloat(in.operands[0], x))
                        {
                            v = Value::constFloat(std::floor(x));
                            folded = true;
                        }
                    }
                    else if (c == "rt_ceil" && in.operands.size() == 1)
                    {
                        double x;
                        if (isConstFloat(in.operands[0], x))
                        {
                            v = Value::constFloat(std::ceil(x));
                            folded = true;
                        }
                    }
                    else if (c == "rt_sqrt" && in.operands.size() == 1)
                    {
                        double x;
                        if (isConstFloat(in.operands[0], x) && x >= 0.0)
                        {
                            v = Value::constFloat(std::sqrt(x));
                            folded = true;
                        }
                    }
                    else if (c == "rt_pow" && in.operands.size() == 2)
                    {
                        double base;
                        if (isConstFloat(in.operands[0], base))
                        {
                            long long ei;
                            double ef;
                            if (isConstInt(in.operands[1], ei))
                            {
                                if (std::llabs(ei) <= 16)
                                {
                                    v = Value::constFloat(std::pow(base, static_cast<double>(ei)));
                                    folded = true;
                                }
                            }
                            else if (isConstFloat(in.operands[1], ef))
                            {
                                double t = std::trunc(ef);
                                long long ei2 = static_cast<long long>(t);
                                if (ef == t && std::llabs(ei2) <= 16)
                                {
                                    v = Value::constFloat(std::pow(base, ef));
                                    folded = true;
                                }
                            }
                        }
                    }
                    else if (c == "rt_sin" && in.operands.size() == 1)
                    {
                        double x;
                        if (isConstFloat(in.operands[0], x) && x == 0.0)
                        {
                            v = Value::constFloat(0.0);
                            folded = true;
                        }
                    }
                    else if (c == "rt_cos" && in.operands.size() == 1)
                    {
                        double x;
                        if (isConstFloat(in.operands[0], x) && x == 0.0)
                        {
                            v = Value::constFloat(1.0);
                            folded = true;
                        }
                    }

                    if (folded)
                    {
                        replaceAll(f, *in.result, v);
                        b.instructions.erase(b.instructions.begin() + i);
                        --i;
                    }

                    continue;
                }

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
