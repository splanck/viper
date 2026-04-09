//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file FrameBuilder.cpp
/// @brief Stack frame layout construction for AArch64 code generation.
///
/// This file implements the FrameBuilder class which manages the layout of
/// a function's stack frame during MIR lowering. It assigns offsets to local
/// variables (allocas), spill slots, and tracks outgoing argument areas.
///
/// **What is a Stack Frame?**
/// A stack frame is the region of memory allocated on the stack for a single
/// function invocation. It contains:
/// - Saved registers (FP, LR, callee-saved registers)
/// - Local variables declared in the function
/// - Spill slots for register allocator overflow
/// - Outgoing argument area for function calls
///
/// **AAPCS64 Stack Frame Layout:**
/// ```
/// Higher addresses (caller's frame)
/// ┌────────────────────────────────────────┐
/// │ Caller's outgoing args (if any)        │
/// ├────────────────────────────────────────┤ ← Old SP (before call)
/// │ Return address (x30/LR)                │ ← Saved by STP x29, x30
/// │ Previous frame pointer (x29/FP)        │
/// ├────────────────────────────────────────┤ ← Current FP (x29)
/// │ Callee-saved registers (x19-x28, etc.) │ ← Saved by prologue
/// ├────────────────────────────────────────┤
/// │ Local variables (alloca slots)         │ ← Managed by FrameBuilder
/// ├────────────────────────────────────────┤
/// │ Spill slots (reg alloc overflow)       │ ← Managed by FrameBuilder
/// ├────────────────────────────────────────┤
/// │ Outgoing argument area                 │ ← For calls with stack args
/// ├────────────────────────────────────────┤ ← Current SP
/// Lower addresses (grows downward)
/// ```
///
/// **Frame Pointer Relative Addressing:**
/// All locals and spills use negative offsets from the frame pointer:
/// ```
/// [fp, #-8]   ← First local (offset = -8)
/// [fp, #-16]  ← Second local (offset = -16)
/// [fp, #-24]  ← First spill slot
/// ...
/// ```
///
/// **Alignment Requirements:**
/// - Stack pointer must be 16-byte aligned at all times
/// - Individual slots are aligned to their natural alignment
/// - The frame builder rounds up the total frame size to 16 bytes
///
/// **Slot Assignment Algorithm:**
/// 1. Start at offset -8 (first available slot below FP)
/// 2. For each slot request:
///    a. Align the current offset downward to the required alignment
///    b. Subtract the slot size
///    c. Record the offset and update the cursor
/// 3. After all slots assigned, round total frame size to 16-byte boundary
///
/// **Usage Example:**
/// ```cpp
/// FrameBuilder fb{mf};
///
/// // Add local variable slots (from alloca instructions)
/// fb.addLocal(tempId, 8, 8);    // 8-byte slot, 8-byte aligned
///
/// // Add spill slots (from register allocator)
/// int spillOffset = fb.ensureSpill(vreg, 8, 8);
///
/// // Track outgoing argument area
/// fb.setMaxOutgoingBytes(32);   // Space for 4 stack arguments
///
/// // Finalize and compute total frame size
/// fb.finalize();
/// // Now mf.frame.totalBytes is set
/// ```
///
/// @see MachineIR.hpp For MFunction::Frame and related types
/// @see RegAllocLinear.cpp For spill slot usage
/// @see AsmEmitter.cpp For prologue/epilogue generation
///
//===----------------------------------------------------------------------===//

#include "FrameBuilder.hpp"

#include <algorithm>

namespace viper::codegen::aarch64 {

void FrameBuilder::addLocal(unsigned tempId, int sizeBytes, int alignBytes) {
    // If already present, ignore.
    for (const auto &L : fn_->frame.locals)
        if (L.tempId == tempId)
            return;
    // Assign an aligned slot and record offset.
    const int off = assignAlignedSlot(sizeBytes, alignBytes);
    fn_->frame.locals.push_back(MFunction::StackLocal{tempId, sizeBytes, alignBytes, off});
}

int FrameBuilder::localOffset(unsigned tempId) const {
    return fn_->frame.getLocalOffset(tempId);
}

int FrameBuilder::ensureSpill(uint32_t vreg, int sizeBytes, int alignBytes) {
    if (const auto *slot = findLatestSpillSlot(vreg))
        return slot->offset;
    const int off = assignAlignedSlot(sizeBytes, alignBytes);
    fn_->frame.spills.push_back(MFunction::SpillSlot{vreg, sizeBytes, alignBytes, off});
    return off;
}

int FrameBuilder::ensureSpillWithReuse(uint32_t vreg,
                                       unsigned lastUseInstrIdx,
                                       unsigned currentInstrIdx,
                                       int sizeBytes,
                                       int alignBytes) {
    // Fast path: reuse the most-recent slot assignment for this vreg only while its
    // tracked lifetime is still active. Once that lifetime is dead and the slot has
    // potentially been recycled for another vreg in the same block, the caller must
    // re-run slot selection instead of blindly reusing the cached offset.
    if (const auto *slot = findLatestSpillSlot(vreg)) {
        if (auto *lifetime = findSlotLifetime(slot->offset)) {
            const bool sameEpoch = lifetime->epoch == blockEpoch_;
            const bool stillLive = lifetime->lastUseIdx >= currentInstrIdx;
            if (lifetime->vreg == vreg && (!sameEpoch || stillLive)) {
                lifetime->lastUseIdx = std::max(lifetime->lastUseIdx, lastUseInstrIdx);
                return slot->offset;
            }
        } else {
            return slot->offset;
        }
    }

    // Try to reuse a dead slot.  A slot is dead when:
    //   (a) it was recorded in the SAME block epoch (same basic block), AND
    //   (b) its previous occupant's last use index is before the current instruction.
    //
    // Cross-epoch (cross-block) reuse is prohibited because currentInstrIdx
    // is a per-block counter that resets to 0 at each block boundary.
    for (auto &L : slotLifetimes_) {
        if (L.sizeBytes == sizeBytes && L.epoch == blockEpoch_ && L.lastUseIdx < currentInstrIdx) {
            // Recycle: update the vreg→offset mapping and refresh the lifetime.
            fn_->frame.spills.push_back(
                MFunction::SpillSlot{vreg, sizeBytes, alignBytes, L.offset});
            L.vreg = vreg;
            L.lastUseIdx = lastUseInstrIdx;
            // epoch stays the same (still the current block)
            return L.offset;
        }
    }

    // No dead slot available: allocate a fresh one and track its lifetime.
    const int off = assignAlignedSlot(sizeBytes, alignBytes);
    fn_->frame.spills.push_back(MFunction::SpillSlot{vreg, sizeBytes, alignBytes, off});
    slotLifetimes_.push_back(SlotLifetime{vreg, off, sizeBytes, lastUseInstrIdx, blockEpoch_});
    return off;
}

void FrameBuilder::setMaxOutgoingBytes(int bytes) {
    fn_->frame.maxOutgoingBytes = std::max(fn_->frame.maxOutgoingBytes, bytes);
}

void FrameBuilder::finalize() {
    // Account for any previously assigned locals/spills on the function as well as
    // slots assigned via this builder instance.
    int usedBytes = slotCursor_.usedBytes();
    for (const auto &L : fn_->frame.locals)
        usedBytes = std::max(usedBytes, -L.offset);
    for (const auto &S : fn_->frame.spills)
        usedBytes = std::max(usedBytes, -S.offset);
    // Account for FP-LR area implicitly; our offsets start at -8, so usedBytes already counts
    // slots. Add any reserved outgoing-arg area.
    usedBytes += fn_->frame.maxOutgoingBytes;
    // Round up to 16-byte alignment.
    usedBytes = common::roundUpBytes(usedBytes, kStackAlignment);
    fn_->frame.totalBytes = usedBytes;
    fn_->localFrameSize = usedBytes; // bridge for current emitter plan field
}

int FrameBuilder::assignAlignedSlot(int sizeBytes, int alignBytes) {
    return slotCursor_.allocate(std::max(1, sizeBytes), alignBytes).offset;
}

MFunction::SpillSlot *FrameBuilder::findLatestSpillSlot(uint32_t vreg) noexcept {
    for (auto it = fn_->frame.spills.rbegin(); it != fn_->frame.spills.rend(); ++it) {
        if (it->vreg == vreg)
            return &*it;
    }
    return nullptr;
}

const MFunction::SpillSlot *FrameBuilder::findLatestSpillSlot(uint32_t vreg) const noexcept {
    for (auto it = fn_->frame.spills.rbegin(); it != fn_->frame.spills.rend(); ++it) {
        if (it->vreg == vreg)
            return &*it;
    }
    return nullptr;
}

FrameBuilder::SlotLifetime *FrameBuilder::findSlotLifetime(int offset) noexcept {
    for (auto &lifetime : slotLifetimes_) {
        if (lifetime.offset == offset)
            return &lifetime;
    }
    return nullptr;
}

const FrameBuilder::SlotLifetime *FrameBuilder::findSlotLifetime(int offset) const noexcept {
    for (const auto &lifetime : slotLifetimes_) {
        if (lifetime.offset == offset)
            return &lifetime;
    }
    return nullptr;
}

} // namespace viper::codegen::aarch64
