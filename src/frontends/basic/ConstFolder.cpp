// File: src/frontends/basic/ConstFolder.cpp
// Purpose: Implements constant folding for BASIC AST nodes with table-driven
// dispatch.
// Key invariants: Folding preserves 64-bit wrap-around semantics.
// Ownership/Lifetime: AST nodes are mutated in place.
// Links: docs/class-catalog.md
// License: MIT (see LICENSE).

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
/// @brief Fold numeric arithmetic with lambda callbacks for float and integer operations.
template <typename FloatOp, typename IntOp>
ExprPtr foldArithmetic(const Expr &l, const Expr &r, FloatOp fop, IntOp iop)
{
    return foldNumericBinary(
        l,
        r,
        [fop, iop](const Numeric &a, const Numeric &b) -> std::optional<Numeric>
        {
            if (a.isFloat)
            {
                double v = fop(a.f, b.f);
                return Numeric{true, v, static_cast<long long>(v)};
            }
            long long v = iop(a.i, b.i);
            return Numeric{false, static_cast<double>(v), v};
        });
}

/// @brief Fold comparisons by dispatching to float and integer comparator callbacks.
template <typename FloatCmp, typename IntCmp>
ExprPtr foldCompare(
    const Expr &l, const Expr &r, FloatCmp fcmp, IntCmp icmp, bool allowFloat)
{
    return foldNumericBinary(
        l,
        r,
        [fcmp, icmp, allowFloat](const Numeric &a, const Numeric &b) -> std::optional<Numeric>
        {
            if (!allowFloat && (a.isFloat || b.isFloat))
                return std::nullopt;
            bool res = a.isFloat ? fcmp(a.f, b.f) : icmp(a.i, b.i);
            long long v = res ? 1 : 0;
            return Numeric{false, static_cast<double>(v), v};
        });
}

/// @brief Invoke a binary string folding callback using the literal payloads.
template <typename Op> ExprPtr foldString(const StringExpr &l, const StringExpr &r, Op op)
{
    return op(l.value, r.value);
}

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

const BinaryFoldEntry *findBinaryFold(BinaryExpr::Op op)
{
    for (const auto &entry : kBinaryFoldTable)
    {
        if (entry.op == op)
            return &entry;
    }
    return nullptr;
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
long long wrapAdd(long long a, long long b)
{
    return static_cast<long long>(static_cast<uint64_t>(a) + static_cast<uint64_t>(b));
}

/// @brief Subtract @p b from @p a with 64-bit wrap-around semantics.
/// @param a Left operand.
/// @param b Right operand.
/// @return Difference modulo 2^64.
/// @invariant Uses unsigned subtraction to emulate BASIC overflow behavior.
long long wrapSub(long long a, long long b)
{
    return static_cast<long long>(static_cast<uint64_t>(a) - static_cast<uint64_t>(b));
}

/// @brief Multiply @p a and @p b with 64-bit wrap-around semantics.
/// @param a Left operand.
/// @param b Right operand.
/// @return Product modulo 2^64.
/// @invariant Uses unsigned multiplication to avoid overflow traps.
long long wrapMul(long long a, long long b)
{
    return static_cast<long long>(static_cast<uint64_t>(a) * static_cast<uint64_t>(b));
}

/// @brief Check whether expression @p e is a string literal.
/// @param e Expression to inspect.
/// @param s Output string populated when @p e is a StringExpr.
/// @return True if @p e is a string literal.
/// @invariant @p s is assigned only when the function returns true.
bool isStringLiteral(const Expr &e, std::string &s)
{
    if (auto *st = dynamic_cast<const StringExpr *>(&e))
    {
        s = st->value;
        return true;
    }
    return false;
}

class ConstFolderPass : public ExprVisitor, public StmtVisitor
{
public:
    void run(Program &prog)
    {
        for (auto &decl : prog.procs)
            foldStmt(decl);
        for (auto &stmt : prog.main)
            foldStmt(stmt);
    }

private:
    void foldExpr(ExprPtr &expr)
    {
        if (!expr)
            return;
        ExprPtr *prev = currentExpr_;
        currentExpr_ = &expr;
        expr->accept(*this);
        currentExpr_ = prev;
    }

    void foldStmt(StmtPtr &stmt)
    {
        if (!stmt)
            return;
        StmtPtr *prev = currentStmt_;
        currentStmt_ = &stmt;
        stmt->accept(*this);
        currentStmt_ = prev;
    }

    ExprPtr &exprSlot()
    {
        return *currentExpr_;
    }

    void replaceWithInt(long long v, il::support::SourceLoc loc)
    {
        auto ni = std::make_unique<IntExpr>();
        ni->loc = loc;
        ni->value = v;
        exprSlot() = std::move(ni);
    }

    void replaceWithBool(bool v, il::support::SourceLoc loc)
    {
        auto nb = std::make_unique<BoolExpr>();
        nb->loc = loc;
        nb->value = v;
        exprSlot() = std::move(nb);
    }

    void replaceWithStr(std::string s, il::support::SourceLoc loc)
    {
        auto ns = std::make_unique<StringExpr>();
        ns->loc = loc;
        ns->value = std::move(s);
        exprSlot() = std::move(ns);
    }

    void replaceWithExpr(ExprPtr replacement)
    {
        exprSlot() = std::move(replacement);
    }

    // ExprVisitor overrides -------------------------------------------------
    void visit(const IntExpr &) override {}
    void visit(const FloatExpr &) override {}
    void visit(const StringExpr &) override {}
    void visit(const BoolExpr &) override {}
    void visit(const VarExpr &) override {}

    void visit(const ArrayExpr &expr) override
    {
        auto &node = const_cast<ArrayExpr &>(expr);
        foldExpr(node.index);
    }

    void visit(const UnaryExpr &expr) override
    {
        auto &node = const_cast<UnaryExpr &>(expr);
        foldExpr(node.expr);

        if (auto *b = dynamic_cast<BoolExpr *>(node.expr.get()))
        {
            if (node.op == UnaryExpr::Op::LogicalNot)
                replaceWithBool(!b->value, node.loc);
            return;
        }

        auto numeric = detail::asNumeric(*node.expr);
        if (numeric && !numeric->isFloat && node.op == UnaryExpr::Op::LogicalNot)
            replaceWithInt(numeric->i == 0 ? 1 : 0, node.loc);
    }

    void visit(const BinaryExpr &expr) override
    {
        auto &node = const_cast<BinaryExpr &>(expr);
        foldExpr(node.lhs);

        if (node.op == BinaryExpr::Op::LogicalAndShort)
        {
            if (auto *lhsBool = dynamic_cast<BoolExpr *>(node.lhs.get()))
            {
                if (!lhsBool->value)
                {
                    replaceWithBool(false, node.loc);
                    return;
                }

                ExprPtr rhs = std::move(node.rhs);
                foldExpr(rhs);
                if (auto *rhsBool = dynamic_cast<BoolExpr *>(rhs.get()))
                {
                    replaceWithBool(rhsBool->value, node.loc);
                }
                else
                {
                    replaceWithExpr(std::move(rhs));
                }
                return;
            }
        }
        else if (node.op == BinaryExpr::Op::LogicalOrShort)
        {
            if (auto *lhsBool = dynamic_cast<BoolExpr *>(node.lhs.get()))
            {
                if (lhsBool->value)
                {
                    replaceWithBool(true, node.loc);
                    return;
                }

                ExprPtr rhs = std::move(node.rhs);
                foldExpr(rhs);
                if (auto *rhsBool = dynamic_cast<BoolExpr *>(rhs.get()))
                {
                    replaceWithBool(rhsBool->value, node.loc);
                }
                else
                {
                    replaceWithExpr(std::move(rhs));
                }
                return;
            }
        }

        foldExpr(node.rhs);

        if (auto *lhsBool = dynamic_cast<BoolExpr *>(node.lhs.get()))
        {
            if (auto *rhsBool = dynamic_cast<BoolExpr *>(node.rhs.get()))
            {
                switch (node.op)
                {
                    case BinaryExpr::Op::LogicalAnd:
                    case BinaryExpr::Op::LogicalAndShort:
                        replaceWithBool(lhsBool->value && rhsBool->value, node.loc);
                        return;
                    case BinaryExpr::Op::LogicalOr:
                    case BinaryExpr::Op::LogicalOrShort:
                        replaceWithBool(lhsBool->value || rhsBool->value, node.loc);
                        return;
                    default:
                        break;
                }
            }
        }

        if (const auto *entry = detail::findBinaryFold(node.op))
        {
            if (entry->numeric)
            {
                if (auto res = entry->numeric(*node.lhs, *node.rhs))
                {
                    res->loc = node.loc;
                    replaceWithExpr(std::move(res));
                    return;
                }
            }

            if (entry->string)
            {
                if (auto *ls = dynamic_cast<StringExpr *>(node.lhs.get()))
                {
                    if (auto *rs = dynamic_cast<StringExpr *>(node.rhs.get()))
                    {
                        if (auto res = entry->string(*ls, *rs))
                        {
                            res->loc = node.loc;
                            replaceWithExpr(std::move(res));
                        }
                    }
                }
            }
        }
    }

    void visit(const BuiltinCallExpr &expr) override
    {
        auto &node = const_cast<BuiltinCallExpr &>(expr);
        for (auto &arg : node.args)
            foldExpr(arg);

        switch (node.builtin)
        {
            case BuiltinCallExpr::Builtin::Len:
            {
                std::string s;
                if (node.args.size() == 1 && node.args[0] && isStringLiteral(*node.args[0], s))
                    replaceWithInt(static_cast<long long>(s.size()), node.loc);
                break;
            }
            case BuiltinCallExpr::Builtin::Mid:
            {
                if (node.args.size() == 3)
                {
                    std::string s;
                    if (node.args[0] && isStringLiteral(*node.args[0], s))
                    {
                        auto nStart = detail::asNumeric(*node.args[1]);
                        auto nLen = detail::asNumeric(*node.args[2]);
                        if (nStart && nLen && !nStart->isFloat && !nLen->isFloat)
                        {
                            long long start = nStart->i;
                            long long len = nLen->i;
                            if (start < 1)
                                start = 1;
                            if (len < 0)
                                len = 0;
                            size_t pos = static_cast<size_t>(start - 1);
                            replaceWithStr(s.substr(pos, static_cast<size_t>(len)), node.loc);
                        }
                    }
                }
                break;
            }
            case BuiltinCallExpr::Builtin::Val:
            {
                std::string s;
                if (node.args.size() == 1 && node.args[0] && isStringLiteral(*node.args[0], s))
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
                        replaceWithInt(v, node.loc);
                }
                break;
            }
            case BuiltinCallExpr::Builtin::Int:
            {
                if (node.args.size() == 1)
                {
                    auto n = detail::asNumeric(*node.args[0]);
                    if (n && n->isFloat)
                        replaceWithInt(static_cast<long long>(n->f), node.loc);
                }
                break;
            }
            case BuiltinCallExpr::Builtin::Str:
            {
                if (node.args.size() == 1)
                {
                    auto n = detail::asNumeric(*node.args[0]);
                    if (n)
                    {
                        char buf[32];
                        if (n->isFloat)
                            snprintf(buf, sizeof(buf), "%g", n->f);
                        else
                            snprintf(buf, sizeof(buf), "%lld", n->i);
                        replaceWithStr(buf, node.loc);
                    }
                }
                break;
            }
            default:
                break;
        }
    }

    void visit(const CallExpr &) override {}

    // StmtVisitor overrides -------------------------------------------------
    void visit(const PrintStmt &stmt) override
    {
        auto &node = const_cast<PrintStmt &>(stmt);
        for (auto &item : node.items)
        {
            if (item.kind == PrintItem::Kind::Expr)
                foldExpr(item.expr);
        }
    }

    void visit(const LetStmt &stmt) override
    {
        auto &node = const_cast<LetStmt &>(stmt);
        foldExpr(node.target);
        foldExpr(node.expr);
    }

    void visit(const DimStmt &stmt) override
    {
        auto &node = const_cast<DimStmt &>(stmt);
        if (node.isArray && node.size)
            foldExpr(node.size);
    }

    void visit(const RandomizeStmt &) override {}

    void visit(const IfStmt &stmt) override
    {
        auto &node = const_cast<IfStmt &>(stmt);
        foldExpr(node.cond);
        foldStmt(node.then_branch);
        for (auto &elseif : node.elseifs)
        {
            foldExpr(elseif.cond);
            foldStmt(elseif.then_branch);
        }
        foldStmt(node.else_branch);
    }

    void visit(const WhileStmt &stmt) override
    {
        auto &node = const_cast<WhileStmt &>(stmt);
        foldExpr(node.cond);
        for (auto &bodyStmt : node.body)
            foldStmt(bodyStmt);
    }

    void visit(const ForStmt &stmt) override
    {
        auto &node = const_cast<ForStmt &>(stmt);
        foldExpr(node.start);
        foldExpr(node.end);
        if (node.step)
            foldExpr(node.step);
        for (auto &bodyStmt : node.body)
            foldStmt(bodyStmt);
    }

    void visit(const NextStmt &) override {}
    void visit(const GotoStmt &) override {}
    void visit(const EndStmt &) override {}
    void visit(const InputStmt &) override {}
    void visit(const ReturnStmt &) override {}

    void visit(const FunctionDecl &) override {}
    void visit(const SubDecl &) override {}

    void visit(const StmtList &stmt) override
    {
        auto &node = const_cast<StmtList &>(stmt);
        for (auto &child : node.stmts)
            foldStmt(child);
    }

    ExprPtr *currentExpr_ = nullptr;
    StmtPtr *currentStmt_ = nullptr;
};

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
    ConstFolderPass pass;
    pass.run(prog);
}

} // namespace il::frontends::basic
