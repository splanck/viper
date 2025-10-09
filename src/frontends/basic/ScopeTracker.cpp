//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the lexical scope tracker used by the BASIC semantic analyzer to
// manage mangled names for locals and resolve identifiers across nested scopes.
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/ScopeTracker.hpp"

namespace il::frontends::basic
{

/// @brief RAII helper that pushes a new scope on construction.
ScopeTracker::ScopedScope::ScopedScope(ScopeTracker &st) : st_(st)
{
    st_.pushScope();
}

/// @brief Pop the scope that was pushed by the constructor.
ScopeTracker::ScopedScope::~ScopedScope()
{
    st_.popScope();
}

/// @brief Reset the tracker to its initial empty state.
void ScopeTracker::reset()
{
    stack_.clear();
    nextId_ = 0;
}

/// @brief Push a new lexical scope onto the stack.
void ScopeTracker::pushScope()
{
    stack_.emplace_back();
}

/// @brief Pop the most recent lexical scope when one exists.
void ScopeTracker::popScope()
{
    if (!stack_.empty())
        stack_.pop_back();
}

/// @brief Bind a human-readable name to a mangled identifier in the current scope.
/// @param name Original variable name.
/// @param mapped Mangled representation that must remain unique within the scope chain.
void ScopeTracker::bind(const std::string &name, const std::string &mapped)
{
    if (!stack_.empty())
        stack_.back()[name] = mapped;
}

/// @brief Determine whether @p name is already declared in the innermost scope.
/// @param name Identifier to test.
/// @return True when the name resolves within the current scope.
bool ScopeTracker::isDeclaredInCurrentScope(const std::string &name) const
{
    return !stack_.empty() && stack_.back().count(name);
}

/// @brief Declare a new local symbol, generating a unique mangled identifier.
///
/// The identifier is produced by appending a monotonically increasing counter,
/// ensuring deterministic yet unique mangled names.
///
/// @param name User-facing name.
/// @return Mangled identifier suitable for IR emission.
std::string ScopeTracker::declareLocal(const std::string &name)
{
    std::string unique = name + "_" + std::to_string(nextId_++);
    if (!stack_.empty())
        stack_.back()[name] = unique;
    return unique;
}

/// @brief Resolve @p name by searching from the innermost scope outward.
/// @param name Identifier to resolve.
/// @return Mangled identifier when found; std::nullopt otherwise.
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

/// @brief Check whether any scopes are currently active.
/// @return True when the scope stack is non-empty.
bool ScopeTracker::hasScope() const
{
    return !stack_.empty();
}

} // namespace il::frontends::basic
