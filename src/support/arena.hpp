//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: support/arena.hpp
// Purpose: Declares bump allocator for temporary objects. 
// Key invariants: None.
// Ownership/Lifetime: Arena owns all allocated memory.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <vector>

namespace il::support
{
/// @brief Simple bump allocator for fast allocations.
///
/// Uses a contiguous internal buffer and a bump-pointer strategy to satisfy
/// allocation requests. Each call to allocate() advances the current position
/// in the buffer by the requested size and alignment. Individual allocations
/// cannot be freed; invoke reset() to make the entire buffer reusable.
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
    /// @return Pointer to allocated memory or nullptr on failure.
    /// @notes Fails if @p align is zero, not a power of two, or capacity exceeded.
    void *allocate(size_t size, size_t align);

    /// @brief Reset arena, making all allocations available again.
    void reset();

  private:
    /// Backing storage for all allocations; owned by the arena.
    std::vector<std::byte> buffer_;
    /// Current offset within buffer_ for the next allocation.
    size_t offset_ = 0;
};
} // namespace il::support
