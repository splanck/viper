//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/codegen/aarch64/peephole/MemoryOpt.hpp
// Purpose: Declarations for memory optimizations: LDP/STP merging,
//          store-load forwarding, and MADD fusion.
//
// Key invariants:
//   - LDP/STP merge only pairs adjacent FP-relative accesses with matching
//     offset alignment.
//   - Store-load forwarding only applies within a basic block scope.
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

/// @brief Try to merge consecutive ldr/str into ldp/stp.
[[nodiscard]] bool tryLdpStpMerge(std::vector<MInstr> &instrs,
                                  std::size_t idx,
                                  PeepholeStats &stats);

/// @brief Store-load forwarding within a basic block.
std::size_t forwardStoreLoads(std::vector<MInstr> &instrs, PeepholeStats &stats);

/// @brief Try to fuse mul+add into madd.
[[nodiscard]] bool tryMaddFusion(std::vector<MInstr> &instrs,
                                 std::size_t idx,
                                 PeepholeStats &stats);

} // namespace viper::codegen::aarch64::peephole
