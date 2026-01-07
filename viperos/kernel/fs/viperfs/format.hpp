//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: kernel/fs/viperfs/format.hpp
// Purpose: On-disk format definitions for the ViperFS filesystem.
// Key invariants: Block size 4KB; inode size 256 bytes; structures packed.
// Ownership/Lifetime: Header-only; defines on-disk ABI.
// Links: kernel/fs/viperfs/viperfs.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "../../include/types.hpp"

namespace fs::viperfs
{

/**
 * @file format.hpp
 * @brief On-disk format definitions for the ViperFS filesystem.
 *
 * @details
 * ViperFS is a small filesystem used by ViperOS for bring-up. This header
 * defines the on-disk data structures and constants:
 * - Superblock layout and metadata.
 * - Inode layout, including direct and indirect block pointers.
 * - Directory entry format.
 *
 * All structures are designed to fit cleanly into the filesystem block size
 * (4KiB) and are laid out explicitly using fixed-width types so the format is
 * stable across builds.
 */

/** @brief ViperFS magic number ("VPFS"). */
constexpr u32 VIPERFS_MAGIC = 0x53465056;

/** @brief ViperFS on-disk format version. */
constexpr u32 VIPERFS_VERSION = 1;

/** @brief On-disk block size in bytes (matches the cache block size). */
constexpr u64 BLOCK_SIZE = 4096;

/** @brief Size of one inode structure in bytes. */
constexpr u64 INODE_SIZE = 256;

/** @brief Number of inodes packed into one block. */
constexpr u64 INODES_PER_BLOCK = BLOCK_SIZE / INODE_SIZE;

/** @brief Inode number for the filesystem root directory. */
constexpr u64 ROOT_INODE = 2;

/**
 * @brief Superblock structure stored at block 0.
 *
 * @details
 * The superblock contains filesystem-wide metadata and the locations of other
 * on-disk structures such as the block bitmap and inode table. The structure is
 * padded to exactly one block.
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
    u8 _reserved[3928];     // Padding to 4096 bytes (4096 - 168 = 3928)
};

static_assert(sizeof(Superblock) == 4096, "Superblock must be 4096 bytes");

// ============================================================================
// Journal Structures
// ============================================================================

/** @brief Journal magic number for header validation. */
constexpr u32 JOURNAL_MAGIC = 0x4A524E4C; // "JRNL"

/** @brief Maximum number of blocks in a single transaction. */
constexpr u32 MAX_JOURNAL_BLOCKS = 32;

/** @brief Size of journal in blocks (16 blocks = 64KB by default). */
constexpr u64 JOURNAL_BLOCKS = 16;

/**
 * @brief Transaction state in journal.
 */
namespace txn_state
{
constexpr u8 TXN_INVALID = 0;   // Invalid/unused entry
constexpr u8 TXN_ACTIVE = 1;    // Transaction in progress
constexpr u8 TXN_COMMITTED = 2; // Transaction committed (needs replay)
constexpr u8 TXN_COMPLETE = 3;  // Transaction completed (can be discarded)
} // namespace txn_state

/**
 * @brief Journal header stored at the first journal block.
 *
 * @details
 * Contains metadata about the journal state and the location of the
 * first valid transaction. Updated atomically when transactions complete.
 */
struct JournalHeader
{
    u32 magic;          // JOURNAL_MAGIC
    u32 version;        // Journal format version (1)
    u64 sequence;       // Current transaction sequence number
    u64 start_block;    // First block of journal data area
    u64 num_blocks;     // Total blocks in journal data area
    u64 head;           // Head of journal (oldest valid transaction)
    u64 tail;           // Tail of journal (next write position)
    u8 _reserved[4048]; // Padding to 4096 bytes (4096 - 48 = 4048)
};

static_assert(sizeof(JournalHeader) == 4096, "JournalHeader must be 4096 bytes");

/**
 * @brief Individual block record in a transaction.
 *
 * @details
 * Records the original block number that was modified. The actual block
 * data follows this record.
 */
struct JournalBlockRecord
{
    u64 block_num; // Original block number on disk
    u64 checksum;  // Simple checksum of block data
};

/**
 * @brief Transaction descriptor stored at the start of each transaction.
 *
 * @details
 * A transaction consists of:
 * 1. This transaction descriptor
 * 2. Array of JournalBlockRecord entries
 * 3. The actual block data for each record
 * 4. A commit record (magic number) at the end
 */
struct JournalTransaction
{
    u32 magic;     // JOURNAL_MAGIC
    u8 state;      // txn_state value
    u8 num_blocks; // Number of blocks in this transaction
    u16 _padding;
    u64 sequence;                                  // Transaction sequence number
    u64 timestamp;                                 // Transaction timestamp
    JournalBlockRecord blocks[MAX_JOURNAL_BLOCKS]; // Block records (32 * 16 = 512 bytes)
    u8 _reserved[3560]; // Padding to 4096 bytes (4096 - 4 - 1 - 1 - 2 - 8 - 8 - 512 = 3560)
};

static_assert(sizeof(JournalTransaction) == 4096, "JournalTransaction must be 4096 bytes");

/**
 * @brief Commit record marking the end of a valid transaction.
 */
struct JournalCommit
{
    u32 magic;          // JOURNAL_MAGIC
    u64 sequence;       // Must match transaction sequence
    u32 checksum;       // Checksum of entire transaction
    u8 _reserved[4076]; // Padding to 4096 bytes
};

static_assert(sizeof(JournalCommit) == 4096, "JournalCommit must be 4096 bytes");

// ============================================================================
// Inode mode bits
/**
 * @brief Inode mode/type and permission bits.
 *
 * @details
 * The high bits encode the inode type (file/dir/symlink). Permission bits are
 * currently simplified.
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

// Inode structure
/**
 * @brief On-disk inode structure (256 bytes).
 *
 * @details
 * Stores metadata and block pointers. Direct pointers cover small files without
 * indirection. Larger files use single and double indirect blocks. Triple
 * indirection is reserved but may not be implemented in the current driver.
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
    u64 generation;      // Inode generation (for NFS/stale handle detection)
    u8 _reserved[72];    // Padding to 256 bytes (256 - 184 = 72)
};

static_assert(sizeof(Inode) == 256, "Inode must be 256 bytes");

// Directory entry file types
/** @brief Directory entry type codes stored in @ref DirEntry::file_type. */
namespace file_type
{
constexpr u8 UNKNOWN = 0;
constexpr u8 FILE = 1;
constexpr u8 DIR = 2;
constexpr u8 LINK = 7;
} // namespace file_type

// Directory entry (variable size)
/**
 * @brief On-disk directory entry (variable length).
 *
 * @details
 * Directory entries are packed sequentially in directory file data. `rec_len`
 * allows skipping to the next entry. Deleted entries are represented by
 * `inode == 0`.
 */
struct DirEntry
{
    u64 inode;    // Inode number (0 = deleted)
    u16 rec_len;  // Total entry length (for skipping)
    u8 name_len;  // Name length
    u8 file_type; // File type
    char name[];  // Name (not null-terminated, use name_len)
};

// Minimum directory entry size
/** @brief Minimum size of a directory entry header without name bytes. */
constexpr usize DIR_ENTRY_MIN_SIZE = sizeof(u64) + sizeof(u16) + sizeof(u8) + sizeof(u8);

// Maximum filename length
/** @brief Maximum filename length supported by the on-disk format. */
constexpr usize MAX_NAME_LEN = 255;

// Helper functions
/**
 * @brief Check whether an inode is a directory.
 *
 * @param inode Inode pointer.
 * @return `true` if inode type is directory.
 */
inline bool is_directory(const Inode *inode)
{
    return (inode->mode & mode::TYPE_MASK) == mode::TYPE_DIR;
}

/** @brief Check whether an inode is a regular file. */
inline bool is_file(const Inode *inode)
{
    return (inode->mode & mode::TYPE_MASK) == mode::TYPE_FILE;
}

/** @brief Check whether an inode is a symbolic link. */
inline bool is_symlink(const Inode *inode)
{
    return (inode->mode & mode::TYPE_MASK) == mode::TYPE_LINK;
}

// Directory entry helpers
/**
 * @brief Convert an inode mode type to a directory entry file_type code.
 *
 * @param mode Inode mode field.
 * @return Directory entry type code.
 */
inline u8 mode_to_file_type(u32 mode)
{
    switch (mode & mode::TYPE_MASK)
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

// Calculate record length for a name
/**
 * @brief Compute the on-disk record length for a directory entry name length.
 *
 * @details
 * The record length is rounded up to 8-byte alignment.
 *
 * @param name_len Length of the name in bytes.
 * @return Total record length in bytes.
 */
inline u16 dir_entry_size(u8 name_len)
{
    // Round up to 8-byte alignment
    usize size = DIR_ENTRY_MIN_SIZE + name_len;
    return static_cast<u16>((size + 7) & ~7);
}

} // namespace fs::viperfs
