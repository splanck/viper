//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/aarch64/peephole/BranchOpt.hpp
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
// Links: codegen/aarch64/Peephole.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "../MachineIR.hpp"
#include "../Peephole.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace zanna::codegen::aarch64::peephole {

/// @brief Invert AArch64 condition code string.
[[nodiscard]] const char *invertCondition(const char *cond) noexcept;

/// @brief Check if an instruction is an unconditional branch to a specific label.
[[nodiscard]] bool isBranchTo(const MInstr &instr, const std::string &label) noexcept;

/// @brief Try to fuse cmp+bcond into cbz/cbnz.
[[nodiscard]] bool tryCbzCbnzFusion(std::vector<MInstr> &instrs,
                                    std::size_t idx,
                                    PeepholeStats &stats);

/// @brief Try to fuse a single-bit and+cbz/cbnz into tbz/tbnz.
/// @param carriedExitRegs Optional sorted list of physical registers carried
///        live across the enclosing block's exit (see tryCsetBranchFusion).
[[nodiscard]] bool tryTbzTbnzFusion(std::vector<MInstr> &instrs,
                                    std::size_t idx,
                                    PeepholeStats &stats,
                                    const std::vector<uint16_t> *carriedExitRegs = nullptr);

/// @brief Try to fuse cset+cbnz/cbz into a single b.cond instruction.
/// @param carriedExitRegs Optional sorted list of physical registers carried
///        live across the enclosing block's exit without any in-block use
///        (MBasicBlock::carriedExitRegs); a CSET into such a register is
///        never fused away.
[[nodiscard]] bool tryCsetBranchFusion(std::vector<MInstr> &instrs,
                                       std::size_t idx,
                                       PeepholeStats &stats,
                                       const std::vector<uint16_t> *carriedExitRegs = nullptr);

/// @brief Reorder blocks for better code layout (move cold blocks to end).
std::size_t reorderBlocks(MFunction &fn);

} // namespace zanna::codegen::aarch64::peephole
