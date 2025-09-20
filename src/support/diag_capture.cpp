// File: src/support/diag_capture.cpp
// Purpose: Provide out-of-line definitions for DiagCapture helper methods.
// Key invariants: The stored string stream contents are preserved when converting
//                 to a Diagnostic; print operations delegate to printDiag.
// Ownership/Lifetime: DiagCapture owns its string buffer; diagnostics copy the text.
// Links: docs/class-catalog.md

#include "support/diag_capture.hpp"

namespace il::support
{
void DiagCapture::printTo(std::ostream &out, const Diag &diag)
{
    printDiag(diag, out);
}

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
