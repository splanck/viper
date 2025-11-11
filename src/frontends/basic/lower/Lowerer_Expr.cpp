//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
// File: src/frontends/basic/lower/Lowerer_Expr.cpp
// Purpose: Implements the expression lowering visitor wiring that bridges BASIC
//          AST nodes to the shared Lowerer helpers.
// Key invariants: Expression visitors honour the Lowerer context and never
//                 mutate ownership of AST nodes.
// Ownership/Lifetime: Operates on a borrowed Lowerer instance; AST nodes remain
//                     owned by the caller.
// Links: docs/codemap.md, docs/basic-language.md
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/LowerExprBuiltin.hpp"
#include "frontends/basic/LowerExprLogical.hpp"
#include "frontends/basic/LowerExprNumeric.hpp"
#include "frontends/basic/Lowerer.hpp"
#include "frontends/basic/TypeSuffix.hpp"
#include "frontends/basic/lower/AstVisitor.hpp"

#include "viper/il/Module.hpp"

#include <limits>
#include <utility>
#include <vector>

namespace il::frontends::basic
{

using IlType = il::core::Type;
using IlValue = il::core::Value;

/// @brief Visitor that lowers BASIC expressions using Lowerer helpers.
/// @details The visitor implements the generated @ref ExprVisitor interface and
///          redirects each AST node type to the specialised lowering helpers on
///          @ref Lowerer. The instance carries a reference to the current
///          lowering context so it can update source locations, perform type
///          coercions, and capture the produced IL value for the caller.
class LowererExprVisitor final : public lower::AstVisitor, public ExprVisitor
{
  public:
    /// @brief Construct a visitor that records results into @p lowerer.
    /// @details The visitor only borrows the lowering context; ownership of the
    ///          surrounding compiler state remains with the caller.
    /// @param lowerer Lowering context that exposes shared helper routines.
    explicit LowererExprVisitor(Lowerer &lowerer) noexcept : lowerer_(lowerer) {}

    /// @brief Dispatch an expression node to the corresponding visit method.
    /// @details Updates the result cache by forwarding the call to
    ///          @ref Expr::accept, allowing dynamic dispatch over the concrete
    ///          expression type.
    /// @param expr Expression node that should be lowered into IL form.
    void visitExpr(const Expr &expr) override
    {
        expr.accept(*this);
    }

    /// @brief Ignore statement nodes encountered through the generic visitor.
    /// @details Expression lowering never acts on statements, so the override is
    ///          a no-op that satisfies the @ref lower::AstVisitor contract.
    void visitStmt(const Stmt &) override {}

    /// @brief Lower an integer literal expression.
    /// @details Captures the literal value as a 64-bit IL constant and records
    ///          the associated source location in the lowering context.
    /// @param expr Integer literal node from the BASIC AST.
    void visit(const IntExpr &expr) override
    {
        lowerer_.curLoc = expr.loc;
        result_ = Lowerer::RVal{IlValue::constInt(expr.value), IlType(IlType::Kind::I64)};
    }

    /// @brief Lower a floating-point literal expression.
    /// @details Emits an IL constant containing the literal value and tags it
    ///          with the 64-bit floating-point type descriptor.
    /// @param expr Floating-point literal node from the BASIC AST.
    void visit(const FloatExpr &expr) override
    {
        lowerer_.curLoc = expr.loc;
        result_ = Lowerer::RVal{IlValue::constFloat(expr.value), IlType(IlType::Kind::F64)};
    }

    /// @brief Lower a string literal expression.
    /// @details Interns the string in the module's constant pool, emits a load
    ///          of the retained runtime string handle, and records the result
    ///          with the IL string type.
    /// @param expr String literal node from the BASIC AST.
    void visit(const StringExpr &expr) override
    {
        lowerer_.curLoc = expr.loc;
        std::string lbl = lowerer_.getStringLabel(expr.value);
        IlValue tmp = lowerer_.emitConstStr(lbl);
        result_ = Lowerer::RVal{tmp, IlType(IlType::Kind::Str)};
    }

    /// @brief Lower a boolean literal expression.
    /// @details Emits the VM's canonical predicate constant (`1` for true,
    ///          `0` for false) while recording the IL boolean type. Numeric
    ///          contexts will later widen the predicate through
    ///          @ref Lowerer::coerceToI64 when BASIC expects `-1/0` values.
    /// @param expr Boolean literal node from the BASIC AST.
    void visit(const BoolExpr &expr) override
    {
        lowerer_.curLoc = expr.loc;
        IlValue flag = lowerer_.emitBoolConst(expr.value);
        result_ = Lowerer::RVal{flag, lowerer_.ilBoolTy()};
    }

    /// @brief Lower a variable reference expression.
    /// @details Defers to @ref Lowerer::lowerVarExpr so storage class specific
    ///          logic (such as locals versus fields) is centralised in the
    ///          lowering helpers.
    /// @param expr Variable reference node from the BASIC AST.
    void visit(const VarExpr &expr) override
    {
        result_ = lowerer_.lowerVarExpr(expr);
    }

    /// @brief Lower an array access expression.
    /// @details Computes the base pointer and index using
    ///          @ref Lowerer::lowerArrayAccess, then emits a runtime call to load
    ///          the indexed element. The result is tagged as a 64-bit integer
    ///          because BASIC integer arrays currently map to that representation.
    /// @param expr Array access node from the BASIC AST.
    void visit(const ArrayExpr &expr) override
    {
        Lowerer::ArrayAccess access =
            lowerer_.lowerArrayAccess(expr, Lowerer::ArrayAccessKind::Load);
        lowerer_.curLoc = expr.loc;
        IlValue val = lowerer_.emitCallRet(
            IlType(IlType::Kind::I64), "rt_arr_i32_get", {access.base, access.index});
        result_ = Lowerer::RVal{val, IlType(IlType::Kind::I64)};
    }

    /// @brief Lower a unary operator expression.
    /// @details Delegates to the shared helper that understands the full matrix
    ///          of BASIC unary operators.
    /// @param expr Unary operator node from the BASIC AST.
    void visit(const UnaryExpr &expr) override
    {
        result_ = lowerer_.lowerUnaryExpr(expr);
    }

    /// @brief Lower a binary operator expression.
    /// @details Forwards to @ref Lowerer::lowerBinaryExpr which handles operand
    ///          coercion and emits the correct IL opcode for the operator.
    /// @param expr Binary operator node from the BASIC AST.
    void visit(const BinaryExpr &expr) override
    {
        result_ = lowerer_.lowerBinaryExpr(expr);
    }

    /// @brief Lower a builtin function call expression.
    /// @details Delegates to @ref lowerBuiltinCall so builtin-specific lowering
    ///          stays concentrated in the helper module.
    /// @param expr Builtin invocation node from the BASIC AST.
    void visit(const BuiltinCallExpr &expr) override
    {
        result_ = lowerBuiltinCall(lowerer_, expr);
    }

    /// @brief Lower the LBOUND intrinsic expression.
    /// @details Emits the constant zero because BASIC arrays are zero based in
    ///          the current runtime configuration.
    /// @param expr LBOUND invocation node from the BASIC AST.
    void visit(const LBoundExpr &expr) override
    {
        lowerer_.curLoc = expr.loc;
        result_ = Lowerer::RVal{IlValue::constInt(0), IlType(IlType::Kind::I64)};
    }

    /// @brief Lower the UBOUND intrinsic expression.
    /// @details Delegates to the helper that computes the appropriate runtime
    ///          query for the array upper bound.
    /// @param expr UBOUND invocation node from the BASIC AST.
    void visit(const UBoundExpr &expr) override
    {
        result_ = lowerer_.lowerUBoundExpr(expr);
    }

    /// @brief Lower a user-defined procedure call expression.
    /// @details Resolves the callee signature when available so argument values
    ///          can be coerced into the expected IL types before emitting the
    ///          call. When the signature advertises a return value the helper
    ///          records the call result; otherwise it fabricates a dummy integer
    ///          to keep the return type consistent for expression contexts.
    /// @param expr Call expression node from the BASIC AST.
    void visit(const CallExpr &expr) override
    {
        const auto *signature = lowerer_.findProcSignature(expr.callee);
        std::vector<IlValue> args;
        args.reserve(expr.args.size());
        for (size_t i = 0; i < expr.args.size(); ++i)
        {
            Lowerer::RVal arg = lowerer_.lowerExpr(*expr.args[i]);
            if (signature && i < signature->paramTypes.size())
            {
                IlType paramTy = signature->paramTypes[i];
                if (paramTy.kind == IlType::Kind::F64)
                {
                    arg = lowerer_.coerceToF64(std::move(arg), expr.loc);
                }
                else if (paramTy.kind == IlType::Kind::I64)
                {
                    arg = lowerer_.coerceToI64(std::move(arg), expr.loc);
                }
            }
            args.push_back(arg.value);
        }
        lowerer_.curLoc = expr.loc;
        if (signature && signature->retType.kind != IlType::Kind::Void)
        {
            IlValue res = lowerer_.emitCallRet(signature->retType, expr.callee, args);
            result_ = Lowerer::RVal{res, signature->retType};
        }
        else
        {
            lowerer_.emitCall(expr.callee, args);
            result_ = Lowerer::RVal{IlValue::constInt(0), IlType(IlType::Kind::I64)};
        }
    }

    /// @brief Lower a @c NEW expression for object construction.
    /// @details Defers to @ref Lowerer::lowerNewExpr so object layout specifics
    ///          reside in the dedicated helper.
    /// @param expr Object construction node from the BASIC AST.
    void visit(const NewExpr &expr) override
    {
        result_ = lowerer_.lowerNewExpr(expr);
    }

    /// @brief Lower a reference to the implicit @c ME parameter.
    /// @details Hands control to @ref Lowerer::lowerMeExpr which implements the
    ///          details around retrieving the current instance reference.
    /// @param expr ME expression node from the BASIC AST.
    void visit(const MeExpr &expr) override
    {
        result_ = lowerer_.lowerMeExpr(expr);
    }

    /// @brief Lower a member access expression.
    /// @details Uses @ref Lowerer::lowerMemberAccessExpr to resolve the field or
    ///          property lookup and produce the corresponding IL value.
    /// @param expr Member access node from the BASIC AST.
    void visit(const MemberAccessExpr &expr) override
    {
        result_ = lowerer_.lowerMemberAccessExpr(expr);
    }

    /// @brief Lower a method call expression.
    /// @details Delegates to @ref Lowerer::lowerMethodCallExpr which handles the
    ///          implicit receiver and method dispatch semantics.
    /// @param expr Method call node from the BASIC AST.
    void visit(const MethodCallExpr &expr) override
    {
        result_ = lowerer_.lowerMethodCallExpr(expr);
    }

    /// @brief Lower IS expression via RTTI helpers.
    void visit(const IsExpr &expr) override
    {
        lowerer_.curLoc = expr.loc;
        // Lower left value to an object pointer
        Lowerer::RVal lhs = lowerer_.lowerExpr(*expr.value);
        // Build type/interface key from dotted name
        std::string dotted;
        for (size_t i = 0; i < expr.typeName.size(); ++i)
        {
            if (i)
                dotted.push_back('.');
            dotted += expr.typeName[i];
        }
        // Determine if target is an interface
        bool isIface = false;
        int targetId = -1;
        // Interface lookup via OOP index
        for (const auto &p : lowerer_.oopIndex_.interfacesByQname())
        {
            if (p.first == dotted)
            {
                isIface = true;
                targetId = p.second.ifaceId;
                break;
            }
        }
        if (!isIface)
        {
            // Use last segment as class key for layout map
            std::string cls = expr.typeName.empty() ? std::string{} : expr.typeName.back();
            auto it = lowerer_.classLayouts_.find(cls);
            if (it != lowerer_.classLayouts_.end())
                targetId = static_cast<int>(it->second.classId);
        }

        // Call rt_typeid_of to get type id and then predicate helper
        Lowerer::Value typeIdVal = lowerer_.emitCallRet(
            Lowerer::Type(Lowerer::Type::Kind::I64), "rt_typeid_of", {lhs.value});
        Lowerer::Value pred64 =
            isIface ? lowerer_.emitCallRet(Lowerer::Type(Lowerer::Type::Kind::I64),
                                           "rt_type_implements",
                                           {typeIdVal, Lowerer::Value::constInt(targetId)})
                    : lowerer_.emitCallRet(Lowerer::Type(Lowerer::Type::Kind::I64),
                                           "rt_type_is_a",
                                           {typeIdVal, Lowerer::Value::constInt(targetId)});
        Lowerer::Value cond = lowerer_.emitBinary(
            il::core::Opcode::ICmpNe, lowerer_.ilBoolTy(), pred64, Lowerer::Value::constInt(0));
        result_ = Lowerer::RVal{cond, lowerer_.ilBoolTy()};
    }

    /// @brief Lower AS expression via RTTI helpers.
    void visit(const AsExpr &expr) override
    {
        lowerer_.curLoc = expr.loc;
        Lowerer::RVal lhs = lowerer_.lowerExpr(*expr.value);
        // Dotted type name
        std::string dotted;
        for (size_t i = 0; i < expr.typeName.size(); ++i)
        {
            if (i)
                dotted.push_back('.');
            dotted += expr.typeName[i];
        }
        bool isIface = false;
        int targetId = -1;
        for (const auto &p : lowerer_.oopIndex_.interfacesByQname())
        {
            if (p.first == dotted)
            {
                isIface = true;
                targetId = p.second.ifaceId;
                break;
            }
        }
        if (!isIface)
        {
            std::string cls = expr.typeName.empty() ? std::string{} : expr.typeName.back();
            auto it = lowerer_.classLayouts_.find(cls);
            if (it != lowerer_.classLayouts_.end())
                targetId = static_cast<int>(it->second.classId);
        }
        Lowerer::Value casted =
            isIface ? lowerer_.emitCallRet(Lowerer::Type(Lowerer::Type::Kind::Ptr),
                                           "rt_cast_as_iface",
                                           {lhs.value, Lowerer::Value::constInt(targetId)})
                    : lowerer_.emitCallRet(Lowerer::Type(Lowerer::Type::Kind::Ptr),
                                           "rt_cast_as",
                                           {lhs.value, Lowerer::Value::constInt(targetId)});
        result_ = Lowerer::RVal{casted, Lowerer::Type(Lowerer::Type::Kind::Ptr)};
    }

    /// @brief Retrieve the IL value produced by the most recent visit.
    /// @details The visitor stores the result internally to avoid allocating on
    ///          every visit call; this accessor exposes the cached value to the
    ///          caller.
    /// @return Pair containing the IL value and its static type.
    [[nodiscard]] Lowerer::RVal result() const noexcept
    {
        return result_;
    }

  private:
    Lowerer &lowerer_;
    Lowerer::RVal result_{IlValue::constInt(0), IlType(IlType::Kind::I64)};
};

/// @brief Lower an arbitrary BASIC expression to IL form.
/// @details Creates a temporary @ref LowererExprVisitor to traverse the AST and
///          capture the resulting value. The current source location is updated
///          so diagnostics emitted during lowering point back to the originating
///          node.
/// @param expr Expression node to lower.
/// @return Resulting IL value and type.
Lowerer::RVal Lowerer::lowerExpr(const Expr &expr)
{
    curLoc = expr.loc;
    LowererExprVisitor visitor(*this);
    visitor.visitExpr(expr);
    return visitor.result();
}

/// @brief Lower an expression and coerce it to a scalar IL type.
/// @details Invokes @ref lowerExpr and then normalises the result to an integer
///          or floating type acceptable for scalar contexts (for example loop
///          bounds). The original source location is reused for any diagnostics
///          emitted during coercion.
/// @param expr Expression node expected to resolve to a scalar.
/// @return Coerced IL value and its scalar type.
Lowerer::RVal Lowerer::lowerScalarExpr(const Expr &expr)
{
    return lowerScalarExpr(lowerExpr(expr), expr.loc);
}

/// @brief Coerce an already-lowered expression result into a scalar type.
/// @details Examines the value's static type and converts booleans and floating
///          values into 64-bit integers when required by the caller. Other types
///          are forwarded unchanged so complex lowering logic can handle them
///          separately.
/// @param value Result previously returned by @ref lowerExpr.
/// @param loc Source location used for diagnostics during coercion.
/// @return IL value adjusted for scalar contexts.
Lowerer::RVal Lowerer::lowerScalarExpr(RVal value, il::support::SourceLoc loc)
{
    switch (value.type.kind)
    {
        case Type::Kind::I1:
        case Type::Kind::I16:
        case Type::Kind::I32:
        case Type::Kind::I64:
        case Type::Kind::F64:
            value = coerceToI64(std::move(value), loc);
            break;
        default:
            break;
    }
    return value;
}

TypeRules::NumericType Lowerer::classifyNumericType(const Expr &expr)
{
    using NumericType = TypeRules::NumericType;

    class Classifier final : public ExprVisitor
    {
      public:
        explicit Classifier(Lowerer &lowerer) noexcept : lowerer_(lowerer) {}

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
            if (const auto *info = lowerer_.findSymbol(var.name))
            {
                if (info->hasType)
                {
                    if (info->type == AstType::F64)
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
                case BuiltinCallExpr::Builtin::Int:
                case BuiltinCallExpr::Builtin::Fix:
                case BuiltinCallExpr::Builtin::Round:
                case BuiltinCallExpr::Builtin::Sqr:
                case BuiltinCallExpr::Builtin::Abs:
                case BuiltinCallExpr::Builtin::Floor:
                case BuiltinCallExpr::Builtin::Ceil:
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
                    case Type::Kind::I16:
                        result_ = NumericType::Integer;
                        return;
                    case Type::Kind::I32:
                    case Type::Kind::I64:
                        result_ = NumericType::Long;
                        return;
                    case Type::Kind::F64:
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

    Classifier classifier(*this);
    expr.accept(classifier);
    return classifier.result();
}

} // namespace il::frontends::basic
