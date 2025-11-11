//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the table-driven constant folder used by the BASIC frontend.  The
// folder interprets literal expressions, applies numeric promotion rules, and
// materialises folded AST nodes while preserving the language's 64-bit wrapping
// semantics and string concatenation behaviour.
//
//===----------------------------------------------------------------------===//
//
/// @file
/// @brief Provides constant folding utilities for BASIC AST nodes.
/// @details The helpers in this translation unit evaluate expression trees built
///          from literal operands.  They promote operands according to BASIC's
///          suffix rules, consult rule tables for arithmetic and comparison
///          folding, and replace AST nodes with canonical literals when
///          evaluation succeeds.  Out-of-line definitions keep the header light
///          while exposing rich documentation for each folding primitive.

#include "frontends/basic/ConstFolder.hpp"
#include "frontends/basic/ASTUtils.hpp"
#include "frontends/basic/constfold/Dispatch.hpp"
#include "frontends/basic/ASTUtils.hpp"

#include "viper/il/io/FormatUtils.hpp"
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <optional>
#include <string>
#include <utility>

namespace il::frontends::basic
{
namespace cf = il::frontends::basic::constfold;

namespace
{

/// @brief AST visitor that performs in-place constant folding for BASIC.
/// @details Traverses expressions and statements, eagerly rewriting literal
///          subtrees into canonical nodes.  The pass threads context via member
///          pointers so nested visits can replace the current expression or
///          statement without returning large structures.
class ConstFolderPass : public MutExprVisitor, public MutStmtVisitor
{
  public:
    /// @brief Fold all procedures and top-level statements in a program.
    /// @param prog Program whose AST will be mutated in place.
    void run(Program &prog)
    {
        for (auto &decl : prog.procs)
            foldStmt(decl);
        for (auto &stmt : prog.main)
            foldStmt(stmt);
    }

  private:
    /// @brief Recursively fold an expression tree and update the current slot.
    /// @param expr Expression pointer reference that may be replaced with a literal.
    void foldExpr(ExprPtr &expr)
    {
        if (!expr)
            return;
        ExprPtr *prev = currentExpr_;
        currentExpr_ = &expr;
        expr->accept(*this);
        currentExpr_ = prev;
    }

    /// @brief Recursively fold a statement subtree.
    /// @param stmt Statement pointer reference that may be rewritten.
    void foldStmt(StmtPtr &stmt)
    {
        if (!stmt)
            return;
        StmtPtr *prev = currentStmt_;
        currentStmt_ = &stmt;
        stmt->accept(*this);
        currentStmt_ = prev;
    }

    /// @brief Access the expression slot currently being rewritten.
    /// @return Reference to the pointer tracked by the visitor.
    ExprPtr &exprSlot()
    {
        return *currentExpr_;
    }

    /// @brief Replace the active expression with an integer literal node.
    /// @param v Integer value assigned to the replacement literal.
    /// @param loc Source location propagated to the new node.
    void replaceWithInt(long long v, il::support::SourceLoc loc)
    {
        auto ni = std::make_unique<IntExpr>();
        ni->loc = loc;
        ni->value = v;
        exprSlot() = std::move(ni);
    }

    /// @brief Replace the active expression with a boolean literal node.
    /// @param v Boolean value assigned to the replacement literal.
    /// @param loc Source location propagated to the new node.
    void replaceWithBool(bool v, il::support::SourceLoc loc)
    {
        auto nb = std::make_unique<BoolExpr>();
        nb->loc = loc;
        nb->value = v;
        exprSlot() = std::move(nb);
    }

    /// @brief Replace the active expression with a string literal node.
    /// @param s String payload moved into the replacement literal.
    /// @param loc Source location propagated to the new node.
    void replaceWithStr(std::string s, il::support::SourceLoc loc)
    {
        auto ns = std::make_unique<StringExpr>();
        ns->loc = loc;
        ns->value = std::move(s);
        exprSlot() = std::move(ns);
    }

    /// @brief Replace the active expression with a floating-point literal node.
    /// @param v Floating-point value assigned to the replacement literal.
    /// @param loc Source location propagated to the new node.
    void replaceWithFloat(double v, il::support::SourceLoc loc)
    {
        auto nf = std::make_unique<FloatExpr>();
        nf->loc = loc;
        nf->value = v;
        exprSlot() = std::move(nf);
    }

    /// @brief Replace the active expression with an arbitrary expression node.
    /// @param replacement Newly constructed expression that becomes current.
    void replaceWithExpr(ExprPtr replacement)
    {
        exprSlot() = std::move(replacement);
    }

    /// @brief Extract a finite numeric value from an expression if possible.
    /// @param expr Expression pointer to inspect.
    /// @return Finite double value or empty optional when non-numeric.
    std::optional<double> getFiniteDouble(const ExprPtr &expr) const
    {
        if (!expr)
            return std::nullopt;
        auto numeric = cf::numeric_from_expr(*expr);
        if (!numeric)
            return std::nullopt;
        double value = numeric->isFloat ? numeric->f : static_cast<double>(numeric->i);
        if (!std::isfinite(value))
            return std::nullopt;
        return value;
    }

    /// @brief Interpret an expression as an integer number of digits.
    /// @param expr Expression to inspect.
    /// @return Rounded digit count within 32-bit range or empty optional when invalid.
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

    /// @brief Round a floating value to the specified decimal digits.
    /// @param value Floating-point value to round.
    /// @param digits Positive for fractional digits, negative for integral multiples.
    /// @return Rounded result or empty optional when intermediate computations overflow.
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

    /// @brief Parse a string literal using BASIC's VAL semantics.
    /// @param expr String literal expression to parse.
    /// @return Parsed double or empty optional when the string is not a valid number.
    std::optional<double> parseValLiteral(const StringExpr &expr) const
    {
        const std::string &s = expr.value;
        const char *raw = s.c_str();
        while (*raw && std::isspace(static_cast<unsigned char>(*raw)))
            ++raw;

        if (*raw == '\0')
            return 0.0;

        auto isDigit = [](char ch) { return ch >= '0' && ch <= '9'; };

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

    /// @brief Fold LEN builtin calls when the argument is a literal.
    /// @param expr Builtin call expression being visited.
    /// @return @c true when the expression was replaced with a constant.
    bool tryFoldLen(BuiltinCallExpr &expr)
    {
        if (expr.args.size() != 1 || !expr.args[0])
            return false;
        if (auto folded = cf::foldLenLiteral(*expr.args[0]))
        {
            folded->loc = expr.loc;
            replaceWithExpr(std::move(folded));
            return true;
        }
        return false;
    }

    /// @brief Fold MID builtin calls when all operands are literal.
    /// @param expr Builtin call expression being visited.
    /// @return @c true when the expression was replaced with a constant.
    bool tryFoldMid(BuiltinCallExpr &expr)
    {
        if (expr.args.size() != 3 || !expr.args[0] || !expr.args[1] || !expr.args[2])
            return false;
        if (auto folded = cf::foldMidLiteral(*expr.args[0], *expr.args[1], *expr.args[2]))
        {
            folded->loc = expr.loc;
            replaceWithExpr(std::move(folded));
            return true;
        }
        return false;
    }

    /// @brief Fold LEFT builtin calls when both operands are literal.
    /// @param expr Builtin call expression being visited.
    /// @return @c true when the expression was replaced with a constant.
    bool tryFoldLeft(BuiltinCallExpr &expr)
    {
        if (expr.args.size() != 2 || !expr.args[0] || !expr.args[1])
            return false;
        if (auto folded = cf::foldLeftLiteral(*expr.args[0], *expr.args[1]))
        {
            folded->loc = expr.loc;
            replaceWithExpr(std::move(folded));
            return true;
        }
        return false;
    }

    /// @brief Fold RIGHT builtin calls when both operands are literal.
    /// @param expr Builtin call expression being visited.
    /// @return @c true when the expression was replaced with a constant.
    bool tryFoldRight(BuiltinCallExpr &expr)
    {
        if (expr.args.size() != 2 || !expr.args[0] || !expr.args[1])
            return false;
        if (auto folded = cf::foldRightLiteral(*expr.args[0], *expr.args[1]))
        {
            folded->loc = expr.loc;
            replaceWithExpr(std::move(folded));
            return true;
        }
        return false;
    }

    /// @brief Fold VAL builtin calls when the argument is a literal string.
    /// @param expr Builtin call expression being visited.
    /// @return @c true when the expression was replaced with a numeric constant.
    bool tryFoldVal(BuiltinCallExpr &expr)
    {
        if (expr.args.size() != 1 || !expr.args[0])
            return false;
        if (auto *literal = as<StringExpr>(*expr.args[0]))
        {
            auto parsed = parseValLiteral(*literal);
            if (!parsed)
                return false;
            replaceWithFloat(*parsed, expr.loc);
            return true;
        }
        return false;
    }

    /// @brief Fold INT builtin calls for literal numeric arguments.
    /// @param expr Builtin call expression being visited.
    /// @return @c true when the expression was replaced with a constant.
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

    /// @brief Fold FIX builtin calls for literal numeric arguments.
    /// @param expr Builtin call expression being visited.
    /// @return @c true when the expression was replaced with a constant.
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

    /// @brief Fold ROUND builtin calls when arguments are literal.
    /// @param expr Builtin call expression being visited.
    /// @return @c true when the expression was replaced with a rounded constant.
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

    /// @brief Fold STR builtin calls when the argument is literal numeric.
    /// @param expr Builtin call expression being visited.
    /// @return @c true when the expression was replaced with a string literal.
    bool tryFoldStr(BuiltinCallExpr &expr)
    {
        if (expr.args.size() != 1)
            return false;
        auto numeric = cf::numeric_from_expr(*expr.args[0]);
        if (!numeric)
            return false;

        std::string formatted = numeric->isFloat ? viper::il::io::format_float(numeric->f)
                                                 : viper::il::io::format_integer(numeric->i);
        replaceWithStr(std::move(formatted), expr.loc);
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
    /// @brief Literals are already canonical, so integer nodes are left untouched.
    void visit(IntExpr &) override {}

    /// @brief Floating literals require no rewriting beyond their existing value.
    void visit(FloatExpr &) override {}

    /// @brief String literals are already canonical and therefore skipped.
    void visit(StringExpr &) override {}

    /// @brief Boolean literals are already canonical and therefore skipped.
    void visit(BoolExpr &) override {}

    /// @brief Variable references cannot be folded directly.
    void visit(VarExpr &) override {}

    /// @brief Fold array index expressions before evaluating bounds.
    void visit(ArrayExpr &expr) override
    {
        foldExpr(expr.index);
    }

    /// @brief Lower bound queries are left untouched because they resolve at runtime.
    void visit(LBoundExpr &) override {}

    /// @brief Upper bound queries are left untouched because they resolve at runtime.
    void visit(UBoundExpr &) override {}

    /// @brief Fold unary operations when the operand collapses to a literal.
    void visit(UnaryExpr &expr) override
    {
        foldExpr(expr.expr);
        switch (expr.op)
        {
            case UnaryExpr::Op::LogicalNot:
                if (auto replacement = cf::fold_logical_not(*expr.expr))
                {
                    replacement->loc = expr.loc;
                    replaceWithExpr(std::move(replacement));
                }
                break;
            case UnaryExpr::Op::Plus:
            case UnaryExpr::Op::Negate:
                if (auto replacement = cf::fold_unary_arith(expr.op, *expr.expr))
                {
                    replacement->loc = expr.loc;
                    replaceWithExpr(std::move(replacement));
                }
                break;
        }
    }

    /// @brief Fold binary operations by evaluating literal operands and applying shortcuts.
    void visit(BinaryExpr &expr) override
    {
        foldExpr(expr.lhs);

        if (auto *lhsBool = as<BoolExpr>(*expr.lhs))
        {
            if (auto shortCircuit = cf::try_short_circuit(expr.op, *lhsBool))
            {
                replaceWithBool(*shortCircuit, expr.loc);
                return;
            }

            if (cf::is_short_circuit(expr.op))
            {
                ExprPtr rhs = std::move(expr.rhs);
                foldExpr(rhs);
                if (auto folded = cf::fold_boolean_binary(*lhsBool, expr.op, *rhs))
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

        if (auto folded = cf::fold_boolean_binary(*expr.lhs, expr.op, *expr.rhs))
        {
            folded->loc = expr.loc;
            replaceWithExpr(std::move(folded));
            return;
        }

        if (auto folded = cf::fold_expr(expr))
        {
            (*folded)->loc = expr.loc;
            replaceWithExpr(std::move(*folded));
        }
    }

    /// @brief Fold builtin function calls whose arguments are literal values.
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

    /// @brief User-defined calls are not folded because they may have side effects.
    void visit(CallExpr &) override {}

    /// @brief Fold constructor arguments to expose more literal values downstream.
    void visit(NewExpr &expr) override
    {
        for (auto &arg : expr.args)
            foldExpr(arg);
    }

    /// @brief The ME pseudo-variable cannot be folded.
    void visit(MeExpr &) override {}

    /// @brief Fold the receiver of member accesses before further lowering.
    void visit(MemberAccessExpr &expr) override
    {
        foldExpr(expr.base);
    }

    /// @brief Fold the receiver and arguments of method invocations.
    void visit(MethodCallExpr &expr) override
    {
        foldExpr(expr.base);
        for (auto &arg : expr.args)
            foldExpr(arg);
    }

    /// @brief Fold inside IS expression (value only; type is metadata).
    void visit(IsExpr &expr) override
    {
        foldExpr(expr.value);
    }

    /// @brief Fold inside AS expression (value only; type is metadata).
    void visit(AsExpr &expr) override
    {
        foldExpr(expr.value);
    }

    // MutStmtVisitor overrides ----------------------------------------------
    /// @brief Labels carry no expressions to fold.
    void visit(LabelStmt &) override {}

    /// @brief Fold expressions embedded in PRINT statement items.
    void visit(PrintStmt &stmt) override
    {
        for (auto &item : stmt.items)
        {
            if (item.kind == PrintItem::Kind::Expr)
                foldExpr(item.expr);
        }
    }

    /// @brief Fold channel and argument expressions for PRINT # statements.
    void visit(PrintChStmt &stmt) override
    {
        foldExpr(stmt.channelExpr);
        for (auto &arg : stmt.args)
            foldExpr(arg);
    }

    /// @brief BEEP has no foldable expressions.
    void visit(BeepStmt &) override {}

    /// @brief Fold arguments within CALL statements while leaving target intact.
    void visit(CallStmt &stmt) override
    {
        if (!stmt.call)
            return;

        if (auto *ce = as<CallExpr>(*stmt.call))
        {
            for (auto &arg : ce->args)
                foldExpr(arg);
            return;
        }

        if (auto *me = as<MethodCallExpr>(*stmt.call))
        {
            if (me->base)
                foldExpr(me->base);
            for (auto &arg : me->args)
                foldExpr(arg);
            return;
        }
    }

    /// @brief CLS has no foldable expressions.
    void visit(ClsStmt &) override {}

    /// @brief CURSOR has no foldable expressions.
    void visit(CursorStmt &) override {}

    /// @brief ALTSCREEN has no foldable expressions.
    void visit(AltScreenStmt &) override {}

    /// @brief Fold the foreground/background expressions for COLOR statements.
    void visit(ColorStmt &stmt) override
    {
        foldExpr(stmt.fg);
        foldExpr(stmt.bg);
    }

    /// @brief Fold the millisecond duration in SLEEP statements.
    /// @details The runtime clamps negatives; folding only simplifies literal
    ///          arithmetic in the duration expression, leaving semantics to lowering/runtime.
    void visit(SleepStmt &stmt) override
    {
        foldExpr(stmt.ms);
    }

    /// @brief Fold cursor position expressions for LOCATE statements.
    void visit(LocateStmt &stmt) override
    {
        foldExpr(stmt.row);
        foldExpr(stmt.col);
    }

    /// @brief Fold both the target and assigned expression in LET statements.
    void visit(LetStmt &stmt) override
    {
        foldExpr(stmt.target);
        foldExpr(stmt.expr);
    }

    /// @brief Fold array size expressions in DIM statements when present.
    void visit(DimStmt &stmt) override
    {
        if (stmt.isArray && stmt.size)
            foldExpr(stmt.size);
    }

    /// @brief Fold new bounds in REDIM statements when present.
    void visit(ReDimStmt &stmt) override
    {
        if (stmt.size)
            foldExpr(stmt.size);
    }

    /// @brief RANDOMIZE statements carry no foldable expressions.
    void visit(RandomizeStmt &) override {}

    /// @brief Fold predicates and branch bodies within IF statements.
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

    /// @brief Fold selectors and arms inside SELECT CASE statements.
    void visit(SelectCaseStmt &stmt) override
    {
        foldExpr(stmt.selector);
        for (auto &arm : stmt.arms)
            for (auto &bodyStmt : arm.body)
                foldStmt(bodyStmt);
        for (auto &bodyStmt : stmt.elseBody)
            foldStmt(bodyStmt);
    }

    /// @brief Fold loop predicates and bodies for WHILE statements.
    void visit(WhileStmt &stmt) override
    {
        foldExpr(stmt.cond);
        for (auto &bodyStmt : stmt.body)
            foldStmt(bodyStmt);
    }

    /// @brief Fold DO loop conditions (when present) and bodies.
    void visit(DoStmt &stmt) override
    {
        if (stmt.cond)
            foldExpr(stmt.cond);
        for (auto &bodyStmt : stmt.body)
            foldStmt(bodyStmt);
    }

    /// @brief Fold range and body expressions for FOR loops.
    void visit(ForStmt &stmt) override
    {
        foldExpr(stmt.start);
        foldExpr(stmt.end);
        if (stmt.step)
            foldExpr(stmt.step);
        for (auto &bodyStmt : stmt.body)
            foldStmt(bodyStmt);
    }

    /// @brief NEXT statements have no expressions to fold.
    void visit(NextStmt &) override {}

    /// @brief EXIT statements have no expressions to fold.
    void visit(ExitStmt &) override {}

    /// @brief GOTO statements have no expressions to fold.
    void visit(GotoStmt &) override {}

    /// @brief GOSUB statements have no expressions to fold.
    void visit(GosubStmt &) override {}

    /// @brief Fold OPEN statement operands such as path and channel.
    void visit(OpenStmt &stmt) override
    {
        if (stmt.pathExpr)
            foldExpr(stmt.pathExpr);
        if (stmt.channelExpr)
            foldExpr(stmt.channelExpr);
    }

    /// @brief Fold channel expressions in CLOSE statements.
    void visit(CloseStmt &stmt) override
    {
        if (stmt.channelExpr)
            foldExpr(stmt.channelExpr);
    }

    /// @brief Fold channel and offset expressions in SEEK statements.
    void visit(SeekStmt &stmt) override
    {
        if (stmt.channelExpr)
            foldExpr(stmt.channelExpr);
        if (stmt.positionExpr)
            foldExpr(stmt.positionExpr);
    }

    /// @brief ON ERROR GOTO contains no literal operands to fold.
    void visit(OnErrorGoto &) override {}

    /// @brief RESUME statements rely on runtime state and are left untouched.
    void visit(Resume &) override {}

    /// @brief END statements have no expressions to fold.
    void visit(EndStmt &) override {}

    /// @brief Fold prompts within INPUT statements when literal.
    void visit(InputStmt &stmt) override
    {
        if (stmt.prompt)
            foldExpr(stmt.prompt);
    }

    /// @brief INPUT # statements have no additional foldable expressions.
    void visit(InputChStmt &) override {}

    /// @brief Fold channel and destination expressions in LINE INPUT #.
    void visit(LineInputChStmt &stmt) override
    {
        foldExpr(stmt.channelExpr);
        foldExpr(stmt.targetVar);
    }

    /// @brief RETURN statements are control-only and do not fold expressions.
    void visit(ReturnStmt &) override {}

    /// @brief FUNCTION declarations are processed elsewhere; nothing to fold here.
    void visit(FunctionDecl &) override {}

    /// @brief SUB declarations are processed elsewhere; nothing to fold here.
    void visit(SubDecl &) override {}

    /// @brief Recursively fold every statement within a statement list.
    void visit(StmtList &stmt) override
    {
        for (auto &child : stmt.stmts)
            foldStmt(child);
    }

    /// @brief Fold the target expression of DELETE statements.
    void visit(DeleteStmt &stmt) override
    {
        foldExpr(stmt.target);
    }

    /// @brief Fold the body statements of constructors.
    void visit(ConstructorDecl &stmt) override
    {
        for (auto &bodyStmt : stmt.body)
            foldStmt(bodyStmt);
    }

    /// @brief Fold the body statements of destructors.
    void visit(DestructorDecl &stmt) override
    {
        for (auto &bodyStmt : stmt.body)
            foldStmt(bodyStmt);
    }

    /// @brief Fold the body statements of method declarations.
    void visit(MethodDecl &stmt) override
    {
        for (auto &bodyStmt : stmt.body)
            foldStmt(bodyStmt);
    }

    /// @brief Fold every member statement within class declarations.
    void visit(ClassDecl &stmt) override
    {
        for (auto &member : stmt.members)
            foldStmt(member);
    }

    /// @brief TYPE declarations define shapes only and do not fold expressions.
    void visit(TypeDecl &) override {}

    /// @brief Fold members inside INTERFACE declarations.
    void visit(InterfaceDecl &stmt) override
    {
        for (auto &member : stmt.members)
            foldStmt(member);
    }

    ExprPtr *currentExpr_ = nullptr;
    StmtPtr *currentStmt_ = nullptr;
};

} // namespace

/// @brief Perform constant folding across an entire BASIC program.
/// @param prog Program to mutate; expressions are folded in place.
void foldConstants(Program &prog)
{
    ConstFolderPass pass;
    pass.run(prog);
}

} // namespace il::frontends::basic
