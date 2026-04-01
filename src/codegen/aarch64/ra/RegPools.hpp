//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/codegen/aarch64/ra/RegPools.hpp
// Purpose: Physical register pool management for the AArch64 register
//          allocator. Maintains free-lists for GPR and FPR classes and
//          tracks callee-saved register usage.
// Key invariants:
//   - build() must be called before any take/release operations.
//   - Callee-saved usage arrays are indexed by PhysReg ordinal.
//   - GPR pool never hands out X9/X16 (global scratch), X18, X29, X30, or SP.
// Ownership/Lifetime:
//   - Owned by the LinearAllocator; one RegPools per allocation run.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <deque>

#include "codegen/aarch64/TargetAArch64.hpp"

namespace viper::codegen::aarch64::ra {

struct RegPools {
    std::deque<PhysReg> gprFree{};
    std::deque<PhysReg> fprFree{};

    /// Which callee-saved GPRs were actually used in this function.
    /// Indexed by static_cast<std::size_t>(PhysReg); AArch64 has 64 PhysReg values.
    std::array<bool, 64> calleeUsed{};
    /// Which callee-saved FPRs were actually used in this function.
    std::array<bool, 64> calleeUsedFPR{};

    /// Pre-computed set of callee-saved GPRs for O(1) lookup in takeGPRPreferCalleeSaved().
    /// Stored as a dense bool array (indexed by PhysReg ID) for cache efficiency.
    std::array<bool, 64> calleeSavedGPRSet{};

    /// @brief Initialize free lists from target info.
    void build(const TargetInfo &ti);

    /// @brief Take any available GPR from the free pool.
    PhysReg takeGPR();

    /// @brief Take a GPR, preferring callee-saved registers.
    PhysReg takeGPRPreferCalleeSaved(const TargetInfo &ti);

    /// @brief Release a GPR back to the free pool.
    void releaseGPR(PhysReg r, const TargetInfo &ti);

    /// @brief Take any available FPR from the free pool.
    PhysReg takeFPR();

    /// @brief Release an FPR back to the free pool.
    void releaseFPR(PhysReg r, const TargetInfo &ti);
};

} // namespace viper::codegen::aarch64::ra
