/**
 * @file viperfs_format.h
 * @brief Shared on-disk format definitions for ViperFS host-side tools.
 *
 * @details
 * This header centralizes the on-disk structure definitions used by both
 * mkfs.viperfs and fsck.viperfs. It defines:
 * - Type aliases (u8, u16, u32, u64, i64)
 * - Filesystem constants (magic, version, block/inode sizes)
 * - Packed on-disk structures (Superblock, Inode, DirEntry)
 * - Mode bit and file type namespaces
 *
 * NOTE: The kernel has its own format header at kernel/fs/viperfs/format.hpp
 * with additional fields (uid, gid, checksum, journal structures). The
 * layouts are binary-compatible when tool-side reserved fields are zero.
 * Keep both headers in sync for the overlapping field offsets.
 */

#pragma once

#include <cstdint>
#include <cstring>

// Type aliases matching the kernel's convention
using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;
using i64 = int64_t;

/** @name Filesystem constants
 *  @brief On-disk constants shared by the mkfs/fsck tools and kernel driver.
 *  @{
 */
constexpr u32 VIPERFS_MAGIC = 0x53465056;                 ///< "VPFS" magic number
constexpr u32 VIPERFS_VERSION = 1;                        ///< On-disk format version
constexpr u64 BLOCK_SIZE = 4096;                          ///< Block size in bytes
constexpr u64 INODE_SIZE = 256;                           ///< Inode size in bytes
constexpr u64 INODES_PER_BLOCK = BLOCK_SIZE / INODE_SIZE; ///< Inodes per block
constexpr u64 ROOT_INODE = 2;                             ///< Root directory inode number
constexpr u64 PTRS_PER_BLOCK = BLOCK_SIZE / sizeof(u64);  ///< Block pointers per indirect block

/** @} */

/**
 * @brief ViperFS superblock (block 0).
 *
 * @details
 * The superblock describes the overall filesystem layout and key parameters.
 * It is written as one full 4 KiB block so it can be read with a single disk
 * I/O operation. Fields are in little-endian host order.
 *
 * The kernel's Superblock has a u32 checksum at offset 168 and _reserved[3924].
 * The tool version omits the checksum field, using _reserved[3928] instead.
 * Since the tools zero-fill the reserved area, the kernel reads checksum = 0
 * (unset), which is the expected default for unchecksummed images.
 */
struct __attribute__((packed)) Superblock {
    u32 magic;
    u32 version;
    u64 block_size;
    u64 total_blocks;
    u64 free_blocks;
    u64 inode_count;
    u64 root_inode;
    u64 bitmap_start;
    u64 bitmap_blocks;
    u64 inode_table_start;
    u64 inode_table_blocks;
    u64 data_start;
    u8 uuid[16];
    char label[64];
    u8 _reserved[3928];
};

static_assert(sizeof(Superblock) == 4096, "Superblock must be 4096 bytes");

/**
 * @brief Mode bit definitions stored in Inode::mode.
 *
 * @details
 * This is a small, filesystem-local permission/type model. The kernel may map
 * these into higher-level VFS permissions.
 */
namespace mode {
constexpr u32 TYPE_MASK = 0xF000;
constexpr u32 TYPE_FILE = 0x8000;
constexpr u32 TYPE_DIR = 0x4000;
constexpr u32 PERM_READ = 0x0004;
constexpr u32 PERM_WRITE = 0x0002;
constexpr u32 PERM_EXEC = 0x0001;
} // namespace mode

/**
 * @brief On-disk inode record.
 *
 * @details
 * Inodes are fixed-size (256 bytes) and stored in a contiguous inode table.
 * Each inode contains basic metadata and pointers to file data blocks:
 * - 12 direct block pointers.
 * - One single-indirect block pointer.
 * - Double/triple indirect pointers are reserved for future use.
 *
 * The kernel's Inode has uid/gid (u16+u16) at offset 12 where this version
 * has u32 flags. The layouts are binary-compatible since tools set flags = 0
 * (which the kernel reads as uid = 0, gid = 0).
 */
struct __attribute__((packed)) Inode {
    u64 inode_num;
    u32 mode;
    u32 flags;
    u64 size;
    u64 blocks;
    u64 atime;
    u64 mtime;
    u64 ctime;
    u64 direct[12];
    u64 indirect;
    u64 double_indirect;
    u64 triple_indirect;
    u64 generation;
    u8 _reserved[72];
};

static_assert(sizeof(Inode) == 256, "Inode must be 256 bytes");

/** @brief Directory entry type values stored in DirEntry::file_type. */
namespace file_type {
constexpr u8 UNKNOWN = 0;
constexpr u8 FILE = 1;
constexpr u8 DIR = 2;
constexpr u8 LINK = 7;
} // namespace file_type

/**
 * @brief Directory entry header used in directory data blocks.
 *
 * @details
 * Directory blocks contain a sequence of variable-length records. Each record
 * begins with this header and is followed by `name_len` bytes of name data.
 * `rec_len` specifies the total size of the record, allowing the reader to
 * skip to the next entry. Records are aligned to 8 bytes.
 */
struct __attribute__((packed)) DirEntry {
    u64 inode;
    u16 rec_len;
    u8 name_len;
    u8 file_type;
    // name follows
};

static_assert(sizeof(DirEntry) == 12, "DirEntry header must be 12 bytes");
