// File: src/il/verify/DiagSink.cpp
// Purpose: Implements diagnostic sink utilities used by verifier components.
// Key invariants: CollectingDiagSink appends diagnostics in the order received.
// Ownership/Lifetime: CollectingDiagSink stores diagnostics until cleared.
// Links: docs/il-guide.md#reference

#include "il/verify/DiagSink.hpp"

#include <utility>

namespace il::verify
{

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
