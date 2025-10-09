//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements helper routines for emitting semantic diagnostics during BASIC
// analysis.  The thin wrapper centralises message formatting utilities and
// forwards all emissions to a shared DiagnosticEmitter instance.
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/SemanticDiagnostics.hpp"

#include <string>
#include <utility>

namespace il::frontends::basic
{

/// @brief Construct the helper that forwards diagnostics to @p emitter.
SemanticDiagnostics::SemanticDiagnostics(DiagnosticEmitter &emitter) : emitter_(emitter) {}

/// @brief Emit a diagnostic by delegating to the shared emitter.
void SemanticDiagnostics::emit(il::support::Severity sev,
                               std::string code,
                               il::support::SourceLoc loc,
                               uint32_t length,
                               std::string message)
{
    emitter_.emit(sev, std::move(code), loc, length, std::move(message));
}

/// @brief Retrieve the number of error diagnostics recorded so far.
size_t SemanticDiagnostics::errorCount() const
{
    return emitter_.errorCount();
}

/// @brief Retrieve the number of warning diagnostics recorded so far.
size_t SemanticDiagnostics::warningCount() const
{
    return emitter_.warningCount();
}

/// @brief Produce a formatted error message for a non-boolean conditional expression.
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

/// @brief Emit a diagnostic indicating that a conditional expression was not boolean.
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

/// @brief Access the underlying diagnostic emitter.
DiagnosticEmitter &SemanticDiagnostics::emitter()
{
    return emitter_;
}

} // namespace il::frontends::basic
