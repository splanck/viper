//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Comparison constant folding helpers for the BASIC front end.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements comparison folding utilities.

#include "frontends/basic/constfold/Dispatch.hpp"
#include "frontends/basic/constfold/Value.hpp"

#include <array>
#include <cmath>
#include <optional>

namespace il::frontends::basic::constfold
{
namespace
{
enum class Outcome : std::size_t
{
    Less = 0,
    Equal = 1,
    Greater = 2,
    Unordered = 3,
};

constexpr std::size_t kOpCount =
    static_cast<std::size_t>(AST::BinaryExpr::Op::LogicalOr) + 1;

using BinOpFn = Value (*)(Value, Value);

struct TruthRow
{
    AST::BinaryExpr::Op op;
    std::array<std::optional<long long>, 4> truth;
};

constexpr std::array<TruthRow, 6> kTruthTable = {
    {{AST::BinaryExpr::Op::Eq, {0LL, 1LL, 0LL, 0LL}},
     {AST::BinaryExpr::Op::Ne, {1LL, 0LL, 1LL, 1LL}},
     {AST::BinaryExpr::Op::Lt, {1LL, 0LL, 0LL, std::nullopt}},
     {AST::BinaryExpr::Op::Le, {1LL, 1LL, 0LL, std::nullopt}},
     {AST::BinaryExpr::Op::Gt, {0LL, 0LL, 1LL, std::nullopt}},
     {AST::BinaryExpr::Op::Ge, {0LL, 1LL, 1LL, std::nullopt}}}};

[[nodiscard]] Value from_truth(AST::BinaryExpr::Op op, Outcome outcome)
{
    for (const auto &row : kTruthTable)
    {
        if (row.op == op)
        {
            auto value = row.truth[static_cast<std::size_t>(outcome)];
            if (!value)
                return Value::invalid();
            return Value::fromInt(*value);
        }
    }
    return Value::invalid();
}

[[nodiscard]] Outcome compare_ordered(Value lhs, Value rhs)
{
    if (lhs.isFloat() || rhs.isFloat())
    {
        double lv = lhs.asDouble();
        double rv = rhs.asDouble();
        if (std::isnan(lv) || std::isnan(rv))
            return Outcome::Unordered;
        if (lv < rv)
            return Outcome::Less;
        if (lv > rv)
            return Outcome::Greater;
        return Outcome::Equal;
    }
    if (lhs.i < rhs.i)
        return Outcome::Less;
    if (lhs.i > rhs.i)
        return Outcome::Greater;
    return Outcome::Equal;
}

Value fold_eq(Value lhs, Value rhs)
{
    return from_truth(AST::BinaryExpr::Op::Eq, compare_ordered(lhs, rhs));
}

Value fold_ne(Value lhs, Value rhs)
{
    return from_truth(AST::BinaryExpr::Op::Ne, compare_ordered(lhs, rhs));
}

Value fold_lt(Value lhs, Value rhs)
{
    return from_truth(AST::BinaryExpr::Op::Lt, compare_ordered(lhs, rhs));
}

Value fold_le(Value lhs, Value rhs)
{
    return from_truth(AST::BinaryExpr::Op::Le, compare_ordered(lhs, rhs));
}

Value fold_gt(Value lhs, Value rhs)
{
    return from_truth(AST::BinaryExpr::Op::Gt, compare_ordered(lhs, rhs));
}

Value fold_ge(Value lhs, Value rhs)
{
    return from_truth(AST::BinaryExpr::Op::Ge, compare_ordered(lhs, rhs));
}

constexpr std::array<BinOpFn, kOpCount> make_compare_table()
{
    std::array<BinOpFn, kOpCount> table{};
    table.fill(nullptr);
    table[static_cast<std::size_t>(AST::BinaryExpr::Op::Eq)] = &fold_eq;
    table[static_cast<std::size_t>(AST::BinaryExpr::Op::Ne)] = &fold_ne;
    table[static_cast<std::size_t>(AST::BinaryExpr::Op::Lt)] = &fold_lt;
    table[static_cast<std::size_t>(AST::BinaryExpr::Op::Le)] = &fold_le;
    table[static_cast<std::size_t>(AST::BinaryExpr::Op::Gt)] = &fold_gt;
    table[static_cast<std::size_t>(AST::BinaryExpr::Op::Ge)] = &fold_ge;
    return table;
}

constexpr auto kCompareFold = make_compare_table();

std::optional<Value> tryFold(AST::BinaryExpr::Op op, Value lhs, Value rhs)
{
    if (!lhs.valid || !rhs.valid)
        return std::nullopt;
    const auto index = static_cast<std::size_t>(op);
    if (index >= kCompareFold.size())
        return std::nullopt;
    auto fn = kCompareFold[index];
    if (!fn)
        return std::nullopt;
    auto promoted = promote(lhs, rhs);
    Value result = fn(promoted.first, promoted.second);
    if (!result.valid)
        return std::nullopt;
    return result;
}

bool is_numeric(LiteralKind kind)
{
    return kind == LiteralKind::Int || kind == LiteralKind::Float;
}

Constant make_int_constant(long long value)
{
    Constant c;
    c.kind = LiteralKind::Int;
    c.numeric = NumericValue{false, static_cast<double>(value), value};
    return c;
}

} // namespace

std::optional<Constant> fold_compare(AST::BinaryExpr::Op op,
                                     const Constant &lhs,
                                     const Constant &rhs)
{
    if (lhs.kind == LiteralKind::String && rhs.kind == LiteralKind::String)
    {
        if (op != AST::BinaryExpr::Op::Eq && op != AST::BinaryExpr::Op::Ne)
            return std::nullopt;
        bool eq = lhs.stringValue == rhs.stringValue;
        long long v = (op == AST::BinaryExpr::Op::Eq) ? (eq ? 1 : 0) : (eq ? 0 : 1);
        return make_int_constant(v);
    }

    if (!is_numeric(lhs.kind) || !is_numeric(rhs.kind))
        return std::nullopt;

    auto folded = tryFold(op, makeValue(lhs.numeric), makeValue(rhs.numeric));
    if (!folded)
        return std::nullopt;

    Constant c;
    c.kind = LiteralKind::Int;
    c.numeric = toNumericValue(*folded);
    return c;
}

} // namespace il::frontends::basic::constfold
