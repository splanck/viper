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
#include "frontends/basic/lower/AstVisitor.hpp"

#include "il/core/Instr.hpp"

#include <utility>
#include <vector>

namespace il::frontends::basic
{

using IlType = il::core::Type;
using IlValue = il::core::Value;

/// @brief Visitor that lowers BASIC expressions using Lowerer helpers.
class LowererExprVisitor final : public lower::AstVisitor, public ExprVisitor
{
  public:
    explicit LowererExprVisitor(Lowerer &lowerer) noexcept : lowerer_(lowerer) {}

    void visitExpr(const Expr &expr) override
    {
        expr.accept(*this);
    }

    void visitStmt(const Stmt &) override {}

    void visit(const IntExpr &expr) override
    {
        lowerer_.curLoc = expr.loc;
        result_ = Lowerer::RVal{IlValue::constInt(expr.value), IlType(IlType::Kind::I64)};
    }

    void visit(const FloatExpr &expr) override
    {
        lowerer_.curLoc = expr.loc;
        result_ = Lowerer::RVal{IlValue::constFloat(expr.value), IlType(IlType::Kind::F64)};
    }

    void visit(const StringExpr &expr) override
    {
        lowerer_.curLoc = expr.loc;
        std::string lbl = lowerer_.getStringLabel(expr.value);
        IlValue tmp = lowerer_.emitConstStr(lbl);
        result_ = Lowerer::RVal{tmp, IlType(IlType::Kind::Str)};
    }

    void visit(const BoolExpr &expr) override
    {
        lowerer_.curLoc = expr.loc;
        IlValue logical = lowerer_.emitConstI64(expr.value ? -1 : 0);
        result_ = Lowerer::RVal{logical, IlType(IlType::Kind::I64)};
    }

    void visit(const VarExpr &expr) override
    {
        result_ = lowerer_.lowerVarExpr(expr);
    }

    void visit(const ArrayExpr &expr) override
    {
        Lowerer::ArrayAccess access =
            lowerer_.lowerArrayAccess(expr, Lowerer::ArrayAccessKind::Load);
        lowerer_.curLoc = expr.loc;
        IlValue val = lowerer_.emitCallRet(IlType(IlType::Kind::I64),
                                           "rt_arr_i32_get",
                                           {access.base, access.index});
        result_ = Lowerer::RVal{val, IlType(IlType::Kind::I64)};
    }

    void visit(const UnaryExpr &expr) override
    {
        result_ = lowerer_.lowerUnaryExpr(expr);
    }

    void visit(const BinaryExpr &expr) override
    {
        result_ = lowerer_.lowerBinaryExpr(expr);
    }

    void visit(const BuiltinCallExpr &expr) override
    {
        result_ = lowerBuiltinCall(lowerer_, expr);
    }

    void visit(const LBoundExpr &expr) override
    {
        lowerer_.curLoc = expr.loc;
        result_ = Lowerer::RVal{IlValue::constInt(0), IlType(IlType::Kind::I64)};
    }

    void visit(const UBoundExpr &expr) override
    {
        result_ = lowerer_.lowerUBoundExpr(expr);
    }

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

    void visit(const NewExpr &expr) override
    {
        result_ = lowerer_.lowerNewExpr(expr);
    }

    void visit(const MeExpr &expr) override
    {
        result_ = lowerer_.lowerMeExpr(expr);
    }

    void visit(const MemberAccessExpr &expr) override
    {
        result_ = lowerer_.lowerMemberAccessExpr(expr);
    }

    void visit(const MethodCallExpr &expr) override
    {
        result_ = lowerer_.lowerMethodCallExpr(expr);
    }

    [[nodiscard]] Lowerer::RVal result() const noexcept
    {
        return result_;
    }

  private:
    Lowerer &lowerer_;
    Lowerer::RVal result_{IlValue::constInt(0), IlType(IlType::Kind::I64)};
};

Lowerer::RVal Lowerer::lowerExpr(const Expr &expr)
{
    curLoc = expr.loc;
    LowererExprVisitor visitor(*this);
    visitor.visitExpr(expr);
    return visitor.result();
}

Lowerer::RVal Lowerer::lowerScalarExpr(const Expr &expr)
{
    return lowerScalarExpr(lowerExpr(expr), expr.loc);
}

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

} // namespace il::frontends::basic
