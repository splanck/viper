// File: src/support/diag_capture.cpp
// License: MIT License (c) 2024 The Viper Project Authors. See LICENSE in the
//          project root for details.
// Purpose: Implement the diagnostic capture bridge that converts legacy
//          text-based error flows into structured diagnostics.
// Ownership/Lifetime: `DiagCapture` owns its buffered stream and reuses it
//                     across conversions.
// Links: docs/contributor-guide.md, docs/codemap.md#support

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
/// @details Delegates to `printDiag()` so formatting logic remains centralized
///          in a single routine.  The capture's internal buffer is not
///          mutated, allowing tooling to reprint the same diagnostic multiple
///          times (for example, to stderr and to a log file) without reformatting.
///
/// @param out Destination stream that receives the formatted diagnostic text.
/// @param diag Diagnostic instance to serialize.
void DiagCapture::printTo(std::ostream &out, const Diag &diag)
{
    printDiag(diag, out);
}

/// @brief Convert the captured message into a Diagnostic value.
///
/// @details The capture accumulates text in its stringstream as callers insert
///          messages.  This method packages the resulting string into an error
///          diagnostic and returns it by value so the caller can propagate it
///          using the `Expected<void>` infrastructure.  The internal buffer
///          remains intact, allowing the capture to continue gathering messages
///          for later conversions.
///
/// @return Diagnostic containing a copy of the captured text.
Diag DiagCapture::toDiag() const
{
    return makeError({}, ss.str());
}

/// @brief Bridge a boolean success flag to an Expected<void> diagnostic result.
/// @details Older APIs return a boolean to signal success.  This helper wraps
///          that value by returning a default-constructed Expected on success or
///          by converting the capture's buffered diagnostic into an error
///          payload on failure.
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
