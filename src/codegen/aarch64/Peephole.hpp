//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/aarch64/Peephole.hpp
// Purpose: Declare peephole optimizations over AArch64 Machine IR.
//
// Key invariants:
// - Rewrites preserve instruction ordering and semantics.
// - Only pattern substitutions (e.g., redundant moves, identity ops) are
//   applied without changing control flow.
// - Must be called after register allocation when physical registers are known.
//
// Ownership/Lifetime:
// - Operates on mutable Machine IR owned by the caller.
// - No dynamic resources are allocated beyond temporary vectors.
//
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include "MachineIR.hpp"

namespace viper::codegen::aarch64
{

/// @brief Statistics from peephole optimization pass.
struct PeepholeStats
{
    int identityMovesRemoved{0};    ///< Number of `mov r, r` instructions removed.
    int identityFMovesRemoved{0};   ///< Number of `fmov d, d` instructions removed.
    int consecutiveMovsFolded{0};   ///< Number of consecutive moves folded.
    int deadInstructionsRemoved{0}; ///< Number of dead instructions removed.

    /// @brief Total number of optimizations applied.
    [[nodiscard]] int total() const noexcept
    {
        return identityMovesRemoved + identityFMovesRemoved + consecutiveMovsFolded +
               deadInstructionsRemoved;
    }
};

/// @brief Run conservative Machine IR peepholes for the AArch64 backend.
///
/// This pass applies local peephole optimizations that are safe to perform
/// after register allocation. It targets common patterns that arise from
/// lowering and register allocation such as identity moves and redundant
/// register-to-register copies.
///
/// @param fn Machine function to optimize (modified in place).
/// @return Statistics about optimizations applied.
[[nodiscard]] PeepholeStats runPeephole(MFunction &fn);

} // namespace viper::codegen::aarch64
