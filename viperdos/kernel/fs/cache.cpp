//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
#include "cache.hpp"
#include "../console/serial.hpp"
#include "../drivers/virtio/blk.hpp"
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

/** @copydoc fs::BlockCache::remove_from_lru */
void BlockCache::remove_from_lru(CacheBlock *block) {
    if (block->lru_prev) {
        block->lru_prev->lru_next = block->lru_next;
    } else {
        lru_head_ = block->lru_next;
    }

    if (block->lru_next) {
        block->lru_next->lru_prev = block->lru_prev;
    } else {
        lru_tail_ = block->lru_prev;
    }

    block->lru_prev = nullptr;
    block->lru_next = nullptr;
}

/** @copydoc fs::BlockCache::add_to_lru_head */
void BlockCache::add_to_lru_head(CacheBlock *block) {
    block->lru_prev = nullptr;
    block->lru_next = lru_head_;

    if (lru_head_) {
        lru_head_->lru_prev = block;
    }
    lru_head_ = block;

    if (!lru_tail_) {
        lru_tail_ = block;
    }
}

/** @copydoc fs::BlockCache::touch */
void BlockCache::touch(CacheBlock *block) {
    // Move to head of LRU
    if (block == lru_head_) {
        return; // Already at head
    }

    remove_from_lru(block);
    add_to_lru_head(block);
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

/** @copydoc fs::BlockCache::get */
CacheBlock *BlockCache::get(u64 block_num) {
    SpinlockGuard guard(cache_lock);

    // Check cache first
    CacheBlock *block = find(block_num);

    if (block) {
        hits_++;
        block->refcount++;
        touch(block);
        last_block_ = block_num;
        return block;
    }

    // Cache miss - need to load from disk
    misses_++;

    // Get a free block
    block = evict();
    if (!block) {
        serial::puts("[cache] Failed to evict block\n");
        return nullptr;
    }

    // Load from disk
    if (!read_block(block_num, block->data)) {
        serial::puts("[cache] Failed to read block ");
        serial::put_dec(block_num);
        serial::puts("\n");
        return nullptr;
    }

    // Set up the block
    block->block_num = block_num;
    block->valid = true;
    block->dirty = false;
    block->refcount = 1;

    // Add to hash
    insert_hash(block);

    // Move to LRU head
    touch(block);

    // Detect sequential access and trigger read-ahead
    bool is_sequential = (block_num == last_block_ + 1);
    last_block_ = block_num;

    if (is_sequential) {
        read_ahead(block_num);
    }

    return block;
}

/** @copydoc fs::BlockCache::get_for_write */
CacheBlock *BlockCache::get_for_write(u64 block_num) {
    SpinlockGuard guard(cache_lock);

    // Check cache first (inline get logic to avoid double-locking)
    CacheBlock *block = find(block_num);

    if (block) {
        hits_++;
        block->refcount++;
        touch(block);
        block->dirty = true;
        return block;
    }

    // Cache miss - need to load from disk
    misses_++;

    block = evict();
    if (!block) {
        serial::puts("[cache] Failed to evict block\n");
        return nullptr;
    }

    if (!read_block(block_num, block->data)) {
        serial::puts("[cache] Failed to read block ");
        serial::put_dec(block_num);
        serial::puts("\n");
        return nullptr;
    }

    block->block_num = block_num;
    block->valid = true;
    block->dirty = true; // Mark as dirty for write
    block->refcount = 1;

    insert_hash(block);
    touch(block);

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
    SpinlockGuard guard(cache_lock);

    serial::puts("[cache] Syncing dirty blocks...\n");

    u32 synced = 0;
    for (usize i = 0; i < CACHE_BLOCKS; i++) {
        if (blocks_[i].valid && blocks_[i].dirty) {
            sync_block(&blocks_[i]);
            synced++;
        }
    }

    serial::puts("[cache] Synced ");
    serial::put_dec(synced);
    serial::puts(" blocks\n");
}

/** @copydoc fs::BlockCache::invalidate */
void BlockCache::invalidate(u64 block_num) {
    SpinlockGuard guard(cache_lock);

    CacheBlock *block = find(block_num);
    if (block) {
        if (block->dirty) {
            sync_block(block);
        }
        remove_hash(block);
        block->valid = false;
        block->pinned = false;
    }
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
    SpinlockGuard guard(cache_lock);

    // First try to find in cache
    CacheBlock *block = find(block_num);

    if (!block) {
        // Need to load from disk
        block = evict();
        if (!block) {
            serial::puts("[cache] Failed to pin block - no space\n");
            return false;
        }

        if (!read_block(block_num, block->data)) {
            serial::puts("[cache] Failed to read block for pinning\n");
            return false;
        }

        block->block_num = block_num;
        block->valid = true;
        block->dirty = false;
        block->refcount = 0;
        insert_hash(block);
        touch(block);
    }

    block->pinned = true;
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
