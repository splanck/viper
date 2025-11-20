//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/support/diag_capture.cpp
// Purpose: Implement the deferred-diagnostic capture used by legacy-style APIs.
// Key invariants: Captured diagnostics are stored as plain text until converted
//                 back into @ref Diag objects; repeated conversions leave the
//                 buffered message intact so callers can print diagnostics
//                 multiple times.
// Ownership/Lifetime: @ref DiagCapture owns its stringstream buffer and borrows
//                     no external resources.  The bridging helper returns
//                     @ref Expected<void> values that outlive the capture object.
// Links: src/support/diag_capture.hpp, docs/codemap.md#support-library
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements the deferred-diagnostic sink used by text-only pipelines.
/// @details `il::support::DiagCapture` buffers formatted diagnostics in a
///          stringstream so subsystems that historically returned `bool`
///          success codes can surface richer error information.  These helpers
///          provide the bridge points that turn the buffered text back into the
///          structured `Diag` objects expected by the modern `Expected<void>`
///          workflow.

#include "support/diag_capture.hpp"

namespace il::support
{
/// @brief Write the given diagnostic to the supplied output stream.
///
/// @details Simply forwards to @ref printDiag so all formatting (severity
///          strings, source location prefixes, newline handling) remains
///          centralised.  The capture's internal buffer is intentionally left
///          untouched so that emitting to multiple streams—stderr, logs, etc.—
///          is inexpensive and deterministic.
///
/// @param out Destination stream that receives the formatted diagnostic text.
/// @param diag Diagnostic instance to serialize.
void DiagCapture::printTo(std::ostream &out, const Diag &diag)
{
    printDiag(diag, out);
}

/// @brief Convert the captured message into a Diagnostic value.
///
/// @details Packages the buffered string into a @ref Diag with error severity
///          using @ref makeError.  Because the stringstream remains untouched,
///          subsequent calls continue to observe the same captured payload—this
///          is important for callers that convert the message into both an error
///          return and a log entry.
///
/// @return Diagnostic containing a copy of the captured text.
Diag DiagCapture::toDiag() const
{
    return makeError({}, ss.str());
}

/// @brief Bridge a boolean success flag to an Expected<void> diagnostic result.
///
/// @details Normalises legacy APIs that report success with a boolean.  When
///          @p ok is @c true the function returns a default-constructed (success)
///          @ref Expected<void>.  Otherwise it invokes @ref DiagCapture::toDiag
///          to convert buffered text into an error payload.  The capture remains
///          unchanged so callers can still print or rewrap the diagnostic after
///          propagating the failure.
///
/// @param ok Boolean indicating whether the preceding operation succeeded.
/// @param capture Capture containing any error text produced by the operation.
/// @return Successful Expected when @p ok is true; otherwise an error payload.
Expected<void> capture_to_expected_impl(bool ok, DiagCapture &capture)
{
    if (ok)
    {
        return Expected<void>{};
    }
    return Expected<void>{capture.toDiag()};
}
} // namespace il::support
