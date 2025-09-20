// File: src/frontends/basic/SemanticDiagnostics.cpp
// Purpose: Implements semantic diagnostic helpers for BASIC front-end analysis.
// Key invariants: Diagnostics are forwarded without altering DiagnosticEmitter state.
// Ownership/Lifetime: Borrows DiagnosticEmitter references; no ownership transfer.
// Links: docs/class-catalog.md

#include "frontends/basic/SemanticDiagnostics.hpp"

#include <string>
#include <utility>

namespace il::frontends::basic
{

SemanticDiagnostics::SemanticDiagnostics(DiagnosticEmitter &emitter) : emitter_(emitter) {}

void SemanticDiagnostics::emit(il::support::Severity sev,
                               std::string code,
                               il::support::SourceLoc loc,
                               uint32_t length,
                               std::string message)
{
    emitter_.emit(sev, std::move(code), loc, length, std::move(message));
}

size_t SemanticDiagnostics::errorCount() const
{
    return emitter_.errorCount();
}

size_t SemanticDiagnostics::warningCount() const
{
    return emitter_.warningCount();
}

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

DiagnosticEmitter &SemanticDiagnostics::emitter()
{
    return emitter_;
}

} // namespace il::frontends::basic
