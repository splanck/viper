//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/codegen/x86_64/ra/Spiller.hpp
// Purpose: Declare helper utilities responsible for spill slot management and
//          load/store emission during linear-scan register allocation.
// Key invariants: Spill slot indices grow monotonically per register class and
//                 refer to 8-byte stack slots. Helper routines do not mutate
//                 Machine IR directly; they return instructions for insertion.
// Ownership/Lifetime: Spiller instances own their slot counters; callers manage
//                     the produced instruction sequences.
// Links: src/codegen/x86_64/ra/Allocator.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "../MachineIR.hpp"
#include "../TargetX64.hpp"

#include <deque>
#include <unordered_map>
#include <vector>

namespace viper::codegen::x64
{
struct AllocationResult;
} // namespace viper::codegen::x64

namespace viper::codegen::x64::ra
{

struct VirtualAllocation; // Forward declaration defined in Allocator.hpp.

/// @brief Describes the spill status of a virtual register.
struct SpillPlan
{
    bool needsSpill{false}; ///< True when the value must reside in memory.
    int slot{-1};           ///< Assigned spill slot index; -1 until materialised.
};

/// @brief Helper component managing spill slot allocation and load/store codegen.
class Spiller
{
  public:
    Spiller() = default;

    /// @brief Ensure the spill plan owns a stack slot for @p cls.
    void ensureSpillSlot(RegClass cls, SpillPlan &plan);

    /// @brief Emit a load from @p plan into @p dst.
    [[nodiscard]] MInstr makeLoad(RegClass cls, PhysReg dst, const SpillPlan &plan) const;

    /// @brief Emit a store from @p src into @p plan.
    [[nodiscard]] MInstr makeStore(RegClass cls, const SpillPlan &plan, PhysReg src) const;

    /// @brief Spill the active value associated with @p vreg.
    void spillValue(RegClass cls,
                    uint16_t vreg,
                    VirtualAllocation &alloc,
                    std::deque<PhysReg> &pool,
                    std::vector<MInstr> &prefix,
                    AllocationResult &result);

    [[nodiscard]] int gprSlots() const noexcept
    {
        return nextSpillSlotGPR_;
    }

    [[nodiscard]] int xmmSlots() const noexcept
    {
        return nextSpillSlotXMM_;
    }

  private:
    int nextSpillSlotGPR_{0};
    int nextSpillSlotXMM_{0};

    [[nodiscard]] Operand makeFrameOperand(int slot) const;
};

} // namespace viper::codegen::x64::ra
