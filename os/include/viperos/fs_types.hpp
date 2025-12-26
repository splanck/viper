/**
 * @file fs_types.hpp
 * @brief Shared filesystem types for ViperOS kernel and user-space.
 *
 * @details
 * This header defines the common filesystem structures and constants shared
 * between kernel VFS implementation and user-space syscall wrappers.
 *
 * Structures defined here are ABI-stable and must match exactly between
 * kernel and user-space to ensure correct syscall operation.
 */

#pragma once

#include "types.hpp"

namespace viper
{

/**
 * @brief Open flags for file operations.
 *
 * @details
 * These flags are used by open syscalls and are designed to be compatible
 * with common POSIX-like conventions while remaining ViperOS-specific.
 */
namespace open_flags
{
constexpr u32 O_RDONLY = 0x0000; ///< Open for read-only access
constexpr u32 O_WRONLY = 0x0001; ///< Open for write-only access
constexpr u32 O_RDWR = 0x0002;   ///< Open for read/write access
constexpr u32 O_CREAT = 0x0040;  ///< Create file if it does not exist
constexpr u32 O_TRUNC = 0x0200;  ///< Truncate file to zero length
constexpr u32 O_APPEND = 0x0400; ///< Append mode
} // namespace open_flags

/**
 * @brief Seek origin constants for lseek operations.
 */
namespace seek_whence
{
constexpr i32 SET = 0; ///< Absolute position from start of file
constexpr i32 CUR = 1; ///< Relative to current position
constexpr i32 END = 2; ///< Relative to end of file
} // namespace seek_whence

/**
 * @brief File metadata structure.
 *
 * @details
 * This structure contains file metadata returned by stat/fstat syscalls.
 * The layout is ABI-stable between kernel and user-space.
 */
struct Stat
{
    u64 ino;    ///< Inode number (filesystem-specific)
    u32 mode;   ///< Type and permissions (kernel-defined bits)
    u64 size;   ///< File size in bytes
    u64 blocks; ///< Allocated blocks (filesystem-defined units)
    u64 atime;  ///< Last access time (epoch/units are kernel-defined)
    u64 mtime;  ///< Last modification time
    u64 ctime;  ///< Creation/change time
};

/**
 * @brief Directory entry record.
 *
 * @details
 * This structure represents a directory entry as returned by readdir/getdents.
 * The `reclen` field indicates the total record size for iterating packed
 * entries in a buffer.
 */
struct DirEnt
{
    u64 ino;        ///< Inode number for this entry
    u16 reclen;     ///< Total record length in bytes (including header + name)
    u8 type;        ///< Entry type (1=file, 2=directory)
    u8 namelen;     ///< Length of filename in bytes (excluding NUL)
    char name[256]; ///< NUL-terminated filename (may be truncated)
};

/**
 * @brief Maximum supported path length.
 */
constexpr usize MAX_PATH = 256;

} // namespace viper
