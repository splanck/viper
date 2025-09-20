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
        explicit ScopedScope(ScopeTracker &st);

        ~ScopedScope();

      private:
        ScopeTracker &st_;
    };

    void reset();

    void pushScope();

    void popScope();

    void bind(const std::string &name, const std::string &mapped);

    bool isDeclaredInCurrentScope(const std::string &name) const;

    std::string declareLocal(const std::string &name);

    std::optional<std::string> resolve(const std::string &name) const;

    bool hasScope() const;

  private:
    std::vector<std::unordered_map<std::string, std::string>> stack_;
    unsigned nextId_{0};
};

} // namespace il::frontends::basic
