//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/codegen/aarch64/ra/RegPools.cpp
// Purpose: Implementation of physical register pool management for the
//          AArch64 register allocator.
// Key invariants:
//   - build() populates free lists with caller-saved first, then callee-saved.
//   - Argument registers are excluded from initial free lists.
// Ownership/Lifetime:
//   - See RegPools.hpp.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#include "RegPools.hpp"

#include "RegClassify.hpp"

#include <stdexcept>

namespace viper::codegen::aarch64::ra
{

void RegPools::build(const TargetInfo &ti)
{
    gprFree.clear();
    fprFree.clear();
    calleeSavedGPRSet = {};

    // Pre-compute callee-saved GPR set for O(1) lookup in takeGPRPreferCalleeSaved()
    for (auto r : ti.calleeSavedGPR)
        calleeSavedGPRSet[static_cast<std::size_t>(r)] = true;

    // Prefer caller-saved first, exclude argument registers
    for (auto r : ti.callerSavedGPR)
    {
        if (isAllocatableGPR(r) && !isArgRegister(r, ti))
            gprFree.push_back(r);
    }
    for (auto r : ti.calleeSavedGPR)
    {
        if (isAllocatableGPR(r))
            gprFree.push_back(r);
    }

    // FPR: also exclude argument registers V0-V7
    for (auto r : ti.callerSavedFPR)
    {
        if (!isArgRegister(r, ti))
            fprFree.push_back(r);
    }
    for (auto r : ti.calleeSavedFPR)
    {
        fprFree.push_back(r);
    }
}

PhysReg RegPools::takeGPR()
{
    if (gprFree.empty())
        throw std::runtime_error("AArch64 register allocator: GPR pool exhausted — "
                                 "maybeSpillForPressure should have freed a register");

    // Prefer caller-saved registers to minimize prologue/epilogue overhead.
    // Callee-saved registers (x19-x28) require save/restore in the function
    // prologue/epilogue for every call, so use them only when caller-saved
    // registers are exhausted.
    for (auto it = gprFree.begin(); it != gprFree.end(); ++it)
    {
        if (!calleeSavedGPRSet[static_cast<std::size_t>(*it)])
        {
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

PhysReg RegPools::takeGPRPreferCalleeSaved(const TargetInfo & /*ti*/)
{
    if (gprFree.empty())
        throw std::runtime_error("AArch64 register allocator: GPR pool exhausted — "
                                 "maybeSpillForPressure should have freed a register");

    // Try to find a callee-saved register first using O(1) array lookup
    for (auto it = gprFree.begin(); it != gprFree.end(); ++it)
    {
        if (calleeSavedGPRSet[static_cast<std::size_t>(*it)])
        {
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

void RegPools::releaseGPR(PhysReg r, const TargetInfo & /*ti*/)
{
    // Release register back to pool - push to back to maintain FIFO order
    gprFree.push_back(r);
}

PhysReg RegPools::takeFPR()
{
    if (fprFree.empty())
        return kScratchFPR; // Fallback to scratch register when pool exhausted
    auto r = fprFree.front();
    fprFree.pop_front();
    return r;
}

void RegPools::releaseFPR(PhysReg r, const TargetInfo & /*ti*/)
{
    // Release register back to pool - push to back to maintain FIFO order
    fprFree.push_back(r);
}

} // namespace viper::codegen::aarch64::ra
