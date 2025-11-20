//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the diagnostic sink helpers shared across verifier components.
// These helpers convert verifier-specific diagnostic codes into user-facing
// strings and provide a simple sink that collects diagnostics for later
// inspection.
//
//===----------------------------------------------------------------------===//
//
/// @file
/// @brief Diagnostic sinks and helpers for the IL verifier.
/// @details The functions here translate enumerated diagnostic codes into
///          textual prefixes, build @ref il::support::Diag instances tagged with
///          the verifier namespace, and provide a basic sink implementation that
///          retains diagnostics in arrival order.

#include "il/verify/DiagSink.hpp"

#include <utility>

namespace
{
/// @brief Map a verifier diagnostic code to its string prefix.
/// @details The verifier exposes a small catalogue of diagnostic codes that are
///          grouped by subsystem.  This helper translates the enumerator into a
///          stable string used as part of the user-facing diagnostic identifier
///          (for example "verify.eh.underflow").
std::string_view diagCodeToPrefix(il::verify::VerifyDiagCode code)
{
    using il::verify::VerifyDiagCode;
    switch (code)
    {
        case VerifyDiagCode::Unknown:
            return {};
        case VerifyDiagCode::EhStackUnderflow:
            return "verify.eh.underflow";
        case VerifyDiagCode::EhStackLeak:
            return "verify.eh.unreleased";
        case VerifyDiagCode::EhResumeTokenMissing:
            return "verify.eh.resume_token_missing";
        case VerifyDiagCode::EhResumeLabelInvalidTarget:
            return "verify.eh.resume_label_target";
    }
    return {};
}
} // namespace

namespace il::verify
{

/// @brief Convert a diagnostic code into its string representation.
/// @details Thin wrapper that forwards to @ref diagCodeToPrefix so external
///          callers do not need to reach into the anonymous namespace when they
///          only care about the string form of the code.
std::string_view toString(VerifyDiagCode code)
{
    return diagCodeToPrefix(code);
}

/// @brief Construct a diagnostic value tagged with the verifier namespace.
/// @details Prepends the derived prefix (when available) to the supplied message
///          and packages the result into an @ref il::support::Diag at the given
///          severity and source location.  The helper ensures consistent
///          formatting across all verifier emission sites.
il::support::Diag makeVerifierDiag(VerifyDiagCode code,
                                   il::support::Severity severity,
                                   il::support::SourceLoc loc,
                                   std::string message)
{
    const std::string_view prefix = diagCodeToPrefix(code);
    if (!prefix.empty())
    {
        if (!message.empty())
        {
            message.insert(0, ": ");
            message.insert(0, prefix);
        }
        else
        {
            message.assign(prefix);
        }
    }
    return {severity, std::move(message), loc};
}

/// @brief Convenience wrapper that always reports an error severity.
/// @details Calls @ref makeVerifierDiag with @ref il::support::Severity::Error,
///          saving callers from repeating the severity constant at each call
///          site.
il::support::Diag makeVerifierError(VerifyDiagCode code,
                                    il::support::SourceLoc loc,
                                    std::string message)
{
    return makeVerifierDiag(code, il::support::Severity::Error, loc, std::move(message));
}

/// @brief Append a diagnostic to the collection in arrival order.
/// @details The sink simply stores diagnostics in a vector, preserving the order
///          they were reported so clients can iterate deterministically.
void CollectingDiagSink::report(il::support::Diag diag)
{
    diags_.push_back(std::move(diag));
}

/// @brief Access the accumulated diagnostics without copying.
/// @details Returns a reference to the internal vector so callers can inspect or
///          iterate the collected diagnostics without incurring a copy.
const std::vector<il::support::Diag> &CollectingDiagSink::diagnostics() const
{
    return diags_;
}

/// @brief Clear all stored diagnostics.
/// @details Empties the backing container, allowing the same sink instance to be
///          reused across multiple verification runs.
void CollectingDiagSink::clear()
{
    diags_.clear();
}

} // namespace il::verify
