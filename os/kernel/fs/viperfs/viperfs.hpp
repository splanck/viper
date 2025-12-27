#pragma once

#include "../cache.hpp"
#include "format.hpp"

namespace fs::viperfs
{

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
    u64 create_symlink(Inode *dir, const char *name, usize name_len, const char *target, usize target_len);

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

    // Sync filesystem (write dirty blocks)
    /**
     * @brief Sync filesystem metadata and dirty blocks to disk.
     *
     * @details
     * Writes the in-memory superblock copy back to block 0 and then calls the
     * global cache sync routine to flush dirty blocks.
     */
    void sync();

  private:
    // Allocation
    /** @brief Allocate a free data block and mark it used in the bitmap. */
    u64 alloc_block();
    /** @brief Allocate and zero-initialize a new block. */
    u64 alloc_zeroed_block();
    /** @brief Mark a data block free in the bitmap. */
    void free_block(u64 block_num);
    /** @brief Allocate a free inode number. */
    u64 alloc_inode();
    /** @brief Mark an inode free in the inode table. */
    void free_inode(u64 ino);

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

    // Get block number for inode
    /** @brief Compute the inode-table block containing an inode number. */
    u64 inode_block(u64 ino);

    // Get offset within block for inode
    /** @brief Compute the byte offset within an inode-table block for an inode number. */
    u64 inode_offset(u64 ino);

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

} // namespace fs::viperfs
