//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// This file declares the ScopeTracker class, which provides lexical scope
// tracking and symbol resolution for the BASIC frontend.
//
// Lexical Scoping in BASIC:
// While classic BASIC had limited scoping (mostly global variables), modern
// BASIC supports:
// - Global scope: Variables declared outside any procedure
// - Procedure scope: Local variables and parameters within SUB/FUNCTION
// - Block scope: Variables in FOR loops and other control structures
//
// The ScopeTracker manages these scopes and tracks variable declarations
// across the program.
//
// Key Responsibilities:
// - Scope management: Maintains a stack of active scopes (global, procedure,
//   block) during semantic analysis
// - Symbol registration: Records variable declarations in the appropriate scope
// - Name resolution: Looks up variable references, searching from innermost
//   to outermost scope
// - Shadowing detection: Reports warnings when local variables shadow global
//   or outer scope variables
// - Lifetime tracking: Determines variable storage duration (global, local,
//   temporary)
//
// Scope Stack:
// Scopes form a stack during semantic analysis:
//   [Global Scope]
//     [Procedure Scope: MySub]
//       [Block Scope: FOR loop]
//         [Block Scope: IF statement]
//
// When resolving a variable reference, the tracker searches from the innermost
// (most recent) scope outward to the global scope.
//
// RAII Scope Guards:
// The ScopeTracker provides ScopedScope objects for automatic scope management:
//   {
//     auto scope = scopeTracker.pushScope();
//     // Variables declared here are in the new scope
//   } // Scope automatically popped when ScopedScope destructs
//
// Name Mangling:
// The tracker integrates with the NameMangler to generate unique IL names for:
// - Local variables (scoped to their procedure)
// - Global variables (visible throughout the module)
// - Temporary values (scoped to their expression)
//
// Integration:
// - Owned by: SemanticAnalyzer
// - Used during: Symbol declaration and reference validation
// - No AST ownership: Only tracks symbol metadata
//
// Design Notes:
// - Scopes form a stack; resolving searches innermost to outermost
// - RAII scope guards ensure proper scope stack maintenance
// - Variable names are stored in canonical form for case-insensitive lookup
//
//===----------------------------------------------------------------------===//
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
