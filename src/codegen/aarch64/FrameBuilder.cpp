//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/codegen/aarch64/FrameBuilder.cpp
// Purpose: Implementation of AArch64 frame layout builder for MIR.
//
//===----------------------------------------------------------------------===//

#include "FrameBuilder.hpp"

#include <algorithm>

namespace viper::codegen::aarch64
{

void FrameBuilder::addLocal(unsigned tempId, int sizeBytes, int alignBytes)
{
    // If already present, ignore.
    for (const auto &L : fn_->frame.locals)
        if (L.tempId == tempId)
            return;
    // Assign an aligned slot and record offset.
    const int off = assignAlignedSlot(sizeBytes, alignBytes);
    fn_->frame.locals.push_back(MFunction::StackLocal{tempId, sizeBytes, alignBytes, off});
}

int FrameBuilder::localOffset(unsigned tempId) const
{
    return fn_->frame.getLocalOffset(tempId);
}

int FrameBuilder::ensureSpill(uint16_t vreg, int sizeBytes, int alignBytes)
{
    for (const auto &S : fn_->frame.spills)
        if (S.vreg == vreg)
            return S.offset;
    const int off = assignAlignedSlot(sizeBytes, alignBytes);
    fn_->frame.spills.push_back(MFunction::SpillSlot{vreg, sizeBytes, alignBytes, off});
    return off;
}

void FrameBuilder::setMaxOutgoingBytes(int bytes)
{
    fn_->frame.maxOutgoingBytes = std::max(fn_->frame.maxOutgoingBytes, bytes);
}

void FrameBuilder::finalize()
{
    // Account for any previously assigned locals/spills on the function as well as
    // slots assigned via this builder instance.
    int mostNegative = minOffset_;
    for (const auto &L : fn_->frame.locals)
        mostNegative = std::min(mostNegative, L.offset);
    for (const auto &S : fn_->frame.spills)
        mostNegative = std::min(mostNegative, S.offset);
    int usedBytes = -mostNegative;
    // Account for FP-LR area implicitly; our offsets start at -8, so usedBytes already counts slots.
    // Add any reserved outgoing-arg area.
    usedBytes += fn_->frame.maxOutgoingBytes;
    // Round up to 16-byte alignment.
    if (usedBytes % kStackAlignment != 0)
        usedBytes = (usedBytes + (kStackAlignment - 1)) & ~(kStackAlignment - 1);
    fn_->frame.totalBytes = usedBytes;
    fn_->localFrameSize = usedBytes; // bridge for current emitter plan field
}

int FrameBuilder::assignAlignedSlot(int sizeBytes, int alignBytes)
{
    // Align the nextOffset downwards to the required alignment, then assign size.
    const int align = std::max(1, alignBytes);
    // Move nextOffset to the nearest multiple of 'align'.
    int aligned = nextOffset_;
    const int mod = (-aligned) % align; // since aligned is negative
    if (mod != 0)
        aligned -= (align - mod);
    const int offset = aligned;
    nextOffset_ = offset - std::max(8, sizeBytes); // allocate at least 8 bytes per slot
    minOffset_ = std::min(minOffset_, offset);
    return offset;
}

} // namespace viper::codegen::aarch64
