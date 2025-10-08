/**
 * @file arena.cpp
 * @brief Implements a simple bump-pointer arena for short-lived allocations.
 * @copyright
 *     MIT License. See the LICENSE file in the project root for full terms.
 * @details
 *     The arena owns a contiguous byte buffer and hands out aligned slices using
 *     a monotonically increasing offset.  Callers are expected to release the
 *     memory in bulk via `reset()` rather than individual frees.
 */

#include "arena.hpp"

#include <cstdint>
#include <limits>

namespace il::support
{
/**
 * @brief Creates an arena with the requested capacity in bytes.
 *
 * The constructor allocates a backing vector whose size defines the maximum
 * amount of memory the arena can hand out before requiring a reset.
 *
 * @param size Capacity, in bytes, of the bump buffer.
 */
Arena::Arena(size_t size) : buffer_(size) {}

/**
 * @brief Allocates a slice of memory with the specified alignment.
 *
 * The function validates that the alignment is a non-zero power of two and that
 * the allocation fits within the remaining buffer space.  It computes an
 * aligned offset from the current bump pointer, checks for overflow at each
 * step, and advances the pointer if sufficient capacity exists.  Failure at any
 * validation step results in a `nullptr` return without modifying the arena.
 *
 * @param size Number of bytes to allocate.
 * @param align Alignment requirement in bytes.
 * @return Pointer to the aligned storage on success; otherwise `nullptr`.
 */
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

/**
 * @brief Resets the arena to its initial state, invalidating outstanding pointers.
 *
 * Resetting simply rewinds the bump pointer to the beginning of the buffer.
 * Subsequent allocations reuse the same storage without reallocating.
 */
void Arena::reset()
{
    offset_ = 0;
}
} // namespace il::support
