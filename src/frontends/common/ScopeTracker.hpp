//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/common/ScopeTracker.hpp
// Purpose: Lexical scope tracking and symbol resolution for language frontends.
//
// This class provides a stack-based scope tracking mechanism that maps source
// identifiers to unique mangled names. It supports:
// - Scope management: Push/pop scopes for blocks, procedures, etc.
// - Symbol registration: Bind names in the current scope
// - Name resolution: Look up names from innermost to outermost scope
// - RAII scope guards: Automatic scope management via ScopedScope
//
// The ScopeTracker is generic and can be used by any language frontend that
// needs lexical scoping with name mangling.
//
//===----------------------------------------------------------------------===//
#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace il::frontends::common
{

/// @brief Lexical scope tracker with name mangling support.
/// @details Manages a stack of scopes for symbol resolution. Each scope is a
///          map from source names to mangled IL names. Resolution searches
///          from innermost to outermost scope.
class ScopeTracker
{
  public:
    /// @brief RAII guard for automatic scope management.
    /// @details Pushes a scope on construction and pops it on destruction,
    ///          ensuring proper scope balancing even with exceptions.
    class ScopedScope
    {
      public:
        /// @brief Push a new scope.
        /// @param st The scope tracker to manage.
        explicit ScopedScope(ScopeTracker &st) : st_(st)
        {
            st_.pushScope();
        }

        /// @brief Pop the scope when the guard is destroyed.
        ~ScopedScope()
        {
            st_.popScope();
        }

        // Non-copyable, non-movable
        ScopedScope(const ScopedScope &) = delete;
        ScopedScope &operator=(const ScopedScope &) = delete;
        ScopedScope(ScopedScope &&) = delete;
        ScopedScope &operator=(ScopedScope &&) = delete;

      private:
        ScopeTracker &st_;
    };

    /// @brief Reset the tracker to an empty state.
    /// @details Clears all scopes and resets the ID counter.
    void reset()
    {
        stack_.clear();
        nextId_ = 0;
    }

    /// @brief Push a new empty scope onto the stack.
    void pushScope()
    {
        stack_.emplace_back();
    }

    /// @brief Pop the innermost scope if one exists.
    void popScope()
    {
        if (!stack_.empty())
            stack_.pop_back();
    }

    /// @brief Bind a name to a mangled identifier in the current scope.
    /// @param name Source identifier.
    /// @param mapped Mangled IL identifier.
    void bind(const std::string &name, const std::string &mapped)
    {
        if (!stack_.empty())
            stack_.back()[name] = mapped;
    }

    /// @brief Check if a name is declared in the current (innermost) scope.
    /// @param name Source identifier to check.
    /// @return True if the name exists in the current scope.
    [[nodiscard]] bool isDeclaredInCurrentScope(const std::string &name) const
    {
        return !stack_.empty() && stack_.back().contains(name);
    }

    /// @brief Declare a new local and generate a unique mangled name.
    /// @param name Source identifier.
    /// @return The generated unique mangled name.
    std::string declareLocal(const std::string &name)
    {
        std::string unique = name + "_" + std::to_string(nextId_++);
        if (!stack_.empty())
            stack_.back()[name] = unique;
        return unique;
    }

    /// @brief Declare a local with a specific mangled name.
    /// @param name Source identifier.
    /// @param mangledName The mangled name to use.
    void declareLocalAs(const std::string &name, const std::string &mangledName)
    {
        if (!stack_.empty())
            stack_.back()[name] = mangledName;
    }

    /// @brief Resolve a name by searching from innermost to outermost scope.
    /// @param name Source identifier to resolve.
    /// @return The mangled name if found, or std::nullopt.
    [[nodiscard]] std::optional<std::string> resolve(const std::string &name) const
    {
        for (auto it = stack_.rbegin(); it != stack_.rend(); ++it)
        {
            auto found = it->find(name);
            if (found != it->end())
                return found->second;
        }
        return std::nullopt;
    }

    /// @brief Check if any scope is currently active.
    /// @return True if the scope stack is non-empty.
    [[nodiscard]] bool hasScope() const
    {
        return !stack_.empty();
    }

    /// @brief Get the current scope depth.
    /// @return Number of scopes on the stack.
    [[nodiscard]] std::size_t depth() const
    {
        return stack_.size();
    }

    /// @brief Get the next unique ID without consuming it.
    /// @return Current value of the ID counter.
    [[nodiscard]] unsigned peekNextId() const
    {
        return nextId_;
    }

    /// @brief Consume and return the next unique ID.
    /// @return A unique ID that can be used for mangling.
    unsigned nextId()
    {
        return nextId_++;
    }

  private:
    std::vector<std::unordered_map<std::string, std::string>> stack_;
    unsigned nextId_{0};
};

} // namespace il::frontends::common
