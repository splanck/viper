//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: il/verify/EhVerifier.hpp
// Purpose: Validates exception handling stack balance in IL functions.
//          Ensures eh.push/eh.pop instructions maintain balanced stack
//          discipline on all control-flow paths, detects underflows and leaks,
//          and verifies resume.* instructions occur within active handlers.
// Key invariants:
//   - Every eh.push must have a corresponding eh.pop on all paths.
//   - No handler stack leaks at function exit.
//   - resume.* requires an active resume token.
// Ownership/Lifetime: EhVerifier is a stateless class; run() borrows the
//          module and diagnostic sink for the duration of the call.
// Links: il/verify/DiagSink.hpp, support/diag_expected.hpp
//
//===----------------------------------------------------------------------===//

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
    [[nodiscard]] il::support::Expected<void> run(const il::core::Module &module,
                                                  DiagSink &sink) const;
};

} // namespace il::verify
