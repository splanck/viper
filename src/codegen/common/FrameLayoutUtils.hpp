//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/common/FrameLayoutUtils.hpp
// Purpose: Shared helpers for stack-slot allocation and frame-size alignment.
//
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <cassert>
#include <cstddef>

namespace viper::codegen::common {

/// @brief Round @p value up to the next multiple of @p align.
[[nodiscard]] inline int roundUpBytes(int value, int align) noexcept {
    assert(align > 0 && "alignment must be positive");
    const int remainder = value % align;
    if (remainder == 0)
        return value;
    return value + (align - remainder);
}

/// @brief Round @p value up to the next multiple of @p align.
[[nodiscard]] inline std::size_t roundUpBytes(std::size_t value, std::size_t align) noexcept {
    assert(align > 0 && "alignment must be positive");
    const std::size_t remainder = value % align;
    if (remainder == 0)
        return value;
    return value + (align - remainder);
}

/// @brief Convert a byte count into fixed-size stack slots.
[[nodiscard]] inline int bytesToSlots(int bytes, int slotBytes) noexcept {
    assert(slotBytes > 0 && "slot size must be positive");
    const int clampedBytes = std::max(1, bytes);
    return roundUpBytes(clampedBytes, slotBytes) / slotBytes;
}

/// @brief Result of reserving one stack object in a downward-growing frame.
struct DownwardFrameSlot {
    int offset{0};        ///< Negative frame-pointer-relative base offset.
    int reservedBytes{0}; ///< Total bytes reserved for the slot, including padding.
};

/// @brief Tracks reserved bytes for a downward-growing frame.
///
/// Offsets returned by allocate() point at the lowest address of the reserved
/// slot, which is the correct base pointer for stack-allocated objects.
class DownwardFrameCursor {
  public:
    explicit DownwardFrameCursor(int minSlotBytes = 1) noexcept
        : minSlotBytes_(std::max(1, minSlotBytes)) {}

    /// @brief Seed the cursor from an already-assigned negative slot offset.
    void seedFromOffset(int offset) noexcept {
        if (offset < 0)
            usedBytes_ = std::max(usedBytes_, -offset);
    }

    /// @brief Reserve a new slot and return its frame-relative base offset.
    [[nodiscard]] DownwardFrameSlot allocate(int sizeBytes, int alignBytes) noexcept {
        const int effectiveSize = std::max(minSlotBytes_, sizeBytes);
        const int effectiveAlign = std::max(1, alignBytes);
        const int alignedStart = roundUpBytes(usedBytes_, effectiveAlign);
        usedBytes_ = alignedStart + effectiveSize;
        return DownwardFrameSlot{.offset = -usedBytes_, .reservedBytes = effectiveSize};
    }

    /// @brief Return the total bytes reserved so far.
    [[nodiscard]] int usedBytes() const noexcept {
        return usedBytes_;
    }

  private:
    int usedBytes_{0};
    int minSlotBytes_{1};
};

} // namespace viper::codegen::common
