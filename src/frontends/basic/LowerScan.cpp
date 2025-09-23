// File: src/frontends/basic/LowerScan.cpp
// License: MIT License. See LICENSE in the project root for full license information.
// Purpose: Implements AST scanning to compute expression types and runtime requirements.
// Key invariants: Scanning only mutates bookkeeping flags; no IR emitted.
// Ownership/Lifetime: Operates on Lowerer state without owning AST or module.
// Links: docs/class-catalog.md

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

Lowerer::ExprType exprTypeFromBuiltinKind(BuiltinValueKind kind)
{
    using ExprType = Lowerer::ExprType;
    switch (kind)
    {
        case BuiltinValueKind::Int:
            return ExprType::I64;
        case BuiltinValueKind::Float:
            return ExprType::F64;
        case BuiltinValueKind::String:
            return ExprType::Str;
        case BuiltinValueKind::Bool:
            return ExprType::Bool;
    }
    return ExprType::I64;
}

} // namespace

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

    const auto &info = getBuiltinInfo(c.builtin);
    ExprType result = ExprType::I64;
    if (info.semantics)
    {
        const auto &semantics = *info.semantics;
        result = exprTypeFromBuiltinKind(semantics.result.type);
        if (semantics.result.kind == BuiltinSemantics::ResultSpec::Kind::FromArg)
        {
            if (auto ty = argType(semantics.result.argIndex))
                result = *ty;
        }
    }
    return result;
}

/// @brief Scans an arbitrary expression node, dispatching to specialized helpers.
/// @param e Expression node to inspect.
/// @return The inferred BASIC expression type.
/// @details Uses dynamic casts to determine the concrete expression kind, scanning
/// child expressions as needed so that nested runtime requirements are recorded. All
/// expressions default to integer type when no specific specialization applies.
Lowerer::ExprType Lowerer::scanExpr(const Expr &e)
{
    if (dynamic_cast<const IntExpr *>(&e))
        return ExprType::I64;
    if (dynamic_cast<const FloatExpr *>(&e))
        return ExprType::F64;
    if (dynamic_cast<const StringExpr *>(&e))
        return ExprType::Str;
    if (auto *v = dynamic_cast<const VarExpr *>(&e))
    {
        auto it = varTypes.find(v->name);
        if (it != varTypes.end())
            return exprTypeFromAstType(it->second);
        return exprTypeFromAstType(inferAstTypeFromName(v->name));
    }
    if (auto *u = dynamic_cast<const UnaryExpr *>(&e))
        return scanUnaryExpr(*u);
    if (auto *b = dynamic_cast<const BinaryExpr *>(&e))
        return scanBinaryExpr(*b);
    if (auto *arr = dynamic_cast<const ArrayExpr *>(&e))
        return scanArrayExpr(*arr);
    if (auto *c = dynamic_cast<const BuiltinCallExpr *>(&e))
        return scanBuiltinCallExpr(*c);
    if (auto *c = dynamic_cast<const CallExpr *>(&e))
    {
        for (const auto &a : c->args)
            if (a)
                scanExpr(*a);
        return ExprType::I64;
    }
    return ExprType::I64;
}

/// @brief Scans a statement tree to accumulate runtime requirements from nested nodes.
/// @param s Statement node to inspect.
/// @details Walks each statement form, scanning contained expressions and child
/// statements so that every reachable expression contributes its requirements.
void Lowerer::scanStmt(const Stmt &s)
{
    if (auto *l = dynamic_cast<const LetStmt *>(&s))
    {
        if (l->expr)
            scanExpr(*l->expr);
        if (auto *var = dynamic_cast<const VarExpr *>(l->target.get()))
        {
            if (!var->name.empty() && varTypes.find(var->name) == varTypes.end())
                varTypes[var->name] = inferAstTypeFromName(var->name);
        }
        else if (auto *arr = dynamic_cast<const ArrayExpr *>(l->target.get()))
        {
            if (!arr->name.empty() && varTypes.find(arr->name) == varTypes.end())
                varTypes[arr->name] = inferAstTypeFromName(arr->name);
            scanExpr(*arr->index);
        }
    }
    else if (auto *p = dynamic_cast<const PrintStmt *>(&s))
    {
        for (const auto &it : p->items)
            if (it.kind == PrintItem::Kind::Expr && it.expr)
                scanExpr(*it.expr);
    }
    else if (auto *i = dynamic_cast<const IfStmt *>(&s))
    {
        if (i->cond)
            scanExpr(*i->cond);
        if (i->then_branch)
            scanStmt(*i->then_branch);
        for (const auto &ei : i->elseifs)
        {
            if (ei.cond)
                scanExpr(*ei.cond);
            if (ei.then_branch)
                scanStmt(*ei.then_branch);
        }
        if (i->else_branch)
            scanStmt(*i->else_branch);
    }
    else if (auto *w = dynamic_cast<const WhileStmt *>(&s))
    {
        scanExpr(*w->cond);
        for (const auto &st : w->body)
            scanStmt(*st);
    }
    else if (auto *f = dynamic_cast<const ForStmt *>(&s))
    {
        if (!f->var.empty() && varTypes.find(f->var) == varTypes.end())
            varTypes[f->var] = inferAstTypeFromName(f->var);
        scanExpr(*f->start);
        scanExpr(*f->end);
        if (f->step)
            scanExpr(*f->step);
        for (const auto &st : f->body)
            scanStmt(*st);
    }
    else if (auto *inp = dynamic_cast<const InputStmt *>(&s))
    {
        requestHelper(RuntimeFeature::InputLine);
        if (inp->prompt)
            scanExpr(*inp->prompt);
        if (inp->var.empty() || inp->var.back() != '$')
            requestHelper(RuntimeFeature::ToInt);
        if (!inp->var.empty() && varTypes.find(inp->var) == varTypes.end())
            varTypes[inp->var] = inferAstTypeFromName(inp->var);
    }
    else if (auto *d = dynamic_cast<const DimStmt *>(&s))
    {
        requestHelper(RuntimeFeature::Alloc);
        if (!d->name.empty())
            varTypes[d->name] = d->type;
        if (d->isArray)
            arrays.insert(d->name);
        if (d->size)
            scanExpr(*d->size);
    }
    else if (auto *r = dynamic_cast<const RandomizeStmt *>(&s))
    {
        trackRuntime(RuntimeFeature::RandomizeI64);
        if (r->seed)
            scanExpr(*r->seed);
    }
    else if (auto *ret = dynamic_cast<const ReturnStmt *>(&s))
    {
        if (ret->value)
            scanExpr(*ret->value);
    }
    else if (auto *fn = dynamic_cast<const FunctionDecl *>(&s))
    {
        for (const auto &bs : fn->body)
            scanStmt(*bs);
    }
    else if (auto *sub = dynamic_cast<const SubDecl *>(&s))
    {
        for (const auto &bs : sub->body)
            scanStmt(*bs);
    }
    else if (auto *lst = dynamic_cast<const StmtList *>(&s))
    {
        for (const auto &sub : lst->stmts)
            scanStmt(*sub);
    }
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
