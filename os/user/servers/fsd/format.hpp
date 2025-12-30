/**
 * @file format.hpp
 * @brief On-disk format definitions for the ViperFS filesystem (user-space).
 *
 * @details
 * This is the user-space version of the ViperFS format definitions.
 * It mirrors the kernel version exactly for binary compatibility.
 */
#pragma once

#include "../../syscall.hpp"

namespace viperfs
{

/** @brief ViperFS magic number ("VPFS"). */
constexpr u32 VIPERFS_MAGIC = 0x53465056;

/** @brief ViperFS on-disk format version. */
constexpr u32 VIPERFS_VERSION = 1;

/** @brief On-disk block size in bytes. */
constexpr u64 BLOCK_SIZE = 4096;

/** @brief Size of one inode structure in bytes. */
constexpr u64 INODE_SIZE = 256;

/** @brief Number of inodes packed into one block. */
constexpr u64 INODES_PER_BLOCK = BLOCK_SIZE / INODE_SIZE;

/** @brief Inode number for the filesystem root directory. */
constexpr u64 ROOT_INODE = 2;

/**
 * @brief Superblock structure stored at block 0.
 */
struct Superblock
{
    u32 magic;              // VIPERFS_MAGIC
    u32 version;            // VIPERFS_VERSION
    u64 block_size;         // Block size (4096)
    u64 total_blocks;       // Total blocks on disk
    u64 free_blocks;        // Number of free blocks
    u64 inode_count;        // Total inodes
    u64 root_inode;         // Root directory inode
    u64 bitmap_start;       // Block bitmap start block
    u64 bitmap_blocks;      // Number of bitmap blocks
    u64 inode_table_start;  // Inode table start block
    u64 inode_table_blocks; // Number of inode table blocks
    u64 data_start;         // Data blocks start
    u8 uuid[16];            // Volume UUID
    char label[64];         // Volume label
    u8 _reserved[3928];     // Padding to 4096 bytes
};

static_assert(sizeof(Superblock) == 4096, "Superblock must be 4096 bytes");

/**
 * @brief Inode mode/type and permission bits.
 */
namespace mode
{
constexpr u32 TYPE_MASK = 0xF000;
constexpr u32 TYPE_FILE = 0x8000;
constexpr u32 TYPE_DIR = 0x4000;
constexpr u32 TYPE_LINK = 0xA000;

// Permissions (simplified)
constexpr u32 PERM_READ = 0x0004;
constexpr u32 PERM_WRITE = 0x0002;
constexpr u32 PERM_EXEC = 0x0001;
} // namespace mode

/**
 * @brief On-disk inode structure (256 bytes).
 */
struct Inode
{
    u64 inode_num;       // Inode number
    u32 mode;            // Type + permissions
    u32 flags;           // Flags
    u64 size;            // File size in bytes
    u64 blocks;          // Blocks allocated
    u64 atime;           // Access time
    u64 mtime;           // Modification time
    u64 ctime;           // Creation time
    u64 direct[12];      // Direct block pointers
    u64 indirect;        // Single indirect block
    u64 double_indirect; // Double indirect block
    u64 triple_indirect; // Triple indirect block
    u64 generation;      // Inode generation
    u8 _reserved[72];    // Padding to 256 bytes
};

static_assert(sizeof(Inode) == 256, "Inode must be 256 bytes");

/**
 * @brief Directory entry file types.
 */
namespace file_type
{
constexpr u8 UNKNOWN = 0;
constexpr u8 FILE = 1;
constexpr u8 DIR = 2;
constexpr u8 LINK = 7;
} // namespace file_type

/**
 * @brief On-disk directory entry (variable length).
 */
struct DirEntry
{
    u64 inode;    // Inode number (0 = deleted)
    u16 rec_len;  // Total entry length
    u8 name_len;  // Name length
    u8 file_type; // File type
    char name[];  // Name (not null-terminated)
};

/** @brief Minimum directory entry size. */
constexpr usize DIR_ENTRY_MIN_SIZE = sizeof(u64) + sizeof(u16) + sizeof(u8) + sizeof(u8);

/** @brief Maximum filename length. */
constexpr usize MAX_NAME_LEN = 255;

// Helper functions
inline bool is_directory(const Inode *inode)
{
    return (inode->mode & mode::TYPE_MASK) == mode::TYPE_DIR;
}

inline bool is_file(const Inode *inode)
{
    return (inode->mode & mode::TYPE_MASK) == mode::TYPE_FILE;
}

inline bool is_symlink(const Inode *inode)
{
    return (inode->mode & mode::TYPE_MASK) == mode::TYPE_LINK;
}

inline u8 mode_to_file_type(u32 m)
{
    switch (m & mode::TYPE_MASK)
    {
        case mode::TYPE_FILE:
            return file_type::FILE;
        case mode::TYPE_DIR:
            return file_type::DIR;
        case mode::TYPE_LINK:
            return file_type::LINK;
        default:
            return file_type::UNKNOWN;
    }
}

inline u16 dir_entry_size(u8 name_len)
{
    usize size = DIR_ENTRY_MIN_SIZE + name_len;
    return static_cast<u16>((size + 7) & ~7);
}

} // namespace viperfs
