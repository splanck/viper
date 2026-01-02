//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: kernel/fs/cache.hpp
// Purpose: Simple block cache for filesystem I/O.
// Key invariants: 4KB blocks; LRU eviction; refcount-protected.
// Ownership/Lifetime: Global singleton; blocks ref-counted.
// Links: kernel/fs/cache.cpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "../include/types.hpp"

namespace fs
{

/**
 * @file cache.hpp
 * @brief Simple block cache for filesystem I/O.
 *
 * @details
 * The filesystem stack frequently reads and writes fixed-size blocks from the
 * underlying block device. To avoid excessive device I/O, ViperOS uses a small
 * in-memory cache of recently accessed blocks.
 *
 * The cache:
 * - Stores blocks of size @ref BLOCK_SIZE.
 * - Uses a fixed pool of @ref CACHE_BLOCKS entries (no dynamic allocation).
 * - Tracks blocks via a hash table for lookup and an LRU list for eviction.
 * - Supports write-back behavior by marking blocks dirty and syncing them later.
 *
 * The design is intentionally simple for bring-up and assumes cooperative
 * single-threaded access; there is no locking for SMP.
 */

/** @brief Block size used by the filesystem cache (4 KiB). */
constexpr usize BLOCK_SIZE = 4096;

/** @brief Number of cached blocks in the global block cache. */
constexpr usize CACHE_BLOCKS = 64; // 256KB cache

/** @brief Number of blocks to prefetch on sequential reads. */
constexpr usize READ_AHEAD_BLOCKS = 4;

/**
 * @brief One cached block of filesystem data.
 *
 * @details
 * The block cache stores blocks by logical block number and tracks:
 * - Validity and dirty status.
 * - A reference count to prevent eviction while in use.
 * - LRU pointers for eviction ordering.
 * - Hash chain pointer for fast lookup.
 */
struct CacheBlock
{
    u64 block_num;         // Block number on disk (sector / 8)
    u8 data[BLOCK_SIZE];   // Block data
    bool valid;            // Data is valid
    bool dirty;            // Data modified, needs write-back
    bool pinned;           // Block is pinned (cannot be evicted)
    u32 refcount;          // Reference count
    CacheBlock *lru_prev;  // LRU list previous
    CacheBlock *lru_next;  // LRU list next
    CacheBlock *hash_next; // Hash chain next
};

// Block cache
/**
 * @brief LRU block cache with a fixed-size backing store.
 *
 * @details
 * Callers obtain a block pointer via @ref get or @ref get_for_write. Each get
 * increments the block refcount; callers must call @ref release when done to
 * allow eviction.
 *
 * Dirty blocks are written back via @ref sync or opportunistically before
 * eviction. The cache does not currently flush on every write for performance.
 */
class BlockCache
{
  public:
    /**
     * @brief Initialize the cache structures.
     *
     * @details
     * Marks all blocks invalid, sets up the LRU list, clears the hash table and
     * statistics counters.
     *
     * @return `true` on success.
     */
    bool init();

    // Get a block (loads from disk if needed)
    // Increments refcount - must call release() when done
    /**
     * @brief Get a cached block by number, loading it from disk if necessary.
     *
     * @details
     * On a cache hit, increments the block refcount and updates LRU position.
     * On a miss, evicts an LRU block with refcount 0 (writing back if dirty),
     * reads the requested block from disk, inserts it into the hash table, and
     * returns it with refcount 1.
     *
     * @param block_num Logical block number.
     * @return Pointer to the cache block, or `nullptr` on I/O or eviction failure.
     */
    CacheBlock *get(u64 block_num);

    // Get a block for writing (marks dirty)
    /**
     * @brief Get a block intended to be modified.
     *
     * @details
     * Equivalent to @ref get but also marks the block dirty so it will be
     * written back by @ref sync or during eviction.
     *
     * @param block_num Logical block number.
     * @return Pointer to cache block, or `nullptr` on failure.
     */
    CacheBlock *get_for_write(u64 block_num);

    // Release a block (decrements refcount)
    /**
     * @brief Release a previously acquired block.
     *
     * @details
     * Decrements the refcount, allowing the block to be evicted when it reaches
     * zero.
     *
     * @param block Cache block pointer (may be `nullptr`).
     */
    void release(CacheBlock *block);

    // Sync all dirty blocks to disk
    /**
     * @brief Write back all dirty blocks to the underlying device.
     *
     * @details
     * Iterates the cache and calls @ref sync_block for each dirty valid block.
     */
    void sync();

    // Sync a specific block if dirty
    /**
     * @brief Write back one block if it is dirty.
     *
     * @param block Cache block pointer.
     */
    void sync_block(CacheBlock *block);

    // Invalidate a block (for unmount)
    /**
     * @brief Invalidate a cached block, optionally writing it back if dirty.
     *
     * @details
     * Used during unmount or when external metadata changes require discarding
     * cached contents.
     *
     * @param block_num Logical block number to invalidate.
     */
    void invalidate(u64 block_num);

    // Statistics
    /** @brief Number of cache hits since initialization. */
    u64 hits() const
    {
        return hits_;
    }

    /** @brief Number of cache misses since initialization. */
    u64 misses() const
    {
        return misses_;
    }

    /** @brief Number of read-ahead blocks loaded. */
    u64 readahead_count() const
    {
        return readahead_count_;
    }

    /**
     * @brief Dump cache statistics to serial console.
     *
     * @details
     * Prints hit/miss counts, hit rate, and current cache utilization.
     */
    void dump_stats();

    /**
     * @brief Pin a block in cache (prevent eviction).
     *
     * @details
     * Pinned blocks remain in cache until explicitly unpinned.
     * Use for critical metadata like superblock and inode table.
     *
     * @param block_num Block number to pin.
     * @return true if block was pinned successfully.
     */
    bool pin(u64 block_num);

    /**
     * @brief Unpin a previously pinned block.
     *
     * @param block_num Block number to unpin.
     */
    void unpin(u64 block_num);

  private:
    /**
     * @brief Prefetch upcoming blocks in the background.
     *
     * @details
     * Called after a cache miss when sequential access is detected.
     * Loads the next READ_AHEAD_BLOCKS blocks into the cache if they
     * are not already present. This is done without incrementing refcount
     * so the blocks can be evicted if needed.
     *
     * @param block_num Starting block number for read-ahead.
     */
    void read_ahead(u64 block_num);

    /**
     * @brief Load a block into cache without incrementing refcount.
     *
     * @details
     * Used by read_ahead to prefetch blocks. Must be called with
     * cache_lock held.
     *
     * @param block_num Block number to load.
     * @return true if block was loaded or already cached.
     */
    bool prefetch_block(u64 block_num);
    CacheBlock blocks_[CACHE_BLOCKS];

    // LRU list (head = most recently used)
    CacheBlock *lru_head_{nullptr};
    CacheBlock *lru_tail_{nullptr};

    // Hash table for fast lookup
    static constexpr usize HASH_SIZE = 32;
    CacheBlock *hash_[HASH_SIZE];

    // Statistics
    u64 hits_{0};
    u64 misses_{0};
    u64 readahead_count_{0};

    // Sequential access tracking
    u64 last_block_{0};

    // Internal helpers
    /** @brief Hash a block number into the lookup table index. */
    u32 hash_func(u64 block_num);
    /** @brief Find a cached block by block number, or nullptr if not present. */
    CacheBlock *find(u64 block_num);
    /** @brief Evict an LRU block with refcount 0 and return it for reuse. */
    CacheBlock *evict();
    /** @brief Mark a block most-recently-used by moving it to LRU head. */
    void touch(CacheBlock *block);
    /** @brief Remove a block from the LRU list. */
    void remove_from_lru(CacheBlock *block);
    /** @brief Insert a block at the LRU head. */
    void add_to_lru_head(CacheBlock *block);
    /** @brief Insert a valid block into the hash table. */
    void insert_hash(CacheBlock *block);
    /** @brief Remove a block from the hash table. */
    void remove_hash(CacheBlock *block);

    // Disk I/O
    /** @brief Read a logical block from the underlying block device. */
    bool read_block(u64 block_num, void *buf);
    /** @brief Write a logical block to the underlying block device. */
    bool write_block(u64 block_num, const void *buf);
};

// Global cache instance
/**
 * @brief Get the global block cache instance.
 *
 * @return Reference to the global cache.
 */
BlockCache &cache();

// Initialize the cache subsystem
/**
 * @brief Initialize the global cache instance.
 *
 * @details
 * Convenience wrapper for `cache().init()`.
 */
void cache_init();

} // namespace fs
