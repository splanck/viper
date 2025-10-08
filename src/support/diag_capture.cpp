//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Provides the out-of-line helpers for the DiagCapture utility, which captures
// diagnostics in string form and later materializes them as structured Diag
// instances.  The helpers bridge legacy APIs that expect boolean success flags
// with the modern Expected<void> diagnostics used by the rest of the library.
//
//===----------------------------------------------------------------------===//

#include "support/diag_capture.hpp"

namespace il::support
{
/// @brief Write the given diagnostic to the supplied output stream.
///
/// Delegates to printDiag() so that formatting logic remains centralized.
/// Emitting a diagnostic does not mutate the capture buffer, allowing the same
/// capture to be reused when printing multiple times.
///
/// @param out Destination stream that receives the formatted diagnostic text.
/// @param diag Diagnostic instance to serialize.
void DiagCapture::printTo(std::ostream &out, const Diag &diag)
{
    printDiag(diag, out);
}

/// @brief Convert the captured message into a Diagnostic value.
///
/// The capture accumulates text in its stringstream as callers insert messages.
/// This method packages the resulting string into an error diagnostic so the
/// caller can propagate it using Expected<void> infrastructure.
///
/// @return Diagnostic containing a copy of the captured text.
Diag DiagCapture::toDiag() const
{
    return makeError({}, ss.str());
}

/// @brief Bridge a boolean success flag to an Expected<void> diagnostic result.
///
/// Older APIs return a boolean to signal success.  This helper wraps that value
/// by returning a default-constructed Expected on success or by converting the
/// capture's buffered diagnostic into an error payload on failure.
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
