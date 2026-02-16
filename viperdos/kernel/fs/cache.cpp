//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
#include "cache.hpp"
#include "../console/serial.hpp"
#include "../drivers/virtio/blk.hpp"
#include "../lib/lru_list.hpp"
#include "../lib/spinlock.hpp"

/**
 * @file cache.cpp
 * @brief Block cache implementation.
 *
 * @details
 * Implements a small fixed-size LRU cache for filesystem blocks backed by the
 * virtio block device. Blocks are indexed by logical block number and cached in
 * memory to reduce device I/O.
 *
 * Eviction uses an LRU list and respects a per-block reference count to avoid
 * evicting blocks that are currently in use by callers.
 */
namespace fs {

// Cache lock for thread-safe access
static Spinlock cache_lock;

// Global cache instance (system disk)
static BlockCache g_cache;
static bool g_cache_initialized = false;

// User disk cache instance
static BlockCache g_user_cache;
static bool g_user_cache_initialized = false;
static Spinlock user_cache_lock;

/** @copydoc fs::cache */
BlockCache &cache() {
    return g_cache;
}

/** @copydoc fs::cache_init */
void cache_init() {
    SpinlockGuard guard(cache_lock);
    if (g_cache.init()) {
        g_cache_initialized = true;
    }
}

/** @copydoc fs::user_cache */
BlockCache &user_cache() {
    return g_user_cache;
}

/** @copydoc fs::user_cache_init */
void user_cache_init() {
    SpinlockGuard guard(user_cache_lock);
    auto *user_blk = ::virtio::user_blk_device();
    if (user_blk && g_user_cache.init(user_blk)) {
        g_user_cache_initialized = true;
        serial::puts("[cache] User disk cache initialized\n");
    }
}

/** @copydoc fs::user_cache_available */
bool user_cache_available() {
    return g_user_cache_initialized;
}

/** @copydoc fs::BlockCache::init */
bool BlockCache::init() {
    // Use default system block device
    return init(nullptr);
}

bool BlockCache::init(::virtio::BlkDevice *device) {
    serial::puts("[cache] Initializing block cache...\n");

    // Store device pointer (nullptr = use default blk_device())
    device_ = device;

    // Initialize all blocks as invalid
    for (usize i = 0; i < CACHE_BLOCKS; i++) {
        blocks_[i].block_num = 0;
        blocks_[i].valid = false;
        blocks_[i].dirty = false;
        blocks_[i].pinned = false;
        blocks_[i].refcount = 0;
        blocks_[i].lru_prev = nullptr;
        blocks_[i].lru_next = nullptr;
        blocks_[i].hash_next = nullptr;
    }

    // Initialize hash table
    for (usize i = 0; i < HASH_SIZE; i++) {
        hash_[i] = nullptr;
    }

    // Initialize LRU list (all blocks in order)
    lru_head_ = &blocks_[0];
    lru_tail_ = &blocks_[CACHE_BLOCKS - 1];

    for (usize i = 0; i < CACHE_BLOCKS; i++) {
        blocks_[i].lru_prev = (i > 0) ? &blocks_[i - 1] : nullptr;
        blocks_[i].lru_next = (i < CACHE_BLOCKS - 1) ? &blocks_[i + 1] : nullptr;
    }

    hits_ = 0;
    misses_ = 0;

    serial::puts("[cache] Block cache initialized: ");
    serial::put_dec(CACHE_BLOCKS);
    serial::puts(" blocks (");
    serial::put_dec(CACHE_BLOCKS * BLOCK_SIZE / 1024);
    serial::puts(" KB)\n");

    return true;
}

/** @copydoc fs::BlockCache::hash_func */
u32 BlockCache::hash_func(u64 block_num) {
    return block_num % HASH_SIZE;
}

/** @copydoc fs::BlockCache::find */
CacheBlock *BlockCache::find(u64 block_num) {
    u32 h = hash_func(block_num);
    CacheBlock *b = hash_[h];

    while (b) {
        if (b->valid && b->block_num == block_num) {
            return b;
        }
        b = b->hash_next;
    }

    return nullptr;
}

CacheBlock *BlockCache::find_any(u64 block_num) {
    u32 h = hash_func(block_num);
    CacheBlock *b = hash_[h];

    while (b) {
        if (b->block_num == block_num) {
            return b;
        }
        b = b->hash_next;
    }

    return nullptr;
}

/** @copydoc fs::BlockCache::remove_from_lru */
void BlockCache::remove_from_lru(CacheBlock *block) {
    lib::lru_remove(block, lru_head_, lru_tail_);
}

/** @copydoc fs::BlockCache::add_to_lru_head */
void BlockCache::add_to_lru_head(CacheBlock *block) {
    lib::lru_add_head(block, lru_head_, lru_tail_);
}

/** @copydoc fs::BlockCache::touch */
void BlockCache::touch(CacheBlock *block) {
    lib::lru_touch(block, lru_head_, lru_tail_);
}

/** @copydoc fs::BlockCache::insert_hash */
void BlockCache::insert_hash(CacheBlock *block) {
    u32 h = hash_func(block->block_num);
    block->hash_next = hash_[h];
    hash_[h] = block;
}

/** @copydoc fs::BlockCache::remove_hash */
void BlockCache::remove_hash(CacheBlock *block) {
    u32 h = hash_func(block->block_num);
    CacheBlock **pp = &hash_[h];

    while (*pp) {
        if (*pp == block) {
            *pp = block->hash_next;
            block->hash_next = nullptr;
            return;
        }
        pp = &(*pp)->hash_next;
    }
}

/** @copydoc fs::BlockCache::evict */
CacheBlock *BlockCache::evict() {
    // Find LRU block with refcount 0 that is not pinned
    CacheBlock *block = lru_tail_;

    while (block) {
        if (block->refcount == 0 && !block->pinned) {
            // Found a candidate
            if (block->valid && block->dirty) {
                // Write back dirty block before evicting
                sync_block(block);
            }

            // Remove from hash if valid
            if (block->valid) {
                remove_hash(block);
            }

            return block;
        }
        block = block->lru_prev;
    }

    // All blocks are in use or pinned
    serial::puts("[cache] WARNING: All cache blocks in use or pinned!\n");
    return nullptr;
}

CacheBlock *BlockCache::find_eviction_victim() {
    CacheBlock *block = lru_tail_;
    while (block) {
        if (block->refcount == 0 && !block->pinned) {
            return block;
        }
        block = block->lru_prev;
    }
    serial::puts("[cache] WARNING: All cache blocks in use or pinned!\n");
    return nullptr;
}

/** @copydoc fs::BlockCache::read_block */
bool BlockCache::read_block(u64 block_num, void *buf) {
    // Use configured device or default to system blk_device
    auto *blk = device_ ? device_ : ::virtio::blk_device();
    if (!blk)
        return false;

    // Convert block number to sector (8 sectors per 4KB block)
    u64 sector = block_num * (BLOCK_SIZE / 512);
    u32 count = BLOCK_SIZE / 512;

    return blk->read_sectors(sector, count, buf) == 0;
}

/** @copydoc fs::BlockCache::write_block */
bool BlockCache::write_block(u64 block_num, const void *buf) {
    // Use configured device or default to system blk_device
    auto *blk = device_ ? device_ : ::virtio::blk_device();
    if (!blk)
        return false;

    u64 sector = block_num * (BLOCK_SIZE / 512);
    u32 count = BLOCK_SIZE / 512;

    return blk->write_sectors(sector, count, buf) == 0;
}

/**
 * @brief Prefetch a block into cache without incrementing refcount.
 *
 * @details
 * Internal helper for read-ahead. Loads a block if not already cached.
 * The block is added to the cache but with refcount 0 so it can be evicted
 * if needed before being accessed.
 *
 * Must be called WITH cache_lock held.
 */
bool BlockCache::prefetch_block(u64 block_num) {
    // Check if already cached
    CacheBlock *block = find(block_num);
    if (block) {
        return true; // Already in cache
    }

    // Get a free block
    block = evict();
    if (!block) {
        return false; // No space
    }

    // Load from disk
    if (!read_block(block_num, block->data)) {
        return false;
    }

    // Set up the block with refcount 0 (prefetched, not accessed yet)
    block->block_num = block_num;
    block->valid = true;
    block->dirty = false;
    block->refcount = 0; // Not referenced - can be evicted if needed

    // Add to hash
    insert_hash(block);

    // Add to LRU (at tail since it's prefetched, not actively used)
    remove_from_lru(block);
    // Insert after head but before tail (middle priority)
    if (lru_head_ && lru_head_->lru_next) {
        CacheBlock *second = lru_head_->lru_next;
        block->lru_prev = lru_head_;
        block->lru_next = second;
        lru_head_->lru_next = block;
        second->lru_prev = block;
    } else {
        add_to_lru_head(block);
    }

    readahead_count_++;
    return true;
}

/**
 * @brief Trigger read-ahead for sequential access patterns.
 *
 * @details
 * Prefetches the next READ_AHEAD_BLOCKS blocks after the given block.
 * Must be called WITH cache_lock held.
 */
void BlockCache::read_ahead(u64 block_num) {
    for (usize i = 1; i <= READ_AHEAD_BLOCKS; i++) {
        u64 ahead_block = block_num + i;
        prefetch_block(ahead_block);
    }
}

void BlockCache::read_ahead_unlocked(u64 block_num) {
    for (usize i = 1; i <= READ_AHEAD_BLOCKS; i++) {
        u64 ahead = block_num + i;

        u64 daif = cache_lock.acquire();
        CacheBlock *existing = find_any(ahead);
        if (existing) {
            cache_lock.release(daif);
            continue; // Already cached or being loaded
        }

        CacheBlock *block = find_eviction_victim();
        if (!block) {
            cache_lock.release(daif);
            break;
        }

        // Save dirty state and remove from hash before releasing lock
        bool was_dirty = block->valid && block->dirty;
        u64 wb_num = block->block_num;

        if (block->valid) {
            remove_hash(block);
        }

        // Reserve under new block_num
        block->block_num = ahead;
        block->valid = false;
        block->dirty = false;
        block->refcount = 1; // Prevent eviction during load
        insert_hash(block);

        cache_lock.release(daif);

        // Write back dirty data if needed
        if (was_dirty) {
            write_block(wb_num, block->data);
        }

        // Read new data
        bool ok = read_block(ahead, block->data);

        daif = cache_lock.acquire();
        if (ok) {
            block->valid = true;
        } else {
            remove_hash(block);
            block->block_num = 0;
        }
        block->refcount = 0; // Prefetched, not actively referenced
        readahead_count_++;
        cache_lock.release(daif);
    }
}

/** @copydoc fs::BlockCache::get */
CacheBlock *BlockCache::get(u64 block_num) {
retry:
    u64 daif = cache_lock.acquire();

    // Check cache (find_any matches both valid and loading blocks)
    CacheBlock *block = find_any(block_num);

    if (block) {
        if (!block->valid) {
            // Block is being loaded by another caller — yield and retry
            cache_lock.release(daif);
            asm volatile("yield");
            goto retry;
        }
        hits_++;
        block->refcount++;
        touch(block);
        last_block_ = block_num;
        cache_lock.release(daif);
        return block;
    }

    // Cache miss
    misses_++;
    u64 prev_last_block = last_block_;

    block = find_eviction_victim();
    if (!block) {
        serial::puts("[cache] Failed to evict block\n");
        cache_lock.release(daif);
        return nullptr;
    }

    // Save dirty state before modifying block
    bool was_dirty = block->valid && block->dirty;
    u64 wb_block_num = block->block_num;

    // Remove from hash under old block_num
    if (block->valid) {
        remove_hash(block);
    }

    // Reserve under new block_num: visible in hash with valid=false
    // This prevents duplicate loads and lets other callers yield/retry
    block->block_num = block_num;
    block->valid = false;
    block->dirty = false;
    block->refcount = 1;
    insert_hash(block);
    touch(block);

    // Release lock for ALL disk I/O (prevents deadlock)
    cache_lock.release(daif);

    // Write back dirty data if needed (old block contents)
    if (was_dirty) {
        write_block(wb_block_num, block->data);
    }

    // Read new block data
    bool ok = read_block(block_num, block->data);

    // Re-acquire lock to finalize
    daif = cache_lock.acquire();

    if (!ok) {
        serial::puts("[cache] Failed to read block ");
        serial::put_dec(block_num);
        serial::puts("\n");
        remove_hash(block);
        block->refcount = 0;
        block->block_num = 0;
        cache_lock.release(daif);
        return nullptr;
    }

    block->valid = true;
    last_block_ = block_num;

    bool is_sequential = (block_num == prev_last_block + 1);

    cache_lock.release(daif);

    // Read-ahead with lock released
    if (is_sequential) {
        read_ahead_unlocked(block_num);
    }

    return block;
}

/** @copydoc fs::BlockCache::get_for_write */
CacheBlock *BlockCache::get_for_write(u64 block_num) {
retry:
    u64 daif = cache_lock.acquire();

    CacheBlock *block = find_any(block_num);

    if (block) {
        if (!block->valid) {
            // Block is being loaded by another caller — yield and retry
            cache_lock.release(daif);
            asm volatile("yield");
            goto retry;
        }
        hits_++;
        block->refcount++;
        touch(block);
        block->dirty = true;
        cache_lock.release(daif);
        return block;
    }

    // Cache miss
    misses_++;

    block = find_eviction_victim();
    if (!block) {
        serial::puts("[cache] Failed to evict block\n");
        cache_lock.release(daif);
        return nullptr;
    }

    // Save dirty state before modifying block
    bool was_dirty = block->valid && block->dirty;
    u64 wb_block_num = block->block_num;

    // Remove from hash under old block_num
    if (block->valid) {
        remove_hash(block);
    }

    // Reserve under new block_num
    block->block_num = block_num;
    block->valid = false;
    block->dirty = false;
    block->refcount = 1;
    insert_hash(block);
    touch(block);

    // Release lock for disk I/O (prevents deadlock)
    cache_lock.release(daif);

    // Write back dirty data if needed
    if (was_dirty) {
        write_block(wb_block_num, block->data);
    }

    // Read new block data
    bool ok = read_block(block_num, block->data);

    // Re-acquire lock to finalize
    daif = cache_lock.acquire();

    if (!ok) {
        serial::puts("[cache] Failed to read block ");
        serial::put_dec(block_num);
        serial::puts("\n");
        remove_hash(block);
        block->refcount = 0;
        block->block_num = 0;
        cache_lock.release(daif);
        return nullptr;
    }

    block->valid = true;
    block->dirty = true; // Mark dirty for write
    cache_lock.release(daif);
    return block;
}

/** @copydoc fs::BlockCache::release */
void BlockCache::release(CacheBlock *block) {
    if (!block)
        return;

    SpinlockGuard guard(cache_lock);
    if (block->refcount > 0) {
        block->refcount--;
    }
}

/** @copydoc fs::BlockCache::sync_block */
void BlockCache::sync_block(CacheBlock *block) {
    if (!block || !block->valid || !block->dirty) {
        return;
    }

    if (write_block(block->block_num, block->data)) {
        block->dirty = false;
    } else {
        serial::puts("[cache] Failed to write block ");
        serial::put_dec(block->block_num);
        serial::puts("\n");
    }
}

/** @copydoc fs::BlockCache::sync */
void BlockCache::sync() {
    u64 daif = cache_lock.acquire();

    // Collect dirty block indices
    usize dirty_indices[CACHE_BLOCKS];
    u32 count = 0;

    for (usize i = 0; i < CACHE_BLOCKS; i++) {
        if (blocks_[i].valid && blocks_[i].dirty) {
            dirty_indices[count++] = i;
        }
    }

    if (count == 0) {
        cache_lock.release(daif);
        return; // Nothing to sync
    }

    serial::puts("[cache] Syncing ");
    serial::put_dec(count);
    serial::puts(" dirty blocks...\n");

    // Simple insertion sort by block number (cache is small, O(n^2) is fine)
    for (u32 i = 1; i < count; i++) {
        usize key = dirty_indices[i];
        u64 key_block = blocks_[key].block_num;
        i32 j = static_cast<i32>(i) - 1;

        while (j >= 0 && blocks_[dirty_indices[j]].block_num > key_block) {
            dirty_indices[j + 1] = dirty_indices[j];
            j--;
        }
        dirty_indices[j + 1] = key;
    }

    // Write each block with lock released (prevents deadlock)
    u32 synced = 0;
    for (u32 i = 0; i < count; i++) {
        CacheBlock *block = &blocks_[dirty_indices[i]];
        if (!block->valid || !block->dirty)
            continue; // May have changed while unlocked

        u64 bn = block->block_num;
        block->refcount++; // Prevent eviction while unlocked
        cache_lock.release(daif);

        write_block(bn, block->data);

        daif = cache_lock.acquire();
        block->dirty = false;
        block->refcount--;
        synced++;
    }

    cache_lock.release(daif);

    serial::puts("[cache] Synced ");
    serial::put_dec(synced);
    serial::puts(" blocks\n");
}

/** @copydoc fs::BlockCache::invalidate */
void BlockCache::invalidate(u64 block_num) {
    u64 daif = cache_lock.acquire();

    CacheBlock *block = find(block_num);
    if (block) {
        if (block->dirty) {
            // Write back with lock released (prevents deadlock)
            block->refcount++;
            u64 bn = block->block_num;
            cache_lock.release(daif);

            write_block(bn, block->data);

            daif = cache_lock.acquire();
            block->dirty = false;
            block->refcount--;
        }
        remove_hash(block);
        block->valid = false;
        block->pinned = false;
    }

    cache_lock.release(daif);
}

/** @copydoc fs::BlockCache::dump_stats */
void BlockCache::dump_stats() {
    SpinlockGuard guard(cache_lock);

    u32 valid_count = 0;
    u32 dirty_count = 0;
    u32 pinned_count = 0;
    u32 in_use_count = 0;

    for (usize i = 0; i < CACHE_BLOCKS; i++) {
        if (blocks_[i].valid)
            valid_count++;
        if (blocks_[i].dirty)
            dirty_count++;
        if (blocks_[i].pinned)
            pinned_count++;
        if (blocks_[i].refcount > 0)
            in_use_count++;
    }

    serial::puts("\n=== Block Cache Statistics ===\n");
    serial::puts("Capacity: ");
    serial::put_dec(CACHE_BLOCKS);
    serial::puts(" blocks (");
    serial::put_dec(CACHE_BLOCKS * BLOCK_SIZE / 1024);
    serial::puts(" KB)\n");

    serial::puts("Valid: ");
    serial::put_dec(valid_count);
    serial::puts(", Dirty: ");
    serial::put_dec(dirty_count);
    serial::puts(", Pinned: ");
    serial::put_dec(pinned_count);
    serial::puts(", In-use: ");
    serial::put_dec(in_use_count);
    serial::puts("\n");

    serial::puts("Hits: ");
    serial::put_dec(hits_);
    serial::puts(", Misses: ");
    serial::put_dec(misses_);

    u64 total = hits_ + misses_;
    if (total > 0) {
        u64 hit_rate = (hits_ * 100) / total;
        serial::puts(" (");
        serial::put_dec(hit_rate);
        serial::puts("% hit rate)\n");
    } else {
        serial::puts("\n");
    }

    serial::puts("Read-ahead: ");
    serial::put_dec(readahead_count_);
    serial::puts(" blocks prefetched\n");
    serial::puts("==============================\n");
}

/** @copydoc fs::BlockCache::pin */
bool BlockCache::pin(u64 block_num) {
    u64 daif = cache_lock.acquire();

    // First try to find in cache
    CacheBlock *block = find(block_num);

    if (!block) {
        // Need to load from disk — find victim and release lock for I/O
        block = find_eviction_victim();
        if (!block) {
            serial::puts("[cache] Failed to pin block - no space\n");
            cache_lock.release(daif);
            return false;
        }

        // Write back dirty victim with lock released
        if (block->valid && block->dirty) {
            block->refcount++;
            u64 wb_num = block->block_num;
            cache_lock.release(daif);

            write_block(wb_num, block->data);

            daif = cache_lock.acquire();
            block->dirty = false;
            block->refcount--;
        }

        // Remove old hash entry
        if (block->valid) {
            remove_hash(block);
        }

        // Reserve for new block
        block->block_num = block_num;
        block->valid = false;
        block->dirty = false;
        block->refcount = 1; // Prevent eviction during load
        insert_hash(block);
        touch(block);

        // Release lock for disk read
        cache_lock.release(daif);

        bool ok = read_block(block_num, block->data);

        daif = cache_lock.acquire();

        if (!ok) {
            serial::puts("[cache] Failed to read block for pinning\n");
            remove_hash(block);
            block->refcount = 0;
            block->block_num = 0;
            cache_lock.release(daif);
            return false;
        }

        block->valid = true;
        block->refcount = 0;
    }

    block->pinned = true;
    cache_lock.release(daif);
    return true;
}

/** @copydoc fs::BlockCache::unpin */
void BlockCache::unpin(u64 block_num) {
    SpinlockGuard guard(cache_lock);

    CacheBlock *block = find(block_num);
    if (block) {
        block->pinned = false;
    }
}

} // namespace fs
