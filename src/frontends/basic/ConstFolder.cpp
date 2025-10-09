// File: src/frontends/basic/ConstFolder.cpp
// Purpose: Implements constant folding for BASIC AST nodes with table-driven
// dispatch.
// Key invariants: Folding preserves 64-bit wrap-around semantics.
// Ownership/Lifetime: AST nodes are mutated in place.
// Links: docs/codemap.md
// License: MIT (see LICENSE).

#include "frontends/basic/ConstFolder.hpp"
#include "frontends/basic/ConstFoldHelpers.hpp"
#include "frontends/basic/ConstFold_Arith.hpp"
#include "frontends/basic/ConstFold_Logic.hpp"
#include "frontends/basic/ConstFold_String.hpp"

extern "C" {
#include "runtime/rt_format.h"
}
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
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

namespace
{

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
        switch (expr.op)
        {
            case UnaryExpr::Op::LogicalNot:
                if (auto replacement = detail::foldLogicalNot(*expr.expr))
                {
                    replacement->loc = expr.loc;
                    replaceWithExpr(std::move(replacement));
                }
                break;
            case UnaryExpr::Op::Plus:
            case UnaryExpr::Op::Negate:
                if (auto replacement = detail::foldUnaryArith(expr.op, *expr.expr))
                {
                    replacement->loc = expr.loc;
                    replaceWithExpr(std::move(replacement));
                }
                break;
        }
    }

    void visit(BinaryExpr &expr) override
    {
        foldExpr(expr.lhs);

        if (auto *lhsBool = dynamic_cast<BoolExpr *>(expr.lhs.get()))
        {
            if (auto shortCircuit = detail::tryShortCircuit(expr.op, *lhsBool))
            {
                replaceWithBool(*shortCircuit, expr.loc);
                return;
            }

            if (detail::isShortCircuitOp(expr.op))
            {
                ExprPtr rhs = std::move(expr.rhs);
                foldExpr(rhs);
                if (auto folded = detail::foldLogicalBinary(*lhsBool, expr.op, *rhs))
                {
                    folded->loc = expr.loc;
                    replaceWithExpr(std::move(folded));
                }
                else
                {
                    replaceWithExpr(std::move(rhs));
                }
                return;
            }
        }

        foldExpr(expr.rhs);

        if (auto folded = detail::foldLogicalBinary(*expr.lhs, expr.op, *expr.rhs))
        {
            folded->loc = expr.loc;
            replaceWithExpr(std::move(folded));
            return;
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
                if (expr.args.size() == 1 && expr.args[0])
                {
                    if (auto folded = detail::foldLenLiteral(*expr.args[0]))
                    {
                        folded->loc = expr.loc;
                        replaceWithExpr(std::move(folded));
                    }
                }
                break;
            }
            case BuiltinCallExpr::Builtin::Mid:
            {
                if (expr.args.size() == 3 && expr.args[0] && expr.args[1] && expr.args[2])
                {
                    if (auto folded = detail::foldMidLiteral(
                            *expr.args[0], *expr.args[1], *expr.args[2]))
                    {
                        folded->loc = expr.loc;
                        replaceWithExpr(std::move(folded));
                    }
                }
                break;
            }
            case BuiltinCallExpr::Builtin::Left:
            {
                if (expr.args.size() == 2 && expr.args[0] && expr.args[1])
                {
                    if (auto folded = detail::foldLeftLiteral(*expr.args[0], *expr.args[1]))
                    {
                        folded->loc = expr.loc;
                        replaceWithExpr(std::move(folded));
                    }
                }
                break;
            }
            case BuiltinCallExpr::Builtin::Right:
            {
                if (expr.args.size() == 2 && expr.args[0] && expr.args[1])
                {
                    if (auto folded = detail::foldRightLiteral(*expr.args[0], *expr.args[1]))
                    {
                        folded->loc = expr.loc;
                        replaceWithExpr(std::move(folded));
                    }
                }
                break;
            }
            case BuiltinCallExpr::Builtin::Val:
            {
                if (expr.args.size() == 1 && expr.args[0])
                {
                    if (auto *literal = dynamic_cast<StringExpr *>(expr.args[0].get()))
                    {
                        const std::string &s = literal->value;
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
                        char buf[64];
                        if (n->isFloat)
                        {
                            rt_format_f64(n->f, buf, sizeof(buf));
                        }
                        else
                        {
                            snprintf(buf, sizeof(buf), "%lld", n->i);
                        }
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
    void visit(LabelStmt &) override {}
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

    void visit(CallStmt &stmt) override
    {
        if (!stmt.call)
            return;
        for (auto &arg : stmt.call->args)
            foldExpr(arg);
    }

    void visit(ClsStmt &) override {}

    void visit(ColorStmt &stmt) override
    {
        foldExpr(stmt.fg);
        foldExpr(stmt.bg);
    }

    void visit(LocateStmt &stmt) override
    {
        foldExpr(stmt.row);
        foldExpr(stmt.col);
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

    void visit(SelectCaseStmt &stmt) override
    {
        foldExpr(stmt.selector);
        for (auto &arm : stmt.arms)
            for (auto &bodyStmt : arm.body)
                foldStmt(bodyStmt);
        for (auto &bodyStmt : stmt.elseBody)
            foldStmt(bodyStmt);
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
    void visit(GosubStmt &) override {}
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

void foldConstants(Program &prog)
{
    ConstFolderPass pass;
    pass.run(prog);
}

} // namespace il::frontends::basic
