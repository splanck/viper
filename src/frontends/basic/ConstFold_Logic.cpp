//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements constant-folding utilities for BASIC logical expressions.  The
// helpers analyse operand types, respect short-circuit semantics, and construct
// replacement AST nodes so later pipeline stages can avoid evaluating redundant
// branches.
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/ConstFold_Logic.hpp"

#include "frontends/basic/ConstFolder.hpp"

#include <utility>

namespace il::frontends::basic::detail
{
namespace
{
/// @brief Allocate a boolean literal expression with the given value.
/// @param value Boolean payload to embed in the new node.
/// @return Owning pointer to a @ref BoolExpr instance.
ExprPtr makeBool(bool value)
{
    auto expr = std::make_unique<BoolExpr>();
    expr->value = value;
    return expr;
}

/// @brief Allocate an integer literal expression with the given value.
/// @param value Integer payload to embed in the new node.
/// @return Owning pointer to an @ref IntExpr instance.
ExprPtr makeInt(long long value)
{
    auto expr = std::make_unique<IntExpr>();
    expr->value = value;
    return expr;
}
} // namespace

/// @brief Fold a logical NOT expression when the operand is constant.
///
/// Boolean operands flip directly.  Numeric operands follow BASIC's convention
/// where zero maps to logical true and any other value maps to false.  Floating
/// values are rejected to avoid imprecise conversions.
///
/// @param operand Expression to evaluate for folding.
/// @return Folded literal when possible; otherwise nullptr.
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

/// @brief Determine whether a short-circuit logical operator can resolve early.
///
/// Examines the left-hand operand for logical `AND`/`OR` variants.  When the
/// operator and boolean value imply that the right-hand operand is irrelevant,
/// the result is returned immediately; otherwise std::nullopt is produced so the
/// caller can continue evaluating.
///
/// @param op  Binary operator under consideration.
/// @param lhs Evaluated left-hand operand.
/// @return Optional folded result when short-circuiting applies.
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

/// @brief Check whether a binary operator participates in short-circuit folding.
/// @param op Candidate operator.
/// @return True for the short-circuit logical operators.
bool isShortCircuitOp(BinaryExpr::Op op)
{
    return op == BinaryExpr::Op::LogicalAndShort || op == BinaryExpr::Op::LogicalOrShort;
}

/// @brief Fold a logical binary expression when both operands are boolean literals.
///
/// Only logical `AND`/`OR` operators (including their short-circuiting forms) are
/// handled.  Mixed or non-boolean operands result in a null return signalling
/// that the caller must leave the expression intact.
///
/// @param lhs Left-hand operand to inspect.
/// @param op  Logical operator applied to the operands.
/// @param rhs Right-hand operand to inspect.
/// @return Folded literal when the operands are both boolean constants; nullptr otherwise.
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
