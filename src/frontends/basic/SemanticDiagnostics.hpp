// File: src/frontends/basic/SemanticDiagnostics.hpp
// Purpose: Wraps DiagnosticEmitter for semantic analysis utilities.
// Key invariants: Forwards diagnostics without altering counts.
// Ownership/Lifetime: Borrows DiagnosticEmitter; no ownership of sources.
// Links: docs/codemap.md
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
