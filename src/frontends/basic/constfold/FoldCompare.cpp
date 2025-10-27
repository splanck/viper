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

Constant make_int_constant(long long value)
{
    Constant c;
    c.kind = LiteralKind::Int;
    c.numeric = NumericValue{false, static_cast<double>(value), value};
    return c;
}

bool is_numeric(LiteralKind kind)
{
    return kind == LiteralKind::Int || kind == LiteralKind::Float;
}

const TruthRow *find_truth(AST::BinaryExpr::Op op)
{
    for (const auto &row : kTruthTable)
    {
        if (row.op == op)
            return &row;
    }
    return nullptr;
}

Outcome compare_numeric(const NumericValue &lhsRaw, const NumericValue &rhsRaw)
{
    NumericValue lhs = promote_numeric(lhsRaw, rhsRaw);
    NumericValue rhs = promote_numeric(rhsRaw, lhsRaw);
    if (lhs.isFloat || rhs.isFloat)
    {
        double lv = lhs.isFloat ? lhs.f : static_cast<double>(lhs.i);
        double rv = rhs.isFloat ? rhs.f : static_cast<double>(rhs.i);
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

    const TruthRow *truth = find_truth(op);
    if (!truth)
        return std::nullopt;

    Outcome outcome = compare_numeric(lhs.numeric, rhs.numeric);
    auto value = truth->truth[static_cast<std::size_t>(outcome)];
    if (!value)
        return std::nullopt;
    return make_int_constant(*value);
}

} // namespace il::frontends::basic::constfold
