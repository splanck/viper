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

OopLoweringHelper::OopLoweringHelper(Lowerer &lowerer) noexcept : lowerer_(lowerer) {}

RVal OopLoweringHelper::lowerNewExpr(const NewExpr &expr)
{
    return lowerer_.lowerNewExpr(expr);
}

RVal OopLoweringHelper::lowerNewExpr(const NewExpr &expr, OopLoweringContext &ctx)
{
    return lowerer_.lowerNewExpr(expr, ctx);
}

RVal OopLoweringHelper::lowerMeExpr(const MeExpr &expr)
{
    return lowerer_.lowerMeExpr(expr);
}

RVal OopLoweringHelper::lowerMeExpr(const MeExpr &expr, OopLoweringContext &ctx)
{
    return lowerer_.lowerMeExpr(expr, ctx);
}

RVal OopLoweringHelper::lowerMemberAccessExpr(const MemberAccessExpr &expr)
{
    return lowerer_.lowerMemberAccessExpr(expr);
}

RVal OopLoweringHelper::lowerMemberAccessExpr(const MemberAccessExpr &expr, OopLoweringContext &ctx)
{
    return lowerer_.lowerMemberAccessExpr(expr, ctx);
}

RVal OopLoweringHelper::lowerMethodCallExpr(const MethodCallExpr &expr)
{
    return lowerer_.lowerMethodCallExpr(expr);
}

RVal OopLoweringHelper::lowerMethodCallExpr(const MethodCallExpr &expr, OopLoweringContext &ctx)
{
    return lowerer_.lowerMethodCallExpr(expr, ctx);
}

void OopLoweringHelper::lowerDelete(const DeleteStmt &stmt)
{
    lowerer_.lowerDelete(stmt);
}

void OopLoweringHelper::lowerDelete(const DeleteStmt &stmt, OopLoweringContext &ctx)
{
    lowerer_.lowerDelete(stmt, ctx);
}

void OopLoweringHelper::scanOOP(const Program &prog)
{
    lowerer_.scanOOP(prog);
}

void OopLoweringHelper::emitOopDeclsAndBodies(const Program &prog)
{
    lowerer_.emitOopDeclsAndBodies(prog);
}

} // namespace il::frontends::basic::lower::detail
