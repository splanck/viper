//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
// File: src/frontends/basic/lower/Lower_Expr_NumericClassifier.cpp
// Purpose: Implements the numeric type classification helper for BASIC
//          expression lowering. Determines the result type of numeric
//          expressions according to BASIC type promotion rules.
// Key invariants: Classification follows QBasic/GW-BASIC type promotion rules.
// Ownership/Lifetime: Operates on borrowed Lowerer reference.
// Links: docs/codemap.md, docs/basic-language.md
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/Lowerer.hpp"
#include "frontends/basic/SemanticAnalyzer.hpp"
#include "frontends/basic/TypeSuffix.hpp"

#include <limits>

namespace il::frontends::basic
{

using NumericType = TypeRules::NumericType;
using AstType = ::il::frontends::basic::Type;
using IlType = il::core::Type;

/// @brief Visitor that classifies BASIC expressions into numeric type categories.
/// @details Walks an expression tree to determine its resulting numeric type,
///          following QBasic/GW-BASIC type promotion rules. This is used by
///          lowering to select the appropriate IL operations and coercions.
class NumericTypeClassifier final : public ExprVisitor
{
  public:
    explicit NumericTypeClassifier(Lowerer &lowerer) noexcept : lowerer_(lowerer) {}

    NumericType result() const noexcept
    {
        return result_;
    }

    void visit(const IntExpr &i) override
    {
        switch (i.suffix)
        {
            case IntExpr::Suffix::Integer:
                result_ = NumericType::Integer;
                return;
            case IntExpr::Suffix::Long:
                result_ = NumericType::Long;
                return;
            case IntExpr::Suffix::None:
                break;
        }

        const long long value = i.value;
        if (value >= std::numeric_limits<int16_t>::min() &&
            value <= std::numeric_limits<int16_t>::max())
        {
            result_ = NumericType::Integer;
        }
        else
        {
            result_ = NumericType::Long;
        }
    }

    void visit(const FloatExpr &f) override
    {
        result_ =
            (f.suffix == FloatExpr::Suffix::Single) ? NumericType::Single : NumericType::Double;
    }

    void visit(const StringExpr &) override
    {
        result_ = NumericType::Double;
    }

    void visit(const BoolExpr &) override
    {
        result_ = NumericType::Integer;
    }

    void visit(const VarExpr &var) override
    {
        // BUG-019 fix: Check semantic analysis first for CONST float types
        AstType effectiveType = AstType::I64;
        bool hasEffectiveType = false;

        if (lowerer_.semanticAnalyzer())
        {
            if (auto semaType = lowerer_.semanticAnalyzer()->lookupVarType(std::string{var.name}))
            {
                using SemaType = SemanticAnalyzer::Type;
                switch (*semaType)
                {
                    case SemaType::Float:
                        effectiveType = AstType::F64;
                        hasEffectiveType = true;
                        break;
                    case SemaType::Int:
                        effectiveType = AstType::I64;
                        hasEffectiveType = true;
                        break;
                    default:
                        break;
                }
            }
        }

        if (const auto *info = lowerer_.findSymbol(var.name))
        {
            if (info->hasType && !hasEffectiveType)
            {
                effectiveType = info->type;
                hasEffectiveType = true;
            }
        }

        if (hasEffectiveType)
        {
            if (effectiveType == AstType::F64)
            {
                if (!var.name.empty())
                {
                    switch (var.name.back())
                    {
                        case '!':
                            result_ = NumericType::Single;
                            return;
                        case '#':
                            result_ = NumericType::Double;
                            return;
                        default:
                            break;
                    }
                }
                result_ = NumericType::Double;
                return;
            }
            if (!var.name.empty())
            {
                switch (var.name.back())
                {
                    case '%':
                        result_ = NumericType::Integer;
                        return;
                    case '&':
                        result_ = NumericType::Long;
                        return;
                    default:
                        break;
                }
            }
            result_ = NumericType::Long;
            return;
        }

        if (!var.name.empty())
        {
            switch (var.name.back())
            {
                case '!':
                    result_ = NumericType::Single;
                    return;
                case '#':
                    result_ = NumericType::Double;
                    return;
                case '%':
                    result_ = NumericType::Integer;
                    return;
                case '&':
                    result_ = NumericType::Long;
                    return;
                default:
                    break;
            }
        }

        AstType astTy = inferAstTypeFromName(var.name);
        result_ = (astTy == AstType::F64) ? NumericType::Double : NumericType::Long;
    }

    void visit(const ArrayExpr &) override
    {
        result_ = NumericType::Long;
    }

    void visit(const UnaryExpr &un) override
    {
        if (!un.expr)
        {
            result_ = NumericType::Long;
            return;
        }
        result_ = lowerer_.classifyNumericType(*un.expr);
    }

    void visit(const BinaryExpr &bin) override
    {
        if (!bin.lhs || !bin.rhs)
        {
            result_ = NumericType::Long;
            return;
        }

        NumericType lhsTy = lowerer_.classifyNumericType(*bin.lhs);
        NumericType rhsTy = lowerer_.classifyNumericType(*bin.rhs);

        switch (bin.op)
        {
            case BinaryExpr::Op::Add:
                result_ = TypeRules::resultType('+', lhsTy, rhsTy);
                return;
            case BinaryExpr::Op::Sub:
                result_ = TypeRules::resultType('-', lhsTy, rhsTy);
                return;
            case BinaryExpr::Op::Mul:
                result_ = TypeRules::resultType('*', lhsTy, rhsTy);
                return;
            case BinaryExpr::Op::Div:
                result_ = TypeRules::resultType('/', lhsTy, rhsTy);
                return;
            case BinaryExpr::Op::IDiv:
                result_ = TypeRules::resultType('\\', lhsTy, rhsTy);
                return;
            case BinaryExpr::Op::Mod:
                result_ = TypeRules::resultType("MOD", lhsTy, rhsTy);
                return;
            case BinaryExpr::Op::Pow:
                result_ = TypeRules::resultType('^', lhsTy, rhsTy);
                return;
            default:
                result_ = NumericType::Long;
                return;
        }
    }

    void visit(const BuiltinCallExpr &call) override
    {
        switch (call.builtin)
        {
            case BuiltinCallExpr::Builtin::Cint:
                result_ = NumericType::Integer;
                return;
            case BuiltinCallExpr::Builtin::Clng:
                result_ = NumericType::Long;
                return;
            case BuiltinCallExpr::Builtin::Csng:
                result_ = NumericType::Single;
                return;
            case BuiltinCallExpr::Builtin::Cdbl:
                result_ = NumericType::Double;
                return;
            // BUG-OOP-016 fix: Int, Fix, Floor, Ceil, Abs return integers
            case BuiltinCallExpr::Builtin::Int:
            case BuiltinCallExpr::Builtin::Fix:
            case BuiltinCallExpr::Builtin::Floor:
            case BuiltinCallExpr::Builtin::Ceil:
            case BuiltinCallExpr::Builtin::Abs:
                result_ = NumericType::Long;
                return;
            case BuiltinCallExpr::Builtin::Round:
            case BuiltinCallExpr::Builtin::Sqr:
            case BuiltinCallExpr::Builtin::Sin:
            case BuiltinCallExpr::Builtin::Cos:
            case BuiltinCallExpr::Builtin::Pow:
            case BuiltinCallExpr::Builtin::Rnd:
            case BuiltinCallExpr::Builtin::Val:
                result_ = NumericType::Double;
                return;
            case BuiltinCallExpr::Builtin::Str:
                if (!call.args.empty() && call.args[0])
                {
                    result_ = lowerer_.classifyNumericType(*call.args[0]);
                }
                else
                {
                    result_ = NumericType::Long;
                }
                return;
            default:
                result_ = NumericType::Double;
                return;
        }
    }

    void visit(const LBoundExpr &) override
    {
        result_ = NumericType::Long;
    }

    void visit(const UBoundExpr &) override
    {
        result_ = NumericType::Long;
    }

    void visit(const CallExpr &callExpr) override
    {
        if (const auto *sig = lowerer_.findProcSignature(callExpr.callee))
        {
            switch (sig->retType.kind)
            {
                case IlType::Kind::I16:
                    result_ = NumericType::Integer;
                    return;
                case IlType::Kind::I32:
                case IlType::Kind::I64:
                    result_ = NumericType::Long;
                    return;
                case IlType::Kind::F64:
                    result_ = NumericType::Double;
                    return;
                default:
                    break;
            }
        }
        result_ = NumericType::Long;
    }

    void visit(const NewExpr &) override
    {
        result_ = NumericType::Long;
    }

    void visit(const MeExpr &) override
    {
        result_ = NumericType::Long;
    }

    void visit(const MemberAccessExpr &) override
    {
        result_ = NumericType::Long;
    }

    void visit(const MethodCallExpr &) override
    {
        result_ = NumericType::Long;
    }

    void visit(const IsExpr &) override
    {
        // Boolean result
        result_ = NumericType::Long;
    }

    void visit(const AsExpr &as) override
    {
        // Classify underlying value
        if (as.value)
            result_ = lowerer_.classifyNumericType(*as.value);
        else
            result_ = NumericType::Long;
    }

  private:
    Lowerer &lowerer_;
    NumericType result_{NumericType::Long};
};

/// @brief Classify an expression's numeric result type.
/// @details Uses a visitor to walk the expression tree and determine what
///          numeric type the expression will produce, following BASIC type
///          promotion rules.
/// @param expr Expression to classify.
/// @return The numeric type category of the expression result.
TypeRules::NumericType Lowerer::classifyNumericType(const Expr &expr)
{
    NumericTypeClassifier classifier(*this);
    expr.accept(classifier);
    return classifier.result();
}

} // namespace il::frontends::basic
