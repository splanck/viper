//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Arithmetic constant folding helpers for the BASIC front end.  These utilities
// canonicalise arithmetic expressions at compile time, handling both integer and
// floating-point operands while preserving overflow semantics mandated by the
// BASIC language definition.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements numeric folding utilities shared by the dispatcher.
/// @details The helpers evaluate constant expressions in place, promoting
///          operands as necessary and preserving diagnostic information so the
///          caller can decide whether folding succeeded.
#include "common/IntegerHelpers.hpp"
#include "frontends/basic/ast/ExprNodes.hpp"
#include "frontends/basic/constfold/Dispatch.hpp"
#include "frontends/basic/constfold/Value.hpp"

#include <array>
#include <cassert>
#include <cstdint>
#include <limits>
#include <optional>

namespace il::frontends::basic::constfold
{
namespace
{
namespace intops = il::common::integer;

constexpr std::size_t kOpCount =
    static_cast<std::size_t>(AST::BinaryExpr::Op::LogicalOr) + 1;

using BinOpFn = Value (*)(Value, Value);

[[nodiscard]] constexpr bool is_i16(long long v) noexcept
{
    return v >= static_cast<long long>(std::numeric_limits<std::int16_t>::min()) &&
           v <= static_cast<long long>(std::numeric_limits<std::int16_t>::max());
}

[[nodiscard]] long long wrap_add(long long lhs, long long rhs) noexcept
{
    const auto promoted = intops::promote_binary(lhs, rhs);
    const auto sum = static_cast<std::uint64_t>(promoted.lhs) +
                     static_cast<std::uint64_t>(promoted.rhs);
    return static_cast<long long>(sum);
}

[[nodiscard]] long long wrap_sub(long long lhs, long long rhs) noexcept
{
    const auto promoted = intops::promote_binary(lhs, rhs);
    const auto diff = static_cast<std::uint64_t>(promoted.lhs) -
                      static_cast<std::uint64_t>(promoted.rhs);
    return static_cast<long long>(diff);
}

[[nodiscard]] long long wrap_mul(long long lhs, long long rhs) noexcept
{
    const auto promoted = intops::promote_binary(lhs, rhs);
    const auto prod = static_cast<std::uint64_t>(promoted.lhs) *
                      static_cast<std::uint64_t>(promoted.rhs);
    return static_cast<long long>(prod);
}

Value fold_add(Value lhs, Value rhs)
{
    if (lhs.isFloat() || rhs.isFloat())
        return Value::fromFloat(lhs.asDouble() + rhs.asDouble());

    const long long sum = wrap_add(lhs.i, rhs.i);
    if (is_i16(lhs.i) && is_i16(rhs.i))
    {
        if (sum < static_cast<long long>(std::numeric_limits<std::int16_t>::min()) ||
            sum > static_cast<long long>(std::numeric_limits<std::int16_t>::max()))
            return Value::invalid();
    }
    return Value::fromInt(sum);
}

Value fold_sub(Value lhs, Value rhs)
{
    if (lhs.isFloat() || rhs.isFloat())
        return Value::fromFloat(lhs.asDouble() - rhs.asDouble());
    return Value::fromInt(wrap_sub(lhs.i, rhs.i));
}

Value fold_mul(Value lhs, Value rhs)
{
    if (lhs.isFloat() || rhs.isFloat())
        return Value::fromFloat(lhs.asDouble() * rhs.asDouble());
    return Value::fromInt(wrap_mul(lhs.i, rhs.i));
}

Value fold_div(Value lhs, Value rhs)
{
    const double divisor = rhs.asDouble();
    if (divisor == 0.0)
        return Value::invalid();
    return Value::fromFloat(lhs.asDouble() / divisor);
}

Value fold_idiv(Value lhs, Value rhs)
{
    if (!lhs.isInt() || !rhs.isInt())
        return Value::invalid();
    if (rhs.i == 0)
        return Value::invalid();
    return Value::fromInt(lhs.i / rhs.i);
}

Value fold_mod(Value lhs, Value rhs)
{
    if (!lhs.isInt() || !rhs.isInt())
        return Value::invalid();
    if (rhs.i == 0)
        return Value::invalid();
    return Value::fromInt(lhs.i % rhs.i);
}

constexpr std::array<BinOpFn, kOpCount> make_arith_table()
{
    std::array<BinOpFn, kOpCount> table{};
    table.fill(nullptr);
    table[static_cast<std::size_t>(AST::BinaryExpr::Op::Add)] = &fold_add;
    table[static_cast<std::size_t>(AST::BinaryExpr::Op::Sub)] = &fold_sub;
    table[static_cast<std::size_t>(AST::BinaryExpr::Op::Mul)] = &fold_mul;
    table[static_cast<std::size_t>(AST::BinaryExpr::Op::Div)] = &fold_div;
    table[static_cast<std::size_t>(AST::BinaryExpr::Op::IDiv)] = &fold_idiv;
    table[static_cast<std::size_t>(AST::BinaryExpr::Op::Mod)] = &fold_mod;
    return table;
}

constexpr auto kArithFold = make_arith_table();

std::optional<Value> tryFold(AST::BinaryExpr::Op op, Value lhs, Value rhs)
{
    if (!lhs.valid || !rhs.valid)
        return std::nullopt;
    const auto index = static_cast<std::size_t>(op);
    if (index >= kArithFold.size())
        return std::nullopt;
    auto fn = kArithFold[index];
    if (!fn)
        return std::nullopt;
    auto promoted = promote(lhs, rhs);
    Value result = fn(promoted.first, promoted.second);
    if (!result.valid)
        return std::nullopt;
    return result;
}

std::optional<NumericValue> fold_numeric(AST::BinaryExpr::Op op,
                                         const NumericValue &lhsRaw,
                                         const NumericValue &rhsRaw)
{
    auto folded = tryFold(op, makeValue(lhsRaw), makeValue(rhsRaw));
#ifdef VIPER_CONSTFOLD_ASSERTS
    if (folded && (op == AST::BinaryExpr::Op::Add || op == AST::BinaryExpr::Op::Mul))
    {
        auto swapped = tryFold(op, makeValue(rhsRaw), makeValue(lhsRaw));
        if (swapped)
        {
            if (folded->isFloat() || swapped->isFloat())
                assert(folded->asDouble() == swapped->asDouble());
            else
                assert(folded->i == swapped->i);
        }
    }
#endif
    if (!folded)
        return std::nullopt;
    return toNumericValue(*folded);
}

} // namespace

AST::ExprPtr fold_unary_arith(AST::UnaryExpr::Op op, const AST::Expr &value)
{
    auto numeric = numeric_from_expr(value);
    if (!numeric)
        return nullptr;

    NumericValue result = *numeric;
    switch (op)
    {
        case AST::UnaryExpr::Op::Plus:
            break;
        case AST::UnaryExpr::Op::Negate:
            if (result.isFloat)
            {
                result = NumericValue{true, -result.f, static_cast<long long>(-result.f)};
            }
            else
            {
                auto negated = wrap_sub(0, result.i);
                result = NumericValue{false, static_cast<double>(negated), negated};
            }
            break;
        default:
            return nullptr;
    }

    if (result.isFloat)
    {
        auto out = std::make_unique<::il::frontends::basic::FloatExpr>();
        out->value = result.f;
        return out;
    }
    auto out = std::make_unique<::il::frontends::basic::IntExpr>();
    out->value = result.i;
    return out;
}

std::optional<Constant> fold_arith(AST::BinaryExpr::Op op,
                                   const Constant &lhs,
                                   const Constant &rhs)
{
    if ((lhs.kind != LiteralKind::Int && lhs.kind != LiteralKind::Float) ||
        (rhs.kind != LiteralKind::Int && rhs.kind != LiteralKind::Float))
        return std::nullopt;

    auto folded = tryFold(op, makeValue(lhs.numeric), makeValue(rhs.numeric));
    if (!folded)
        return std::nullopt;

    Constant c;
    c.kind = folded->isFloat() ? LiteralKind::Float : LiteralKind::Int;
    c.numeric = toNumericValue(*folded);
    return c;
}

} // namespace il::frontends::basic::constfold

