// File: src/support/diag_capture.hpp
// Purpose: Provide a capture-only diagnostic sink to bridge legacy bool plus ostream APIs.
// Key invariants: Diagnostics recorded in the capture are printed verbatim and converted into error diagnostics on demand.
// Ownership/Lifetime: DiagCapture owns its string buffer; returned diagnostics copy the captured message text.
// Links: docs/class-catalog.md
#pragma once

#include "support/diag_expected.hpp"

#include <ostream>
#include <sstream>
#include <utility>

namespace il::support
{
/// @brief Sink that accumulates diagnostic text for later conversion.
struct DiagCapture
{
    /// @brief Accumulated diagnostic text from legacy helpers.
    std::ostringstream ss;

    /// @brief Forward a diagnostic to an output stream using standard formatting.
    /// @param out Stream receiving the formatted diagnostic.
    /// @param diag Diagnostic to print.
    void printTo(std::ostream &out, const Diag &diag)
    {
        printDiag(diag, out);
    }

    /// @brief Convert the captured text into an error diagnostic without a location.
    /// @return Diagnostic containing the captured message.
    [[nodiscard]] Diag toDiag() const
    {
        return makeError({}, ss.str());
    }
};

/// @brief Adapt a legacy bool plus ostream diagnostic API to Expected<void>.
/// @tparam F Callable type invoked with an std::ostream& to perform the legacy work.
/// @param legacyCall Callable that returns true on success and writes diagnostics on failure.
/// @return Empty Expected on success; diagnostic payload converted from captured text on failure.
template <class F> inline Expected<void> capture_to_expected(F &&legacyCall)
{
    DiagCapture capture;
    bool ok = std::forward<F>(legacyCall)(capture.ss);
    if (ok)
    {
        return {};
    }
    return Expected<void>{capture.toDiag()};
}

} // namespace il::support
