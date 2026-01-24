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
#include <memory>
#include <new>
#include <type_traits>
#include <utility>
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

/// @brief Growing arena allocator that allocates additional chunks as needed.
///
/// Unlike the basic Arena which has a fixed capacity, GrowingArena automatically
/// allocates new chunks when the current one is exhausted. This makes it suitable
/// for AST allocation where the total size is not known in advance.
///
/// Objects with non-trivial destructors are tracked and destroyed when the arena
/// is destroyed or reset. For trivially-destructible types, no destruction
/// overhead is incurred.
///
/// @invariant Individual allocations cannot be freed; reset() reclaims all memory.
/// @ownership Owns all allocated chunks and manages object destruction.
class GrowingArena
{
  public:
    /// @brief Create a growing arena with specified initial and growth chunk sizes.
    /// @param initialChunkSize Size of the first chunk in bytes (default 4KB).
    /// @param growthChunkSize Size of subsequent chunks (default 8KB).
    explicit GrowingArena(size_t initialChunkSize = 4096, size_t growthChunkSize = 8192);

    /// @brief Destructor - destroys all tracked objects and frees memory.
    ~GrowingArena();

    /// Non-copyable.
    GrowingArena(const GrowingArena &) = delete;
    GrowingArena &operator=(const GrowingArena &) = delete;

    /// Movable.
    GrowingArena(GrowingArena &&other) noexcept;
    GrowingArena &operator=(GrowingArena &&other) noexcept;

    /// @brief Allocate raw memory with specified alignment.
    /// @param size Number of bytes to allocate.
    /// @param align Alignment requirement (must be power of two).
    /// @return Pointer to allocated memory, never nullptr (throws on failure).
    void *allocate(size_t size, size_t align);

    /// @brief Construct an object of type T in the arena.
    /// @tparam T Type of object to construct.
    /// @tparam Args Constructor argument types.
    /// @param args Constructor arguments forwarded to T's constructor.
    /// @return Pointer to the constructed object (never nullptr).
    /// @details For trivially-destructible types, no tracking overhead is incurred.
    ///          For types with non-trivial destructors, the destructor will be
    ///          called when the arena is destroyed or reset.
    template <typename T, typename... Args> T *create(Args &&...args)
    {
        void *mem = allocate(sizeof(T), alignof(T));
        T *obj = new (mem) T(std::forward<Args>(args)...);

        // Track objects that need destruction
        if constexpr (!std::is_trivially_destructible_v<T>)
        {
            destructors_.push_back({obj, [](void *p) { static_cast<T *>(p)->~T(); }});
        }

        return obj;
    }

    /// @brief Reset the arena, destroying all objects and reclaiming memory.
    /// @details After reset(), the arena can be reused. All previously allocated
    ///          pointers become invalid.
    void reset();

    /// @brief Get total bytes allocated across all chunks.
    [[nodiscard]] size_t totalAllocated() const noexcept;

    /// @brief Get number of chunks allocated.
    [[nodiscard]] size_t chunkCount() const noexcept
    {
        return chunks_.size();
    }

  private:
    /// @brief A memory chunk with bump-pointer allocation.
    struct Chunk
    {
        std::unique_ptr<std::byte[]> data;
        size_t size = 0;
        size_t offset = 0;

        Chunk() = default;

        explicit Chunk(size_t sz) : data(std::make_unique<std::byte[]>(sz)), size(sz), offset(0) {}

        // Move-only
        Chunk(const Chunk &) = delete;
        Chunk &operator=(const Chunk &) = delete;
        Chunk(Chunk &&) noexcept = default;
        Chunk &operator=(Chunk &&) noexcept = default;

        void *tryAllocate(size_t sz, size_t align);
    };

    /// @brief Destructor record for non-trivially-destructible objects.
    struct DestructorRecord
    {
        void *object;
        void (*destroy)(void *);
    };

    /// @brief Allocate a new chunk of at least the given size.
    void allocateChunk(size_t minSize);

    /// @brief Destroy all tracked objects in reverse order.
    void destroyObjects();

    std::vector<Chunk> chunks_;
    std::vector<DestructorRecord> destructors_;
    size_t growthChunkSize_;
};
} // namespace il::support
