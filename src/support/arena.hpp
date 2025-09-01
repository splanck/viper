// File: src/support/arena.hpp
// Purpose: Declares bump allocator for temporary objects.
// Key invariants: None.
// Ownership/Lifetime: Arena owns all allocated memory.
// Links: docs/class-catalog.md
#pragma once

#include <cstddef>
#include <vector>

namespace il::support
{

/// @brief Simple bump allocator for fast allocations.
/// @invariant Allocations are not individually freed; use reset() to reuse.
/// @ownership Owns its internal buffer.
class Arena
{
  public:
    /// @brief Create arena with @p size bytes of storage.
    /// @param size Capacity in bytes.
    explicit Arena(size_t size);

    /// @brief Allocate @p size bytes with alignment @p align.
    /// @param size Number of bytes to allocate.
    /// @param align Alignment requirement.
    /// @return Pointer to allocated memory within the arena.
    void *allocate(size_t size, size_t align);

    /// @brief Reset arena, making all allocations available again.
    void reset();

  private:
    std::vector<std::byte> buffer_;
    size_t offset_ = 0;
};
} // namespace il::support
