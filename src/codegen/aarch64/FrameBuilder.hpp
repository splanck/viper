// src/codegen/aarch64/FrameBuilder.hpp
// SPDX-License-Identifier: GPL-3.0-only
//
// Purpose: Centralise AArch64 frame layout construction for Machine IR.
//          Assigns FP-relative offsets for locals and spills and computes the
//          total frame size (aligned to 16). Cooperates with the allocator and
//          lowering by exposing helpers to declare locals/spills and finalise
//          the layout onto MFunction.
//

#pragma once

#include "MachineIR.hpp"

namespace viper::codegen::aarch64
{

class FrameBuilder
{
  public:
    explicit FrameBuilder(MFunction &fn) noexcept : fn_(&fn) {}

    // Declare a local stack slot by IL temp id (size/align in bytes).
    void addLocal(unsigned tempId, int sizeBytes, int alignBytes = 8);

    // Returns the assigned FP-relative offset for the given local.
    int localOffset(unsigned tempId) const;

    // Ensure a spill slot exists for a vreg and return its FP-relative offset.
    int ensureSpill(uint16_t vreg, int sizeBytes = 8, int alignBytes = 8);

    // Optional: reserve a maximum outgoing-arg area (bytes).
    void setMaxOutgoingBytes(int bytes);

    // Assign offsets for any deferred slots and compute total frame size.
    void finalize();

  private:
    MFunction *fn_{};
    int nextOffset_{-8}; // first slot at [x29, #-8]
    int minOffset_{0};   // most negative offset assigned

    int assignAlignedSlot(int sizeBytes, int alignBytes);
};

} // namespace viper::codegen::aarch64

