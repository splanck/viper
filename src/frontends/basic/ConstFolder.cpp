// File: src/frontends/basic/ConstFolder.cpp
// Purpose: Implements constant folding for BASIC AST nodes.
// Key invariants: Folding preserves 64-bit wrap-around semantics.
// Ownership/Lifetime: AST nodes are mutated in place.
// Links: docs/class-catalog.md

#include "frontends/basic/ConstFolder.hpp"
#include <cstdint>

namespace il::frontends::basic
{

namespace
{

static long long wrapAdd(long long a, long long b)
{
    return static_cast<long long>(static_cast<uint64_t>(a) + static_cast<uint64_t>(b));
}

static long long wrapSub(long long a, long long b)
{
    return static_cast<long long>(static_cast<uint64_t>(a) - static_cast<uint64_t>(b));
}

static long long wrapMul(long long a, long long b)
{
    return static_cast<long long>(static_cast<uint64_t>(a) * static_cast<uint64_t>(b));
}

static bool isInt(const Expr *e, long long &v)
{
    if (auto *i = dynamic_cast<const IntExpr *>(e))
    {
        v = i->value;
        return true;
    }
    return false;
}

static bool isStr(const Expr *e, std::string &s)
{
    if (auto *st = dynamic_cast<const StringExpr *>(e))
    {
        s = st->value;
        return true;
    }
    return false;
}

static void foldExpr(ExprPtr &e);

static void replaceWithInt(ExprPtr &e, long long v, support::SourceLoc loc)
{
    auto ni = std::make_unique<IntExpr>();
    ni->loc = loc;
    ni->value = v;
    e = std::move(ni);
}

static void replaceWithStr(ExprPtr &e, std::string s, support::SourceLoc loc)
{
    auto ns = std::make_unique<StringExpr>();
    ns->loc = loc;
    ns->value = std::move(s);
    e = std::move(ns);
}

static void foldCall(ExprPtr &e, CallExpr *c)
{
    for (auto &a : c->args)
        foldExpr(a);
    if (c->builtin == CallExpr::Builtin::Len)
    {
        std::string s;
        if (c->args.size() == 1 && isStr(c->args[0].get(), s))
            replaceWithInt(e, static_cast<long long>(s.size()), c->loc);
    }
    else if (c->builtin == CallExpr::Builtin::Mid)
    {
        if (c->args.size() == 3)
        {
            std::string s;
            long long start, len;
            if (isStr(c->args[0].get(), s) && isInt(c->args[1].get(), start) &&
                isInt(c->args[2].get(), len))
            {
                if (start < 1)
                    start = 1;
                if (len < 0)
                    len = 0;
                size_t pos = static_cast<size_t>(start - 1);
                replaceWithStr(e, s.substr(pos, static_cast<size_t>(len)), c->loc);
            }
        }
    }
}

static void foldUnary(ExprPtr &e, UnaryExpr *u)
{
    foldExpr(u->expr);
    long long v;
    if (isInt(u->expr.get(), v))
    {
        if (u->op == UnaryExpr::Op::Not)
            replaceWithInt(e, v == 0 ? 1 : 0, u->loc);
    }
}

static void foldBinary(ExprPtr &e, BinaryExpr *b)
{
    foldExpr(b->lhs);
    foldExpr(b->rhs);
    long long l, r;
    std::string ls, rs;
    if (isInt(b->lhs.get(), l) && isInt(b->rhs.get(), r))
    {
        switch (b->op)
        {
            case BinaryExpr::Op::Add:
                replaceWithInt(e, wrapAdd(l, r), b->loc);
                return;
            case BinaryExpr::Op::Sub:
                replaceWithInt(e, wrapSub(l, r), b->loc);
                return;
            case BinaryExpr::Op::Mul:
                replaceWithInt(e, wrapMul(l, r), b->loc);
                return;
            case BinaryExpr::Op::IDiv:
                if (r != 0)
                    replaceWithInt(e, l / r, b->loc);
                return;
            case BinaryExpr::Op::Mod:
                if (r != 0)
                    replaceWithInt(e, l % r, b->loc);
                return;
            case BinaryExpr::Op::Eq:
                replaceWithInt(e, l == r, b->loc);
                return;
            case BinaryExpr::Op::Ne:
                replaceWithInt(e, l != r, b->loc);
                return;
            case BinaryExpr::Op::Lt:
                replaceWithInt(e, l < r, b->loc);
                return;
            case BinaryExpr::Op::Le:
                replaceWithInt(e, l <= r, b->loc);
                return;
            case BinaryExpr::Op::Gt:
                replaceWithInt(e, l > r, b->loc);
                return;
            case BinaryExpr::Op::Ge:
                replaceWithInt(e, l >= r, b->loc);
                return;
            case BinaryExpr::Op::And:
                replaceWithInt(e, (l != 0 && r != 0) ? 1 : 0, b->loc);
                return;
            case BinaryExpr::Op::Or:
                replaceWithInt(e, (l != 0 || r != 0) ? 1 : 0, b->loc);
                return;
            default:
                break;
        }
    }
    else if (b->op == BinaryExpr::Op::Add && isStr(b->lhs.get(), ls) && isStr(b->rhs.get(), rs))
    {
        replaceWithStr(e, ls + rs, b->loc);
        return;
    }
}

static void foldExpr(ExprPtr &e)
{
    if (!e)
        return;
    if (auto *u = dynamic_cast<UnaryExpr *>(e.get()))
    {
        foldUnary(e, u);
    }
    else if (auto *b = dynamic_cast<BinaryExpr *>(e.get()))
    {
        foldBinary(e, b);
    }
    else if (auto *c = dynamic_cast<CallExpr *>(e.get()))
    {
        foldCall(e, c);
    }
    else if (auto *a = dynamic_cast<ArrayExpr *>(e.get()))
    {
        foldExpr(a->index);
    }
}

static void foldStmt(StmtPtr &s)
{
    if (!s)
        return;
    if (auto *lst = dynamic_cast<StmtList *>(s.get()))
    {
        for (auto &st : lst->stmts)
            foldStmt(st);
    }
    else if (auto *p = dynamic_cast<PrintStmt *>(s.get()))
    {
        for (auto &it : p->items)
            if (it.kind == PrintItem::Kind::Expr)
                foldExpr(it.expr);
    }
    else if (auto *l = dynamic_cast<LetStmt *>(s.get()))
    {
        foldExpr(l->target);
        foldExpr(l->expr);
    }
    else if (auto *i = dynamic_cast<IfStmt *>(s.get()))
    {
        foldExpr(i->cond);
        foldStmt(i->then_branch);
        for (auto &e : i->elseifs)
        {
            foldExpr(e.cond);
            foldStmt(e.then_branch);
        }
        foldStmt(i->else_branch);
    }
    else if (auto *w = dynamic_cast<WhileStmt *>(s.get()))
    {
        foldExpr(w->cond);
        for (auto &b : w->body)
            foldStmt(b);
    }
    else if (auto *f = dynamic_cast<ForStmt *>(s.get()))
    {
        foldExpr(f->start);
        foldExpr(f->end);
        if (f->step)
            foldExpr(f->step);
        for (auto &b : f->body)
            foldStmt(b);
    }
    else if (auto *d = dynamic_cast<DimStmt *>(s.get()))
    {
        foldExpr(d->size);
    }
}

} // namespace

void foldConstants(Program &prog)
{
    for (auto &s : prog.statements)
        foldStmt(s);
}

} // namespace il::frontends::basic
