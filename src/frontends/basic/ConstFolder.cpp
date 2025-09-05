// File: src/frontends/basic/ConstFolder.cpp
// Purpose: Implements constant folding for BASIC AST nodes with table-driven
// dispatch.
// Key invariants: Folding preserves 64-bit wrap-around semantics.
// Ownership/Lifetime: AST nodes are mutated in place.
// Links: docs/class-catalog.md

#include "frontends/basic/ConstFolder.hpp"
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <optional>
#include <unordered_map>

namespace il::frontends::basic
{

namespace detail
{
std::optional<Numeric> asNumeric(const Expr &e)
{
    if (auto *i = dynamic_cast<const IntExpr *>(&e))
        return Numeric{false, static_cast<double>(i->value), static_cast<long long>(i->value)};
    if (auto *f = dynamic_cast<const FloatExpr *>(&e))
        return Numeric{true, f->value, static_cast<long long>(f->value)};
    return std::nullopt;
}

Numeric promote(const Numeric &a, const Numeric &b)
{
    if (a.isFloat || b.isFloat)
        return Numeric{true, a.isFloat ? a.f : static_cast<double>(a.i), a.i};
    return a;
}

ExprPtr foldStringBinary(const StringExpr &l, TokenKind op, const StringExpr &r)
{
    switch (op)
    {
        case TokenKind::Plus:
        {
            auto out = std::make_unique<StringExpr>();
            out->value = l.value + r.value;
            return out;
        }
        case TokenKind::Equal:
        case TokenKind::NotEqual:
        {
            auto out = std::make_unique<IntExpr>();
            bool res = (op == TokenKind::Equal) ? (l.value == r.value) : (l.value != r.value);
            out->value = res ? 1 : 0;
            return out;
        }
        default:
            return nullptr;
    }
}
} // namespace detail

template <typename F> ExprPtr detail::foldNumericBinary(const Expr &l, const Expr &r, F op)
{
    auto ln = asNumeric(l);
    auto rn = asNumeric(r);
    if (!ln || !rn)
        return nullptr;
    Numeric a = promote(*ln, *rn);
    Numeric b = promote(*rn, *ln);
    auto res = op(a, b);
    if (!res)
        return nullptr;
    if (res->isFloat)
    {
        auto out = std::make_unique<FloatExpr>();
        out->value = res->f;
        return out;
    }
    auto out = std::make_unique<IntExpr>();
    out->value = static_cast<int>(res->i);
    return out;
}

namespace
{

using detail::Numeric;

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
    ni->value = static_cast<int>(v);
    e = std::move(ni);
}

static void replaceWithStr(ExprPtr &e, std::string s, support::SourceLoc loc)
{
    auto ns = std::make_unique<StringExpr>();
    ns->loc = loc;
    ns->value = std::move(s);
    e = std::move(ns);
}

static void foldCall(ExprPtr &e, BuiltinCallExpr *c)
{
    for (auto &a : c->args)
        foldExpr(a);
    if (c->builtin == BuiltinCallExpr::Builtin::Len)
    {
        std::string s;
        if (c->args.size() == 1 && isStr(c->args[0].get(), s))
            replaceWithInt(e, static_cast<long long>(s.size()), c->loc);
    }
    else if (c->builtin == BuiltinCallExpr::Builtin::Mid)
    {
        if (c->args.size() == 3)
        {
            std::string s;
            if (isStr(c->args[0].get(), s))
            {
                auto nStart = detail::asNumeric(*c->args[1]);
                auto nLen = detail::asNumeric(*c->args[2]);
                if (nStart && nLen && !nStart->isFloat && !nLen->isFloat)
                {
                    long long start = nStart->i;
                    long long len = nLen->i;
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
    else if (c->builtin == BuiltinCallExpr::Builtin::Val)
    {
        std::string s;
        if (c->args.size() == 1 && isStr(c->args[0].get(), s))
        {
            const char *p = s.c_str();
            while (*p && isspace((unsigned char)*p))
                ++p;
            const char *q = p + strlen(p);
            while (q > p && isspace((unsigned char)q[-1]))
                --q;
            std::string trimmed(p, q - p);
            char *endp = nullptr;
            long long v = strtoll(trimmed.c_str(), &endp, 10);
            if (endp && *endp == '\0')
                replaceWithInt(e, v, c->loc);
        }
    }
    else if (c->builtin == BuiltinCallExpr::Builtin::Int)
    {
        if (c->args.size() == 1)
        {
            auto n = detail::asNumeric(*c->args[0]);
            if (n && n->isFloat)
                replaceWithInt(e, static_cast<long long>(n->f), c->loc);
        }
    }
    else if (c->builtin == BuiltinCallExpr::Builtin::Str)
    {
        if (c->args.size() == 1)
        {
            auto n = detail::asNumeric(*c->args[0]);
            if (n)
            {
                char buf[32];
                if (n->isFloat)
                    snprintf(buf, sizeof(buf), "%g", n->f);
                else
                    snprintf(buf, sizeof(buf), "%lld", n->i);
                replaceWithStr(e, buf, c->loc);
            }
        }
    }
}

static void foldUnary(ExprPtr &e, UnaryExpr *u)
{
    foldExpr(u->expr);
    auto n = detail::asNumeric(*u->expr);
    if (n && !n->isFloat && u->op == UnaryExpr::Op::Not)
        replaceWithInt(e, n->i == 0 ? 1 : 0, u->loc);
}

static TokenKind toToken(BinaryExpr::Op op)
{
    switch (op)
    {
        case BinaryExpr::Op::Add:
            return TokenKind::Plus;
        case BinaryExpr::Op::Sub:
            return TokenKind::Minus;
        case BinaryExpr::Op::Mul:
            return TokenKind::Star;
        case BinaryExpr::Op::Div:
            return TokenKind::Slash;
        case BinaryExpr::Op::IDiv:
            return TokenKind::Backslash;
        case BinaryExpr::Op::Mod:
            return TokenKind::KeywordMod;
        case BinaryExpr::Op::Eq:
            return TokenKind::Equal;
        case BinaryExpr::Op::Ne:
            return TokenKind::NotEqual;
        case BinaryExpr::Op::Lt:
            return TokenKind::Less;
        case BinaryExpr::Op::Le:
            return TokenKind::LessEqual;
        case BinaryExpr::Op::Gt:
            return TokenKind::Greater;
        case BinaryExpr::Op::Ge:
            return TokenKind::GreaterEqual;
        case BinaryExpr::Op::And:
            return TokenKind::KeywordAnd;
        case BinaryExpr::Op::Or:
            return TokenKind::KeywordOr;
    }
    return TokenKind::EndOfFile;
}

static void foldBinary(ExprPtr &e, BinaryExpr *b)
{
    foldExpr(b->lhs);
    foldExpr(b->rhs);
    TokenKind tk = toToken(b->op);

    using FoldFn = std::function<ExprPtr(const Expr &, const Expr &)>;
    static const std::unordered_map<TokenKind, FoldFn> numOps = {
        {TokenKind::Plus,
         [](const Expr &l, const Expr &r)
         {
             return detail::foldNumericBinary(
                 l,
                 r,
                 [](const Numeric &a, const Numeric &b) -> std::optional<Numeric>
                 {
                     if (a.isFloat)
                     {
                         double v = a.f + b.f;
                         return Numeric{true, v, static_cast<long long>(v)};
                     }
                     long long v = wrapAdd(a.i, b.i);
                     return Numeric{false, static_cast<double>(v), v};
                 });
         }},
        {TokenKind::Minus,
         [](const Expr &l, const Expr &r)
         {
             return detail::foldNumericBinary(
                 l,
                 r,
                 [](const Numeric &a, const Numeric &b) -> std::optional<Numeric>
                 {
                     if (a.isFloat)
                     {
                         double v = a.f - b.f;
                         return Numeric{true, v, static_cast<long long>(v)};
                     }
                     long long v = wrapSub(a.i, b.i);
                     return Numeric{false, static_cast<double>(v), v};
                 });
         }},
        {TokenKind::Star,
         [](const Expr &l, const Expr &r)
         {
             return detail::foldNumericBinary(
                 l,
                 r,
                 [](const Numeric &a, const Numeric &b) -> std::optional<Numeric>
                 {
                     if (a.isFloat)
                     {
                         double v = a.f * b.f;
                         return Numeric{true, v, static_cast<long long>(v)};
                     }
                     long long v = wrapMul(a.i, b.i);
                     return Numeric{false, static_cast<double>(v), v};
                 });
         }},
        {TokenKind::Slash,
         [](const Expr &l, const Expr &r)
         {
             return detail::foldNumericBinary(
                 l,
                 r,
                 [](const Numeric &a, const Numeric &b) -> std::optional<Numeric>
                 {
                     double rv = b.isFloat ? b.f : static_cast<double>(b.i);
                     if (rv == 0.0)
                         return std::nullopt;
                     double lv = a.isFloat ? a.f : static_cast<double>(a.i);
                     double v = lv / rv;
                     return Numeric{true, v, static_cast<long long>(v)};
                 });
         }},
        {TokenKind::Backslash,
         [](const Expr &l, const Expr &r)
         {
             return detail::foldNumericBinary(
                 l,
                 r,
                 [](const Numeric &a, const Numeric &b) -> std::optional<Numeric>
                 {
                     if (a.isFloat || b.isFloat || b.i == 0)
                         return std::nullopt;
                     long long v = a.i / b.i;
                     return Numeric{false, static_cast<double>(v), v};
                 });
         }},
        {TokenKind::KeywordMod,
         [](const Expr &l, const Expr &r)
         {
             return detail::foldNumericBinary(
                 l,
                 r,
                 [](const Numeric &a, const Numeric &b) -> std::optional<Numeric>
                 {
                     if (a.isFloat || b.isFloat || b.i == 0)
                         return std::nullopt;
                     long long v = a.i % b.i;
                     return Numeric{false, static_cast<double>(v), v};
                 });
         }},
        {TokenKind::Equal,
         [](const Expr &l, const Expr &r)
         {
             return detail::foldNumericBinary(
                 l,
                 r,
                 [](const Numeric &a, const Numeric &b) -> std::optional<Numeric>
                 {
                     bool res = a.isFloat ? (a.f == b.f) : (a.i == b.i);
                     return Numeric{false, static_cast<double>(res ? 1 : 0), res ? 1 : 0};
                 });
         }},
        {TokenKind::NotEqual,
         [](const Expr &l, const Expr &r)
         {
             return detail::foldNumericBinary(
                 l,
                 r,
                 [](const Numeric &a, const Numeric &b) -> std::optional<Numeric>
                 {
                     bool res = a.isFloat ? (a.f != b.f) : (a.i != b.i);
                     return Numeric{false, static_cast<double>(res ? 1 : 0), res ? 1 : 0};
                 });
         }},
        {TokenKind::Less,
         [](const Expr &l, const Expr &r)
         {
             return detail::foldNumericBinary(
                 l,
                 r,
                 [](const Numeric &a, const Numeric &b) -> std::optional<Numeric>
                 {
                     bool res = a.isFloat ? (a.f < b.f) : (a.i < b.i);
                     return Numeric{false, static_cast<double>(res ? 1 : 0), res ? 1 : 0};
                 });
         }},
        {TokenKind::LessEqual,
         [](const Expr &l, const Expr &r)
         {
             return detail::foldNumericBinary(
                 l,
                 r,
                 [](const Numeric &a, const Numeric &b) -> std::optional<Numeric>
                 {
                     bool res = a.isFloat ? (a.f <= b.f) : (a.i <= b.i);
                     return Numeric{false, static_cast<double>(res ? 1 : 0), res ? 1 : 0};
                 });
         }},
        {TokenKind::Greater,
         [](const Expr &l, const Expr &r)
         {
             return detail::foldNumericBinary(
                 l,
                 r,
                 [](const Numeric &a, const Numeric &b) -> std::optional<Numeric>
                 {
                     bool res = a.isFloat ? (a.f > b.f) : (a.i > b.i);
                     return Numeric{false, static_cast<double>(res ? 1 : 0), res ? 1 : 0};
                 });
         }},
        {TokenKind::GreaterEqual,
         [](const Expr &l, const Expr &r)
         {
             return detail::foldNumericBinary(
                 l,
                 r,
                 [](const Numeric &a, const Numeric &b) -> std::optional<Numeric>
                 {
                     bool res = a.isFloat ? (a.f >= b.f) : (a.i >= b.i);
                     return Numeric{false, static_cast<double>(res ? 1 : 0), res ? 1 : 0};
                 });
         }},
        {TokenKind::KeywordAnd,
         [](const Expr &l, const Expr &r)
         {
             return detail::foldNumericBinary(
                 l,
                 r,
                 [](const Numeric &a, const Numeric &b) -> std::optional<Numeric>
                 {
                     if (a.isFloat || b.isFloat)
                         return std::nullopt;
                     long long v = (a.i != 0 && b.i != 0) ? 1 : 0;
                     return Numeric{false, static_cast<double>(v), v};
                 });
         }},
        {TokenKind::KeywordOr,
         [](const Expr &l, const Expr &r)
         {
             return detail::foldNumericBinary(
                 l,
                 r,
                 [](const Numeric &a, const Numeric &b) -> std::optional<Numeric>
                 {
                     if (a.isFloat || b.isFloat)
                         return std::nullopt;
                     long long v = (a.i != 0 || b.i != 0) ? 1 : 0;
                     return Numeric{false, static_cast<double>(v), v};
                 });
         }},
    };

    auto it = numOps.find(tk);
    if (it != numOps.end())
    {
        if (auto res = it->second(*b->lhs, *b->rhs))
        {
            res->loc = b->loc;
            e = std::move(res);
            return;
        }
    }

    if (auto *ls = dynamic_cast<StringExpr *>(b->lhs.get()))
    {
        if (auto *rs = dynamic_cast<StringExpr *>(b->rhs.get()))
        {
            if (auto res = detail::foldStringBinary(*ls, tk, *rs))
            {
                res->loc = b->loc;
                e = std::move(res);
            }
        }
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
    else if (auto *c = dynamic_cast<BuiltinCallExpr *>(e.get()))
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
