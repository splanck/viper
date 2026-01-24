//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: kernel/mm/kheap.hpp
// Purpose: Kernel heap allocator with free list support.
// Key invariants: All allocations 16-byte aligned; free list coalesced.
// Ownership/Lifetime: Global singleton; provides new/delete operators.
// Links: kernel/mm/kheap.cpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "../include/types.hpp"

/**
 * @file kheap.hpp
 * @brief Kernel heap allocator with free list support.
 *
 * @details
 * The kernel heap provides dynamic allocation facilities using a free-list
 * based allocator. Each allocation includes an 8-byte header storing the
 * block size, enabling proper freeing and coalescing.
 *
 * ## Allocation Strategy
 *
 * - First-fit free list search for allocations
 * - Immediate coalescing of adjacent free blocks on free
 * - Minimum block size of 32 bytes (including header)
 * - 16-byte alignment for all allocations
 *
 * ## Thread Safety
 *
 * The allocator uses a spinlock to protect the free list, making it safe
 * for concurrent use from multiple contexts (though interrupt handlers
 * should avoid allocation when possible).
 *
 * This header also declares global C++ allocation operators so that kernel
 * code can use `new`/`delete` while routing allocations through the kernel
 * heap implementation.
 */
namespace kheap {

/**
 * @brief Initialize the kernel heap allocator.
 *
 * @details
 * Obtains an initial backing region from the PMM and initializes the free
 * list. This should be called after @ref pmm::init.
 */
void init();

/**
 * @brief Allocate memory from the kernel heap.
 *
 * @details
 * Allocates `size` bytes from the heap and returns a pointer. The allocator
 * aligns allocations to a 16-byte boundary. If there is insufficient space,
 * the heap attempts to expand by allocating additional pages from the PMM.
 *
 * @param size Number of bytes to allocate.
 * @return Pointer to allocated memory, or `nullptr` on failure.
 */
void *kmalloc(u64 size);

/**
 * @brief Allocate zero-initialized memory from the kernel heap.
 *
 * @details
 * Allocates `size` bytes as with @ref kmalloc, then clears the memory to zero.
 *
 * @param size Number of bytes to allocate and clear.
 * @return Pointer to allocated memory, or `nullptr` on failure.
 */
void *kzalloc(u64 size);

/**
 * @brief Reallocate memory to a new size.
 *
 * @details
 * Allocates a new block of `new_size` bytes, copies the contents of the old
 * block (up to the minimum of old and new sizes), and frees the old block.
 *
 * @param ptr Pointer to existing allocation (or nullptr for new allocation).
 * @param new_size New size in bytes.
 * @return Pointer to reallocated memory, or nullptr on failure.
 */
void *krealloc(void *ptr, u64 new_size);

/**
 * @brief Free previously allocated heap memory.
 *
 * @details
 * Returns the memory to the free list and attempts to coalesce with adjacent
 * free blocks. Passing nullptr is a safe no-op.
 *
 * @param ptr Pointer returned by @ref kmalloc/@ref kzalloc/@ref krealloc.
 */
void kfree(void *ptr);

/**
 * @brief Get the total bytes allocated from the heap.
 * @return Total allocated bytes (including headers and fragmentation).
 */
u64 get_used();

/**
 * @brief Get the total bytes in free blocks.
 * @return Total free bytes available.
 */
u64 get_available();

/**
 * @brief Get heap statistics.
 * @param out_total_size Total heap size in bytes.
 * @param out_used Used bytes.
 * @param out_free Free bytes.
 * @param out_free_blocks Number of free list blocks.
 */
void get_stats(u64 *out_total_size, u64 *out_used, u64 *out_free, u64 *out_free_blocks);

/**
 * @brief Dump heap state for debugging.
 */
void dump();

} // namespace kheap

// size_t for operator new/delete
using size_t = decltype(sizeof(0));

// C++ new/delete operators
/**
 * @brief Global `operator new` routed to the kernel heap.
 */
void *operator new(size_t size);

/**
 * @brief Global `operator new[]` routed to the kernel heap.
 */
void *operator new[](size_t size);

/**
 * @brief Global `operator delete` routed to the kernel heap.
 */
void operator delete(void *ptr) noexcept;

/**
 * @brief Global `operator delete[]` routed to the kernel heap.
 */
void operator delete[](void *ptr) noexcept;

/**
 * @brief Sized `operator delete` (C++14+).
 */
void operator delete(void *ptr, size_t size) noexcept;

/**
 * @brief Sized `operator delete[]` (C++14+).
 */
void operator delete[](void *ptr, size_t size) noexcept;

// =============================================================================
// RAII Wrappers for Heap Memory
// =============================================================================

namespace kheap {

/**
 * @brief Simple unique pointer for kernel heap allocations.
 *
 * @details
 * Provides RAII semantics for memory allocated via kheap::kmalloc.
 * When the UniquePtr goes out of scope, the memory is automatically freed.
 * This is useful for temporary buffers and objects not managed by the
 * capability system.
 *
 * Usage:
 * @code
 * UniquePtr<u8> buffer(static_cast<u8*>(kheap::kmalloc(1024)));
 * // Use buffer.get() to access the memory
 * // Automatically freed when buffer goes out of scope
 * @endcode
 *
 * @tparam T Type of the pointed-to object.
 */
template <typename T> class UniquePtr {
  public:
    /// Default constructor - creates a null pointer
    UniquePtr() : ptr_(nullptr) {}

    /// Construct from raw pointer, taking ownership
    explicit UniquePtr(T *ptr) : ptr_(ptr) {}

    /// Move constructor - steals ownership
    UniquePtr(UniquePtr &&other) noexcept : ptr_(other.ptr_) {
        other.ptr_ = nullptr;
    }

    /// Destructor - frees the memory
    ~UniquePtr() {
        if (ptr_)
            kfree(ptr_);
    }

    // Non-copyable
    UniquePtr(const UniquePtr &) = delete;
    UniquePtr &operator=(const UniquePtr &) = delete;

    /// Move assignment
    UniquePtr &operator=(UniquePtr &&other) noexcept {
        if (this != &other) {
            if (ptr_)
                kfree(ptr_);
            ptr_ = other.ptr_;
            other.ptr_ = nullptr;
        }
        return *this;
    }

    /// Reset to a new pointer (frees old memory)
    void reset(T *ptr = nullptr) {
        if (ptr_)
            kfree(ptr_);
        ptr_ = ptr;
    }

    /// Release ownership and return raw pointer
    T *release() {
        T *ptr = ptr_;
        ptr_ = nullptr;
        return ptr;
    }

    /// Dereference operators
    T *operator->() const {
        return ptr_;
    }

    T &operator*() const {
        return *ptr_;
    }

    /// Get raw pointer
    T *get() const {
        return ptr_;
    }

    /// Boolean conversion
    explicit operator bool() const {
        return ptr_ != nullptr;
    }

  private:
    T *ptr_;
};

/**
 * @brief Create a UniquePtr to a zero-initialized allocation.
 *
 * @tparam T Type of the allocation.
 * @param size Size in bytes (defaults to sizeof(T)).
 * @return UniquePtr owning the allocation, or null on failure.
 */
template <typename T> UniquePtr<T> make_unique(u64 size = sizeof(T)) {
    return UniquePtr<T>(static_cast<T *>(kzalloc(size)));
}

} // namespace kheap
