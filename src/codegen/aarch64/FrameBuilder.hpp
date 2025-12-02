//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/aarch64/FrameBuilder.hpp
// Purpose: Centralise AArch64 frame layout construction for Machine IR.
// Key invariants: To be documented.
// Ownership/Lifetime: To be documented.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include "MachineIR.hpp"
#include "TargetAArch64.hpp"

namespace viper::codegen::aarch64
{

/// @brief Centralizes AArch64 stack frame layout construction.
///
/// Manages allocation of local variables, spill slots, and outgoing argument
/// areas according to AAPCS64 stack layout requirements.
///
/// @invariant All offsets are negative (below the frame pointer).
/// @invariant Stack slots are 8-byte aligned minimum; 16-byte alignment is
///            maintained for the overall frame.
class FrameBuilder
{
  public:
    explicit FrameBuilder(MFunction &fn) noexcept : fn_(&fn) {}

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
