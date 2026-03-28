//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/codegen/aarch64/peephole/CopyPropDCE.hpp
// Purpose: Declarations for copy propagation, dead code elimination, dead FP
//          store elimination, dead flag-setter removal, and compute-into-target
//          folding peephole sub-passes.
//
// Key invariants:
//   - Copy propagation does not propagate through ABI registers.
//   - DCE conservatively marks callee-saved and ABI registers as live at exit.
//
// Ownership/Lifetime:
//   - Operates on mutable instruction vectors owned by the caller.
//
// Links: src/codegen/aarch64/Peephole.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "../MachineIR.hpp"
#include "../Peephole.hpp"

#include <cstddef>
#include <vector>

namespace viper::codegen::aarch64::peephole {

/// @brief Perform copy propagation within a basic block.
std::size_t propagateCopies(std::vector<MInstr> &instrs, PeepholeStats &stats);

/// @brief Perform dead code elimination within a basic block.
std::size_t removeDeadInstructions(std::vector<MInstr> &instrs, PeepholeStats &stats);

/// @brief Eliminate dead stores to frame-pointer-relative offsets.
std::size_t eliminateDeadFpStores(std::vector<MInstr> &instrs, PeepholeStats &stats);

/// @brief Remove dead flag-setting instructions whose flags are clobbered.
std::size_t removeDeadFlagSetters(std::vector<MInstr> &instrs, PeepholeStats &stats);

/// @brief Fold compute-then-move patterns where an ALU result is immediately
///        moved to another register and the original destination is dead.
std::size_t foldComputeIntoTarget(std::vector<MInstr> &instrs, PeepholeStats &stats);

/// @brief Remove stores to FP offsets that are never loaded anywhere in the function.
/// Cross-block analysis: collects all loaded FP offsets, then removes stores to
/// offsets not in that set.
std::size_t eliminateDeadFpStoresCrossBlock(MFunction &fn, PeepholeStats &stats);

} // namespace viper::codegen::aarch64::peephole
