//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/aarch64/FrameBuilder.cpp
// Purpose: AArch64 AAPCS64 stack-frame layout construction.
//          Implements FrameBuilder: assigns FP-relative negative offsets to
//          local variables (allocas), register spill slots, and outgoing
//          argument areas. finalize() rounds the total up to 16-byte alignment
//          and writes MFunction::localFrameSize.
//
// AAPCS64 frame layout (high → low):
//   [FP+8]  saved LR        (paired STP x29,x30)
//   [FP+0]  saved FP
//   [FP-8]  first local/spill slot
//   ...     more locals / spill slots
//   [SP]    base of outgoing arg area
//
// Key invariants:
//   - All slot offsets are negative (below the frame pointer).
//   - Slots are 8-byte aligned minimum; total frame is 16-byte aligned.
//   - addLocal()/ensureSpill() must precede finalize().
//   - finalize() is idempotent but should be called exactly once.
// Ownership/Lifetime:
//   - Borrows MFunction*; the MFunction must outlive the FrameBuilder.
// Links: codegen/aarch64/FrameBuilder.hpp,
//        codegen/aarch64/RegAllocLinear.cpp (spill usage),
//        codegen/aarch64/AsmEmitter.cpp (prologue/epilogue)
//
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
