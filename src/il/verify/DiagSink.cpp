// File: src/il/verify/DiagSink.cpp
// Purpose: Implements diagnostic sink utilities used by verifier components.
// Key invariants: CollectingDiagSink appends diagnostics in the order received.
// Ownership/Lifetime: CollectingDiagSink stores diagnostics until cleared.
// Links: docs/il-guide.md#reference

#include "il/verify/DiagSink.hpp"

#include <utility>

namespace
{
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

std::string_view toString(VerifyDiagCode code)
{
    return diagCodeToPrefix(code);
}

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

il::support::Diag makeVerifierError(VerifyDiagCode code,
                                    il::support::SourceLoc loc,
                                    std::string message)
{
    return makeVerifierDiag(code, il::support::Severity::Error, loc, std::move(message));
}

void CollectingDiagSink::report(il::support::Diag diag)
{
    // Store diagnostics in arrival order for later inspection.
    diags_.push_back(std::move(diag));
}

const std::vector<il::support::Diag> &CollectingDiagSink::diagnostics() const
{
    // Expose immutable access to the stored diagnostics without copying.
    return diags_;
}

void CollectingDiagSink::clear()
{
    // Remove all stored diagnostics to reset the sink state.
    diags_.clear();
}

} // namespace il::verify
