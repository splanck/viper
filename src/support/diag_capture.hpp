//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: support/diag_capture.hpp
// Purpose: Provide a capture-only diagnostic sink to bridge legacy bool plus ostream APIs.
// Key invariants: Diagnostics recorded in the capture are printed verbatim and converted into error
// Ownership/Lifetime: To be documented.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

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
    void printTo(std::ostream &out, const Diag &diag);

    /// @brief Convert the captured text into an error diagnostic without a location.
    /// @return Diagnostic containing the captured message.
    [[nodiscard]] Diag toDiag() const;
};

/// @brief Helper bridging legacy bool-returning APIs to Expected<void>.
/// @param ok Result flag reported by the legacy call.
/// @param capture Diagnostic capture containing any emitted message.
/// @return Empty Expected on success; diagnostic payload on failure.
Expected<void> capture_to_expected_impl(bool ok, DiagCapture &capture);

/// @brief Adapt a legacy bool plus ostream diagnostic API to Expected<void>.
/// @tparam F Callable type invoked with an std::ostream& to perform the legacy work.
/// @param legacyCall Callable that returns true on success and writes diagnostics on failure.
/// @return Empty Expected on success; diagnostic payload converted from captured text on failure.
///
/// This function must remain inline because it is a template instantiated with
/// arbitrary functors across translation units; moving it to a source file
/// would break those instantiations.
template <class F> inline Expected<void> capture_to_expected(F &&legacyCall)
{
    DiagCapture capture;
    bool ok = std::forward<F>(legacyCall)(capture.ss);
    return capture_to_expected_impl(ok, capture);
}

} // namespace il::support
