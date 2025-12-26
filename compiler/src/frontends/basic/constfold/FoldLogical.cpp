//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
// This source file is part of the Viper project.
//
// File: src/frontends/basic/constfold/FoldLogical.cpp
// Purpose: Realise the logical-expression portion of the BASIC constant folder
//          so boolean expressions composed of literals can be reduced during
//          parsing.
// Key invariants: Preserves BASIC short-circuit semantics, refuses to fold when
//                 numeric operands would promote to floating point, and
//                 maintains canonical literal node types for folded results.
// Ownership/Lifetime: Allocates new AST nodes for folded expressions while
//                     leaving ownership with the caller via smart pointers.
// Links: docs/codemap.md, docs/il-guide.md#basic-frontend-constant-folding
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements logical folding helpers shared by ConstFolder.
/// @details Covers unary NOT, binary boolean operations, and short-circuit
///          detection so the dispatcher can replace literal logical expressions
///          with compact AST nodes.

#include "frontends/basic/constfold/ConstantUtils.hpp"
#include "frontends/basic/constfold/Dispatch.hpp"

#include "frontends/basic/ASTUtils.hpp"
#include "frontends/basic/ast/ExprNodes.hpp"
#include <cassert>

namespace il::frontends::basic::constfold
{
/// @brief Attempt to fold a unary NOT expression when the operand is literal.
/// @details Handles both boolean and integer representations, ensuring that
///          integer literals follow BASIC's zero/non-zero truthiness rules.
///          Non-literal operands cause the helper to return @c nullptr so the
///          caller can leave the expression untouched.
/// @param operand Candidate operand to fold.
/// @return Folded expression node or @c nullptr when folding is not possible.
AST::ExprPtr fold_logical_not(const AST::Expr &operand)
{
    if (const auto *boolExpr = as<const BoolExpr>(operand))
    {
        auto out = std::make_unique<BoolExpr>();
        out->value = !boolExpr->value;
        return out;
    }
    if (auto numeric = numeric_from_expr(operand))
    {
        if (numeric->isFloat)
            return nullptr;
        auto out = std::make_unique<::il::frontends::basic::BoolExpr>();
        out->value = numeric->i == 0;
        return out;
    }
    return nullptr;
}

/// @brief Evaluate short-circuit rules for a boolean left-hand operand.
/// @details Implements BASIC's semantics for `AND`/`OR` short-circuit variants
///          by inspecting the left operand. When the operator guarantees the
///          result without examining the right operand the folded boolean is
///          returned.
/// @param op Logical operator under consideration.
/// @param lhs Literal boolean expression on the left-hand side.
/// @return Folded boolean result when short-circuiting applies.
std::optional<bool> try_short_circuit(AST::BinaryExpr::Op op, const AST::BoolExpr &lhs)
{
    switch (op)
    {
        case AST::BinaryExpr::Op::LogicalAndShort:
            if (!lhs.value)
                return false;
            break;
        case AST::BinaryExpr::Op::LogicalOrShort:
            if (lhs.value)
                return true;
            break;
        default:
            break;
    }
    return std::nullopt;
}

/// @brief Determine whether an operator participates in short-circuit logic.
/// @param op Operator from the BASIC AST.
/// @return @c true when @p op is a short-circuiting AND/OR variant.
bool is_short_circuit(AST::BinaryExpr::Op op)
{
    return op == AST::BinaryExpr::Op::LogicalAndShort || op == AST::BinaryExpr::Op::LogicalOrShort;
}

/// @brief Fold binary logical expressions when both operands are boolean
///        literals.
/// @details Supports both eager and short-circuit operators by applying the
///          standard truth tables. Returns @c nullptr when either operand is not
///          a literal boolean so the dispatcher can attempt numeric folding
///          instead.
/// @param lhs Left-hand operand.
/// @param op Binary logical opcode.
/// @param rhs Right-hand operand.
/// @return New boolean literal expression or @c nullptr when folding fails.
AST::ExprPtr fold_boolean_binary(const AST::Expr &lhs, AST::BinaryExpr::Op op, const AST::Expr &rhs)
{
    const auto *lhsBool = as<const BoolExpr>(lhs);
    const auto *rhsBool = as<const BoolExpr>(rhs);
    if (!lhsBool || !rhsBool)
        return nullptr;

    auto out = std::make_unique<BoolExpr>();
    switch (op)
    {
        case AST::BinaryExpr::Op::LogicalAnd:
        case AST::BinaryExpr::Op::LogicalAndShort:
            out->value = lhsBool->value && rhsBool->value;
            return out;
        case AST::BinaryExpr::Op::LogicalOr:
        case AST::BinaryExpr::Op::LogicalOrShort:
            out->value = lhsBool->value || rhsBool->value;
            return out;
        default:
            return nullptr;
    }
}

/// @brief Fold logical operators applied to numeric literal operands.
/// @details Promotes operands to a shared integer representation, enforces that
///          both remain integral (rejecting floats), and evaluates the logical
///          expression using BASIC's non-zero truthiness rules.
/// @param op Logical operator being folded.
/// @param lhs Left-hand literal constant.
/// @param rhs Right-hand literal constant.
/// @return Integer constant representing the folded logical result.
std::optional<Constant> fold_numeric_logic(AST::BinaryExpr::Op op,
                                           const Constant &lhs,
                                           const Constant &rhs)
{
    if ((lhs.kind != LiteralKind::Int && lhs.kind != LiteralKind::Float) ||
        (rhs.kind != LiteralKind::Int && rhs.kind != LiteralKind::Float))
        return std::nullopt;

    NumericValue left = promote_numeric(lhs.numeric, rhs.numeric);
    NumericValue right = promote_numeric(rhs.numeric, lhs.numeric);
    if (left.isFloat || right.isFloat)
        return std::nullopt;

    bool result = false;
    switch (op)
    {
        case AST::BinaryExpr::Op::LogicalAnd:
        case AST::BinaryExpr::Op::LogicalAndShort:
            result = (left.i != 0) && (right.i != 0);
            break;
        case AST::BinaryExpr::Op::LogicalOr:
        case AST::BinaryExpr::Op::LogicalOrShort:
            result = (left.i != 0) || (right.i != 0);
            break;
        default:
            return std::nullopt;
    }
#ifdef VIPER_CONSTFOLD_ASSERTS
    if (op == AST::BinaryExpr::Op::LogicalAnd || op == AST::BinaryExpr::Op::LogicalAndShort ||
        op == AST::BinaryExpr::Op::LogicalOr || op == AST::BinaryExpr::Op::LogicalOrShort)
    {
        bool swapped =
            (op == AST::BinaryExpr::Op::LogicalAnd || op == AST::BinaryExpr::Op::LogicalAndShort)
                ? (right.i != 0 && left.i != 0)
                : (right.i != 0 || left.i != 0);
        assert(result == swapped);
    }
#endif
    return make_bool_constant(result);
}

} // namespace il::frontends::basic::constfold
