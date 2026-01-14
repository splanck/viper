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
    int cmpZeroToTst{0};            ///< Number of `cmp r, #0` → `tst r, r` transforms.
    int arithmeticIdentities{0};    ///< Number of identity arithmetic ops removed.
    int strengthReductions{0};      ///< Number of mul→shift strength reductions.
    int branchesToNextRemoved{0};   ///< Number of branches to next block removed.
    int blocksReordered{0};          ///< Number of blocks reordered for layout.
    int copiesPropagated{0};         ///< Number of copy propagations applied.

    /// @brief Total number of optimizations applied.
    [[nodiscard]] int total() const noexcept
    {
        return identityMovesRemoved + identityFMovesRemoved + consecutiveMovsFolded +
               deadInstructionsRemoved + cmpZeroToTst + arithmeticIdentities +
               strengthReductions + branchesToNextRemoved + blocksReordered +
               copiesPropagated;
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
