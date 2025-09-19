// File: src/support/diag_expected.hpp
// Purpose: Provides diagnostic aliases and helpers built on std::expected.
// Key invariants: Diagnostics encapsulate a single severity, message, and location.
// Ownership/Lifetime: Aliases do not own data; diagnostics own their message buffers.
// Links: docs/class-catalog.md
#pragma once

#include "support/diagnostics.hpp"
#include "support/source_manager.hpp"

#include <expected>
#include <ostream>
#include <string>
#include <utility>

using Diag = il::support::Diagnostic;

template <class T> using Expected = std::expected<T, Diag>;

namespace il::support
{
using ::Diag;

template <class T> using Expected = ::Expected<T>;
} // namespace il::support

namespace il::support::detail
{
/// @brief Convert diagnostic severity to lowercase string.
inline const char *diagSeverityToString(il::support::Severity severity)
{
    switch (severity)
    {
        case il::support::Severity::Note:
            return "note";
        case il::support::Severity::Warning:
            return "warning";
        case il::support::Severity::Error:
            return "error";
    }
    return "";
}
} // namespace il::support::detail

/// @brief Create an error diagnostic with location and message.
/// @param loc Optional source location associated with the diagnostic.
/// @param msg Human-readable diagnostic message.
/// @return Diagnostic marked as an error severity.
inline Diag makeError(il::support::SourceLoc loc, std::string msg)
{
    return Diag{il::support::Severity::Error, std::move(msg), loc};
}

/// @brief Print a single diagnostic to the provided stream.
/// @param diag Diagnostic to format.
/// @param os Output stream receiving the text.
/// @param sm Optional source manager to resolve file paths.
/// @note Follows DiagnosticEngine::printAll formatting for consistency.
inline void printDiag(const Diag &diag, std::ostream &os,
                      const il::support::SourceManager *sm = nullptr)
{
    if (diag.loc.isValid() && sm)
    {
        auto path = sm->getPath(diag.loc.file_id);
        os << path << ":" << diag.loc.line << ":" << diag.loc.column << ": ";
    }
    os << il::support::detail::diagSeverityToString(diag.severity) << ": " << diag.message
       << '\n';
}
