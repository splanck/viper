// File: src/frontends/basic/AstWalker.hpp
// Purpose: Provides a reusable recursive AST walker for BASIC front-end passes.
// Key invariants: Traversal order matches the legacy visitors for statements and expressions.
// Ownership/Lifetime: Walker borrows AST nodes without owning them.
// Links: docs/codemap.md
#pragma once

#include "frontends/basic/AST.hpp"
#include "frontends/basic/AstWalkerUtils.hpp"

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
        expr.accept(derived());
    }

    /// @brief Visit a statement subtree.
    /// @param stmt Root statement to walk.
    void walkStmt(const Stmt &stmt)
    {
        stmt.accept(derived());
    }

  protected:
    BasicAstWalker() = default;

    /// @brief Invoke Derived::before(@p node) if provided.
    /// @details Override `before` to prepare state prior to visiting children
    /// (or to short-circuit by mutating walker state).
    template <typename Node>
    void callBefore(const Node &node)
    {
        walker_detail::dispatchBefore(derived(), node);
    }

    /// @brief Invoke Derived::after(@p node) if present.
    /// @details Override `after` to finalize state once a node and its children
    /// have been visited.
    template <typename Node>
    void callAfter(const Node &node)
    {
        walker_detail::dispatchAfter(derived(), node);
    }

    /// @brief Query Derived::shouldVisitChildren(@p node) when available.
    /// @details Override to skip visiting children for nodes where traversal is
    /// unnecessary or handled manually.
    template <typename Node>
    bool callShouldVisit(const Node &node)
    {
        return walker_detail::dispatchShouldVisit(derived(), node);
    }

    /// @brief Invoke Derived::beforeChild(@p parent, @p child) when supplied.
    /// @details Override to set up state before descending into a specific
    /// child relationship.
    template <typename Parent, typename Child>
    void callBeforeChild(const Parent &parent, const Child &child)
    {
        walker_detail::dispatchBeforeChild(derived(), parent, child);
    }

    /// @brief Invoke Derived::afterChild(@p parent, @p child) when supplied.
    /// @details Override to tear down per-child state or accumulate results
    /// after returning from that child.
    template <typename Parent, typename Child>
    void callAfterChild(const Parent &parent, const Child &child)
    {
        walker_detail::dispatchAfterChild(derived(), parent, child);
    }

  private:
    Derived &derived() noexcept
    {
        return *static_cast<Derived *>(this);
    }

  protected:
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
                expr.index->accept(derived());
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
                expr.expr->accept(derived());
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
                expr.lhs->accept(derived());
                callAfterChild(expr, *expr.lhs);
            }
            if (expr.rhs)
            {
                callBeforeChild(expr, *expr.rhs);
                expr.rhs->accept(derived());
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
                arg->accept(derived());
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
                arg->accept(derived());
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
                arg->accept(derived());
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
            expr.base->accept(derived());
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
                expr.base->accept(derived());
                callAfterChild(expr, *expr.base);
            }
            for (const auto &arg : expr.args)
            {
                if (!arg)
                    continue;
                callBeforeChild(expr, *arg);
                arg->accept(derived());
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
                item.expr->accept(derived());
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
                stmt.channelExpr->accept(derived());
                callAfterChild(stmt, *stmt.channelExpr);
            }
            for (const auto &arg : stmt.args)
            {
                if (!arg)
                    continue;
                callBeforeChild(stmt, *arg);
                arg->accept(derived());
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
            stmt.call->accept(derived());
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
                stmt.fg->accept(derived());
                callAfterChild(stmt, *stmt.fg);
            }
            if (stmt.bg)
            {
                callBeforeChild(stmt, *stmt.bg);
                stmt.bg->accept(derived());
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
                stmt.row->accept(derived());
                callAfterChild(stmt, *stmt.row);
            }
            if (stmt.col)
            {
                callBeforeChild(stmt, *stmt.col);
                stmt.col->accept(derived());
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
                stmt.target->accept(derived());
                callAfterChild(stmt, *stmt.target);
            }
            if (stmt.expr)
            {
                callBeforeChild(stmt, *stmt.expr);
                stmt.expr->accept(derived());
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
                stmt.size->accept(derived());
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
                stmt.size->accept(derived());
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
            stmt.seed->accept(derived());
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
                stmt.cond->accept(derived());
                callAfterChild(stmt, *stmt.cond);
            }
            if (stmt.then_branch)
            {
                callBeforeChild(stmt, *stmt.then_branch);
                stmt.then_branch->accept(derived());
                callAfterChild(stmt, *stmt.then_branch);
            }
            for (const auto &elseif : stmt.elseifs)
            {
                if (elseif.cond)
                {
                    callBeforeChild(stmt, *elseif.cond);
                    elseif.cond->accept(derived());
                    callAfterChild(stmt, *elseif.cond);
                }
                if (elseif.then_branch)
                {
                    callBeforeChild(stmt, *elseif.then_branch);
                    elseif.then_branch->accept(derived());
                    callAfterChild(stmt, *elseif.then_branch);
                }
            }
            if (stmt.else_branch)
            {
                callBeforeChild(stmt, *stmt.else_branch);
                stmt.else_branch->accept(derived());
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
                stmt.selector->accept(derived());
                callAfterChild(stmt, *stmt.selector);
            }
            for (const auto &arm : stmt.arms)
            {
                for (const auto &armStmt : arm.body)
                {
                    if (!armStmt)
                        continue;
                    callBeforeChild(stmt, *armStmt);
                    armStmt->accept(derived());
                    callAfterChild(stmt, *armStmt);
                }
            }
            for (const auto &elseStmt : stmt.elseBody)
            {
                if (!elseStmt)
                    continue;
                callBeforeChild(stmt, *elseStmt);
                elseStmt->accept(derived());
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
                stmt.cond->accept(derived());
                callAfterChild(stmt, *stmt.cond);
            }
            for (const auto &bodyStmt : stmt.body)
            {
                if (!bodyStmt)
                    continue;
                callBeforeChild(stmt, *bodyStmt);
                bodyStmt->accept(derived());
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
                stmt.cond->accept(derived());
                callAfterChild(stmt, *stmt.cond);
            }
            for (const auto &bodyStmt : stmt.body)
            {
                if (!bodyStmt)
                    continue;
                callBeforeChild(stmt, *bodyStmt);
                bodyStmt->accept(derived());
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
                stmt.start->accept(derived());
                callAfterChild(stmt, *stmt.start);
            }
            if (stmt.end)
            {
                callBeforeChild(stmt, *stmt.end);
                stmt.end->accept(derived());
                callAfterChild(stmt, *stmt.end);
            }
            if (stmt.step)
            {
                callBeforeChild(stmt, *stmt.step);
                stmt.step->accept(derived());
                callAfterChild(stmt, *stmt.step);
            }
            for (const auto &bodyStmt : stmt.body)
            {
                if (!bodyStmt)
                    continue;
                callBeforeChild(stmt, *bodyStmt);
                bodyStmt->accept(derived());
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
                stmt.pathExpr->accept(derived());
                callAfterChild(stmt, *stmt.pathExpr);
            }
            if (stmt.channelExpr)
            {
                callBeforeChild(stmt, *stmt.channelExpr);
                stmt.channelExpr->accept(derived());
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
            stmt.channelExpr->accept(derived());
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
                stmt.channelExpr->accept(derived());
                callAfterChild(stmt, *stmt.channelExpr);
            }
            if (stmt.positionExpr)
            {
                callBeforeChild(stmt, *stmt.positionExpr);
                stmt.positionExpr->accept(derived());
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
            stmt.prompt->accept(derived());
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
                stmt.channelExpr->accept(derived());
                callAfterChild(stmt, *stmt.channelExpr);
            }
            if (stmt.targetVar)
            {
                callBeforeChild(stmt, *stmt.targetVar);
                stmt.targetVar->accept(derived());
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
            stmt.value->accept(derived());
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
                bodyStmt->accept(derived());
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
                bodyStmt->accept(derived());
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
            stmt.target->accept(derived());
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
                bodyStmt->accept(derived());
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
                bodyStmt->accept(derived());
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
                bodyStmt->accept(derived());
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
                member->accept(derived());
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
                sub->accept(derived());
                callAfterChild(stmt, *sub);
            }
        }
        callAfter(stmt);
    }
};

} // namespace il::frontends::basic
