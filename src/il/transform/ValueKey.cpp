//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//

#include "il/transform/ValueKey.hpp"

#include <algorithm>
#include <functional>
#include <string>

using namespace il::core;

namespace il::transform
{

size_t ValueHash::operator()(const Value &v) const noexcept
{
    size_t h = static_cast<size_t>(v.kind) * 1469598103934665603ULL;
    switch (v.kind)
    {
        case Value::Kind::Temp:
            h ^= static_cast<size_t>(v.id) + 0x9e3779b97f4a7c15ULL;
            break;
        case Value::Kind::ConstInt:
            h ^= static_cast<size_t>(v.i64) ^ (v.isBool ? 0xBEEF : 0);
            break;
        case Value::Kind::ConstFloat:
        {
            union
            {
                double d;
                unsigned long long u;
            } u{};

            u.d = v.f64;
            h ^= static_cast<size_t>(u.u);
            break;
        }
        case Value::Kind::ConstStr:
        case Value::Kind::GlobalAddr:
            h ^= std::hash<std::string>{}(v.str);
            break;
        case Value::Kind::NullPtr:
            h ^= 0xabcdefULL;
            break;
    }
    return h;
}

bool ValueEq::operator()(const Value &a, const Value &b) const noexcept
{
    if (a.kind != b.kind)
        return false;
    switch (a.kind)
    {
        case Value::Kind::Temp:
            return a.id == b.id;
        case Value::Kind::ConstInt:
            return a.i64 == b.i64 && a.isBool == b.isBool;
        case Value::Kind::ConstFloat:
            return a.f64 == b.f64;
        case Value::Kind::ConstStr:
        case Value::Kind::GlobalAddr:
            return a.str == b.str;
        case Value::Kind::NullPtr:
            return true;
    }
    return false;
}

bool ValueKey::operator==(const ValueKey &o) const noexcept
{
    if (op != o.op || type != o.type || operands.size() != o.operands.size())
        return false;
    ValueEq eq;
    for (std::size_t i = 0; i < operands.size(); ++i)
    {
        if (!eq(operands[i], o.operands[i]))
            return false;
    }
    return true;
}

size_t ValueKeyHash::operator()(const ValueKey &k) const noexcept
{
    size_t h = static_cast<size_t>(k.op) * 1099511628211ULL ^ static_cast<size_t>(k.type);
    ValueHash hv;
    for (const auto &v : k.operands)
    {
        h ^= hv(v) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
    }
    return h;
}

bool isCommutativeCSE(Opcode op) noexcept
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
        case Opcode::UCmpLT:
        case Opcode::UCmpLE:
        case Opcode::UCmpGT:
        case Opcode::UCmpGE:
        case Opcode::SCmpLT:
        case Opcode::SCmpLE:
        case Opcode::SCmpGT:
        case Opcode::SCmpGE:
        case Opcode::FAdd:
        case Opcode::FMul:
        case Opcode::FCmpEQ:
        case Opcode::FCmpNE:
            return true;
        default:
            return false;
    }
}

bool isSafeCSEOpcode(Opcode op) noexcept
{
    // Restrict to operations that cannot trap and have no hidden side effects.
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
        case Opcode::UCmpLT:
        case Opcode::UCmpLE:
        case Opcode::UCmpGT:
        case Opcode::UCmpGE:
        case Opcode::FAdd:
        case Opcode::FSub:
        case Opcode::FMul:
        case Opcode::FCmpEQ:
        case Opcode::FCmpNE:
        case Opcode::FCmpLT:
        case Opcode::FCmpLE:
        case Opcode::FCmpGT:
        case Opcode::FCmpGE:
        case Opcode::Zext1:
        case Opcode::Trunc1:
            return true;
        default:
            return false;
    }
}

static std::vector<Value> normaliseOperands(const Instr &instr)
{
    std::vector<Value> ops = instr.operands;
    if (ops.size() < 2)
        return ops;

    if (!isCommutativeCSE(instr.op))
        return ops;

    auto rank = [](const Value &v) -> std::tuple<int, unsigned long long, std::string>
    {
        switch (v.kind)
        {
            case Value::Kind::Temp:
                return {3, v.id, {}};
            case Value::Kind::ConstInt:
                return {2, static_cast<unsigned long long>(v.i64 ^ (v.isBool ? 1u : 0u)), {}};
            case Value::Kind::ConstFloat:
            {
                union
                {
                    double d;
                    unsigned long long u;
                } u{};
                u.d = v.f64;
                return {1, u.u, {}};
            }
            case Value::Kind::ConstStr:
            case Value::Kind::GlobalAddr:
                return {0, 0ULL, v.str};
            case Value::Kind::NullPtr:
                return {0, 0ULL, std::string("null")};
        }
        return {0, 0ULL, std::string{}};
    };

    if (!(rank(ops[0]) >= rank(ops[1])))
        std::swap(ops[0], ops[1]);
    return ops;
}

std::optional<ValueKey> makeValueKey(const Instr &instr)
{
    const auto &meta = getOpcodeInfo(instr.op);
    if (meta.isTerminator || meta.hasSideEffects)
        return std::nullopt;
    if (hasMemoryRead(instr.op) || hasMemoryWrite(instr.op))
        return std::nullopt;
    if (!instr.result)
        return std::nullopt;
    if (!isSafeCSEOpcode(instr.op))
        return std::nullopt;

    ValueKey key;
    key.op = instr.op;
    key.type = instr.type.kind;
    key.operands = normaliseOperands(instr);
    return key;
}

} // namespace il::transform
