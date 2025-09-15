// File: src/frontends/basic/SemanticAnalyzer.hpp
// Purpose: Orchestrates BASIC semantic passes for symbol collection and type checking.
// Key invariants: Delegates work to SymbolCollector and TypeChecker passes.
// Ownership/Lifetime: Borrows DiagnosticEmitter; AST nodes owned externally.
// Links: docs/class-catalog.md
#pragma once

#include "frontends/basic/AST.hpp"
#include "frontends/basic/DiagnosticEmitter.hpp"
#include "frontends/basic/semantic/SymbolCollector.hpp"
#include "frontends/basic/semantic/TypeChecker.hpp"
#include <unordered_set>

namespace il::frontends::basic
{

/// @brief Runs symbol collection then type checking over BASIC AST.
/// @invariant Results mirror those from individual passes.
/// @ownership Borrows DiagnosticEmitter; does not own AST.
class SemanticAnalyzer
{
  public:
    explicit SemanticAnalyzer(DiagnosticEmitter &de);

    /// @brief Run semantic analysis over @p prog.
    void analyze(const Program &prog);

    /// @brief Collected variable names defined in the program.
    const std::unordered_set<std::string> &symbols() const { return symbols_; }
    /// @brief Line numbers present in the program.
    const std::unordered_set<int> &labels() const { return labels_; }
    /// @brief GOTO targets referenced in the program.
    const std::unordered_set<int> &labelRefs() const { return labelRefs_; }
    /// @brief Registered procedures and their signatures (unused placeholder).
    const ProcTable &procs() const { return procs_; }

  private:
    DiagnosticEmitter &de_;
    semantic::SymbolCollector collector_;
    TypeChecker checker_;
    std::unordered_set<std::string> symbols_;
    std::unordered_set<int> labels_;
    std::unordered_set<int> labelRefs_;
    ProcTable procs_;
};

} // namespace il::frontends::basic

