// File: src/frontends/basic/SemanticAnalyzer.h
// Purpose: Declares BASIC semantic analyzer for symbol and label tracking,
//          basic validation, and rudimentary type checking.
// Key invariants: Analyzer tracks defined symbols and reports unknown
//                 references.
// Ownership/Lifetime: Analyzer borrows DiagnosticEmitter; no AST ownership.
// Links: docs/class-catalog.md
#pragma once
#include "frontends/basic/AST.h"
#include "frontends/basic/DiagnosticEmitter.h"
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace il::frontends::basic {

/// @brief Traverses BASIC AST to collect symbols and labels, validate variable
///        references, and verify FOR/NEXT nesting.
/// @invariant Symbol table only contains definitions; unknown uses report
///            diagnostics.
/// @ownership Borrows DiagnosticEmitter; AST not owned.
class SemanticAnalyzer {
public:
  /// @brief Create analyzer reporting to @p de.
  explicit SemanticAnalyzer(DiagnosticEmitter &de) : de(de) {}

  /// @brief Analyze @p prog collecting symbols and labels.
  /// @param prog Program AST to walk.
  void analyze(const Program &prog);

  /// @brief Collected variable names defined in the program.
  const std::unordered_set<std::string> &symbols() const { return symbols_; }

  /// @brief Line numbers present in the program.
  const std::unordered_set<int> &labels() const { return labels_; }

  /// @brief GOTO targets referenced in the program.
  const std::unordered_set<int> &labelRefs() const { return labelRefs_; }

private:
  /// @brief Record symbols and labels from a statement.
  /// @param s Statement node to analyze.
  void visitStmt(const Stmt &s);

  /// @brief Inferred BASIC value type.
  enum class Type { Int, String, Unknown };

  /// @brief Validate variable references in @p e and recurse into subtrees.
  /// @param e Expression node to analyze.
  /// @return Inferred type of the expression.
  Type visitExpr(const Expr &e);

  DiagnosticEmitter &de; ///< Diagnostic sink.
  std::unordered_set<std::string> symbols_;
  std::unordered_set<std::string> arrays_;
  std::unordered_map<std::string, int> arraySizes_;
  std::unordered_map<std::string, Type> varTypes_;
  std::unordered_set<int> labels_;
  std::unordered_set<int> labelRefs_;
  std::vector<std::string> forStack_; ///< Active FOR loop variables.
};

} // namespace il::frontends::basic
