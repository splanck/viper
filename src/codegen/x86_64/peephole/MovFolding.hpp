//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/codegen/x86_64/peephole/MovFolding.hpp
// Purpose: Declarations for move folding peephole sub-passes: consecutive
//          register-to-register move coalescing.
//
// Key invariants:
//   - Only folds when the intermediate register is provably dead.
//   - Argument registers near calls are not folded to avoid ABI violations.
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

/// @brief Try to fold consecutive moves: mov r1, r2; mov r3, r1 -> mov r3, r2.
/// @return true if a fold was performed.
[[nodiscard]] bool tryFoldConsecutiveMoves(std::vector<MInstr> &instrs,
                                           std::size_t idx,
                                           PeepholeStats &stats);

} // namespace viper::codegen::x64::peephole
