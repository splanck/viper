//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: kernel/fs/viperfs/viperfs.hpp
// Purpose: ViperFS filesystem driver interface.
// Key invariants: Inodes ref-counted; blocks cached; spinlock protects metadata.
// Ownership/Lifetime: Global singleton; mounted once at boot.
// Links: kernel/fs/viperfs/viperfs.cpp, kernel/fs/viperfs/format.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "../../lib/spinlock.hpp"
#include "../cache.hpp"
#include "format.hpp"

namespace fs::viperfs
{

// ============================================================================
// Inode Cache
// ============================================================================

/** @brief Number of inodes to cache. */
constexpr usize INODE_CACHE_SIZE = 32;

/** @brief Hash table size for inode lookup. */
constexpr usize INODE_HASH_SIZE = 16;

/**
 * @brief Cached inode entry with reference counting.
 *
 * @details
 * Wraps an on-disk Inode with caching metadata including reference count,
 * dirty flag, and LRU/hash chain pointers.
 */
struct CachedInode
{
    Inode inode;            // Copy of on-disk inode
    u32 refcount;           // Reference count
    bool valid;             // Entry is valid
    bool dirty;             // Inode modified, needs write-back
    CachedInode *lru_prev;  // LRU list previous
    CachedInode *lru_next;  // LRU list next
    CachedInode *hash_next; // Hash chain next
};

/**
 * @brief LRU inode cache with reference counting.
 *
 * @details
 * Caches recently accessed inodes to reduce disk I/O and provide
 * consistent inode views across multiple references.
 */
class InodeCache
{
  public:
    /**
     * @brief Initialize the inode cache.
     */
    void init();

    /**
     * @brief Get an inode from cache or load from disk.
     *
     * @param ino Inode number.
     * @return Pointer to cached inode, or nullptr on failure.
     */
    CachedInode *get(u64 ino);

    /**
     * @brief Release a cached inode reference.
     *
     * @param ci Cached inode pointer.
     */
    void release(CachedInode *ci);

    /**
     * @brief Write a dirty inode back to disk.
     *
     * @param ci Cached inode to sync.
     * @return true on success.
     */
    bool sync(CachedInode *ci);

    /**
     * @brief Sync all dirty inodes to disk.
     */
    void sync_all();

    /**
     * @brief Invalidate a cached inode (remove from cache).
     *
     * @param ino Inode number to invalidate.
     */
    void invalidate(u64 ino);

    /**
     * @brief Dump cache statistics.
     */
    void dump_stats();

  private:
    CachedInode entries_[INODE_CACHE_SIZE];
    CachedInode *hash_[INODE_HASH_SIZE];
    CachedInode *lru_head_;
    CachedInode *lru_tail_;

    u64 hits_;
    u64 misses_;

    u32 hash_func(u64 ino);
    CachedInode *find(u64 ino);
    CachedInode *evict();
    void touch(CachedInode *ci);
    void remove_from_lru(CachedInode *ci);
    void add_to_lru_head(CachedInode *ci);
    void insert_hash(CachedInode *ci);
    void remove_hash(CachedInode *ci);

    // Disk I/O helpers (use ViperFS methods)
    bool load_inode(u64 ino, Inode *out);
    bool store_inode(const Inode *inode);
};

/**
 * @file viperfs.hpp
 * @brief ViperFS filesystem driver interface.
 *
 * @details
 * ViperFS is a simple block-based filesystem used by ViperOS. The driver uses
 * the global block cache (`fs::cache`) to access on-disk blocks and provides
 * operations required by the VFS layer:
 * - Mounting/unmounting a filesystem.
 * - Inode loading and writing.
 * - Directory lookup and enumeration.
 * - Reading and writing file data via direct and indirect block pointers.
 * - Creating/removing files and directories.
 *
 * The driver is intentionally minimal and optimized for bring-up rather than
 * advanced POSIX semantics. Many operations perform synchronous writes via the
 * cache sync path.
 */

/**
 * @brief ViperFS filesystem driver instance.
 *
 * @details
 * The driver maintains an in-memory copy of the superblock and relies on the
 * block cache to buffer disk I/O. Inodes returned by @ref read_inode are heap
 * allocated and must be released by callers via @ref release_inode.
 */
class ViperFS
{
  public:
    /**
     * @brief Mount the filesystem.
     *
     * @details
     * Reads and validates the superblock from block 0 and marks the filesystem
     * mounted. On success, the driver is ready to resolve paths and perform file
     * operations.
     *
     * @return `true` on success, otherwise `false`.
     */
    bool mount();

    /**
     * @brief Unmount the filesystem.
     *
     * @details
     * Writes back dirty metadata (superblock via cache) and syncs the block
     * cache, then marks the filesystem unmounted.
     */
    void unmount();

    /** @brief Whether the filesystem is currently mounted. */
    bool is_mounted() const
    {
        return mounted_;
    }

    // Filesystem info
    /** @brief Volume label from the superblock. */
    const char *label() const
    {
        return sb_.label;
    }

    /** @brief Total number of blocks on disk. */
    u64 total_blocks() const
    {
        return sb_.total_blocks;
    }

    /** @brief Current free block count (tracked in superblock). */
    u64 free_blocks() const
    {
        return sb_.free_blocks;
    }

    /** @brief Root directory inode number. */
    u64 root_inode() const
    {
        return sb_.root_inode;
    }

    // Inode operations
    // Read an inode from disk. Caller must call release_inode() when done.
    /**
     * @brief Read an inode from disk into a heap-allocated structure.
     *
     * @details
     * Loads the inode table block containing `ino`, copies the inode data into a
     * newly allocated @ref Inode, and returns it.
     *
     * @param ino Inode number to read.
     * @return Pointer to allocated inode, or `nullptr` on failure.
     */
    Inode *read_inode(u64 ino);

    // Release an inode (frees memory)
    /**
     * @brief Release an inode returned by @ref read_inode.
     *
     * @param inode Inode pointer (may be `nullptr`).
     */
    void release_inode(Inode *inode);

    // Directory operations
    // Lookup a name in a directory. Returns inode number, or 0 if not found.
    /**
     * @brief Look up a directory entry by name.
     *
     * @details
     * Scans directory entries in `dir` and returns the inode number associated
     * with the matching name, or 0 if not found.
     *
     * @param dir Directory inode (must be a directory).
     * @param name Entry name bytes.
     * @param name_len Length of `name`.
     * @return Inode number, or 0 if not found.
     */
    u64 lookup(Inode *dir, const char *name, usize name_len);

    // Callback for readdir
    using ReaddirCallback =
        void (*)(const char *name, usize name_len, u64 ino, u8 file_type, void *ctx);

    // Read directory entries
    /**
     * @brief Enumerate directory entries and invoke a callback for each.
     *
     * @details
     * Reads directory data starting at `offset` and calls `cb` for each valid
     * entry (inode != 0). The callback can accumulate results into `ctx`.
     *
     * @param dir Directory inode.
     * @param offset Byte offset within the directory file.
     * @param cb Callback invoked per entry.
     * @param ctx Opaque callback context pointer.
     * @return Number of entries reported, or negative on error.
     */
    i32 readdir(Inode *dir, u64 offset, ReaddirCallback cb, void *ctx);

    // File data operations
    // Read file data. Returns bytes read, or negative on error.
    /**
     * @brief Read file data from an inode.
     *
     * @details
     * Reads up to `len` bytes starting at `offset` into `buf`, clamping to the
     * file size. Sparse/unallocated blocks are returned as zero bytes.
     *
     * @param inode File inode.
     * @param offset Byte offset within the file.
     * @param buf Destination buffer.
     * @param len Maximum bytes to read.
     * @return Bytes read (0 at EOF) or negative on error.
     */
    i64 read_data(Inode *inode, u64 offset, void *buf, usize len);

    // Write file data. Returns bytes written, or negative on error.
    /**
     * @brief Write file data to an inode.
     *
     * @details
     * Writes `len` bytes from `buf` starting at `offset`, allocating data blocks
     * and indirect blocks as needed. Updates inode size and writes inode metadata
     * back to disk.
     *
     * @param inode File inode.
     * @param offset Byte offset within the file.
     * @param buf Source buffer.
     * @param len Number of bytes to write.
     * @return Bytes written or negative on error.
     */
    i64 write_data(Inode *inode, u64 offset, const void *buf, usize len);

    // Create operations
    // Create a new file in directory. Returns new inode number, or 0 on failure.
    /**
     * @brief Create a new empty file entry in a directory.
     *
     * @param dir Parent directory inode.
     * @param name File name.
     * @param name_len Length of file name.
     * @return New inode number on success, or 0 on failure.
     */
    u64 create_file(Inode *dir, const char *name, usize name_len);

    // Create a new directory. Returns new inode number, or 0 on failure.
    /**
     * @brief Create a new empty directory entry in a directory.
     *
     * @details
     * Creates a directory inode and adds an entry in `dir`. The new directory
     * will typically contain `.` and `..` entries depending on implementation.
     *
     * @param dir Parent directory inode.
     * @param name Directory name.
     * @param name_len Length of directory name.
     * @return New inode number on success, or 0 on failure.
     */
    u64 create_dir(Inode *dir, const char *name, usize name_len);

    /**
     * @brief Create a symbolic link in a directory.
     *
     * @details
     * Creates a symlink inode that points to `target` and adds an entry
     * in `dir`. The target path is stored in the inode's data blocks.
     *
     * @param dir Parent directory inode.
     * @param name Symlink name.
     * @param name_len Length of symlink name.
     * @param target Target path string.
     * @param target_len Length of target path.
     * @return New inode number on success, or 0 on failure.
     */
    u64 create_symlink(
        Inode *dir, const char *name, usize name_len, const char *target, usize target_len);

    /**
     * @brief Read the target of a symbolic link.
     *
     * @details
     * Reads the target path from the symlink inode's data.
     *
     * @param inode Symlink inode.
     * @param buf Buffer to receive target path.
     * @param buf_len Maximum bytes to read.
     * @return Number of bytes read, or negative on error.
     */
    i64 read_symlink(Inode *inode, char *buf, usize buf_len);

    // File size operations
    /**
     * @brief Truncate or extend a file to a specified size.
     *
     * @details
     * If the new size is smaller, frees any blocks beyond the new end.
     * If the new size is larger, the file is extended (sparse - reads return zeros).
     *
     * @param inode File inode.
     * @param new_size Target file size in bytes.
     * @return true on success, false on failure.
     */
    bool truncate(Inode *inode, u64 new_size);

    /**
     * @brief Sync a specific inode to disk.
     *
     * @details
     * Writes the inode metadata back to disk. For complete file sync,
     * the caller should also call cache sync.
     *
     * @param inode Inode to sync.
     * @return true on success.
     */
    bool fsync(Inode *inode);

    // Delete operations
    // Unlink a file from directory. Frees inode and blocks if no more links.
    /**
     * @brief Unlink a file from a directory.
     *
     * @details
     * Removes the directory entry, frees the inode and its blocks if it is no
     * longer referenced.
     *
     * @param dir Parent directory inode.
     * @param name Name of the entry to remove.
     * @param name_len Length of name.
     * @return `true` on success, otherwise `false`.
     */
    bool unlink_file(Inode *dir, const char *name, usize name_len);

    // Remove an empty directory from parent directory.
    /**
     * @brief Remove an empty directory.
     *
     * @param parent Parent directory inode.
     * @param name Name of directory entry.
     * @param name_len Length of name.
     * @return `true` on success, otherwise `false`.
     */
    bool rmdir(Inode *parent, const char *name, usize name_len);

    // Rename/move a file or directory
    /**
     * @brief Rename or move an entry between directories.
     *
     * @details
     * Removes the old entry and adds a new entry with the new name/location.
     *
     * @param old_dir Source directory inode.
     * @param old_name Existing entry name.
     * @param old_len Length of old name.
     * @param new_dir Destination directory inode.
     * @param new_name New entry name.
     * @param new_len Length of new name.
     * @return `true` on success, otherwise `false`.
     */
    bool rename(Inode *old_dir,
                const char *old_name,
                usize old_len,
                Inode *new_dir,
                const char *new_name,
                usize new_len);

    // Write inode back to disk
    /**
     * @brief Write an inode's metadata back to disk.
     *
     * @param inode Inode to write.
     * @return `true` on success, otherwise `false`.
     */
    bool write_inode(Inode *inode);

    // Cached inode operations
    /**
     * @brief Get a cached inode by number.
     *
     * @details
     * Returns a reference-counted cached inode. Caller must call
     * release_cached_inode() when done.
     *
     * @param ino Inode number.
     * @return Pointer to cached inode, or nullptr on failure.
     */
    CachedInode *get_cached_inode(u64 ino);

    /**
     * @brief Release a cached inode reference.
     *
     * @param ci Cached inode pointer.
     */
    void release_cached_inode(CachedInode *ci);

    /**
     * @brief Mark a cached inode as dirty.
     *
     * @param ci Cached inode pointer.
     */
    void mark_inode_dirty(CachedInode *ci);

    /**
     * @brief Sync all cached inodes to disk.
     */
    void sync_inodes();

    // Sync filesystem (write dirty blocks)
    /**
     * @brief Sync filesystem metadata and dirty blocks to disk.
     *
     * @details
     * Writes the in-memory superblock copy back to block 0 and then calls the
     * global cache sync routine to flush dirty blocks.
     */
    void sync();

    // Inode block helpers (public for InodeCache access)
    /** @brief Compute the inode-table block containing an inode number. */
    u64 inode_block(u64 ino);

    /** @brief Compute the byte offset within an inode-table block for an inode number. */
    u64 inode_offset(u64 ino);

  private:
    // Allocation - internal unlocked versions (caller must hold fs_lock_)
    /** @brief Allocate a free data block and mark it used in the bitmap (unlocked). */
    u64 alloc_block_unlocked();
    /** @brief Allocate and zero-initialize a new block (unlocked). */
    u64 alloc_zeroed_block_unlocked();
    /** @brief Mark a data block free in the bitmap (unlocked). */
    void free_block_unlocked(u64 block_num);
    /** @brief Allocate a free inode number (unlocked). */
    u64 alloc_inode_unlocked();
    /** @brief Mark an inode free in the inode table (unlocked). */
    void free_inode_unlocked(u64 ino);
    /** @brief Free all data blocks belonging to an inode (unlocked). */
    void free_inode_data_unlocked(u64 ino);

    // Add directory entry
    /** @brief Add a directory entry to a directory inode. */
    bool add_dir_entry(Inode *dir, u64 ino, const char *name, usize name_len, u8 type);

    // Remove directory entry by name (marks inode=0)
    /** @brief Remove a directory entry and optionally return the removed inode. */
    bool remove_dir_entry(Inode *dir, const char *name, usize name_len, u64 *out_ino);

    // Free all data blocks of an inode
    /** @brief Free all blocks referenced by an inode (data and indirect). */
    void free_inode_blocks(Inode *inode);

    // Set block pointer for a file block index (allocating indirect blocks as needed)
    /** @brief Set the data block pointer for a file block index, allocating indirection as needed.
     */
    bool set_block_ptr(Inode *inode, u64 block_idx, u64 block_num);

    // Write to indirect block
    /** @brief Write a pointer value into an indirect block. */
    bool write_indirect(u64 block_num, u64 index, u64 value);


    Superblock sb_;
    bool mounted_{false};
    InodeCache inode_cache_; // Inode cache instance

    // Thread safety: protects all filesystem metadata operations
    // This lock is held during:
    // - Block allocation/deallocation (bitmap updates)
    // - Inode allocation/deallocation
    // - Superblock updates (free_blocks counter)
    // - Directory modifications (add/remove entries)
    mutable Spinlock fs_lock_;

    // Get block pointer for a file block index
    /** @brief Resolve a file block index to a data block number (direct/indirect). */
    u64 get_block_ptr(Inode *inode, u64 block_idx);

    // Read an indirect block pointer
    /** @brief Read a pointer value from an indirect block. */
    u64 read_indirect(u64 block, u64 index);
};

// Global ViperFS instance
/**
 * @brief Get the global ViperFS instance.
 *
 * @return Reference to global instance.
 */
ViperFS &viperfs();

// Initialize ViperFS (mount root filesystem)
/**
 * @brief Convenience initialization for ViperFS.
 *
 * @details
 * Mounts the global ViperFS instance as the root filesystem.
 *
 * @return `true` on success, otherwise `false`.
 */
bool viperfs_init();

/**
 * @brief RAII guard for automatic inode release.
 *
 * @details
 * Automatically calls viperfs().release_inode() on destruction, ensuring
 * inodes are properly released even on early returns or exceptions.
 *
 * Usage:
 * @code
 * Inode *inode = viperfs().read_inode(ino);
 * if (!inode) return -1;
 * InodeGuard guard(inode);
 * // ... use inode ...
 * // inode automatically released when guard goes out of scope
 * @endcode
 */
class InodeGuard
{
  public:
    /**
     * @brief Construct guard and take ownership of inode.
     * @param inode The inode to guard (may be nullptr).
     */
    explicit InodeGuard(Inode *inode) : inode_(inode) {}

    /**
     * @brief Destruct guard and release inode.
     */
    ~InodeGuard()
    {
        if (inode_)
        {
            viperfs().release_inode(inode_);
        }
    }

    // Non-copyable, non-movable
    InodeGuard(const InodeGuard &) = delete;
    InodeGuard &operator=(const InodeGuard &) = delete;

    /**
     * @brief Get the guarded inode.
     * @return Pointer to the inode.
     */
    Inode *get() const { return inode_; }

    /**
     * @brief Check if a valid inode is held.
     * @return true if inode is non-null.
     */
    operator bool() const { return inode_ != nullptr; }

    /**
     * @brief Access inode members via arrow operator.
     * @return Pointer to the inode.
     */
    Inode *operator->() const { return inode_; }

    /**
     * @brief Release ownership of the inode without freeing.
     *
     * @details
     * Returns the inode pointer and clears the guard, preventing automatic
     * release. The caller takes responsibility for releasing the inode.
     *
     * @return The inode pointer (may be nullptr).
     */
    Inode *release()
    {
        Inode *tmp = inode_;
        inode_ = nullptr;
        return tmp;
    }

  private:
    Inode *inode_;
};

} // namespace fs::viperfs
