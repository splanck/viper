//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
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

/// @file
/// @brief Defines the `il::support::Arena` bump allocator implementation.
/// @details The arena backs many short-lived allocations inside the compiler's
///          support layer.  It owns a contiguous byte buffer, hands out aligned
///          slices on demand, and exposes a single `reset()` entry point that
///          rewinds the allocation cursor.  The design deliberately avoids
///          deallocation of individual blocks so call sites can trade off
///          lifetime tracking for speed when building transient data structures.

#include "arena.hpp"

#include <cstdint>
#include <limits>

namespace il::support
{
/// @brief Construct an arena that manages a fixed-capacity backing buffer.
///
/// @details The constructor initializes the internal byte vector with @p size
///          elements and places the bump pointer at the start of the buffer.
///          This guarantees that the first allocation returns the first byte in
///          the buffer while subsequent allocations advance the pointer.  No
///          dynamic allocation occurs beyond reserving the storage owned by the
///          vector, keeping construction cheap enough to use on the stack.
///
/// @param size Number of bytes reserved for subsequent allocation requests.
Arena::Arena(size_t size) : buffer_(size) {}

/// @brief Allocate memory from the arena honoring the requested alignment.
///
/// @details Allocation proceeds in a handful of steps:
///          1. Validate that @p align is a non-zero power of two to keep
///             bit-mask alignment logic well defined.
///          2. Compute the aligned pointer relative to the arena's base while
///             guarding every arithmetic operation against overflow.
///          3. Ensure the new allocation fits inside the backing buffer.
///          4. Advance the bump pointer and return the aligned slice.
///          Failure at any stage returns @c nullptr without mutating state so
///          callers can attempt fallbacks.  This is sufficient for the compiler
///          where allocation failure typically signals an out-of-memory
///          condition.
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
/// @details Clearing the bump pointer invalidates all outstanding allocations
///          because subsequent requests begin writing from the start of the
///          buffer.  Callers typically pair this with stack allocation of the
///          arena so reclamation happens deterministically at scope exit after a
///          full phase of compilation completes.
void Arena::reset()
{
    offset_ = 0;
}
} // namespace il::support
