/**
 * @file viperfs.cpp
 * @brief ViperFS filesystem driver implementation.
 *
 * @details
 * Implements the ViperFS filesystem described by the on-disk format structures
 * in `format.hpp`. The driver uses the global block cache (`fs::cache`) for
 * block I/O and maintains an in-memory copy of the superblock.
 *
 * Major responsibilities:
 * - Mount/unmount and superblock validation.
 * - Inode read/write operations (inode table access).
 * - Directory entry lookup and enumeration.
 * - File data read/write using direct and indirect block pointers.
 * - Block and inode allocation using a bitmap and inode table scanning.
 */
#include "viperfs.hpp"
#include "../../arch/aarch64/timer.hpp"
#include "../../console/serial.hpp"
#include "../../mm/kheap.hpp"
#include "../../mm/slab.hpp"
#include "../cache.hpp"
#include "journal.hpp"

namespace fs::viperfs
{

// Global instance
static ViperFS g_viperfs;

// Inode cache spinlock (separate from fs_lock_ to allow concurrent inode access)
static Spinlock inode_cache_lock;

/** @copydoc fs::viperfs::viperfs */
ViperFS &viperfs()
{
    return g_viperfs;
}

// ============================================================================
// InodeCache Implementation
// ============================================================================

void InodeCache::init()
{
    for (usize i = 0; i < INODE_CACHE_SIZE; i++)
    {
        entries_[i].refcount = 0;
        entries_[i].valid = false;
        entries_[i].dirty = false;
        entries_[i].lru_prev = nullptr;
        entries_[i].lru_next = nullptr;
        entries_[i].hash_next = nullptr;
    }

    for (usize i = 0; i < INODE_HASH_SIZE; i++)
    {
        hash_[i] = nullptr;
    }

    // Initialize LRU list
    lru_head_ = &entries_[0];
    lru_tail_ = &entries_[INODE_CACHE_SIZE - 1];

    for (usize i = 0; i < INODE_CACHE_SIZE; i++)
    {
        entries_[i].lru_prev = (i > 0) ? &entries_[i - 1] : nullptr;
        entries_[i].lru_next = (i < INODE_CACHE_SIZE - 1) ? &entries_[i + 1] : nullptr;
    }

    hits_ = 0;
    misses_ = 0;
}

u32 InodeCache::hash_func(u64 ino)
{
    return ino % INODE_HASH_SIZE;
}

CachedInode *InodeCache::find(u64 ino)
{
    u32 h = hash_func(ino);
    CachedInode *ci = hash_[h];

    while (ci)
    {
        if (ci->valid && ci->inode.inode_num == ino)
        {
            return ci;
        }
        ci = ci->hash_next;
    }

    return nullptr;
}

void InodeCache::remove_from_lru(CachedInode *ci)
{
    if (ci->lru_prev)
    {
        ci->lru_prev->lru_next = ci->lru_next;
    }
    else
    {
        lru_head_ = ci->lru_next;
    }

    if (ci->lru_next)
    {
        ci->lru_next->lru_prev = ci->lru_prev;
    }
    else
    {
        lru_tail_ = ci->lru_prev;
    }

    ci->lru_prev = nullptr;
    ci->lru_next = nullptr;
}

void InodeCache::add_to_lru_head(CachedInode *ci)
{
    ci->lru_prev = nullptr;
    ci->lru_next = lru_head_;

    if (lru_head_)
    {
        lru_head_->lru_prev = ci;
    }
    lru_head_ = ci;

    if (!lru_tail_)
    {
        lru_tail_ = ci;
    }
}

void InodeCache::touch(CachedInode *ci)
{
    if (ci == lru_head_)
    {
        return;
    }

    remove_from_lru(ci);
    add_to_lru_head(ci);
}

void InodeCache::insert_hash(CachedInode *ci)
{
    u32 h = hash_func(ci->inode.inode_num);
    ci->hash_next = hash_[h];
    hash_[h] = ci;
}

void InodeCache::remove_hash(CachedInode *ci)
{
    u32 h = hash_func(ci->inode.inode_num);
    CachedInode **pp = &hash_[h];

    while (*pp)
    {
        if (*pp == ci)
        {
            *pp = ci->hash_next;
            ci->hash_next = nullptr;
            return;
        }
        pp = &(*pp)->hash_next;
    }
}

CachedInode *InodeCache::evict()
{
    CachedInode *ci = lru_tail_;

    while (ci)
    {
        if (ci->refcount == 0)
        {
            // Write back if dirty
            if (ci->valid && ci->dirty)
            {
                sync(ci);
            }

            // Remove from hash
            if (ci->valid)
            {
                remove_hash(ci);
            }

            return ci;
        }
        ci = ci->lru_prev;
    }

    serial::puts("[inode_cache] WARNING: All inodes in use!\n");
    return nullptr;
}

bool InodeCache::load_inode(u64 ino, Inode *out)
{
    u64 block_num = viperfs().inode_block(ino);
    u64 offset = viperfs().inode_offset(ino);

    CacheBlock *block = cache().get(block_num);
    if (!block)
    {
        return false;
    }

    const Inode *disk_inode = reinterpret_cast<const Inode *>(block->data + offset);
    *out = *disk_inode;

    cache().release(block);
    return true;
}

bool InodeCache::store_inode(const Inode *inode)
{
    u64 ino = inode->inode_num;
    u64 block_num = viperfs().inode_block(ino);
    u64 offset = viperfs().inode_offset(ino);

    CacheBlock *block = cache().get_for_write(block_num);
    if (!block)
    {
        return false;
    }

    Inode *disk_inode = reinterpret_cast<Inode *>(block->data + offset);
    *disk_inode = *inode;
    block->dirty = true;

    cache().release(block);
    return true;
}

CachedInode *InodeCache::get(u64 ino)
{
    SpinlockGuard guard(inode_cache_lock);

    // Check cache first
    CachedInode *ci = find(ino);

    if (ci)
    {
        hits_++;
        ci->refcount++;
        touch(ci);
        return ci;
    }

    // Cache miss - need to load
    misses_++;

    ci = evict();
    if (!ci)
    {
        return nullptr;
    }

    if (!load_inode(ino, &ci->inode))
    {
        serial::puts("[inode_cache] Failed to load inode ");
        serial::put_dec(ino);
        serial::puts("\n");
        return nullptr;
    }

    ci->valid = true;
    ci->dirty = false;
    ci->refcount = 1;

    insert_hash(ci);
    touch(ci);

    return ci;
}

void InodeCache::release(CachedInode *ci)
{
    if (!ci)
        return;

    SpinlockGuard guard(inode_cache_lock);
    if (ci->refcount > 0)
    {
        ci->refcount--;
    }
}

bool InodeCache::sync(CachedInode *ci)
{
    if (!ci || !ci->valid || !ci->dirty)
    {
        return true;
    }

    if (store_inode(&ci->inode))
    {
        ci->dirty = false;
        return true;
    }

    return false;
}

void InodeCache::sync_all()
{
    SpinlockGuard guard(inode_cache_lock);

    u32 synced = 0;
    for (usize i = 0; i < INODE_CACHE_SIZE; i++)
    {
        if (entries_[i].valid && entries_[i].dirty)
        {
            if (sync(&entries_[i]))
            {
                synced++;
            }
        }
    }

    if (synced > 0)
    {
        serial::puts("[inode_cache] Synced ");
        serial::put_dec(synced);
        serial::puts(" inodes\n");
    }
}

void InodeCache::invalidate(u64 ino)
{
    SpinlockGuard guard(inode_cache_lock);

    CachedInode *ci = find(ino);
    if (ci)
    {
        if (ci->dirty)
        {
            sync(ci);
        }
        remove_hash(ci);
        ci->valid = false;
    }
}

void InodeCache::dump_stats()
{
    SpinlockGuard guard(inode_cache_lock);

    u32 valid_count = 0;
    u32 dirty_count = 0;
    u32 in_use_count = 0;

    for (usize i = 0; i < INODE_CACHE_SIZE; i++)
    {
        if (entries_[i].valid)
            valid_count++;
        if (entries_[i].dirty)
            dirty_count++;
        if (entries_[i].refcount > 0)
            in_use_count++;
    }

    serial::puts("\n=== Inode Cache Statistics ===\n");
    serial::puts("Capacity: ");
    serial::put_dec(INODE_CACHE_SIZE);
    serial::puts(" inodes\n");

    serial::puts("Valid: ");
    serial::put_dec(valid_count);
    serial::puts(", Dirty: ");
    serial::put_dec(dirty_count);
    serial::puts(", In-use: ");
    serial::put_dec(in_use_count);
    serial::puts("\n");

    serial::puts("Hits: ");
    serial::put_dec(hits_);
    serial::puts(", Misses: ");
    serial::put_dec(misses_);

    u64 total = hits_ + misses_;
    if (total > 0)
    {
        u64 hit_rate = (hits_ * 100) / total;
        serial::puts(" (");
        serial::put_dec(hit_rate);
        serial::puts("% hit rate)\n");
    }
    else
    {
        serial::puts("\n");
    }

    serial::puts("==============================\n");
}

// ============================================================================
// ViperFS Cached Inode Methods
// ============================================================================

CachedInode *ViperFS::get_cached_inode(u64 ino)
{
    if (!mounted_)
        return nullptr;
    return inode_cache_.get(ino);
}

void ViperFS::release_cached_inode(CachedInode *ci)
{
    inode_cache_.release(ci);
}

void ViperFS::mark_inode_dirty(CachedInode *ci)
{
    if (ci)
    {
        ci->dirty = true;
    }
}

void ViperFS::sync_inodes()
{
    inode_cache_.sync_all();
}

/** @copydoc fs::viperfs::ViperFS::alloc_zeroed_block_unlocked */
u64 ViperFS::alloc_zeroed_block_unlocked()
{
    u64 block_num = alloc_block_unlocked();
    if (block_num == 0)
        return 0;

    CacheBlock *block = cache().get(block_num);
    if (!block)
    {
        // Allocation succeeded but cache failed - free the block to prevent leak
        free_block_unlocked(block_num);
        return 0;
    }

    // Zero the block
    for (usize i = 0; i < BLOCK_SIZE; i++)
    {
        block->data[i] = 0;
    }
    block->dirty = true;
    cache().release(block);

    return block_num;
}

/** @copydoc fs::viperfs::viperfs_init */
bool viperfs_init()
{
    return g_viperfs.mount();
}

/** @copydoc fs::viperfs::ViperFS::mount */
bool ViperFS::mount()
{
    serial::puts("[viperfs] Mounting filesystem...\n");

    // Read superblock (block 0)
    CacheBlock *sb_block = cache().get(0);
    if (!sb_block)
    {
        serial::puts("[viperfs] Failed to read superblock\n");
        return false;
    }

    // Copy superblock
    const Superblock *sb = reinterpret_cast<const Superblock *>(sb_block->data);

    // Verify magic
    if (sb->magic != VIPERFS_MAGIC)
    {
        serial::puts("[viperfs] Invalid magic: ");
        serial::put_hex(sb->magic);
        serial::puts(" (expected ");
        serial::put_hex(VIPERFS_MAGIC);
        serial::puts(")\n");
        cache().release(sb_block);
        return false;
    }

    // Verify version
    if (sb->version != VIPERFS_VERSION)
    {
        serial::puts("[viperfs] Unsupported version: ");
        serial::put_dec(sb->version);
        serial::puts("\n");
        cache().release(sb_block);
        return false;
    }

    // Copy superblock data
    sb_ = *sb;
    cache().release(sb_block);

    // Initialize inode cache
    inode_cache_.init();

    mounted_ = true;

    serial::puts("[viperfs] Mounted '");
    serial::puts(sb_.label);
    serial::puts("'\n");
    serial::puts("[viperfs] Total blocks: ");
    serial::put_dec(sb_.total_blocks);
    serial::puts(", free: ");
    serial::put_dec(sb_.free_blocks);
    serial::puts("\n");
    serial::puts("[viperfs] Root inode: ");
    serial::put_dec(sb_.root_inode);
    serial::puts("\n");

    // Initialize journal (located after data blocks)
    // Use the last JOURNAL_BLOCKS blocks of the filesystem for journaling
    u64 journal_start = sb_.total_blocks - JOURNAL_BLOCKS;
    if (journal_start > sb_.data_start)
    {
        if (journal_init(journal_start, JOURNAL_BLOCKS))
        {
            // Replay any committed transactions from previous crash
            journal().replay();
            serial::puts("[viperfs] Journaling enabled\n");
        }
        else
        {
            serial::puts("[viperfs] Warning: journaling disabled\n");
        }
    }
    else
    {
        serial::puts("[viperfs] Filesystem too small for journaling\n");
    }

    return true;
}

/** @copydoc fs::viperfs::ViperFS::unmount */
void ViperFS::unmount()
{
    if (!mounted_)
        return;

    // Sync inode cache first (before block cache)
    inode_cache_.sync_all();

    // Sync journal and cache
    if (journal().is_enabled())
    {
        journal().sync();
    }
    cache().sync();

    mounted_ = false;
    serial::puts("[viperfs] Unmounted\n");
}

/** @copydoc fs::viperfs::ViperFS::inode_block */
u64 ViperFS::inode_block(u64 ino)
{
    return sb_.inode_table_start + (ino / INODES_PER_BLOCK);
}

/** @copydoc fs::viperfs::ViperFS::inode_offset */
u64 ViperFS::inode_offset(u64 ino)
{
    return (ino % INODES_PER_BLOCK) * INODE_SIZE;
}

/** @copydoc fs::viperfs::ViperFS::read_inode */
Inode *ViperFS::read_inode(u64 ino)
{
    if (!mounted_)
        return nullptr;

    u64 block_num = inode_block(ino);
    u64 offset = inode_offset(ino);

    CacheBlock *block = cache().get(block_num);
    if (!block)
    {
        serial::puts("[viperfs] Failed to read inode block\n");
        return nullptr;
    }

    // Allocate inode from slab cache (falls back to heap if cache unavailable)
    Inode *inode = nullptr;
    slab::SlabCache *cache_ptr = slab::inode_cache();
    if (cache_ptr)
    {
        inode = static_cast<Inode *>(slab::alloc(cache_ptr));
    }
    else
    {
        inode = static_cast<Inode *>(kheap::kmalloc(sizeof(Inode)));
    }
    if (!inode)
    {
        cache().release(block);
        return nullptr;
    }

    const Inode *disk_inode = reinterpret_cast<const Inode *>(block->data + offset);
    *inode = *disk_inode;

    cache().release(block);
    return inode;
}

/** @copydoc fs::viperfs::ViperFS::release_inode */
void ViperFS::release_inode(Inode *inode)
{
    if (inode)
    {
        // Free to slab cache if available, otherwise heap
        slab::SlabCache *cache_ptr = slab::inode_cache();
        if (cache_ptr)
        {
            slab::free(cache_ptr, inode);
        }
        else
        {
            kheap::kfree(inode);
        }
    }
}

/** @copydoc fs::viperfs::ViperFS::read_indirect */
u64 ViperFS::read_indirect(u64 block_num, u64 index)
{
    constexpr u64 PTRS_PER_BLOCK = BLOCK_SIZE / sizeof(u64);

    if (block_num == 0)
        return 0;

    // Bounds check to prevent out-of-bounds read
    if (index >= PTRS_PER_BLOCK)
    {
        serial::puts("[viperfs] ERROR: indirect block index out of bounds\n");
        return 0;
    }

    CacheBlock *block = cache().get(block_num);
    if (!block)
        return 0;

    const u64 *ptrs = reinterpret_cast<const u64 *>(block->data);
    u64 result = ptrs[index];

    cache().release(block);
    return result;
}

/** @copydoc fs::viperfs::ViperFS::get_block_ptr */
u64 ViperFS::get_block_ptr(Inode *inode, u64 block_idx)
{
    constexpr u64 PTRS_PER_BLOCK = BLOCK_SIZE / sizeof(u64);

    // Direct blocks (0-11)
    if (block_idx < 12)
    {
        return inode->direct[block_idx];
    }
    block_idx -= 12;

    // Single indirect (12 to 12+512-1)
    if (block_idx < PTRS_PER_BLOCK)
    {
        return read_indirect(inode->indirect, block_idx);
    }
    block_idx -= PTRS_PER_BLOCK;

    // Double indirect
    if (block_idx < PTRS_PER_BLOCK * PTRS_PER_BLOCK)
    {
        u64 l1_idx = block_idx / PTRS_PER_BLOCK;
        u64 l2_idx = block_idx % PTRS_PER_BLOCK;

        u64 l1_block = read_indirect(inode->double_indirect, l1_idx);
        if (l1_block == 0)
            return 0;

        return read_indirect(l1_block, l2_idx);
    }
    block_idx -= PTRS_PER_BLOCK * PTRS_PER_BLOCK;

    // Triple indirect (not implemented for now)
    return 0;
}

/** @copydoc fs::viperfs::ViperFS::read_data */
i64 ViperFS::read_data(Inode *inode, u64 offset, void *buf, usize len)
{
    if (!mounted_ || !inode || !buf)
        return -1;

    // Clamp to file size
    if (offset >= inode->size)
        return 0;
    if (offset + len > inode->size)
    {
        len = inode->size - offset;
    }

    u8 *dst = static_cast<u8 *>(buf);
    usize remaining = len;

    while (remaining > 0)
    {
        u64 block_idx = offset / BLOCK_SIZE;
        u64 block_off = offset % BLOCK_SIZE;
        usize to_read = BLOCK_SIZE - block_off;
        if (to_read > remaining)
            to_read = remaining;

        u64 block_num = get_block_ptr(inode, block_idx);
        if (block_num == 0)
        {
            // Sparse file - zero fill
            for (usize i = 0; i < to_read; i++)
            {
                dst[i] = 0;
            }
        }
        else
        {
            // Read from cache
            CacheBlock *block = cache().get(block_num);
            if (!block)
            {
                serial::puts("[viperfs] Failed to read data block\n");
                return -1;
            }

            for (usize i = 0; i < to_read; i++)
            {
                dst[i] = block->data[block_off + i];
            }

            cache().release(block);
        }

        dst += to_read;
        offset += to_read;
        remaining -= to_read;
    }

    // Update access time (note: not written to disk until fsync or unmount)
    inode->atime = timer::get_ms();

    return len;
}

/** @copydoc fs::viperfs::ViperFS::lookup */
u64 ViperFS::lookup(Inode *dir, const char *name, usize name_len)
{
    if (!mounted_ || !dir || !name)
        return 0;
    if (!is_directory(dir))
        return 0;

    u64 offset = 0;
    u8 buf[BLOCK_SIZE];

    while (offset < dir->size)
    {
        // Read a block of directory data
        i64 r = read_data(dir, offset, buf, BLOCK_SIZE);
        if (r < 0)
            return 0;
        if (r == 0)
            break;

        // Scan directory entries in this block
        usize pos = 0;
        while (pos < static_cast<usize>(r))
        {
            const DirEntry *entry = reinterpret_cast<const DirEntry *>(buf + pos);

            // End of entries
            if (entry->rec_len == 0)
                break;

            // Validate rec_len to prevent malformed directory entries
            if (entry->rec_len < DIR_ENTRY_MIN_SIZE ||
                pos + entry->rec_len > static_cast<usize>(r))
            {
                serial::puts("[viperfs] ERROR: Invalid rec_len in directory\n");
                return 0;
            }

            // Check if this entry matches
            if (entry->inode != 0 && entry->name_len == name_len)
            {
                bool match = true;
                for (usize i = 0; i < name_len; i++)
                {
                    if (entry->name[i] != name[i])
                    {
                        match = false;
                        break;
                    }
                }
                if (match)
                {
                    return entry->inode;
                }
            }

            pos += entry->rec_len;
        }

        offset += r;
    }

    return 0;
}

/** @copydoc fs::viperfs::ViperFS::readdir */
i32 ViperFS::readdir(Inode *dir, u64 offset, ReaddirCallback cb, void *ctx)
{
    if (!mounted_ || !dir || !cb)
        return -1;
    if (!is_directory(dir))
        return -1;

    u8 buf[BLOCK_SIZE];
    i32 count = 0;

    while (offset < dir->size)
    {
        i64 r = read_data(dir, offset, buf, BLOCK_SIZE);
        if (r < 0)
            return -1;
        if (r == 0)
            break;

        usize pos = 0;
        while (pos < static_cast<usize>(r))
        {
            const DirEntry *entry = reinterpret_cast<const DirEntry *>(buf + pos);

            if (entry->rec_len == 0)
                break;

            // Validate rec_len to prevent malformed directory entries
            if (entry->rec_len < DIR_ENTRY_MIN_SIZE ||
                pos + entry->rec_len > static_cast<usize>(r))
            {
                serial::puts("[viperfs] ERROR: Invalid rec_len in readdir\n");
                return -1;
            }

            if (entry->inode != 0)
            {
                cb(entry->name, entry->name_len, entry->inode, entry->file_type, ctx);
                count++;
            }

            pos += entry->rec_len;
        }

        offset += r;
    }

    return count;
}

// Allocation functions (internal unlocked versions - caller must hold fs_lock_)
/** @copydoc fs::viperfs::ViperFS::alloc_block_unlocked */
u64 ViperFS::alloc_block_unlocked()
{
    if (!mounted_)
        return 0;

    if (sb_.free_blocks == 0)
        return 0;

    // Scan bitmap for free block
    for (u64 bitmap_block = 0; bitmap_block < sb_.bitmap_blocks; bitmap_block++)
    {
        CacheBlock *block = cache().get(sb_.bitmap_start + bitmap_block);
        if (!block)
            continue;

        for (u64 byte = 0; byte < BLOCK_SIZE; byte++)
        {
            if (block->data[byte] != 0xFF)
            {
                // Found a byte with a free bit
                for (u8 bit = 0; bit < 8; bit++)
                {
                    if (!(block->data[byte] & (1 << bit)))
                    {
                        // Found free block
                        u64 block_num = bitmap_block * BLOCK_SIZE * 8 + byte * 8 + bit;
                        if (block_num >= sb_.total_blocks)
                        {
                            cache().release(block);
                            return 0;
                        }

                        // Mark as used
                        block->data[byte] |= (1 << bit);
                        block->dirty = true;
                        cache().release(block);

                        sb_.free_blocks--;
                        return block_num;
                    }
                }
            }
        }
        cache().release(block);
    }

    return 0;
}

/** @copydoc fs::viperfs::ViperFS::free_block_unlocked */
void ViperFS::free_block_unlocked(u64 block_num)
{
    if (!mounted_)
        return;
    if (block_num >= sb_.total_blocks)
        return;

    u64 bitmap_block = block_num / (BLOCK_SIZE * 8);
    u64 byte_in_block = (block_num / 8) % BLOCK_SIZE;
    u8 bit = block_num % 8;

    CacheBlock *block = cache().get(sb_.bitmap_start + bitmap_block);
    if (!block)
        return;

    block->data[byte_in_block] &= ~(1 << bit);
    block->dirty = true;
    cache().release(block);

    sb_.free_blocks++;
}

/** @copydoc fs::viperfs::ViperFS::alloc_inode_unlocked */
u64 ViperFS::alloc_inode_unlocked()
{
    if (!mounted_)
        return 0;

    // Scan inode table for free inode
    for (u64 ino = 2; ino < sb_.inode_count; ino++)
    {
        u64 block_num = inode_block(ino);
        u64 offset = inode_offset(ino);

        CacheBlock *block = cache().get(block_num);
        if (!block)
            continue;

        Inode *inode = reinterpret_cast<Inode *>(block->data + offset);
        if (inode->mode == 0)
        {
            // Free inode found - mark it as allocated immediately
            // to prevent races (set a minimal mode that indicates "in use")
            inode->mode = mode::TYPE_FILE; // Will be overwritten by caller
            block->dirty = true;
            cache().release(block);
            return ino;
        }
        cache().release(block);
    }

    return 0;
}

/**
 * @brief Free all data blocks belonging to an inode.
 * @note Caller must hold fs_lock_.
 */
void ViperFS::free_inode_data_unlocked(u64 ino)
{
    if (!mounted_)
        return;

    u64 block_num = inode_block(ino);
    u64 offset = inode_offset(ino);

    CacheBlock *block = cache().get(block_num);
    if (!block)
        return;

    Inode *inode = reinterpret_cast<Inode *>(block->data + offset);

    // Free all data blocks
    u64 num_blocks = (inode->size + BLOCK_SIZE - 1) / BLOCK_SIZE;
    for (u64 i = 0; i < num_blocks; i++)
    {
        u64 data_block = get_block_ptr(inode, i);
        if (data_block != 0)
        {
            free_block_unlocked(data_block);
            set_block_ptr(inode, i, 0);
        }
    }

    // Free indirect block if present
    if (inode->indirect != 0)
    {
        free_block_unlocked(inode->indirect);
        inode->indirect = 0;
    }

    // Free double indirect blocks if present
    if (inode->double_indirect != 0)
    {
        // Read first level indirect block
        CacheBlock *di_block = cache().get(inode->double_indirect);
        if (di_block)
        {
            u64 *l1_ptrs = reinterpret_cast<u64 *>(di_block->data);
            constexpr u64 PTRS_PER_BLOCK = BLOCK_SIZE / sizeof(u64);
            for (u64 i = 0; i < PTRS_PER_BLOCK; i++)
            {
                if (l1_ptrs[i] != 0)
                {
                    free_block_unlocked(l1_ptrs[i]);
                }
            }
            cache().release(di_block);
        }
        free_block_unlocked(inode->double_indirect);
        inode->double_indirect = 0;
    }

    inode->size = 0;
    inode->blocks = 0;
    block->dirty = true;
    cache().release(block);
}

/** @copydoc fs::viperfs::ViperFS::free_inode_unlocked */
void ViperFS::free_inode_unlocked(u64 ino)
{
    if (!mounted_)
        return;

    u64 block_num = inode_block(ino);
    u64 offset = inode_offset(ino);

    CacheBlock *block = cache().get(block_num);
    if (!block)
        return;

    Inode *inode = reinterpret_cast<Inode *>(block->data + offset);
    inode->mode = 0; // Mark as free
    block->dirty = true;
    cache().release(block);
}

/** @copydoc fs::viperfs::ViperFS::write_inode */
bool ViperFS::write_inode(Inode *inode)
{
    if (!mounted_ || !inode)
        return false;

    u64 block_num = inode_block(inode->inode_num);
    u64 offset = inode_offset(inode->inode_num);

    CacheBlock *block = cache().get(block_num);
    if (!block)
        return false;

    Inode *disk_inode = reinterpret_cast<Inode *>(block->data + offset);
    *disk_inode = *inode;
    block->dirty = true;
    cache().release(block);

    return true;
}

/** @copydoc fs::viperfs::ViperFS::sync */
void ViperFS::sync()
{
    if (!mounted_)
        return;

    // Write superblock
    CacheBlock *sb_block = cache().get(0);
    if (sb_block)
    {
        Superblock *sb = reinterpret_cast<Superblock *>(sb_block->data);
        *sb = sb_;
        sb_block->dirty = true;
        cache().release(sb_block);
    }

    // Sync all dirty blocks
    cache().sync();
}

/** @copydoc fs::viperfs::ViperFS::write_indirect */
bool ViperFS::write_indirect(u64 block_num, u64 index, u64 value)
{
    constexpr u64 PTRS_PER_BLOCK = BLOCK_SIZE / sizeof(u64);

    if (block_num == 0)
        return false;

    // Bounds check to prevent out-of-bounds write
    if (index >= PTRS_PER_BLOCK)
    {
        serial::puts("[viperfs] ERROR: indirect block write index out of bounds\n");
        return false;
    }

    CacheBlock *block = cache().get(block_num);
    if (!block)
        return false;

    u64 *ptrs = reinterpret_cast<u64 *>(block->data);
    ptrs[index] = value;
    block->dirty = true;
    cache().release(block);
    return true;
}

/** @copydoc fs::viperfs::ViperFS::set_block_ptr */
bool ViperFS::set_block_ptr(Inode *inode, u64 block_idx, u64 block_num)
{
    constexpr u64 PTRS_PER_BLOCK = BLOCK_SIZE / sizeof(u64);

    // Direct blocks (0-11)
    if (block_idx < 12)
    {
        inode->direct[block_idx] = block_num;
        return true;
    }
    block_idx -= 12;

    // Single indirect (12 to 12+512-1)
    if (block_idx < PTRS_PER_BLOCK)
    {
        if (inode->indirect == 0)
        {
            inode->indirect = alloc_zeroed_block_unlocked();
            if (inode->indirect == 0)
                return false;
        }
        return write_indirect(inode->indirect, block_idx, block_num);
    }
    block_idx -= PTRS_PER_BLOCK;

    // Double indirect
    if (block_idx < PTRS_PER_BLOCK * PTRS_PER_BLOCK)
    {
        if (inode->double_indirect == 0)
        {
            inode->double_indirect = alloc_zeroed_block_unlocked();
            if (inode->double_indirect == 0)
                return false;
        }

        u64 l1_idx = block_idx / PTRS_PER_BLOCK;
        u64 l2_idx = block_idx % PTRS_PER_BLOCK;

        u64 l1_block = read_indirect(inode->double_indirect, l1_idx);
        if (l1_block == 0)
        {
            l1_block = alloc_zeroed_block_unlocked();
            if (l1_block == 0)
                return false;
            write_indirect(inode->double_indirect, l1_idx, l1_block);
        }

        return write_indirect(l1_block, l2_idx, block_num);
    }

    // Triple indirect not supported
    return false;
}

/** @copydoc fs::viperfs::ViperFS::write_data */
i64 ViperFS::write_data(Inode *inode, u64 offset, const void *buf, usize len)
{
    if (!mounted_ || !inode || !buf)
        return -1;

    const u8 *src = static_cast<const u8 *>(buf);
    usize written = 0;

    while (written < len)
    {
        u64 block_idx = offset / BLOCK_SIZE;
        u64 block_off = offset % BLOCK_SIZE;
        usize to_write = BLOCK_SIZE - block_off;
        if (to_write > len - written)
            to_write = len - written;

        // Get or allocate block
        u64 block_num = get_block_ptr(inode, block_idx);
        if (block_num == 0)
        {
            block_num = alloc_block_unlocked();
            if (block_num == 0)
            {
                serial::puts("[viperfs] Out of blocks\n");
                return written > 0 ? static_cast<i64>(written) : -1;
            }
            if (!set_block_ptr(inode, block_idx, block_num))
            {
                free_block_unlocked(block_num);
                return written > 0 ? static_cast<i64>(written) : -1;
            }
            inode->blocks++;
        }

        // Write to block
        CacheBlock *block = cache().get(block_num);
        if (!block)
            return written > 0 ? static_cast<i64>(written) : -1;

        for (usize i = 0; i < to_write; i++)
        {
            block->data[block_off + i] = src[written + i];
        }
        block->dirty = true;
        cache().release(block);

        written += to_write;
        offset += to_write;
    }

    // Update file size if extended
    if (offset > inode->size)
    {
        inode->size = offset;
    }

    // Update modification time
    inode->mtime = timer::get_ms();

    return static_cast<i64>(written);
}

/** @copydoc fs::viperfs::ViperFS::truncate */
bool ViperFS::truncate(Inode *inode, u64 new_size)
{
    if (!mounted_ || !inode)
        return false;

    SpinlockGuard guard(fs_lock_);

    u64 old_size = inode->size;

    if (new_size == old_size)
    {
        return true; // Nothing to do
    }

    if (new_size < old_size)
    {
        // Shrinking - free blocks beyond new size
        u64 new_blocks = (new_size + BLOCK_SIZE - 1) / BLOCK_SIZE;
        u64 old_blocks = (old_size + BLOCK_SIZE - 1) / BLOCK_SIZE;

        for (u64 block_idx = new_blocks; block_idx < old_blocks; block_idx++)
        {
            u64 block_num = get_block_ptr(inode, block_idx);
            if (block_num != 0)
            {
                free_block_unlocked(block_num);
                set_block_ptr(inode, block_idx, 0);
                inode->blocks--;
            }
        }

        // Zero out partial block if needed
        if (new_size > 0)
        {
            u64 partial_offset = new_size % BLOCK_SIZE;
            if (partial_offset > 0)
            {
                u64 last_block_idx = new_size / BLOCK_SIZE;
                u64 block_num = get_block_ptr(inode, last_block_idx);
                if (block_num != 0)
                {
                    CacheBlock *block = cache().get_for_write(block_num);
                    if (block)
                    {
                        // Zero from partial_offset to end of block
                        for (u64 i = partial_offset; i < BLOCK_SIZE; i++)
                        {
                            block->data[i] = 0;
                        }
                        cache().release(block);
                    }
                }
            }
        }
    }
    // Extending: sparse file - no blocks allocated, just update size
    // Reads will return zeros for unallocated blocks

    inode->size = new_size;
    inode->mtime = timer::get_ms();
    inode->ctime = inode->mtime;

    // Write inode back to disk
    write_inode(inode);

    return true;
}

/** @copydoc fs::viperfs::ViperFS::fsync */
bool ViperFS::fsync(Inode *inode)
{
    if (!mounted_ || !inode)
        return false;

    // Update access time
    inode->atime = timer::get_ms();

    // Write inode metadata to disk
    if (!write_inode(inode))
    {
        return false;
    }

    // Sync all dirty blocks for this file
    // Note: In a more sophisticated implementation, we would track
    // which blocks belong to this file. For now, sync all dirty blocks.
    cache().sync();

    return true;
}

/** @copydoc fs::viperfs::ViperFS::add_dir_entry */
bool ViperFS::add_dir_entry(Inode *dir, u64 ino, const char *name, usize name_len, u8 type)
{
    if (!mounted_ || !dir || !name)
        return false;
    if (!is_directory(dir))
        return false;
    if (name_len > MAX_NAME_LEN)
        return false;

    u16 needed_len = dir_entry_size(static_cast<u8>(name_len));

    // Scan directory for space
    u64 offset = 0;
    u8 buf[BLOCK_SIZE];

    while (offset < dir->size)
    {
        i64 r = read_data(dir, offset, buf, BLOCK_SIZE);
        if (r <= 0)
            break;

        usize pos = 0;
        while (pos < static_cast<usize>(r))
        {
            DirEntry *entry = reinterpret_cast<DirEntry *>(buf + pos);

            if (entry->rec_len == 0)
                break;

            // Validate rec_len to prevent malformed directory entries
            if (entry->rec_len < DIR_ENTRY_MIN_SIZE ||
                pos + entry->rec_len > static_cast<usize>(r))
            {
                serial::puts("[viperfs] ERROR: Invalid rec_len in add_dirent\n");
                return false;
            }

            // Calculate actual size of this entry
            u16 actual_size = dir_entry_size(entry->name_len);
            if (entry->rec_len < actual_size)
            {
                serial::puts("[viperfs] ERROR: rec_len too small for entry\n");
                return false;
            }
            u16 remaining = entry->rec_len - actual_size;

            if (remaining >= needed_len)
            {
                // Found space - split this entry
                // Modify existing entry's rec_len
                entry->rec_len = actual_size;

                // Create new entry
                DirEntry *new_entry = reinterpret_cast<DirEntry *>(buf + pos + actual_size);
                new_entry->inode = ino;
                new_entry->rec_len = remaining;
                new_entry->name_len = static_cast<u8>(name_len);
                new_entry->file_type = type;
                for (usize i = 0; i < name_len; i++)
                {
                    new_entry->name[i] = name[i];
                }

                // Write block back
                if (write_data(dir, offset, buf, BLOCK_SIZE) != BLOCK_SIZE)
                {
                    return false;
                }
                return true;
            }

            pos += entry->rec_len;
        }

        offset += static_cast<u64>(r);
    }

    // No space in existing blocks - allocate new block
    u8 new_block[BLOCK_SIZE] = {};
    DirEntry *entry = reinterpret_cast<DirEntry *>(new_block);
    entry->inode = ino;
    entry->rec_len = BLOCK_SIZE;
    entry->name_len = static_cast<u8>(name_len);
    entry->file_type = type;
    for (usize i = 0; i < name_len; i++)
    {
        entry->name[i] = name[i];
    }

    if (write_data(dir, dir->size, new_block, BLOCK_SIZE) != BLOCK_SIZE)
    {
        return false;
    }

    return true;
}

/** @copydoc fs::viperfs::ViperFS::create_file */
u64 ViperFS::create_file(Inode *dir, const char *name, usize name_len)
{
    if (!mounted_ || !dir || !name)
        return 0;
    if (!is_directory(dir))
        return 0;

    SpinlockGuard guard(fs_lock_);

    // Check if name already exists
    if (lookup(dir, name, name_len) != 0)
    {
        serial::puts("[viperfs] File already exists\n");
        return 0;
    }

    // Begin journaled transaction
    Transaction *txn = nullptr;
    if (journal().is_enabled())
    {
        txn = journal().begin();
    }

    // Allocate inode
    u64 ino = alloc_inode_unlocked();
    if (ino == 0)
    {
        serial::puts("[viperfs] No free inodes\n");
        if (txn)
            journal().abort(txn);
        return 0;
    }

    // Initialize inode
    Inode new_inode = {};
    new_inode.inode_num = ino;
    new_inode.mode = mode::TYPE_FILE | mode::PERM_READ | mode::PERM_WRITE;
    new_inode.size = 0;
    new_inode.blocks = 0;

    // Set creation time
    u64 now = timer::get_ms();
    new_inode.atime = now;
    new_inode.mtime = now;
    new_inode.ctime = now;

    // Log inode block before modification
    if (txn)
    {
        u64 inode_blk = inode_block(ino);
        CacheBlock *blk = cache().get(inode_blk);
        if (blk)
        {
            journal().log_block(txn, inode_blk, blk->data);
            cache().release(blk);
        }
    }

    // Write inode to disk
    if (!write_inode(&new_inode))
    {
        free_inode_unlocked(ino);
        if (txn)
            journal().abort(txn);
        return 0;
    }

    // Add directory entry
    if (!add_dir_entry(dir, ino, name, name_len, file_type::FILE))
    {
        free_inode_unlocked(ino);
        if (txn)
            journal().abort(txn);
        return 0;
    }

    // Update directory inode
    write_inode(dir);

    // Commit the transaction
    if (txn)
    {
        if (!journal().commit(txn))
        {
            serial::puts("[viperfs] Warning: journal commit failed\n");
        }
    }

    return ino;
}

/** @copydoc fs::viperfs::ViperFS::create_dir */
u64 ViperFS::create_dir(Inode *dir, const char *name, usize name_len)
{
    if (!mounted_ || !dir || !name)
        return 0;
    if (!is_directory(dir))
        return 0;

    SpinlockGuard guard(fs_lock_);

    // Check if name already exists
    if (lookup(dir, name, name_len) != 0)
    {
        serial::puts("[viperfs] Directory already exists\n");
        return 0;
    }

    // Allocate inode
    u64 ino = alloc_inode_unlocked();
    if (ino == 0)
    {
        serial::puts("[viperfs] No free inodes\n");
        return 0;
    }

    // Allocate data block for directory entries
    u64 data_block = alloc_block_unlocked();
    if (data_block == 0)
    {
        free_inode_unlocked(ino);
        serial::puts("[viperfs] No free blocks\n");
        return 0;
    }

    // Initialize inode
    Inode new_inode = {};
    new_inode.inode_num = ino;
    new_inode.mode = mode::TYPE_DIR | mode::PERM_READ | mode::PERM_WRITE | mode::PERM_EXEC;
    new_inode.size = BLOCK_SIZE;
    new_inode.blocks = 1;
    new_inode.direct[0] = data_block;

    // Set creation time
    u64 now = timer::get_ms();
    new_inode.atime = now;
    new_inode.mtime = now;
    new_inode.ctime = now;

    // Create . and .. entries
    u8 dir_data[BLOCK_SIZE] = {};
    usize pos = 0;

    // Entry for "."
    DirEntry *dot = reinterpret_cast<DirEntry *>(dir_data + pos);
    dot->inode = ino;
    dot->rec_len = dir_entry_size(1);
    dot->name_len = 1;
    dot->file_type = file_type::DIR;
    dot->name[0] = '.';
    pos += dot->rec_len;

    // Entry for ".."
    DirEntry *dotdot = reinterpret_cast<DirEntry *>(dir_data + pos);
    dotdot->inode = dir->inode_num;
    dotdot->rec_len = BLOCK_SIZE - pos;
    dotdot->name_len = 2;
    dotdot->file_type = file_type::DIR;
    dotdot->name[0] = '.';
    dotdot->name[1] = '.';

    // Write directory data
    CacheBlock *block = cache().get(data_block);
    if (!block)
    {
        free_block_unlocked(data_block);
        free_inode_unlocked(ino);
        return 0;
    }
    for (usize i = 0; i < BLOCK_SIZE; i++)
    {
        block->data[i] = dir_data[i];
    }
    block->dirty = true;
    cache().release(block);

    // Write inode to disk
    if (!write_inode(&new_inode))
    {
        free_block_unlocked(data_block);
        free_inode_unlocked(ino);
        return 0;
    }

    // Add directory entry to parent
    if (!add_dir_entry(dir, ino, name, name_len, file_type::DIR))
    {
        free_block_unlocked(data_block);
        free_inode_unlocked(ino);
        return 0;
    }

    // Update parent directory inode
    write_inode(dir);

    return ino;
}

/** @copydoc fs::viperfs::ViperFS::create_symlink */
u64 ViperFS::create_symlink(
    Inode *dir, const char *name, usize name_len, const char *target, usize target_len)
{
    if (!mounted_ || !dir || !name || !target)
        return 0;
    if (!is_directory(dir))
        return 0;
    if (target_len == 0 || target_len > BLOCK_SIZE)
        return 0;

    SpinlockGuard guard(fs_lock_);

    // Check if name already exists
    if (lookup(dir, name, name_len) != 0)
    {
        serial::puts("[viperfs] Entry already exists\n");
        return 0;
    }

    // Allocate inode
    u64 ino = alloc_inode_unlocked();
    if (ino == 0)
    {
        serial::puts("[viperfs] No free inodes\n");
        return 0;
    }

    // Initialize inode
    Inode new_inode = {};
    new_inode.inode_num = ino;
    new_inode.mode = mode::TYPE_LINK | mode::PERM_READ | mode::PERM_WRITE;
    new_inode.size = target_len;
    new_inode.blocks = 0;

    // Set creation time
    u64 now = timer::get_ms();
    new_inode.atime = now;
    new_inode.mtime = now;
    new_inode.ctime = now;

    // Write inode first
    if (!write_inode(&new_inode))
    {
        free_inode_unlocked(ino);
        return 0;
    }

    // Read back and write the target path to symlink data
    Inode *inode = read_inode(ino);
    if (!inode)
    {
        free_inode_unlocked(ino);
        return 0;
    }

    i64 written = write_data(inode, 0, target, target_len);
    release_inode(inode);

    if (written != static_cast<i64>(target_len))
    {
        free_inode_data_unlocked(ino); // Free data blocks allocated by write_data
        free_inode_unlocked(ino);
        return 0;
    }

    // Add directory entry to parent
    if (!add_dir_entry(dir, ino, name, name_len, file_type::LINK))
    {
        free_inode_data_unlocked(ino); // Free data blocks
        free_inode_unlocked(ino);
        return 0;
    }

    // Update parent directory inode
    write_inode(dir);

    return ino;
}

/** @copydoc fs::viperfs::ViperFS::read_symlink */
i64 ViperFS::read_symlink(Inode *inode, char *buf, usize buf_len)
{
    if (!mounted_ || !inode || !buf)
        return -1;
    if (!is_symlink(inode))
        return -1;

    usize read_len = buf_len;
    if (read_len > inode->size)
        read_len = inode->size;

    return read_data(inode, 0, buf, read_len);
}

// Remove a directory entry by name
// Sets *out_ino to the inode number of the removed entry
/** @copydoc fs::viperfs::ViperFS::remove_dir_entry */
bool ViperFS::remove_dir_entry(Inode *dir, const char *name, usize name_len, u64 *out_ino)
{
    if (!mounted_ || !dir || !name)
        return false;
    if (!is_directory(dir))
        return false;

    u64 offset = 0;
    u8 buf[BLOCK_SIZE];

    while (offset < dir->size)
    {
        i64 r = read_data(dir, offset, buf, BLOCK_SIZE);
        if (r <= 0)
            break;

        usize pos = 0;
        DirEntry *prev = nullptr;

        while (pos < static_cast<usize>(r))
        {
            DirEntry *entry = reinterpret_cast<DirEntry *>(buf + pos);

            if (entry->rec_len == 0)
                break;

            // Validate rec_len to prevent malformed directory entries
            if (entry->rec_len < DIR_ENTRY_MIN_SIZE ||
                pos + entry->rec_len > static_cast<usize>(r))
            {
                serial::puts("[viperfs] ERROR: Invalid rec_len in unlink\n");
                return false;
            }

            // Check if this entry matches
            if (entry->inode != 0 && entry->name_len == name_len)
            {
                bool match = true;
                for (usize i = 0; i < name_len; i++)
                {
                    if (entry->name[i] != name[i])
                    {
                        match = false;
                        break;
                    }
                }
                if (match)
                {
                    // Found the entry - save inode number
                    if (out_ino)
                        *out_ino = entry->inode;

                    // Mark entry as deleted (set inode to 0)
                    // If there's a previous entry in this block, merge rec_len
                    if (prev && prev->inode != 0)
                    {
                        prev->rec_len += entry->rec_len;
                    }
                    entry->inode = 0;

                    // Write block back
                    if (write_data(dir, offset, buf, BLOCK_SIZE) != BLOCK_SIZE)
                    {
                        return false;
                    }
                    return true;
                }
            }

            prev = entry;
            pos += entry->rec_len;
        }

        offset += static_cast<u64>(r);
    }

    return false; // Entry not found
}

// Free all data blocks belonging to an inode (caller must hold fs_lock_)
/** @copydoc fs::viperfs::ViperFS::free_inode_blocks */
void ViperFS::free_inode_blocks(Inode *inode)
{
    if (!mounted_ || !inode)
        return;

    constexpr u64 PTRS_PER_BLOCK = BLOCK_SIZE / sizeof(u64);

    // Free direct blocks
    for (int i = 0; i < 12; i++)
    {
        if (inode->direct[i] != 0)
        {
            free_block_unlocked(inode->direct[i]);
            inode->direct[i] = 0;
        }
    }

    // Free single indirect blocks
    if (inode->indirect != 0)
    {
        CacheBlock *block = cache().get(inode->indirect);
        if (block)
        {
            const u64 *ptrs = reinterpret_cast<const u64 *>(block->data);
            for (u64 i = 0; i < PTRS_PER_BLOCK; i++)
            {
                if (ptrs[i] != 0)
                {
                    free_block_unlocked(ptrs[i]);
                }
            }
            cache().release(block);
        }
        free_block_unlocked(inode->indirect);
        inode->indirect = 0;
    }

    // Free double indirect blocks
    if (inode->double_indirect != 0)
    {
        CacheBlock *l1_block = cache().get(inode->double_indirect);
        if (l1_block)
        {
            const u64 *l1_ptrs = reinterpret_cast<const u64 *>(l1_block->data);
            for (u64 i = 0; i < PTRS_PER_BLOCK; i++)
            {
                if (l1_ptrs[i] != 0)
                {
                    CacheBlock *l2_block = cache().get(l1_ptrs[i]);
                    if (l2_block)
                    {
                        const u64 *l2_ptrs = reinterpret_cast<const u64 *>(l2_block->data);
                        for (u64 j = 0; j < PTRS_PER_BLOCK; j++)
                        {
                            if (l2_ptrs[j] != 0)
                            {
                                free_block_unlocked(l2_ptrs[j]);
                            }
                        }
                        cache().release(l2_block);
                    }
                    free_block_unlocked(l1_ptrs[i]);
                }
            }
            cache().release(l1_block);
        }
        free_block_unlocked(inode->double_indirect);
        inode->double_indirect = 0;
    }

    // Triple indirect not implemented
    inode->blocks = 0;
    inode->size = 0;
}

// Unlink a file from a directory
/** @copydoc fs::viperfs::ViperFS::unlink_file */
bool ViperFS::unlink_file(Inode *dir, const char *name, usize name_len)
{
    if (!mounted_ || !dir || !name)
        return false;

    // Cannot unlink . or ..
    if (name_len == 1 && name[0] == '.')
        return false;
    if (name_len == 2 && name[0] == '.' && name[1] == '.')
        return false;

    SpinlockGuard guard(fs_lock_);

    // Find the file's inode
    u64 ino = lookup(dir, name, name_len);
    if (ino == 0)
    {
        serial::puts("[viperfs] unlink: file not found\n");
        return false;
    }

    // Read the inode
    Inode *inode = read_inode(ino);
    if (!inode)
        return false;

    // Cannot unlink directories with this function
    if (is_directory(inode))
    {
        serial::puts("[viperfs] unlink: is a directory\n");
        release_inode(inode);
        return false;
    }

    // Remove directory entry
    u64 removed_ino = 0;
    if (!remove_dir_entry(dir, name, name_len, &removed_ino))
    {
        release_inode(inode);
        return false;
    }

    // Free the file's data blocks
    free_inode_blocks(inode);

    // Free the inode
    free_inode_unlocked(ino);

    release_inode(inode);
    write_inode(dir);

    return true;
}

// Check if directory is empty (only . and ..)
/**
 * @brief Determine whether a directory contains entries other than `.` and `..`.
 *
 * @details
 * Uses the filesystem's @ref ViperFS::readdir callback interface to count
 * entries and treats only `.` and `..` as ignorable.
 *
 * @param fs Filesystem instance.
 * @param dir Directory inode to inspect.
 * @return `true` if empty (excluding `.` and `..`), otherwise `false`.
 */
static bool dir_is_empty(ViperFS *fs, Inode *dir)
{
    // Count entries (excluding . and ..)
    struct CountCtx
    {
        int count;
    };

    CountCtx ctx = {0};

    fs->readdir(
        dir,
        0,
        [](const char *name, usize name_len, u64 ino, u8 file_type, void *ctx_ptr)
        {
            (void)ino;
            (void)file_type;
            auto *c = static_cast<CountCtx *>(ctx_ptr);
            // Skip . and ..
            if (name_len == 1 && name[0] == '.')
                return;
            if (name_len == 2 && name[0] == '.' && name[1] == '.')
                return;
            c->count++;
        },
        &ctx);

    return ctx.count == 0;
}

// Remove an empty directory
/** @copydoc fs::viperfs::ViperFS::rmdir */
bool ViperFS::rmdir(Inode *parent, const char *name, usize name_len)
{
    if (!mounted_ || !parent || !name)
        return false;

    // Cannot remove . or ..
    if (name_len == 1 && name[0] == '.')
        return false;
    if (name_len == 2 && name[0] == '.' && name[1] == '.')
        return false;

    SpinlockGuard guard(fs_lock_);

    // Find the directory's inode
    u64 ino = lookup(parent, name, name_len);
    if (ino == 0)
    {
        serial::puts("[viperfs] rmdir: not found\n");
        return false;
    }

    // Read the inode
    Inode *dir = read_inode(ino);
    if (!dir)
        return false;

    // Must be a directory
    if (!is_directory(dir))
    {
        serial::puts("[viperfs] rmdir: not a directory\n");
        release_inode(dir);
        return false;
    }

    // Must be empty
    if (!dir_is_empty(this, dir))
    {
        serial::puts("[viperfs] rmdir: directory not empty\n");
        release_inode(dir);
        return false;
    }

    // Remove directory entry from parent
    u64 removed_ino = 0;
    if (!remove_dir_entry(parent, name, name_len, &removed_ino))
    {
        release_inode(dir);
        return false;
    }

    // Free the directory's data blocks
    free_inode_blocks(dir);

    // Free the inode
    free_inode_unlocked(ino);

    release_inode(dir);
    write_inode(parent);

    return true;
}

// Rename/move a file or directory
/** @copydoc fs::viperfs::ViperFS::rename */
bool ViperFS::rename(Inode *old_dir,
                     const char *old_name,
                     usize old_len,
                     Inode *new_dir,
                     const char *new_name,
                     usize new_len)
{
    if (!mounted_ || !old_dir || !new_dir || !old_name || !new_name)
        return false;

    // Cannot rename . or ..
    if (old_len == 1 && old_name[0] == '.')
        return false;
    if (old_len == 2 && old_name[0] == '.' && old_name[1] == '.')
        return false;

    SpinlockGuard guard(fs_lock_);

    // Find source inode
    u64 src_ino = lookup(old_dir, old_name, old_len);
    if (src_ino == 0)
    {
        serial::puts("[viperfs] rename: source not found\n");
        return false;
    }

    // Check if destination exists
    u64 dst_ino = lookup(new_dir, new_name, new_len);
    if (dst_ino != 0)
    {
        // Destination exists - for now, fail (could implement overwrite)
        serial::puts("[viperfs] rename: destination exists\n");
        return false;
    }

    // Get file type of source
    Inode *src_inode = read_inode(src_ino);
    if (!src_inode)
        return false;
    u8 file_type = is_directory(src_inode) ? viperfs::file_type::DIR : viperfs::file_type::FILE;
    release_inode(src_inode);

    // Add new directory entry
    if (!add_dir_entry(new_dir, src_ino, new_name, new_len, file_type))
    {
        return false;
    }

    // Remove old directory entry
    u64 removed_ino = 0;
    if (!remove_dir_entry(old_dir, old_name, old_len, &removed_ino))
    {
        // Failed to remove old entry - try to remove new entry to rollback
        remove_dir_entry(new_dir, new_name, new_len, nullptr);
        return false;
    }

    // If moving a directory, update its .. entry
    if (file_type == viperfs::file_type::DIR && old_dir->inode_num != new_dir->inode_num)
    {
        Inode *moved_dir = read_inode(src_ino);
        if (moved_dir)
        {
            // Update .. entry to point to new parent
            // Read first block of directory
            u8 buf[BLOCK_SIZE];
            if (read_data(moved_dir, 0, buf, BLOCK_SIZE) > 0)
            {
                // Find .. entry (should be second entry)
                usize pos = 0;
                DirEntry *entry = reinterpret_cast<DirEntry *>(buf + pos);
                // Validate rec_len before skipping
                if (entry->rec_len >= DIR_ENTRY_MIN_SIZE && entry->rec_len < BLOCK_SIZE)
                {
                    pos += entry->rec_len; // Skip .
                }
                if (pos < BLOCK_SIZE && pos >= DIR_ENTRY_MIN_SIZE)
                {
                    DirEntry *dotdot = reinterpret_cast<DirEntry *>(buf + pos);
                    if (dotdot->name_len == 2 && dotdot->name[0] == '.' && dotdot->name[1] == '.')
                    {
                        dotdot->inode = new_dir->inode_num;
                        write_data(moved_dir, 0, buf, BLOCK_SIZE);
                    }
                }
            }
            release_inode(moved_dir);
        }
    }

    write_inode(old_dir);
    write_inode(new_dir);

    return true;
}

} // namespace fs::viperfs
