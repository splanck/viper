//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/aarch64/FrameBuilder.hpp
//
// This file declares the FrameBuilder class, which centralizes AArch64
// stack frame layout construction for the Viper codegen backend. FrameBuilder
// manages the allocation of local variable slots, register spill slots, and
// outgoing argument areas according to the AAPCS64 (ARM Architecture
// Procedure Call Standard for 64-bit) requirements.
//
// Stack layout is constructed incrementally: locals are added during IL-to-MIR
// lowering, spill slots are added during register allocation, and outgoing
// argument space is reserved based on the maximum call argument count.
// The finalize() method computes the total frame size with proper alignment.
//
// All slots are addressed as negative offsets from the frame pointer (x29).
// The first local slot begins at [x29, #-8] and subsequent slots grow
// downward. This matches the AAPCS64 convention where the frame pointer
// points to the saved frame pointer/link register pair.
//
// Key invariants:
//   - All offsets are negative (below the frame pointer).
//   - Stack slots are 8-byte aligned minimum; 16-byte overall frame alignment.
//   - addLocal() and ensureSpill() must be called before finalize().
//   - finalize() must be called exactly once before prologue/epilogue emission.
//
// Ownership: FrameBuilder borrows the MFunction reference and mutates its
// frame layout data. The MFunction must outlive the FrameBuilder.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "MachineIR.hpp"
#include "TargetAArch64.hpp"

namespace viper::codegen::aarch64
{

/// @brief Centralizes AArch64 stack frame layout construction for codegen.
/// @details Manages incremental allocation of local variable slots, register spill
///          slots, and outgoing argument areas following AAPCS64 conventions. All
///          slots use negative offsets from the frame pointer (x29). The builder
///          tracks the next available offset and ensures proper alignment.
///
///          Usage flow:
///          1. During MIR lowering: addLocal() for each alloca instruction
///          2. During register allocation: ensureSpill() for each spilled vreg
///          3. After call lowering: setMaxOutgoingBytes() for stack arguments
///          4. Before prologue emission: finalize() to compute total frame size
///
/// @invariant All offsets are negative (below the frame pointer).
/// @invariant Stack slots are 8-byte aligned minimum; 16-byte alignment is
///            maintained for the overall frame.
class FrameBuilder
{
  public:
    explicit FrameBuilder(MFunction &fn) noexcept : fn_(&fn)
    {
        // Initialize nextOffset_ based on existing frame state to avoid
        // collisions when the register allocator creates a new FrameBuilder
        // after locals have already been allocated during MIR lowering.
        int minExisting = -kSlotSizeBytes;
        for (const auto &L : fn.frame.locals)
            minExisting = std::min(minExisting, L.offset - kSlotSizeBytes);
        for (const auto &S : fn.frame.spills)
            minExisting = std::min(minExisting, S.offset - kSlotSizeBytes);
        nextOffset_ = minExisting;
        minOffset_ = minExisting + kSlotSizeBytes;
    }

    /// @brief Declare a local stack slot by IL temp id.
    /// @param tempId The IL temporary identifier.
    /// @param sizeBytes Size of the slot in bytes.
    /// @param alignBytes Alignment requirement (default: 8 bytes for 64-bit values).
    void addLocal(unsigned tempId, int sizeBytes, int alignBytes = kSlotSizeBytes);

    /// @brief Returns the assigned FP-relative offset for a local variable.
    /// @param tempId The IL temporary identifier.
    /// @return Negative offset from the frame pointer.
    int localOffset(unsigned tempId) const;

    /// @brief Ensure a spill slot exists for a virtual register.
    /// @param vreg Virtual register identifier.
    /// @param sizeBytes Size of the spill slot (default: 8 bytes).
    /// @param alignBytes Alignment requirement (default: 8 bytes).
    /// @return FP-relative offset of the spill slot.
    int ensureSpill(uint16_t vreg, int sizeBytes = kSlotSizeBytes, int alignBytes = kSlotSizeBytes);

    /// @brief Ensure a spill slot for @p vreg, reusing a dead slot if available.
    ///
    /// @details A slot is dead when its previous occupant's last use occurred
    ///          before @p currentInstrIdx.  If such a slot exists and has a
    ///          compatible size, it is recycled for @p vreg without growing the
    ///          frame.  Otherwise a fresh slot is allocated as normal.
    ///
    /// @param vreg           Virtual register to assign a slot to.
    /// @param lastUseInstrIdx  Last instruction index that reads @p vreg
    ///                         (used to record this slot's new lifetime end).
    /// @param currentInstrIdx  Instruction index at the point of spill
    ///                         (slots with lastUse < this value are dead).
    /// @param sizeBytes      Slot size in bytes (default: 8).
    /// @param alignBytes     Alignment in bytes (default: 8).
    /// @return FP-relative offset of the (possibly reused) spill slot.
    int ensureSpillWithReuse(uint16_t vreg,
                             unsigned lastUseInstrIdx,
                             unsigned currentInstrIdx,
                             int sizeBytes  = kSlotSizeBytes,
                             int alignBytes = kSlotSizeBytes);

    /// @brief Reserve space for outgoing arguments passed on the stack.
    /// @param bytes Maximum bytes needed for outgoing arguments.
    void setMaxOutgoingBytes(int bytes);

    /// @brief Finalize frame layout and compute total frame size.
    ///
    /// Must be called once after all locals and spills are declared.
    /// Assigns final offsets and ensures proper stack alignment.
    void finalize();

    /// @brief Notify the frame builder that a new basic block is starting.
    ///
    /// Increments the block epoch so that spill slots from previous blocks are
    /// never reused in the current block.  Must be called before processing
    /// each basic block during register allocation.
    void beginNewBlock() noexcept { ++blockEpoch_; }

  private:

    /// @brief Lifetime record for a single spill slot.
    ///
    /// Tracks the FP-relative offset, the instruction index of the last use of
    /// the most-recent vreg assigned to this slot, and the block epoch in which
    /// that last use occurred.  Slots are only eligible for reuse within the
    /// SAME block epoch — cross-block reuse is prohibited because
    /// @p currentInstrIdx is a per-block counter that resets to 0 at each block
    /// boundary, making cross-epoch comparisons meaningless.
    struct SlotLifetime
    {
        int      offset;       ///< FP-relative offset (always negative).
        int      sizeBytes;    ///< Slot size (for size-compatible reuse check).
        unsigned lastUseIdx;   ///< Last instruction index reading the current vreg.
        uint32_t epoch;        ///< Block epoch when lastUseIdx was recorded.
    };

    MFunction *fn_{};
    int nextOffset_{-kSlotSizeBytes}; ///< Next available slot (first at [x29, #-8]).
    int minOffset_{0};                ///< Most negative offset assigned.
    uint32_t blockEpoch_{0};         ///< Monotonically-increasing block counter.

    /// Lifetime records for every slot allocated via ensureSpillWithReuse().
    std::vector<SlotLifetime> slotLifetimes_;

    int assignAlignedSlot(int sizeBytes, int alignBytes);
};

} // namespace viper::codegen::aarch64
