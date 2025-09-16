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
#include <functional>
#include <optional>
#include <typeindex>
#include <unordered_map>

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

using detail::Numeric;

/// @brief Add @p a and @p b with 64-bit wrap-around semantics.
static long long wrapAdd(long long a, long long b)
{
    return static_cast<long long>(static_cast<uint64_t>(a) + static_cast<uint64_t>(b));
}

/// @brief Subtract @p b from @p a with 64-bit wrap-around semantics.
static long long wrapSub(long long a, long long b)
{
    return static_cast<long long>(static_cast<uint64_t>(a) - static_cast<uint64_t>(b));
}

/// @brief Multiply @p a and @p b with 64-bit wrap-around semantics.
static long long wrapMul(long long a, long long b)
{
    return static_cast<long long>(static_cast<uint64_t>(a) * static_cast<uint64_t>(b));
}

/// @brief Check whether expression @p e is a string literal and capture its value.
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
static void replaceWithInt(ExprPtr &e, long long v, support::SourceLoc loc)
{
    auto ni = std::make_unique<IntExpr>();
    ni->loc = loc;
    ni->value = v;
    e = std::move(ni);
}

/// @brief Replace expression @p e with a string literal.
static void replaceWithStr(ExprPtr &e, std::string s, support::SourceLoc loc)
{
    auto ns = std::make_unique<StringExpr>();
    ns->loc = loc;
    ns->value = std::move(s);
    e = std::move(ns);
}

/// @brief Result policy producing integer or float Numeric values based on operands.
struct ArithmeticResult
{
    static std::optional<Numeric> fromFloat(std::optional<double> value)
    {
        if (!value)
            return std::nullopt;
        return Numeric{true, *value, static_cast<long long>(*value)};
    }

    static std::optional<Numeric> fromInt(std::optional<long long> value)
    {
        if (!value)
            return std::nullopt;
        long long v = *value;
        return Numeric{false, static_cast<double>(v), v};
    }
};

/// @brief Result policy forcing floating-point outputs.
struct FloatResult
{
    static std::optional<Numeric> fromFloat(std::optional<double> value)
    {
        if (!value)
            return std::nullopt;
        return Numeric{true, *value, static_cast<long long>(*value)};
    }

    static std::optional<Numeric> fromInt(std::optional<double> value)
    {
        if (!value)
            return std::nullopt;
        return Numeric{true, *value, static_cast<long long>(*value)};
    }
};

/// @brief Result policy converting boolean results into integer numerics.
struct BoolResult
{
    static std::optional<Numeric> fromFloat(std::optional<bool> value)
    {
        if (!value)
            return std::nullopt;
        long long v = *value ? 1 : 0;
        return Numeric{false, static_cast<double>(v), v};
    }

    static std::optional<Numeric> fromInt(std::optional<bool> value)
    {
        return fromFloat(value);
    }
};

struct NumericRule
{
    std::function<std::optional<Numeric>(const Numeric &, const Numeric &)> fold;

    std::optional<Numeric> operator()(const Numeric &lhs, const Numeric &rhs) const
    {
        return fold(lhs, rhs);
    }
};

struct StringRule
{
    std::function<ExprPtr(const StringExpr &, const StringExpr &)> fold;

    ExprPtr operator()(const StringExpr &lhs, const StringExpr &rhs) const
    {
        return fold(lhs, rhs);
    }
};

struct BinaryRule
{
    std::optional<NumericRule> numeric;
    std::optional<StringRule> string;
};

struct BinaryOpHash
{
    size_t operator()(BinaryExpr::Op op) const noexcept
    {
        return static_cast<size_t>(op);
    }
};

template <typename ResultPolicy, bool AllowFloat = true, typename FloatOp, typename IntOp>
NumericRule makeNumericRule(FloatOp fop, IntOp iop)
{
    NumericRule rule;
    rule.fold = detail::NumericVisitor<ResultPolicy, AllowFloat, FloatOp, IntOp>{fop, iop};
    return rule;
}

template <typename Op>
StringRule makeStringRule(Op op)
{
    StringRule rule;
    rule.fold = [op](const StringExpr &lhs, const StringExpr &rhs) -> ExprPtr {
        return detail::foldString(lhs, rhs, op);
    };
    return rule;
}

static const std::unordered_map<BinaryExpr::Op, BinaryRule, BinaryOpHash> &binaryRules()
{
    static const std::unordered_map<BinaryExpr::Op, BinaryRule, BinaryOpHash> rules = {
        {BinaryExpr::Op::Add,
         BinaryRule{
             makeNumericRule<ArithmeticResult>(
                 [](double a, double b) -> std::optional<double> { return a + b; },
                 [](long long a, long long b) -> std::optional<long long> { return wrapAdd(a, b); }),
             makeStringRule([](const std::string &lhs, const std::string &rhs) -> ExprPtr {
                 auto out = std::make_unique<StringExpr>();
                 out->value = lhs + rhs;
                 return out;
             })}},
        {BinaryExpr::Op::Sub,
         BinaryRule{makeNumericRule<ArithmeticResult>(
                             [](double a, double b) -> std::optional<double> { return a - b; },
                             [](long long a, long long b) -> std::optional<long long> { return wrapSub(a, b); }),
                     std::nullopt}},
        {BinaryExpr::Op::Mul,
         BinaryRule{makeNumericRule<ArithmeticResult>(
                             [](double a, double b) -> std::optional<double> { return a * b; },
                             [](long long a, long long b) -> std::optional<long long> { return wrapMul(a, b); }),
                     std::nullopt}},
        {BinaryExpr::Op::Div,
         BinaryRule{makeNumericRule<FloatResult>(
             [](double a, double b) -> std::optional<double>
             {
                 if (b == 0.0)
                     return std::nullopt;
                 return a / b;
             },
             [](long long a, long long b) -> std::optional<double>
             {
                 if (b == 0)
                     return std::nullopt;
                 return static_cast<double>(a) / static_cast<double>(b);
             }),
                     std::nullopt}},
        {BinaryExpr::Op::IDiv,
         BinaryRule{makeNumericRule<ArithmeticResult, false>(
                             [](double, double) -> std::optional<double> { return std::nullopt; },
                             [](long long a, long long b) -> std::optional<long long>
                             {
                                 if (b == 0)
                                     return std::nullopt;
                                 return a / b;
                             }),
                     std::nullopt}},
        {BinaryExpr::Op::Mod,
         BinaryRule{makeNumericRule<ArithmeticResult, false>(
                             [](double, double) -> std::optional<double> { return std::nullopt; },
                             [](long long a, long long b) -> std::optional<long long>
                             {
                                 if (b == 0)
                                     return std::nullopt;
                                 return a % b;
                             }),
                     std::nullopt}},
        {BinaryExpr::Op::Eq,
         BinaryRule{
             makeNumericRule<BoolResult>(
                 [](double a, double b) -> std::optional<bool> { return a == b; },
                 [](long long a, long long b) -> std::optional<bool> { return a == b; }),
             makeStringRule([](const std::string &lhs, const std::string &rhs) -> ExprPtr {
                 auto out = std::make_unique<IntExpr>();
                 out->value = lhs == rhs ? 1 : 0;
                 return out;
             })}},
        {BinaryExpr::Op::Ne,
         BinaryRule{
             makeNumericRule<BoolResult>(
                 [](double a, double b) -> std::optional<bool> { return a != b; },
                 [](long long a, long long b) -> std::optional<bool> { return a != b; }),
             makeStringRule([](const std::string &lhs, const std::string &rhs) -> ExprPtr {
                 auto out = std::make_unique<IntExpr>();
                 out->value = lhs != rhs ? 1 : 0;
                 return out;
             })}},
        {BinaryExpr::Op::Lt,
         BinaryRule{makeNumericRule<BoolResult>(
                             [](double a, double b) -> std::optional<bool> { return a < b; },
                             [](long long a, long long b) -> std::optional<bool> { return a < b; }),
                     std::nullopt}},
        {BinaryExpr::Op::Le,
         BinaryRule{makeNumericRule<BoolResult>(
                             [](double a, double b) -> std::optional<bool> { return a <= b; },
                             [](long long a, long long b) -> std::optional<bool> { return a <= b; }),
                     std::nullopt}},
        {BinaryExpr::Op::Gt,
         BinaryRule{makeNumericRule<BoolResult>(
                             [](double a, double b) -> std::optional<bool> { return a > b; },
                             [](long long a, long long b) -> std::optional<bool> { return a > b; }),
                     std::nullopt}},
        {BinaryExpr::Op::Ge,
         BinaryRule{makeNumericRule<BoolResult>(
                             [](double a, double b) -> std::optional<bool> { return a >= b; },
                             [](long long a, long long b) -> std::optional<bool> { return a >= b; }),
                     std::nullopt}},
        {BinaryExpr::Op::And,
         BinaryRule{makeNumericRule<BoolResult, false>(
                             [](double, double) -> std::optional<bool> { return std::nullopt; },
                             [](long long a, long long b) -> std::optional<bool>
                             {
                                 return (a != 0 && b != 0);
                             }),
                     std::nullopt}},
        {BinaryExpr::Op::Or,
         BinaryRule{makeNumericRule<BoolResult, false>(
                             [](double, double) -> std::optional<bool> { return std::nullopt; },
                             [](long long a, long long b) -> std::optional<bool>
                             {
                                 return (a != 0 || b != 0);
                             }),
                     std::nullopt}},
    };
    return rules;
}

/// @brief Fold built-in call expression @p c when arguments are constant.
static void foldCall(ExprPtr &e, BuiltinCallExpr &c)
{
    for (auto &a : c.args)
        foldExpr(a);
    if (c.builtin == BuiltinCallExpr::Builtin::Len)
    {
        std::string s;
        if (c.args.size() == 1 && isStr(c.args[0].get(), s))
            replaceWithInt(e, static_cast<long long>(s.size()), c.loc);
    }
    else if (c.builtin == BuiltinCallExpr::Builtin::Mid)
    {
        if (c.args.size() == 3)
        {
            std::string s;
            if (isStr(c.args[0].get(), s))
            {
                auto nStart = detail::asNumeric(*c.args[1]);
                auto nLen = detail::asNumeric(*c.args[2]);
                if (nStart && nLen && !nStart->isFloat && !nLen->isFloat)
                {
                    long long start = nStart->i;
                    long long len = nLen->i;
                    if (start < 1)
                        start = 1;
                    if (len < 0)
                        len = 0;
                    size_t pos = static_cast<size_t>(start - 1);
                    replaceWithStr(e, s.substr(pos, static_cast<size_t>(len)), c.loc);
                }
            }
        }
    }
    else if (c.builtin == BuiltinCallExpr::Builtin::Val)
    {
        std::string s;
        if (c.args.size() == 1 && isStr(c.args[0].get(), s))
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
                replaceWithInt(e, v, c.loc);
        }
    }
    else if (c.builtin == BuiltinCallExpr::Builtin::Int)
    {
        if (c.args.size() == 1)
        {
            auto n = detail::asNumeric(*c.args[0]);
            if (n && n->isFloat)
                replaceWithInt(e, static_cast<long long>(n->f), c.loc);
        }
    }
    else if (c.builtin == BuiltinCallExpr::Builtin::Str)
    {
        if (c.args.size() == 1)
        {
            auto n = detail::asNumeric(*c.args[0]);
            if (n)
            {
                char buf[32];
                if (n->isFloat)
                    snprintf(buf, sizeof(buf), "%g", n->f);
                else
                    snprintf(buf, sizeof(buf), "%lld", n->i);
                replaceWithStr(e, buf, c.loc);
            }
        }
    }
}

/// @brief Fold unary expression @p u when its operand is constant.
static void foldUnary(ExprPtr &e, UnaryExpr &u)
{
    foldExpr(u.expr);
    auto n = detail::asNumeric(*u.expr);
    if (n && !n->isFloat && u.op == UnaryExpr::Op::Not)
        replaceWithInt(e, n->i == 0 ? 1 : 0, u.loc);
}

/// @brief Fold binary expression @p b when both operands are constant.
static void foldBinary(ExprPtr &e, BinaryExpr &b)
{
    foldExpr(b.lhs);
    foldExpr(b.rhs);

    const auto &rules = binaryRules();
    auto it = rules.find(b.op);
    if (it == rules.end())
        return;

    const BinaryRule &rule = it->second;
    if (rule.numeric)
    {
        if (auto res = detail::foldNumericBinary(*b.lhs, *b.rhs, *rule.numeric))
        {
            res->loc = b.loc;
            e = std::move(res);
            return;
        }
    }

    if (rule.string)
    {
        if (auto *ls = dynamic_cast<StringExpr *>(b.lhs.get()))
        {
            if (auto *rs = dynamic_cast<StringExpr *>(b.rhs.get()))
            {
                if (auto res = (*rule.string)(*ls, *rs))
                {
                    res->loc = b.loc;
                    e = std::move(res);
                }
            }
        }
    }
}

/// @brief Fold array index expression recursively.
static void foldArray(ExprPtr &, ArrayExpr &a)
{
    foldExpr(a.index);
}

using ExprHandler = void (*)(ExprPtr &, Expr &);

template <typename T, void (*Fn)(ExprPtr &, T &)> void dispatchExpr(ExprPtr &slot, Expr &node)
{
    Fn(slot, static_cast<T &>(node));
}

static const std::unordered_map<std::type_index, ExprHandler> &exprDispatchTable()
{
    static const std::unordered_map<std::type_index, ExprHandler> table = {
        {typeid(UnaryExpr), &dispatchExpr<UnaryExpr, foldUnary>},
        {typeid(BinaryExpr), &dispatchExpr<BinaryExpr, foldBinary>},
        {typeid(BuiltinCallExpr), &dispatchExpr<BuiltinCallExpr, foldCall>},
        {typeid(ArrayExpr), &dispatchExpr<ArrayExpr, foldArray>},
    };
    return table;
}

/// @brief Recursively fold constants within expression @p e.
/// @param e Expression pointer to process.
/// @invariant Replaces @p e with literal nodes when folding succeeds.
static void foldExpr(ExprPtr &e)
{
    if (!e)
        return;
    const auto &table = exprDispatchTable();
    auto it = table.find(typeid(*e));
    if (it != table.end())
        it->second(e, *e);
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

void foldConstants(Program &prog)
{
    for (auto &s : prog.procs)
        foldStmt(s);
    for (auto &s : prog.main)
        foldStmt(s);
}

} // namespace il::frontends::basic
