//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/aarch64/ra/RegPools.cpp
// Purpose: Implementation of physical register pool management for the
//          AArch64 register allocator.
//
// Key invariants:
//   - build() populates free lists with caller-saved first, then callee-saved.
//   - Argument registers are excluded from initial free lists.
//
// Ownership/Lifetime:
//   - Owned by LinearAllocator; one instance per allocation run.
//
// Links: codegen/aarch64/ra/RegPools.hpp,
//        codegen/aarch64/ra/RegClassify.hpp
//
//===----------------------------------------------------------------------===//

#include "RegPools.hpp"

#include "RegClassify.hpp"

#include <algorithm>
#include <stdexcept>

namespace viper::codegen::aarch64::ra {

void RegPools::build(const TargetInfo &ti) {
    gprFree.clear();
    fprFree.clear();
    calleeUsed = {};
    calleeUsedFPR = {};
    calleeSavedGPRSet = {};
    calleeSavedFPRSet = {};

    // Pre-compute callee-saved GPR set for O(1) lookup in takeGPRPreferCalleeSaved()
    for (auto r : ti.calleeSavedGPR)
        calleeSavedGPRSet[static_cast<std::size_t>(r)] = true;
    for (auto r : ti.calleeSavedFPR)
        calleeSavedFPRSet[static_cast<std::size_t>(r)] = true;

    // Prefer caller-saved first, exclude argument registers
    for (auto r : ti.callerSavedGPR) {
        if (isAllocatableGPR(r) && !isArgRegister(r, ti))
            gprFree.push_back(r);
    }
    for (auto r : ti.calleeSavedGPR) {
        if (isAllocatableGPR(r))
            gprFree.push_back(r);
    }

    // FPR: also exclude argument registers V0-V7
    for (auto r : ti.callerSavedFPR) {
        if (!isArgRegister(r, ti) && r != kScratchFPR && r != kScratchFPR2)
            fprFree.push_back(r);
    }
    for (auto r : ti.calleeSavedFPR) {
        if (r != kScratchFPR && r != kScratchFPR2)
            fprFree.push_back(r);
    }
}

PhysReg RegPools::takeGPR() {
    if (gprFree.empty())
        throw std::runtime_error("AArch64 register allocator: GPR pool exhausted — "
                                 "maybeSpillForPressure should have freed a register");

    // Prefer caller-saved registers to minimize prologue/epilogue overhead.
    // Callee-saved registers (x19-x28) require save/restore in the function
    // prologue/epilogue for every call, so use them only when caller-saved
    // registers are exhausted.
    for (auto it = gprFree.begin(); it != gprFree.end(); ++it) {
        if (!calleeSavedGPRSet[static_cast<std::size_t>(*it)]) {
            PhysReg r = *it;
            gprFree.erase(it);
            return r;
        }
    }

    // No caller-saved available, take any register.
    auto r = gprFree.front();
    gprFree.pop_front();
    return r;
}

PhysReg RegPools::takeGPRPreferCalleeSaved(const TargetInfo & /*ti*/) {
    if (gprFree.empty())
        throw std::runtime_error("AArch64 register allocator: GPR pool exhausted — "
                                 "maybeSpillForPressure should have freed a register");

    // Try to find a callee-saved register first using O(1) array lookup
    for (auto it = gprFree.begin(); it != gprFree.end(); ++it) {
        if (calleeSavedGPRSet[static_cast<std::size_t>(*it)]) {
            PhysReg r = *it;
            gprFree.erase(it);
            return r;
        }
    }

    // No callee-saved available, take any
    auto r = gprFree.front();
    gprFree.pop_front();
    return r;
}

void RegPools::releaseGPR(PhysReg r, const TargetInfo & ti) {
    if (!isAllocatableGPR(r) || isArgRegister(r, ti))
        throw std::runtime_error("AArch64 register allocator: attempted to release non-allocatable GPR");
    if (std::find(gprFree.begin(), gprFree.end(), r) != gprFree.end())
        throw std::runtime_error("AArch64 register allocator: duplicate GPR release");
    // Release register back to pool - push to back to maintain FIFO order
    gprFree.push_back(r);
}

PhysReg RegPools::takeFPR() {
    if (fprFree.empty())
        throw std::runtime_error("AArch64 register allocator: FPR pool exhausted — "
                                 "maybeSpillForPressure should have freed a register");
    auto r = fprFree.front();
    fprFree.pop_front();
    return r;
}

PhysReg RegPools::takeFPRPreferCalleeSaved(const TargetInfo & /*ti*/) {
    if (fprFree.empty())
        throw std::runtime_error("AArch64 register allocator: FPR pool exhausted — "
                                 "maybeSpillForPressure should have freed a register");

    for (auto it = fprFree.begin(); it != fprFree.end(); ++it) {
        if (calleeSavedFPRSet[static_cast<std::size_t>(*it)]) {
            PhysReg r = *it;
            fprFree.erase(it);
            return r;
        }
    }

    auto r = fprFree.front();
    fprFree.pop_front();
    return r;
}

void RegPools::releaseFPR(PhysReg r, const TargetInfo & ti) {
    if (!isFPR(r) || isArgRegister(r, ti) || r == kScratchFPR || r == kScratchFPR2)
        throw std::runtime_error("AArch64 register allocator: attempted to release non-allocatable FPR");
    if (std::find(fprFree.begin(), fprFree.end(), r) != fprFree.end())
        throw std::runtime_error("AArch64 register allocator: duplicate FPR release");
    // Release register back to pool - push to back to maintain FIFO order
    fprFree.push_back(r);
}

} // namespace viper::codegen::aarch64::ra
