//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Arithmetic constant folding helpers for the BASIC front end.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements numeric folding utilities shared by the dispatcher.
#include "frontends/basic/constfold/Dispatch.hpp"
#include "frontends/basic/ast/ExprNodes.hpp"
#include <cassert>
#include <cstdint>
#include <limits>
#include <optional>
namespace il::frontends::basic::constfold
{
namespace
{
std::optional<NumericValue> fold_numeric_impl(AST::BinaryExpr::Op op, const NumericValue &lhsRaw, const NumericValue &rhsRaw)
{
    NumericValue lhs = promote_numeric(lhsRaw, rhsRaw);
    NumericValue rhs = promote_numeric(rhsRaw, lhsRaw);
    const bool ints = !lhs.isFloat && !rhs.isFloat;
    const double lv = lhs.isFloat ? lhs.f : static_cast<double>(lhs.i);
    const double rv = rhs.isFloat ? rhs.f : static_cast<double>(rhs.i);

    switch (op)
    {
        case AST::BinaryExpr::Op::Add:
            if (ints)
            {
                const auto min16 = std::numeric_limits<std::int16_t>::min(),
                           max16 = std::numeric_limits<std::int16_t>::max();
                auto sum = static_cast<long long>(static_cast<std::uint64_t>(lhs.i) +
                                                  static_cast<std::uint64_t>(rhs.i));
                if (lhs.i >= min16 && lhs.i <= max16 && rhs.i >= min16 && rhs.i <= max16 &&
                    (sum < min16 || sum > max16))
                    return std::nullopt;
                return NumericValue{false, static_cast<double>(sum), sum};
            }
            return NumericValue{true, lv + rv, static_cast<long long>(lv + rv)};
        case AST::BinaryExpr::Op::Sub:
            if (ints)
            {
                auto diff = static_cast<long long>(static_cast<std::uint64_t>(lhs.i) -
                                                   static_cast<std::uint64_t>(rhs.i));
                return NumericValue{false, static_cast<double>(diff), diff};
            }
            return NumericValue{true, lv - rv, static_cast<long long>(lv - rv)};
        case AST::BinaryExpr::Op::Mul:
            if (ints)
            {
                auto prod = static_cast<long long>(static_cast<std::uint64_t>(lhs.i) *
                                                   static_cast<std::uint64_t>(rhs.i));
                return NumericValue{false, static_cast<double>(prod), prod};
            }
            return NumericValue{true, lv * rv, static_cast<long long>(lv * rv)};
        case AST::BinaryExpr::Op::Div:
            if (rv == 0.0)
                return std::nullopt;
            return NumericValue{true, lv / rv, static_cast<long long>(lv / rv)};
        case AST::BinaryExpr::Op::IDiv:
            if (!ints || rhs.i == 0)
                return std::nullopt;
            return NumericValue{false, static_cast<double>(lhs.i / rhs.i), lhs.i / rhs.i};
        case AST::BinaryExpr::Op::Mod:
            if (!ints || rhs.i == 0)
                return std::nullopt;
            return NumericValue{false, static_cast<double>(lhs.i % rhs.i), lhs.i % rhs.i};
        default:
            return std::nullopt;
    }
}

std::optional<NumericValue> fold_numeric(AST::BinaryExpr::Op op, const NumericValue &lhsRaw, const NumericValue &rhsRaw)
{
    auto result = fold_numeric_impl(op, lhsRaw, rhsRaw);
#ifdef VIPER_CONSTFOLD_ASSERTS
    if (result && (op == AST::BinaryExpr::Op::Add || op == AST::BinaryExpr::Op::Mul))
        if (auto swapped = fold_numeric_impl(op, rhsRaw, lhsRaw))
            if (result->isFloat || swapped->isFloat)
                assert(result->isFloat == swapped->isFloat && result->f == swapped->f);
            else
                assert(result->i == swapped->i);
#endif
    return result;
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
                auto neg = static_cast<long long>(static_cast<std::uint64_t>(0) -
                                                  static_cast<std::uint64_t>(result.i));
                result = NumericValue{false, static_cast<double>(neg), neg};
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

std::optional<Constant> fold_arith(AST::BinaryExpr::Op op, const Constant &lhs, const Constant &rhs)
{
    if ((lhs.kind != LiteralKind::Int && lhs.kind != LiteralKind::Float) ||
        (rhs.kind != LiteralKind::Int && rhs.kind != LiteralKind::Float))
        return std::nullopt;

    if (auto folded = fold_numeric(op, lhs.numeric, rhs.numeric))
    {
        Constant c;
        c.kind = folded->isFloat ? LiteralKind::Float : LiteralKind::Int;
        c.numeric = *folded;
        return c;
    }
    return std::nullopt;
}

} // namespace il::frontends::basic::constfold
