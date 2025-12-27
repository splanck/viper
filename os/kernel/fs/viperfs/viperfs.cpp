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
#include "../../console/serial.hpp"
#include "../../mm/kheap.hpp"
#include "../../mm/slab.hpp"
#include "../cache.hpp"

namespace fs::viperfs
{

// Global instance
static ViperFS g_viperfs;

/** @copydoc fs::viperfs::viperfs */
ViperFS &viperfs()
{
    return g_viperfs;
}

/** @copydoc fs::viperfs::ViperFS::alloc_zeroed_block */
u64 ViperFS::alloc_zeroed_block()
{
    u64 block_num = alloc_block();
    if (block_num == 0)
        return 0;

    CacheBlock *block = cache().get(block_num);
    if (!block)
    {
        // Allocation succeeded but cache failed - should not normally happen
        // but we can't easily free the block, so just return failure
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

    return true;
}

/** @copydoc fs::viperfs::ViperFS::unmount */
void ViperFS::unmount()
{
    if (!mounted_)
        return;

    // Sync cache
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
    if (block_num == 0)
        return 0;

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

// Allocation functions
/** @copydoc fs::viperfs::ViperFS::alloc_block */
u64 ViperFS::alloc_block()
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

/** @copydoc fs::viperfs::ViperFS::free_block */
void ViperFS::free_block(u64 block_num)
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

/** @copydoc fs::viperfs::ViperFS::alloc_inode */
u64 ViperFS::alloc_inode()
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
            // Free inode found
            cache().release(block);
            return ino;
        }
        cache().release(block);
    }

    return 0;
}

/** @copydoc fs::viperfs::ViperFS::free_inode */
void ViperFS::free_inode(u64 ino)
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
    if (block_num == 0)
        return false;

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
            inode->indirect = alloc_zeroed_block();
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
            inode->double_indirect = alloc_zeroed_block();
            if (inode->double_indirect == 0)
                return false;
        }

        u64 l1_idx = block_idx / PTRS_PER_BLOCK;
        u64 l2_idx = block_idx % PTRS_PER_BLOCK;

        u64 l1_block = read_indirect(inode->double_indirect, l1_idx);
        if (l1_block == 0)
        {
            l1_block = alloc_zeroed_block();
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
            block_num = alloc_block();
            if (block_num == 0)
            {
                serial::puts("[viperfs] Out of blocks\n");
                return written > 0 ? static_cast<i64>(written) : -1;
            }
            if (!set_block_ptr(inode, block_idx, block_num))
            {
                free_block(block_num);
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

    return static_cast<i64>(written);
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

            // Calculate actual size of this entry
            u16 actual_size = dir_entry_size(entry->name_len);
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

    // Check if name already exists
    if (lookup(dir, name, name_len) != 0)
    {
        serial::puts("[viperfs] File already exists\n");
        return 0;
    }

    // Allocate inode
    u64 ino = alloc_inode();
    if (ino == 0)
    {
        serial::puts("[viperfs] No free inodes\n");
        return 0;
    }

    // Initialize inode
    Inode new_inode = {};
    new_inode.inode_num = ino;
    new_inode.mode = mode::TYPE_FILE | mode::PERM_READ | mode::PERM_WRITE;
    new_inode.size = 0;
    new_inode.blocks = 0;

    // Write inode to disk
    if (!write_inode(&new_inode))
    {
        free_inode(ino);
        return 0;
    }

    // Add directory entry
    if (!add_dir_entry(dir, ino, name, name_len, file_type::FILE))
    {
        free_inode(ino);
        return 0;
    }

    // Update directory inode
    write_inode(dir);

    return ino;
}

/** @copydoc fs::viperfs::ViperFS::create_dir */
u64 ViperFS::create_dir(Inode *dir, const char *name, usize name_len)
{
    if (!mounted_ || !dir || !name)
        return 0;
    if (!is_directory(dir))
        return 0;

    // Check if name already exists
    if (lookup(dir, name, name_len) != 0)
    {
        serial::puts("[viperfs] Directory already exists\n");
        return 0;
    }

    // Allocate inode
    u64 ino = alloc_inode();
    if (ino == 0)
    {
        serial::puts("[viperfs] No free inodes\n");
        return 0;
    }

    // Allocate data block for directory entries
    u64 data_block = alloc_block();
    if (data_block == 0)
    {
        free_inode(ino);
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
        free_block(data_block);
        free_inode(ino);
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
        free_block(data_block);
        free_inode(ino);
        return 0;
    }

    // Add directory entry to parent
    if (!add_dir_entry(dir, ino, name, name_len, file_type::DIR))
    {
        free_block(data_block);
        free_inode(ino);
        return 0;
    }

    // Update parent directory inode
    write_inode(dir);

    return ino;
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

// Free all data blocks belonging to an inode
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
            free_block(inode->direct[i]);
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
                    free_block(ptrs[i]);
                }
            }
            cache().release(block);
        }
        free_block(inode->indirect);
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
                                free_block(l2_ptrs[j]);
                            }
                        }
                        cache().release(l2_block);
                    }
                    free_block(l1_ptrs[i]);
                }
            }
            cache().release(l1_block);
        }
        free_block(inode->double_indirect);
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
    free_inode(ino);

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
    free_inode(ino);

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
                pos += entry->rec_len; // Skip .
                if (pos < BLOCK_SIZE)
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
