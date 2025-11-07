//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the scope-tracking utility used by the BASIC semantic analyser to
// resolve symbols and generate unique mangled identifiers.  The tracker maintains
// a stack of hash tables keyed by source names and maps them to unique IR-level
// identifiers.
//
//===----------------------------------------------------------------------===//
//
/// @file
/// @brief Lexical scope tracking utilities for the BASIC front end.
/// @details Provides RAII helpers for entering/leaving scopes, routines for
///          declaring locals, and lookup helpers that search from innermost to
///          outermost scope while preserving mangled names.

#include "frontends/basic/ScopeTracker.hpp"
#include "frontends/basic/IdentifierUtils.hpp"

namespace il::frontends::basic
{

/// @brief Enter a new lexical scope and automatically pop it on destruction.
/// @details The guard pushes a fresh scope during construction and pops it when
///          leaving scope, making it easy to model nested blocks with
///          exception-safe semantics.
ScopeTracker::ScopedScope::ScopedScope(ScopeTracker &st) : st_(st)
{
    st_.pushScope();
}

/// @brief Pop the active scope when the guard is destroyed.
/// @details Ensures the scope pushed in the constructor is removed, restoring
///          the previous lookup environment.
ScopeTracker::ScopedScope::~ScopedScope()
{
    st_.popScope();
}

/// @brief Reset the tracker to an empty stack and identifier counter.
/// @details Clears all scope tables and resets the mangling counter so the
///          tracker can be reused for a new procedure.
void ScopeTracker::reset()
{
    stack_.clear();
    nextId_ = 0;
}

/// @brief Introduce a new empty scope on the stack.
/// @details Appends an empty hash map to the scope vector representing a deeper
///          lexical nesting level.
void ScopeTracker::pushScope()
{
    stack_.emplace_back();
}

/// @brief Remove the innermost scope when present.
/// @details If no scope exists the call is a no-op; otherwise the most recent
///          scope is removed to mirror exiting a block.
void ScopeTracker::popScope()
{
    if (!stack_.empty())
        stack_.pop_back();
}

/// @brief Bind @p name to @p mapped in the current scope.
/// @details Records the association in the innermost scope, overwriting any
///          existing binding for @p name within that scope.  Outer scopes remain
///          untouched.
void ScopeTracker::bind(const std::string &name, const std::string &mapped)
{
    if (!stack_.empty())
        stack_.back()[canonicalizeIdentifier(name)] = mapped;
}

/// @brief Determine whether @p name already exists in the innermost scope.
///
/// @param name Identifier to query.
/// @return True when the name is bound in the current scope.
bool ScopeTracker::isDeclaredInCurrentScope(const std::string &name) const
{
    if (stack_.empty())
        return false;
    return stack_.back().count(canonicalizeIdentifier(name)) != 0;
}

/// @brief Declare a new local symbol and generate a unique mangled identifier.
///
/// Appends an incrementing suffix to the original name and records the mapping
/// in the current scope.
///
/// @param name Original identifier.
/// @return Mangled name assigned to the declaration.
std::string ScopeTracker::declareLocal(const std::string &name)
{
    std::string unique = name + "_" + std::to_string(nextId_++);
    if (!stack_.empty())
        stack_.back()[canonicalizeIdentifier(name)] = unique;
    return unique;
}

/// @brief Resolve an identifier by searching from innermost to outermost scope.
///
/// @param name Identifier to resolve.
/// @return Mangled name when found; otherwise std::nullopt.
std::optional<std::string> ScopeTracker::resolve(const std::string &name) const
{
    std::string key = canonicalizeIdentifier(name);
    for (auto it = stack_.rbegin(); it != stack_.rend(); ++it)
    {
        auto found = it->find(key);
        if (found != it->end())
            return found->second;
    }
    return std::nullopt;
}

/// @brief Report whether any scope is currently active.
/// @details Useful for asserting that push/pop pairs are balanced during
///          compilation.
bool ScopeTracker::hasScope() const
{
    return !stack_.empty();
}

} // namespace il::frontends::basic
