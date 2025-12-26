#pragma once

#include "../../../include/viperos/fs_types.hpp"
#include "../../include/types.hpp"

namespace fs::vfs
{

/**
 * @file vfs.hpp
 * @brief Virtual File System (VFS) API and per-process file descriptor table.
 *
 * @details
 * The VFS layer provides a stable, syscall-facing API for basic file and
 * directory operations. It sits above a concrete filesystem implementation
 * (currently ViperFS) and provides:
 * - Path resolution to inodes.
 * - File descriptor allocation and tracking.
 * - Convenience wrappers for common operations (open/read/write/seek/stat).
 *
 * The current implementation uses a single global file descriptor table as a
 * bring-up simplification. The structure is designed to evolve to a per-process
 * table once user processes are fully supported.
 */

// Re-export shared types and constants into the vfs namespace for compatibility
using viper::DirEnt;
using viper::MAX_PATH;
using viper::Stat;

/**
 * @brief Open flags compatible with the syscall ABI.
 *
 * @details
 * These flags are re-exported from the shared fs_types.hpp for VFS use.
 */
namespace flags
{
using namespace viper::open_flags;
}

/**
 * @brief Seek origin constants for @ref lseek.
 */
namespace seek
{
using namespace viper::seek_whence;
}

// Maximum file descriptors per process
/** @brief Maximum number of file descriptors in one FD table. */
constexpr usize MAX_FDS = 32;

// File descriptor entry
/**
 * @brief One open file descriptor entry.
 *
 * @details
 * Stores inode number, current file offset, and open flags.
 */
struct FileDesc
{
    bool in_use;
    u64 inode_num; // Inode number
    u64 offset;    // Current file position
    u32 flags;     // Open flags
};

// File descriptor table (per-process)
/**
 * @brief File descriptor table for a process.
 *
 * @details
 * Provides allocation and lookup of file descriptor indices. The current kernel
 * uses a single global instance as a placeholder for per-process tables.
 */
struct FDTable
{
    FileDesc fds[MAX_FDS];

    /**
     * @brief Initialize the table, marking all descriptors free.
     */
    void init()
    {
        for (usize i = 0; i < MAX_FDS; i++)
        {
            fds[i].in_use = false;
        }
    }

    /**
     * @brief Allocate a free file descriptor index.
     *
     * @return File descriptor index on success, or -1 if table is full.
     */
    i32 alloc()
    {
        for (usize i = 0; i < MAX_FDS; i++)
        {
            if (!fds[i].in_use)
            {
                fds[i].in_use = true;
                return static_cast<i32>(i);
            }
        }
        return -1; // No free FDs
    }

    /**
     * @brief Free a file descriptor index.
     *
     * @param fd File descriptor index.
     */
    void free(i32 fd)
    {
        if (fd >= 0 && static_cast<usize>(fd) < MAX_FDS)
        {
            fds[fd].in_use = false;
        }
    }

    /**
     * @brief Look up an active file descriptor entry.
     *
     * @param fd File descriptor index.
     * @return Pointer to entry if valid and in-use, otherwise `nullptr`.
     */
    FileDesc *get(i32 fd)
    {
        if (fd >= 0 && static_cast<usize>(fd) < MAX_FDS && fds[fd].in_use)
        {
            return &fds[fd];
        }
        return nullptr;
    }
};

// Initialize VFS
/**
 * @brief Initialize the VFS layer.
 *
 * @details
 * Initializes the current file descriptor table and prints diagnostics.
 */
void init();

// VFS operations (uses current process's FD table)
/**
 * @brief Open a path and return a file descriptor.
 *
 * @details
 * Resolves the path to an inode. If the inode does not exist and O_CREAT is
 * specified, attempts to create a new file in the parent directory.
 *
 * @param path NUL-terminated path string.
 * @param flags Open flags bitmask.
 * @return File descriptor index on success, or -1 on error.
 */
i32 open(const char *path, u32 flags);
/**
 * @brief Close an open file descriptor.
 *
 * @details
 * Closing a descriptor releases the slot in the current process's
 * file-descriptor table. The current VFS design does not maintain per-open-file
 * kernel objects beyond the table entry, so close does not flush or sync file
 * data on its own. File data is written through to ViperFS as operations occur.
 *
 * After a successful close, the `fd` must not be used again unless re-opened.
 *
 * @param fd File descriptor index returned from @ref open.
 * @return 0 on success, or -1 if `fd` is invalid or not currently in use.
 */
i32 close(i32 fd);
/**
 * @brief Read bytes from a file descriptor.
 *
 * @details
 * Reads up to `len` bytes from the file associated with `fd` starting at the
 * current file offset. The file offset is advanced by the number of bytes
 * successfully read.
 *
 * The current implementation enforces the basic access mode:
 * - If `fd` was opened with `O_WRONLY`, read fails.
 * - Otherwise read is permitted.
 *
 * EOF is reported by returning 0 (no more bytes available). Errors are
 * reported by returning -1.
 *
 * @param fd File descriptor index.
 * @param buf Output buffer to fill.
 * @param len Maximum number of bytes to read.
 * @return Number of bytes read (0 at EOF), or -1 on error.
 */
i64 read(i32 fd, void *buf, usize len);
/**
 * @brief Write bytes to a file descriptor.
 *
 * @details
 * Writes up to `len` bytes from `buf` to the file associated with `fd`,
 * beginning at the current file offset. The file offset is advanced by the
 * number of bytes successfully written. If the write extends the file, the
 * underlying inode metadata is updated accordingly.
 *
 * The current implementation enforces the basic access mode:
 * - If `fd` was opened with `O_RDONLY`, write fails.
 * - Otherwise write is permitted.
 *
 * Note that this API currently does not expose `fsync`-style durability.
 * Directory-changing operations perform a filesystem sync, but normal writes
 * rely on ViperFS/cache policy during bring-up.
 *
 * @param fd File descriptor index.
 * @param buf Input buffer to write.
 * @param len Number of bytes requested to write.
 * @return Number of bytes written, or -1 on error.
 */
i64 write(i32 fd, const void *buf, usize len);
/**
 * @brief Seek within a file descriptor.
 *
 * @details
 * Updates the current file offset used by @ref read and @ref write.
 *
 * - `seek::SET`: set offset to `offset`
 * - `seek::CUR`: add `offset` to the current position
 * - `seek::END`: add `offset` to the file size (reads inode size)
 *
 * Seeking to a negative position fails. Seeking beyond end-of-file is allowed
 * and will affect subsequent reads/writes according to the underlying
 * filesystem behavior.
 *
 * @param fd File descriptor index.
 * @param offset Offset value (interpretation depends on `whence`).
 * @param whence Seek origin from @ref seek.
 * @return New absolute file offset on success, or -1 on error.
 */
i64 lseek(i32 fd, i64 offset, i32 whence);
/**
 * @brief Get metadata for a path.
 *
 * @details
 * Resolves `path` to an inode and fills a simplified @ref Stat record with
 * inode number, mode, size, block count and timestamps.
 *
 * This VFS does not currently support symbolic links, mount namespaces, or
 * alternate roots; resolution always begins at the filesystem root.
 *
 * @param path NUL-terminated path string.
 * @param st Output metadata structure.
 * @return 0 on success, or -1 on error.
 */
i32 stat(const char *path, Stat *st);
/**
 * @brief Get metadata for an open file descriptor.
 *
 * @details
 * Fills a simplified @ref Stat record for the inode referenced by `fd`.
 * Equivalent to @ref stat, but uses an already-resolved inode number rather
 * than performing path resolution.
 *
 * @param fd File descriptor index.
 * @param st Output metadata structure.
 * @return 0 on success, or -1 on error.
 */
i32 fstat(i32 fd, Stat *st);
/**
 * @brief Read directory entries into a caller buffer.
 *
 * @details
 * Reads directory contents from the directory referenced by `fd` and packs
 * entries into `buf` as a sequence of variable-length @ref DirEnt records.
 *
 * The record size (`DirEnt::reclen`) is aligned to 8 bytes. Names are copied as
 * NUL-terminated strings and may be truncated to 255 bytes to fit in the fixed
 * @ref DirEnt storage.
 *
 * The current implementation is intentionally simple: it reads all available
 * entries in one pass and then advances the directory offset to an end-of-file
 * sentinel so repeated calls return 0. If `len` is too small to fit even one
 * entry, 0 may be returned without advancing the offset.
 *
 * @param fd File descriptor index referring to a directory.
 * @param buf Output buffer to fill with @ref DirEnt records.
 * @param len Capacity of `buf` in bytes.
 * @return Number of bytes written to `buf`, or -1 on error.
 */
i64 getdents(i32 fd, void *buf, usize len);

// Directory operations
/**
 * @brief Create a new directory at `path`.
 *
 * @details
 * Creates a new directory entry in the parent directory implied by `path`.
 * The parent is resolved via the normal VFS path-walking logic.
 *
 * This is a bring-up API with simplified behavior:
 * - Creation fails if an entry already exists at `path`.
 * - On success, the filesystem is explicitly synced.
 *
 * @param path Directory path to create.
 * @return 0 on success, or -1 on error.
 */
i32 mkdir(const char *path);
/**
 * @brief Remove an empty directory at `path`.
 *
 * @details
 * Removes the directory entry named by `path` from its parent directory.
 * The underlying filesystem rejects attempts to remove non-empty directories.
 *
 * On success, the filesystem is explicitly synced.
 *
 * @param path Directory path to remove.
 * @return 0 on success, or -1 on error.
 */
i32 rmdir(const char *path);
/**
 * @brief Unlink (remove) a file at `path`.
 *
 * @details
 * Removes the directory entry for the file named by `path`. The underlying
 * filesystem is expected to reject attempts to unlink a directory via this
 * call.
 *
 * On success, the filesystem is explicitly synced.
 *
 * @param path File path to remove.
 * @return 0 on success, or -1 on error.
 */
i32 unlink(const char *path);
/**
 * @brief Rename or move a filesystem entry.
 *
 * @details
 * Renames the entry at `old_path` to `new_path`. If the parent directory
 * changes, the entry is moved between directories.
 *
 * The current ViperFS-backed implementation:
 * - Fails if the destination already exists (no overwrite semantics yet).
 * - Rejects renaming `.` and `..`.
 * - Updates the moved directory's `..` entry when moving directories.
 *
 * On success, the filesystem is explicitly synced.
 *
 * @param old_path Existing path to rename.
 * @param new_path New path (destination name and/or parent).
 * @return 0 on success, or -1 on error.
 */
i32 rename(const char *old_path, const char *new_path);

// Get file descriptor table for current process
/**
 * @brief Get the current process file descriptor table.
 *
 * @details
 * Currently returns a global FD table. Future versions should return the table
 * belonging to the current task/process.
 *
 * @return Pointer to FD table.
 */
FDTable *current_fdt();

// Path resolution: given path, get inode number (0 if not found)
/**
 * @brief Resolve a path to an inode number.
 *
 * @details
 * Walks path components from the filesystem root and looks up each component in
 * the corresponding directory inode.
 *
 * @param path NUL-terminated path string.
 * @return Inode number on success, or 0 if not found.
 */
u64 resolve_path(const char *path);

} // namespace fs::vfs
