//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/pascal/Lowerer_Expr.cpp
// Purpose: Expression lowering dispatcher and literals for Pascal AST to IL.
// Key invariants: Produces valid IL values with proper typing.
// Ownership/Lifetime: Part of Lowerer; operates on borrowed AST.
//
// Note: This file contains the main expression dispatcher and literal lowering.
// Other expression lowering is split into:
//   - Lowerer_Expr_Name.cpp   (name resolution)
//   - Lowerer_Expr_Ops.cpp    (unary/binary operations)
//   - Lowerer_Expr_Call.cpp   (function/method calls)
//   - Lowerer_Expr_Access.cpp (field/index access)
//
//===----------------------------------------------------------------------===//

#include "frontends/common/CharUtils.hpp"
#include "frontends/pascal/Lowerer.hpp"

namespace il::frontends::pascal
{

using common::char_utils::toLowercase;

inline std::string toLower(const std::string &s)
{
    return toLowercase(s);
}

//===----------------------------------------------------------------------===//
// Expression Lowering Dispatcher
//===----------------------------------------------------------------------===//

LowerResult Lowerer::lowerExpr(const Expr &expr)
{
    switch (expr.kind)
    {
        case ExprKind::IntLiteral:
            return lowerIntLiteral(static_cast<const IntLiteralExpr &>(expr));
        case ExprKind::RealLiteral:
            return lowerRealLiteral(static_cast<const RealLiteralExpr &>(expr));
        case ExprKind::StringLiteral:
            return lowerStringLiteral(static_cast<const StringLiteralExpr &>(expr));
        case ExprKind::BoolLiteral:
            return lowerBoolLiteral(static_cast<const BoolLiteralExpr &>(expr));
        case ExprKind::NilLiteral:
            return lowerNilLiteral(static_cast<const NilLiteralExpr &>(expr));
        case ExprKind::Name:
            return lowerName(static_cast<const NameExpr &>(expr));
        case ExprKind::Unary:
            return lowerUnary(static_cast<const UnaryExpr &>(expr));
        case ExprKind::Binary:
            return lowerBinary(static_cast<const BinaryExpr &>(expr));
        case ExprKind::Call:
            return lowerCall(static_cast<const CallExpr &>(expr));
        case ExprKind::Index:
            return lowerIndex(static_cast<const IndexExpr &>(expr));
        case ExprKind::Field:
            return lowerField(static_cast<const FieldExpr &>(expr));
        case ExprKind::Is:
        {
            const auto &isExpr = static_cast<const IsExpr &>(expr);
            // Lower operand
            LowerResult obj = lowerExpr(*isExpr.operand);
            // Resolve target type via semantic analyzer
            il::frontends::pascal::PasType target = sema_->resolveType(*isExpr.targetType);
            // Default: false
            Value result = Value::constBool(false);
            if (target.kind == PasTypeKind::Class)
            {
                // Lookup class id
                int64_t classId = 0;
                auto it = classLayouts_.find(toLower(target.name));
                if (it != classLayouts_.end())
                    classId = it->second.classId;
                usedExterns_.insert("rt_cast_as");
                Value casted = emitCallRet(
                    Type(Type::Kind::Ptr), "rt_cast_as", {obj.value, Value::constInt(classId)});
                // Compare ptr != null -> i1
                result = emitBinary(Opcode::ICmpNe, Type(Type::Kind::I1), casted, Value::null());
            }
            else if (target.kind == PasTypeKind::Interface)
            {
                // If/when interfaces have ids, use rt_cast_as_iface; for now fall back to false
                usedExterns_.insert("rt_cast_as_iface");
                // Without interface ids wired here, result remains false
            }
            return {result, Type(Type::Kind::I1)};
        }
        case ExprKind::As:
        {
            const auto &asExpr = static_cast<const AsExpr &>(expr);
            // Lower operand
            LowerResult obj = lowerExpr(*asExpr.operand);
            // Resolve target type via semantic analyzer
            il::frontends::pascal::PasType target = sema_->resolveType(*asExpr.targetType);
            // Default: null pointer
            Value result = Value::null();
            if (target.kind == PasTypeKind::Class)
            {
                // Lookup class id
                int64_t classId = 0;
                auto it = classLayouts_.find(toLower(target.name));
                if (it != classLayouts_.end())
                    classId = it->second.classId;
                usedExterns_.insert("rt_cast_as");
                // rt_cast_as returns the object ptr if valid, null otherwise
                result = emitCallRet(
                    Type(Type::Kind::Ptr), "rt_cast_as", {obj.value, Value::constInt(classId)});
            }
            else if (target.kind == PasTypeKind::Interface)
            {
                // If/when interfaces have ids, use rt_cast_as_iface
                usedExterns_.insert("rt_cast_as_iface");
                // Without interface ids wired here, result remains null
            }
            return {result, Type(Type::Kind::Ptr)};
        }
        default:
            // Unsupported expression type - return zero
            return {Value::constInt(0), Type(Type::Kind::I64)};
    }
}

//===----------------------------------------------------------------------===//
// Literal Lowering
//===----------------------------------------------------------------------===//

LowerResult Lowerer::lowerIntLiteral(const IntLiteralExpr &expr)
{
    return {Value::constInt(expr.value), Type(Type::Kind::I64)};
}

LowerResult Lowerer::lowerRealLiteral(const RealLiteralExpr &expr)
{
    return {Value::constFloat(expr.value), Type(Type::Kind::F64)};
}

LowerResult Lowerer::lowerStringLiteral(const StringLiteralExpr &expr)
{
    std::string globalName = getStringGlobal(expr.value);
    Value strVal = emitConstStr(globalName);
    return {strVal, Type(Type::Kind::Str)};
}

LowerResult Lowerer::lowerBoolLiteral(const BoolLiteralExpr &expr)
{
    return {Value::constBool(expr.value), Type(Type::Kind::I1)};
}

LowerResult Lowerer::lowerNilLiteral(const NilLiteralExpr &)
{
    return {Value::null(), Type(Type::Kind::Ptr)};
}

} // namespace il::frontends::pascal
