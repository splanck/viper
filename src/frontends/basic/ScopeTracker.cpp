// File: src/frontends/basic/ScopeTracker.cpp
// License: MIT License. See LICENSE in the project root for full license information.
// Purpose: Implements lexical scope tracking with name mangling for the BASIC front end.
// Key invariants: Scopes form a stack; resolving searches innermost to outermost.
// Ownership/Lifetime: Owned by SemanticAnalyzer; no AST ownership.
// Links: docs/codemap.md

#include "frontends/basic/ScopeTracker.hpp"

namespace il::frontends::basic
{

/// @brief Enter a new lexical scope and automatically pop it on destruction.
ScopeTracker::ScopedScope::ScopedScope(ScopeTracker &st) : st_(st)
{
    st_.pushScope();
}

/// @brief Pop the active scope when the guard is destroyed.
ScopeTracker::ScopedScope::~ScopedScope()
{
    st_.popScope();
}

/// @brief Reset the tracker to an empty stack and identifier counter.
void ScopeTracker::reset()
{
    stack_.clear();
    nextId_ = 0;
}

/// @brief Introduce a new empty scope on the stack.
void ScopeTracker::pushScope()
{
    stack_.emplace_back();
}

/// @brief Remove the innermost scope when present.
void ScopeTracker::popScope()
{
    if (!stack_.empty())
        stack_.pop_back();
}

/// @brief Bind @p name to @p mapped in the current scope.
void ScopeTracker::bind(const std::string &name, const std::string &mapped)
{
    if (!stack_.empty())
        stack_.back()[name] = mapped;
}

/// @brief Determine whether @p name already exists in the innermost scope.
///
/// @param name Identifier to query.
/// @return True when the name is bound in the current scope.
bool ScopeTracker::isDeclaredInCurrentScope(const std::string &name) const
{
    return !stack_.empty() && stack_.back().count(name);
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
        stack_.back()[name] = unique;
    return unique;
}

/// @brief Resolve an identifier by searching from innermost to outermost scope.
///
/// @param name Identifier to resolve.
/// @return Mangled name when found; otherwise std::nullopt.
std::optional<std::string> ScopeTracker::resolve(const std::string &name) const
{
    for (auto it = stack_.rbegin(); it != stack_.rend(); ++it)
    {
        auto found = it->find(name);
        if (found != it->end())
            return found->second;
    }
    return std::nullopt;
}

/// @brief Report whether any scope is currently active.
bool ScopeTracker::hasScope() const
{
    return !stack_.empty();
}

} // namespace il::frontends::basic
