// File: src/frontends/basic/lower/Scan_ExprTypes.cpp
// License: MIT License. See LICENSE in the project root for full license information.
// Purpose: Implements expression-type inference for BASIC scan passes.
// Key invariants: Produces expression classifications without mutating runtime flags.
// Ownership/Lifetime: Operates on Lowerer state without owning AST or module.
// Links: docs/codemap.md

#include "frontends/basic/AstWalker.hpp"
#include "frontends/basic/BuiltinRegistry.hpp"
#include "frontends/basic/Lowerer.hpp"
#include "frontends/basic/TypeSuffix.hpp"
#include "il/core/Type.hpp"
#include <cassert>
#include <optional>
#include <vector>

namespace il::frontends::basic::lower
{
Lowerer::ExprType scanBuiltinExprTypes(Lowerer &lowerer, const BuiltinCallExpr &expr);

namespace detail
{

Lowerer::ExprType exprTypeFromAstType(Type ty)
{
    switch (ty)
    {
        case Type::Str: return Lowerer::ExprType::Str;
        case Type::F64: return Lowerer::ExprType::F64;
        case Type::Bool: return Lowerer::ExprType::Bool;
        case Type::I64:
        default: return Lowerer::ExprType::I64;
    }
}

class ExprTypeScanner final : public BasicAstWalker<ExprTypeScanner>
{
  public:
    explicit ExprTypeScanner(Lowerer &lowerer) noexcept : lowerer_(lowerer) {}

    using ExprType = Lowerer::ExprType;

    ExprType evaluateExpr(const Expr &expr)
    {
        StackScope scope(*this);
        expr.accept(*this);
        return pop();
    }

    bool shouldVisitChildren(const BuiltinCallExpr &) { return false; }

    bool shouldVisitChildren(const CallExpr &) { return false; }

    bool shouldVisitChildren(const NewExpr &) { return false; }

    bool shouldVisitChildren(const MemberAccessExpr &) { return false; }

    bool shouldVisitChildren(const MethodCallExpr &) { return false; }

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
        push(ExprType::I64);
    }

    void after(const LBoundExpr &)
    {
        push(ExprType::I64);
    }

    void after(const UBoundExpr &)
    {
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
        push(combineBinary(expr, lhs, rhs));
    }

    void after(const BuiltinCallExpr &expr)
    {
        push(::il::frontends::basic::lower::scanBuiltinExprTypes(lowerer_, expr));
    }

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
                case K::F64: push(ExprType::F64); break;
                case K::Ptr:
                case K::I1:
                case K::I16:
                case K::I32:
                case K::I64: push(ExprType::I64); break;
                case K::Str: push(ExprType::Str); break;
                case K::Void:
                default: push(ExprType::I64); break;
            }
        }
        else
        {
            push(ExprType::I64);
        }
    }

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

  private:
    struct StackScope
    {
        ExprTypeScanner &walker;
        std::size_t depth;
        explicit StackScope(ExprTypeScanner &w) : walker(w), depth(w.exprStack_.size()) {}
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

    ExprType combineBinary(const BinaryExpr &expr, ExprType lhs, ExprType rhs)
    {
        using Op = BinaryExpr::Op;
        if (expr.op == Op::Pow)
            return ExprType::F64;
        if (expr.op == Op::Add && lhs == ExprType::Str && rhs == ExprType::Str)
            return ExprType::Str;
        if (expr.op == Op::Eq || expr.op == Op::Ne)
            return ExprType::Bool;
        if (expr.op == Op::LogicalAndShort || expr.op == Op::LogicalOrShort || expr.op == Op::LogicalAnd ||
            expr.op == Op::LogicalOr)
            return ExprType::Bool;
        if (lhs == ExprType::F64 || rhs == ExprType::F64)
            return ExprType::F64;
        return ExprType::I64;
    }

    Lowerer &lowerer_;
    std::vector<ExprType> exprStack_{};
};

} // namespace detail

Lowerer::ExprType scanExprTypes(Lowerer &lowerer, const Expr &expr)
{
    detail::ExprTypeScanner scanner(lowerer);
    return scanner.evaluateExpr(expr);
}

Lowerer::ExprType scanBuiltinExprTypes(Lowerer &lowerer, const BuiltinCallExpr &expr)
{
    const auto &rule = getBuiltinScanRule(expr.builtin);
    std::vector<std::optional<Lowerer::ExprType>> argTypes(expr.args.size());

    auto scanArg = [&](std::size_t idx) {
        if (idx >= expr.args.size())
            return;
        const auto &arg = expr.args[idx];
        if (!arg)
            return;
        detail::ExprTypeScanner scanner(lowerer);
        argTypes[idx] = scanner.evaluateExpr(*arg);
    };

    if (rule.traversal == BuiltinScanRule::ArgTraversal::All)
    {
        for (std::size_t i = 0; i < expr.args.size(); ++i)
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

} // namespace il::frontends::basic::lower
