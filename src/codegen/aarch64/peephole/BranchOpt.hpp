//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/codegen/aarch64/peephole/BranchOpt.hpp
// Purpose: Declarations for branch optimizations: CBZ/CBNZ fusion, cset
//          branch fusion, branch inversion, block reordering, and
//          branch-to-next removal.
//
// Key invariants:
//   - Branch rewrites preserve control-flow semantics.
//   - Block reordering only moves cold blocks (trap/error handlers) to the end.
//
// Ownership/Lifetime:
//   - Operates on mutable MFunction/instruction vectors owned by the caller.
//
// Links: src/codegen/aarch64/Peephole.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "../MachineIR.hpp"
#include "../Peephole.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace viper::codegen::aarch64::peephole {

/// @brief Invert AArch64 condition code string.
[[nodiscard]] const char *invertCondition(const char *cond) noexcept;

/// @brief Check if an instruction is an unconditional branch to a specific label.
[[nodiscard]] bool isBranchTo(const MInstr &instr, const std::string &label) noexcept;

/// @brief Try to fuse cmp+bcond into cbz/cbnz.
[[nodiscard]] bool tryCbzCbnzFusion(std::vector<MInstr> &instrs,
                                    std::size_t idx,
                                    PeepholeStats &stats);

/// @brief Try to fuse cset+cbnz/cbz into a single b.cond instruction.
[[nodiscard]] bool tryCsetBranchFusion(std::vector<MInstr> &instrs,
                                       std::size_t idx,
                                       PeepholeStats &stats);

/// @brief Reorder blocks for better code layout (move cold blocks to end).
std::size_t reorderBlocks(MFunction &fn);

} // namespace viper::codegen::aarch64::peephole
