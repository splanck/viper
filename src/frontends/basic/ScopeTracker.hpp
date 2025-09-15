// File: src/frontends/basic/ScopeTracker.hpp
// Purpose: Provides lexical scope tracking with name mangling for the BASIC front end.
// Key invariants: Scopes form a stack; resolving searches innermost to outermost.
// Ownership/Lifetime: Owned by SemanticAnalyzer; no AST ownership.
// Links: docs/class-catalog.md
#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace il::frontends::basic
{

class ScopeTracker
{
  public:
    class ScopedScope
    {
      public:
        explicit ScopedScope(ScopeTracker &st) : st_(st)
        {
            st_.pushScope();
        }

        ~ScopedScope()
        {
            st_.popScope();
        }

      private:
        ScopeTracker &st_;
    };

    void reset()
    {
        stack_.clear();
        nextId_ = 0;
    }

    void pushScope()
    {
        stack_.emplace_back();
    }

    void popScope()
    {
        if (!stack_.empty())
            stack_.pop_back();
    }

    void bind(const std::string &name, const std::string &mapped)
    {
        if (!stack_.empty())
            stack_.back()[name] = mapped;
    }

    bool isDeclaredInCurrentScope(const std::string &name) const
    {
        return !stack_.empty() && stack_.back().count(name);
    }

    std::string declareLocal(const std::string &name)
    {
        std::string unique = name + "_" + std::to_string(nextId_++);
        if (!stack_.empty())
            stack_.back()[name] = unique;
        return unique;
    }

    std::optional<std::string> resolve(const std::string &name) const
    {
        for (auto it = stack_.rbegin(); it != stack_.rend(); ++it)
        {
            auto found = it->find(name);
            if (found != it->end())
                return found->second;
        }
        return std::nullopt;
    }

    bool hasScope() const
    {
        return !stack_.empty();
    }

  private:
    std::vector<std::unordered_map<std::string, std::string>> stack_;
    unsigned nextId_{0};
};

} // namespace il::frontends::basic
