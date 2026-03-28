//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/codegen/x86_64/peephole/ArithSimplify.hpp
// Purpose: Declarations for arithmetic simplification peephole sub-passes:
//          MOV-zero to XOR, CMP-zero to TEST, arithmetic identities, and
//          strength reduction (multiply by power-of-2 to shift).
//
// Key invariants:
//   - Rewrites preserve semantic equivalence under the x86-64 ISA.
//   - Flag-clobbering rewrites (XOR, TEST) check for downstream flag readers.
//
// Ownership/Lifetime:
//   - Operates on mutable instructions owned by the caller.
//
// Links: src/codegen/x86_64/Peephole.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "../MachineIR.hpp"
#include "PeepholeCommon.hpp"

#include <cstddef>
#include <vector>

namespace viper::codegen::x64::peephole {

/// @brief Rewrite a MOV immediate-to-register into XOR to synthesize zero.
void rewriteToXor(MInstr &instr, Operand regOperand);

/// @brief Convert a compare-against-zero into a register self-test.
void rewriteToTest(MInstr &instr, Operand regOperand);

/// @brief Rewrite arithmetic identity operations (add #0, or #0, xor #0, and #-1, shift #0).
/// @return true if the instruction is an identity and should be removed.
[[nodiscard]] bool tryArithmeticIdentity(const std::vector<MInstr> &instrs,
                                         std::size_t idx,
                                         PeepholeStats &stats);

/// @brief Apply strength reduction: mul by power-of-2 -> shift left.
/// @return true if reduction was applied.
[[nodiscard]] bool tryStrengthReduction(std::vector<MInstr> &instrs,
                                        std::size_t idx,
                                        const RegConstMap &knownConsts,
                                        PeepholeStats &stats);

} // namespace viper::codegen::x64::peephole
