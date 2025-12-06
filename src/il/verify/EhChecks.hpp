//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
// File: src/il/verify/EhChecks.hpp
//
// Purpose:
//   Declare reusable predicates and invariants that operate on the canonical
//   EhModel. These checks capture EH structural constraints shared across
//   verifier components.
//
// EH Verification Checks:
//   This module provides four complementary checks that together ensure
//   well-formed exception handling in IL programs:
//
//   1. checkEhStackBalance - Validates eh.push/eh.pop balance across all paths
//   2. checkDominanceOfHandlers - Ensures handlers dominate protected blocks
//   3. checkUnreachableHandlers - Detects dead handler code
//   4. checkResumeEdges - Validates resume.label postdominance requirements
//
// Testing:
//   All checks are comprehensively tested in test_il_verify_eh_checks.cpp with
//   both passing and failing test cases for each invariant.
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
[[nodiscard]] il::support::Expected<void> checkEhStackBalance(const EhModel &model);

/// @brief Validate that exception handlers dominate the blocks they protect.
///
/// Handler Dominance Invariant:
///   When an `eh.push ^handler` instruction installs a handler, the basic block
///   containing that eh.push must dominate all basic blocks that could potentially
///   fault while under the handler's protection. This ensures structured exception
///   handling: a handler cannot be installed for code paths that may have already
///   executed, which would create non-deterministic exception dispatch.
///
/// The check builds a forward dominator tree and computes handler coverage (which
/// blocks are protected by which handlers). For each handler, it verifies that the
/// block containing the eh.push instruction dominates every block in its coverage set.
///
/// @param model Canonical EH model describing the function.
/// @return Success when all eh.push blocks dominate their protected blocks; diagnostic otherwise.
[[nodiscard]] il::support::Expected<void> checkDominanceOfHandlers(const EhModel &model);

/// @brief Validate that all exception handler blocks are reachable.
///
/// Handler Reachability Invariant:
///   Every handler block referenced by an `eh.push ^handler` instruction must be
///   reachable from the function entry. Reachability is determined by considering
///   both normal CFG edges (branches) and exception edges (trap → handler).
///   Unreachable handlers indicate dead code that could never execute.
///
/// The check identifies handler blocks by scanning for eh.push instructions, then
/// performs a CFG traversal from entry tracking the EH stack to determine which
/// handlers can be reached via trap instructions.
///
/// @param model Canonical EH model describing the function.
/// @return Success when all handlers are reachable; diagnostic listing unreachable handlers
/// otherwise.
[[nodiscard]] il::support::Expected<void> checkUnreachableHandlers(const EhModel &model);

/// @brief Validate resume.label edges against handler coverage information.
/// @param model Canonical EH model describing the function.
/// @return Success when all resume targets are valid; diagnostic otherwise.
[[nodiscard]] il::support::Expected<void> checkResumeEdges(const EhModel &model);

} // namespace il::verify
