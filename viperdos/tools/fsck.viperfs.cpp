/**
 * @file fsck.viperfs.cpp
 * @brief Filesystem check tool for ViperFS disk images.
 *
 * @details
 * This tool verifies the integrity of a ViperFS filesystem image and reports
 * any inconsistencies found. It performs the following checks:
 * - Superblock validation (magic, version, layout)
 * - Block bitmap consistency
 * - Inode table verification
 * - Directory structure traversal
 * - Orphaned inode/block detection
 * - Cross-reference verification (blocks claimed by multiple inodes)
 *
 * Command line usage:
 * - `fsck.viperfs <image>` - Check the filesystem
 * - `fsck.viperfs -v <image>` - Verbose output
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "viperfs_format.h"

// Global state
FILE *disk_fp = nullptr;
Superblock sb;
std::vector<u8> disk_bitmap;
std::vector<u8> computed_bitmap;
std::vector<Inode> inodes;
std::set<u64> visited_inodes;
std::map<u64, std::vector<u64>> block_owners; // block -> list of inodes claiming it
int error_count = 0;
int warning_count = 0;
bool verbose = false;

/// @brief Log an error message to stderr and increment the error counter.
void report_error(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "ERROR: ");
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    va_end(args);
    error_count++;
}

/// @brief Log a warning message to stderr and increment the warning counter.
void report_warning(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "WARNING: ");
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    va_end(args);
    warning_count++;
}

/// @brief Print a diagnostic message to stdout when verbose mode is enabled.
void verbose_log(const char *fmt, ...) {
    if (!verbose)
        return;
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    printf("\n");
    va_end(args);
}

/// @brief Read a single block from the disk image into the given buffer.
/// @return true on success, false on I/O error (buffer is zeroed on failure).
bool read_block(u64 block, void *data) {
    if (fseek(disk_fp, block * BLOCK_SIZE, SEEK_SET) != 0) {
        return false;
    }
    if (fread(data, BLOCK_SIZE, 1, disk_fp) != 1) {
        memset(data, 0, BLOCK_SIZE);
        return false;
    }
    return true;
}

// Bitmap bit operations
inline bool bit_get(const u8 *bitmap, u64 bit) {
    return (bitmap[bit / 8] & (1 << (bit % 8))) != 0;
}

inline void bit_set(u8 *bitmap, u64 bit) {
    bitmap[bit / 8] |= static_cast<u8>(1 << (bit % 8));
}

/// @brief Check whether a block is marked as used in the on-disk bitmap.
bool is_block_used_disk(u64 block) {
    return bit_get(disk_bitmap, block);
}

/// @brief Mark a block as used in the computed (expected) bitmap.
void mark_block_computed(u64 block) {
    if (block < sb.total_blocks) {
        bit_set(computed_bitmap, block);
    }
}

/// @brief Check whether a block is marked as used in the computed bitmap.
bool is_block_computed(u64 block) {
    return bit_get(computed_bitmap, block);
}

/// @brief Record that a block is owned by the given inode and mark it in the computed bitmap.
void claim_block(u64 block, u64 inode_num) {
    if (block == 0 || block >= sb.total_blocks)
        return;
    block_owners[block].push_back(inode_num);
    mark_block_computed(block);
}

/// @brief Read a single block pointer from an indirect block at the given index.
u64 read_indirect_ptr(u64 block, u64 index) {
    if (block == 0)
        return 0;
    u8 data[BLOCK_SIZE];
    if (!read_block(block, data))
        return 0;
    u64 *ptrs = reinterpret_cast<u64 *>(data);
    return ptrs[index];
}

// Forward declarations
void check_directory(u64 ino, const std::string &path);

/// @brief Resolve a logical block index to a physical block number via direct/indirect pointers.
u64 get_block_ptr(Inode *inode, u64 block_idx) {
    // Direct blocks (0-11)
    if (block_idx < 12) {
        return inode->direct[block_idx];
    }
    block_idx -= 12;

    // Single indirect
    if (block_idx < PTRS_PER_BLOCK) {
        return read_indirect_ptr(inode->indirect, block_idx);
    }
    block_idx -= PTRS_PER_BLOCK;

    // Double indirect
    if (block_idx < PTRS_PER_BLOCK * PTRS_PER_BLOCK) {
        u64 l1_idx = block_idx / PTRS_PER_BLOCK;
        u64 l2_idx = block_idx % PTRS_PER_BLOCK;
        u64 l1_block = read_indirect_ptr(inode->double_indirect, l1_idx);
        if (l1_block == 0)
            return 0;
        return read_indirect_ptr(l1_block, l2_idx);
    }

    return 0;
}

/// @brief Validate all block pointers (direct, indirect, double-indirect) for an inode.
/// @details Claims each referenced block and reports invalid or out-of-range pointers.
void check_inode_blocks(u64 ino, Inode *inode) {
    if (inode->size == 0)
        return;

    u64 expected_blocks = (inode->size + BLOCK_SIZE - 1) / BLOCK_SIZE;

    // Check direct blocks
    for (u64 i = 0; i < 12 && i < expected_blocks; i++) {
        u64 block = inode->direct[i];
        if (block != 0) {
            if (block >= sb.total_blocks) {
                report_error("Inode %lu direct[%lu] points to invalid block %lu", ino, i, block);
            } else {
                claim_block(block, ino);
            }
        } else if (i < expected_blocks) {
            report_warning("Inode %lu has hole at direct[%lu] (sparse file)", ino, i);
        }
    }

    // Check single indirect
    if (inode->indirect != 0) {
        if (inode->indirect >= sb.total_blocks) {
            report_error("Inode %lu indirect block %lu is invalid", ino, inode->indirect);
        } else {
            claim_block(inode->indirect, ino);
            u8 data[BLOCK_SIZE];
            read_block(inode->indirect, data);
            u64 *ptrs = reinterpret_cast<u64 *>(data);
            for (u64 i = 0; i < PTRS_PER_BLOCK && (12 + i) < expected_blocks; i++) {
                if (ptrs[i] != 0) {
                    if (ptrs[i] >= sb.total_blocks) {
                        report_error(
                            "Inode %lu indirect[%lu] points to invalid block %lu", ino, i, ptrs[i]);
                    } else {
                        claim_block(ptrs[i], ino);
                    }
                }
            }
        }
    }

    // Check double indirect
    if (inode->double_indirect != 0) {
        if (inode->double_indirect >= sb.total_blocks) {
            report_error(
                "Inode %lu double_indirect block %lu is invalid", ino, inode->double_indirect);
        } else {
            claim_block(inode->double_indirect, ino);
            u8 l1_data[BLOCK_SIZE];
            read_block(inode->double_indirect, l1_data);
            u64 *l1_ptrs = reinterpret_cast<u64 *>(l1_data);

            for (u64 i = 0; i < PTRS_PER_BLOCK; i++) {
                u64 l1_block = l1_ptrs[i];
                if (l1_block == 0)
                    continue;

                if (l1_block >= sb.total_blocks) {
                    report_error("Inode %lu double_indirect[%lu] points to invalid block %lu",
                                 ino,
                                 i,
                                 l1_block);
                    continue;
                }

                claim_block(l1_block, ino);

                u8 l2_data[BLOCK_SIZE];
                read_block(l1_block, l2_data);
                u64 *l2_ptrs = reinterpret_cast<u64 *>(l2_data);

                for (u64 j = 0; j < PTRS_PER_BLOCK; j++) {
                    u64 data_block = l2_ptrs[j];
                    if (data_block != 0) {
                        if (data_block >= sb.total_blocks) {
                            report_error("Inode %lu double_indirect data block invalid: %lu",
                                         ino,
                                         data_block);
                        } else {
                            claim_block(data_block, ino);
                        }
                    }
                }
            }
        }
    }
}

/// @brief Recursively check a directory: verify entries, '.' / '..' presence, and child inodes.
/// @param ino Inode number of the directory to check.
/// @param path Human-readable path for error messages.
void check_directory(u64 ino, const std::string &path) {
    if (visited_inodes.count(ino)) {
        report_error("Directory cycle detected at inode %lu (%s)", ino, path.c_str());
        return;
    }
    visited_inodes.insert(ino);

    Inode &inode = inodes[ino];
    verbose_log("Checking directory: %s (inode %lu)", path.c_str(), ino);

    // Check mode
    if ((inode.mode & mode::TYPE_MASK) != mode::TYPE_DIR) {
        report_error("Inode %lu (%s) not a directory but mode=0x%x", ino, path.c_str(), inode.mode);
        return;
    }

    // Check blocks
    check_inode_blocks(ino, &inode);

    // Read directory data
    if (inode.size == 0) {
        report_error("Directory %s (inode %lu) has zero size", path.c_str(), ino);
        return;
    }

    u64 dir_block = inode.direct[0];
    if (dir_block == 0) {
        report_error("Directory %s (inode %lu) has no data block", path.c_str(), ino);
        return;
    }

    u8 data[BLOCK_SIZE];
    read_block(dir_block, data);

    bool found_dot = false;
    bool found_dotdot = false;
    size_t pos = 0;

    while (pos < inode.size && pos < BLOCK_SIZE) {
        DirEntry *entry = reinterpret_cast<DirEntry *>(data + pos);

        if (entry->rec_len == 0) {
            report_error("Directory %s has zero rec_len at offset %zu", path.c_str(), pos);
            break;
        }

        if (entry->rec_len > BLOCK_SIZE - pos) {
            report_error("Directory %s has invalid rec_len %u at offset %zu",
                         path.c_str(),
                         entry->rec_len,
                         pos);
            break;
        }

        if (entry->inode != 0) {
            char name[256] = {};
            memcpy(name, data + pos + sizeof(DirEntry), entry->name_len);

            // Detect "." and ".." entries using both name matching and structural
            // heuristics. The secondary checks handle edge cases where the name
            // bytes may be corrupt but the inode/type/length metadata is consistent:
            //  - "." : name_len==1, type==DIR, inode==self
            //  - "..": name_len==2, type==DIR, first byte is NUL (corrupt name)
            bool is_dot_entry =
                (strcmp(name, ".") == 0) ||
                (entry->name_len == 1 && entry->file_type == file_type::DIR && entry->inode == ino);
            bool is_dotdot_entry =
                (strcmp(name, "..") == 0) ||
                (entry->name_len == 2 && entry->file_type == file_type::DIR && name[0] == '\0');

            if (is_dot_entry) {
                if (strcmp(name, ".") != 0) {
                    report_error("Directory %s: '.' entry has corrupted name (name_len=%u)",
                                 path.c_str(),
                                 entry->name_len);
                }
                found_dot = true;
                if (entry->inode != ino) {
                    report_error("Directory %s: '.' points to inode %lu instead of %lu",
                                 path.c_str(),
                                 entry->inode,
                                 ino);
                }
            } else if (is_dotdot_entry) {
                if (strcmp(name, "..") != 0) {
                    report_error("Directory %s: '..' entry has corrupted name", path.c_str());
                }
                found_dotdot = true;
            } else {
                // Check child inode
                if (entry->inode >= sb.inode_count) {
                    report_error("Directory %s entry '%s' has invalid inode %lu",
                                 path.c_str(),
                                 name,
                                 entry->inode);
                } else {
                    Inode &child = inodes[entry->inode];
                    if (child.inode_num == 0) {
                        report_error("Directory %s entry '%s' points to unallocated inode %lu",
                                     path.c_str(),
                                     name,
                                     entry->inode);
                    } else {
                        // Recurse into subdirectories (skip self-references to avoid cycles)
                        if (entry->inode == ino) {
                            report_error(
                                "Directory %s has self-referencing entry '%s'", path.c_str(), name);
                        } else {
                            std::string child_path = path;
                            if (path != "/")
                                child_path += "/";
                            child_path += name;

                            if (entry->file_type == file_type::DIR) {
                                check_directory(entry->inode, child_path);
                            } else if (entry->file_type == file_type::FILE ||
                                       entry->file_type == file_type::LINK) {
                                verbose_log(
                                    "  File: %s (inode %lu)", child_path.c_str(), entry->inode);
                                check_inode_blocks(entry->inode, &child);
                                visited_inodes.insert(entry->inode);
                            }
                        }
                    }
                }
            }
        }

        pos += entry->rec_len;
    }

    if (!found_dot) {
        report_error("Directory %s missing '.' entry", path.c_str());
    }
    if (!found_dotdot) {
        report_error("Directory %s missing '..' entry", path.c_str());
    }
}

/// @brief Read and validate the superblock: magic, version, block size, and layout.
/// @return true if the superblock is usable, false on fatal error.
bool check_superblock() {
    printf("Checking superblock...\n");

    if (!read_block(0, &sb)) {
        report_error("Failed to read superblock");
        return false;
    }

    if (sb.magic != VIPERFS_MAGIC) {
        report_error("Invalid magic number: 0x%08x (expected 0x%08x)", sb.magic, VIPERFS_MAGIC);
        return false;
    }

    if (sb.version != VIPERFS_VERSION) {
        report_warning("Filesystem version %u (expected %u)", sb.version, VIPERFS_VERSION);
    }

    if (sb.block_size != BLOCK_SIZE) {
        report_error("Invalid block size: %lu (expected %lu)", sb.block_size, BLOCK_SIZE);
        return false;
    }

    verbose_log("  Magic: 0x%08x", sb.magic);
    verbose_log("  Version: %u", sb.version);
    verbose_log("  Total blocks: %lu", sb.total_blocks);
    verbose_log("  Free blocks: %lu", sb.free_blocks);
    verbose_log("  Inode count: %lu", sb.inode_count);
    verbose_log("  Root inode: %lu", sb.root_inode);
    verbose_log(
        "  Bitmap: blocks %lu-%lu", sb.bitmap_start, sb.bitmap_start + sb.bitmap_blocks - 1);
    verbose_log("  Inode table: blocks %lu-%lu",
                sb.inode_table_start,
                sb.inode_table_start + sb.inode_table_blocks - 1);
    verbose_log("  Data start: %lu", sb.data_start);
    verbose_log("  Label: %s", sb.label);

    // Validate layout
    if (sb.bitmap_start != 1) {
        report_warning("Bitmap doesn't start at block 1 (starts at %lu)", sb.bitmap_start);
    }

    if (sb.inode_table_start != sb.bitmap_start + sb.bitmap_blocks) {
        report_error("Inode table doesn't immediately follow bitmap");
    }

    if (sb.data_start != sb.inode_table_start + sb.inode_table_blocks) {
        report_error("Data blocks don't immediately follow inode table");
    }

    if (sb.root_inode != ROOT_INODE) {
        report_warning("Root inode is %lu (expected %lu)", sb.root_inode, ROOT_INODE);
    }

    printf("  Superblock OK\n");
    return true;
}

/// @brief Load the on-disk block bitmap and initialize the computed bitmap.
/// @return true on success, false if bitmap blocks could not be read.
bool load_bitmap() {
    printf("Loading block bitmap...\n");

    disk_bitmap.resize(sb.bitmap_blocks * BLOCK_SIZE, 0);
    computed_bitmap.resize(sb.bitmap_blocks * BLOCK_SIZE, 0);

    for (u64 i = 0; i < sb.bitmap_blocks; i++) {
        if (!read_block(sb.bitmap_start + i, &disk_bitmap[i * BLOCK_SIZE])) {
            report_error("Failed to read bitmap block %lu", sb.bitmap_start + i);
            return false;
        }
    }

    // Mark metadata blocks as used in computed bitmap
    for (u64 i = 0; i < sb.data_start; i++) {
        mark_block_computed(i);
    }

    printf("  Bitmap loaded (%llu blocks)\n", (unsigned long long)sb.bitmap_blocks);
    return true;
}

/// @brief Load all inodes from the inode table blocks into memory.
/// @return true on success, false if inode table blocks could not be read.
bool load_inodes() {
    printf("Loading inode table...\n");

    inodes.resize(sb.inode_count);

    for (u64 i = 0; i < sb.inode_table_blocks; i++) {
        u8 block_data[BLOCK_SIZE];
        if (!read_block(sb.inode_table_start + i, block_data)) {
            report_error("Failed to read inode table block %lu", sb.inode_table_start + i);
            return false;
        }

        Inode *block_inodes = reinterpret_cast<Inode *>(block_data);
        for (u64 j = 0; j < INODES_PER_BLOCK && (i * INODES_PER_BLOCK + j) < sb.inode_count; j++) {
            inodes[i * INODES_PER_BLOCK + j] = block_inodes[j];
        }
    }

    // Count allocated inodes
    u64 allocated = 0;
    for (u64 i = 0; i < sb.inode_count; i++) {
        if (inodes[i].inode_num != 0) {
            allocated++;
            // Verify inode_num matches index
            if (inodes[i].inode_num != i) {
                report_warning("Inode at index %lu has inode_num %lu", i, inodes[i].inode_num);
            }
        }
    }

    printf("  Loaded %llu inodes (%llu allocated)\n",
           (unsigned long long)sb.inode_count,
           (unsigned long long)allocated);
    return true;
}

/// @brief Walk the directory tree from the root inode and detect orphaned inodes.
void check_directory_tree() {
    printf("Checking directory tree...\n");

    // Start from root
    if (inodes[sb.root_inode].inode_num == 0) {
        report_error("Root inode %lu is not allocated", sb.root_inode);
        return;
    }

    check_directory(sb.root_inode, "/");

    // Check for orphaned inodes
    for (u64 i = ROOT_INODE; i < sb.inode_count; i++) {
        if (inodes[i].inode_num != 0 && !visited_inodes.count(i)) {
            report_warning("Orphaned inode %lu (not reachable from root)", i);
        }
    }

    printf("  Directory tree checked (%zu inodes visited)\n", visited_inodes.size());
}

/// @brief Compare computed vs on-disk block bitmaps and detect allocation inconsistencies.
void check_block_allocation() {
    printf("Checking block allocation...\n");

    // Check for blocks claimed by multiple inodes
    for (auto &kv : block_owners) {
        if (kv.second.size() > 1) {
            report_error("Block %lu claimed by multiple inodes:", kv.first);
            for (u64 ino : kv.second) {
                fprintf(stderr, "  - Inode %llu\n", (unsigned long long)ino);
            }
        }
    }

    // Compare computed vs disk bitmap
    u64 computed_used = 0;
    u64 disk_used = 0;
    u64 mismatch_used = 0; // computed says used, disk says free
    u64 mismatch_free = 0; // computed says free, disk says used

    for (u64 i = 0; i < sb.total_blocks; i++) {
        bool computed = is_block_computed(i);
        bool on_disk = is_block_used_disk(i);

        if (computed)
            computed_used++;
        if (on_disk)
            disk_used++;

        if (computed && !on_disk) {
            if (mismatch_used < 10) {
                report_error("Block %lu is used but marked free in bitmap", i);
            }
            mismatch_used++;
        } else if (!computed && on_disk && i >= sb.data_start) {
            // Only warn about data blocks marked used but not claimed
            if (mismatch_free < 10) {
                report_warning("Block %lu is marked used but not claimed by any inode", i);
            }
            mismatch_free++;
        }
    }

    if (mismatch_used > 10) {
        fprintf(stderr,
                "  ... and %llu more used-but-free errors\n",
                (unsigned long long)(mismatch_used - 10));
    }
    if (mismatch_free > 10) {
        fprintf(stderr,
                "  ... and %llu more unreferenced blocks\n",
                (unsigned long long)(mismatch_free - 10));
    }

    // Check free block count
    u64 actual_free = sb.total_blocks - disk_used;
    if (actual_free != sb.free_blocks) {
        report_warning(
            "Superblock free_blocks=%lu but counted %lu free", sb.free_blocks, actual_free);
    }

    printf("  Block allocation checked:\n");
    printf("    Computed used: %llu blocks\n", (unsigned long long)computed_used);
    printf("    Bitmap used: %llu blocks\n", (unsigned long long)disk_used);
    printf("    Actual free: %llu blocks\n", (unsigned long long)actual_free);
}

/// @brief Print command-line usage information to stderr.
void print_usage(const char *prog) {
    fprintf(stderr, "Usage: %s [-v] <image>\n", prog);
    fprintf(stderr, "\n");
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -v    Verbose output\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Checks ViperFS filesystem integrity and reports errors.\n");
}

int main(int argc, char **argv) {
    const char *image_path = nullptr;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-v") == 0) {
            verbose = true;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (argv[i][0] != '-') {
            image_path = argv[i];
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            return 1;
        }
    }

    if (!image_path) {
        print_usage(argv[0]);
        return 1;
    }

    printf("fsck.viperfs - ViperFS Filesystem Check\n");
    printf("Checking: %s\n\n", image_path);

    disk_fp = fopen(image_path, "rb");
    if (!disk_fp) {
        perror(image_path);
        return 1;
    }

    // Get file size
    fseek(disk_fp, 0, SEEK_END);
    u64 file_size = ftell(disk_fp);
    fseek(disk_fp, 0, SEEK_SET);
    printf("Image size: %llu bytes (%llu blocks)\n\n",
           (unsigned long long)file_size,
           (unsigned long long)(file_size / BLOCK_SIZE));

    // Run checks
    if (!check_superblock()) {
        fclose(disk_fp);
        return 1;
    }

    if (!load_bitmap()) {
        fclose(disk_fp);
        return 1;
    }

    if (!load_inodes()) {
        fclose(disk_fp);
        return 1;
    }

    check_directory_tree();
    check_block_allocation();

    fclose(disk_fp);

    // Summary
    printf("\n");
    printf("=== Summary ===\n");
    printf("Errors:   %d\n", error_count);
    printf("Warnings: %d\n", warning_count);

    if (error_count == 0 && warning_count == 0) {
        printf("\nFilesystem is clean.\n");
        return 0;
    } else if (error_count == 0) {
        printf("\nFilesystem has minor issues but is usable.\n");
        return 0;
    } else {
        printf("\nFilesystem has errors!\n");
        return 1;
    }
}
