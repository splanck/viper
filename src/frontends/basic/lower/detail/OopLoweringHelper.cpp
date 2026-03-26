//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE in the project root for license information.
//
// File: src/frontends/basic/lower/detail/OopLoweringHelper.cpp
//
// Summary:
//   Implements OopLoweringHelper which coordinates OOP construct lowering.
//   This helper delegates to the Lowerer's OOP methods while providing
//   a unified interface for class, method, and object operations.
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/Lowerer.hpp"
#include "frontends/basic/OopLoweringContext.hpp"
#include "frontends/basic/lower/detail/LowererDetail.hpp"

namespace il::frontends::basic::lower::detail
{

OopLoweringHelper::OopLoweringHelper(Lowerer::DetailAccess access) noexcept : access_(access) {}

/// @brief Lower New Expr.
RVal OopLoweringHelper::lowerNewExpr(const NewExpr &expr)
{
    return access_.lowerNewExpr(expr);
}

/// @brief Lower New Expr.
RVal OopLoweringHelper::lowerNewExpr(const NewExpr &expr, OopLoweringContext &ctx)
{
    return access_.lowerNewExpr(expr, ctx);
}

/// @brief Lower Me Expr.
RVal OopLoweringHelper::lowerMeExpr(const MeExpr &expr)
{
    return access_.lowerMeExpr(expr);
}

/// @brief Lower Me Expr.
RVal OopLoweringHelper::lowerMeExpr(const MeExpr &expr, OopLoweringContext &ctx)
{
    return access_.lowerMeExpr(expr, ctx);
}

/// @brief Lower Member Access Expr.
RVal OopLoweringHelper::lowerMemberAccessExpr(const MemberAccessExpr &expr)
{
    return access_.lowerMemberAccessExpr(expr);
}

/// @brief Lower Member Access Expr.
RVal OopLoweringHelper::lowerMemberAccessExpr(const MemberAccessExpr &expr, OopLoweringContext &ctx)
{
    return access_.lowerMemberAccessExpr(expr, ctx);
}

/// @brief Lower Method Call Expr.
RVal OopLoweringHelper::lowerMethodCallExpr(const MethodCallExpr &expr)
{
    return access_.lowerMethodCallExpr(expr);
}

/// @brief Lower Method Call Expr.
RVal OopLoweringHelper::lowerMethodCallExpr(const MethodCallExpr &expr, OopLoweringContext &ctx)
{
    return access_.lowerMethodCallExpr(expr, ctx);
}

/// @brief Lower Delete.
void OopLoweringHelper::lowerDelete(const DeleteStmt &stmt)
{
    access_.lowerDelete(stmt);
}

/// @brief Lower Delete.
void OopLoweringHelper::lowerDelete(const DeleteStmt &stmt, OopLoweringContext &ctx)
{
    access_.lowerDelete(stmt, ctx);
}

/// @brief Scan OOP.
void OopLoweringHelper::scanOOP(const Program &prog)
{
    access_.scanOOP(prog);
}

/// @brief Emit Oop Decls And Bodies.
void OopLoweringHelper::emitOopDeclsAndBodies(const Program &prog)
{
    access_.emitOopDeclsAndBodies(prog);
}

} // namespace il::frontends::basic::lower::detail
