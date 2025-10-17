//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the logical constant folding helpers used by the BASIC front end.
//
//===----------------------------------------------------------------------===//
//
/// @file
/// @brief Logical constant-folding utilities for BASIC expressions.
/// @details Provides functions that fold boolean expressions, including
///          short-circuit behaviour and numeric coercions, producing new AST
///          literal nodes where possible.

#include "frontends/basic/ConstFold_Logic.hpp"

#include "frontends/basic/ConstFolder.hpp"

#include <utility>

namespace il::frontends::basic::detail
{
namespace
{
/// @brief Construct a boolean literal expression with the given value.
///
/// Allocates a @ref BoolExpr node, assigns the provided truth value, and hands
/// ownership back to the caller.  Keeping the helper local centralizes the
/// boilerplate required when folding logical operations to constants.
///
/// @param value Truth value stored in the new literal.
/// @return Owning pointer to the freshly created literal node.
ExprPtr makeBool(bool value)
{
    auto expr = std::make_unique<BoolExpr>();
    expr->value = value;
    return expr;
}

/// @brief Construct an integer literal expression with the supplied value.
///
/// The folding utilities occasionally need to return 0/1 sentinels when a
/// boolean expression is coerced into numeric context.  This helper mirrors
/// @ref makeBool by providing a concise way to allocate the literal node.
///
/// @param value Integer payload copied into the literal node.
/// @return Owning pointer to the new integer literal expression.
ExprPtr makeInt(long long value)
{
    auto expr = std::make_unique<IntExpr>();
    expr->value = value;
    return expr;
}
} // namespace

/// @brief Attempt to fold a logical NOT expression to a literal.
///
/// The folding logic first checks for a boolean literal operand.  If present,
/// the value is inverted directly.  When the operand is numeric, BASIC treats
/// zero as false and any other integer as true—so the helper emits the
/// corresponding 0 or 1 integer literal.  Floating-point values are rejected to
/// avoid misrepresenting precision-heavy results.
///
/// @param operand Expression being negated.
/// @return Literal expression on success, otherwise nullptr when folding fails.
ExprPtr foldLogicalNot(const Expr &operand)
{
    if (auto *boolExpr = dynamic_cast<const BoolExpr *>(&operand))
        return makeBool(!boolExpr->value);

    if (auto numeric = asNumeric(operand))
    {
        if (numeric->isFloat)
            return nullptr;
        return makeInt(numeric->i == 0 ? 1 : 0);
    }

    return nullptr;
}

/// @brief Inspect the left-hand side of a short-circuit operator for early exit.
///
/// Evaluates BASIC's short-circuit rules: when the first operand of ANDALSO is
/// false the result is immediately false, and when the first operand of ORELSE
/// is true the result is immediately true.  Any other combination—including
/// non-short-circuit operators—returns std::nullopt to signal that the caller
/// must continue evaluating the right-hand side.
///
/// @param op Operator being folded.
/// @param lhs Evaluated boolean literal from the left-hand side.
/// @return Optional literal result that bypasses evaluating the RHS.
std::optional<bool> tryShortCircuit(BinaryExpr::Op op, const BoolExpr &lhs)
{
    switch (op)
    {
        case BinaryExpr::Op::LogicalAndShort:
            if (!lhs.value)
                return false;
            break;
        case BinaryExpr::Op::LogicalOrShort:
            if (lhs.value)
                return true;
            break;
        default:
            break;
    }
    return std::nullopt;
}

/// @brief Test whether the operator participates in short-circuit evaluation.
///
/// Distinguishes the `_Short` variants of AND/OR from their eager counterparts.
/// This helps the folding pipeline decide when to evaluate the right-hand side
/// during constant propagation.
///
/// @param op Binary operator kind under inspection.
/// @return True for short-circuit operators; false otherwise.
bool isShortCircuitOp(BinaryExpr::Op op)
{
    return op == BinaryExpr::Op::LogicalAndShort || op == BinaryExpr::Op::LogicalOrShort;
}

/// @brief Fold a binary logical expression when both operands are literals.
///
/// Accepts only boolean literal operands.  When the operator is a form of AND
/// or OR, the helper computes the logical result directly and returns an
/// equivalent literal.  Other operators are ignored so arithmetic folding can
/// handle them elsewhere.
///
/// @param lhs Left-hand operand inspected for literal value.
/// @param op Logical operator connecting @p lhs and @p rhs.
/// @param rhs Right-hand operand inspected for literal value.
/// @return Literal expression matching the evaluated result, or nullptr when
///         folding is not possible.
ExprPtr foldLogicalBinary(const Expr &lhs, BinaryExpr::Op op, const Expr &rhs)
{
    auto *lhsBool = dynamic_cast<const BoolExpr *>(&lhs);
    auto *rhsBool = dynamic_cast<const BoolExpr *>(&rhs);
    if (!lhsBool || !rhsBool)
        return nullptr;

    switch (op)
    {
        case BinaryExpr::Op::LogicalAnd:
        case BinaryExpr::Op::LogicalAndShort:
            return makeBool(lhsBool->value && rhsBool->value);
        case BinaryExpr::Op::LogicalOr:
        case BinaryExpr::Op::LogicalOrShort:
            return makeBool(lhsBool->value || rhsBool->value);
        default:
            return nullptr;
    }
}

} // namespace il::frontends::basic::detail
