//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Logical constant folding helpers for the BASIC front end.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements logical folding helpers shared by ConstFolder.

#include "frontends/basic/constfold/Dispatch.hpp"

#include "frontends/basic/ast/ExprNodes.hpp"
#include <cassert>

namespace il::frontends::basic::constfold
{
namespace
{
Constant make_int_constant(bool value)
{
    Constant c;
    c.kind = LiteralKind::Int;
    c.numeric = NumericValue{false, value ? 1.0 : 0.0, value ? 1 : 0};
    return c;
}
} // namespace

AST::ExprPtr fold_logical_not(const AST::Expr &operand)
{
    if (auto *boolExpr = dynamic_cast<const ::il::frontends::basic::BoolExpr *>(&operand))
    {
        auto out = std::make_unique<::il::frontends::basic::BoolExpr>();
        out->value = !boolExpr->value;
        return out;
    }
    if (auto numeric = numeric_from_expr(operand))
    {
        if (numeric->isFloat)
            return nullptr;
        auto out = std::make_unique<::il::frontends::basic::IntExpr>();
        out->value = numeric->i == 0 ? 1 : 0;
        return out;
    }
    return nullptr;
}

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

bool is_short_circuit(AST::BinaryExpr::Op op)
{
    return op == AST::BinaryExpr::Op::LogicalAndShort || op == AST::BinaryExpr::Op::LogicalOrShort;
}

AST::ExprPtr fold_boolean_binary(const AST::Expr &lhs, AST::BinaryExpr::Op op, const AST::Expr &rhs)
{
    auto *lhsBool = dynamic_cast<const ::il::frontends::basic::BoolExpr *>(&lhs);
    auto *rhsBool = dynamic_cast<const ::il::frontends::basic::BoolExpr *>(&rhs);
    if (!lhsBool || !rhsBool)
        return nullptr;

    auto out = std::make_unique<::il::frontends::basic::BoolExpr>();
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
        bool swapped = (op == AST::BinaryExpr::Op::LogicalAnd || op == AST::BinaryExpr::Op::LogicalAndShort)
                           ? (right.i != 0 && left.i != 0)
                           : (right.i != 0 || left.i != 0);
        assert(result == swapped);
    }
#endif
    return make_int_constant(result);
}

} // namespace il::frontends::basic::constfold
