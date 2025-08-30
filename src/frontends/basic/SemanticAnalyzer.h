// File: src/frontends/basic/SemanticAnalyzer.h
// Purpose: Declares BASIC semantic analyzer for symbol and label tracking.
// Key invariants: Analyzer only records symbols and labels; no validation yet.
// Ownership/Lifetime: Analyzer borrows DiagnosticEngine; no AST ownership.
// Links: docs/class-catalog.md
#pragma once
#include "frontends/basic/AST.h"
#include "support/diagnostics.h"
#include <string>
#include <unordered_set>

namespace il::frontends::basic {

/// @brief Traverses BASIC AST to collect symbols and labels.
/// @invariant Does not emit diagnostics for now.
/// @ownership Borrows DiagnosticEngine; AST not owned.
class SemanticAnalyzer {
public:
  /// @brief Create analyzer reporting to @p de.
  explicit SemanticAnalyzer(il::support::DiagnosticEngine &de) : de(de) {}

  /// @brief Analyze @p prog collecting symbols and labels.
  /// @param prog Program AST to walk.
  void analyze(const Program &prog);

  /// @brief Collected variable names.
  const std::unordered_set<std::string> &symbols() const { return symbols_; }

  /// @brief Line numbers present in the program.
  const std::unordered_set<int> &labels() const { return labels_; }

  /// @brief GOTO targets referenced in the program.
  const std::unordered_set<int> &labelRefs() const { return labelRefs_; }

private:
  void visitStmt(const Stmt &s);
  void visitExpr(const Expr &e);

  il::support::DiagnosticEngine &de; ///< Diagnostic sink (unused for now).
  std::unordered_set<std::string> symbols_;
  std::unordered_set<int> labels_;
  std::unordered_set<int> labelRefs_;
};

} // namespace il::frontends::basic

