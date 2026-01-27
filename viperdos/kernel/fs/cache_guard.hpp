//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
#pragma once

#include "cache.hpp"

/**
 * @file cache_guard.hpp
 * @brief RAII wrapper for filesystem cache blocks.
 *
 * @details
 * The CacheBlockGuard class provides automatic release of cache blocks
 * acquired via BlockCache::get() or BlockCache::get_for_write(). This ensures
 * blocks are properly released even when early returns or errors occur.
 *
 * Usage:
 * @code
 * CacheBlockGuard guard(cache().get(block_num));
 * if (!guard) {
 *     return false;  // Cache miss or error
 * }
 * // Use guard.get() or guard-> to access the block
 * // Block is automatically released when guard goes out of scope
 * @endcode
 */
namespace fs {

/**
 * @brief RAII guard for cache block pointers.
 *
 * @details
 * Takes ownership of a CacheBlock* and calls cache().release() on destruction.
 * Non-copyable, move-only semantics.
 */
class CacheBlockGuard {
  public:
    /// Default constructor - creates null guard
    CacheBlockGuard() : block_(nullptr) {}

    /// Construct from raw block pointer, taking ownership
    explicit CacheBlockGuard(CacheBlock *block) : block_(block) {}

    /// Move constructor
    CacheBlockGuard(CacheBlockGuard &&other) noexcept : block_(other.block_) {
        other.block_ = nullptr;
    }

    /// Destructor - releases the block
    ~CacheBlockGuard() {
        if (block_) {
            cache().release(block_);
        }
    }

    // Non-copyable
    CacheBlockGuard(const CacheBlockGuard &) = delete;
    CacheBlockGuard &operator=(const CacheBlockGuard &) = delete;

    /// Move assignment
    CacheBlockGuard &operator=(CacheBlockGuard &&other) noexcept {
        if (this != &other) {
            if (block_) {
                cache().release(block_);
            }
            block_ = other.block_;
            other.block_ = nullptr;
        }
        return *this;
    }

    /// Reset to a new block (releases old)
    void reset(CacheBlock *block = nullptr) {
        if (block_) {
            cache().release(block_);
        }
        block_ = block;
    }

    /// Release ownership and return raw pointer
    CacheBlock *release() {
        CacheBlock *ptr = block_;
        block_ = nullptr;
        return ptr;
    }

    /// Dereference operators
    CacheBlock *operator->() const {
        return block_;
    }

    CacheBlock &operator*() const {
        return *block_;
    }

    /// Get raw pointer
    CacheBlock *get() const {
        return block_;
    }

    /// Get block data directly
    u8 *data() const {
        return block_ ? block_->data : nullptr;
    }

    /// Mark block as dirty
    void mark_dirty() {
        if (block_)
            block_->dirty = true;
    }

    /// Boolean conversion
    explicit operator bool() const {
        return block_ != nullptr;
    }

  private:
    CacheBlock *block_;
};

} // namespace fs
