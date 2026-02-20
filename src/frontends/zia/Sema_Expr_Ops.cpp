//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file Sema_Expr_Ops.cpp
/// @brief Operator expression analysis (binary, unary, ternary) and common
///        type computation for the Zia semantic analyzer.
///
//===----------------------------------------------------------------------===//

#include "frontends/zia/Sema.hpp"

namespace il::frontends::zia
{

/// @brief Analyze a binary expression (e.g., a + b, x == y).
/// @param expr The binary expression node.
/// @return The result type of the operation.
/// @details Handles arithmetic, comparison, logical, bitwise, and assignment operators.
///          Performs type checking and widening for numeric operations.
TypeRef Sema::analyzeBinary(BinaryExpr *expr)
{
    TypeRef leftType = analyzeExpr(expr->left.get());
    TypeRef rightType = analyzeExpr(expr->right.get());

    switch (expr->op)
    {
        case BinaryOp::Add:
        case BinaryOp::Sub:
        case BinaryOp::Mul:
        case BinaryOp::Div:
        case BinaryOp::Mod:
            // Numeric operations
            if (leftType->kind == TypeKindSem::String && expr->op == BinaryOp::Add)
            {
                // String concatenation
                return types::string();
            }
            if (leftType->isNumeric() && rightType->isNumeric())
            {
                // Return wider type
                if (leftType->kind == TypeKindSem::Number || rightType->kind == TypeKindSem::Number)
                    return types::number();
                return types::integer();
            }
            error(expr->loc, "Invalid operands for arithmetic operation");
            return types::unknown();

        case BinaryOp::Eq:
        case BinaryOp::Ne:
        case BinaryOp::Lt:
        case BinaryOp::Le:
        case BinaryOp::Gt:
        case BinaryOp::Ge:
            // Comparison operations
            return types::boolean();

        case BinaryOp::And:
        case BinaryOp::Or:
            // Logical operations
            if (leftType->kind != TypeKindSem::Boolean || rightType->kind != TypeKindSem::Boolean)
            {
                error(expr->loc, "Logical operators require Boolean operands");
            }
            return types::boolean();

        case BinaryOp::BitAnd:
        case BinaryOp::BitOr:
        case BinaryOp::BitXor:
            // Bitwise operations
            if (!leftType->isIntegral() || !rightType->isIntegral())
            {
                error(expr->loc, "Bitwise operators require integral operands");
            }
            return types::integer();

        case BinaryOp::Assign:
            // Assignment - LHS must be assignable, types must be compatible
            // For now, just check that the types are compatible
            if (!rightType->isConvertibleTo(*leftType))
            {
                errorTypeMismatch(expr->loc, leftType, rightType);
            }
            // Assignment expression returns the assigned value
            return leftType;
    }

    return types::unknown();
}

/// @brief Analyze a unary expression (e.g., -x, !flag, ~bits).
/// @param expr The unary expression node.
/// @return The result type of the operation.
/// @details Handles negation, logical not, bitwise not, and address-of operators.
TypeRef Sema::analyzeUnary(UnaryExpr *expr)
{
    TypeRef operandType = analyzeExpr(expr->operand.get());

    switch (expr->op)
    {
        case UnaryOp::Neg:
            if (!operandType->isNumeric())
            {
                error(expr->loc, "Negation requires numeric operand");
            }
            return operandType;

        case UnaryOp::Not:
            if (operandType->kind != TypeKindSem::Boolean)
            {
                error(expr->loc, "Logical not requires Boolean operand");
            }
            return types::boolean();

        case UnaryOp::BitNot:
            if (!operandType->isIntegral())
            {
                error(expr->loc, "Bitwise not requires integral operand");
            }
            return types::integer();

        case UnaryOp::AddressOf:
        {
            // Address-of operator for function references: &funcName
            // The operand must be an identifier referring to a function
            auto *ident = dynamic_cast<IdentExpr *>(expr->operand.get());
            if (!ident)
            {
                error(expr->loc, "Address-of operator requires a function name");
                return types::unknown();
            }

            Symbol *sym = lookupSymbol(ident->name);
            if (!sym)
            {
                error(expr->loc, "Unknown identifier '" + ident->name + "'");
                return types::unknown();
            }

            if (sym->kind != Symbol::Kind::Function && sym->kind != Symbol::Kind::Method)
            {
                error(expr->loc, "Address-of operator requires a function name");
                return types::unknown();
            }

            // Return the function's type (which is already a function type)
            // This allows assignment to function-typed variables
            return sym->type;
        }
    }

    return types::unknown();
}

/// @brief Analyze a ternary conditional expression (cond ? then : else).
/// @param expr The ternary expression node.
/// @return The common type of the then and else branches.
/// @details Validates condition is Boolean and finds common type of branches.
TypeRef Sema::analyzeTernary(TernaryExpr *expr)
{
    TypeRef condType = analyzeExpr(expr->condition.get());
    if (condType->kind != TypeKindSem::Boolean)
    {
        error(expr->condition->loc, "Condition must be Boolean");
    }

    TypeRef thenType = analyzeExpr(expr->thenExpr.get());
    TypeRef elseType = analyzeExpr(expr->elseExpr.get());

    TypeRef resultType = commonType(thenType, elseType);
    if (resultType && resultType->kind != TypeKindSem::Unknown)
        return resultType;

    error(expr->loc, "Incompatible types in ternary expression");
    return types::unknown();
}

/// @brief Analyze an if-expression (`if cond { thenExpr } else { elseExpr }`).
/// @param expr The if-expression AST node.
/// @return The common type of the then and else branches.
TypeRef Sema::analyzeIfExpr(IfExpr *expr)
{
    TypeRef condType = analyzeExpr(expr->condition.get());
    if (condType && condType->kind != TypeKindSem::Boolean && condType->kind != TypeKindSem::Unknown)
    {
        error(expr->condition->loc, "Condition must be Boolean");
    }

    TypeRef thenType = analyzeExpr(expr->thenBranch.get());
    TypeRef elseType = analyzeExpr(expr->elseBranch.get());

    TypeRef resultType = commonType(thenType, elseType);
    if (resultType && resultType->kind != TypeKindSem::Unknown)
        return resultType;

    // If one branch is unknown, return the other — avoids spurious errors on null branches
    if (thenType && thenType->kind != TypeKindSem::Unknown)
        return thenType;
    if (elseType && elseType->kind != TypeKindSem::Unknown)
        return elseType;

    error(expr->loc, "Incompatible types in if-expression");
    return types::unknown();
}

/// @brief Compute the common type of two types for type unification.
/// @param lhs The first type.
/// @param rhs The second type.
/// @return The most general type compatible with both, or Unknown if incompatible.
/// @details Handles numeric widening, optional lifting, and subtype relationships.
TypeRef Sema::commonType(TypeRef lhs, TypeRef rhs)
{
    if (!lhs && !rhs)
        return types::unknown();
    if (!lhs)
        return rhs;
    if (!rhs)
        return lhs;
    if (lhs->kind == TypeKindSem::Unknown)
        return rhs;
    if (rhs->kind == TypeKindSem::Unknown)
        return lhs;

    if (lhs->kind == TypeKindSem::Optional || rhs->kind == TypeKindSem::Optional)
    {
        TypeRef innerL = lhs->kind == TypeKindSem::Optional ? lhs->innerType() : lhs;
        TypeRef innerR = rhs->kind == TypeKindSem::Optional ? rhs->innerType() : rhs;
        TypeRef inner = commonType(innerL, innerR);
        return types::optional(inner ? inner : types::unknown());
    }

    if (lhs->isNumeric() && rhs->isNumeric())
    {
        if (lhs->kind == TypeKindSem::Number || rhs->kind == TypeKindSem::Number)
            return types::number();
        if (lhs->kind == TypeKindSem::Integer || rhs->kind == TypeKindSem::Integer)
            return types::integer();
        return types::byte();
    }

    if (lhs->isAssignableFrom(*rhs))
        return lhs;
    if (rhs->isAssignableFrom(*lhs))
        return rhs;

    return types::unknown();
}

} // namespace il::frontends::zia
