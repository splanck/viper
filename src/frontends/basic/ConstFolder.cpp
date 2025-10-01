// File: src/frontends/basic/ConstFolder.cpp
// Purpose: Implements constant folding for BASIC AST nodes with table-driven
// dispatch.
// Key invariants: Folding preserves 64-bit wrap-around semantics.
// Ownership/Lifetime: AST nodes are mutated in place.
// Links: docs/codemap.md
// License: MIT (see LICENSE).

#include "frontends/basic/ConstFolder.hpp"
#include "frontends/basic/ConstFoldHelpers.hpp"
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
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

class ConstFolderPass : public MutExprVisitor, public MutStmtVisitor
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

    void replaceWithFloat(double v, il::support::SourceLoc loc)
    {
        auto nf = std::make_unique<FloatExpr>();
        nf->loc = loc;
        nf->value = v;
        exprSlot() = std::move(nf);
    }

    void replaceWithExpr(ExprPtr replacement)
    {
        exprSlot() = std::move(replacement);
    }

    // MutExprVisitor overrides ----------------------------------------------
    void visit(IntExpr &) override {}
    void visit(FloatExpr &) override {}
    void visit(StringExpr &) override {}
    void visit(BoolExpr &) override {}
    void visit(VarExpr &) override {}

    void visit(ArrayExpr &expr) override
    {
        foldExpr(expr.index);
    }

    void visit(LBoundExpr &) override {}

    void visit(UBoundExpr &) override {}

    void visit(UnaryExpr &expr) override
    {
        foldExpr(expr.expr);

        if (auto *b = dynamic_cast<BoolExpr *>(expr.expr.get()))
        {
            if (expr.op == UnaryExpr::Op::LogicalNot)
                replaceWithBool(!b->value, expr.loc);
            return;
        }

        auto numeric = detail::asNumeric(*expr.expr);
        if (numeric && !numeric->isFloat && expr.op == UnaryExpr::Op::LogicalNot)
            replaceWithInt(numeric->i == 0 ? 1 : 0, expr.loc);
    }

    void visit(BinaryExpr &expr) override
    {
        foldExpr(expr.lhs);

        if (expr.op == BinaryExpr::Op::LogicalAndShort)
        {
            if (auto *lhsBool = dynamic_cast<BoolExpr *>(expr.lhs.get()))
            {
                if (!lhsBool->value)
                {
                    replaceWithBool(false, expr.loc);
                    return;
                }

                ExprPtr rhs = std::move(expr.rhs);
                foldExpr(rhs);
                if (auto *rhsBool = dynamic_cast<BoolExpr *>(rhs.get()))
                {
                    replaceWithBool(rhsBool->value, expr.loc);
                }
                else
                {
                    replaceWithExpr(std::move(rhs));
                }
                return;
            }
        }
        else if (expr.op == BinaryExpr::Op::LogicalOrShort)
        {
            if (auto *lhsBool = dynamic_cast<BoolExpr *>(expr.lhs.get()))
            {
                if (lhsBool->value)
                {
                    replaceWithBool(true, expr.loc);
                    return;
                }

                ExprPtr rhs = std::move(expr.rhs);
                foldExpr(rhs);
                if (auto *rhsBool = dynamic_cast<BoolExpr *>(rhs.get()))
                {
                    replaceWithBool(rhsBool->value, expr.loc);
                }
                else
                {
                    replaceWithExpr(std::move(rhs));
                }
                return;
            }
        }

        foldExpr(expr.rhs);

        if (auto *lhsBool = dynamic_cast<BoolExpr *>(expr.lhs.get()))
        {
            if (auto *rhsBool = dynamic_cast<BoolExpr *>(expr.rhs.get()))
            {
                switch (expr.op)
                {
                    case BinaryExpr::Op::LogicalAnd:
                    case BinaryExpr::Op::LogicalAndShort:
                        replaceWithBool(lhsBool->value && rhsBool->value, expr.loc);
                        return;
                    case BinaryExpr::Op::LogicalOr:
                    case BinaryExpr::Op::LogicalOrShort:
                        replaceWithBool(lhsBool->value || rhsBool->value, expr.loc);
                        return;
                    default:
                        break;
                }
            }
        }

        if (const auto *entry = detail::findBinaryFold(expr.op))
        {
            if (entry->numeric)
            {
                if (auto res = entry->numeric(*expr.lhs, *expr.rhs))
                {
                    res->loc = expr.loc;
                    replaceWithExpr(std::move(res));
                    return;
                }
            }

            if (entry->string)
            {
                if (auto *ls = dynamic_cast<StringExpr *>(expr.lhs.get()))
                {
                    if (auto *rs = dynamic_cast<StringExpr *>(expr.rhs.get()))
                    {
                        if (auto res = entry->string(*ls, *rs))
                        {
                            res->loc = expr.loc;
                            replaceWithExpr(std::move(res));
                        }
                    }
                }
            }
        }
    }

    void visit(BuiltinCallExpr &expr) override
    {
        for (auto &arg : expr.args)
            foldExpr(arg);

        switch (expr.builtin)
        {
            case BuiltinCallExpr::Builtin::Len:
            {
                std::string s;
                if (expr.args.size() == 1 && expr.args[0] && isStringLiteral(*expr.args[0], s))
                    replaceWithInt(static_cast<long long>(s.size()), expr.loc);
                break;
            }
            case BuiltinCallExpr::Builtin::Mid:
            {
                if (expr.args.size() == 3)
                {
                    std::string s;
                    if (expr.args[0] && isStringLiteral(*expr.args[0], s))
                    {
                        auto nStart = detail::asNumeric(*expr.args[1]);
                        auto nLen = detail::asNumeric(*expr.args[2]);
                        if (nStart && nLen && !nStart->isFloat && !nLen->isFloat)
                        {
                            long long start = nStart->i;
                            long long len = nLen->i;
                            if (start < 1)
                                start = 1;
                            if (len < 0)
                                len = 0;
                            size_t pos = static_cast<size_t>(start - 1);
                            replaceWithStr(s.substr(pos, static_cast<size_t>(len)), expr.loc);
                        }
                    }
                }
                break;
            }
            case BuiltinCallExpr::Builtin::Val:
            {
                std::string s;
                if (expr.args.size() == 1 && expr.args[0] && isStringLiteral(*expr.args[0], s))
                {
                    const char *raw = s.c_str();
                    while (*raw && std::isspace(static_cast<unsigned char>(*raw)))
                        ++raw;

                    if (*raw == '\0')
                    {
                        replaceWithFloat(0.0, expr.loc);
                        break;
                    }

                    auto isDigit = [](char ch) {
                        return ch >= '0' && ch <= '9';
                    };

                    if (*raw == '+' || *raw == '-')
                    {
                        char next = raw[1];
                        if (next == '.')
                        {
                            if (!isDigit(raw[2]))
                            {
                                replaceWithFloat(0.0, expr.loc);
                                break;
                            }
                        }
                        else if (!isDigit(next))
                        {
                            replaceWithFloat(0.0, expr.loc);
                            break;
                        }
                    }
                    else if (*raw == '.')
                    {
                        if (!isDigit(raw[1]))
                        {
                            replaceWithFloat(0.0, expr.loc);
                            break;
                        }
                    }
                    else if (!isDigit(*raw))
                    {
                        replaceWithFloat(0.0, expr.loc);
                        break;
                    }

                    char *endp = nullptr;
                    double parsed = std::strtod(raw, &endp);
                    if (endp == raw)
                    {
                        replaceWithFloat(0.0, expr.loc);
                        break;
                    }
                    if (!std::isfinite(parsed))
                        break;
                    replaceWithFloat(parsed, expr.loc);
                }
                break;
            }
            case BuiltinCallExpr::Builtin::Int:
            {
                if (expr.args.size() == 1)
                {
                    auto n = detail::asNumeric(*expr.args[0]);
                    if (n)
                    {
                        double operand = n->isFloat ? n->f : static_cast<double>(n->i);
                        if (!std::isfinite(operand))
                            break;
                        double floored = std::floor(operand);
                        if (!std::isfinite(floored))
                            break;
                        replaceWithFloat(floored, expr.loc);
                    }
                }
                break;
            }
            case BuiltinCallExpr::Builtin::Fix:
            {
                if (expr.args.size() == 1)
                {
                    auto n = detail::asNumeric(*expr.args[0]);
                    if (n)
                    {
                        double operand = n->isFloat ? n->f : static_cast<double>(n->i);
                        if (!std::isfinite(operand))
                            break;
                        double truncated = std::trunc(operand);
                        if (!std::isfinite(truncated))
                            break;
                        replaceWithFloat(truncated, expr.loc);
                    }
                }
                break;
            }
            case BuiltinCallExpr::Builtin::Round:
            {
                if (!expr.args.empty())
                {
                    auto first = detail::asNumeric(*expr.args[0]);
                    if (!first)
                        break;
                    double value = first->isFloat ? first->f : static_cast<double>(first->i);
                    if (!std::isfinite(value))
                        break;
                    int digits = 0;
                    if (expr.args.size() >= 2 && expr.args[1])
                    {
                        if (auto second = detail::asNumeric(*expr.args[1]))
                        {
                            double raw = second->isFloat ? second->f : static_cast<double>(second->i);
                            double rounded = std::nearbyint(raw);
                            if (!std::isfinite(rounded))
                                break;
                            if (rounded < static_cast<double>(std::numeric_limits<int32_t>::min()) ||
                                rounded > static_cast<double>(std::numeric_limits<int32_t>::max()))
                                break;
                            digits = static_cast<int>(rounded);
                        }
                        else
                        {
                            break;
                        }
                    }

                    double result = value;
                    if (digits > 0)
                    {
                        double scale = std::pow(10.0, static_cast<double>(digits));
                        if (!std::isfinite(scale) || scale == 0.0)
                            break;
                        double scaled = value * scale;
                        if (!std::isfinite(scaled))
                            break;
                        double rounded = std::nearbyint(scaled);
                        if (!std::isfinite(rounded))
                            break;
                        result = rounded / scale;
                    }
                    else if (digits < 0)
                    {
                        double scale = std::pow(10.0, static_cast<double>(-digits));
                        if (!std::isfinite(scale) || scale == 0.0)
                            break;
                        double scaled = value / scale;
                        if (!std::isfinite(scaled))
                            break;
                        double rounded = std::nearbyint(scaled);
                        if (!std::isfinite(rounded))
                            break;
                        result = rounded * scale;
                    }
                    else
                    {
                        result = std::nearbyint(value);
                    }

                    if (!std::isfinite(result))
                        break;
                    replaceWithFloat(result, expr.loc);
                }
                break;
            }
            case BuiltinCallExpr::Builtin::Str:
            {
                if (expr.args.size() == 1)
                {
                    auto n = detail::asNumeric(*expr.args[0]);
                    if (n)
                    {
                        char buf[32];
                        if (n->isFloat)
                            snprintf(buf, sizeof(buf), "%g", n->f);
                        else
                            snprintf(buf, sizeof(buf), "%lld", n->i);
                        replaceWithStr(buf, expr.loc);
                    }
                }
                break;
            }
            default:
                break;
        }
    }

    void visit(CallExpr &) override {}

    // MutStmtVisitor overrides ----------------------------------------------
    void visit(PrintStmt &stmt) override
    {
        for (auto &item : stmt.items)
        {
            if (item.kind == PrintItem::Kind::Expr)
                foldExpr(item.expr);
        }
    }

    void visit(PrintChStmt &stmt) override
    {
        foldExpr(stmt.channelExpr);
        for (auto &arg : stmt.args)
            foldExpr(arg);
    }

    void visit(LetStmt &stmt) override
    {
        foldExpr(stmt.target);
        foldExpr(stmt.expr);
    }

    void visit(DimStmt &stmt) override
    {
        if (stmt.isArray && stmt.size)
            foldExpr(stmt.size);
    }

    void visit(ReDimStmt &stmt) override
    {
        if (stmt.size)
            foldExpr(stmt.size);
    }

    void visit(RandomizeStmt &) override {}

    void visit(IfStmt &stmt) override
    {
        foldExpr(stmt.cond);
        foldStmt(stmt.then_branch);
        for (auto &elseif : stmt.elseifs)
        {
            foldExpr(elseif.cond);
            foldStmt(elseif.then_branch);
        }
        foldStmt(stmt.else_branch);
    }

    void visit(WhileStmt &stmt) override
    {
        foldExpr(stmt.cond);
        for (auto &bodyStmt : stmt.body)
            foldStmt(bodyStmt);
    }

    void visit(DoStmt &stmt) override
    {
        if (stmt.cond)
            foldExpr(stmt.cond);
        for (auto &bodyStmt : stmt.body)
            foldStmt(bodyStmt);
    }

    void visit(ForStmt &stmt) override
    {
        foldExpr(stmt.start);
        foldExpr(stmt.end);
        if (stmt.step)
            foldExpr(stmt.step);
        for (auto &bodyStmt : stmt.body)
            foldStmt(bodyStmt);
    }

    void visit(NextStmt &) override {}
    void visit(ExitStmt &) override {}
    void visit(GotoStmt &) override {}
    void visit(OpenStmt &stmt) override
    {
        if (stmt.pathExpr)
            foldExpr(stmt.pathExpr);
        if (stmt.channelExpr)
            foldExpr(stmt.channelExpr);
    }
    void visit(CloseStmt &stmt) override
    {
        if (stmt.channelExpr)
            foldExpr(stmt.channelExpr);
    }
    void visit(OnErrorGoto &) override {}
    void visit(Resume &) override {}
    void visit(EndStmt &) override {}
    void visit(InputStmt &stmt) override
    {
        if (stmt.prompt)
            foldExpr(stmt.prompt);
    }

    void visit(LineInputChStmt &stmt) override
    {
        foldExpr(stmt.channelExpr);
        foldExpr(stmt.targetVar);
    }
    void visit(ReturnStmt &) override {}

    void visit(FunctionDecl &) override {}
    void visit(SubDecl &) override {}

    void visit(StmtList &stmt) override
    {
        for (auto &child : stmt.stmts)
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
    return foldNumericBinary(
        l,
        r,
        [](const Numeric &a, const Numeric &b) -> std::optional<Numeric>
        {
            Numeric lhs = promote(a, b);
            Numeric rhs = promote(b, a);
            if (!lhs.isFloat && !rhs.isFloat)
            {
                const auto minI16 = std::numeric_limits<int16_t>::min();
                const auto maxI16 = std::numeric_limits<int16_t>::max();
                const bool lhsFitsI16 = lhs.i >= minI16 && lhs.i <= maxI16;
                const bool rhsFitsI16 = rhs.i >= minI16 && rhs.i <= maxI16;
                long long sum = wrapAdd(lhs.i, rhs.i);
                if (lhsFitsI16 && rhsFitsI16 && (sum < minI16 || sum > maxI16))
                    return std::nullopt;
                return Numeric{false, static_cast<double>(sum), sum};
            }
            double lv = lhs.isFloat ? lhs.f : static_cast<double>(lhs.i);
            double rv = rhs.isFloat ? rhs.f : static_cast<double>(rhs.i);
            double result = lv + rv;
            return Numeric{true, result, static_cast<long long>(result)};
        });
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
