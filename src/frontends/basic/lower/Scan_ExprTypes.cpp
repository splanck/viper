// File: src/frontends/basic/lower/Scan_ExprTypes.cpp
// License: MIT License. See LICENSE in the project root for full license information.
// Purpose: Implements expression-type scanning for BASIC lowering.
// Key invariants: Scan sets flags only; no IR emission.
// Ownership/Lifetime: Operates on Lowerer state without owning AST or module.
// Links: docs/codemap.md

#include "frontends/basic/AstWalker.hpp"
#include "frontends/basic/BuiltinRegistry.hpp"
#include "frontends/basic/Lowerer.hpp"
#include "frontends/basic/TypeSuffix.hpp"

#include <cassert>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace il::frontends::basic
{
namespace
{

Lowerer::ExprType exprTypeFromAstType(Type ty)
{
    using AstType = ::il::frontends::basic::Type;
    switch (ty)
    {
        case AstType::Str:
            return Lowerer::ExprType::Str;
        case AstType::F64:
            return Lowerer::ExprType::F64;
        case AstType::Bool:
            return Lowerer::ExprType::Bool;
        case AstType::I64:
        default:
            return Lowerer::ExprType::I64;
    }
}

Lowerer::ExprType combineBinaryTypes(const BinaryExpr &expr,
                                     Lowerer::ExprType lhs,
                                     Lowerer::ExprType rhs)
{
    using ExprType = Lowerer::ExprType;
    using Op = BinaryExpr::Op;
    if (expr.op == Op::Pow)
        return ExprType::F64;
    if (expr.op == Op::Add && lhs == ExprType::Str && rhs == ExprType::Str)
        return ExprType::Str;
    if (expr.op == Op::Eq || expr.op == Op::Ne || expr.op == Op::LogicalAndShort || expr.op == Op::LogicalOrShort ||
        expr.op == Op::LogicalAnd || expr.op == Op::LogicalOr)
    {
        if (expr.op == Op::Eq || expr.op == Op::Ne)
        {
            if (lhs == ExprType::Str || rhs == ExprType::Str)
                return ExprType::Bool;
        }
        return ExprType::Bool;
    }
    if (lhs == ExprType::F64 || rhs == ExprType::F64)
        return ExprType::F64;
    return ExprType::I64;
}

} // unnamed namespace

namespace scan_detail
{

Lowerer::ExprType scanBuiltinCallExprType(Lowerer &lowerer, const BuiltinCallExpr &c);

class ExprTypeScanWalker final : public BasicAstWalker<ExprTypeScanWalker>
{
  public:
    explicit ExprTypeScanWalker(Lowerer &lowerer) noexcept : lowerer_(lowerer) {}

    using ExprType = Lowerer::ExprType;

    ExprType evaluateExpr(const Expr &expr)
    {
        StackScope scope(*this);
        expr.accept(*this);
        return pop();
    }

    void evaluateStmt(const Stmt &stmt)
    {
        StackScope scope(*this);
        stmt.accept(*this);
    }

    void evaluateProgram(const Program &prog)
    {
        StackScope scope(*this);
        for (const auto &decl : prog.procs)
        {
            if (decl)
                decl->accept(*this);
        }
        for (const auto &stmt : prog.main)
        {
            if (stmt)
                stmt->accept(*this);
        }
    }

    void after(const IntExpr &)
    {
        push(ExprType::I64);
    }

    void after(const FloatExpr &)
    {
        push(ExprType::F64);
    }

    void after(const StringExpr &)
    {
        push(ExprType::Str);
    }

    void after(const BoolExpr &)
    {
        push(ExprType::I64);
    }

    void after(const VarExpr &expr)
    {
        ExprType result = ExprType::I64;
        if (const auto *info = lowerer_.findSymbol(expr.name))
        {
            if (info->hasType)
                result = exprTypeFromAstType(info->type);
            else
                result = exprTypeFromAstType(inferAstTypeFromName(expr.name));
        }
        else
        {
            result = exprTypeFromAstType(inferAstTypeFromName(expr.name));
        }
        push(result);
    }

    void after(const ArrayExpr &expr)
    {
        discardIf(expr.index != nullptr);
        if (lvalueDepth_ > 0)
        {
            push(ExprType::I64);
            return;
        }
        lowerer_.markSymbolReferenced(expr.name);
        lowerer_.markArray(expr.name);
        push(ExprType::I64);
    }

    void after(const LBoundExpr &expr)
    {
        if (!expr.name.empty())
        {
            lowerer_.markSymbolReferenced(expr.name);
            lowerer_.markArray(expr.name);
        }
        push(ExprType::I64);
    }

    void after(const UBoundExpr &expr)
    {
        if (!expr.name.empty())
        {
            lowerer_.markSymbolReferenced(expr.name);
            lowerer_.markArray(expr.name);
        }
        push(ExprType::I64);
    }

    void after(const UnaryExpr &)
    {
        ExprType operand = pop();
        push(operand);
    }

    void after(const BinaryExpr &expr)
    {
        ExprType rhs = pop();
        ExprType lhs = pop();
        push(combineBinaryTypes(expr, lhs, rhs));
    }

    bool shouldVisitChildren(const BuiltinCallExpr &) { return false; }

    void after(const BuiltinCallExpr &expr)
    {
        push(scanBuiltinCallExprType(lowerer_, expr));
    }

    bool shouldVisitChildren(const CallExpr &) { return false; }

    void after(const CallExpr &expr)
    {
        for (const auto &arg : expr.args)
        {
            if (!arg)
                continue;
            consumeExpr(*arg);
        }
        if (const auto *sig = lowerer_.findProcSignature(expr.callee))
        {
            using K = il::core::Type::Kind;
            switch (sig->retType.kind)
            {
                case K::F64:
                    push(ExprType::F64);
                    break;
                case K::Ptr:
                case K::I1:
                case K::I16:
                case K::I32:
                case K::I64:
                    push(ExprType::I64);
                    break;
                case K::Str:
                    push(ExprType::Str);
                    break;
                case K::Void:
                default:
                    push(ExprType::I64);
                    break;
            }
        }
        else
        {
            push(ExprType::I64);
        }
    }

    bool shouldVisitChildren(const NewExpr &) { return false; }

    void after(const NewExpr &expr)
    {
        for (const auto &arg : expr.args)
        {
            if (!arg)
                continue;
            consumeExpr(*arg);
        }
        push(ExprType::I64);
    }

    void after(const MeExpr &)
    {
        push(ExprType::I64);
    }

    bool shouldVisitChildren(const MemberAccessExpr &) { return false; }

    void after(const MemberAccessExpr &expr)
    {
        ExprType result = ExprType::I64;
        if (expr.base)
        {
            consumeExpr(*expr.base);
            std::string className = lowerer_.resolveObjectClass(*expr.base);
            auto layoutIt = lowerer_.classLayouts_.find(className);
            if (layoutIt != lowerer_.classLayouts_.end())
            {
                if (const auto *field = layoutIt->second.findField(expr.member))
                    result = exprTypeFromAstType(field->type);
            }
        }
        push(result);
    }

    bool shouldVisitChildren(const MethodCallExpr &) { return false; }

    void after(const MethodCallExpr &expr)
    {
        if (expr.base)
            consumeExpr(*expr.base);
        for (const auto &arg : expr.args)
        {
            if (!arg)
                continue;
            consumeExpr(*arg);
        }
        push(ExprType::I64);
    }

    void after(const PrintStmt &stmt)
    {
        for (auto it = stmt.items.rbegin(); it != stmt.items.rend(); ++it)
        {
            if (it->kind == PrintItem::Kind::Expr && it->expr)
                pop();
        }
    }

    void after(const PrintChStmt &stmt)
    {
        for (auto it = stmt.args.rbegin(); it != stmt.args.rend(); ++it)
        {
            if (!*it)
                continue;
            pop();
        }
        if (stmt.channelExpr)
            pop();
    }

    void after(const CallStmt &stmt)
    {
        discardIf(stmt.call != nullptr);
    }

    void after(const LetStmt &stmt)
    {
        if (stmt.expr)
            pop();

        if (!stmt.target)
        {
            pop();
            return;
        }

        if (auto *var = dynamic_cast<const VarExpr *>(stmt.target.get()))
        {
            if (!var->name.empty())
            {
                if (stmt.expr)
                {
                    std::string className;
                    if (const auto *alloc = dynamic_cast<const NewExpr *>(stmt.expr.get()))
                        className = alloc->className;
                    else
                        className = lowerer_.resolveObjectClass(*stmt.expr);
                    if (!className.empty())
                        lowerer_.setSymbolObjectType(var->name, className);
                }
                const auto *info = lowerer_.findSymbol(var->name);
                if (!info || !info->hasType)
                    lowerer_.setSymbolType(var->name, inferAstTypeFromName(var->name));
            }
        }
        else if (auto *arr = dynamic_cast<const ArrayExpr *>(stmt.target.get()))
        {
            if (!arr->name.empty())
            {
                const auto *info = lowerer_.findSymbol(arr->name);
                if (!info || !info->hasType)
                    lowerer_.setSymbolType(arr->name, inferAstTypeFromName(arr->name));
                lowerer_.markSymbolReferenced(arr->name);
                lowerer_.markArray(arr->name);
            }
        }

        pop();
    }

    void before(const DimStmt &stmt)
    {
        if (stmt.name.empty())
            return;
        lowerer_.setSymbolType(stmt.name, stmt.type);
        lowerer_.markSymbolReferenced(stmt.name);
        if (stmt.isArray)
            lowerer_.markArray(stmt.name);
    }

    void after(const DimStmt &stmt)
    {
        discardIf(stmt.size != nullptr);
    }

    void before(const ReDimStmt &stmt)
    {
        if (stmt.name.empty())
            return;
        lowerer_.markSymbolReferenced(stmt.name);
        lowerer_.markArray(stmt.name);
    }

    void after(const ReDimStmt &stmt)
    {
        discardIf(stmt.size != nullptr);
    }

    void after(const RandomizeStmt &stmt)
    {
        discardIf(stmt.seed != nullptr);
    }

    void after(const IfStmt &stmt)
    {
        for (auto it = stmt.elseifs.rbegin(); it != stmt.elseifs.rend(); ++it)
        {
            if (it->cond)
                pop();
        }
        if (stmt.cond)
            pop();
    }

    void after(const SelectCaseStmt &stmt)
    {
        discardIf(stmt.selector != nullptr);
    }

    void after(const WhileStmt &stmt)
    {
        discardIf(stmt.cond != nullptr);
    }

    void after(const DoStmt &stmt)
    {
        discardIf(stmt.cond != nullptr);
    }

    void before(const ForStmt &stmt)
    {
        if (!stmt.var.empty())
        {
            const auto *info = lowerer_.findSymbol(stmt.var);
            if (!info || !info->hasType)
                lowerer_.setSymbolType(stmt.var, inferAstTypeFromName(stmt.var));
        }
    }

    void after(const ForStmt &stmt)
    {
        if (stmt.step)
            pop();
        if (stmt.end)
            pop();
        if (stmt.start)
            pop();
    }

    void after(const OpenStmt &stmt)
    {
        if (stmt.channelExpr)
            pop();
        if (stmt.pathExpr)
            pop();
    }

    void after(const CloseStmt &stmt)
    {
        discardIf(stmt.channelExpr != nullptr);
    }

    void after(const SeekStmt &stmt)
    {
        discardIf(stmt.positionExpr != nullptr);
        discardIf(stmt.channelExpr != nullptr);
    }

    void after(const InputStmt &stmt)
    {
        discardIf(stmt.prompt != nullptr);
        for (const auto &name : stmt.vars)
        {
            if (name.empty())
                continue;
            const auto *info = lowerer_.findSymbol(name);
            if (!info || !info->hasType)
            {
                Type astTy = inferAstTypeFromName(name);
                lowerer_.setSymbolType(name, astTy);
            }
        }
    }

    void after(const InputChStmt &stmt)
    {
        const auto &name = stmt.target.name;
        if (name.empty())
            return;
        const auto *info = lowerer_.findSymbol(name);
        if (!info || !info->hasType)
            lowerer_.setSymbolType(name, inferAstTypeFromName(name));
    }

    void after(const LineInputChStmt &stmt)
    {
        if (stmt.targetVar)
            pop();
        if (stmt.channelExpr)
            pop();
    }

    void after(const ReturnStmt &stmt)
    {
        discardIf(stmt.value != nullptr);
    }

    void after(const DeleteStmt &stmt)
    {
        discardIf(stmt.target != nullptr);
    }

    void after(const FunctionDecl &)
    {
    }

    void after(const SubDecl &)
    {
    }

    void after(const StmtList &)
    {
    }

    void beforeChild(const LetStmt &stmt, const Expr &child)
    {
        if (stmt.target && stmt.target.get() == &child)
            ++lvalueDepth_;
    }

    void afterChild(const LetStmt &stmt, const Expr &child)
    {
        if (stmt.target && stmt.target.get() == &child)
            --lvalueDepth_;
    }

    void beforeChild(const LineInputChStmt &stmt, const Expr &child)
    {
        if (stmt.targetVar && stmt.targetVar.get() == &child)
        {
            if (auto *var = dynamic_cast<const VarExpr *>(stmt.targetVar.get()))
            {
                if (!var->name.empty())
                    lowerer_.setSymbolType(var->name, Type::Str);
            }
        }
    }

  private:
    struct StackScope
    {
        ExprTypeScanWalker &walker;
        std::size_t depth;
        explicit StackScope(ExprTypeScanWalker &w) : walker(w), depth(w.exprStack_.size()) {}
        ~StackScope()
        {
            assert(walker.exprStack_.size() == depth && "expression stack imbalance");
        }
    };

    void push(ExprType ty)
    {
        exprStack_.push_back(ty);
    }

    ExprType pop()
    {
        assert(!exprStack_.empty());
        ExprType ty = exprStack_.back();
        exprStack_.pop_back();
        return ty;
    }

    void discardIf(bool condition)
    {
        if (condition)
            (void)pop();
    }

    ExprType consumeExpr(const Expr &expr)
    {
        expr.accept(*this);
        return pop();
    }

    Lowerer &lowerer_;
    std::vector<ExprType> exprStack_{};
    int lvalueDepth_{0};
};

Lowerer::ExprType scanExprType(Lowerer &lowerer, const Expr &expr)
{
    ExprTypeScanWalker walker(lowerer);
    return walker.evaluateExpr(expr);
}

void scanStmtExprTypes(Lowerer &lowerer, const Stmt &stmt)
{
    ExprTypeScanWalker walker(lowerer);
    walker.evaluateStmt(stmt);
}

void scanProgramExprTypes(Lowerer &lowerer, const Program &prog)
{
    ExprTypeScanWalker walker(lowerer);
    walker.evaluateProgram(prog);
}

Lowerer::ExprType scanBuiltinCallExprType(Lowerer &lowerer, const BuiltinCallExpr &c)
{
    const auto &rule = getBuiltinScanRule(c.builtin);
    std::vector<std::optional<Lowerer::ExprType>> argTypes(c.args.size());

    auto scanArg = [&](std::size_t idx) {
        if (idx >= c.args.size())
            return;
        const auto &arg = c.args[idx];
        if (!arg)
            return;
        ExprTypeScanWalker walker(lowerer);
        argTypes[idx] = walker.evaluateExpr(*arg);
    };

    if (rule.traversal == BuiltinScanRule::ArgTraversal::All)
    {
        for (std::size_t i = 0; i < c.args.size(); ++i)
            scanArg(i);
    }
    else
    {
        for (std::size_t idx : rule.explicitArgs)
            scanArg(idx);
    }

    auto argType = [&](std::size_t idx) -> std::optional<Lowerer::ExprType> {
        if (idx >= argTypes.size())
            return std::nullopt;
        return argTypes[idx];
    };

    Lowerer::ExprType result = rule.result.type;
    if (rule.result.kind == BuiltinScanRule::ResultSpec::Kind::FromArg)
    {
        if (auto ty = argType(rule.result.argIndex))
            result = *ty;
    }
    return result;
}

} // namespace scan_detail

Lowerer::ExprType Lowerer::scanBuiltinCallExpr(const BuiltinCallExpr &c)
{
    return scan_detail::scanBuiltinCallExprType(*this, c);
}

Lowerer::ExprType Lowerer::scanExpr(const Expr &e)
{
    return scan_detail::scanExprType(*this, e);
}

} // namespace il::frontends::basic
