//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the diagnostic helper used by BASIC semantic analysis to emit
// errors and warnings.  The wrapper centralises common formatting routines and
// exposes convenience functions for frequent error patterns.
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/SemanticDiagnostics.hpp"

#include <string>
#include <utility>

namespace il::frontends::basic
{

/// @brief Construct a diagnostic helper that forwards to @p emitter.
SemanticDiagnostics::SemanticDiagnostics(DiagnosticEmitter &emitter) : emitter_(emitter) {}

/// @brief Emit a diagnostic with the provided severity, code, and location.
void SemanticDiagnostics::emit(il::support::Severity sev,
                               std::string code,
                               il::support::SourceLoc loc,
                               uint32_t length,
                               std::string message)
{
    emitter_.emit(sev, std::move(code), loc, length, std::move(message));
}

/// @brief Report the number of errors recorded so far.
size_t SemanticDiagnostics::errorCount() const
{
    return emitter_.errorCount();
}

/// @brief Report the number of warnings recorded so far.
size_t SemanticDiagnostics::warningCount() const
{
    return emitter_.warningCount();
}

/// @brief Format a diagnostic for expressions used in boolean contexts.
///
/// BASIC requires conditional expressions to evaluate to numeric types that
/// behave like booleans.  This helper substitutes the relevant type and source
/// text into a pre-defined template.
///
/// @param typeName Name of the offending type.
/// @param exprText Textual representation of the expression.
/// @return Fully formatted diagnostic message.
std::string SemanticDiagnostics::formatNonBooleanCondition(std::string_view typeName,
                                                           std::string_view exprText)
{
    std::string message(NonBooleanConditionMessage);
    const auto replace = [](std::string &subject,
                            std::string_view placeholder,
                            std::string_view value) {
        if (auto pos = subject.find(placeholder); pos != std::string::npos)
        {
            subject.replace(pos, placeholder.size(), value);
        }
    };
    replace(message, "{type}", typeName);
    replace(message, "{expr}", exprText);
    return message;
}

/// @brief Emit a diagnostic for non-boolean conditional expressions.
///
/// Wraps @ref formatNonBooleanCondition and forwards the resulting message to
/// @ref emit with @ref il::support::Severity::Error.
///
/// @param code     Diagnostic identifier.
/// @param loc      Source location of the expression.
/// @param length   Length of the expression in characters.
/// @param typeName Name of the offending type.
/// @param exprText Textual representation of the expression.
void SemanticDiagnostics::emitNonBooleanCondition(std::string code,
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

/// @brief Expose the underlying diagnostic emitter used for reporting.
DiagnosticEmitter &SemanticDiagnostics::emitter()
{
    return emitter_;
}

} // namespace il::frontends::basic
