// File: src/frontends/basic/LowerScan.cpp
// License: MIT License. See LICENSE in the project root for full license information.
// Purpose: Implements AST scanning to compute expression types and runtime requirements.
// Key invariants: Scanning only mutates bookkeeping flags; no IR emitted.
// Ownership/Lifetime: Operates on Lowerer state without owning AST or module.
// Links: docs/codemap.md

#include "frontends/basic/BuiltinRegistry.hpp"
#include "frontends/basic/Lowerer.hpp"
#include "frontends/basic/TypeSuffix.hpp"
#include <optional>
#include <vector>

namespace il::frontends::basic
{

namespace
{

Lowerer::ExprType exprTypeFromAstType(::il::frontends::basic::Type ty)
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

} // namespace

class ScanExprVisitor final : public ExprVisitor
{
  public:
    explicit ScanExprVisitor(Lowerer &lowerer) noexcept : lowerer_(lowerer) {}

    void visit(const IntExpr &) override { result_ = Lowerer::ExprType::I64; }

    void visit(const FloatExpr &) override { result_ = Lowerer::ExprType::F64; }

    void visit(const StringExpr &) override { result_ = Lowerer::ExprType::Str; }

    void visit(const BoolExpr &) override { result_ = Lowerer::ExprType::I64; }

    void visit(const VarExpr &expr) override
    {
        if (const auto *info = lowerer_.findSymbol(expr.name))
        {
            if (info->hasType)
            {
                result_ = exprTypeFromAstType(info->type);
                return;
            }
        }
        result_ = exprTypeFromAstType(inferAstTypeFromName(expr.name));
    }

    void visit(const ArrayExpr &expr) override { result_ = lowerer_.scanArrayExpr(expr); }

    void visit(const UnaryExpr &expr) override { result_ = lowerer_.scanUnaryExpr(expr); }

    void visit(const BinaryExpr &expr) override { result_ = lowerer_.scanBinaryExpr(expr); }

    void visit(const BuiltinCallExpr &expr) override
    {
        result_ = lowerer_.scanBuiltinCallExpr(expr);
    }

    void visit(const CallExpr &expr) override
    {
        for (const auto &arg : expr.args)
        {
            if (arg)
                lowerer_.scanExpr(*arg);
        }
        result_ = Lowerer::ExprType::I64;
    }

    [[nodiscard]] Lowerer::ExprType result() const noexcept { return result_; }

  private:
    Lowerer &lowerer_;
    Lowerer::ExprType result_{Lowerer::ExprType::I64};
};

class ScanStmtVisitor final : public StmtVisitor
{
  public:
    explicit ScanStmtVisitor(Lowerer &lowerer) noexcept : lowerer_(lowerer) {}

    void visit(const PrintStmt &stmt) override
    {
        for (const auto &it : stmt.items)
        {
            if (it.kind == PrintItem::Kind::Expr && it.expr)
                lowerer_.scanExpr(*it.expr);
        }
    }

    void visit(const LetStmt &stmt) override
    {
        if (stmt.expr)
            lowerer_.scanExpr(*stmt.expr);
        if (auto *var = dynamic_cast<const VarExpr *>(stmt.target.get()))
        {
            if (!var->name.empty())
            {
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
            }
            lowerer_.scanExpr(*arr->index);
        }
    }

    void visit(const DimStmt &stmt) override
    {
        lowerer_.requestHelper(Lowerer::RuntimeFeature::Alloc);
        if (!stmt.name.empty())
            lowerer_.setSymbolType(stmt.name, stmt.type);
        if (stmt.isArray)
            lowerer_.markArray(stmt.name);
        if (stmt.size)
            lowerer_.scanExpr(*stmt.size);
    }

    void visit(const RandomizeStmt &stmt) override
    {
        lowerer_.trackRuntime(Lowerer::RuntimeFeature::RandomizeI64);
        if (stmt.seed)
            lowerer_.scanExpr(*stmt.seed);
    }

    void visit(const IfStmt &stmt) override
    {
        if (stmt.cond)
            lowerer_.scanExpr(*stmt.cond);
        if (stmt.then_branch)
            lowerer_.scanStmt(*stmt.then_branch);
        for (const auto &elseif : stmt.elseifs)
        {
            if (elseif.cond)
                lowerer_.scanExpr(*elseif.cond);
            if (elseif.then_branch)
                lowerer_.scanStmt(*elseif.then_branch);
        }
        if (stmt.else_branch)
            lowerer_.scanStmt(*stmt.else_branch);
    }

    void visit(const WhileStmt &stmt) override
    {
        lowerer_.scanExpr(*stmt.cond);
        for (const auto &child : stmt.body)
        {
            if (child)
                lowerer_.scanStmt(*child);
        }
    }

    void visit(const ForStmt &stmt) override
    {
        if (!stmt.var.empty())
        {
            const auto *info = lowerer_.findSymbol(stmt.var);
            if (!info || !info->hasType)
                lowerer_.setSymbolType(stmt.var, inferAstTypeFromName(stmt.var));
        }
        lowerer_.scanExpr(*stmt.start);
        lowerer_.scanExpr(*stmt.end);
        if (stmt.step)
            lowerer_.scanExpr(*stmt.step);
        for (const auto &child : stmt.body)
        {
            if (child)
                lowerer_.scanStmt(*child);
        }
    }

    void visit(const NextStmt &) override {}

    void visit(const GotoStmt &) override {}

    void visit(const EndStmt &) override {}

    void visit(const InputStmt &stmt) override
    {
        lowerer_.requestHelper(Lowerer::RuntimeFeature::InputLine);
        if (stmt.prompt)
            lowerer_.scanExpr(*stmt.prompt);
        if (stmt.var.empty() || stmt.var.back() != '$')
            lowerer_.requestHelper(Lowerer::RuntimeFeature::ToInt);
        if (!stmt.var.empty())
        {
            const auto *info = lowerer_.findSymbol(stmt.var);
            if (!info || !info->hasType)
                lowerer_.setSymbolType(stmt.var, inferAstTypeFromName(stmt.var));
        }
    }

    void visit(const ReturnStmt &stmt) override
    {
        if (stmt.value)
            lowerer_.scanExpr(*stmt.value);
    }

    void visit(const FunctionDecl &stmt) override
    {
        for (const auto &child : stmt.body)
        {
            if (child)
                lowerer_.scanStmt(*child);
        }
    }

    void visit(const SubDecl &stmt) override
    {
        for (const auto &child : stmt.body)
        {
            if (child)
                lowerer_.scanStmt(*child);
        }
    }

    void visit(const StmtList &stmt) override
    {
        for (const auto &child : stmt.stmts)
        {
            if (child)
                lowerer_.scanStmt(*child);
        }
    }

  private:
    Lowerer &lowerer_;
};

/// @brief Scans a unary expression and propagates operand requirements.
/// @param u BASIC unary expression to inspect.
/// @return The inferred type of the operand expression.
/// @details Delegates scanning to the operand and introduces no additional runtime
/// dependencies on its own.
Lowerer::ExprType Lowerer::scanUnaryExpr(const UnaryExpr &u)
{
    return scanExpr(*u.expr);
}

/// @brief Scans a binary expression, recording runtime helpers for string operations.
/// @param b BASIC binary expression to inspect.
/// @return The resulting expression type after combining both operands.
/// @details Recursively scans both child expressions. String concatenation marks the
/// runtime concatenation helper as required, while string equality/inequality enables
/// the runtime string comparison helper. Logical operators produce boolean types.
Lowerer::ExprType Lowerer::scanBinaryExpr(const BinaryExpr &b)
{
    ExprType lt = scanExpr(*b.lhs);
    ExprType rt = scanExpr(*b.rhs);
    if (b.op == BinaryExpr::Op::Add && lt == ExprType::Str && rt == ExprType::Str)
    {
        requestHelper(RuntimeFeature::Concat);
        return ExprType::Str;
    }
    if (b.op == BinaryExpr::Op::Eq || b.op == BinaryExpr::Op::Ne)
    {
        if (lt == ExprType::Str || rt == ExprType::Str)
            requestHelper(RuntimeFeature::StrEq);
        return ExprType::Bool;
    }
    if (b.op == BinaryExpr::Op::LogicalAndShort || b.op == BinaryExpr::Op::LogicalOrShort ||
        b.op == BinaryExpr::Op::LogicalAnd || b.op == BinaryExpr::Op::LogicalOr)
        return ExprType::Bool;
    if (lt == ExprType::F64 || rt == ExprType::F64)
        return ExprType::F64;
    return ExprType::I64;
}

/// @brief Scans an array access expression to capture index dependencies.
/// @param arr BASIC array expression being inspected.
/// @return Always reports an integer result for array loads.
/// @details Recursively scans the index child expression so that nested requirements
/// propagate to the containing expression.
Lowerer::ExprType Lowerer::scanArrayExpr(const ArrayExpr &arr)
{
    scanExpr(*arr.index);
    return ExprType::I64;
}

/// @brief Scans a BASIC builtin call using declarative metadata.
/// @param c Builtin call expression to inspect.
/// @return The inferred result type of the builtin invocation.
/// @details Applies the BuiltinScanRule for the builtin to traverse arguments and
/// request any runtime helpers required by the call.
Lowerer::ExprType Lowerer::scanBuiltinCallExpr(const BuiltinCallExpr &c)
{
    const auto &rule = getBuiltinScanRule(c.builtin);
    std::vector<std::optional<ExprType>> argTypes(c.args.size());

    auto scanArg = [&](std::size_t idx) {
        if (idx >= c.args.size())
            return;
        const auto &arg = c.args[idx];
        if (!arg)
            return;
        argTypes[idx] = scanExpr(*arg);
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

    auto hasArg = [&](std::size_t idx) {
        return idx < c.args.size() && c.args[idx] != nullptr;
    };

    auto argType = [&](std::size_t idx) -> std::optional<ExprType> {
        if (idx >= argTypes.size())
            return std::nullopt;
        return argTypes[idx];
    };

    using Feature = BuiltinScanRule::Feature;
    for (const auto &feature : rule.features)
    {
        bool apply = false;
        switch (feature.condition)
        {
            case Feature::Condition::Always:
                apply = true;
                break;
            case Feature::Condition::IfArgPresent:
                apply = hasArg(feature.argIndex);
                break;
            case Feature::Condition::IfArgMissing:
                apply = !hasArg(feature.argIndex);
                break;
            case Feature::Condition::IfArgTypeIs:
            {
                auto ty = argType(feature.argIndex);
                apply = ty && *ty == feature.type;
                break;
            }
            case Feature::Condition::IfArgTypeIsNot:
            {
                auto ty = argType(feature.argIndex);
                apply = ty && *ty != feature.type;
                break;
            }
        }

        if (!apply)
            continue;

        switch (feature.action)
        {
            case Feature::Action::Request:
                requestHelper(feature.feature);
                break;
            case Feature::Action::Track:
                trackRuntime(feature.feature);
                break;
        }
    }

    ExprType result = rule.result.type;
    if (rule.result.kind == BuiltinScanRule::ResultSpec::Kind::FromArg)
    {
        if (auto ty = argType(rule.result.argIndex))
            result = *ty;
    }
    return result;
}

/// @brief Scans an arbitrary expression node, dispatching to specialized helpers.
/// @param e Expression node to inspect.
/// @return The inferred BASIC expression type.
/// @details Instantiates a ScanExprVisitor that captures the result type and traverses
/// child expressions so nested runtime requirements are recorded. All expressions
/// default to integer type when no specific specialization applies.
Lowerer::ExprType Lowerer::scanExpr(const Expr &e)
{
    ScanExprVisitor visitor(*this);
    e.accept(visitor);
    return visitor.result();
}

/// @brief Scans a statement tree to accumulate runtime requirements from nested nodes.
/// @param s Statement node to inspect.
/// @details Walks each statement form, scanning contained expressions and child
/// statements so that every reachable expression contributes its requirements.
void Lowerer::scanStmt(const Stmt &s)
{
    ScanStmtVisitor visitor(*this);
    s.accept(visitor);
}

/// @brief Scans a full BASIC program for runtime requirements.
/// @param prog Parsed BASIC program to inspect.
/// @details Visits all procedure declarations and main statements, delegating to
/// scanStmt for each top-level statement.
void Lowerer::scanProgram(const Program &prog)
{
    for (const auto &s : prog.procs)
        scanStmt(*s);
    for (const auto &s : prog.main)
        scanStmt(*s);
}

} // namespace il::frontends::basic
