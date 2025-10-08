//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the bump-pointer arena used throughout Viper's support library.
// The arena manages a single contiguous buffer and satisfies allocation
// requests by monotonically advancing an offset.  Callers can request memory
// with an explicit alignment and the arena will compute the correct aligned
// address while guarding against overflow.  All allocations remain valid until
// `reset()` is invoked, at which point the arena reuses the entire buffer.
//
//===----------------------------------------------------------------------===//

#include "arena.hpp"

#include <cstdint>
#include <limits>

namespace il::support
{
/// @brief Construct an arena that manages a fixed-capacity backing buffer.
///
/// The constructor initializes the internal byte vector with @p size elements
/// and sets the bump pointer to the start of the buffer.  No allocation occurs
/// beyond reserving the storage owned by the vector.
///
/// @param size Number of bytes reserved for subsequent allocation requests.
Arena::Arena(size_t size) : buffer_(size) {}

/// @brief Allocate memory from the arena honoring the requested alignment.
///
/// The allocator performs several overflow checks while computing the aligned
/// pointer to ensure the arithmetic remains within platform limits.  The method
/// rejects zero or non power-of-two alignments, advances the internal bump
/// pointer when successful, and returns @c nullptr if the request cannot be
/// satisfied either because of insufficient capacity or arithmetic overflow.
///
/// @param size Number of bytes to allocate from the arena.
/// @param align Alignment requirement in bytes; must be a non-zero power of two.
/// @return Pointer to aligned memory on success, or nullptr on failure.
void *Arena::allocate(size_t size, size_t align)
{
    // Reject zero or non power-of-two alignments.
    if (align == 0 || (align & (align - 1)) != 0)
        return nullptr;

    const size_t current = offset_;
    const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(buffer_.data());
    if (current > std::numeric_limits<std::uintptr_t>::max() - base)
        return nullptr;

    const std::uintptr_t current_ptr = base + current;
    const std::uintptr_t mask = static_cast<std::uintptr_t>(align - 1);

    if (current_ptr > std::numeric_limits<std::uintptr_t>::max() - mask)
        return nullptr;

    const std::uintptr_t aligned_ptr = (current_ptr + mask) & ~mask;
    const size_t adjustment = static_cast<size_t>(aligned_ptr - current_ptr);

    if (adjustment > std::numeric_limits<size_t>::max() - current)
        return nullptr;

    const size_t aligned_offset = current + adjustment;

    if (size > std::numeric_limits<size_t>::max() - aligned_offset)
        return nullptr;

    const size_t new_offset = aligned_offset + size;

    if (aligned_offset > buffer_.size() || size > buffer_.size() - aligned_offset)
        return nullptr;

    if (new_offset > buffer_.size())
        return nullptr;

    offset_ = new_offset;
    return buffer_.data() + aligned_offset;
}

/// @brief Reset the arena to reuse the entire buffer for future allocations.
///
/// Clearing the bump pointer invalidates all outstanding allocations because
/// subsequent requests will begin writing from the start of the buffer again.
/// This is intended for short-lived allocations where the caller controls the
/// lifetime of the data stored in the arena.
void Arena::reset()
{
    offset_ = 0;
}
} // namespace il::support
