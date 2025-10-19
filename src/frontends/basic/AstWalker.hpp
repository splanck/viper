// File: src/frontends/basic/AstWalker.hpp
// Purpose: Provides a reusable recursive AST walker for BASIC front-end passes.
// Key invariants: Traversal order matches the legacy visitors for statements and expressions.
// Ownership/Lifetime: Walker borrows AST nodes without owning them.
// Links: docs/codemap.md
#pragma once

#include "frontends/basic/ast/DeclNodes.hpp"
#include <type_traits>

namespace il::frontends::basic
{

/// @brief Generic recursive AST walker that forwards traversal hooks to @p Derived.
/// @tparam Derived Concrete walker implementing optional callbacks.
/// @details The walker visits statements and expressions in the same order as the
/// legacy lowering visitors. Derived classes may override `before`,
/// `after`, `shouldVisitChildren`, `beforeChild`, and `afterChild` for any
/// node type; the base implementation provides no-op defaults.
template <typename Derived>
class BasicAstWalker : public ExprVisitor, public StmtVisitor
{
  public:
    /// @brief Visit an expression subtree.
    /// @param expr Root expression to walk.
    void walkExpr(const Expr &expr)
    {
        expr.accept(*static_cast<Derived *>(this));
    }

    /// @brief Visit a statement subtree.
    /// @param stmt Root statement to walk.
    void walkStmt(const Stmt &stmt)
    {
        stmt.accept(*static_cast<Derived *>(this));
    }

  protected:
    BasicAstWalker() = default;

    template <typename Node>
    void callBefore(const Node &node)
    {
        if constexpr (requires(Derived &d, const Node &n) { d.before(n); })
            static_cast<Derived *>(this)->before(node);
    }

    template <typename Node>
    void callAfter(const Node &node)
    {
        if constexpr (requires(Derived &d, const Node &n) { d.after(n); })
            static_cast<Derived *>(this)->after(node);
    }

    template <typename Node>
    bool callShouldVisit(const Node &node)
    {
        if constexpr (requires(Derived &d, const Node &n) { d.shouldVisitChildren(n); })
            return static_cast<Derived *>(this)->shouldVisitChildren(node);
        return true;
    }

    template <typename Parent, typename Child>
    void callBeforeChild(const Parent &parent, const Child &child)
    {
        if constexpr (requires(Derived &d, const Parent &p, const Child &c) { d.beforeChild(p, c); })
            static_cast<Derived *>(this)->beforeChild(parent, child);
    }

    template <typename Parent, typename Child>
    void callAfterChild(const Parent &parent, const Child &child)
    {
        if constexpr (requires(Derived &d, const Parent &p, const Child &c) { d.afterChild(p, c); })
            static_cast<Derived *>(this)->afterChild(parent, child);
    }

    // Expression visitors --------------------------------------------------

    void visit(const IntExpr &expr) override
    {
        callBefore(expr);
        callAfter(expr);
    }

    void visit(const FloatExpr &expr) override
    {
        callBefore(expr);
        callAfter(expr);
    }

    void visit(const StringExpr &expr) override
    {
        callBefore(expr);
        callAfter(expr);
    }

    void visit(const BoolExpr &expr) override
    {
        callBefore(expr);
        callAfter(expr);
    }

    void visit(const VarExpr &expr) override
    {
        callBefore(expr);
        callAfter(expr);
    }

    void visit(const ArrayExpr &expr) override
    {
        callBefore(expr);
        if (callShouldVisit(expr))
        {
            if (expr.index)
            {
                callBeforeChild(expr, *expr.index);
                expr.index->accept(*static_cast<Derived *>(this));
                callAfterChild(expr, *expr.index);
            }
        }
        callAfter(expr);
    }

    void visit(const LBoundExpr &expr) override
    {
        callBefore(expr);
        callAfter(expr);
    }

    void visit(const UBoundExpr &expr) override
    {
        callBefore(expr);
        callAfter(expr);
    }

    void visit(const UnaryExpr &expr) override
    {
        callBefore(expr);
        if (callShouldVisit(expr))
        {
            if (expr.expr)
            {
                callBeforeChild(expr, *expr.expr);
                expr.expr->accept(*static_cast<Derived *>(this));
                callAfterChild(expr, *expr.expr);
            }
        }
        callAfter(expr);
    }

    void visit(const BinaryExpr &expr) override
    {
        callBefore(expr);
        if (callShouldVisit(expr))
        {
            if (expr.lhs)
            {
                callBeforeChild(expr, *expr.lhs);
                expr.lhs->accept(*static_cast<Derived *>(this));
                callAfterChild(expr, *expr.lhs);
            }
            if (expr.rhs)
            {
                callBeforeChild(expr, *expr.rhs);
                expr.rhs->accept(*static_cast<Derived *>(this));
                callAfterChild(expr, *expr.rhs);
            }
        }
        callAfter(expr);
    }

    void visit(const BuiltinCallExpr &expr) override
    {
        callBefore(expr);
        if (callShouldVisit(expr))
        {
            for (const auto &arg : expr.args)
            {
                if (!arg)
                    continue;
                callBeforeChild(expr, *arg);
                arg->accept(*static_cast<Derived *>(this));
                callAfterChild(expr, *arg);
            }
        }
        callAfter(expr);
    }

    void visit(const CallExpr &expr) override
    {
        callBefore(expr);
        if (callShouldVisit(expr))
        {
            for (const auto &arg : expr.args)
            {
                if (!arg)
                    continue;
                callBeforeChild(expr, *arg);
                arg->accept(*static_cast<Derived *>(this));
                callAfterChild(expr, *arg);
            }
        }
        callAfter(expr);
    }

    void visit(const NewExpr &expr) override
    {
        callBefore(expr);
        if (callShouldVisit(expr))
        {
            for (const auto &arg : expr.args)
            {
                if (!arg)
                    continue;
                callBeforeChild(expr, *arg);
                arg->accept(*static_cast<Derived *>(this));
                callAfterChild(expr, *arg);
            }
        }
        callAfter(expr);
    }

    void visit(const MeExpr &expr) override
    {
        callBefore(expr);
        callAfter(expr);
    }

    void visit(const MemberAccessExpr &expr) override
    {
        callBefore(expr);
        if (callShouldVisit(expr) && expr.base)
        {
            callBeforeChild(expr, *expr.base);
            expr.base->accept(*static_cast<Derived *>(this));
            callAfterChild(expr, *expr.base);
        }
        callAfter(expr);
    }

    void visit(const MethodCallExpr &expr) override
    {
        callBefore(expr);
        if (callShouldVisit(expr))
        {
            if (expr.base)
            {
                callBeforeChild(expr, *expr.base);
                expr.base->accept(*static_cast<Derived *>(this));
                callAfterChild(expr, *expr.base);
            }
            for (const auto &arg : expr.args)
            {
                if (!arg)
                    continue;
                callBeforeChild(expr, *arg);
                arg->accept(*static_cast<Derived *>(this));
                callAfterChild(expr, *arg);
            }
        }
        callAfter(expr);
    }

    // Statement visitors ---------------------------------------------------

    void visit(const LabelStmt &stmt) override
    {
        callBefore(stmt);
        callAfter(stmt);
    }

    void visit(const PrintStmt &stmt) override
    {
        callBefore(stmt);
        if (callShouldVisit(stmt))
        {
            for (const auto &item : stmt.items)
            {
                if (item.kind != PrintItem::Kind::Expr || !item.expr)
                    continue;
                callBeforeChild(stmt, *item.expr);
                item.expr->accept(*static_cast<Derived *>(this));
                callAfterChild(stmt, *item.expr);
            }
        }
        callAfter(stmt);
    }

    void visit(const PrintChStmt &stmt) override
    {
        callBefore(stmt);
        if (callShouldVisit(stmt))
        {
            if (stmt.channelExpr)
            {
                callBeforeChild(stmt, *stmt.channelExpr);
                stmt.channelExpr->accept(*static_cast<Derived *>(this));
                callAfterChild(stmt, *stmt.channelExpr);
            }
            for (const auto &arg : stmt.args)
            {
                if (!arg)
                    continue;
                callBeforeChild(stmt, *arg);
                arg->accept(*static_cast<Derived *>(this));
                callAfterChild(stmt, *arg);
            }
        }
        callAfter(stmt);
    }

    void visit(const CallStmt &stmt) override
    {
        callBefore(stmt);
        if (callShouldVisit(stmt) && stmt.call)
        {
            callBeforeChild(stmt, *stmt.call);
            stmt.call->accept(*static_cast<Derived *>(this));
            callAfterChild(stmt, *stmt.call);
        }
        callAfter(stmt);
    }

    void visit(const ClsStmt &stmt) override
    {
        callBefore(stmt);
        if (callShouldVisit(stmt))
        {
            // CLS has no child expressions.
        }
        callAfter(stmt);
    }

    void visit(const ColorStmt &stmt) override
    {
        callBefore(stmt);
        if (callShouldVisit(stmt))
        {
            if (stmt.fg)
            {
                callBeforeChild(stmt, *stmt.fg);
                stmt.fg->accept(*static_cast<Derived *>(this));
                callAfterChild(stmt, *stmt.fg);
            }
            if (stmt.bg)
            {
                callBeforeChild(stmt, *stmt.bg);
                stmt.bg->accept(*static_cast<Derived *>(this));
                callAfterChild(stmt, *stmt.bg);
            }
        }
        callAfter(stmt);
    }

    void visit(const LocateStmt &stmt) override
    {
        callBefore(stmt);
        if (callShouldVisit(stmt))
        {
            if (stmt.row)
            {
                callBeforeChild(stmt, *stmt.row);
                stmt.row->accept(*static_cast<Derived *>(this));
                callAfterChild(stmt, *stmt.row);
            }
            if (stmt.col)
            {
                callBeforeChild(stmt, *stmt.col);
                stmt.col->accept(*static_cast<Derived *>(this));
                callAfterChild(stmt, *stmt.col);
            }
        }
        callAfter(stmt);
    }

    void visit(const LetStmt &stmt) override
    {
        callBefore(stmt);
        if (callShouldVisit(stmt))
        {
            if (stmt.target)
            {
                callBeforeChild(stmt, *stmt.target);
                stmt.target->accept(*static_cast<Derived *>(this));
                callAfterChild(stmt, *stmt.target);
            }
            if (stmt.expr)
            {
                callBeforeChild(stmt, *stmt.expr);
                stmt.expr->accept(*static_cast<Derived *>(this));
                callAfterChild(stmt, *stmt.expr);
            }
        }
        callAfter(stmt);
    }

    void visit(const DimStmt &stmt) override
    {
        callBefore(stmt);
        if (callShouldVisit(stmt))
        {
            if (stmt.size)
            {
                callBeforeChild(stmt, *stmt.size);
                stmt.size->accept(*static_cast<Derived *>(this));
                callAfterChild(stmt, *stmt.size);
            }
        }
        callAfter(stmt);
    }

    void visit(const ReDimStmt &stmt) override
    {
        callBefore(stmt);
        if (callShouldVisit(stmt))
        {
            if (stmt.size)
            {
                callBeforeChild(stmt, *stmt.size);
                stmt.size->accept(*static_cast<Derived *>(this));
                callAfterChild(stmt, *stmt.size);
            }
        }
        callAfter(stmt);
    }

    void visit(const RandomizeStmt &stmt) override
    {
        callBefore(stmt);
        if (callShouldVisit(stmt) && stmt.seed)
        {
            callBeforeChild(stmt, *stmt.seed);
            stmt.seed->accept(*static_cast<Derived *>(this));
            callAfterChild(stmt, *stmt.seed);
        }
        callAfter(stmt);
    }

    void visit(const IfStmt &stmt) override
    {
        callBefore(stmt);
        if (callShouldVisit(stmt))
        {
            if (stmt.cond)
            {
                callBeforeChild(stmt, *stmt.cond);
                stmt.cond->accept(*static_cast<Derived *>(this));
                callAfterChild(stmt, *stmt.cond);
            }
            if (stmt.then_branch)
            {
                callBeforeChild(stmt, *stmt.then_branch);
                stmt.then_branch->accept(*static_cast<Derived *>(this));
                callAfterChild(stmt, *stmt.then_branch);
            }
            for (const auto &elseif : stmt.elseifs)
            {
                if (elseif.cond)
                {
                    callBeforeChild(stmt, *elseif.cond);
                    elseif.cond->accept(*static_cast<Derived *>(this));
                    callAfterChild(stmt, *elseif.cond);
                }
                if (elseif.then_branch)
                {
                    callBeforeChild(stmt, *elseif.then_branch);
                    elseif.then_branch->accept(*static_cast<Derived *>(this));
                    callAfterChild(stmt, *elseif.then_branch);
                }
            }
            if (stmt.else_branch)
            {
                callBeforeChild(stmt, *stmt.else_branch);
                stmt.else_branch->accept(*static_cast<Derived *>(this));
                callAfterChild(stmt, *stmt.else_branch);
            }
        }
        callAfter(stmt);
    }

    void visit(const SelectCaseStmt &stmt) override
    {
        callBefore(stmt);
        if (callShouldVisit(stmt))
        {
            if (stmt.selector)
            {
                callBeforeChild(stmt, *stmt.selector);
                stmt.selector->accept(*static_cast<Derived *>(this));
                callAfterChild(stmt, *stmt.selector);
            }
            for (const auto &arm : stmt.arms)
            {
                for (const auto &armStmt : arm.body)
                {
                    if (!armStmt)
                        continue;
                    callBeforeChild(stmt, *armStmt);
                    armStmt->accept(*static_cast<Derived *>(this));
                    callAfterChild(stmt, *armStmt);
                }
            }
            for (const auto &elseStmt : stmt.elseBody)
            {
                if (!elseStmt)
                    continue;
                callBeforeChild(stmt, *elseStmt);
                elseStmt->accept(*static_cast<Derived *>(this));
                callAfterChild(stmt, *elseStmt);
            }
        }
        callAfter(stmt);
    }

    void visit(const WhileStmt &stmt) override
    {
        callBefore(stmt);
        if (callShouldVisit(stmt))
        {
            if (stmt.cond)
            {
                callBeforeChild(stmt, *stmt.cond);
                stmt.cond->accept(*static_cast<Derived *>(this));
                callAfterChild(stmt, *stmt.cond);
            }
            for (const auto &bodyStmt : stmt.body)
            {
                if (!bodyStmt)
                    continue;
                callBeforeChild(stmt, *bodyStmt);
                bodyStmt->accept(*static_cast<Derived *>(this));
                callAfterChild(stmt, *bodyStmt);
            }
        }
        callAfter(stmt);
    }

    void visit(const DoStmt &stmt) override
    {
        callBefore(stmt);
        if (callShouldVisit(stmt))
        {
            if (stmt.cond)
            {
                callBeforeChild(stmt, *stmt.cond);
                stmt.cond->accept(*static_cast<Derived *>(this));
                callAfterChild(stmt, *stmt.cond);
            }
            for (const auto &bodyStmt : stmt.body)
            {
                if (!bodyStmt)
                    continue;
                callBeforeChild(stmt, *bodyStmt);
                bodyStmt->accept(*static_cast<Derived *>(this));
                callAfterChild(stmt, *bodyStmt);
            }
        }
        callAfter(stmt);
    }

    void visit(const ForStmt &stmt) override
    {
        callBefore(stmt);
        if (callShouldVisit(stmt))
        {
            if (stmt.start)
            {
                callBeforeChild(stmt, *stmt.start);
                stmt.start->accept(*static_cast<Derived *>(this));
                callAfterChild(stmt, *stmt.start);
            }
            if (stmt.end)
            {
                callBeforeChild(stmt, *stmt.end);
                stmt.end->accept(*static_cast<Derived *>(this));
                callAfterChild(stmt, *stmt.end);
            }
            if (stmt.step)
            {
                callBeforeChild(stmt, *stmt.step);
                stmt.step->accept(*static_cast<Derived *>(this));
                callAfterChild(stmt, *stmt.step);
            }
            for (const auto &bodyStmt : stmt.body)
            {
                if (!bodyStmt)
                    continue;
                callBeforeChild(stmt, *bodyStmt);
                bodyStmt->accept(*static_cast<Derived *>(this));
                callAfterChild(stmt, *bodyStmt);
            }
        }
        callAfter(stmt);
    }

    void visit(const NextStmt &stmt) override
    {
        callBefore(stmt);
        callAfter(stmt);
    }

    void visit(const ExitStmt &stmt) override
    {
        callBefore(stmt);
        callAfter(stmt);
    }

    void visit(const GotoStmt &stmt) override
    {
        callBefore(stmt);
        callAfter(stmt);
    }

    void visit(const GosubStmt &stmt) override
    {
        callBefore(stmt);
        callAfter(stmt);
    }

    void visit(const OpenStmt &stmt) override
    {
        callBefore(stmt);
        if (callShouldVisit(stmt))
        {
            if (stmt.pathExpr)
            {
                callBeforeChild(stmt, *stmt.pathExpr);
                stmt.pathExpr->accept(*static_cast<Derived *>(this));
                callAfterChild(stmt, *stmt.pathExpr);
            }
            if (stmt.channelExpr)
            {
                callBeforeChild(stmt, *stmt.channelExpr);
                stmt.channelExpr->accept(*static_cast<Derived *>(this));
                callAfterChild(stmt, *stmt.channelExpr);
            }
        }
        callAfter(stmt);
    }

    void visit(const CloseStmt &stmt) override
    {
        callBefore(stmt);
        if (callShouldVisit(stmt) && stmt.channelExpr)
        {
            callBeforeChild(stmt, *stmt.channelExpr);
            stmt.channelExpr->accept(*static_cast<Derived *>(this));
            callAfterChild(stmt, *stmt.channelExpr);
        }
        callAfter(stmt);
    }

    void visit(const SeekStmt &stmt) override
    {
        callBefore(stmt);
        if (callShouldVisit(stmt))
        {
            if (stmt.channelExpr)
            {
                callBeforeChild(stmt, *stmt.channelExpr);
                stmt.channelExpr->accept(*static_cast<Derived *>(this));
                callAfterChild(stmt, *stmt.channelExpr);
            }
            if (stmt.positionExpr)
            {
                callBeforeChild(stmt, *stmt.positionExpr);
                stmt.positionExpr->accept(*static_cast<Derived *>(this));
                callAfterChild(stmt, *stmt.positionExpr);
            }
        }
        callAfter(stmt);
    }

    void visit(const OnErrorGoto &stmt) override
    {
        callBefore(stmt);
        callAfter(stmt);
    }

    void visit(const Resume &stmt) override
    {
        callBefore(stmt);
        callAfter(stmt);
    }

    void visit(const EndStmt &stmt) override
    {
        callBefore(stmt);
        callAfter(stmt);
    }

    void visit(const InputStmt &stmt) override
    {
        callBefore(stmt);
        if (callShouldVisit(stmt) && stmt.prompt)
        {
            callBeforeChild(stmt, *stmt.prompt);
            stmt.prompt->accept(*static_cast<Derived *>(this));
            callAfterChild(stmt, *stmt.prompt);
        }
        callAfter(stmt);
    }

    void visit(const InputChStmt &stmt) override
    {
        callBefore(stmt);
        callAfter(stmt);
    }

    void visit(const LineInputChStmt &stmt) override
    {
        callBefore(stmt);
        if (callShouldVisit(stmt))
        {
            if (stmt.channelExpr)
            {
                callBeforeChild(stmt, *stmt.channelExpr);
                stmt.channelExpr->accept(*static_cast<Derived *>(this));
                callAfterChild(stmt, *stmt.channelExpr);
            }
            if (stmt.targetVar)
            {
                callBeforeChild(stmt, *stmt.targetVar);
                stmt.targetVar->accept(*static_cast<Derived *>(this));
                callAfterChild(stmt, *stmt.targetVar);
            }
        }
        callAfter(stmt);
    }

    void visit(const ReturnStmt &stmt) override
    {
        callBefore(stmt);
        if (callShouldVisit(stmt) && stmt.value)
        {
            callBeforeChild(stmt, *stmt.value);
            stmt.value->accept(*static_cast<Derived *>(this));
            callAfterChild(stmt, *stmt.value);
        }
        callAfter(stmt);
    }

    void visit(const FunctionDecl &stmt) override
    {
        callBefore(stmt);
        if (callShouldVisit(stmt))
        {
            for (const auto &bodyStmt : stmt.body)
            {
                if (!bodyStmt)
                    continue;
                callBeforeChild(stmt, *bodyStmt);
                bodyStmt->accept(*static_cast<Derived *>(this));
                callAfterChild(stmt, *bodyStmt);
            }
        }
        callAfter(stmt);
    }

    void visit(const SubDecl &stmt) override
    {
        callBefore(stmt);
        if (callShouldVisit(stmt))
        {
            for (const auto &bodyStmt : stmt.body)
            {
                if (!bodyStmt)
                    continue;
                callBeforeChild(stmt, *bodyStmt);
                bodyStmt->accept(*static_cast<Derived *>(this));
                callAfterChild(stmt, *bodyStmt);
            }
        }
        callAfter(stmt);
    }

    void visit(const DeleteStmt &stmt) override
    {
        callBefore(stmt);
        if (callShouldVisit(stmt) && stmt.target)
        {
            callBeforeChild(stmt, *stmt.target);
            stmt.target->accept(*static_cast<Derived *>(this));
            callAfterChild(stmt, *stmt.target);
        }
        callAfter(stmt);
    }

    void visit(const ConstructorDecl &stmt) override
    {
        callBefore(stmt);
        if (callShouldVisit(stmt))
        {
            for (const auto &param : stmt.params)
            {
                callBeforeChild(stmt, param);
                callAfterChild(stmt, param);
            }
            for (const auto &bodyStmt : stmt.body)
            {
                if (!bodyStmt)
                    continue;
                callBeforeChild(stmt, *bodyStmt);
                bodyStmt->accept(*static_cast<Derived *>(this));
                callAfterChild(stmt, *bodyStmt);
            }
        }
        callAfter(stmt);
    }

    void visit(const DestructorDecl &stmt) override
    {
        callBefore(stmt);
        if (callShouldVisit(stmt))
        {
            for (const auto &bodyStmt : stmt.body)
            {
                if (!bodyStmt)
                    continue;
                callBeforeChild(stmt, *bodyStmt);
                bodyStmt->accept(*static_cast<Derived *>(this));
                callAfterChild(stmt, *bodyStmt);
            }
        }
        callAfter(stmt);
    }

    void visit(const MethodDecl &stmt) override
    {
        callBefore(stmt);
        if (callShouldVisit(stmt))
        {
            for (const auto &param : stmt.params)
            {
                callBeforeChild(stmt, param);
                callAfterChild(stmt, param);
            }
            for (const auto &bodyStmt : stmt.body)
            {
                if (!bodyStmt)
                    continue;
                callBeforeChild(stmt, *bodyStmt);
                bodyStmt->accept(*static_cast<Derived *>(this));
                callAfterChild(stmt, *bodyStmt);
            }
        }
        callAfter(stmt);
    }

    void visit(const ClassDecl &stmt) override
    {
        callBefore(stmt);
        if (callShouldVisit(stmt))
        {
            for (const auto &member : stmt.members)
            {
                if (!member)
                    continue;
                callBeforeChild(stmt, *member);
                member->accept(*static_cast<Derived *>(this));
                callAfterChild(stmt, *member);
            }
        }
        callAfter(stmt);
    }

    void visit(const TypeDecl &stmt) override
    {
        callBefore(stmt);
        if (callShouldVisit(stmt))
        {
            // TYPE fields are simple declarations without nested AST nodes.
        }
        callAfter(stmt);
    }

    void visit(const StmtList &stmt) override
    {
        callBefore(stmt);
        if (callShouldVisit(stmt))
        {
            for (const auto &sub : stmt.stmts)
            {
                if (!sub)
                    continue;
                callBeforeChild(stmt, *sub);
                sub->accept(*static_cast<Derived *>(this));
                callAfterChild(stmt, *sub);
            }
        }
        callAfter(stmt);
    }
};

} // namespace il::frontends::basic
