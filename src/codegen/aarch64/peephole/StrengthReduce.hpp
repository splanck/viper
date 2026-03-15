//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/codegen/aarch64/peephole/StrengthReduce.hpp
// Purpose: Declarations for arithmetic identity elimination, strength reduction,
//          division/modulo optimization, and immediate folding peephole sub-passes.
//
// Key invariants:
//   - Rewrites preserve semantic equivalence under the AArch64 ISA.
//   - Strength reduction only applies to provably equivalent transforms.
//   - Division strength reduction covers UDIV, SDIV (power-of-2 and arbitrary
//     constant), and remainder fusion (UDIV+MSUB, SDIV+MSUB by power-of-2).
//
// Ownership/Lifetime:
//   - Operates on mutable instructions owned by the caller.
//
// Links: src/codegen/aarch64/Peephole.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "../MachineIR.hpp"
#include "../Peephole.hpp"
#include "PeepholeCommon.hpp"

namespace viper::codegen::aarch64::peephole
{

/// @brief Rewrite cmp reg, #0 to tst reg, reg.
[[nodiscard]] bool tryCmpZeroToTst(MInstr &instr, PeepholeStats &stats);

/// @brief Rewrite arithmetic identity operations (add #0, sub #0, shift #0).
[[nodiscard]] bool tryArithmeticIdentity(MInstr &instr, PeepholeStats &stats);

/// @brief Apply strength reduction: mul by power of 2 -> shift left.
[[nodiscard]] bool tryStrengthReduction(MInstr &instr,
                                        const RegConstMap &knownConsts,
                                        PeepholeStats &stats);

/// @brief Apply strength reduction: unsigned division by power of 2 -> logical shift right.
[[nodiscard]] bool tryDivStrengthReduction(MInstr &instr,
                                           const RegConstMap &knownConsts,
                                           PeepholeStats &stats);

/// @brief Apply strength reduction: signed division by constant.
///
/// Handles two cases:
/// - SDIV by power-of-2: Replaces with ASR sign-correction sequence (4 instructions).
/// - SDIV by arbitrary positive constant: Replaces with SMULH magic-number multiply
///   sequence (4-6 instructions, but each ~1-3 cycles vs ~22 cycles for SDIV).
///
/// @param instrs The instruction vector (may be modified via insertion/deletion).
/// @param idx Index of the SDivRRR instruction.
/// @param knownConsts Map of registers to their known constant values.
/// @param stats Peephole statistics (strengthReductions incremented on success).
/// @return true if the instruction was replaced.
[[nodiscard]] bool trySDivStrengthReduction(std::vector<MInstr> &instrs,
                                            std::size_t idx,
                                            const RegConstMap &knownConsts,
                                            PeepholeStats &stats);

/// @brief Fuse [SU]DIV+MSUB remainder pattern into cheaper operations.
///
/// Matches: [SU]DivRRR tmp, lhs, rhs; MSubRRRR dst, tmp, rhs, lhs
/// where rhs is a known power-of-2 constant.
///
/// - UREM by 2^k: Replaced with AND dst, lhs, #(2^k - 1)
/// - SREM by 2^k: Replaced with sign-corrected AND+SUB sequence (5 instructions,
///   each 1 cycle vs ~22 cycles for SDIV + ~4 cycles for MSUB).
///
/// @param instrs The instruction vector (may be modified via insertion/deletion).
/// @param idx Index of the [SU]DivRRR instruction.
/// @param knownConsts Map of registers to their known constant values.
/// @param stats Peephole statistics (strengthReductions incremented on success).
/// @return true if the pattern was replaced.
[[nodiscard]] bool tryRemainderFusion(std::vector<MInstr> &instrs,
                                      std::size_t idx,
                                      const RegConstMap &knownConsts,
                                      PeepholeStats &stats);

/// @brief Try to fold an RRR operation into RI when one operand is a known constant.
[[nodiscard]] bool tryImmediateFolding(MInstr &instr,
                                       const RegConstMap &knownConsts,
                                       PeepholeStats &stats);

/// @brief Rewrite FP arithmetic identity operations (placeholder for future enhancement).
[[nodiscard]] bool tryFPArithmeticIdentity(MInstr &instr, PeepholeStats &stats);

} // namespace viper::codegen::aarch64::peephole
