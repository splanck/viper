// File: src/support/diag_capture.cpp
// Purpose: Provide out-of-line definitions for DiagCapture helper methods.
// Key invariants: The stored string stream contents are preserved when converting
//                 to a Diagnostic; print operations delegate to printDiag.
// Ownership/Lifetime: DiagCapture owns its string buffer; diagnostics copy the text.
// License: MIT License (see LICENSE).
// Links: docs/codemap.md

#include "support/diag_capture.hpp"

namespace il::support
{
/// @brief Write the given diagnostic to the supplied stream.
/// @param out Destination stream that receives the formatted diagnostic text.
/// @param diag Diagnostic instance to serialize.
/// @note Writes to @p out but does not mutate the capture state.
void DiagCapture::printTo(std::ostream &out, const Diag &diag)
{
    printDiag(diag, out);
}

/// @brief Convert the captured message into a Diagnostic value.
/// @return Diagnostic containing a copy of the captured text.
Diag DiagCapture::toDiag() const
{
    return makeError({}, ss.str());
}

/// @brief Convert the legacy call result into an Expected<void> diagnostic outcome.
Expected<void> capture_to_expected_impl(bool ok, DiagCapture &capture)
{
    if (ok)
    {
        return Expected<void>{};
    }
    return Expected<void>{capture.toDiag()};
}
} // namespace il::support
