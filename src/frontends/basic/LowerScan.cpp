// File: src/frontends/basic/LowerScan.cpp
// Purpose: Implements AST scanning to compute expression types and runtime requirements.
// Key invariants: Scanning only mutates bookkeeping flags; no IR emitted.
// Ownership/Lifetime: Operates on Lowerer state without owning AST or module.
// Links: docs/class-catalog.md

#include "frontends/basic/Lowerer.hpp"

namespace il::frontends::basic
{

// Purpose: scan unary expr.
// Parameters: const UnaryExpr &u.
// Returns: Lowerer::ExprType.
// Side effects: may modify lowering state or emit IL.
Lowerer::ExprType Lowerer::scanUnaryExpr(const UnaryExpr &u)
{
    return scanExpr(*u.expr);
}

// Purpose: scan binary expr.
// Parameters: const BinaryExpr &b.
// Returns: Lowerer::ExprType.
// Side effects: may modify lowering state or emit IL.
Lowerer::ExprType Lowerer::scanBinaryExpr(const BinaryExpr &b)
{
    ExprType lt = scanExpr(*b.lhs);
    ExprType rt = scanExpr(*b.rhs);
    if (b.op == BinaryExpr::Op::Add && lt == ExprType::Str && rt == ExprType::Str)
    {
        needRtConcat = true;
        return ExprType::Str;
    }
    if (b.op == BinaryExpr::Op::Eq || b.op == BinaryExpr::Op::Ne)
    {
        if (lt == ExprType::Str || rt == ExprType::Str)
            needRtStrEq = true;
        return ExprType::Bool;
    }
    if (lt == ExprType::F64 || rt == ExprType::F64)
        return ExprType::F64;
    return ExprType::I64;
}

// Purpose: scan array expr.
// Parameters: const ArrayExpr &arr.
// Returns: Lowerer::ExprType.
// Side effects: may modify lowering state or emit IL.
Lowerer::ExprType Lowerer::scanArrayExpr(const ArrayExpr &arr)
{
    scanExpr(*arr.index);
    return ExprType::I64;
}

// Purpose: scan builtin call expr.
// Parameters: const BuiltinCallExpr &c.
// Returns: Lowerer::ExprType.
// Side effects: may modify lowering state or emit IL.
Lowerer::ExprType Lowerer::scanBuiltinCallExpr(const BuiltinCallExpr &c)
{
    switch (c.builtin)
    {
        case BuiltinCallExpr::Builtin::Rnd:
            trackRuntime(RuntimeFn::Rnd);
            return ExprType::F64;
        case BuiltinCallExpr::Builtin::Val:
            needRtToInt = true;
            if (c.args[0])
                scanExpr(*c.args[0]);
            return ExprType::I64;
        case BuiltinCallExpr::Builtin::Str:
            needRtIntToStr = true;
            needRtF64ToStr = true;
            if (c.args[0])
                scanExpr(*c.args[0]);
            return ExprType::Str;
        case BuiltinCallExpr::Builtin::Len:
            if (c.args[0])
                scanExpr(*c.args[0]);
            return ExprType::I64;
        case BuiltinCallExpr::Builtin::Left:
            needRtLeft = true;
            for (auto &a : c.args)
                if (a)
                    scanExpr(*a);
            return ExprType::Str;
        case BuiltinCallExpr::Builtin::Right:
            needRtRight = true;
            for (auto &a : c.args)
                if (a)
                    scanExpr(*a);
            return ExprType::Str;
        case BuiltinCallExpr::Builtin::Mid:
            if (c.args.size() >= 3 && c.args[2])
                needRtMid3 = true;
            else
                needRtMid2 = true;
            for (auto &a : c.args)
                if (a)
                    scanExpr(*a);
            return ExprType::Str;
        case BuiltinCallExpr::Builtin::Instr:
            if (c.args.size() >= 3 && c.args[0])
                needRtInstr3 = true;
            else
                needRtInstr2 = true;
            for (auto &a : c.args)
                if (a)
                    scanExpr(*a);
            return ExprType::I64;
        case BuiltinCallExpr::Builtin::Ltrim:
            needRtLtrim = true;
            if (c.args[0])
                scanExpr(*c.args[0]);
            return ExprType::Str;
        case BuiltinCallExpr::Builtin::Rtrim:
            needRtRtrim = true;
            if (c.args[0])
                scanExpr(*c.args[0]);
            return ExprType::Str;
        case BuiltinCallExpr::Builtin::Trim:
            needRtTrim = true;
            if (c.args[0])
                scanExpr(*c.args[0]);
            return ExprType::Str;
        case BuiltinCallExpr::Builtin::Ucase:
            needRtUcase = true;
            if (c.args[0])
                scanExpr(*c.args[0]);
            return ExprType::Str;
        case BuiltinCallExpr::Builtin::Lcase:
            needRtLcase = true;
            if (c.args[0])
                scanExpr(*c.args[0]);
            return ExprType::Str;
        case BuiltinCallExpr::Builtin::Chr:
            needRtChr = true;
            if (c.args[0])
                scanExpr(*c.args[0]);
            return ExprType::Str;
        case BuiltinCallExpr::Builtin::Asc:
            needRtAsc = true;
            if (c.args[0])
                scanExpr(*c.args[0]);
            return ExprType::I64;
        case BuiltinCallExpr::Builtin::Sqr:
            if (c.args[0])
                scanExpr(*c.args[0]);
            trackRuntime(RuntimeFn::Sqrt);
            return ExprType::F64;
        case BuiltinCallExpr::Builtin::Abs:
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
        case BuiltinCallExpr::Builtin::Floor:
            if (c.args[0])
                scanExpr(*c.args[0]);
            trackRuntime(RuntimeFn::Floor);
            return ExprType::F64;
        case BuiltinCallExpr::Builtin::Ceil:
            if (c.args[0])
                scanExpr(*c.args[0]);
            trackRuntime(RuntimeFn::Ceil);
            return ExprType::F64;
        case BuiltinCallExpr::Builtin::Sin:
            if (c.args[0])
                scanExpr(*c.args[0]);
            trackRuntime(RuntimeFn::Sin);
            return ExprType::F64;
        case BuiltinCallExpr::Builtin::Cos:
            if (c.args[0])
                scanExpr(*c.args[0]);
            trackRuntime(RuntimeFn::Cos);
            return ExprType::F64;
        case BuiltinCallExpr::Builtin::Pow:
            if (c.args[0])
                scanExpr(*c.args[0]);
            if (c.args[1])
                scanExpr(*c.args[1]);
            trackRuntime(RuntimeFn::Pow);
            return ExprType::F64;
        case BuiltinCallExpr::Builtin::Int:
            if (c.args[0])
                scanExpr(*c.args[0]);
            return ExprType::I64;
        default:
            for (auto &a : c.args)
                if (a)
                    scanExpr(*a);
            return ExprType::I64;
    }
}

// Purpose: scan expr.
// Parameters: const Expr &e.
// Returns: Lowerer::ExprType.
// Side effects: may modify lowering state or emit IL.
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

// Purpose: scan stmt.
// Parameters: const Stmt &s.
// Returns: void.
// Side effects: may modify lowering state or emit IL.
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
        needInputLine = true;
        if (inp->prompt)
            scanExpr(*inp->prompt);
        if (inp->var.empty() || inp->var.back() != '$')
            needRtToInt = true;
    }
    else if (auto *d = dynamic_cast<const DimStmt *>(&s))
    {
        needAlloc = true;
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

// Purpose: scan program.
// Parameters: const Program &prog.
// Returns: void.
// Side effects: may modify lowering state or emit IL.
void Lowerer::scanProgram(const Program &prog)
{
    for (const auto &s : prog.procs)
        scanStmt(*s);
    for (const auto &s : prog.main)
        scanStmt(*s);
}

} // namespace il::frontends::basic
