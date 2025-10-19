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
#include <array>
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

enum class LiteralType
{
    Int,
    Float,
    Bool,
    String,
    Numeric, ///< Wildcard used for numeric promotion lookups.
};

using BinaryFolderFn = ExprPtr (*)(const Expr &, const Expr &);

struct BinaryFoldRule
{
    BinaryExpr::Op op;
    LiteralType lhs;
    LiteralType rhs;
    BinaryFolderFn folder;
};

constexpr std::array<BinaryFoldRule, 19> kBinaryFoldRules = {{
    // Numeric operations rely on ConstFold_Arith helpers which honor BASIC's
    // two's-complement overflow semantics via wrapAdd/wrapSub/wrapMul.
    {BinaryExpr::Op::Add, LiteralType::Numeric, LiteralType::Numeric, &foldNumericAdd},
    {BinaryExpr::Op::Sub, LiteralType::Numeric, LiteralType::Numeric, &foldNumericSub},
    {BinaryExpr::Op::Mul, LiteralType::Numeric, LiteralType::Numeric, &foldNumericMul},
    {BinaryExpr::Op::Div, LiteralType::Numeric, LiteralType::Numeric, &foldNumericDiv},
    {BinaryExpr::Op::IDiv, LiteralType::Numeric, LiteralType::Numeric, &foldNumericIDiv},
    {BinaryExpr::Op::Mod, LiteralType::Numeric, LiteralType::Numeric, &foldNumericMod},
    {BinaryExpr::Op::Eq, LiteralType::Numeric, LiteralType::Numeric, &foldNumericEq},
    {BinaryExpr::Op::Ne, LiteralType::Numeric, LiteralType::Numeric, &foldNumericNe},
    {BinaryExpr::Op::Lt, LiteralType::Numeric, LiteralType::Numeric, &foldNumericLt},
    {BinaryExpr::Op::Le, LiteralType::Numeric, LiteralType::Numeric, &foldNumericLe},
    {BinaryExpr::Op::Gt, LiteralType::Numeric, LiteralType::Numeric, &foldNumericGt},
    {BinaryExpr::Op::Ge, LiteralType::Numeric, LiteralType::Numeric, &foldNumericGe},
    {BinaryExpr::Op::LogicalAndShort, LiteralType::Numeric, LiteralType::Numeric, &foldNumericAnd},
    {BinaryExpr::Op::LogicalOrShort, LiteralType::Numeric, LiteralType::Numeric, &foldNumericOr},
    {BinaryExpr::Op::LogicalAnd, LiteralType::Numeric, LiteralType::Numeric, &foldNumericAnd},
    {BinaryExpr::Op::LogicalOr, LiteralType::Numeric, LiteralType::Numeric, &foldNumericOr},
    {BinaryExpr::Op::Add, LiteralType::String, LiteralType::String, &foldStringBinaryConcat},
    {BinaryExpr::Op::Eq, LiteralType::String, LiteralType::String, &foldStringBinaryEq},
    {BinaryExpr::Op::Ne, LiteralType::String, LiteralType::String, &foldStringBinaryNe},
}};

bool isNumeric(LiteralType type)
{
    return type == LiteralType::Int || type == LiteralType::Float;
}

std::optional<LiteralType> classifyLiteral(const Expr &expr)
{
    if (dynamic_cast<const IntExpr *>(&expr))
        return LiteralType::Int;
    if (dynamic_cast<const FloatExpr *>(&expr))
        return LiteralType::Float;
    if (dynamic_cast<const BoolExpr *>(&expr))
        return LiteralType::Bool;
    if (dynamic_cast<const StringExpr *>(&expr))
        return LiteralType::String;
    return std::nullopt;
}

const BinaryFoldRule *lookupRule(BinaryExpr::Op op, LiteralType lhs, LiteralType rhs)
{
    const BinaryFoldRule *numericFallback = nullptr;
    for (const auto &rule : kBinaryFoldRules)
    {
        if (rule.op != op)
            continue;
        if (rule.lhs == lhs && rule.rhs == rhs)
            return &rule;
        if (rule.lhs == LiteralType::Numeric && rule.rhs == LiteralType::Numeric)
            numericFallback = &rule;
    }

    if (numericFallback && isNumeric(lhs) && isNumeric(rhs))
        return numericFallback;

    return nullptr;
}

ExprPtr foldBinaryLiteral(BinaryExpr::Op op, const Expr &lhs, const Expr &rhs)
{
    auto lhsType = classifyLiteral(lhs);
    auto rhsType = classifyLiteral(rhs);
    if (!lhsType || !rhsType)
        return nullptr;

    if (auto *rule = lookupRule(op, *lhsType, *rhsType))
        return rule->folder(lhs, rhs);

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

    std::optional<double> getFiniteDouble(const ExprPtr &expr) const
    {
        if (!expr)
            return std::nullopt;
        auto numeric = detail::asNumeric(*expr);
        if (!numeric)
            return std::nullopt;
        double value = numeric->isFloat ? numeric->f : static_cast<double>(numeric->i);
        if (!std::isfinite(value))
            return std::nullopt;
        return value;
    }

    std::optional<int> getRoundedDigits(const ExprPtr &expr) const
    {
        auto value = getFiniteDouble(expr);
        if (!value)
            return std::nullopt;
        double rounded = std::nearbyint(*value);
        if (!std::isfinite(rounded))
            return std::nullopt;
        if (rounded < static_cast<double>(std::numeric_limits<int32_t>::min()) ||
            rounded > static_cast<double>(std::numeric_limits<int32_t>::max()))
            return std::nullopt;
        return static_cast<int>(rounded);
    }

    std::optional<double> roundToDigits(double value, int digits) const
    {
        if (!std::isfinite(value))
            return std::nullopt;

        if (digits == 0)
        {
            double rounded = std::nearbyint(value);
            if (!std::isfinite(rounded))
                return std::nullopt;
            return rounded;
        }

        double scaleExponent = static_cast<double>(std::abs(digits));
        double scale = std::pow(10.0, scaleExponent);
        if (!std::isfinite(scale) || scale == 0.0)
            return std::nullopt;

        double scaled = digits > 0 ? value * scale : value / scale;
        if (!std::isfinite(scaled))
            return std::nullopt;

        double rounded = std::nearbyint(scaled);
        if (!std::isfinite(rounded))
            return std::nullopt;

        double result = digits > 0 ? rounded / scale : rounded * scale;
        if (!std::isfinite(result))
            return std::nullopt;
        return result;
    }

    std::optional<double> parseValLiteral(const StringExpr &expr) const
    {
        const std::string &s = expr.value;
        const char *raw = s.c_str();
        while (*raw && std::isspace(static_cast<unsigned char>(*raw)))
            ++raw;

        if (*raw == '\0')
            return 0.0;

        auto isDigit = [](char ch) {
            return ch >= '0' && ch <= '9';
        };

        if (*raw == '+' || *raw == '-')
        {
            char next = raw[1];
            if (next == '.')
            {
                if (!isDigit(raw[2]))
                    return 0.0;
            }
            else if (!isDigit(next))
            {
                return 0.0;
            }
        }
        else if (*raw == '.')
        {
            if (!isDigit(raw[1]))
                return 0.0;
        }
        else if (!isDigit(*raw))
        {
            return 0.0;
        }

        char *endp = nullptr;
        double parsed = std::strtod(raw, &endp);
        if (endp == raw)
            return 0.0;
        if (!std::isfinite(parsed))
            return std::nullopt;
        return parsed;
    }

    bool tryFoldLen(BuiltinCallExpr &expr)
    {
        if (expr.args.size() != 1 || !expr.args[0])
            return false;
        if (auto folded = detail::foldLenLiteral(*expr.args[0]))
        {
            folded->loc = expr.loc;
            replaceWithExpr(std::move(folded));
            return true;
        }
        return false;
    }

    bool tryFoldMid(BuiltinCallExpr &expr)
    {
        if (expr.args.size() != 3 || !expr.args[0] || !expr.args[1] || !expr.args[2])
            return false;
        if (auto folded = detail::foldMidLiteral(*expr.args[0], *expr.args[1], *expr.args[2]))
        {
            folded->loc = expr.loc;
            replaceWithExpr(std::move(folded));
            return true;
        }
        return false;
    }

    bool tryFoldLeft(BuiltinCallExpr &expr)
    {
        if (expr.args.size() != 2 || !expr.args[0] || !expr.args[1])
            return false;
        if (auto folded = detail::foldLeftLiteral(*expr.args[0], *expr.args[1]))
        {
            folded->loc = expr.loc;
            replaceWithExpr(std::move(folded));
            return true;
        }
        return false;
    }

    bool tryFoldRight(BuiltinCallExpr &expr)
    {
        if (expr.args.size() != 2 || !expr.args[0] || !expr.args[1])
            return false;
        if (auto folded = detail::foldRightLiteral(*expr.args[0], *expr.args[1]))
        {
            folded->loc = expr.loc;
            replaceWithExpr(std::move(folded));
            return true;
        }
        return false;
    }

    bool tryFoldVal(BuiltinCallExpr &expr)
    {
        if (expr.args.size() != 1 || !expr.args[0])
            return false;
        if (auto *literal = dynamic_cast<StringExpr *>(expr.args[0].get()))
        {
            auto parsed = parseValLiteral(*literal);
            if (!parsed)
                return false;
            replaceWithFloat(*parsed, expr.loc);
            return true;
        }
        return false;
    }

    bool tryFoldInt(BuiltinCallExpr &expr)
    {
        if (expr.args.size() != 1)
            return false;
        auto value = getFiniteDouble(expr.args[0]);
        if (!value)
            return false;
        double floored = std::floor(*value);
        if (!std::isfinite(floored))
            return false;
        replaceWithFloat(floored, expr.loc);
        return true;
    }

    bool tryFoldFix(BuiltinCallExpr &expr)
    {
        if (expr.args.size() != 1)
            return false;
        auto value = getFiniteDouble(expr.args[0]);
        if (!value)
            return false;
        double truncated = std::trunc(*value);
        if (!std::isfinite(truncated))
            return false;
        replaceWithFloat(truncated, expr.loc);
        return true;
    }

    bool tryFoldRound(BuiltinCallExpr &expr)
    {
        if (expr.args.empty() || !expr.args[0])
            return false;

        auto value = getFiniteDouble(expr.args[0]);
        if (!value)
            return false;

        int digits = 0;
        if (expr.args.size() >= 2 && expr.args[1])
        {
            auto parsedDigits = getRoundedDigits(expr.args[1]);
            if (!parsedDigits)
                return false;
            digits = *parsedDigits;
        }

        auto result = roundToDigits(*value, digits);
        if (!result)
            return false;
        replaceWithFloat(*result, expr.loc);
        return true;
    }

    bool tryFoldStr(BuiltinCallExpr &expr)
    {
        if (expr.args.size() != 1)
            return false;
        auto numeric = detail::asNumeric(*expr.args[0]);
        if (!numeric)
            return false;

        char buf[64];
        if (numeric->isFloat)
        {
            rt_format_f64(numeric->f, buf, sizeof(buf));
        }
        else
        {
            snprintf(buf, sizeof(buf), "%lld", numeric->i);
        }
        replaceWithStr(buf, expr.loc);
        return true;
    }

    struct BuiltinDispatchEntry
    {
        BuiltinCallExpr::Builtin builtin;
        bool (ConstFolderPass::*folder)(BuiltinCallExpr &);
    };

    static constexpr std::array<BuiltinDispatchEntry, 9> kBuiltinDispatch{{
        {BuiltinCallExpr::Builtin::Len, &ConstFolderPass::tryFoldLen},
        {BuiltinCallExpr::Builtin::Mid, &ConstFolderPass::tryFoldMid},
        {BuiltinCallExpr::Builtin::Left, &ConstFolderPass::tryFoldLeft},
        {BuiltinCallExpr::Builtin::Right, &ConstFolderPass::tryFoldRight},
        {BuiltinCallExpr::Builtin::Val, &ConstFolderPass::tryFoldVal},
        {BuiltinCallExpr::Builtin::Int, &ConstFolderPass::tryFoldInt},
        {BuiltinCallExpr::Builtin::Fix, &ConstFolderPass::tryFoldFix},
        {BuiltinCallExpr::Builtin::Round, &ConstFolderPass::tryFoldRound},
        {BuiltinCallExpr::Builtin::Str, &ConstFolderPass::tryFoldStr},
    }};

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

        if (auto folded = detail::foldBinaryLiteral(expr.op, *expr.lhs, *expr.rhs))
        {
            folded->loc = expr.loc;
            replaceWithExpr(std::move(folded));
        }
    }

    void visit(BuiltinCallExpr &expr) override
    {
        for (auto &arg : expr.args)
            foldExpr(arg);

        for (const auto &entry : kBuiltinDispatch)
        {
            if (entry.builtin == expr.builtin)
            {
                if ((this->*entry.folder)(expr))
                    return;
                break;
            }
        }
    }

    void visit(CallExpr &) override {}

    void visit(NewExpr &expr) override
    {
        for (auto &arg : expr.args)
            foldExpr(arg);
    }

    void visit(MeExpr &) override {}

    void visit(MemberAccessExpr &expr) override
    {
        foldExpr(expr.base);
    }

    void visit(MethodCallExpr &expr) override
    {
        foldExpr(expr.base);
        for (auto &arg : expr.args)
            foldExpr(arg);
    }

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
    void visit(SeekStmt &stmt) override
    {
        if (stmt.channelExpr)
            foldExpr(stmt.channelExpr);
        if (stmt.positionExpr)
            foldExpr(stmt.positionExpr);
    }
    void visit(OnErrorGoto &) override {}
    void visit(Resume &) override {}
    void visit(EndStmt &) override {}
    void visit(InputStmt &stmt) override
    {
        if (stmt.prompt)
            foldExpr(stmt.prompt);
    }

    void visit(InputChStmt &) override {}

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

    void visit(DeleteStmt &stmt) override
    {
        foldExpr(stmt.target);
    }

    void visit(ConstructorDecl &stmt) override
    {
        for (auto &bodyStmt : stmt.body)
            foldStmt(bodyStmt);
    }

    void visit(DestructorDecl &stmt) override
    {
        for (auto &bodyStmt : stmt.body)
            foldStmt(bodyStmt);
    }

    void visit(MethodDecl &stmt) override
    {
        for (auto &bodyStmt : stmt.body)
            foldStmt(bodyStmt);
    }

    void visit(ClassDecl &stmt) override
    {
        for (auto &member : stmt.members)
            foldStmt(member);
    }

    void visit(TypeDecl &) override {}

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
