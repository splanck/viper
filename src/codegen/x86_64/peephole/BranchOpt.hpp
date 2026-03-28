//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/codegen/x86_64/peephole/BranchOpt.hpp
// Purpose: Declarations for branch optimizations: greedy trace block layout,
//          cold block reordering, branch chain elimination, conditional branch
//          inversion, and fallthrough jump removal.
//
// Key invariants:
//   - Branch rewrites preserve control-flow semantics.
//   - Entry block always stays first in the layout.
//   - Block reordering only moves cold blocks (trap/error handlers) to the end.
//
// Ownership/Lifetime:
//   - Operates on mutable MFunction/instruction vectors owned by the caller.
//
// Links: src/codegen/x86_64/Peephole.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "../MachineIR.hpp"
#include "PeepholeCommon.hpp"

#include <cstddef>

namespace viper::codegen::x64::peephole {

/// @brief Greedy trace block layout — follow JMP targets to maximize fall-through.
void traceBlockLayout(MFunction &fn, PeepholeStats &stats);

/// @brief Move cold blocks (trap/error handlers) to end of function.
void moveColdBlocks(MFunction &fn, PeepholeStats &stats);

/// @brief Eliminate branch chains: retarget JMP/JCC through single-JMP blocks.
void eliminateBranchChains(MFunction &fn, PeepholeStats &stats);

/// @brief Invert conditional branches to eliminate trailing unconditional jumps.
void invertConditionalBranches(MFunction &fn, PeepholeStats &stats);

/// @brief Remove jumps to the immediately following block.
void removeFallthroughJumps(MFunction &fn, PeepholeStats &stats);

} // namespace viper::codegen::x64::peephole
