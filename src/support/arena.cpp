// File: src/support/arena.cpp
// Purpose: Implements bump allocator for short-lived objects.
// Key invariants: None.
// Ownership/Lifetime: Arena owns allocated memory until reset.
// Links: docs/class-catalog.md

#include "arena.hpp"

namespace il::support
{
/// @brief Construct an arena with a fixed capacity.
/// @param size Number of bytes reserved for allocations.
Arena::Arena(size_t size) : buffer_(size) {}

/// @brief Allocate memory with a specified alignment.
/// @param size Number of bytes to allocate.
/// @param align Alignment in bytes; must be a non-zero power of two.
/// @return Pointer to aligned memory or nullptr on failure.
/// @note Fails if alignment is invalid or if remaining capacity is insufficient.
void *Arena::allocate(size_t size, size_t align)
{
    // Reject zero or non power-of-two alignments.
    if (align == 0 || (align & (align - 1)) != 0)
        return nullptr;

    size_t current = offset_;
    size_t aligned = (current + align - 1) & ~(align - 1);
    if (aligned + size > buffer_.size())
        return nullptr;
    offset_ = aligned + size;
    return buffer_.data() + aligned;
}

/// @brief Reset the arena, allowing memory to be reused.
/// @note All previously returned pointers become invalid after this call.
void Arena::reset()
{
    offset_ = 0;
}
} // namespace il::support
