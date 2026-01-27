//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
/**
 * @file fat32.cpp
 * @brief FAT32 filesystem driver implementation.
 */

#include "fat32.hpp"
#include "../../console/serial.hpp"
#include "../../drivers/virtio/blk.hpp"

namespace fs::fat32 {

// Global FAT32 instance for user disk
static FAT32 g_fat32;

FAT32 &fat32() {
    return g_fat32;
}

bool fat32_init() {
    auto *user_blk = virtio::user_blk_device();
    if (!user_blk) {
        serial::puts("[fat32] No user block device available\n");
        return false;
    }

    if (g_fat32.mount(user_blk)) {
        serial::puts("[fat32] Mounted FAT32 volume: ");
        serial::puts(g_fat32.label());
        serial::puts("\n");
        return true;
    }

    return false;
}

// ============================================================================
// Mount/Unmount
// ============================================================================

bool FAT32::mount(virtio::BlkDevice *device) {
    if (!device) {
        return false;
    }

    device_ = device;

    // Read boot sector
    if (!read_sector(0, sector_buf_)) {
        serial::puts("[fat32] Failed to read boot sector\n");
        return false;
    }

    const BPB *bpb = reinterpret_cast<const BPB *>(sector_buf_);

    // Verify boot signature
    if (sector_buf_[510] != 0x55 || sector_buf_[511] != 0xAA) {
        serial::puts("[fat32] Invalid boot signature\n");
        return false;
    }

    // Verify FAT32 markers
    if (bpb->bytes_per_sector != 512) {
        serial::puts("[fat32] Unsupported sector size (only 512 supported)\n");
        return false;
    }

    if (bpb->root_entry_count != 0) {
        serial::puts("[fat32] Not FAT32 (root_entry_count != 0)\n");
        return false;
    }

    if (bpb->fat_size_16 != 0) {
        serial::puts("[fat32] Not FAT32 (fat_size_16 != 0)\n");
        return false;
    }

    // Cache BPB values
    bytes_per_sector_ = bpb->bytes_per_sector;
    sectors_per_cluster_ = bpb->sectors_per_cluster;
    reserved_sectors_ = bpb->reserved_sectors;
    num_fats_ = bpb->num_fats;
    fat_size_ = bpb->fat_size_32;
    root_cluster_ = bpb->root_cluster;
    fs_info_sector_ = bpb->fs_info_sector;

    // Calculate derived values
    u32 total_sectors = bpb->total_sectors_32;
    u32 fat_sectors = num_fats_ * fat_size_;
    first_data_sector_ = reserved_sectors_ + fat_sectors;
    u32 data_sectors = total_sectors - first_data_sector_;
    total_clusters_ = data_sectors / sectors_per_cluster_;

    // Verify cluster count is in FAT32 range
    if (total_clusters_ < 65525) {
        serial::puts("[fat32] Not FAT32 (too few clusters)\n");
        return false;
    }

    // Copy volume label
    for (int i = 0; i < 11; i++) {
        volume_label_[i] = bpb->volume_label[i];
    }
    volume_label_[11] = '\0';

    // Trim trailing spaces from label
    for (int i = 10; i >= 0; i--) {
        if (volume_label_[i] == ' ') {
            volume_label_[i] = '\0';
        } else {
            break;
        }
    }

    // Read FSInfo
    if (fs_info_sector_ != 0 && fs_info_sector_ != 0xFFFF) {
        if (read_sector(fs_info_sector_, sector_buf_)) {
            const FSInfo *fsinfo = reinterpret_cast<const FSInfo *>(sector_buf_);
            if (fsinfo->lead_signature == FSINFO_LEAD_SIG &&
                fsinfo->struct_signature == FSINFO_STRUCT_SIG) {
                free_count_ = fsinfo->free_count;
                next_free_ = fsinfo->next_free;
            }
        }
    }

    mounted_ = true;

    serial::puts("[fat32] Mounted: ");
    serial::put_dec(total_clusters_);
    serial::puts(" clusters, ");
    serial::put_dec(sectors_per_cluster_);
    serial::puts(" sectors/cluster\n");

    return true;
}

void FAT32::unmount() {
    if (!mounted_) {
        return;
    }

    sync();
    mounted_ = false;
    device_ = nullptr;
}

u64 FAT32::total_size() const {
    return static_cast<u64>(total_clusters_) * sectors_per_cluster_ * bytes_per_sector_;
}

u64 FAT32::free_space() const {
    if (free_count_ == 0xFFFFFFFF) {
        return 0; // Unknown
    }
    return static_cast<u64>(free_count_) * sectors_per_cluster_ * bytes_per_sector_;
}

// ============================================================================
// Low-Level I/O
// ============================================================================

bool FAT32::read_sector(u64 sector, void *buf) {
    if (!device_) {
        return false;
    }
    return device_->read_sectors(sector, 1, buf) == 0;
}

bool FAT32::write_sector(u64 sector, const void *buf) {
    if (!device_) {
        return false;
    }
    return device_->write_sectors(sector, 1, buf) == 0;
}

u64 FAT32::cluster_to_sector(u32 cluster) const {
    return first_data_sector_ + static_cast<u64>(cluster - 2) * sectors_per_cluster_;
}

// ============================================================================
// FAT Operations
// ============================================================================

u32 FAT32::read_fat(u32 cluster) {
    if (cluster < 2 || cluster >= total_clusters_ + 2) {
        return cluster::EOC;
    }

    // Calculate FAT sector and offset
    u32 fat_offset = cluster * 4;
    u32 fat_sector = reserved_sectors_ + (fat_offset / bytes_per_sector_);
    u32 entry_offset = fat_offset % bytes_per_sector_;

    if (!read_sector(fat_sector, sector_buf_)) {
        return cluster::EOC;
    }

    u32 value = *reinterpret_cast<u32 *>(&sector_buf_[entry_offset]);
    return value & cluster::MASK;
}

bool FAT32::write_fat(u32 cluster, u32 value) {
    if (cluster < 2 || cluster >= total_clusters_ + 2) {
        return false;
    }

    u32 fat_offset = cluster * 4;
    u32 fat_sector = reserved_sectors_ + (fat_offset / bytes_per_sector_);
    u32 entry_offset = fat_offset % bytes_per_sector_;

    if (!read_sector(fat_sector, sector_buf_)) {
        return false;
    }

    // Preserve upper 4 bits
    u32 *entry = reinterpret_cast<u32 *>(&sector_buf_[entry_offset]);
    *entry = (*entry & 0xF0000000) | (value & cluster::MASK);

    // Write to primary FAT
    if (!write_sector(fat_sector, sector_buf_)) {
        return false;
    }

    // Write to backup FAT(s)
    for (u32 i = 1; i < num_fats_; i++) {
        u32 backup_sector = fat_sector + i * fat_size_;
        write_sector(backup_sector, sector_buf_);
    }

    return true;
}

u32 FAT32::follow_chain(u32 start, u32 cluster_offset) {
    u32 cluster = start;

    for (u32 i = 0; i < cluster_offset && cluster < cluster::EOC_MIN; i++) {
        cluster = read_fat(cluster);
    }

    return cluster;
}

u32 FAT32::alloc_cluster() {
    // Start search from hint or beginning
    u32 search_start = (next_free_ != 0xFFFFFFFF && next_free_ >= 2) ? next_free_ : 2;

    for (u32 i = 0; i < total_clusters_; i++) {
        u32 cluster = ((search_start - 2 + i) % total_clusters_) + 2;
        u32 value = read_fat(cluster);

        if (value == cluster::FREE) {
            // Mark as end of chain
            if (!write_fat(cluster, cluster::EOC)) {
                return 0;
            }

            // Update free count and hint
            if (free_count_ != 0xFFFFFFFF) {
                free_count_--;
            }
            next_free_ = cluster + 1;
            fsinfo_dirty_ = true;

            return cluster;
        }
    }

    return 0; // No free clusters
}

void FAT32::free_chain(u32 start_cluster) {
    u32 cluster = start_cluster;

    while (cluster >= 2 && cluster < cluster::EOC_MIN) {
        u32 next = read_fat(cluster);
        write_fat(cluster, cluster::FREE);

        if (free_count_ != 0xFFFFFFFF) {
            free_count_++;
        }
        fsinfo_dirty_ = true;

        cluster = next;
    }
}

// ============================================================================
// Directory Operations
// ============================================================================

void FAT32::parse_short_name(const DirEntry *entry, char *out) {
    int j = 0;

    // Copy name (8 chars, trim trailing spaces)
    for (int i = 0; i < 8 && entry->name[i] != ' '; i++) {
        out[j++] = entry->name[i];
    }

    // Add extension if present
    if (entry->name[8] != ' ') {
        out[j++] = '.';
        for (int i = 8; i < 11 && entry->name[i] != ' '; i++) {
            out[j++] = entry->name[i];
        }
    }

    out[j] = '\0';
}

bool FAT32::find_entry(u32 dir_cluster, const char *name, DirEntry *out,
                       u32 *out_cluster, u32 *out_offset) {
    u32 cluster = (dir_cluster == 0) ? root_cluster_ : dir_cluster;
    char short_name[13];

    while (cluster >= 2 && cluster < cluster::EOC_MIN) {
        u64 sector = cluster_to_sector(cluster);

        for (u32 s = 0; s < sectors_per_cluster_; s++) {
            if (!read_sector(sector + s, sector_buf_)) {
                return false;
            }

            for (u32 i = 0; i < bytes_per_sector_; i += 32) {
                const DirEntry *entry = reinterpret_cast<const DirEntry *>(&sector_buf_[i]);

                // End of directory
                if (entry->name[0] == 0x00) {
                    return false;
                }

                // Deleted entry
                if (static_cast<u8>(entry->name[0]) == 0xE5) {
                    continue;
                }

                // Skip LFN entries and volume labels
                if (entry->attr == attr::LFN || (entry->attr & attr::VOLUME_ID)) {
                    continue;
                }

                parse_short_name(entry, short_name);

                // Case-insensitive compare
                bool match = true;
                for (int j = 0; name[j] || short_name[j]; j++) {
                    char c1 = name[j];
                    char c2 = short_name[j];

                    // Convert to uppercase
                    if (c1 >= 'a' && c1 <= 'z')
                        c1 -= 32;
                    if (c2 >= 'a' && c2 <= 'z')
                        c2 -= 32;

                    if (c1 != c2) {
                        match = false;
                        break;
                    }
                }

                if (match) {
                    if (out) {
                        *out = *entry;
                    }
                    if (out_cluster) {
                        *out_cluster = cluster;
                    }
                    if (out_offset) {
                        *out_offset = s * bytes_per_sector_ + i;
                    }
                    return true;
                }
            }
        }

        cluster = read_fat(cluster);
    }

    return false;
}

bool FAT32::resolve_path(const char *path, DirEntry *out, u32 *parent_cluster) {
    if (!path || path[0] != '/') {
        return false;
    }

    // Handle root directory special case
    if (path[1] == '\0') {
        if (out) {
            // Synthesize root directory entry
            for (int i = 0; i < 11; i++) {
                out->name[i] = ' ';
            }
            out->attr = attr::DIRECTORY;
            out->cluster_high = (root_cluster_ >> 16) & 0xFFFF;
            out->cluster_low = root_cluster_ & 0xFFFF;
            out->file_size = 0;
        }
        if (parent_cluster) {
            *parent_cluster = 0;
        }
        return true;
    }

    u32 current_cluster = root_cluster_;
    const char *p = path + 1; // Skip leading '/'

    while (*p) {
        // Extract next path component
        char component[MAX_FILENAME + 1];
        int j = 0;

        while (*p && *p != '/' && j < static_cast<int>(MAX_FILENAME)) {
            component[j++] = *p++;
        }
        component[j] = '\0';

        // Skip trailing slashes
        while (*p == '/') {
            p++;
        }

        // Find this component
        DirEntry entry;
        if (!find_entry(current_cluster, component, &entry, nullptr, nullptr)) {
            return false;
        }

        // Check if we need to continue (more path components)
        if (*p) {
            // Must be a directory to continue
            if (!(entry.attr & attr::DIRECTORY)) {
                return false;
            }

            current_cluster = (static_cast<u32>(entry.cluster_high) << 16) | entry.cluster_low;
        } else {
            // This is the final component
            if (out) {
                *out = entry;
            }
            if (parent_cluster) {
                *parent_cluster = current_cluster;
            }
            return true;
        }
    }

    return false;
}

i32 FAT32::read_dir(u32 dir_cluster, FileInfo *entries, i32 max_entries) {
    u32 cluster = (dir_cluster == 0) ? root_cluster_ : dir_cluster;
    i32 count = 0;

    while (cluster >= 2 && cluster < cluster::EOC_MIN && count < max_entries) {
        u64 sector = cluster_to_sector(cluster);

        for (u32 s = 0; s < sectors_per_cluster_ && count < max_entries; s++) {
            if (!read_sector(sector + s, sector_buf_)) {
                return count;
            }

            for (u32 i = 0; i < bytes_per_sector_ && count < max_entries; i += 32) {
                const DirEntry *entry = reinterpret_cast<const DirEntry *>(&sector_buf_[i]);

                // End of directory
                if (entry->name[0] == 0x00) {
                    return count;
                }

                // Skip deleted entries
                if (static_cast<u8>(entry->name[0]) == 0xE5) {
                    continue;
                }

                // Skip LFN entries and volume labels
                if (entry->attr == attr::LFN || (entry->attr & attr::VOLUME_ID)) {
                    continue;
                }

                // Skip . and ..
                if (entry->name[0] == '.') {
                    continue;
                }

                FileInfo *fi = &entries[count];
                parse_short_name(entry, fi->name);
                fi->first_cluster = (static_cast<u32>(entry->cluster_high) << 16) | entry->cluster_low;
                fi->size = entry->file_size;
                fi->attr = entry->attr;
                fi->is_directory = (entry->attr & attr::DIRECTORY) != 0;

                count++;
            }
        }

        cluster = read_fat(cluster);
    }

    return count;
}

// ============================================================================
// File Operations
// ============================================================================

bool FAT32::open(const char *path, FileInfo *info) {
    if (!mounted_ || !path || !info) {
        return false;
    }

    DirEntry entry;
    if (!resolve_path(path, &entry, nullptr)) {
        return false;
    }

    // Copy name - use short name for now
    parse_short_name(&entry, info->name);

    info->first_cluster = (static_cast<u32>(entry.cluster_high) << 16) | entry.cluster_low;
    info->size = entry.file_size;
    info->attr = entry.attr;
    info->is_directory = (entry.attr & attr::DIRECTORY) != 0;

    return true;
}

i64 FAT32::read(const FileInfo *info, u64 offset, void *buf, usize len) {
    if (!mounted_ || !info || !buf) {
        return -1;
    }

    // Check bounds
    if (offset >= info->size) {
        return 0; // EOF
    }

    if (offset + len > info->size) {
        len = info->size - offset;
    }

    u8 *dst = static_cast<u8 *>(buf);
    usize bytes_read = 0;
    u32 cluster_size = sectors_per_cluster_ * bytes_per_sector_;

    // Find starting cluster
    u32 cluster_offset = offset / cluster_size;
    u32 byte_offset = offset % cluster_size;
    u32 cluster = follow_chain(info->first_cluster, cluster_offset);

    while (bytes_read < len && cluster >= 2 && cluster < cluster::EOC_MIN) {
        u64 sector = cluster_to_sector(cluster);
        u32 sector_offset = byte_offset / bytes_per_sector_;
        u32 in_sector_offset = byte_offset % bytes_per_sector_;

        for (u32 s = sector_offset; s < sectors_per_cluster_ && bytes_read < len; s++) {
            if (!read_sector(sector + s, sector_buf_)) {
                return bytes_read > 0 ? static_cast<i64>(bytes_read) : -1;
            }

            usize copy_start = (s == sector_offset) ? in_sector_offset : 0;
            usize copy_len = bytes_per_sector_ - copy_start;

            if (copy_len > len - bytes_read) {
                copy_len = len - bytes_read;
            }

            for (usize i = 0; i < copy_len; i++) {
                dst[bytes_read + i] = sector_buf_[copy_start + i];
            }

            bytes_read += copy_len;
        }

        byte_offset = 0; // Subsequent clusters start at offset 0
        cluster = read_fat(cluster);
    }

    return static_cast<i64>(bytes_read);
}

i64 FAT32::write(FileInfo *info, u64 offset, const void *buf, usize len) {
    if (!mounted_ || !info || !buf || info->is_directory) {
        return -1;
    }

    const u8 *src = static_cast<const u8 *>(buf);
    usize bytes_written = 0;
    u32 cluster_size = sectors_per_cluster_ * bytes_per_sector_;

    // Extend file if necessary
    u64 new_size = offset + len;
    u32 clusters_needed = (new_size + cluster_size - 1) / cluster_size;
    u32 current_clusters = (info->size + cluster_size - 1) / cluster_size;

    if (info->size == 0) {
        current_clusters = 0;
    }

    // Allocate additional clusters if needed
    u32 last_cluster = info->first_cluster;
    if (last_cluster >= 2) {
        while (read_fat(last_cluster) < cluster::EOC_MIN) {
            last_cluster = read_fat(last_cluster);
        }
    }

    while (current_clusters < clusters_needed) {
        u32 new_cluster = alloc_cluster();
        if (new_cluster == 0) {
            return bytes_written > 0 ? static_cast<i64>(bytes_written) : -1;
        }

        if (current_clusters == 0) {
            info->first_cluster = new_cluster;
        } else {
            write_fat(last_cluster, new_cluster);
        }

        last_cluster = new_cluster;
        current_clusters++;
    }

    // Write data
    u32 cluster_offset = offset / cluster_size;
    u32 byte_offset = offset % cluster_size;
    u32 cluster = follow_chain(info->first_cluster, cluster_offset);

    while (bytes_written < len && cluster >= 2 && cluster < cluster::EOC_MIN) {
        u64 sector = cluster_to_sector(cluster);
        u32 sector_offset = byte_offset / bytes_per_sector_;
        u32 in_sector_offset = byte_offset % bytes_per_sector_;

        for (u32 s = sector_offset; s < sectors_per_cluster_ && bytes_written < len; s++) {
            // Read-modify-write for partial sectors
            if (in_sector_offset != 0 || len - bytes_written < bytes_per_sector_) {
                if (!read_sector(sector + s, sector_buf_)) {
                    return bytes_written > 0 ? static_cast<i64>(bytes_written) : -1;
                }
            }

            usize copy_start = (s == sector_offset) ? in_sector_offset : 0;
            usize copy_len = bytes_per_sector_ - copy_start;

            if (copy_len > len - bytes_written) {
                copy_len = len - bytes_written;
            }

            for (usize i = 0; i < copy_len; i++) {
                sector_buf_[copy_start + i] = src[bytes_written + i];
            }

            if (!write_sector(sector + s, sector_buf_)) {
                return bytes_written > 0 ? static_cast<i64>(bytes_written) : -1;
            }

            bytes_written += copy_len;
            in_sector_offset = 0;
        }

        byte_offset = 0;
        cluster = read_fat(cluster);
    }

    // Update file size if extended
    if (new_size > info->size) {
        info->size = new_size;
        // Note: Directory entry should be updated by caller
    }

    return static_cast<i64>(bytes_written);
}

bool FAT32::create_file(const char *path, FileInfo *info) {
    // TODO: Implement file creation
    (void)path;
    (void)info;
    serial::puts("[fat32] create_file not yet implemented\n");
    return false;
}

bool FAT32::create_dir(const char *path) {
    // TODO: Implement directory creation
    (void)path;
    serial::puts("[fat32] create_dir not yet implemented\n");
    return false;
}

bool FAT32::remove(const char *path) {
    // TODO: Implement file/directory removal
    (void)path;
    serial::puts("[fat32] remove not yet implemented\n");
    return false;
}

// ============================================================================
// Sync
// ============================================================================

void FAT32::update_fsinfo() {
    if (!fsinfo_dirty_ || fs_info_sector_ == 0 || fs_info_sector_ == 0xFFFF) {
        return;
    }

    if (!read_sector(fs_info_sector_, sector_buf_)) {
        return;
    }

    FSInfo *fsinfo = reinterpret_cast<FSInfo *>(sector_buf_);
    if (fsinfo->lead_signature == FSINFO_LEAD_SIG &&
        fsinfo->struct_signature == FSINFO_STRUCT_SIG) {
        fsinfo->free_count = free_count_;
        fsinfo->next_free = next_free_;
        write_sector(fs_info_sector_, sector_buf_);
    }

    fsinfo_dirty_ = false;
}

void FAT32::sync() {
    if (!mounted_) {
        return;
    }

    update_fsinfo();
}

} // namespace fs::fat32
