//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
// File: src/il/verify/EhChecks.hpp
//
// Purpose:
//   Declare reusable predicates and invariants that operate on the canonical
//   EhModel. These checks capture stack balance and resume-edge constraints
//   shared across verifier components.
//
// Key invariants:
//   * Diagnostics mirror the wording emitted by the legacy EH verifier.
//   * Callers construct an EhModel and supply it to the desired predicates.
//
// Ownership/Lifetime:
//   Functions observe the EhModel and never take ownership of IR resources.
//
// Links:
//   docs/il-guide.md#reference
//
//===----------------------------------------------------------------------===//

#pragma once

#include "il/verify/EhModel.hpp"

#include "support/diag_expected.hpp"

namespace il::verify
{

/// @brief Validate that eh.push/eh.pop instructions remain balanced.
/// @param model Canonical EH model describing the function.
/// @return Success when balanced; diagnostic otherwise.
il::support::Expected<void> checkEhStackBalance(const EhModel &model);

/// @brief Placeholder for handler dominance validation (not yet implemented).
/// @param model Canonical EH model describing the function.
/// @return Always success; reserved for future diagnostics.
il::support::Expected<void> checkDominanceOfHandlers(const EhModel &model);

/// @brief Placeholder ensuring all handlers are reachable.
/// @param model Canonical EH model describing the function.
/// @return Always success; reserved for future diagnostics.
il::support::Expected<void> checkUnreachableHandlers(const EhModel &model);

/// @brief Validate resume.label edges against handler coverage information.
/// @param model Canonical EH model describing the function.
/// @return Success when all resume targets are valid; diagnostic otherwise.
il::support::Expected<void> checkResumeEdges(const EhModel &model);

} // namespace il::verify
