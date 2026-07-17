//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
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

namespace zanna::codegen::x64::peephole {

/// @brief Remove frame stores whose value is never loaded before being overwritten.
/// @details Scans @p instrs forward looking for two consecutive `STORE`s to the
///          same frame slot with no intervening load. The first store is dead
///          and can be eliminated. Only frame-relative addresses (`RSP`/`RBP`
///          base + immediate offset) are considered; aliasing with arbitrary
///          base registers is conservative and disables the rewrite.
/// @param instrs Instruction list being scanned (mutated in place).
/// @param stats  Peephole statistics counter (incremented per removal).
/// @return Number of stores eliminated.
std::size_t eliminateDeadFrameStores(std::vector<MInstr> &instrs, PeepholeStats &stats);

/// @brief Forward frame stores to subsequent loads, eliminating the load.
/// @details After `STORE [frame_slot], r1` followed by `r2 = LOAD [frame_slot]`,
///          the load can be replaced with `r2 = MOV r1` when `r1` is still live
///          at the load's position. Eliminates the round-trip through memory
///          when no aliased write intervenes.
/// @param instrs Instruction list being scanned (mutated in place).
/// @param stats  Peephole statistics counter (incremented per removal).
/// @return Number of load instructions eliminated.
std::size_t forwardFrameStoreLoads(std::vector<MInstr> &instrs, PeepholeStats &stats);

} // namespace zanna::codegen::x64::peephole
