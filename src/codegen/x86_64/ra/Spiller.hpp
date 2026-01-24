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

#include <cstddef>
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

/// @brief Describes the lifetime of a spill slot for reuse analysis.
struct SlotLifetime
{
    std::size_t start{0}; ///< First instruction index using this slot.
    std::size_t end{0};   ///< Last instruction index using this slot (exclusive).
    bool inUse{false};    ///< True if the slot is currently assigned to a live value.
};

/// @brief Helper component managing spill slot allocation and load/store codegen.
/// @details Implements spill slot reuse by tracking the lifetime of each slot
///          and assigning slots with non-overlapping lifetimes to different values.
class Spiller
{
  public:
    Spiller() = default;

    /// @brief Ensure the spill plan owns a stack slot for @p cls.
    void ensureSpillSlot(RegClass cls, SpillPlan &plan);

    /// @brief Assign a spill slot with lifetime-based reuse analysis.
    /// @details Attempts to reuse an existing slot whose lifetime doesn't overlap
    ///          with the interval [start, end). If no reusable slot exists,
    ///          allocates a new one. This can reduce stack frame size by 20-40%.
    /// @param cls Register class for the spill slot.
    /// @param plan Spill plan to receive the slot assignment.
    /// @param start Start of the live interval (instruction index).
    /// @param end End of the live interval (instruction index, exclusive).
    void ensureSpillSlotWithReuse(RegClass cls,
                                  SpillPlan &plan,
                                  std::size_t start,
                                  std::size_t end);

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

    /// @brief Spill the active value with lifetime-based slot reuse.
    /// @details This variant enables spill slot reuse by tracking the live interval
    ///          of the spilled value. Values with non-overlapping lifetimes can
    ///          share the same stack slot, reducing stack frame size.
    /// @param cls Register class of the spilled value.
    /// @param vreg Virtual register identifier being spilled.
    /// @param alloc Allocation record tracking the virtual register state.
    /// @param pool Register pool receiving the freed physical register.
    /// @param prefix Instruction buffer that receives the spill store.
    /// @param result Global allocation result map updated to forget @p vreg.
    /// @param intervalStart Start of the value's live interval.
    /// @param intervalEnd End of the value's live interval.
    void spillValueWithReuse(RegClass cls,
                             uint16_t vreg,
                             VirtualAllocation &alloc,
                             std::deque<PhysReg> &pool,
                             std::vector<MInstr> &prefix,
                             AllocationResult &result,
                             std::size_t intervalStart,
                             std::size_t intervalEnd);

    [[nodiscard]] int gprSlots() const noexcept
    {
        return nextSpillSlotGPR_;
    }

    [[nodiscard]] int xmmSlots() const noexcept
    {
        return nextSpillSlotXMM_;
    }

    /// @brief Mark a spill slot as no longer in use (for lifetime tracking).
    /// @details Called when a spilled value's live interval ends, allowing the
    ///          slot to be reused by future spills with non-overlapping lifetimes.
    /// @param cls Register class of the slot.
    /// @param slot Slot index to release.
    void releaseSlot(RegClass cls, int slot);

  private:
    int nextSpillSlotGPR_{0};
    int nextSpillSlotXMM_{0};

    /// @brief Lifetime tracking for GPR spill slots to enable reuse.
    std::vector<SlotLifetime> gprSlotLifetimes_{};

    /// @brief Lifetime tracking for XMM spill slots to enable reuse.
    std::vector<SlotLifetime> xmmSlotLifetimes_{};

    /// @brief Find a reusable slot with non-overlapping lifetime.
    /// @param lifetimes Vector of slot lifetimes to search.
    /// @param start Start of the new value's live interval.
    /// @param end End of the new value's live interval.
    /// @return Index of a reusable slot, or -1 if none found.
    [[nodiscard]] int findReusableSlot(std::vector<SlotLifetime> &lifetimes,
                                       std::size_t start,
                                       std::size_t end) const;

    [[nodiscard]] Operand makeFrameOperand(int slot) const;
};

} // namespace viper::codegen::x64::ra
