// File: src/support/diag_expected.hpp
// Purpose: Provides diagnostic-aware std::expected alias and helpers.
// Key invariants: Diagnostics contain severity and optional source location.
// Ownership/Lifetime: Diagnostics are value types without shared ownership.
// Links: docs/class-catalog.md
#pragma once

#include "support/diagnostics.hpp"
#include "support/source_manager.hpp"

#include <expected>
#include <ostream>
#include <string>
#include <utility>

namespace il
{
/// @brief Alias for support diagnostic type for broader use.
using Diag = il::support::Diagnostic;

/// @brief Convenience alias for std::expected with Diagnostic errors.
template <class T> using Expected = std::expected<T, Diag>;

/// @brief Create an error diagnostic with @p loc and @p msg.
/// @param loc Source location associated with the diagnostic.
/// @param msg Message describing the error.
/// @return Diagnostic with severity set to Error.
inline Diag makeError(il::support::SourceLoc loc, std::string msg)
{
    return Diag{il::support::Severity::Error, std::move(msg), loc};
}

/// @brief Convert severity enum to lowercase string.
/// @param severity Severity value to stringify.
/// @return Lowercase string matching diagnostic severity.
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

/// @brief Print @p diag using diagnostic formatting rules.
/// @param diag Diagnostic to print.
/// @param os Output stream receiving the formatted message.
/// @param sm Optional source manager for resolving file paths.
inline void printDiag(const Diag &diag, std::ostream &os,
                      const il::support::SourceManager *sm = nullptr)
{
    if (diag.loc.isValid() && sm)
    {
        auto path = sm->getPath(diag.loc.file_id);
        os << path << ":" << diag.loc.line << ":" << diag.loc.column << ": ";
    }
    os << diagSeverityToString(diag.severity) << ": " << diag.message << '\n';
}
} // namespace il
