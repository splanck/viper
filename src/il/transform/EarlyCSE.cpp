//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements a simple within-block common subexpression elimination. Only
// handles a subset of pure opcodes (integer/float arithmetic, bitwise, compares)
// and avoids memory operations, control flow, and calls. Commutative ops are
// normalized to improve hit rate.
//
//===----------------------------------------------------------------------===//

#include "il/transform/EarlyCSE.hpp"

#include "il/core/Instr.hpp"
#include "il/core/Opcode.hpp"
#include "il/core/Type.hpp"

#include <tuple>
#include <unordered_map>

using namespace il::core;

namespace il::transform
{

namespace
{
inline bool isPure(Opcode op)
{
    switch (op)
    {
        case Opcode::Add:
        case Opcode::Sub:
        case Opcode::Mul:
        case Opcode::And:
        case Opcode::Or:
        case Opcode::Xor:
        case Opcode::ICmpEq:
        case Opcode::ICmpNe:
        case Opcode::SCmpLT:
        case Opcode::SCmpLE:
        case Opcode::SCmpGT:
        case Opcode::SCmpGE:
        case Opcode::FAdd:
        case Opcode::FSub:
        case Opcode::FMul:
        case Opcode::FCmpEQ:
        case Opcode::FCmpNE:
        case Opcode::FCmpLT:
        case Opcode::FCmpLE:
        case Opcode::FCmpGT:
        case Opcode::FCmpGE:
            return true;
        default:
            return false;
    }
}

inline bool isCommutative(Opcode op)
{
    switch (op)
    {
        case Opcode::Add:
        case Opcode::Mul:
        case Opcode::And:
        case Opcode::Or:
        case Opcode::Xor:
        case Opcode::ICmpEq:
        case Opcode::ICmpNe:
        case Opcode::FAdd:
        case Opcode::FMul:
        case Opcode::FCmpEQ:
        case Opcode::FCmpNE:
            return true;
        default:
            return false;
    }
}

struct Key
{
    Opcode op;
    Type type;
    Value a;
    Value b;

    bool operator==(const Key &o) const noexcept
    {
        if (op != o.op)
            return false;
        if (type.kind != o.type.kind)
            return false;
        auto eqVal = [](const Value &x, const Value &y)
        {
            if (x.kind != y.kind)
                return false;
            using K = Value::Kind;
            switch (x.kind)
            {
                case K::Temp:
                    return x.id == y.id;
                case K::ConstInt:
                    return x.i64 == y.i64 && x.isBool == y.isBool;
                case K::ConstFloat:
                    return x.f64 == y.f64;
                case K::ConstStr:
                case K::GlobalAddr:
                    return x.str == y.str;
                case K::NullPtr:
                    return true;
            }
            return false;
        };
        return eqVal(a, o.a) && eqVal(b, o.b);
    }
};

struct KeyHash
{
    size_t operator()(const Key &k) const noexcept
    {
        auto h = static_cast<size_t>(k.op) * 1469598103934665603ULL ^ static_cast<size_t>(k.type.kind);
        auto hval = [](const Value &v)
        {
            size_t h0 = static_cast<size_t>(v.kind);
            switch (v.kind)
            {
                case Value::Kind::Temp:
                    h0 ^= static_cast<size_t>(v.id) + 0x9e3779b97f4a7c15ULL;
                    break;
                case Value::Kind::ConstInt:
                    h0 ^= static_cast<size_t>(v.i64) + (v.isBool ? 1ULL : 0ULL);
                    break;
                case Value::Kind::ConstFloat:
                {
                    // reinterpret bits
                    union
                    {
                        double d;
                        unsigned long long u;
                    } u{};
                    u.d = v.f64;
                    h0 ^= static_cast<size_t>(u.u);
                    break;
                }
                case Value::Kind::ConstStr:
                case Value::Kind::GlobalAddr:
                    h0 ^= std::hash<std::string>{}(v.str);
                    break;
                case Value::Kind::NullPtr:
                    h0 ^= 0xabcdefULL;
                    break;
            }
            return h0;
        };
        h ^= (hval(k.a) << 1);
        h ^= (hval(k.b) << 3);
        return h;
    }
};

inline void normalizeCommutative(Opcode op, Value &a, Value &b)
{
    if (!isCommutative(op))
        return;
    // Place constants last to prefer temp-first ordering; tie-break by id
    auto rank = [](const Value &v)
    {
        return v.kind == Value::Kind::Temp ? (1000000 + static_cast<int>(v.id)) : 0;
    };
    if (rank(a) < rank(b))
        std::swap(a, b);
}

} // namespace

bool runEarlyCSE(Function &F)
{
    bool changed = false;
    for (auto &B : F.blocks)
    {
        std::unordered_map<Key, unsigned, KeyHash> table;
        for (std::size_t idx = 0; idx < B.instructions.size();) 
        {
            Instr &I = B.instructions[idx];
            if (!I.result || !isPure(I.op) || I.operands.empty())
            {
                ++idx;
                continue;
            }
            Value a = I.operands[0];
            Value b = I.operands.size() >= 2 ? I.operands[1] : Value::constInt(0);
            normalizeCommutative(I.op, a, b);
            Key k{I.op, I.type, a, b};
            auto it = table.find(k);
            if (it != table.end())
            {
                unsigned existing = it->second;
                unsigned dead = *I.result;
                // Replace all uses of dead with existing across function
                for (auto &BB : F.blocks)
                {
                    for (auto &J : BB.instructions)
                    {
                        for (auto &Op : J.operands)
                            if (Op.kind == Value::Kind::Temp && Op.id == dead)
                                Op.id = existing;
                        for (auto &ArgList : J.brArgs)
                            for (auto &Arg : ArgList)
                                if (Arg.kind == Value::Kind::Temp && Arg.id == dead)
                                    Arg.id = existing;
                    }
                }
                // Erase instruction
                B.instructions.erase(B.instructions.begin() + static_cast<long>(idx));
                changed = true;
                continue; // don't advance idx
            }
            table.emplace(k, *I.result);
            ++idx;
        }
    }
    return changed;
}

} // namespace il::transform
