// File: src/frontends/basic/LowerScan.cpp
// License: MIT License. See LICENSE in the project root for full license information.
// Purpose: Implements AST scanning to compute expression types and runtime requirements.
// Key invariants: Scanning only mutates bookkeeping flags; no IR emitted.
// Ownership/Lifetime: Operates on Lowerer state without owning AST or module.
// Links: docs/class-catalog.md

#include "frontends/basic/BuiltinRegistry.hpp"
#include "frontends/basic/Lowerer.hpp"

namespace il::frontends::basic
{

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
        requestHelper(RuntimeHelper::Concat);
        return ExprType::Str;
    }
    if (b.op == BinaryExpr::Op::Eq || b.op == BinaryExpr::Op::Ne)
    {
        if (lt == ExprType::Str || rt == ExprType::Str)
            requestHelper(RuntimeHelper::StrEq);
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

/// @brief Scans a BASIC builtin call and delegates to specialized helpers when present.
/// @param c Builtin call expression to inspect.
/// @return The inferred result type of the builtin invocation.
/// @details Looks up builtin metadata to find a scan helper. When a helper is
/// registered, it is invoked to set runtime requirements and scan child expressions;
/// otherwise, each argument is scanned generically and the expression is treated as
/// returning an integer value.
Lowerer::ExprType Lowerer::scanBuiltinCallExpr(const BuiltinCallExpr &c)
{
    const auto &info = getBuiltinInfo(c.builtin);
    if (info.scan)
        return (this->*(info.scan))(c);
    for (auto &a : c.args)
        if (a)
            scanExpr(*a);
    return ExprType::I64;
}

/// @brief Scans the LEN builtin to ensure its operand requirements are processed.
/// @param c Builtin call expression representing LEN.
/// @return Always yields an integer length value.
/// @details LEN does not require additional runtime helpers; it simply scans the
/// first argument when present so nested expressions propagate their requirements.
Lowerer::ExprType Lowerer::scanLen(const BuiltinCallExpr &c)
{
    if (c.args[0])
        scanExpr(*c.args[0]);
    return ExprType::I64;
}

/// @brief Scans the MID builtin and marks the appropriate runtime helper variant.
/// @param c Builtin call expression representing MID.
/// @return Reports a string result for the substring extraction.
/// @details Chooses between the two-argument and three-argument runtime helpers based
/// on the provided arguments, then scans each child argument to propagate additional
/// requirements.
Lowerer::ExprType Lowerer::scanMid(const BuiltinCallExpr &c)
{
    if (c.args.size() >= 3 && c.args[2])
        requestHelper(RuntimeHelper::Mid3);
    else
        requestHelper(RuntimeHelper::Mid2);
    for (auto &a : c.args)
        if (a)
            scanExpr(*a);
    return ExprType::Str;
}

/// @brief Scans the LEFT$ builtin and records its runtime dependency.
/// @param c Builtin call expression representing LEFT$.
/// @return Reports a string type for the substring result.
/// @details Enables the LEFT runtime helper and scans every provided argument so that
/// nested expressions contribute their requirements.
Lowerer::ExprType Lowerer::scanLeft(const BuiltinCallExpr &c)
{
    requestHelper(RuntimeHelper::Left);
    for (auto &a : c.args)
        if (a)
            scanExpr(*a);
    return ExprType::Str;
}

/// @brief Scans the RIGHT$ builtin and records its runtime dependency.
/// @param c Builtin call expression representing RIGHT$.
/// @return Reports a string type for the substring result.
/// @details Enables the RIGHT runtime helper and scans every provided argument so that
/// nested expressions contribute their requirements.
Lowerer::ExprType Lowerer::scanRight(const BuiltinCallExpr &c)
{
    requestHelper(RuntimeHelper::Right);
    for (auto &a : c.args)
        if (a)
            scanExpr(*a);
    return ExprType::Str;
}

/// @brief Scans the STR$ builtin and records numeric-to-string runtime needs.
/// @param c Builtin call expression representing STR$.
/// @return Reports a string type produced by the conversion.
/// @details Marks both integer and floating-point to string runtime helpers as required
/// and scans the argument expression when present.
Lowerer::ExprType Lowerer::scanStr(const BuiltinCallExpr &c)
{
    requestHelper(RuntimeHelper::IntToStr);
    requestHelper(RuntimeHelper::F64ToStr);
    if (c.args[0])
        scanExpr(*c.args[0]);
    return ExprType::Str;
}

/// @brief Scans the VAL builtin and records numeric parsing runtime usage.
/// @param c Builtin call expression representing VAL.
/// @return Reports an integer result for the parsed numeric value.
/// @details Enables the VAL runtime helper and scans the argument expression when
/// provided to capture nested requirements.
Lowerer::ExprType Lowerer::scanVal(const BuiltinCallExpr &c)
{
    requestHelper(RuntimeHelper::ToInt);
    if (c.args[0])
        scanExpr(*c.args[0]);
    return ExprType::I64;
}

/// @brief Scans the INT builtin to propagate operand requirements.
/// @param c Builtin call expression representing INT.
/// @return Returns the integer truncation result type.
/// @details INT does not require additional runtime helpers; it scans the first
/// argument when present.
Lowerer::ExprType Lowerer::scanInt(const BuiltinCallExpr &c)
{
    if (c.args[0])
        scanExpr(*c.args[0]);
    return ExprType::I64;
}

/// @brief Scans the SQR builtin and records the runtime square-root helper.
/// @param c Builtin call expression representing SQR.
/// @return Reports a floating-point result.
/// @details Scans the operand to propagate nested requirements and marks the runtime
/// square-root function as required.
Lowerer::ExprType Lowerer::scanSqr(const BuiltinCallExpr &c)
{
    if (c.args[0])
        scanExpr(*c.args[0]);
    trackRuntime(RuntimeFn::Sqrt);
    return ExprType::F64;
}

/// @brief Scans the ABS builtin and selects the appropriate runtime helper.
/// @param c Builtin call expression representing ABS.
/// @return Propagates the operand's numeric type.
/// @details Scans the operand to determine its type and then records either the integer
/// or floating-point absolute-value runtime helper based on the operand type.
Lowerer::ExprType Lowerer::scanAbs(const BuiltinCallExpr &c)
{
    ExprType ty = ExprType::I64;
    if (c.args[0])
        ty = scanExpr(*c.args[0]);
    if (ty == ExprType::F64)
        trackRuntime(RuntimeFn::AbsF64);
    else
        trackRuntime(RuntimeFn::AbsI64);
    return ty;
}

/// @brief Scans the FLOOR builtin and records its runtime helper requirement.
/// @param c Builtin call expression representing FLOOR.
/// @return Reports a floating-point result after flooring.
/// @details Scans the operand when present and marks the runtime floor helper.
Lowerer::ExprType Lowerer::scanFloor(const BuiltinCallExpr &c)
{
    if (c.args[0])
        scanExpr(*c.args[0]);
    trackRuntime(RuntimeFn::Floor);
    return ExprType::F64;
}

/// @brief Scans the CEIL builtin and records its runtime helper requirement.
/// @param c Builtin call expression representing CEIL.
/// @return Reports a floating-point result after ceiling.
/// @details Scans the operand when present and marks the runtime ceil helper.
Lowerer::ExprType Lowerer::scanCeil(const BuiltinCallExpr &c)
{
    if (c.args[0])
        scanExpr(*c.args[0]);
    trackRuntime(RuntimeFn::Ceil);
    return ExprType::F64;
}

/// @brief Scans the SIN builtin and records its runtime helper requirement.
/// @param c Builtin call expression representing SIN.
/// @return Reports a floating-point result from the sine operation.
/// @details Scans the operand when present and marks the runtime sine helper.
Lowerer::ExprType Lowerer::scanSin(const BuiltinCallExpr &c)
{
    if (c.args[0])
        scanExpr(*c.args[0]);
    trackRuntime(RuntimeFn::Sin);
    return ExprType::F64;
}

/// @brief Scans the COS builtin and records its runtime helper requirement.
/// @param c Builtin call expression representing COS.
/// @return Reports a floating-point result from the cosine operation.
/// @details Scans the operand when present and marks the runtime cosine helper.
Lowerer::ExprType Lowerer::scanCos(const BuiltinCallExpr &c)
{
    if (c.args[0])
        scanExpr(*c.args[0]);
    trackRuntime(RuntimeFn::Cos);
    return ExprType::F64;
}

/// @brief Scans the POW builtin and records its runtime helper requirement.
/// @param c Builtin call expression representing POW.
/// @return Reports a floating-point result from exponentiation.
/// @details Scans both base and exponent operands when present and marks the runtime
/// power helper.
Lowerer::ExprType Lowerer::scanPow(const BuiltinCallExpr &c)
{
    if (c.args[0])
        scanExpr(*c.args[0]);
    if (c.args[1])
        scanExpr(*c.args[1]);
    trackRuntime(RuntimeFn::Pow);
    return ExprType::F64;
}

/// @brief Scans the RND builtin and records its runtime helper requirement.
/// @return Reports a floating-point random result.
/// @details RND has no child expressions; it simply marks the runtime random helper.
Lowerer::ExprType Lowerer::scanRnd(const BuiltinCallExpr &)
{
    trackRuntime(RuntimeFn::Rnd);
    return ExprType::F64;
}

/// @brief Scans the INSTR builtin and selects the matching runtime helper variant.
/// @param c Builtin call expression representing INSTR.
/// @return Reports an integer position result.
/// @details Chooses between the two-argument and three-argument runtime helpers based
/// on the arguments supplied, scanning each child expression to capture nested
/// requirements.
Lowerer::ExprType Lowerer::scanInstr(const BuiltinCallExpr &c)
{
    if (c.args.size() >= 3 && c.args[0])
        requestHelper(RuntimeHelper::Instr3);
    else
        requestHelper(RuntimeHelper::Instr2);
    for (auto &a : c.args)
        if (a)
            scanExpr(*a);
    return ExprType::I64;
}

/// @brief Scans the LTRIM$ builtin and records its runtime helper requirement.
/// @param c Builtin call expression representing LTRIM$.
/// @return Reports a string result for the trimmed value.
/// @details Enables the left-trim runtime helper and scans the operand when present.
Lowerer::ExprType Lowerer::scanLtrim(const BuiltinCallExpr &c)
{
    requestHelper(RuntimeHelper::Ltrim);
    if (c.args[0])
        scanExpr(*c.args[0]);
    return ExprType::Str;
}

/// @brief Scans the RTRIM$ builtin and records its runtime helper requirement.
/// @param c Builtin call expression representing RTRIM$.
/// @return Reports a string result for the trimmed value.
/// @details Enables the right-trim runtime helper and scans the operand when present.
Lowerer::ExprType Lowerer::scanRtrim(const BuiltinCallExpr &c)
{
    requestHelper(RuntimeHelper::Rtrim);
    if (c.args[0])
        scanExpr(*c.args[0]);
    return ExprType::Str;
}

/// @brief Scans the TRIM$ builtin and records its runtime helper requirement.
/// @param c Builtin call expression representing TRIM$.
/// @return Reports a string result for the trimmed value.
/// @details Enables the full-trim runtime helper and scans the operand when present.
Lowerer::ExprType Lowerer::scanTrim(const BuiltinCallExpr &c)
{
    requestHelper(RuntimeHelper::Trim);
    if (c.args[0])
        scanExpr(*c.args[0]);
    return ExprType::Str;
}

/// @brief Scans the UCASE$ builtin and records its runtime helper requirement.
/// @param c Builtin call expression representing UCASE$.
/// @return Reports a string result for the upper-cased value.
/// @details Enables the uppercase runtime helper and scans the operand when present.
Lowerer::ExprType Lowerer::scanUcase(const BuiltinCallExpr &c)
{
    requestHelper(RuntimeHelper::Ucase);
    if (c.args[0])
        scanExpr(*c.args[0]);
    return ExprType::Str;
}

/// @brief Scans the LCASE$ builtin and records its runtime helper requirement.
/// @param c Builtin call expression representing LCASE$.
/// @return Reports a string result for the lower-cased value.
/// @details Enables the lowercase runtime helper and scans the operand when present.
Lowerer::ExprType Lowerer::scanLcase(const BuiltinCallExpr &c)
{
    requestHelper(RuntimeHelper::Lcase);
    if (c.args[0])
        scanExpr(*c.args[0]);
    return ExprType::Str;
}

/// @brief Scans the CHR$ builtin and records its runtime helper requirement.
/// @param c Builtin call expression representing CHR$.
/// @return Reports a string result from the character conversion.
/// @details Enables the character runtime helper and scans the operand when present.
Lowerer::ExprType Lowerer::scanChr(const BuiltinCallExpr &c)
{
    requestHelper(RuntimeHelper::Chr);
    if (c.args[0])
        scanExpr(*c.args[0]);
    return ExprType::Str;
}

/// @brief Scans the ASC builtin and records its runtime helper requirement.
/// @param c Builtin call expression representing ASC.
/// @return Reports an integer code point.
/// @details Enables the ASCII runtime helper and scans the operand when present.
Lowerer::ExprType Lowerer::scanAsc(const BuiltinCallExpr &c)
{
    requestHelper(RuntimeHelper::Asc);
    if (c.args[0])
        scanExpr(*c.args[0]);
    return ExprType::I64;
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
        if (!v->name.empty() && v->name.back() == '$')
            return ExprType::Str;
        if (!v->name.empty() && v->name.back() == '#')
            return ExprType::F64;
        return ExprType::I64;
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
        if (auto *arr = dynamic_cast<const ArrayExpr *>(l->target.get()))
            scanExpr(*arr->index);
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
        scanExpr(*f->start);
        scanExpr(*f->end);
        if (f->step)
            scanExpr(*f->step);
        for (const auto &st : f->body)
            scanStmt(*st);
    }
    else if (auto *inp = dynamic_cast<const InputStmt *>(&s))
    {
        requestHelper(RuntimeHelper::InputLine);
        if (inp->prompt)
            scanExpr(*inp->prompt);
        if (inp->var.empty() || inp->var.back() != '$')
            requestHelper(RuntimeHelper::ToInt);
    }
    else if (auto *d = dynamic_cast<const DimStmt *>(&s))
    {
        requestHelper(RuntimeHelper::Alloc);
        if (d->size)
            scanExpr(*d->size);
    }
    else if (auto *r = dynamic_cast<const RandomizeStmt *>(&s))
    {
        trackRuntime(RuntimeFn::RandomizeI64);
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
