// File: src/frontends/basic/ConstFolder.cpp
// Purpose: Implements constant folding for BASIC AST nodes with table-driven
// dispatch.
// Key invariants: Folding preserves 64-bit wrap-around semantics.
// Ownership/Lifetime: AST nodes are mutated in place.
// Links: docs/class-catalog.md

#include "frontends/basic/ConstFolder.hpp"
#include "frontends/basic/ConstFoldHelpers.hpp"
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>

namespace il::frontends::basic
{

namespace detail
{
/// @brief Interpret expression @p e as a numeric literal.
/// @param e Expression to inspect.
/// @return Numeric wrapper if @p e is an IntExpr or FloatExpr; std::nullopt otherwise.
/// @invariant Does not evaluate non-literal expressions.
std::optional<Numeric> asNumeric(const Expr &e)
{
    if (auto *i = dynamic_cast<const IntExpr *>(&e))
        return Numeric{false, static_cast<double>(i->value), static_cast<long long>(i->value)};
    if (auto *f = dynamic_cast<const FloatExpr *>(&e))
        return Numeric{true, f->value, static_cast<long long>(f->value)};
    return std::nullopt;
}

/// @brief Promote @p a to floating-point if either operand is float.
/// @param a First numeric operand.
/// @param b Second numeric operand.
/// @return @p a converted to float when necessary; otherwise @p a unchanged.
/// @invariant Integer value @p a.i remains intact after promotion.
Numeric promote(const Numeric &a, const Numeric &b)
{
    if (a.isFloat || b.isFloat)
        return Numeric{true, a.isFloat ? a.f : static_cast<double>(a.i), a.i};
    return a;
}

ExprPtr foldStringConcat(const StringExpr &l, const StringExpr &r)
{
    return foldString(l, r, [](const std::string &a, const std::string &b) -> ExprPtr
                      {
                          auto out = std::make_unique<StringExpr>();
                          out->value = a + b;
                          return out;
                      });
}

ExprPtr foldStringEq(const StringExpr &l, const StringExpr &r)
{
    return foldString(l, r, [](const std::string &a, const std::string &b) -> ExprPtr
                      {
                          auto out = std::make_unique<IntExpr>();
                          out->value = (a == b) ? 1 : 0;
                          return out;
                      });
}

ExprPtr foldStringNe(const StringExpr &l, const StringExpr &r)
{
    return foldString(l, r, [](const std::string &a, const std::string &b) -> ExprPtr
                      {
                          auto out = std::make_unique<IntExpr>();
                          out->value = (a != b) ? 1 : 0;
                          return out;
                      });
}
} // namespace detail

/// @brief Fold numeric binary expression using callback @p op.
/// @param l Left operand expression.
/// @param r Right operand expression.
/// @param op Callback operating on promoted numerics and returning optional result.
/// @return Folded literal or nullptr if operands aren't numeric or @p op fails.
/// @invariant Preserves 64-bit wrap-around semantics for integers.
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
    out->value = res->i;
    return out;
}

namespace
{

/// @brief Add @p a and @p b with 64-bit wrap-around semantics.
/// @param a Left operand.
/// @param b Right operand.
/// @return Sum modulo 2^64.
/// @invariant Uses unsigned addition to emulate BASIC overflow behavior.
static long long wrapAdd(long long a, long long b)
{
    return static_cast<long long>(static_cast<uint64_t>(a) + static_cast<uint64_t>(b));
}

/// @brief Subtract @p b from @p a with 64-bit wrap-around semantics.
/// @param a Left operand.
/// @param b Right operand.
/// @return Difference modulo 2^64.
/// @invariant Uses unsigned subtraction to emulate BASIC overflow behavior.
static long long wrapSub(long long a, long long b)
{
    return static_cast<long long>(static_cast<uint64_t>(a) - static_cast<uint64_t>(b));
}

/// @brief Multiply @p a and @p b with 64-bit wrap-around semantics.
/// @param a Left operand.
/// @param b Right operand.
/// @return Product modulo 2^64.
/// @invariant Uses unsigned multiplication to avoid overflow traps.
static long long wrapMul(long long a, long long b)
{
    return static_cast<long long>(static_cast<uint64_t>(a) * static_cast<uint64_t>(b));
}

/// @brief Check whether expression @p e is a string literal.
/// @param e Expression to inspect.
/// @param s Output string populated when @p e is a StringExpr.
/// @return True if @p e is a string literal.
/// @invariant @p s is assigned only when the function returns true.
static bool isStr(const Expr *e, std::string &s)
{
    if (auto *st = dynamic_cast<const StringExpr *>(e))
    {
        s = st->value;
        return true;
    }
    return false;
}

/// @brief Forward declaration for recursive expression folding.
static void foldExpr(ExprPtr &e);

/// @brief Replace expression @p e with an integer literal.
/// @param e Expression pointer to replace.
/// @param v Integer value to insert.
/// @param loc Source location for the new literal.
/// @invariant Ownership of @p e transfers to the newly created IntExpr.
static void replaceWithInt(ExprPtr &e, long long v, support::SourceLoc loc)
{
    auto ni = std::make_unique<IntExpr>();
    ni->loc = loc;
    ni->value = v;
    e = std::move(ni);
}

/// @brief Replace expression @p e with a string literal.
/// @param e Expression pointer to replace.
/// @param s String value to insert.
/// @param loc Source location for the new literal.
/// @invariant Ownership of @p e transfers to the newly created StringExpr.
static void replaceWithStr(ExprPtr &e, std::string s, support::SourceLoc loc)
{
    auto ns = std::make_unique<StringExpr>();
    ns->loc = loc;
    ns->value = std::move(s);
    e = std::move(ns);
}

/// @brief Attempt to fold built-in call expression @p c.
/// @param e Expression pointer to replace on success.
/// @param c Builtin call expression to evaluate.
/// @invariant Only pure builtins with constant arguments are folded.
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

/// @brief Fold unary expression @p u when its operand is constant.
/// @param e Expression pointer to replace.
/// @param u Unary expression to evaluate.
/// @invariant Only logical NOT on integer literals is supported.
static void foldUnary(ExprPtr &e, UnaryExpr *u)
{
    foldExpr(u->expr);
    auto n = detail::asNumeric(*u->expr);
    if (n && !n->isFloat && u->op == UnaryExpr::Op::Not)
        replaceWithInt(e, n->i == 0 ? 1 : 0, u->loc);
}

/// @brief Fold binary expression @p b when both operands are constant.
/// @param e Expression pointer to replace.
/// @param b Binary expression to evaluate.
/// @invariant Attempts numeric folding first, then string operations.
static void foldBinary(ExprPtr &e, BinaryExpr *b)
{
    foldExpr(b->lhs);
    foldExpr(b->rhs);
    if (const auto *entry = detail::findBinaryFold(b->op))
    {
        if (entry->numeric)
        {
            if (auto res = entry->numeric(*b->lhs, *b->rhs))
            {
                res->loc = b->loc;
                e = std::move(res);
                return;
            }
        }

        if (entry->string)
        {
            if (auto *ls = dynamic_cast<StringExpr *>(b->lhs.get()))
            {
                if (auto *rs = dynamic_cast<StringExpr *>(b->rhs.get()))
                {
                    if (auto res = entry->string(*ls, *rs))
                    {
                        res->loc = b->loc;
                        e = std::move(res);
                    }
                }
            }
        }
    }
}

/// @brief Recursively fold constants within expression @p e.
/// @param e Expression pointer to process.
/// @invariant Replaces @p e with literal nodes when folding succeeds.
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

/// @brief Recursively fold constants within statement @p s.
/// @param s Statement pointer to process.
/// @invariant Traverses nested AST nodes without altering control flow.
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

namespace detail
{

ExprPtr foldNumericAdd(const Expr &l, const Expr &r)
{
    return foldArithmetic(
        l,
        r,
        [](double a, double b) { return a + b; },
        [](long long a, long long b) { return wrapAdd(a, b); });
}

ExprPtr foldNumericSub(const Expr &l, const Expr &r)
{
    return foldArithmetic(
        l,
        r,
        [](double a, double b) { return a - b; },
        [](long long a, long long b) { return wrapSub(a, b); });
}

ExprPtr foldNumericMul(const Expr &l, const Expr &r)
{
    return foldArithmetic(
        l,
        r,
        [](double a, double b) { return a * b; },
        [](long long a, long long b) { return wrapMul(a, b); });
}

ExprPtr foldNumericDiv(const Expr &l, const Expr &r)
{
    return foldNumericBinary(
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
}

ExprPtr foldNumericIDiv(const Expr &l, const Expr &r)
{
    return foldNumericBinary(
        l,
        r,
        [](const Numeric &a, const Numeric &b) -> std::optional<Numeric>
        {
            if (a.isFloat || b.isFloat || b.i == 0)
                return std::nullopt;
            long long v = a.i / b.i;
            return Numeric{false, static_cast<double>(v), v};
        });
}

ExprPtr foldNumericMod(const Expr &l, const Expr &r)
{
    return foldNumericBinary(
        l,
        r,
        [](const Numeric &a, const Numeric &b) -> std::optional<Numeric>
        {
            if (a.isFloat || b.isFloat || b.i == 0)
                return std::nullopt;
            long long v = a.i % b.i;
            return Numeric{false, static_cast<double>(v), v};
        });
}

ExprPtr foldNumericEq(const Expr &l, const Expr &r)
{
    return foldCompare(
        l,
        r,
        [](double a, double b) { return a == b; },
        [](long long a, long long b) { return a == b; });
}

ExprPtr foldNumericNe(const Expr &l, const Expr &r)
{
    return foldCompare(
        l,
        r,
        [](double a, double b) { return a != b; },
        [](long long a, long long b) { return a != b; });
}

ExprPtr foldNumericLt(const Expr &l, const Expr &r)
{
    return foldCompare(
        l,
        r,
        [](double a, double b) { return a < b; },
        [](long long a, long long b) { return a < b; });
}

ExprPtr foldNumericLe(const Expr &l, const Expr &r)
{
    return foldCompare(
        l,
        r,
        [](double a, double b) { return a <= b; },
        [](long long a, long long b) { return a <= b; });
}

ExprPtr foldNumericGt(const Expr &l, const Expr &r)
{
    return foldCompare(
        l,
        r,
        [](double a, double b) { return a > b; },
        [](long long a, long long b) { return a > b; });
}

ExprPtr foldNumericGe(const Expr &l, const Expr &r)
{
    return foldCompare(
        l,
        r,
        [](double a, double b) { return a >= b; },
        [](long long a, long long b) { return a >= b; });
}

ExprPtr foldNumericAnd(const Expr &l, const Expr &r)
{
    return foldCompare(
        l,
        r,
        [](double, double) { return false; },
        [](long long a, long long b) { return (a != 0 && b != 0); },
        false);
}

ExprPtr foldNumericOr(const Expr &l, const Expr &r)
{
    return foldCompare(
        l,
        r,
        [](double, double) { return false; },
        [](long long a, long long b) { return (a != 0 || b != 0); },
        false);
}

} // namespace detail

void foldConstants(Program &prog)
{
    for (auto &s : prog.procs)
        foldStmt(s);
    for (auto &s : prog.main)
        foldStmt(s);
}

} // namespace il::frontends::basic
