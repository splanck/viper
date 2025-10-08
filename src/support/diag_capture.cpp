/**
 * @file diag_capture.cpp
 * @brief Implements helpers for capturing formatted diagnostics in memory.
 * @copyright
 *     MIT License. See the LICENSE file in the project root for full terms.
 * @details
 *     `DiagCapture` accumulates message text in a string stream so that callers
 *     can convert legacy interfaces into structured diagnostics without losing
 *     the formatted output.
 */

#include "support/diag_capture.hpp"

namespace il::support
{
/**
 * @brief Writes a diagnostic to the caller-supplied output stream.
 *
 * The helper forwards to the shared `printDiag` routine to ensure consistent
 * formatting.  Because no internal state changes, captures can be reprinted
 * multiple times.
 *
 * @param out Destination stream for the diagnostic text.
 * @param diag Diagnostic value to serialize.
 */
void DiagCapture::printTo(std::ostream &out, const Diag &diag)
{
    printDiag(diag, out);
}

/**
 * @brief Converts the captured buffer into an error diagnostic.
 *
 * The method constructs a `Diag` with severity `Error` and no source location,
 * copying the accumulated string stream contents into the diagnostic message.
 *
 * @return Newly created diagnostic containing the captured text.
 */
Diag DiagCapture::toDiag() const
{
    return makeError({}, ss.str());
}

/**
 * @brief Bridges legacy boolean success codes to `Expected<void>` diagnostics.
 *
 * When `ok` is `true`, the function returns a default-constructed successful
 * `Expected<void>`.  Otherwise it materializes the captured diagnostic to
 * describe the failure and returns it as an error payload.
 *
 * @param ok Legacy success flag.
 * @param capture Diagnostic capture holding the formatted message.
 * @return Success or failure expressed as `Expected<void>`.
 */
Expected<void> capture_to_expected_impl(bool ok, DiagCapture &capture)
{
    if (ok)
    {
        return Expected<void>{};
    }
    return Expected<void>{capture.toDiag()};
}
} // namespace il::support
