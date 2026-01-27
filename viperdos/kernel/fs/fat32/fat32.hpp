//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
/**
 * @file fat32.hpp
 * @brief FAT32 filesystem driver for secondary disk access.
 *
 * @details
 * Implements FAT32 filesystem support for reading and writing secondary disks
 * (USB drives, SD cards, etc.). The system disk must be ViperFS, but FAT32
 * provides compatibility with external media.
 *
 * ## FAT32 On-Disk Layout
 *
 * @verbatim
 * +------------------+  Sector 0
 * | Boot Sector/BPB  |  BIOS Parameter Block
 * +------------------+  Sector 1
 * | FSInfo           |  Free cluster tracking
 * +------------------+  Sector 6 (backup boot)
 * | Backup Boot      |  Copy of boot sector
 * +------------------+  Reserved sectors end
 * |                  |
 * | FAT #1           |  Primary File Allocation Table
 * |                  |
 * +------------------+
 * |                  |
 * | FAT #2           |  Backup FAT (optional)
 * |                  |
 * +------------------+  Data area begins
 * |                  |
 * | Data Clusters    |  File and directory data
 * |                  |
 * +------------------+
 * @endverbatim
 */
#pragma once

#include "../../include/types.hpp"

// Forward declaration
namespace virtio {
class BlkDevice;
}

namespace fs::fat32 {

// ============================================================================
// FAT32 On-Disk Structures
// ============================================================================

/**
 * @brief BIOS Parameter Block (BPB) - Boot sector structure.
 *
 * @details
 * The BPB is located at sector 0 and contains filesystem geometry and
 * configuration. FAT32 extends the basic FAT16 BPB with additional fields.
 */
struct __attribute__((packed)) BPB {
    u8 jump[3];             // Jump instruction (EB xx 90 or E9 xx xx)
    char oem_name[8];       // OEM identifier
    u16 bytes_per_sector;   // Usually 512
    u8 sectors_per_cluster; // Power of 2 (1, 2, 4, 8, 16, 32, 64, 128)
    u16 reserved_sectors;   // Sectors before FAT (usually 32 for FAT32)
    u8 num_fats;            // Usually 2
    u16 root_entry_count;   // 0 for FAT32
    u16 total_sectors_16;   // 0 for FAT32 (use total_sectors_32)
    u8 media_type;          // 0xF8 for fixed disk
    u16 fat_size_16;        // 0 for FAT32 (use fat_size_32)
    u16 sectors_per_track;  // Geometry for INT 13h
    u16 num_heads;          // Geometry for INT 13h
    u32 hidden_sectors;     // Sectors before partition
    u32 total_sectors_32;   // Total sectors in volume

    // FAT32-specific fields (offset 36)
    u32 fat_size_32;        // Sectors per FAT
    u16 ext_flags;          // FAT mirroring flags
    u16 fs_version;         // Version (0.0)
    u32 root_cluster;       // First cluster of root directory
    u16 fs_info_sector;     // FSInfo sector (usually 1)
    u16 backup_boot_sector; // Backup boot sector (usually 6)
    u8 reserved[12];        // Reserved
    u8 drive_number;        // BIOS drive number
    u8 reserved1;           // Reserved
    u8 boot_signature;      // 0x29 if extended boot signature present
    u32 volume_id;          // Volume serial number
    char volume_label[11];  // Volume label
    char fs_type[8];        // "FAT32   "
};

static_assert(sizeof(BPB) == 90, "BPB must be 90 bytes");

/**
 * @brief FSInfo structure for free cluster tracking.
 *
 * @details
 * FAT32 includes an FSInfo sector to speed up free cluster searches.
 * Located at fs_info_sector (usually sector 1).
 */
struct __attribute__((packed)) FSInfo {
    u32 lead_signature;   // 0x41615252 ("RRaA")
    u8 reserved1[480];    // Reserved
    u32 struct_signature; // 0x61417272 ("rrAa")
    u32 free_count;       // Free cluster count (0xFFFFFFFF if unknown)
    u32 next_free;        // Next free cluster hint (0xFFFFFFFF if unknown)
    u8 reserved2[12];     // Reserved
    u32 trail_signature;  // 0xAA550000
};

static_assert(sizeof(FSInfo) == 512, "FSInfo must be 512 bytes");

/** @brief FSInfo lead signature. */
constexpr u32 FSINFO_LEAD_SIG = 0x41615252;

/** @brief FSInfo struct signature. */
constexpr u32 FSINFO_STRUCT_SIG = 0x61417272;

/** @brief FSInfo trail signature. */
constexpr u32 FSINFO_TRAIL_SIG = 0xAA550000;

/**
 * @brief FAT32 directory entry (32 bytes).
 *
 * @details
 * Standard 8.3 filename directory entry. Long filenames use additional
 * LFN entries preceding this entry.
 */
struct __attribute__((packed)) DirEntry {
    char name[11];        // 8.3 filename (space-padded)
    u8 attr;              // Attribute flags
    u8 nt_reserved;       // Reserved for Windows NT
    u8 create_time_tenth; // Creation time (tenths of second)
    u16 create_time;      // Creation time
    u16 create_date;      // Creation date
    u16 access_date;      // Last access date
    u16 cluster_high;     // High 16 bits of first cluster
    u16 modify_time;      // Last modification time
    u16 modify_date;      // Last modification date
    u16 cluster_low;      // Low 16 bits of first cluster
    u32 file_size;        // File size in bytes
};

static_assert(sizeof(DirEntry) == 32, "DirEntry must be 32 bytes");

/**
 * @brief Long Filename (LFN) directory entry.
 *
 * @details
 * LFN entries store portions of long filenames in UCS-2 encoding.
 * Multiple LFN entries precede the standard 8.3 entry in reverse order.
 */
struct __attribute__((packed)) LFNEntry {
    u8 order;       // Sequence number (1-20, 0x40 marks last)
    u16 name1[5];   // Characters 1-5 (UCS-2)
    u8 attr;        // Always 0x0F for LFN
    u8 type;        // Always 0
    u8 checksum;    // Checksum of 8.3 name
    u16 name2[6];   // Characters 6-11 (UCS-2)
    u16 cluster;    // Always 0
    u16 name3[2];   // Characters 12-13 (UCS-2)
};

static_assert(sizeof(LFNEntry) == 32, "LFNEntry must be 32 bytes");

// Directory entry attribute flags
namespace attr {
constexpr u8 READ_ONLY = 0x01;
constexpr u8 HIDDEN = 0x02;
constexpr u8 SYSTEM = 0x04;
constexpr u8 VOLUME_ID = 0x08;
constexpr u8 DIRECTORY = 0x10;
constexpr u8 ARCHIVE = 0x20;
constexpr u8 LFN = 0x0F; // READ_ONLY | HIDDEN | SYSTEM | VOLUME_ID
} // namespace attr

// Special cluster values
namespace cluster {
constexpr u32 FREE = 0x00000000;
constexpr u32 RESERVED_MIN = 0x0FFFFFF0;
constexpr u32 BAD = 0x0FFFFFF7;
constexpr u32 EOC_MIN = 0x0FFFFFF8; // End of chain (0xFFFFFFF8-0xFFFFFFFF)
constexpr u32 EOC = 0x0FFFFFFF;     // Common end-of-chain marker
constexpr u32 MASK = 0x0FFFFFFF;    // Valid cluster bits
} // namespace cluster

// ============================================================================
// FAT32 Driver
// ============================================================================

/** @brief Maximum supported filename length (LFN). */
constexpr usize MAX_FILENAME = 255;

/** @brief Maximum path length. */
constexpr usize MAX_PATH = 260;

/**
 * @brief File information structure.
 */
struct FileInfo {
    char name[MAX_FILENAME + 1]; // Filename (null-terminated)
    u32 first_cluster;           // First cluster
    u32 size;                    // File size
    u8 attr;                     // Attributes
    bool is_directory;           // True if directory
    u64 atime;                   // Last access time (ms since epoch)
    u64 mtime;                   // Last modification time (ms since epoch)
    u64 ctime;                   // Creation time (ms since epoch)
};

/// Convert FAT32 DOS date/time to milliseconds since Unix epoch.
u64 dos_datetime_to_ms(u16 date, u16 time, u8 tenths = 0);

/**
 * @brief FAT32 filesystem driver.
 *
 * @details
 * Provides read and write access to FAT32-formatted volumes.
 * Supports long filenames and subdirectories.
 */
class FAT32 {
  public:
    /**
     * @brief Mount a FAT32 volume.
     *
     * @param device Block device containing the volume.
     * @return true on success, false if not a valid FAT32 volume.
     */
    bool mount(virtio::BlkDevice *device);

    /**
     * @brief Unmount the volume.
     */
    void unmount();

    /**
     * @brief Check if volume is mounted.
     */
    bool is_mounted() const {
        return mounted_;
    }

    /**
     * @brief Get volume label.
     */
    const char *label() const {
        return volume_label_;
    }

    /**
     * @brief Get total volume size in bytes.
     */
    u64 total_size() const;

    /**
     * @brief Get free space in bytes.
     */
    u64 free_space() const;

    // File operations

    /**
     * @brief Open a file by path.
     *
     * @param path File path (e.g., "/DIR/FILE.TXT").
     * @param info Output file information.
     * @return true if file found, false otherwise.
     */
    bool open(const char *path, FileInfo *info);

    /**
     * @brief Read data from a file.
     *
     * @param info File information from open().
     * @param offset Byte offset within file.
     * @param buf Output buffer.
     * @param len Number of bytes to read.
     * @return Number of bytes read, or -1 on error.
     */
    i64 read(const FileInfo *info, u64 offset, void *buf, usize len);

    /**
     * @brief Write data to a file.
     *
     * @param info File information from open().
     * @param offset Byte offset within file.
     * @param buf Input buffer.
     * @param len Number of bytes to write.
     * @return Number of bytes written, or -1 on error.
     */
    i64 write(FileInfo *info, u64 offset, const void *buf, usize len);

    /**
     * @brief Create a new file.
     *
     * @param path File path.
     * @param info Output file information.
     * @return true on success.
     */
    bool create_file(const char *path, FileInfo *info);

    /**
     * @brief Create a new directory.
     *
     * @param path Directory path.
     * @return true on success.
     */
    bool create_dir(const char *path);

    /**
     * @brief Delete a file or empty directory.
     *
     * @param path Path to delete.
     * @return true on success.
     */
    bool remove(const char *path);

    // Directory operations

    /**
     * @brief Read directory entries.
     *
     * @param dir_cluster First cluster of directory (0 for root).
     * @param entries Output array.
     * @param max_entries Maximum entries to return.
     * @return Number of entries read.
     */
    i32 read_dir(u32 dir_cluster, FileInfo *entries, i32 max_entries);

    /**
     * @brief Sync all changes to disk.
     */
    void sync();

  private:
    virtio::BlkDevice *device_{nullptr};
    bool mounted_{false};

    // BPB values cached for fast access
    u32 bytes_per_sector_{0};
    u32 sectors_per_cluster_{0};
    u32 reserved_sectors_{0};
    u32 num_fats_{0};
    u32 fat_size_{0};
    u32 root_cluster_{0};
    u32 total_clusters_{0};
    u32 first_data_sector_{0};
    u32 fs_info_sector_{0};

    // FSInfo cache
    u32 free_count_{0xFFFFFFFF};
    u32 next_free_{0xFFFFFFFF};
    bool fsinfo_dirty_{false};

    // Volume label (null-terminated)
    char volume_label_[12]{};

    // Sector buffer
    u8 sector_buf_[512];

    // Helper methods

    /**
     * @brief Read a sector from the device.
     */
    bool read_sector(u64 sector, void *buf);

    /**
     * @brief Write a sector to the device.
     */
    bool write_sector(u64 sector, const void *buf);

    /**
     * @brief Read a FAT entry.
     */
    u32 read_fat(u32 cluster);

    /**
     * @brief Write a FAT entry.
     */
    bool write_fat(u32 cluster, u32 value);

    /**
     * @brief Get the first sector of a cluster.
     */
    u64 cluster_to_sector(u32 cluster) const;

    /**
     * @brief Allocate a new cluster.
     */
    u32 alloc_cluster();

    /**
     * @brief Free a cluster chain.
     */
    void free_chain(u32 start_cluster);

    /**
     * @brief Follow cluster chain to find cluster at offset.
     */
    u32 follow_chain(u32 start, u32 offset);

    /**
     * @brief Parse an 8.3 filename into a readable string.
     */
    void parse_short_name(const DirEntry *entry, char *out);

    /**
     * @brief Find a directory entry by name.
     */
    bool find_entry(u32 dir_cluster, const char *name, DirEntry *out, u32 *out_cluster, u32 *out_offset);

    /**
     * @brief Resolve a path to its directory entry.
     */
    bool resolve_path(const char *path, DirEntry *out, u32 *parent_cluster);

    /**
     * @brief Update FSInfo structure on disk.
     */
    void update_fsinfo();
};

/**
 * @brief Get the global FAT32 driver instance for user disk.
 */
FAT32 &fat32();

/**
 * @brief Initialize and mount FAT32 on user disk if present.
 *
 * @return true if FAT32 volume detected and mounted.
 */
bool fat32_init();

} // namespace fs::fat32
