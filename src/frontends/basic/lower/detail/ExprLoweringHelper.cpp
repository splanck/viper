//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE in the project root for license information.
//
// File: src/frontends/basic/lower/detail/ExprLoweringHelper.cpp
//
// Summary:
//   Implements ExprLoweringHelper which coordinates expression lowering
//   operations. This helper delegates to existing expression lowering functions
//   (NumericExprLowering, LogicalExprLowering, BuiltinExprLowering) while
//   providing a unified interface for the Lowerer class.
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/lower/detail/LowererDetail.hpp"
#include "frontends/basic/LowerExprBuiltin.hpp"
#include "frontends/basic/LowerExprLogical.hpp"
#include "frontends/basic/LowerExprNumeric.hpp"
#include "frontends/basic/Lowerer.hpp"

namespace il::frontends::basic::lower::detail
{

ExprLoweringHelper::ExprLoweringHelper(Lowerer &lowerer) noexcept : lowerer_(lowerer) {}

RVal ExprLoweringHelper::lowerVarExpr(const VarExpr &expr)
{
    return lowerer_.lowerVarExpr(expr);
}

RVal ExprLoweringHelper::lowerUnaryExpr(const UnaryExpr &expr)
{
    return lowerer_.lowerUnaryExpr(expr);
}

RVal ExprLoweringHelper::lowerBinaryExpr(const BinaryExpr &expr)
{
    return lowerer_.lowerBinaryExpr(expr);
}

RVal ExprLoweringHelper::lowerBuiltinCall(const BuiltinCallExpr &expr)
{
    return ::il::frontends::basic::lowerBuiltinCall(lowerer_, expr);
}

RVal ExprLoweringHelper::lowerUBoundExpr(const UBoundExpr &expr)
{
    return lowerer_.lowerUBoundExpr(expr);
}

RVal ExprLoweringHelper::lowerLogicalBinary(const BinaryExpr &expr)
{
    return ::il::frontends::basic::lowerLogicalBinary(lowerer_, expr);
}

RVal ExprLoweringHelper::lowerDivOrMod(const BinaryExpr &expr)
{
    return ::il::frontends::basic::lowerDivOrMod(lowerer_, expr);
}

RVal ExprLoweringHelper::lowerStringBinary(const BinaryExpr &expr, RVal lhs, RVal rhs)
{
    return ::il::frontends::basic::lowerStringBinary(lowerer_, expr, std::move(lhs), std::move(rhs));
}

RVal ExprLoweringHelper::lowerNumericBinary(const BinaryExpr &expr, RVal lhs, RVal rhs)
{
    return ::il::frontends::basic::lowerNumericBinary(lowerer_, expr, std::move(lhs), std::move(rhs));
}

RVal ExprLoweringHelper::lowerPowBinary(const BinaryExpr &expr, RVal lhs, RVal rhs)
{
    return ::il::frontends::basic::lowerPowBinary(lowerer_, expr, std::move(lhs), std::move(rhs));
}

} // namespace il::frontends::basic::lower::detail
