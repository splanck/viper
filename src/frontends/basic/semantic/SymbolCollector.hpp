// File: src/frontends/basic/semantic/SymbolCollector.hpp
// Purpose: Collect variable symbols and labels from BASIC AST.
// Key invariants: Only definitions are recorded; GOTO targets tracked separately.
// Ownership/Lifetime: Borrows DiagnosticEmitter; AST nodes owned externally.
// Links: docs/class-catalog.md
#pragma once

#include "frontends/basic/AST.hpp"
#include "frontends/basic/DiagnosticEmitter.hpp"
#include <unordered_set>

namespace il::frontends::basic::semantic
{

/// @brief Traverses AST to collect variable names and labels.
/// @invariant Only definitions are recorded; unknown uses are handled by type checker.
/// @ownership Borrows DiagnosticEmitter; does not own AST.
class SymbolCollector
{
  public:
    /// @brief Create collector emitting diagnostics to @p de.
    explicit SymbolCollector(DiagnosticEmitter &de) : de_(de) {}

    /// @brief Collect symbols and labels from program @p prog.
    void collect(const Program &prog);

    /// @brief Collected variable names.
    const std::unordered_set<std::string> &symbols() const { return symbols_; }

    /// @brief Line numbers present in program.
    const std::unordered_set<int> &labels() const { return labels_; }

    /// @brief Referenced GOTO targets.
    const std::unordered_set<int> &labelRefs() const { return labelRefs_; }

  private:
    void visitStmt(const Stmt &s);

    DiagnosticEmitter &de_;
    std::unordered_set<std::string> symbols_;
    std::unordered_set<int> labels_;
    std::unordered_set<int> labelRefs_;
};

} // namespace il::frontends::basic::semantic

