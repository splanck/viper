//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// This file declares the EhVerifier class, which validates exception handling
// stack balance in IL functions. The IL exception handling model uses explicit
// stack operations (eh.push/eh.pop) to manage handler registration, requiring
// careful verification to ensure correct nesting and balance.
//
// The IL specification requires that exception handlers are registered and
// unregistered in a balanced stack discipline. Each eh.push instruction must
// have a corresponding eh.pop on all control flow paths. The EhVerifier performs
// control-flow analysis to ensure that handler stacks remain balanced throughout
// function execution, with no underflows (pop without matching push) or leaks
// (handlers active at function exit).
//
// Key Responsibilities:
// - Verify eh.push/eh.pop instructions maintain stack balance on all paths
// - Detect stack underflows (pop without active handler)
// - Detect stack leaks (handlers still active at function return)
// - Validate resume.* instructions occur within active exception handlers
// - Ensure handler blocks are reachable and properly structured
//
// Design Notes:
// The EhVerifier operates function-by-function, constructing an EhModel for each
// function to analyze its exception handling structure. It delegates to specialized
// checks (checkEhStackBalance, checkResumeEdges) that work on the canonical model.
// The verifier reports errors through the diagnostic sink or Expected return values,
// maintaining separation from the underlying analysis algorithms.
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
    il::support::Expected<void> run(const il::core::Module &module, DiagSink &sink) const;
};

} // namespace il::verify
