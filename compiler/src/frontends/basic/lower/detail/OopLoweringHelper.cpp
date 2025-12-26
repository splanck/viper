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

RVal OopLoweringHelper::lowerNewExpr(const NewExpr &expr)
{
    return access_.lowerNewExpr(expr);
}

RVal OopLoweringHelper::lowerNewExpr(const NewExpr &expr, OopLoweringContext &ctx)
{
    return access_.lowerNewExpr(expr, ctx);
}

RVal OopLoweringHelper::lowerMeExpr(const MeExpr &expr)
{
    return access_.lowerMeExpr(expr);
}

RVal OopLoweringHelper::lowerMeExpr(const MeExpr &expr, OopLoweringContext &ctx)
{
    return access_.lowerMeExpr(expr, ctx);
}

RVal OopLoweringHelper::lowerMemberAccessExpr(const MemberAccessExpr &expr)
{
    return access_.lowerMemberAccessExpr(expr);
}

RVal OopLoweringHelper::lowerMemberAccessExpr(const MemberAccessExpr &expr, OopLoweringContext &ctx)
{
    return access_.lowerMemberAccessExpr(expr, ctx);
}

RVal OopLoweringHelper::lowerMethodCallExpr(const MethodCallExpr &expr)
{
    return access_.lowerMethodCallExpr(expr);
}

RVal OopLoweringHelper::lowerMethodCallExpr(const MethodCallExpr &expr, OopLoweringContext &ctx)
{
    return access_.lowerMethodCallExpr(expr, ctx);
}

void OopLoweringHelper::lowerDelete(const DeleteStmt &stmt)
{
    access_.lowerDelete(stmt);
}

void OopLoweringHelper::lowerDelete(const DeleteStmt &stmt, OopLoweringContext &ctx)
{
    access_.lowerDelete(stmt, ctx);
}

void OopLoweringHelper::scanOOP(const Program &prog)
{
    access_.scanOOP(prog);
}

void OopLoweringHelper::emitOopDeclsAndBodies(const Program &prog)
{
    access_.emitOopDeclsAndBodies(prog);
}

} // namespace il::frontends::basic::lower::detail
