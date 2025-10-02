// File: src/il/verify/EhVerifier.hpp
// Purpose: Declares the verifier pass responsible for checking EH stack balance.
// Key invariants: Each function is inspected independently; analysis relies on
// control-flow reachability to ensure eh.push/eh.pop pairs nest properly.
// Ownership/Lifetime: The pass borrows module references without taking
// ownership. Diagnostics are reported through the provided sink or Expected.
// Links: docs/il-guide.md#reference
#pragma once

#include "il/verify/DiagSink.hpp"

#include "support/diag_expected.hpp"

namespace il::core
{
struct Module;
}

namespace il::verify
{

/// @brief Verifier pass that ensures eh.push/eh.pop usage remains balanced.
class EhVerifier
{
  public:
    /// @brief Analyse all functions within @p module for balanced EH stacks.
    /// @param module Module whose functions are analysed.
    /// @param sink Diagnostic sink receiving auxiliary diagnostics.
    /// @return Success or the first diagnostic describing an imbalance.
    il::support::Expected<void> run(const il::core::Module &module, DiagSink &sink) const;
};

} // namespace il::verify
