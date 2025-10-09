// File: src/il/verify/DiagSink.cpp
// License: MIT License. See LICENSE in the project root for full license information.
// Purpose: Implements diagnostic sink utilities used by verifier components.
// Key invariants: CollectingDiagSink appends diagnostics in the order received.
// Ownership/Lifetime: CollectingDiagSink stores diagnostics until cleared.
// Links: docs/il-guide.md#reference

#include "il/verify/DiagSink.hpp"

#include <utility>

namespace
{
/// @brief Map a verifier diagnostic code to its string prefix.
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
std::string_view toString(VerifyDiagCode code)
{
    return diagCodeToPrefix(code);
}

/// @brief Construct a diagnostic value tagged with the verifier namespace.
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
il::support::Diag makeVerifierError(VerifyDiagCode code,
                                    il::support::SourceLoc loc,
                                    std::string message)
{
    return makeVerifierDiag(code, il::support::Severity::Error, loc, std::move(message));
}

/// @brief Append a diagnostic to the collection in arrival order.
void CollectingDiagSink::report(il::support::Diag diag)
{
    diags_.push_back(std::move(diag));
}

/// @brief Access the accumulated diagnostics without copying.
const std::vector<il::support::Diag> &CollectingDiagSink::diagnostics() const
{
    return diags_;
}

/// @brief Clear all stored diagnostics.
void CollectingDiagSink::clear()
{
    diags_.clear();
}

} // namespace il::verify
