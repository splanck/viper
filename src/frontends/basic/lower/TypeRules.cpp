// File: src/frontends/basic/lower/TypeRules.cpp
// Purpose: Implements numeric classification helpers used during lowering.
// Key invariants: Delegates to frontend TypeRules for operator semantics.
// Ownership/Lifetime: Stateless; invoked on demand by Lowerer helpers.
// Links: docs/codemap.md

#include "frontends/basic/lower/TypeRules.hpp"

#include "frontends/basic/Lowerer.hpp"
#include "frontends/basic/ast/ExprNodes.hpp"

namespace il::frontends::basic::lower
{

using NumericType = TypeRules::NumericType;

NumericType classifyBinaryNumericResult(const BinaryExpr &bin,
                                        NumericType lhs,
                                        NumericType rhs) noexcept
{
    switch (bin.op)
    {
        case BinaryExpr::Op::Add:
            return TypeRules::resultType('+', lhs, rhs);
        case BinaryExpr::Op::Sub:
            return TypeRules::resultType('-', lhs, rhs);
        case BinaryExpr::Op::Mul:
            return TypeRules::resultType('*', lhs, rhs);
        case BinaryExpr::Op::Div:
            return TypeRules::resultType('/', lhs, rhs);
        case BinaryExpr::Op::IDiv:
            return TypeRules::resultType('\\', lhs, rhs);
        case BinaryExpr::Op::Mod:
            return TypeRules::resultType("MOD", lhs, rhs);
        case BinaryExpr::Op::Pow:
            return TypeRules::resultType('^', lhs, rhs);
        default:
            return NumericType::Long;
    }
}

NumericType classifyBuiltinCall(const BuiltinCallExpr &call,
                                std::optional<NumericType> firstArgType)
{
    using Builtin = BuiltinCallExpr::Builtin;
    switch (call.builtin)
    {
        case Builtin::Cint:
            return NumericType::Integer;
        case Builtin::Clng:
            return NumericType::Long;
        case Builtin::Csng:
            return NumericType::Single;
        case Builtin::Cdbl:
            return NumericType::Double;
        case Builtin::Int:
        case Builtin::Fix:
        case Builtin::Round:
        case Builtin::Sqr:
        case Builtin::Abs:
        case Builtin::Floor:
        case Builtin::Ceil:
        case Builtin::Sin:
        case Builtin::Cos:
        case Builtin::Pow:
        case Builtin::Rnd:
        case Builtin::Val:
            return NumericType::Double;
        case Builtin::Str:
            if (firstArgType)
                return *firstArgType;
            return NumericType::Long;
        default:
            return NumericType::Double;
    }
}

NumericType classifyProcedureCall(const Lowerer &lowerer, const CallExpr &call)
{
    if (const auto *sig = lowerer.findProcSignature(call.callee))
    {
        switch (sig->retType.kind)
        {
            case il::core::Type::Kind::I16:
                return NumericType::Integer;
            case il::core::Type::Kind::I32:
            case il::core::Type::Kind::I64:
                return NumericType::Long;
            case il::core::Type::Kind::F64:
                return NumericType::Double;
            default:
                break;
        }
    }
    return NumericType::Long;
}

} // namespace il::frontends::basic::lower

