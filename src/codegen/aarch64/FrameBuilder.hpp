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

    /// @brief Reserve space for outgoing arguments passed on the stack.
    /// @param bytes Maximum bytes needed for outgoing arguments.
    void setMaxOutgoingBytes(int bytes);

    /// @brief Finalize frame layout and compute total frame size.
    ///
    /// Must be called once after all locals and spills are declared.
    /// Assigns final offsets and ensures proper stack alignment.
    void finalize();

  private:
    MFunction *fn_{};
    int nextOffset_{-kSlotSizeBytes}; ///< Next available slot (first at [x29, #-8]).
    int minOffset_{0};                ///< Most negative offset assigned.

    int assignAlignedSlot(int sizeBytes, int alignBytes);
};

} // namespace viper::codegen::aarch64
