// File: src/frontends/basic/AstWalkerUtils.hpp
// Purpose: Declares helper utilities for BASIC AST walker hooks.
// Key invariants: Utilities never alter traversal order; they only bridge
//                 optional hooks and diagnostics.
// Ownership/Lifetime: Header-only templates operate on caller-provided
//                     walker instances.
// Links: docs/codemap.md
#pragma once

#include <string_view>

namespace il::frontends::basic
{
namespace walker_detail
{

enum class WalkerHook
{
    Before,
    After,
    ShouldVisit,
    BeforeChild,
    AfterChild,
};

void logHookInvocation(WalkerHook hook, std::string_view nodeType) noexcept;
void logChildHookInvocation(WalkerHook hook,
                            std::string_view parentType,
                            std::string_view childType) noexcept;

inline constexpr bool kWalkerLoggingEnabled = false;

template <typename Derived, typename Node>
void dispatchBefore(Derived &derived, const Node &node)
{
    if constexpr (requires(Derived &d, const Node &n) { d.before(n); })
    {
        if constexpr (kWalkerLoggingEnabled)
            logHookInvocation(WalkerHook::Before, {});
        derived.before(node);
    }
}

template <typename Derived, typename Node>
void dispatchAfter(Derived &derived, const Node &node)
{
    if constexpr (requires(Derived &d, const Node &n) { d.after(n); })
    {
        if constexpr (kWalkerLoggingEnabled)
            logHookInvocation(WalkerHook::After, {});
        derived.after(node);
    }
}

template <typename Derived, typename Node>
bool dispatchShouldVisit(Derived &derived, const Node &node)
{
    if constexpr (requires(Derived &d, const Node &n) { d.shouldVisitChildren(n); })
    {
        if constexpr (kWalkerLoggingEnabled)
            logHookInvocation(WalkerHook::ShouldVisit, {});
        return derived.shouldVisitChildren(node);
    }
    return true;
}

template <typename Derived, typename Parent, typename Child>
void dispatchBeforeChild(Derived &derived, const Parent &parent, const Child &child)
{
    if constexpr (requires(Derived &d, const Parent &p, const Child &c) { d.beforeChild(p, c); })
    {
        if constexpr (kWalkerLoggingEnabled)
            logChildHookInvocation(WalkerHook::BeforeChild, {}, {});
        derived.beforeChild(parent, child);
    }
}

template <typename Derived, typename Parent, typename Child>
void dispatchAfterChild(Derived &derived, const Parent &parent, const Child &child)
{
    if constexpr (requires(Derived &d, const Parent &p, const Child &c) { d.afterChild(p, c); })
    {
        if constexpr (kWalkerLoggingEnabled)
            logChildHookInvocation(WalkerHook::AfterChild, {}, {});
        derived.afterChild(parent, child);
    }
}

} // namespace walker_detail

} // namespace il::frontends::basic

