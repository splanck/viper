// File: src/support/diag_expected.hpp
// Purpose: Provide diagnostic-aware std::expected aliases and helpers.
// Key invariants: None.
// Ownership/Lifetime: Diagnostics are returned by value.
// Links: docs/class-catalog.md
#pragma once

#include "diagnostics.hpp"
#include "source_manager.hpp"

#include <expected>
#include <ostream>
#include <string>
#include <utility>

namespace il::support
{
namespace detail
{
inline const char *toString(Severity severity)
{
    switch (severity)
    {
        case Severity::Note:
            return "note";
        case Severity::Warning:
            return "warning";
        case Severity::Error:
            return "error";
    }
    return "";
}
} // namespace detail

/// @brief Alias representing a single diagnostic entry.
using Diag = Diagnostic;

/// @brief Convenience alias for std::expected using diagnostics for errors.
/// @tparam T Value type stored on success.
template <class T>
using Expected = std::expected<T, Diag>;

/// @brief Construct an error diagnostic with message @p msg at @p loc.
/// @param loc Source location associated with the error.
/// @param msg Human-readable description of the problem.
/// @return Diagnostic describing an error.
inline Diag makeError(SourceLoc loc, std::string msg)
{
    return Diag{Severity::Error, std::move(msg), loc};
}

/// @brief Print diagnostic @p diag using the standard formatting rules.
/// @param diag Diagnostic to print.
/// @param os Output stream receiving the text.
/// @param sm Optional source manager for resolving file paths.
inline void printDiag(const Diag &diag, std::ostream &os, const SourceManager *sm = nullptr)
{
    if (diag.loc.isValid() && sm)
    {
        auto path = sm->getPath(diag.loc.file_id);
        os << path << ":" << diag.loc.line << ":" << diag.loc.column << ": ";
    }
    os << detail::toString(diag.severity) << ": " << diag.message << '\n';
}
} // namespace il::support
