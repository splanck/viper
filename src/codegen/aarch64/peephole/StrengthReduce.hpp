//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/codegen/aarch64/peephole/StrengthReduce.hpp
// Purpose: Declarations for arithmetic identity elimination, strength reduction,
//          and immediate folding peephole sub-passes.
//
// Key invariants:
//   - Rewrites preserve semantic equivalence under the AArch64 ISA.
//   - Strength reduction only applies to provably equivalent transforms.
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

/// @brief Try to fold an RRR operation into RI when one operand is a known constant.
[[nodiscard]] bool tryImmediateFolding(MInstr &instr,
                                       const RegConstMap &knownConsts,
                                       PeepholeStats &stats);

/// @brief Rewrite FP arithmetic identity operations (placeholder for future enhancement).
[[nodiscard]] bool tryFPArithmeticIdentity(MInstr &instr, PeepholeStats &stats);

} // namespace viper::codegen::aarch64::peephole
