//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/codegen/aarch64/peephole/IdentityElim.hpp
// Purpose: Declarations for identity move elimination and consecutive move
//          folding peephole sub-passes.
//
// Key invariants:
//   - Identity move removal only removes provably redundant mov r,r / fmov d,d.
//   - Consecutive move folding checks liveness before transforming.
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

/// @brief Check if an instruction is an identity move (mov r, r).
[[nodiscard]] bool isIdentityMovRR(const MInstr &instr) noexcept;

/// @brief Check if an instruction is an identity FPR move (fmov d, d).
[[nodiscard]] bool isIdentityFMovRR(const MInstr &instr) noexcept;

/// @brief Try to fold consecutive moves: mov r1, r2; mov r3, r1 -> mov r3, r2
[[nodiscard]] bool tryFoldConsecutiveMoves(std::vector<MInstr> &instrs,
                                           std::size_t idx,
                                           PeepholeStats &stats);

/// @brief Try to fold immediate-then-move: mov Rd, #imm; mov Rt, Rd -> mov Rt, #imm
[[nodiscard]] bool tryFoldImmThenMove(std::vector<MInstr> &instrs,
                                      std::size_t idx,
                                      PeepholeStats &stats);

} // namespace viper::codegen::aarch64::peephole
