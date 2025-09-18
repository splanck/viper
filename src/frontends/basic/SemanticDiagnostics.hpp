// File: src/frontends/basic/SemanticDiagnostics.hpp
// Purpose: Wraps DiagnosticEmitter for semantic analysis utilities.
// Key invariants: Forwards diagnostics without altering counts.
// Ownership/Lifetime: Borrows DiagnosticEmitter; no ownership of sources.
// Links: docs/class-catalog.md
#pragma once

#include "frontends/basic/DiagnosticEmitter.hpp"
#include <string_view>
#include <utility>

namespace il::frontends::basic
{

class SemanticDiagnostics
{
  public:
    explicit SemanticDiagnostics(DiagnosticEmitter &emitter) : emitter_(emitter) {}

    /// @brief Message template for non-boolean conditions.
    static constexpr std::string_view NonBooleanConditionMessage =
        "Expected BOOLEAN condition, got {type}. Suggestion: use {expr} <> 0.";

    void emit(il::support::Severity sev,
              std::string code,
              il::support::SourceLoc loc,
              uint32_t length,
              std::string message)
    {
        emitter_.emit(sev, std::move(code), loc, length, std::move(message));
    }

    size_t errorCount() const
    {
        return emitter_.errorCount();
    }

    size_t warningCount() const
    {
        return emitter_.warningCount();
    }

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

    DiagnosticEmitter &emitter()
    {
        return emitter_;
    }

  private:
    DiagnosticEmitter &emitter_;
};

} // namespace il::frontends::basic

inline std::string
il::frontends::basic::SemanticDiagnostics::formatNonBooleanCondition(std::string_view typeName,
                                                                     std::string_view exprText)
{
    std::string message(NonBooleanConditionMessage);
    const auto replace = [](std::string &subject,
                            std::string_view placeholder,
                            std::string_view value) {
        if (auto pos = subject.find(placeholder); pos != std::string::npos)
            subject.replace(pos, placeholder.size(), value);
    };
    replace(message, "{type}", typeName);
    replace(message, "{expr}", exprText);
    return message;
}

inline void il::frontends::basic::SemanticDiagnostics::emitNonBooleanCondition(
    std::string code,
    il::support::SourceLoc loc,
    uint32_t length,
    std::string_view typeName,
    std::string_view exprText)
{
    emit(il::support::Severity::Error,
         std::move(code),
         loc,
         length,
         formatNonBooleanCondition(typeName, exprText));
}
