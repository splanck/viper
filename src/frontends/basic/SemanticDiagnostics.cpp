// File: src/frontends/basic/SemanticDiagnostics.cpp
// Purpose: Implements semantic diagnostic helpers for BASIC front-end analysis.
// Key invariants: Diagnostics are forwarded without altering DiagnosticEmitter state.
// Ownership/Lifetime: Borrows DiagnosticEmitter references; no ownership transfer.
// Links: docs/class-catalog.md

#include "frontends/basic/SemanticDiagnostics.hpp"

#include <string>

namespace il::frontends::basic
{

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

} // namespace il::frontends::basic
