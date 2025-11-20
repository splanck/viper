//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// This file declares the SemanticDiagnostics class, which wraps
// DiagnosticEmitter to provide semantic-analysis-specific diagnostic utilities.
//
// The SemanticDiagnostics wrapper provides a typed interface for emitting
// diagnostics during semantic analysis, with specialized methods for common
// semantic error scenarios.
//
// Key Responsibilities:
// - Type-safe diagnostic emission for semantic errors
// - Specialized methods for common semantic validation failures:
//   * Undefined symbols (variables, procedures, labels)
//   * Type mismatches in expressions and assignments
//   * Invalid procedure calls (wrong argument count/types)
//   * Scope violations (accessing out-of-scope variables)
//   * Control flow errors (NEXT without FOR, etc.)
// - Consistent error code assignment for programmatic error handling
//
// Diagnostic Categories:
// - Symbol errors: Undefined, duplicate, or misused symbols
// - Type errors: Incompatible types in operations or assignments
// - Control flow errors: Invalid nesting or missing control flow terminators
// - Procedure errors: Invalid calls, parameter mismatches, missing return
//
// Integration:
// - Used by: SemanticAnalyzer and related semantic checking utilities
// - Wraps: DiagnosticEmitter for actual message emission
// - Borrows: DiagnosticEmitter; does not take ownership
//
// Design Notes:
// - Forwards diagnostics without altering counts
// - Provides type-safe, semantic-specific diagnostic methods
// - No ownership of source text or diagnostic engine
//
//===----------------------------------------------------------------------===//
#pragma once

#include "frontends/basic/DiagnosticEmitter.hpp"
#include "viper/diag/BasicDiag.hpp"

#include <initializer_list>
#include <string>
#include <string_view>

namespace il::frontends::basic
{

class SemanticDiagnostics
{
  public:
    explicit SemanticDiagnostics(DiagnosticEmitter &emitter);

    /// @brief Message template for non-boolean conditions.
    static constexpr std::string_view NonBooleanConditionMessage =
        "Expected BOOLEAN condition, got {type}. Suggestion: use {expr} <> 0.";

    void emit(il::support::Severity sev,
              std::string code,
              il::support::SourceLoc loc,
              uint32_t length,
              std::string message);

    void emit(diag::BasicDiag diag,
              il::support::SourceLoc loc,
              uint32_t length,
              std::initializer_list<diag::Replacement> replacements = {});

    [[nodiscard]] size_t errorCount() const;

    [[nodiscard]] size_t warningCount() const;

    /// @brief Format the NonBooleanCondition diagnostic message.
    /// @param typeName Describes the inferred BASIC type.
    /// @param exprText Expression text used for suggestions.
    /// @return Message with placeholders substituted.
    [[nodiscard]] static std::string formatNonBooleanCondition(std::string_view typeName,
                                                               std::string_view exprText);

    /// @brief Emit the NonBooleanCondition diagnostic with formatted message.
    /// @param code Diagnostic code to emit (e.g., E1001).
    /// @param loc Source location associated with the condition.
    /// @param length Number of characters to underline.
    /// @param typeName BASIC type name for the offending expression.
    /// @param exprText Textual representation of the expression.
    void emitNonBooleanCondition(std::string code,
                                 il::support::SourceLoc loc,
                                 uint32_t length,
                                 std::string_view typeName,
                                 std::string_view exprText);

    [[nodiscard]] DiagnosticEmitter &emitter();

  private:
    DiagnosticEmitter &emitter_;
};

} // namespace il::frontends::basic
