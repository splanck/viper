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
