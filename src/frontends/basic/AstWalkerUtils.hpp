// File: src/frontends/basic/AstWalkerUtils.hpp
// Purpose: Provides helper utilities shared by BASIC AST walker implementations.
// Key invariants: Utilities preserve traversal semantics defined by BasicAstWalker.
// Ownership/Lifetime: Helpers operate on borrowed AST nodes without taking ownership.
// Links: docs/codemap.md
#pragma once

#include "frontends/basic/ast/DeclNodes.hpp"
#include "frontends/basic/ast/StmtIO.hpp"

namespace il::frontends::basic
{
template <typename Derived> class BasicAstWalker;
}

namespace il::frontends::basic::walker
{
/// @brief Determine whether a PRINT item carries an expression to visit.
[[nodiscard]] bool printItemHasExpr(const PrintItem &item) noexcept;

namespace detail
{
template <typename Derived>
[[nodiscard]] inline Derived &asDerived(BasicAstWalker<Derived> &walker) noexcept
{
    return *static_cast<Derived *>(&walker);
}

template <typename Derived, typename Parent, typename Child>
inline void notifyChild(BasicAstWalker<Derived> &walker, const Parent &parent, const Child &child)
{
    walker.callBeforeChild(parent, child);
    walker.callAfterChild(parent, child);
}

template <typename Derived, typename Parent, typename Range>
inline void notifyChildRange(BasicAstWalker<Derived> &walker,
                             const Parent &parent,
                             const Range &range)
{
    for (const auto &child : range)
    {
        notifyChild(walker, parent, child);
    }
}

template <typename Derived, typename Parent, typename Ptr>
inline void visitOptionalChild(BasicAstWalker<Derived> &walker,
                               const Parent &parent,
                               const Ptr &childPtr)
{
    if (!childPtr)
        return;
    walker.callBeforeChild(parent, *childPtr);
    childPtr->accept(asDerived(walker));
    walker.callAfterChild(parent, *childPtr);
}

template <typename Derived, typename Parent, typename Range>
inline void visitChildRange(BasicAstWalker<Derived> &walker,
                            const Parent &parent,
                            const Range &range)
{
    for (const auto &child : range)
    {
        if (!child)
            continue;
        visitOptionalChild(walker, parent, child);
    }
}

template <typename Derived>
inline void visitPrintItem(BasicAstWalker<Derived> &walker,
                           const PrintStmt &stmt,
                           const PrintItem &item)
{
    if (!printItemHasExpr(item))
        return;
    visitOptionalChild(walker, stmt, item.expr);
}

template <typename Derived>
inline void visitPrintItems(BasicAstWalker<Derived> &walker, const PrintStmt &stmt)
{
    for (const auto &item : stmt.items)
    {
        visitPrintItem(walker, stmt, item);
    }
}
} // namespace detail

} // namespace il::frontends::basic::walker
