// File: src/support/diag_expected.hpp
// Purpose: Provide shared diagnostic aliases and helpers using std::expected.
// Key invariants: Diagnostic severity strings mirror DiagnosticEngine output.
// Ownership/Lifetime: Utilities operate on borrowed diagnostics and streams.
// Links: docs/class-catalog.md
#pragma once

#include "diagnostics.hpp"
#include "source_manager.hpp"

#include <expected>
#include <ostream>
#include <string>
#include <utility>

namespace il
{

/// @brief Alias for diagnostics leveraged across helper utilities.
using Diag = il::support::Diagnostic;

/// @brief Convenience alias for std::expected with the project diagnostic type.
/// @tparam T Value stored on success.
template <class T>
using Expected = std::expected<T, Diag>;

namespace detail
{
/// @brief Convert severity enum to lowercase string literal.
/// @param severity Severity value to stringify.
/// @return String literal representing the severity.
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
} // namespace detail

/// @brief Create an error diagnostic with provided location and message.
/// @param loc Optional source location metadata.
/// @param msg Human-readable diagnostic message.
/// @return Diagnostic tagged with error severity.
inline Diag makeError(il::support::SourceLoc loc, std::string msg)
{
    return Diag{il::support::Severity::Error, std::move(msg), loc};
}

/// @brief Print a single diagnostic using canonical formatting.
/// @param diag Diagnostic to render.
/// @param os Output stream receiving the formatted diagnostic.
/// @param sm Optional source manager to resolve file paths.
inline void printDiag(const Diag &diag,
                      std::ostream &os,
                      const il::support::SourceManager *sm = nullptr)
{
    if (diag.loc.isValid() && sm != nullptr)
    {
        auto path = sm->getPath(diag.loc.file_id);
        os << path << ":" << diag.loc.line << ":" << diag.loc.column << ": ";
    }
    os << detail::diagSeverityToString(diag.severity) << ": " << diag.message << '\n';
}

} // namespace il

