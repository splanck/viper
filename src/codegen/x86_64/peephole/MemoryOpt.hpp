//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/x86_64/peephole/MemoryOpt.hpp
// Purpose: x86-64 memory access peephole optimizations: dead frame store
//          elimination and store-to-load forwarding.
// Key invariants:
//   - Only frame-relative (RSP/RBP-based) memory accesses are considered.
//   - Load forwarding only substitutes when the stored value register is still live.
// Ownership/Lifetime:
//   - Operates on mutable instruction vectors owned by the caller.
// Links: codegen/x86_64/peephole/MemoryOpt.cpp,
//        codegen/x86_64/peephole/PeepholeCommon.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "../MachineIR.hpp"
#include "PeepholeCommon.hpp"

#include <cstddef>
#include <vector>

namespace viper::codegen::x64::peephole {

/// @brief Remove frame stores whose value is never loaded before being overwritten.
/// @return Number of stores eliminated.
std::size_t eliminateDeadFrameStores(std::vector<MInstr> &instrs, PeepholeStats &stats);

/// @brief Forward frame stores to subsequent loads, eliminating the load.
/// @return Number of load instructions eliminated.
std::size_t forwardFrameStoreLoads(std::vector<MInstr> &instrs, PeepholeStats &stats);

} // namespace viper::codegen::x64::peephole
