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
#include <cassert>
#include <cstdint>
#include <limits>
#include <optional>

namespace il::frontends::basic::constfold
{
namespace
{
namespace intops = il::common::integer;

/// @brief Perform the core arithmetic folding logic for numeric operands.
/// @details The helper promotes operands to a common representation, executes
///          the arithmetic operation specified by @p op, and applies BASIC's
///          overflow and division-by-zero semantics.  Integer operations reuse
///          the integer helper utilities to avoid undefined behaviour on
///          overflow, while floating-point operations operate in double
///          precision.  Returning @c std::nullopt signals that the expression
///          cannot be folded safely.
/// @param op Arithmetic operator being folded.
/// @param lhsRaw Left-hand constant operand.
/// @param rhsRaw Right-hand constant operand.
/// @return Folded numeric value or @c std::nullopt when folding is invalid.
std::optional<NumericValue> fold_numeric_impl(AST::BinaryExpr::Op op,
                                              const NumericValue &lhsRaw,
                                              const NumericValue &rhsRaw)
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
                const auto promoted = intops::promote_binary(lhs.i, rhs.i);
                auto sum = static_cast<long long>(static_cast<std::uint64_t>(promoted.lhs) +
                                                  static_cast<std::uint64_t>(promoted.rhs));
                if (lhs.i >= min16 && lhs.i <= max16 && rhs.i >= min16 && rhs.i <= max16 &&
                    (sum < min16 || sum > max16))
                    return std::nullopt;
                return NumericValue{false, static_cast<double>(sum), sum};
            }
            return NumericValue{true, lv + rv, static_cast<long long>(lv + rv)};
        case AST::BinaryExpr::Op::Sub:
            if (ints)
            {
                const auto promoted = intops::promote_binary(lhs.i, rhs.i);
                auto diff = static_cast<long long>(static_cast<std::uint64_t>(promoted.lhs) -
                                                   static_cast<std::uint64_t>(promoted.rhs));
                return NumericValue{false, static_cast<double>(diff), diff};
            }
            return NumericValue{true, lv - rv, static_cast<long long>(lv - rv)};
        case AST::BinaryExpr::Op::Mul:
            if (ints)
            {
                const auto promoted = intops::promote_binary(lhs.i, rhs.i);
                auto prod = static_cast<long long>(static_cast<std::uint64_t>(promoted.lhs) *
                                                   static_cast<std::uint64_t>(promoted.rhs));
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

/// @brief Public entry point for folding numeric binary expressions.
/// @details Delegates to @ref fold_numeric_impl and, when assertions are
///          enabled, verifies commutativity for addition and multiplication by
///          re-folding with operands swapped.  This guards against asymmetric
///          promotion bugs without affecting release builds.
/// @param op Arithmetic operator under evaluation.
/// @param lhsRaw Left-hand operand.
/// @param rhsRaw Right-hand operand.
/// @return Folded numeric result or @c std::nullopt.
std::optional<NumericValue> fold_numeric(AST::BinaryExpr::Op op,
                                         const NumericValue &lhsRaw,
                                         const NumericValue &rhsRaw)
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

/// @brief Fold unary arithmetic expressions when the operand is constant.
/// @details Attempts to extract a @ref NumericValue from @p value and applies the
///          unary operator.  Only the identity and negation operators are
///          supported; unsupported operators or non-constant operands result in a
///          @c nullptr return so the caller can emit the original expression.
///          When folding succeeds a new AST node containing the literal result
///          is returned.
/// @param op Unary operator being folded.
/// @param value Expression to evaluate.
/// @return Newly allocated constant expression or @c nullptr.
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
                const auto promoted = intops::promote_binary(0, result.i);
                auto neg = static_cast<long long>(static_cast<std::uint64_t>(promoted.lhs) -
                                                  static_cast<std::uint64_t>(promoted.rhs));
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

/// @brief Fold binary arithmetic expressions composed of literal constants.
/// @details Validates that both operands are numeric literals before forwarding
///          to @ref fold_numeric.  When folding succeeds a @ref Constant with the
///          appropriate literal kind (integer or float) is produced so callers
///          can splice the result back into the AST.
/// @param op Operator to fold.
/// @param lhs Left-hand constant operand.
/// @param rhs Right-hand constant operand.
/// @return Folded constant or @c std::nullopt when folding fails.
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
