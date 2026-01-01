/**
 * @file viperfs.cpp
 * @brief User-space ViperFS filesystem driver implementation.
 */

#include "viperfs.hpp"

namespace viperfs
{

// Simple memory operations (no libc)
static void *memcpy(void *dst, const void *src, usize n)
{
    u8 *d = static_cast<u8 *>(dst);
    const u8 *s = static_cast<const u8 *>(src);
    while (n--)
        *d++ = *s++;
    return dst;
}

static void *memset(void *s, int c, usize n)
{
    u8 *p = static_cast<u8 *>(s);
    while (n--)
        *p++ = static_cast<u8>(c);
    return s;
}

static int memcmp(const void *s1, const void *s2, usize n)
{
    const u8 *p1 = static_cast<const u8 *>(s1);
    const u8 *p2 = static_cast<const u8 *>(s2);
    while (n--)
    {
        if (*p1 != *p2)
            return *p1 - *p2;
        p1++;
        p2++;
    }
    return 0;
}

// ============================================================================
// BlockCache Implementation
// ============================================================================

u8 *BlockCache::get(u64 block_num)
{
    // Check if already cached
    CacheEntry *entry = find(block_num);
    if (entry)
        return entry->data;

    // Evict an entry and load
    entry = evict();
    if (!entry)
        return nullptr;

    entry->block_num = block_num;
    entry->valid = true;
    entry->dirty = false;

    if (fs_->read_block(block_num, entry->data) != 0)
    {
        entry->valid = false;
        return nullptr;
    }

    return entry->data;
}

void BlockCache::mark_dirty(u64 block_num)
{
    CacheEntry *entry = find(block_num);
    if (entry)
        entry->dirty = true;
}

void BlockCache::sync()
{
    for (usize i = 0; i < CACHE_SIZE; i++)
    {
        if (entries_[i].valid && entries_[i].dirty)
        {
            fs_->write_block(entries_[i].block_num, entries_[i].data);
            entries_[i].dirty = false;
        }
    }
}

void BlockCache::invalidate(u64 block_num)
{
    CacheEntry *entry = find(block_num);
    if (entry)
    {
        if (entry->dirty)
            fs_->write_block(block_num, entry->data);
        entry->valid = false;
    }
}

CacheEntry *BlockCache::find(u64 block_num)
{
    for (usize i = 0; i < CACHE_SIZE; i++)
    {
        if (entries_[i].valid && entries_[i].block_num == block_num)
            return &entries_[i];
    }
    return nullptr;
}

CacheEntry *BlockCache::evict()
{
    // First, look for an unused entry
    for (usize i = 0; i < CACHE_SIZE; i++)
    {
        if (!entries_[i].valid)
            return &entries_[i];
    }

    // Evict first clean entry
    for (usize i = 0; i < CACHE_SIZE; i++)
    {
        if (!entries_[i].dirty)
            return &entries_[i];
    }

    // All dirty - evict first and write back
    CacheEntry *entry = &entries_[0];
    fs_->write_block(entry->block_num, entry->data);
    entry->dirty = false;
    return entry;
}

// ============================================================================
// ViperFS Implementation
// ============================================================================

bool ViperFS::mount(BlkClient *blk)
{
    blk_ = blk;
    cache_.init(this);

    // Read superblock
    if (read_block(0, &sb_) != 0)
        return false;

    // Validate magic
    if (sb_.magic != VIPERFS_MAGIC)
        return false;

    if (sb_.version != VIPERFS_VERSION)
        return false;

    mounted_ = true;
    return true;
}

void ViperFS::unmount()
{
    if (mounted_)
    {
        sync();
        mounted_ = false;
    }
}

i32 ViperFS::read_block(u64 block_num, void *buf)
{
    return blk_->read_block(block_num, buf);
}

i32 ViperFS::write_block(u64 block_num, const void *buf)
{
    return blk_->write_block(block_num, buf);
}

void ViperFS::sync()
{
    // Write superblock
    write_block(0, &sb_);
    // Sync cache
    cache_.sync();
    // Flush to disk
    blk_->flush();
}

u64 ViperFS::inode_block(u64 ino)
{
    return sb_.inode_table_start + (ino / INODES_PER_BLOCK);
}

u64 ViperFS::inode_offset(u64 ino)
{
    return (ino % INODES_PER_BLOCK) * INODE_SIZE;
}

Inode *ViperFS::read_inode(u64 ino)
{
    if (ino == 0)
        return nullptr;

    u8 *block = cache_.get(inode_block(ino));
    if (!block)
        return nullptr;

    // Allocate and copy inode
    // Note: In user-space we'd use malloc, but for now use static allocation
    static Inode inode_storage[16];
    static u32 next_slot = 0;

    Inode *inode = &inode_storage[next_slot++ % 16];
    memcpy(inode, block + inode_offset(ino), sizeof(Inode));

    return inode;
}

void ViperFS::release_inode(Inode *inode)
{
    // In a real implementation, this would free memory
    (void)inode;
}

bool ViperFS::write_inode(Inode *inode)
{
    if (!inode)
        return false;

    u8 *block = cache_.get(inode_block(inode->inode_num));
    if (!block)
        return false;

    memcpy(block + inode_offset(inode->inode_num), inode, sizeof(Inode));
    cache_.mark_dirty(inode_block(inode->inode_num));

    return true;
}

u64 ViperFS::lookup(Inode *dir, const char *name, usize name_len)
{
    if (!dir || !is_directory(dir))
        return 0;

    u64 offset = 0;
    while (offset < dir->size)
    {
        u64 block_idx = offset / BLOCK_SIZE;
        u64 block_num = get_block_ptr(dir, block_idx);
        if (block_num == 0)
        {
            offset = (block_idx + 1) * BLOCK_SIZE;
            continue;
        }

        u8 *block = cache_.get(block_num);
        if (!block)
            return 0;

        u64 block_off = offset % BLOCK_SIZE;
        while (block_off < BLOCK_SIZE && offset < dir->size)
        {
            DirEntry *entry = reinterpret_cast<DirEntry *>(block + block_off);

            if (entry->rec_len == 0)
            {
                // Corrupted directory entry; skip to next block to avoid infinite loop.
                offset = (block_idx + 1) * BLOCK_SIZE;
                break;
            }

            if (entry->inode != 0 && entry->name_len == name_len &&
                memcmp(entry->name, name, name_len) == 0)
            {
                return entry->inode;
            }

            block_off += entry->rec_len;
            offset += entry->rec_len;
        }
    }

    return 0;
}

i32 ViperFS::readdir(Inode *dir, u64 offset, ReaddirCallback cb, void *ctx)
{
    if (!dir || !is_directory(dir))
        return -1;

    i32 count = 0;
    while (offset < dir->size)
    {
        u64 block_idx = offset / BLOCK_SIZE;
        u64 block_num = get_block_ptr(dir, block_idx);
        if (block_num == 0)
        {
            offset = (block_idx + 1) * BLOCK_SIZE;
            continue;
        }

        u8 *block = cache_.get(block_num);
        if (!block)
            return count;

        u64 block_off = offset % BLOCK_SIZE;
        while (block_off < BLOCK_SIZE && offset < dir->size)
        {
            DirEntry *entry = reinterpret_cast<DirEntry *>(block + block_off);

            if (entry->rec_len == 0)
            {
                // Corrupted directory entry; skip to next block to avoid infinite loop.
                offset = (block_idx + 1) * BLOCK_SIZE;
                break;
            }

            if (entry->inode != 0)
            {
                cb(entry->name, entry->name_len, entry->inode, entry->file_type, ctx);
                count++;
            }

            block_off += entry->rec_len;
            offset += entry->rec_len;
        }
    }

    return count;
}

i32 ViperFS::readdir_next(Inode *dir,
                          u64 *inout_offset,
                          char *name_out,
                          usize name_out_len,
                          usize *out_name_len,
                          u64 *out_ino,
                          u8 *out_file_type)
{
    if (!dir || !is_directory(dir) || !inout_offset)
        return -1;

    u64 offset = *inout_offset;

    while (offset < dir->size)
    {
        u64 block_idx = offset / BLOCK_SIZE;
        u64 block_num = get_block_ptr(dir, block_idx);
        if (block_num == 0)
        {
            offset = (block_idx + 1) * BLOCK_SIZE;
            continue;
        }

        u8 *block = cache_.get(block_num);
        if (!block)
        {
            *inout_offset = offset;
            return -1;
        }

        u64 block_off = offset % BLOCK_SIZE;
        while (block_off < BLOCK_SIZE && offset < dir->size)
        {
            DirEntry *entry = reinterpret_cast<DirEntry *>(block + block_off);
            if (entry->rec_len == 0)
            {
                // Corrupted directory entry; skip to next block.
                offset = (block_idx + 1) * BLOCK_SIZE;
                break;
            }

            u64 next_offset = offset + entry->rec_len;

            if (entry->inode != 0)
            {
                if (out_ino)
                    *out_ino = entry->inode;
                if (out_file_type)
                    *out_file_type = entry->file_type;
                if (out_name_len)
                    *out_name_len = entry->name_len;

                if (name_out && name_out_len > 0)
                {
                    usize to_copy = entry->name_len;
                    if (to_copy >= name_out_len)
                        to_copy = name_out_len - 1;
                    memcpy(name_out, entry->name, to_copy);
                    name_out[to_copy] = '\0';
                }

                *inout_offset = next_offset;
                return 1;
            }

            block_off += entry->rec_len;
            offset = next_offset;
        }
    }

    *inout_offset = offset;
    return 0;
}

i64 ViperFS::read_data(Inode *inode, u64 offset, void *buf, usize len)
{
    if (!inode || offset >= inode->size)
        return 0;

    // Clamp to file size
    if (offset + len > inode->size)
        len = inode->size - offset;

    u8 *dst = static_cast<u8 *>(buf);
    usize total = 0;

    while (len > 0)
    {
        u64 block_idx = offset / BLOCK_SIZE;
        u64 block_off = offset % BLOCK_SIZE;
        u64 to_read = BLOCK_SIZE - block_off;
        if (to_read > len)
            to_read = len;

        u64 block_num = get_block_ptr(inode, block_idx);
        if (block_num == 0)
        {
            // Sparse block - return zeros
            memset(dst, 0, to_read);
        }
        else
        {
            u8 *block = cache_.get(block_num);
            if (!block)
                break;
            memcpy(dst, block + block_off, to_read);
        }

        dst += to_read;
        offset += to_read;
        len -= to_read;
        total += to_read;
    }

    return static_cast<i64>(total);
}

i64 ViperFS::write_data(Inode *inode, u64 offset, const void *buf, usize len)
{
    if (!inode)
        return -1;

    const u8 *src = static_cast<const u8 *>(buf);
    usize total = 0;

    while (len > 0)
    {
        u64 block_idx = offset / BLOCK_SIZE;
        u64 block_off = offset % BLOCK_SIZE;
        u64 to_write = BLOCK_SIZE - block_off;
        if (to_write > len)
            to_write = len;

        u64 block_num = get_block_ptr(inode, block_idx);
        if (block_num == 0)
        {
            // Allocate new block
            block_num = alloc_block();
            if (block_num == 0)
                break;
            if (!set_block_ptr(inode, block_idx, block_num))
            {
                free_block(block_num);
                break;
            }
            inode->blocks++;
        }

        u8 *block = cache_.get(block_num);
        if (!block)
            break;

        memcpy(block + block_off, src, to_write);
        cache_.mark_dirty(block_num);

        src += to_write;
        offset += to_write;
        len -= to_write;
        total += to_write;
    }

    // Update size if extended
    if (offset > inode->size)
        inode->size = offset;

    write_inode(inode);
    return static_cast<i64>(total);
}

u64 ViperFS::get_block_ptr(Inode *inode, u64 block_idx)
{
    constexpr u64 PTRS_PER_BLOCK = BLOCK_SIZE / sizeof(u64);

    // Direct blocks (0-11)
    if (block_idx < 12)
        return inode->direct[block_idx];

    // Single indirect
    block_idx -= 12;
    if (block_idx < PTRS_PER_BLOCK)
    {
        if (inode->indirect == 0)
            return 0;
        return read_indirect(inode->indirect, block_idx);
    }

    // Double indirect
    block_idx -= PTRS_PER_BLOCK;
    if (block_idx < PTRS_PER_BLOCK * PTRS_PER_BLOCK)
    {
        if (inode->double_indirect == 0)
            return 0;
        u64 l1_idx = block_idx / PTRS_PER_BLOCK;
        u64 l2_idx = block_idx % PTRS_PER_BLOCK;
        u64 l1_block = read_indirect(inode->double_indirect, l1_idx);
        if (l1_block == 0)
            return 0;
        return read_indirect(l1_block, l2_idx);
    }

    return 0;
}

bool ViperFS::set_block_ptr(Inode *inode, u64 block_idx, u64 block_num)
{
    constexpr u64 PTRS_PER_BLOCK = BLOCK_SIZE / sizeof(u64);

    // Direct blocks
    if (block_idx < 12)
    {
        inode->direct[block_idx] = block_num;
        return true;
    }

    // Single indirect
    block_idx -= 12;
    if (block_idx < PTRS_PER_BLOCK)
    {
        if (inode->indirect == 0)
        {
            inode->indirect = alloc_block();
            if (inode->indirect == 0)
                return false;
            // Zero the indirect block
            u8 *block = cache_.get(inode->indirect);
            if (block)
            {
                memset(block, 0, BLOCK_SIZE);
                cache_.mark_dirty(inode->indirect);
            }
        }
        return write_indirect(inode->indirect, block_idx, block_num);
    }

    // TODO: Handle double/triple indirect
    return false;
}

u64 ViperFS::read_indirect(u64 block, u64 index)
{
    u8 *data = cache_.get(block);
    if (!data)
        return 0;
    return reinterpret_cast<u64 *>(data)[index];
}

bool ViperFS::write_indirect(u64 block_num, u64 index, u64 value)
{
    u8 *data = cache_.get(block_num);
    if (!data)
        return false;
    reinterpret_cast<u64 *>(data)[index] = value;
    cache_.mark_dirty(block_num);
    return true;
}

u64 ViperFS::alloc_block()
{
    // Scan bitmap for free block
    for (u64 bm_block = 0; bm_block < sb_.bitmap_blocks; bm_block++)
    {
        u8 *bitmap = cache_.get(sb_.bitmap_start + bm_block);
        if (!bitmap)
            continue;

        for (usize byte = 0; byte < BLOCK_SIZE; byte++)
        {
            if (bitmap[byte] != 0xFF)
            {
                for (u32 bit = 0; bit < 8; bit++)
                {
                    if (!(bitmap[byte] & (1 << bit)))
                    {
                        bitmap[byte] |= (1 << bit);
                        cache_.mark_dirty(sb_.bitmap_start + bm_block);
                        sb_.free_blocks--;
                        return sb_.data_start + bm_block * BLOCK_SIZE * 8 + byte * 8 + bit;
                    }
                }
            }
        }
    }
    return 0;
}

void ViperFS::free_block(u64 block_num)
{
    if (block_num < sb_.data_start)
        return;

    u64 bit_index = block_num - sb_.data_start;
    u64 bm_block = bit_index / (BLOCK_SIZE * 8);
    u64 byte = (bit_index % (BLOCK_SIZE * 8)) / 8;
    u64 bit = bit_index % 8;

    u8 *bitmap = cache_.get(sb_.bitmap_start + bm_block);
    if (bitmap)
    {
        bitmap[byte] &= ~(1 << bit);
        cache_.mark_dirty(sb_.bitmap_start + bm_block);
        sb_.free_blocks++;
    }
}

u64 ViperFS::alloc_inode()
{
    // Simple linear scan of inode table for free inode
    for (u64 ino = 1; ino < sb_.inode_count; ino++)
    {
        Inode *inode = read_inode(ino);
        if (inode && inode->mode == 0)
        {
            release_inode(inode);
            return ino;
        }
        release_inode(inode);
    }
    return 0;
}

void ViperFS::free_inode(u64 ino)
{
    Inode *inode = read_inode(ino);
    if (inode)
    {
        memset(inode, 0, sizeof(Inode));
        inode->inode_num = ino;
        write_inode(inode);
        release_inode(inode);
    }
}

bool ViperFS::add_dir_entry(Inode *dir, u64 ino, const char *name, usize name_len, u8 type)
{
    u16 new_rec_len = dir_entry_size(static_cast<u8>(name_len));

    // Find space in existing blocks or at end
    u64 offset = 0;
    while (offset < dir->size)
    {
        u64 block_idx = offset / BLOCK_SIZE;
        u64 block_num = get_block_ptr(dir, block_idx);
        if (block_num == 0)
        {
            offset = (block_idx + 1) * BLOCK_SIZE;
            continue;
        }

        u8 *block = cache_.get(block_num);
        if (!block)
            return false;

        u64 block_off = offset % BLOCK_SIZE;
        while (block_off < BLOCK_SIZE && offset < dir->size)
        {
            DirEntry *entry = reinterpret_cast<DirEntry *>(block + block_off);

            if (entry->rec_len == 0)
            {
                // Corrupted directory entry; skip to next block to avoid infinite loop.
                offset = (block_idx + 1) * BLOCK_SIZE;
                break;
            }

            // Check if this entry has space after it
            u16 real_size = dir_entry_size(entry->name_len);
            u16 available = entry->rec_len - real_size;

            if (entry->inode == 0 && entry->rec_len >= new_rec_len)
            {
                // Reuse deleted entry
                entry->inode = ino;
                entry->name_len = static_cast<u8>(name_len);
                entry->file_type = type;
                memcpy(entry->name, name, name_len);
                cache_.mark_dirty(block_num);
                return true;
            }

            if (available >= new_rec_len)
            {
                // Split this entry
                entry->rec_len = real_size;

                DirEntry *new_entry = reinterpret_cast<DirEntry *>(block + block_off + real_size);
                new_entry->inode = ino;
                new_entry->rec_len = available;
                new_entry->name_len = static_cast<u8>(name_len);
                new_entry->file_type = type;
                memcpy(new_entry->name, name, name_len);
                cache_.mark_dirty(block_num);
                return true;
            }

            block_off += entry->rec_len;
            offset += entry->rec_len;
        }
    }

    // Need new block at end
    u64 block_num = alloc_block();
    if (block_num == 0)
        return false;

    u64 block_idx = dir->size / BLOCK_SIZE;
    if (!set_block_ptr(dir, block_idx, block_num))
    {
        free_block(block_num);
        return false;
    }

    u8 *block = cache_.get(block_num);
    if (!block)
        return false;

    memset(block, 0, BLOCK_SIZE);

    DirEntry *entry = reinterpret_cast<DirEntry *>(block);
    entry->inode = ino;
    entry->rec_len = BLOCK_SIZE;
    entry->name_len = static_cast<u8>(name_len);
    entry->file_type = type;
    memcpy(entry->name, name, name_len);

    cache_.mark_dirty(block_num);
    dir->size += BLOCK_SIZE;
    dir->blocks++;
    write_inode(dir);

    return true;
}

bool ViperFS::remove_dir_entry(Inode *dir, const char *name, usize name_len, u64 *out_ino)
{
    u64 offset = 0;
    while (offset < dir->size)
    {
        u64 block_idx = offset / BLOCK_SIZE;
        u64 block_num = get_block_ptr(dir, block_idx);
        if (block_num == 0)
        {
            offset = (block_idx + 1) * BLOCK_SIZE;
            continue;
        }

        u8 *block = cache_.get(block_num);
        if (!block)
            return false;

        u64 block_off = offset % BLOCK_SIZE;
        while (block_off < BLOCK_SIZE && offset < dir->size)
        {
            DirEntry *entry = reinterpret_cast<DirEntry *>(block + block_off);

            if (entry->rec_len == 0)
            {
                // Corrupted directory entry; skip to next block to avoid infinite loop.
                offset = (block_idx + 1) * BLOCK_SIZE;
                break;
            }

            if (entry->inode != 0 && entry->name_len == name_len &&
                memcmp(entry->name, name, name_len) == 0)
            {
                if (out_ino)
                    *out_ino = entry->inode;
                entry->inode = 0;
                cache_.mark_dirty(block_num);
                return true;
            }

            block_off += entry->rec_len;
            offset += entry->rec_len;
        }
    }
    return false;
}

u64 ViperFS::create_file(Inode *dir, const char *name, usize name_len)
{
    // Check if already exists
    if (lookup(dir, name, name_len) != 0)
        return 0;

    // Allocate inode
    u64 ino = alloc_inode();
    if (ino == 0)
        return 0;

    // Initialize inode
    Inode *inode = read_inode(ino);
    if (!inode)
        return 0;

    memset(inode, 0, sizeof(Inode));
    inode->inode_num = ino;
    inode->mode = mode::TYPE_FILE | mode::PERM_READ | mode::PERM_WRITE;
    // TODO: Set timestamps

    write_inode(inode);
    release_inode(inode);

    // Add directory entry
    if (!add_dir_entry(dir, ino, name, name_len, file_type::FILE))
    {
        free_inode(ino);
        return 0;
    }

    return ino;
}

u64 ViperFS::create_dir(Inode *dir, const char *name, usize name_len)
{
    // Check if already exists
    if (lookup(dir, name, name_len) != 0)
        return 0;

    // Allocate inode
    u64 ino = alloc_inode();
    if (ino == 0)
        return 0;

    // Initialize inode
    Inode *inode = read_inode(ino);
    if (!inode)
        return 0;

    memset(inode, 0, sizeof(Inode));
    inode->inode_num = ino;
    inode->mode = mode::TYPE_DIR | mode::PERM_READ | mode::PERM_WRITE | mode::PERM_EXEC;

    write_inode(inode);

    // Add . and .. entries
    add_dir_entry(inode, ino, ".", 1, file_type::DIR);
    add_dir_entry(inode, dir->inode_num, "..", 2, file_type::DIR);

    release_inode(inode);

    // Add entry in parent
    if (!add_dir_entry(dir, ino, name, name_len, file_type::DIR))
    {
        free_inode(ino);
        return 0;
    }

    return ino;
}

void ViperFS::free_inode_blocks(Inode *inode)
{
    // Free direct blocks
    for (u32 i = 0; i < 12; i++)
    {
        if (inode->direct[i] != 0)
        {
            free_block(inode->direct[i]);
            inode->direct[i] = 0;
        }
    }

    // Free indirect block and its contents
    if (inode->indirect != 0)
    {
        u8 *block = cache_.get(inode->indirect);
        if (block)
        {
            u64 *ptrs = reinterpret_cast<u64 *>(block);
            for (usize i = 0; i < BLOCK_SIZE / sizeof(u64); i++)
            {
                if (ptrs[i] != 0)
                    free_block(ptrs[i]);
            }
        }
        free_block(inode->indirect);
        inode->indirect = 0;
    }

    // TODO: Handle double/triple indirect
}

bool ViperFS::unlink_file(Inode *dir, const char *name, usize name_len)
{
    u64 ino = 0;
    if (!remove_dir_entry(dir, name, name_len, &ino))
        return false;

    Inode *inode = read_inode(ino);
    if (!inode)
        return false;

    // Free blocks and inode
    free_inode_blocks(inode);
    release_inode(inode);
    free_inode(ino);

    return true;
}

bool ViperFS::rmdir(Inode *parent, const char *name, usize name_len)
{
    u64 ino = lookup(parent, name, name_len);
    if (ino == 0)
        return false;

    Inode *dir = read_inode(ino);
    if (!dir || !is_directory(dir))
    {
        release_inode(dir);
        return false;
    }

    // Check if empty (only . and ..)
    i32 count = 0;
    readdir(
        dir,
        0,
        [](const char *, usize, u64, u8, void *ctx) { (*static_cast<i32 *>(ctx))++; },
        &count);

    if (count > 2)
    {
        release_inode(dir);
        return false;
    }

    release_inode(dir);
    return unlink_file(parent, name, name_len);
}

bool ViperFS::rename(Inode *old_dir,
                     const char *old_name,
                     usize old_len,
                     Inode *new_dir,
                     const char *new_name,
                     usize new_len)
{
    // Look up the old entry
    u64 ino = lookup(old_dir, old_name, old_len);
    if (ino == 0)
        return false;

    Inode *inode = read_inode(ino);
    if (!inode)
        return false;

    u8 type = mode_to_file_type(inode->mode);
    release_inode(inode);

    // Add to new location
    if (!add_dir_entry(new_dir, ino, new_name, new_len, type))
        return false;

    // Remove from old location
    u64 dummy;
    if (!remove_dir_entry(old_dir, old_name, old_len, &dummy))
    {
        // Rollback
        remove_dir_entry(new_dir, new_name, new_len, &dummy);
        return false;
    }

    return true;
}

} // namespace viperfs
