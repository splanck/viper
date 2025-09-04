// File: src/frontends/basic/ConstFolder.cpp
// Purpose: Implements table-driven constant folding for BASIC AST nodes.
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
#include <type_traits>
#include <unordered_map>

namespace il::frontends::basic
{

std::optional<Numeric> asNumeric(const Expr &e)
{
    if (auto *i = dynamic_cast<const IntExpr *>(&e))
        return Numeric{false, 0.0, i->value};
    if (auto *f = dynamic_cast<const FloatExpr *>(&e))
        return Numeric{true, f->value, 0};
    return std::nullopt;
}

Numeric promote(const Numeric &a, const Numeric &b)
{
    if (a.isFloat || b.isFloat)
        return Numeric{true, a.isFloat ? a.f : static_cast<double>(a.i), 0};
    return a;
}

ExprPtr foldStringBinary(const StringExpr &L, TokenKind op, const StringExpr &R)
{
    if (op == TokenKind::Plus)
    {
        auto s = std::make_unique<StringExpr>();
        s->value = L.value + R.value;
        return s;
    }
    if (op == TokenKind::Equal)
    {
        auto i = std::make_unique<IntExpr>();
        i->value = (L.value == R.value);
        return i;
    }
    if (op == TokenKind::NotEqual)
    {
        auto i = std::make_unique<IntExpr>();
        i->value = (L.value != R.value);
        return i;
    }
    return nullptr;
}

template <typename F> ExprPtr foldNumericBinary(const Expr &L, const Expr &R, F op)
{
    auto ln = asNumeric(L);
    auto rn = asNumeric(R);
    if (!ln || !rn)
        return nullptr;
    Numeric pl = promote(*ln, *rn);
    Numeric pr = promote(*rn, *ln);
    std::optional<Numeric> res = pl.isFloat ? op(pl.f, pr.f) : op(pl.i, pr.i);
    if (!res)
        return nullptr;
    if (res->isFloat)
    {
        auto fe = std::make_unique<FloatExpr>();
        fe->value = res->f;
        return fe;
    }
    auto ie = std::make_unique<IntExpr>();
    ie->value = res->i;
    return ie;
}

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

using FoldFn = std::function<ExprPtr(const Expr &, const Expr &)>;

static const std::unordered_map<TokenKind, FoldFn> kNumericFold = {
    {TokenKind::Plus,
     [](const Expr &L, const Expr &R)
     {
         return foldNumericBinary(L,
                                  R,
                                  [](auto a, auto b) -> std::optional<Numeric>
                                  {
                                      using T = decltype(a);
                                      if constexpr (std::is_floating_point_v<T>)
                                          return Numeric{true, a + b, 0};
                                      else
                                          return Numeric{false, 0.0, wrapAdd(a, b)};
                                  });
     }},
    {TokenKind::Minus,
     [](const Expr &L, const Expr &R)
     {
         return foldNumericBinary(L,
                                  R,
                                  [](auto a, auto b) -> std::optional<Numeric>
                                  {
                                      using T = decltype(a);
                                      if constexpr (std::is_floating_point_v<T>)
                                          return Numeric{true, a - b, 0};
                                      else
                                          return Numeric{false, 0.0, wrapSub(a, b)};
                                  });
     }},
    {TokenKind::Star,
     [](const Expr &L, const Expr &R)
     {
         return foldNumericBinary(L,
                                  R,
                                  [](auto a, auto b) -> std::optional<Numeric>
                                  {
                                      using T = decltype(a);
                                      if constexpr (std::is_floating_point_v<T>)
                                          return Numeric{true, a * b, 0};
                                      else
                                          return Numeric{false, 0.0, wrapMul(a, b)};
                                  });
     }},
    {TokenKind::Slash,
     [](const Expr &L, const Expr &R)
     {
         return foldNumericBinary(L,
                                  R,
                                  [](auto a, auto b) -> std::optional<Numeric>
                                  {
                                      if (b == 0)
                                          return std::nullopt;
                                      return Numeric{
                                          true, static_cast<double>(a) / static_cast<double>(b), 0};
                                  });
     }},
    {TokenKind::Backslash,
     [](const Expr &L, const Expr &R)
     {
         return foldNumericBinary(L,
                                  R,
                                  [](auto a, auto b) -> std::optional<Numeric>
                                  {
                                      using T = decltype(a);
                                      if constexpr (std::is_floating_point_v<T>)
                                      {
                                          return std::nullopt;
                                      }
                                      else
                                      {
                                          if (b == 0)
                                              return std::nullopt;
                                          return Numeric{false, 0.0, a / b};
                                      }
                                  });
     }},
    {TokenKind::KeywordMod,
     [](const Expr &L, const Expr &R)
     {
         return foldNumericBinary(L,
                                  R,
                                  [](auto a, auto b) -> std::optional<Numeric>
                                  {
                                      using T = decltype(a);
                                      if constexpr (std::is_floating_point_v<T>)
                                      {
                                          return std::nullopt;
                                      }
                                      else
                                      {
                                          if (b == 0)
                                              return std::nullopt;
                                          return Numeric{false, 0.0, a % b};
                                      }
                                  });
     }},
    {TokenKind::Equal,
     [](const Expr &L, const Expr &R)
     {
         return foldNumericBinary(L,
                                  R,
                                  [](auto a, auto b) -> std::optional<Numeric>
                                  { return Numeric{false, 0.0, a == b}; });
     }},
    {TokenKind::NotEqual,
     [](const Expr &L, const Expr &R)
     {
         return foldNumericBinary(L,
                                  R,
                                  [](auto a, auto b) -> std::optional<Numeric>
                                  { return Numeric{false, 0.0, a != b}; });
     }},
    {TokenKind::Less,
     [](const Expr &L, const Expr &R)
     {
         return foldNumericBinary(L,
                                  R,
                                  [](auto a, auto b) -> std::optional<Numeric>
                                  { return Numeric{false, 0.0, a < b}; });
     }},
    {TokenKind::LessEqual,
     [](const Expr &L, const Expr &R)
     {
         return foldNumericBinary(L,
                                  R,
                                  [](auto a, auto b) -> std::optional<Numeric>
                                  { return Numeric{false, 0.0, a <= b}; });
     }},
    {TokenKind::Greater,
     [](const Expr &L, const Expr &R)
     {
         return foldNumericBinary(L,
                                  R,
                                  [](auto a, auto b) -> std::optional<Numeric>
                                  { return Numeric{false, 0.0, a > b}; });
     }},
    {TokenKind::GreaterEqual,
     [](const Expr &L, const Expr &R)
     {
         return foldNumericBinary(L,
                                  R,
                                  [](auto a, auto b) -> std::optional<Numeric>
                                  { return Numeric{false, 0.0, a >= b}; });
     }},
    {TokenKind::KeywordAnd,
     [](const Expr &L, const Expr &R)
     {
         return foldNumericBinary(L,
                                  R,
                                  [](auto a, auto b) -> std::optional<Numeric>
                                  { return Numeric{false, 0.0, (a != 0 && b != 0) ? 1 : 0}; });
     }},
    {TokenKind::KeywordOr,
     [](const Expr &L, const Expr &R)
     {
         return foldNumericBinary(L,
                                  R,
                                  [](auto a, auto b) -> std::optional<Numeric>
                                  { return Numeric{false, 0.0, (a != 0 || b != 0) ? 1 : 0}; });
     }},
};

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

static bool isFloat(const Expr *e, double &v)
{
    if (auto *f = dynamic_cast<const FloatExpr *>(e))
    {
        v = f->value;
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
    else if (c->builtin == CallExpr::Builtin::Val)
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
    else if (c->builtin == CallExpr::Builtin::Int)
    {
        double f;
        if (c->args.size() == 1 && isFloat(c->args[0].get(), f))
            replaceWithInt(e, static_cast<long long>(f), c->loc);
    }
    else if (c->builtin == CallExpr::Builtin::Str)
    {
        long long i;
        double f;
        if (c->args.size() == 1 && isInt(c->args[0].get(), i))
        {
            char buf[32];
            snprintf(buf, sizeof(buf), "%lld", i);
            replaceWithStr(e, buf, c->loc);
        }
        else if (c->args.size() == 1 && isFloat(c->args[0].get(), f))
        {
            char buf[32];
            snprintf(buf, sizeof(buf), "%g", f);
            replaceWithStr(e, buf, c->loc);
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
    auto toToken = [](BinaryExpr::Op op)
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
        return TokenKind::Plus;
    };

    TokenKind tk = toToken(b->op);
    if (auto *ls = dynamic_cast<StringExpr *>(b->lhs.get()))
    {
        if (auto *rs = dynamic_cast<StringExpr *>(b->rhs.get()))
        {
            if (auto se = foldStringBinary(*ls, tk, *rs))
            {
                se->loc = b->loc;
                e = std::move(se);
                return;
            }
        }
    }
    auto it = kNumericFold.find(tk);
    if (it != kNumericFold.end())
    {
        if (auto ne = it->second(*b->lhs, *b->rhs))
        {
            ne->loc = b->loc;
            e = std::move(ne);
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
