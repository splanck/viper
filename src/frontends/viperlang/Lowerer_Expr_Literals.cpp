//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file Lowerer_Expr_Literals.cpp
/// @brief Literal expression lowering for the ViperLang IL lowerer.
///
//===----------------------------------------------------------------------===//

#include "frontends/viperlang/Lowerer.hpp"

namespace il::frontends::viperlang
{

//=============================================================================
// Literal Expression Lowering
//=============================================================================

LowerResult Lowerer::lowerIntLiteral(IntLiteralExpr *expr)
{
    return {Value::constInt(expr->value), Type(Type::Kind::I64)};
}

LowerResult Lowerer::lowerNumberLiteral(NumberLiteralExpr *expr)
{
    return {Value::constFloat(expr->value), Type(Type::Kind::F64)};
}

LowerResult Lowerer::lowerStringLiteral(StringLiteralExpr *expr)
{
    std::string globalName = getStringGlobal(expr->value);
    Value val = emitConstStr(globalName);
    return {val, Type(Type::Kind::Str)};
}

LowerResult Lowerer::lowerBoolLiteral(BoolLiteralExpr *expr)
{
    return {Value::constBool(expr->value), Type(Type::Kind::I1)};
}

LowerResult Lowerer::lowerNullLiteral(NullLiteralExpr * /*expr*/)
{
    return {Value::null(), Type(Type::Kind::Ptr)};
}

} // namespace il::frontends::viperlang
