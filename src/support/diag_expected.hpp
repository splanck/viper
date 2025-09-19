// File: src/support/diag_expected.hpp
// Purpose: Provides diagnostic aliases integrated with std::expected.
// Key invariants: Diagnostics preserve severity and location metadata.
// Ownership/Lifetime: Aliases reference il::support diagnostics; functions create value semantics.
// Links: docs/class-catalog.md
#pragma once

#include "diagnostics.hpp"

#include <expected>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>

namespace il
{
namespace detail
{
/// @brief Convert diagnostic severity to lowercase text.
/// @param severity Severity value to describe.
/// @return String view with textual name.
constexpr std::string_view severityToString(il::support::Severity severity)
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

/// @brief Alias exposing the project diagnostic type at the il namespace level.
using Diag = il::support::Diagnostic;

/// @brief Convenience alias for std::expected returning diagnostics on failure.
/// @tparam T Value type held on success.
template <class T> using Expected = std::expected<T, Diag>;

/// @brief Construct an error diagnostic with the provided location and message.
/// @param loc Source location where the error occurred.
/// @param msg Error message text.
/// @return Diagnostic marked with Severity::Error.
inline Diag makeError(il::support::SourceLoc loc, std::string msg)
{
    return Diag{il::support::Severity::Error, std::move(msg), loc};
}

/// @brief Print a single diagnostic using standard formatting.
/// @param diag Diagnostic to render.
/// @param os Output stream receiving the formatted text.
/// @param sm Optional source manager resolving file identifiers.
inline void printDiag(const Diag &diag,
                      std::ostream &os,
                      const il::support::SourceManager *sm = nullptr)
{
    if (diag.loc.isValid() && sm)
    {
        os << sm->getPath(diag.loc.file_id) << ":" << diag.loc.line << ":" << diag.loc.column << ": ";
    }
    os << detail::severityToString(diag.severity) << ": " << diag.message << '\n';
}
} // namespace il

