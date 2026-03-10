//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/codegen/aarch64/ra/VState.hpp
// Purpose: Virtual register state tracking for the AArch64 linear-scan
//          register allocator.
// Key invariants:
//   - hasPhys is true iff the vreg currently occupies a physical register.
//   - dirty is true iff the register value is newer than the spill slot.
// Ownership/Lifetime:
//   - Owned by the allocator's state maps; one VState per active vreg.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include <climits>
#include <cstdint>

#include "codegen/aarch64/TargetAArch64.hpp"

namespace viper::codegen::aarch64::ra
{

/// @brief Tracks the allocation state of a virtual register.
///
/// Maintains whether the vreg is currently in a physical register,
/// has been spilled to the stack, and when it was last used (for
/// victim selection heuristics).
struct VState
{
    bool hasPhys{false};        ///< True if currently in a physical register.
    PhysReg phys{PhysReg::X0};  ///< Physical register (valid when hasPhys).
    bool spilled{false};        ///< True if value is on the stack.
    bool dirty{false};          ///< True if register value is newer than spill slot.
    int fpOffset{0};            ///< FP-relative offset of spill slot.
    unsigned lastUse{0};        ///< Instruction index of last use (for LRU).
    unsigned nextUse{UINT_MAX}; ///< Instruction index of next use (for furthest-end-point).
};

} // namespace viper::codegen::aarch64::ra
