// MIT License. See LICENSE in the project root for full license information.
// File: src/support/diag_capture.cpp
// Purpose: Provide out-of-line definitions for DiagCapture helper methods.
// Key invariants: The stored string stream contents are preserved when converting
//                 to a Diagnostic; print operations delegate to printDiag.
// Ownership/Lifetime: DiagCapture owns its string buffer; diagnostics copy the text.
// Links: docs/codemap.md

#include "support/diag_capture.hpp"

namespace il::support
{
/// Print the diagnostic object to the provided output stream.
///
/// @param out Stream that receives the formatted diagnostic text.
/// @param diag Diagnostic value to serialize.
void DiagCapture::printTo(std::ostream &out, const Diag &diag)
{
    printDiag(diag, out);
}

/// Translate the captured buffer into a Diagnostic instance.
///
/// @return A Diagnostic populated with the captured message and an empty location.
Diag DiagCapture::toDiag() const
{
    return makeError({}, ss.str());
}

/// Convert the legacy boolean status into an Expected<void> diagnostic outcome.
///
/// @param ok Indicates whether the original operation succeeded.
/// @param capture Diagnostic capture used to materialize errors when @p ok is false.
/// @return An empty Expected when @p ok is true; otherwise contains the captured diagnostic.
Expected<void> capture_to_expected_impl(bool ok, DiagCapture &capture)
{
    if (ok)
    {
        return Expected<void>{};
    }
    return Expected<void>{capture.toDiag()};
}
} // namespace il::support
