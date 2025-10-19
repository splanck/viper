// File: src/frontends/basic/AstWalker.hpp
// Purpose: Provides a reusable recursive AST walker for BASIC front-end passes.
// Key invariants: Traversal order matches the legacy visitors for statements and expressions.
// Ownership/Lifetime: Walker borrows AST nodes without owning them.
// Links: docs/codemap.md
#pragma once

#include "frontends/basic/AstWalkerUtils.hpp"
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
template <typename Derived> class BasicAstWalker : public ExprVisitor, public StmtVisitor
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

    template <typename OtherDerived, typename Parent, typename Ptr>
    friend void walker::detail::visitOptionalChild(BasicAstWalker<OtherDerived> &,
                                                   const Parent &,
                                                   const Ptr &);
    template <typename OtherDerived, typename Parent, typename Range>
    friend void walker::detail::visitChildRange(BasicAstWalker<OtherDerived> &,
                                                const Parent &,
                                                const Range &);
    template <typename OtherDerived, typename Parent, typename Child>
    friend void walker::detail::notifyChild(BasicAstWalker<OtherDerived> &,
                                            const Parent &,
                                            const Child &);
    template <typename OtherDerived, typename Parent, typename Range>
    friend void walker::detail::notifyChildRange(BasicAstWalker<OtherDerived> &,
                                                 const Parent &,
                                                 const Range &);
    template <typename OtherDerived>
    friend void walker::detail::visitPrintItem(BasicAstWalker<OtherDerived> &,
                                               const PrintStmt &,
                                               const PrintItem &);
    template <typename OtherDerived>
    friend void walker::detail::visitPrintItems(BasicAstWalker<OtherDerived> &, const PrintStmt &);

    /// @brief Invoke the Derived pre-visit hook when available.
    /// @details Override `Derived::before` to run logic before traversing @p node, such as
    /// updating bookkeeping stacks or allocating temporary state.
    template <typename Node> void callBefore(const Node &node)
    {
        if constexpr (requires(Derived &d, const Node &n) { d.before(n); })
            static_cast<Derived *>(this)->before(node);
    }

    /// @brief Invoke the Derived post-visit hook when available.
    /// @details Override `Derived::after` to clean up state after all children of @p node
    /// were processed or to record synthesized results.
    template <typename Node> void callAfter(const Node &node)
    {
        if constexpr (requires(Derived &d, const Node &n) { d.after(n); })
            static_cast<Derived *>(this)->after(node);
    }

    /// @brief Ask Derived whether to traverse the children of @p node.
    /// @details Override `Derived::shouldVisitChildren` to short-circuit traversal for
    /// pruned subtrees or to skip nodes that were already processed elsewhere.
    template <typename Node> bool callShouldVisit(const Node &node)
    {
        if constexpr (requires(Derived &d, const Node &n) { d.shouldVisitChildren(n); })
            return static_cast<Derived *>(this)->shouldVisitChildren(node);
        return true;
    }

    /// @brief Invoke the Derived hook before visiting @p child.
    /// @details Override `Derived::beforeChild` to observe the parent/child relationship
    /// prior to recursively traversing the child node.
    template <typename Parent, typename Child>
    void callBeforeChild(const Parent &parent, const Child &child)
    {
        if constexpr (requires(Derived &d, const Parent &p, const Child &c) {
                          d.beforeChild(p, c);
                      })
            static_cast<Derived *>(this)->beforeChild(parent, child);
    }

    /// @brief Invoke the Derived hook after visiting @p child.
    /// @details Override `Derived::afterChild` to perform post-processing that requires
    /// both the parent context and the just-visited child node.
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
            walker::detail::visitOptionalChild(*this, expr, expr.index);
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
            walker::detail::visitOptionalChild(*this, expr, expr.expr);
        }
        callAfter(expr);
    }

    void visit(const BinaryExpr &expr) override
    {
        callBefore(expr);
        if (callShouldVisit(expr))
        {
            walker::detail::visitOptionalChild(*this, expr, expr.lhs);
            walker::detail::visitOptionalChild(*this, expr, expr.rhs);
        }
        callAfter(expr);
    }

    void visit(const BuiltinCallExpr &expr) override
    {
        callBefore(expr);
        if (callShouldVisit(expr))
        {
            walker::detail::visitChildRange(*this, expr, expr.args);
        }
        callAfter(expr);
    }

    void visit(const CallExpr &expr) override
    {
        callBefore(expr);
        if (callShouldVisit(expr))
        {
            walker::detail::visitChildRange(*this, expr, expr.args);
        }
        callAfter(expr);
    }

    void visit(const NewExpr &expr) override
    {
        callBefore(expr);
        if (callShouldVisit(expr))
        {
            walker::detail::visitChildRange(*this, expr, expr.args);
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
        if (callShouldVisit(expr))
        {
            walker::detail::visitOptionalChild(*this, expr, expr.base);
        }
        callAfter(expr);
    }

    void visit(const MethodCallExpr &expr) override
    {
        callBefore(expr);
        if (callShouldVisit(expr))
        {
            walker::detail::visitOptionalChild(*this, expr, expr.base);
            walker::detail::visitChildRange(*this, expr, expr.args);
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
            walker::detail::visitPrintItems(*this, stmt);
        }
        callAfter(stmt);
    }

    void visit(const PrintChStmt &stmt) override
    {
        callBefore(stmt);
        if (callShouldVisit(stmt))
        {
            walker::detail::visitOptionalChild(*this, stmt, stmt.channelExpr);
            walker::detail::visitChildRange(*this, stmt, stmt.args);
        }
        callAfter(stmt);
    }

    void visit(const CallStmt &stmt) override
    {
        callBefore(stmt);
        if (callShouldVisit(stmt))
        {
            walker::detail::visitOptionalChild(*this, stmt, stmt.call);
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
            walker::detail::visitOptionalChild(*this, stmt, stmt.fg);
            walker::detail::visitOptionalChild(*this, stmt, stmt.bg);
        }
        callAfter(stmt);
    }

    void visit(const LocateStmt &stmt) override
    {
        callBefore(stmt);
        if (callShouldVisit(stmt))
        {
            walker::detail::visitOptionalChild(*this, stmt, stmt.row);
            walker::detail::visitOptionalChild(*this, stmt, stmt.col);
        }
        callAfter(stmt);
    }

    void visit(const LetStmt &stmt) override
    {
        callBefore(stmt);
        if (callShouldVisit(stmt))
        {
            walker::detail::visitOptionalChild(*this, stmt, stmt.target);
            walker::detail::visitOptionalChild(*this, stmt, stmt.expr);
        }
        callAfter(stmt);
    }

    void visit(const DimStmt &stmt) override
    {
        callBefore(stmt);
        if (callShouldVisit(stmt))
        {
            walker::detail::visitOptionalChild(*this, stmt, stmt.size);
        }
        callAfter(stmt);
    }

    void visit(const ReDimStmt &stmt) override
    {
        callBefore(stmt);
        if (callShouldVisit(stmt))
        {
            walker::detail::visitOptionalChild(*this, stmt, stmt.size);
        }
        callAfter(stmt);
    }

    void visit(const RandomizeStmt &stmt) override
    {
        callBefore(stmt);
        if (callShouldVisit(stmt))
        {
            walker::detail::visitOptionalChild(*this, stmt, stmt.seed);
        }
        callAfter(stmt);
    }

    void visit(const IfStmt &stmt) override
    {
        callBefore(stmt);
        if (callShouldVisit(stmt))
        {
            walker::detail::visitOptionalChild(*this, stmt, stmt.cond);
            walker::detail::visitOptionalChild(*this, stmt, stmt.then_branch);
            for (const auto &elseif : stmt.elseifs)
            {
                walker::detail::visitOptionalChild(*this, stmt, elseif.cond);
                walker::detail::visitOptionalChild(*this, stmt, elseif.then_branch);
            }
            walker::detail::visitOptionalChild(*this, stmt, stmt.else_branch);
        }
        callAfter(stmt);
    }

    void visit(const SelectCaseStmt &stmt) override
    {
        callBefore(stmt);
        if (callShouldVisit(stmt))
        {
            walker::detail::visitOptionalChild(*this, stmt, stmt.selector);
            for (const auto &arm : stmt.arms)
            {
                walker::detail::visitChildRange(*this, stmt, arm.body);
            }
            walker::detail::visitChildRange(*this, stmt, stmt.elseBody);
        }
        callAfter(stmt);
    }

    void visit(const WhileStmt &stmt) override
    {
        callBefore(stmt);
        if (callShouldVisit(stmt))
        {
            walker::detail::visitOptionalChild(*this, stmt, stmt.cond);
            walker::detail::visitChildRange(*this, stmt, stmt.body);
        }
        callAfter(stmt);
    }

    void visit(const DoStmt &stmt) override
    {
        callBefore(stmt);
        if (callShouldVisit(stmt))
        {
            walker::detail::visitOptionalChild(*this, stmt, stmt.cond);
            walker::detail::visitChildRange(*this, stmt, stmt.body);
        }
        callAfter(stmt);
    }

    void visit(const ForStmt &stmt) override
    {
        callBefore(stmt);
        if (callShouldVisit(stmt))
        {
            walker::detail::visitOptionalChild(*this, stmt, stmt.start);
            walker::detail::visitOptionalChild(*this, stmt, stmt.end);
            walker::detail::visitOptionalChild(*this, stmt, stmt.step);
            walker::detail::visitChildRange(*this, stmt, stmt.body);
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
            walker::detail::visitOptionalChild(*this, stmt, stmt.pathExpr);
            walker::detail::visitOptionalChild(*this, stmt, stmt.channelExpr);
        }
        callAfter(stmt);
    }

    void visit(const CloseStmt &stmt) override
    {
        callBefore(stmt);
        if (callShouldVisit(stmt))
        {
            walker::detail::visitOptionalChild(*this, stmt, stmt.channelExpr);
        }
        callAfter(stmt);
    }

    void visit(const SeekStmt &stmt) override
    {
        callBefore(stmt);
        if (callShouldVisit(stmt))
        {
            walker::detail::visitOptionalChild(*this, stmt, stmt.channelExpr);
            walker::detail::visitOptionalChild(*this, stmt, stmt.positionExpr);
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
        if (callShouldVisit(stmt))
        {
            walker::detail::visitOptionalChild(*this, stmt, stmt.prompt);
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
            walker::detail::visitOptionalChild(*this, stmt, stmt.channelExpr);
            walker::detail::visitOptionalChild(*this, stmt, stmt.targetVar);
        }
        callAfter(stmt);
    }

    void visit(const ReturnStmt &stmt) override
    {
        callBefore(stmt);
        if (callShouldVisit(stmt))
        {
            walker::detail::visitOptionalChild(*this, stmt, stmt.value);
        }
        callAfter(stmt);
    }

    void visit(const FunctionDecl &stmt) override
    {
        callBefore(stmt);
        if (callShouldVisit(stmt))
        {
            walker::detail::visitChildRange(*this, stmt, stmt.body);
        }
        callAfter(stmt);
    }

    void visit(const SubDecl &stmt) override
    {
        callBefore(stmt);
        if (callShouldVisit(stmt))
        {
            walker::detail::visitChildRange(*this, stmt, stmt.body);
        }
        callAfter(stmt);
    }

    void visit(const DeleteStmt &stmt) override
    {
        callBefore(stmt);
        if (callShouldVisit(stmt))
        {
            walker::detail::visitOptionalChild(*this, stmt, stmt.target);
        }
        callAfter(stmt);
    }

    void visit(const ConstructorDecl &stmt) override
    {
        callBefore(stmt);
        if (callShouldVisit(stmt))
        {
            walker::detail::notifyChildRange(*this, stmt, stmt.params);
            walker::detail::visitChildRange(*this, stmt, stmt.body);
        }
        callAfter(stmt);
    }

    void visit(const DestructorDecl &stmt) override
    {
        callBefore(stmt);
        if (callShouldVisit(stmt))
        {
            walker::detail::visitChildRange(*this, stmt, stmt.body);
        }
        callAfter(stmt);
    }

    void visit(const MethodDecl &stmt) override
    {
        callBefore(stmt);
        if (callShouldVisit(stmt))
        {
            walker::detail::notifyChildRange(*this, stmt, stmt.params);
            walker::detail::visitChildRange(*this, stmt, stmt.body);
        }
        callAfter(stmt);
    }

    void visit(const ClassDecl &stmt) override
    {
        callBefore(stmt);
        if (callShouldVisit(stmt))
        {
            walker::detail::visitChildRange(*this, stmt, stmt.members);
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
            walker::detail::visitChildRange(*this, stmt, stmt.stmts);
        }
        callAfter(stmt);
    }
};

} // namespace il::frontends::basic
