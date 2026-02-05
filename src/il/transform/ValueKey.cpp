//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements hashing and equality helpers for value-based CSE keys.
/// @details Provides deterministic hashing, equality, and canonicalization for
///          IL values and expression keys. These utilities underpin CSE/GVN
///          passes by ensuring identical expressions map to the same key even
///          when operand order varies for commutative operations.

#include "il/transform/ValueKey.hpp"

#include <algorithm>
#include <functional>
#include <string>

using namespace il::core;

namespace il::transform
{

/// @brief Hash a Value based on its kind and payload.
/// @details Delegates to the shared valueHash() helper in il::core for
///          consistent hashing across the codebase.
/// @param v Value to hash.
/// @return Hash code suitable for unordered containers.
size_t ValueHash::operator()(const Value &v) const noexcept
{
    return valueHash(v);
}

/// @brief Compare two Values for equality of semantic payload.
/// @details Delegates to the shared valueEquals() helper in il::core for
///          consistent comparison across the codebase.
/// @param a First value to compare.
/// @param b Second value to compare.
/// @return True if both values represent the same payload; false otherwise.
bool ValueEq::operator()(const Value &a, const Value &b) const noexcept
{
    return valueEquals(a, b);
}

/// @brief Compare two expression keys for structural equivalence.
/// @details Keys are equal when opcode, result type, and operand list match.
///          Operand comparison uses ValueEq so temporaries and constants are
///          matched by payload rather than by metadata.
/// @param o Other key to compare against.
/// @return True if both keys describe the same normalized expression.
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

/// @brief Hash an expression key for use in unordered maps.
/// @details Combines opcode and type with each operand hash using a mixing
///          pattern that reduces collisions for different operand sequences.
///          The hash is stable across runs as long as ValueHash is stable.
/// @param k Key to hash.
/// @return Hash code suitable for unordered containers.
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

/// @brief Determine whether an opcode is commutative for CSE purposes.
/// @details Commutative operations can have their operands reordered without
///          changing semantics, allowing keys to be canonicalized by sorting.
///          Only opcodes proven commutative in IL semantics are included.
/// @param op Opcode to test.
/// @return True if operand order does not affect the result; false otherwise.
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

/// @brief Determine whether an opcode is safe to use in expression CSE/GVN.
/// @details The whitelist is intentionally conservative: only operations with
///          no side effects and no trapping behavior are accepted. This allows
///          common subexpression elimination to replace occurrences freely
///          without altering program behavior.
/// @param op Opcode to test.
/// @return True if the opcode is safe for value-based CSE; false otherwise.
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

/// @brief Canonicalize operands for commutative instructions.
/// @details For commutative opcodes, operands are ordered using a rank tuple so
///          the resulting ValueKey is deterministic regardless of input order.
///          Non-commutative operations are returned unchanged. The ranking
///          prefers temporaries over constants, then integers over floats, and
///          uses payload values to provide a stable ordering.
/// @param instr Instruction providing the operand list.
/// @return A new operand vector, possibly reordered, for key construction.
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

/// @brief Build a normalized ValueKey for a candidate instruction.
/// @details The helper filters out instructions that are terminators, have side
///          effects, read or write memory, or lack a result. For eligible opcodes
///          it constructs a key with normalized operands so equivalent
///          expressions map to the same key for CSE/GVN.
/// @param instr Instruction to analyze.
/// @return Populated ValueKey when eligible; std::nullopt otherwise.
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
