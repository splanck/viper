/**
 * @file viperfs.hpp
 * @brief User-space ViperFS filesystem driver.
 *
 * @details
 * Simplified user-space implementation of ViperFS.
 * Uses BlkClient to communicate with the block device server.
 */
#pragma once

#include "../../syscall.hpp"
#include "blk_client.hpp"
#include "format.hpp"

namespace viperfs
{

// Forward declarations
class ViperFS;

/**
 * @brief Simple block cache entry.
 */
struct CacheEntry
{
    u64 block_num;
    bool valid;
    bool dirty;
    u8 data[BLOCK_SIZE];
};

/**
 * @brief Simple block cache for ViperFS.
 */
class BlockCache
{
  public:
    static constexpr usize CACHE_SIZE = 16;

    BlockCache() : fs_(nullptr) {}

    void init(ViperFS *fs) { fs_ = fs; }

    /**
     * @brief Get a block (from cache or disk).
     */
    u8 *get(u64 block_num);

    /**
     * @brief Mark a block as dirty.
     */
    void mark_dirty(u64 block_num);

    /**
     * @brief Sync all dirty blocks to disk.
     */
    void sync();

    /**
     * @brief Invalidate a cache entry.
     */
    void invalidate(u64 block_num);

  private:
    ViperFS *fs_;
    CacheEntry entries_[CACHE_SIZE];

    CacheEntry *find(u64 block_num);
    CacheEntry *evict();
};

/**
 * @brief User-space ViperFS filesystem driver.
 */
class ViperFS
{
  public:
    ViperFS() : mounted_(false) {}

    /**
     * @brief Mount the filesystem.
     *
     * @param blk Block device client.
     * @return true on success.
     */
    bool mount(BlkClient *blk);

    /**
     * @brief Unmount the filesystem.
     */
    void unmount();

    /** @brief Check if mounted. */
    bool is_mounted() const { return mounted_; }

    // Filesystem info
    const char *label() const { return sb_.label; }
    u64 total_blocks() const { return sb_.total_blocks; }
    u64 free_blocks() const { return sb_.free_blocks; }
    u64 root_inode() const { return sb_.root_inode; }

    // Block I/O (used by cache)
    i32 read_block(u64 block_num, void *buf);
    i32 write_block(u64 block_num, const void *buf);

    // Inode operations
    Inode *read_inode(u64 ino);
    void release_inode(Inode *inode);
    bool write_inode(Inode *inode);

    // Directory operations
    u64 lookup(Inode *dir, const char *name, usize name_len);

    using ReaddirCallback = void (*)(const char *name, usize name_len,
                                     u64 ino, u8 file_type, void *ctx);
    i32 readdir(Inode *dir, u64 offset, ReaddirCallback cb, void *ctx);

    // File data operations
    i64 read_data(Inode *inode, u64 offset, void *buf, usize len);
    i64 write_data(Inode *inode, u64 offset, const void *buf, usize len);

    // Create operations
    u64 create_file(Inode *dir, const char *name, usize name_len);
    u64 create_dir(Inode *dir, const char *name, usize name_len);

    // Delete operations
    bool unlink_file(Inode *dir, const char *name, usize name_len);
    bool rmdir(Inode *parent, const char *name, usize name_len);

    // Rename
    bool rename(Inode *old_dir, const char *old_name, usize old_len,
                Inode *new_dir, const char *new_name, usize new_len);

    // Sync
    void sync();

    // Helper functions
    u64 inode_block(u64 ino);
    u64 inode_offset(u64 ino);

  private:
    Superblock sb_;
    bool mounted_;
    BlkClient *blk_;
    BlockCache cache_;

    // Allocation
    u64 alloc_block();
    void free_block(u64 block_num);
    u64 alloc_inode();
    void free_inode(u64 ino);

    // Directory helpers
    bool add_dir_entry(Inode *dir, u64 ino, const char *name, usize name_len, u8 type);
    bool remove_dir_entry(Inode *dir, const char *name, usize name_len, u64 *out_ino);

    // Block pointer helpers
    u64 get_block_ptr(Inode *inode, u64 block_idx);
    bool set_block_ptr(Inode *inode, u64 block_idx, u64 block_num);
    u64 read_indirect(u64 block, u64 index);
    bool write_indirect(u64 block_num, u64 index, u64 value);

    // Free inode blocks
    void free_inode_blocks(Inode *inode);
};

} // namespace viperfs
