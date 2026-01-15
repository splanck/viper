/**
 * @file mkfs.viperfs.cpp
 * @brief Build a ViperFS disk image from a set of input files.
 *
 * @details
 * This tool is a host-side utility that creates a ViperFS filesystem image for
 * use by ViperDOS. It writes a simple on-disk layout consisting of:
 * - A 4 KiB superblock at block 0.
 * - A block allocation bitmap.
 * - An inode table (fixed-size inodes).
 * - Data blocks containing directory entries and file contents.
 *
 * The tool is intentionally pragmatic and optimized for OS bring-up:
 * - The inode format supports direct blocks plus a single indirect block.
 * - Only a subset of metadata is populated (timestamps, basic mode bits).
 * - Directories are constructed with `.` and `..` entries and a simple
 *   variable-length record layout.
 *
 * Command line usage:
 * - `mkfs.viperfs <image> <size_mb> [options...] [files...]`
 *
 * Options:
 * - `--mkdir <path>`: create a directory (and parents) inside the image.
 * - `--add <src>:<dest>`: add a host file `src` to image path `dest`.
 * - `<file>` (legacy): add a host file to the image root directory.
 */

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <map>
#include <string>
#include <vector>

// Types
using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;

/** @name Filesystem constants
 *  @brief On-disk constants shared by the mkfs tool and kernel ViperFS driver.
 *  @{
 */
constexpr u32 VIPERFS_MAGIC = 0x53465056; // "VPFS"
constexpr u32 VIPERFS_VERSION = 1;
constexpr u64 BLOCK_SIZE = 4096;
constexpr u64 INODE_SIZE = 256;
constexpr u64 INODES_PER_BLOCK = BLOCK_SIZE / INODE_SIZE;
constexpr u64 ROOT_INODE = 2;

/** @} */

/**
 * @brief ViperFS superblock (block 0).
 *
 * @details
 * The superblock describes the overall filesystem layout and key parameters.
 * It is written as one full 4 KiB block so it can be read with a single disk
 * I/O operation.
 *
 * Fields are written in little-endian host order (the tool targets typical
 * little-endian systems used for development).
 */
struct __attribute__((packed)) Superblock
{
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
    u8 _reserved[3928]; // Adjusted for packed struct
};

static_assert(sizeof(Superblock) == 4096, "Superblock must be 4096 bytes");

/**
 * @brief Mode bit definitions stored in @ref Inode::mode.
 *
 * @details
 * This is a small, filesystem-local permission/type model. The kernel may map
 * these into higher-level VFS permissions.
 */
namespace mode
{
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
 */
struct __attribute__((packed)) Inode
{
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
    u8 _reserved[72]; // Adjusted for packed struct (256 - 184 = 72)
};

static_assert(sizeof(Inode) == 256, "Inode must be 256 bytes");

/** @brief Directory entry type values stored in @ref DirEntry::file_type. */
namespace file_type
{
constexpr u8 FILE = 1;
constexpr u8 DIR = 2;
} // namespace file_type

/**
 * @brief Directory entry header used in directory data blocks.
 *
 * @details
 * Directory blocks contain a sequence of variable-length records. Each record
 * begins with this header and is followed by `name_len` bytes of name data.
 *
 * `rec_len` specifies the total size of the record, allowing the reader to
 * skip to the next entry. Records are aligned to 8 bytes.
 */
struct __attribute__((packed)) DirEntry
{
    u64 inode;
    u16 rec_len;
    u8 name_len;
    u8 file_type;
    // name follows
};

static_assert(sizeof(DirEntry) == 12, "DirEntry header must be 12 bytes");

/**
 * @brief Abstraction for writing a ViperFS disk image file.
 *
 * @details
 * `DiskImage` owns the output file and the in-memory bitmap/inode arrays. It
 * provides helpers to allocate blocks/inodes and to write the final metadata
 * structures back to disk.
 */
class DiskImage
{
  public:
    FILE *fp;
    u64 total_blocks;
    u64 next_free_block;
    u64 next_free_inode;

    std::vector<u8> bitmap;
    std::vector<Inode> inodes;

    /**
     * @brief Create a new empty image file and initialize filesystem metadata.
     *
     * @details
     * Calculates the filesystem layout based on the requested size:
     * - Superblock at block 0.
     * - Bitmap blocks immediately after.
     * - Inode table blocks sized heuristically as `total_blocks/64` (minimum 4).
     * - Data blocks follow the inode table.
     *
     * The method also marks all metadata blocks as used in the bitmap and
     * writes an initialized superblock to disk.
     *
     * @param path Output image file path.
     * @param size_mb Image size in megabytes.
     * @return True on success, false on error opening/writing the file.
     */
    bool create(const char *path, u64 size_mb)
    {
        fp = fopen(path, "w+b");
        if (!fp)
        {
            perror("fopen");
            return false;
        }

        total_blocks = (size_mb * 1024 * 1024) / BLOCK_SIZE;

        // Calculate layout
        // Block 0: superblock
        // Block 1-N: bitmap (1 bit per block)
        u64 bitmap_blocks = (total_blocks + BLOCK_SIZE * 8 - 1) / (BLOCK_SIZE * 8);

        // Inode table: 1 inode table block per 64 data blocks (heuristic)
        u64 inode_table_blocks = (total_blocks / 64) + 1;
        if (inode_table_blocks < 4)
            inode_table_blocks = 4;

        u64 bitmap_start = 1;
        u64 inode_table_start = bitmap_start + bitmap_blocks;
        u64 data_start = inode_table_start + inode_table_blocks;

        printf("Creating ViperFS image:\n");
        printf("  Total blocks: %lu\n", total_blocks);
        printf("  Bitmap: blocks %lu-%lu (%lu blocks)\n",
               bitmap_start,
               bitmap_start + bitmap_blocks - 1,
               bitmap_blocks);
        printf("  Inode table: blocks %lu-%lu (%lu blocks, %lu inodes)\n",
               inode_table_start,
               inode_table_start + inode_table_blocks - 1,
               inode_table_blocks,
               inode_table_blocks * INODES_PER_BLOCK);
        printf("  Data: blocks %lu-%lu\n", data_start, total_blocks - 1);

        // Initialize structures
        bitmap.resize(bitmap_blocks * BLOCK_SIZE, 0);
        inodes.resize(inode_table_blocks * INODES_PER_BLOCK);
        memset(inodes.data(), 0, inodes.size() * sizeof(Inode));

        // Mark system blocks as used
        for (u64 i = 0; i < data_start; i++)
        {
            mark_block_used(i);
        }

        next_free_block = data_start;
        next_free_inode = ROOT_INODE + 1; // Start at 3 (0, 1 reserved, 2 is root)

        // Create superblock
        Superblock sb = {};
        sb.magic = VIPERFS_MAGIC;
        sb.version = VIPERFS_VERSION;
        sb.block_size = BLOCK_SIZE;
        sb.total_blocks = total_blocks;
        sb.free_blocks = total_blocks - data_start;
        sb.inode_count = inode_table_blocks * INODES_PER_BLOCK;
        sb.root_inode = ROOT_INODE;
        sb.bitmap_start = bitmap_start;
        sb.bitmap_blocks = bitmap_blocks;
        sb.inode_table_start = inode_table_start;
        sb.inode_table_blocks = inode_table_blocks;
        sb.data_start = data_start;
        strncpy(sb.label, "ViperDOS", sizeof(sb.label) - 1);

        // Generate random UUID
        for (int i = 0; i < 16; i++)
        {
            sb.uuid[i] = rand() & 0xFF;
        }

        // Write superblock
        write_block(0, &sb);

        return true;
    }

    /**
     * @brief Mark a block as used in the allocation bitmap.
     *
     * @details
     * This is used during initialization to reserve metadata blocks and during
     * allocation to track data blocks.
     *
     * @param block Block index to mark used.
     */
    void mark_block_used(u64 block)
    {
        bitmap[block / 8] |= (1 << (block % 8));
    }

    /**
     * @brief Allocate one free data block.
     *
     * @details
     * Scans forward from `next_free_block` until a free bitmap bit is found,
     * marks it used, and returns the allocated block index.
     *
     * This is a simple first-fit allocator suitable for mkfs usage.
     *
     * @return Allocated block number.
     */
    u64 alloc_block()
    {
        while (next_free_block < total_blocks)
        {
            u64 byte = next_free_block / 8;
            u64 bit = next_free_block % 8;
            if (!(bitmap[byte] & (1 << bit)))
            {
                bitmap[byte] |= (1 << bit);
                return next_free_block++;
            }
            next_free_block++;
        }
        fprintf(stderr, "Out of blocks!\n");
        exit(1);
    }

    /**
     * @brief Allocate a new inode number.
     *
     * @details
     * Inodes are allocated sequentially starting at `ROOT_INODE + 1`. The inode
     * table is pre-sized based on the chosen layout; running out of inodes is a
     * fatal mkfs error.
     *
     * @return Allocated inode number.
     */
    u64 alloc_inode()
    {
        u64 ino = next_free_inode++;
        if (ino >= inodes.size())
        {
            fprintf(stderr, "Out of inodes!\n");
            exit(1);
        }
        return ino;
    }

    /**
     * @brief Write one 4 KiB block to the image file.
     *
     * @param block Block index to write.
     * @param data Pointer to 4 KiB of data to write.
     */
    void write_block(u64 block, const void *data)
    {
        fseek(fp, block * BLOCK_SIZE, SEEK_SET);
        fwrite(data, BLOCK_SIZE, 1, fp);
    }

    /**
     * @brief Read one 4 KiB block from the image file.
     *
     * @details
     * If the read fails, the destination buffer is zero-filled.
     *
     * @param block Block index to read.
     * @param data Output buffer (must be at least 4 KiB).
     */
    void read_block(u64 block, void *data)
    {
        fseek(fp, block * BLOCK_SIZE, SEEK_SET);
        size_t r = fread(data, BLOCK_SIZE, 1, fp);
        if (r != 1)
        {
            memset(data, 0, BLOCK_SIZE);
        }
    }

    /**
     * @brief Finalize and write filesystem metadata to disk.
     *
     * @details
     * Writes the in-memory bitmap and inode arrays to their on-disk locations,
     * extends the file to its full size, and closes the output file handle.
     *
     * @param bitmap_start First bitmap block index.
     * @param inode_table_start First inode table block index.
     */
    void finalize(u64 bitmap_start, u64 inode_table_start)
    {
        // Write bitmap
        for (size_t i = 0; i < bitmap.size() / BLOCK_SIZE; i++)
        {
            write_block(bitmap_start + i, &bitmap[i * BLOCK_SIZE]);
        }

        // Write inode table
        u8 inode_block[BLOCK_SIZE];
        for (size_t i = 0; i < inodes.size() / INODES_PER_BLOCK; i++)
        {
            memcpy(inode_block, &inodes[i * INODES_PER_BLOCK], BLOCK_SIZE);
            write_block(inode_table_start + i, inode_block);
        }

        // Extend file to full size
        fseek(fp, total_blocks * BLOCK_SIZE - 1, SEEK_SET);
        u8 zero = 0;
        fwrite(&zero, 1, 1, fp);

        fclose(fp);
    }
};

/**
 * @brief Map of already-created directory paths to their inode numbers.
 *
 * @details
 * When creating nested directories for `--mkdir` or `--add`, the tool needs to
 * avoid re-creating the same directory multiple times. This map caches the inode
 * number for each normalized directory path.
 */
std::map<std::string, u64> dir_inode_map;

/**
 * @brief Normalize an image path string.
 *
 * @details
 * Normalization rules used by this tool:
 * - Strip leading `/` characters (treat paths as relative within the image).
 * - Strip trailing `/` characters.
 *
 * @param path Input path.
 * @return Normalized path.
 */
std::string normalize_path(const std::string &path)
{
    std::string result = path;
    // Remove leading slashes
    while (!result.empty() && result[0] == '/')
    {
        result = result.substr(1);
    }
    // Remove trailing slashes
    while (!result.empty() && result.back() == '/')
    {
        result.pop_back();
    }
    return result;
}

/**
 * @brief Return the parent directory portion of a normalized path.
 *
 * @details
 * For a path without `/`, the parent is the root directory (represented by an
 * empty string). For `a/b/c`, the parent is `a/b`.
 *
 * @param path Normalized path.
 * @return Parent path (may be empty for root).
 */
std::string get_parent_path(const std::string &path)
{
    size_t pos = path.rfind('/');
    if (pos == std::string::npos)
    {
        return ""; // Root directory
    }
    return path.substr(0, pos);
}

/**
 * @brief Return the final path component (basename).
 *
 * @param path Normalized path.
 * @return Basename component.
 */
std::string get_basename(const std::string &path)
{
    size_t pos = path.rfind('/');
    if (pos == std::string::npos)
    {
        return path;
    }
    return path.substr(pos + 1);
}

// Forward declarations
u64 ensure_directory_exists(DiskImage &img, const std::string &path);
u64 add_directory(DiskImage &img, u64 parent_ino, const char *name);

/**
 * @brief Add an entry to an existing directory inode.
 *
 * @details
 * Directory contents are stored in a single data block referenced by the
 * directory's first direct pointer (`direct[0]`). The directory block contains
 * variable-length entries:
 * - Each entry has a `rec_len` that spans to the next entry.
 * - The last entry typically extends to the end of the block.
 *
 * To append a new entry, this function walks existing entries until it finds
 * the last one (the one whose `rec_len` spans to the end). It then splits that
 * record into:
 * - A resized original record (its minimum aligned size).
 * - A new record occupying the remaining space.
 *
 * This is a simple implementation suitable for mkfs usage; it does not handle
 * multi-block directories.
 *
 * @param img Disk image being constructed.
 * @param parent_ino Inode number of the directory to modify.
 * @param child_ino Inode number referenced by the new entry.
 * @param name Entry name (single path component).
 * @param ftype Entry type (see @ref file_type).
 * @return True on success, false if there is no space in the directory block.
 */
bool add_dir_entry(DiskImage &img, u64 parent_ino, u64 child_ino, const char *name, u8 ftype)
{
    Inode &parent = img.inodes[parent_ino];
    u64 dir_block_num = parent.direct[0];

    u8 dir_block[BLOCK_SIZE];
    img.read_block(dir_block_num, dir_block);

    // Find last entry and add new one
    size_t pos = 0;
    while (pos < BLOCK_SIZE)
    {
        DirEntry *entry = reinterpret_cast<DirEntry *>(dir_block + pos);
        if (entry->rec_len == 0)
            break;

        // Check if this is the last entry (rec_len extends to end)
        size_t actual_size = sizeof(DirEntry) + entry->name_len;
        actual_size = (actual_size + 7) & ~7; // Align to 8

        if (entry->rec_len > actual_size)
        {
            // Split this entry
            size_t name_len = strlen(name);
            size_t new_entry_size = sizeof(DirEntry) + name_len;
            new_entry_size = (new_entry_size + 7) & ~7;

            size_t remaining = entry->rec_len - actual_size;
            if (remaining >= new_entry_size)
            {
                // Create new entry
                DirEntry *new_entry = reinterpret_cast<DirEntry *>(dir_block + pos + actual_size);
                new_entry->inode = child_ino;
                new_entry->name_len = name_len;
                new_entry->file_type = ftype;
                new_entry->rec_len = remaining;
                memcpy(dir_block + pos + actual_size + sizeof(DirEntry), name, name_len);

                // Update old entry's rec_len
                entry->rec_len = actual_size;

                img.write_block(dir_block_num, dir_block);
                return true;
            }
        }

        pos += entry->rec_len;
    }

    fprintf(stderr, "No space in directory for '%s'\n", name);
    return false;
}

/**
 * @brief Create a subdirectory under an existing directory.
 *
 * @details
 * Allocates a new inode and one data block for the directory contents. The new
 * directory is initialized with `.` and `..` entries and is then added to the
 * parent directory via @ref add_dir_entry.
 *
 * @param img Disk image being constructed.
 * @param parent_ino Inode number of the parent directory.
 * @param name Directory name (single path component).
 * @return New directory inode number on success, or 0 on failure.
 */
u64 add_directory(DiskImage &img, u64 parent_ino, const char *name)
{
    u64 now = time(nullptr);

    // Allocate inode for directory
    u64 ino = img.alloc_inode();
    Inode &inode = img.inodes[ino];
    inode.inode_num = ino;
    inode.mode = mode::TYPE_DIR | mode::PERM_READ | mode::PERM_WRITE | mode::PERM_EXEC;
    inode.atime = inode.mtime = inode.ctime = now;

    // Create directory data block with . and ..
    u8 dir_block[BLOCK_SIZE] = {};
    size_t pos = 0;

    // Entry for "."
    DirEntry *dot = reinterpret_cast<DirEntry *>(dir_block + pos);
    dot->inode = ino;
    dot->name_len = 1;
    dot->file_type = file_type::DIR;
    dot->rec_len = 16; // 12 + 1 + padding
    memcpy(dir_block + pos + sizeof(DirEntry), ".", 1);
    pos += dot->rec_len;

    // Entry for ".."
    DirEntry *dotdot = reinterpret_cast<DirEntry *>(dir_block + pos);
    dotdot->inode = parent_ino;
    dotdot->name_len = 2;
    dotdot->file_type = file_type::DIR;
    dotdot->rec_len = BLOCK_SIZE - pos; // Rest of block
    memcpy(dir_block + pos + sizeof(DirEntry), "..", 2);

    // Allocate and write data block
    u64 data_block = img.alloc_block();
    img.write_block(data_block, dir_block);

    inode.direct[0] = data_block;
    inode.size = BLOCK_SIZE;
    inode.blocks = 1;

    // Add entry to parent directory
    if (!add_dir_entry(img, parent_ino, ino, name, file_type::DIR))
    {
        return 0;
    }

    printf("Created directory '%s' (inode %lu, data block %lu)\n", name, ino, data_block);
    return ino;
}

/**
 * @brief Ensure a directory path exists, creating parent directories as needed.
 *
 * @details
 * This helper is used to implement `--mkdir` and `--add <src>:<dest>`. The
 * input is normalized and then recursively ensures the parent exists before
 * creating the final directory component.
 *
 * The function caches created directories in @ref dir_inode_map so repeated
 * operations on the same path do not create duplicates.
 *
 * @param img Disk image being constructed.
 * @param path Directory path inside the image (may include `/` separators).
 * @return Inode number of the directory on success, or 0 on failure.
 */
u64 ensure_directory_exists(DiskImage &img, const std::string &path)
{
    std::string normalized = normalize_path(path);

    if (normalized.empty())
    {
        return ROOT_INODE;
    }

    // Check if already created
    auto it = dir_inode_map.find(normalized);
    if (it != dir_inode_map.end())
    {
        return it->second;
    }

    // Ensure parent exists first
    std::string parent_path = get_parent_path(normalized);
    u64 parent_ino = ensure_directory_exists(img, parent_path);
    if (parent_ino == 0)
    {
        return 0;
    }

    // Create this directory
    std::string name = get_basename(normalized);
    u64 ino = add_directory(img, parent_ino, name.c_str());
    if (ino == 0)
    {
        return 0;
    }

    dir_inode_map[normalized] = ino;
    return ino;
}

/**
 * @brief Create and initialize the root directory inode.
 *
 * @details
 * The root inode number is fixed as @ref ROOT_INODE. This function:
 * - Initializes the inode metadata and mode bits.
 * - Allocates one data block for directory contents.
 * - Writes `.` and `..` directory entries into that block.
 *
 * @param img Disk image being constructed.
 */
void create_root_dir(DiskImage &img)
{
    u64 now = time(nullptr);

    // Allocate root inode
    Inode &root = img.inodes[ROOT_INODE];
    root.inode_num = ROOT_INODE;
    root.mode = mode::TYPE_DIR | mode::PERM_READ | mode::PERM_WRITE | mode::PERM_EXEC;
    root.atime = root.mtime = root.ctime = now;

    // Create directory data block with . and ..
    u8 dir_block[BLOCK_SIZE] = {};
    size_t pos = 0;

    // Entry for "."
    DirEntry *dot = reinterpret_cast<DirEntry *>(dir_block + pos);
    dot->inode = ROOT_INODE;
    dot->name_len = 1;
    dot->file_type = file_type::DIR;
    dot->rec_len = 16; // 12 + 1 + padding
    memcpy(dir_block + pos + sizeof(DirEntry), ".", 1);
    pos += dot->rec_len;

    // Entry for ".."
    DirEntry *dotdot = reinterpret_cast<DirEntry *>(dir_block + pos);
    dotdot->inode = ROOT_INODE;
    dotdot->name_len = 2;
    dotdot->file_type = file_type::DIR;
    dotdot->rec_len = BLOCK_SIZE - pos; // Rest of block
    memcpy(dir_block + pos + sizeof(DirEntry), "..", 2);

    // Allocate and write data block
    u64 data_block = img.alloc_block();
    img.write_block(data_block, dir_block);

    root.direct[0] = data_block;
    root.size = BLOCK_SIZE;
    root.blocks = 1;

    printf("Created root directory (inode %lu, data block %lu)\n", ROOT_INODE, data_block);
}

/**
 * @brief Add an in-memory file to a directory.
 *
 * @details
 * Allocates a new inode and enough blocks to store the file contents. Data is
 * written using:
 * - Up to 12 direct blocks.
 * - One single-indirect block for additional blocks beyond the direct range.
 *
 * Double/triple indirection is not implemented in this mkfs tool.
 *
 * After writing the data blocks, a directory entry is added to the parent
 * directory.
 *
 * @param img Disk image being constructed.
 * @param parent_ino Parent directory inode number.
 * @param name File name (single path component).
 * @param data Pointer to file bytes.
 * @param size File size in bytes.
 * @return New file inode number on success, or 0 on failure.
 */
u64 add_file(DiskImage &img, u64 parent_ino, const char *name, const void *data, size_t size)
{
    u64 now = time(nullptr);

    // Allocate inode for file
    u64 ino = img.alloc_inode();
    Inode &inode = img.inodes[ino];
    inode.inode_num = ino;
    inode.mode = mode::TYPE_FILE | mode::PERM_READ | mode::PERM_WRITE;
    inode.size = size;
    inode.atime = inode.mtime = inode.ctime = now;

    // Write file data
    const u8 *src = static_cast<const u8 *>(data);
    u64 blocks_needed = (size + BLOCK_SIZE - 1) / BLOCK_SIZE;
    if (blocks_needed == 0)
        blocks_needed = 0; // Empty file
    inode.blocks = blocks_needed;

    constexpr u64 PTRS_PER_BLOCK = BLOCK_SIZE / sizeof(u64);

    // Direct blocks (0-11)
    for (u64 i = 0; i < blocks_needed && i < 12; i++)
    {
        u64 block = img.alloc_block();
        inode.direct[i] = block;

        u8 block_data[BLOCK_SIZE] = {};
        size_t to_copy = size - i * BLOCK_SIZE;
        if (to_copy > BLOCK_SIZE)
            to_copy = BLOCK_SIZE;
        memcpy(block_data, src + i * BLOCK_SIZE, to_copy);
        img.write_block(block, block_data);
    }

    // Single indirect blocks (12 to 12+511)
    if (blocks_needed > 12)
    {
        // Allocate indirect block
        u64 indirect_block = img.alloc_block();
        inode.indirect = indirect_block;

        u64 indirect_ptrs[PTRS_PER_BLOCK] = {};
        u64 indirect_count = blocks_needed - 12;
        if (indirect_count > PTRS_PER_BLOCK)
            indirect_count = PTRS_PER_BLOCK;

        for (u64 i = 0; i < indirect_count; i++)
        {
            u64 block = img.alloc_block();
            indirect_ptrs[i] = block;

            u8 block_data[BLOCK_SIZE] = {};
            u64 file_block_idx = 12 + i;
            size_t to_copy = size - file_block_idx * BLOCK_SIZE;
            if (to_copy > BLOCK_SIZE)
                to_copy = BLOCK_SIZE;
            memcpy(block_data, src + file_block_idx * BLOCK_SIZE, to_copy);
            img.write_block(block, block_data);
        }

        img.write_block(indirect_block, indirect_ptrs);
        printf("  (used indirect block %lu for %lu additional blocks)\n",
               indirect_block,
               indirect_count);
    }

    // Add directory entry to parent
    if (!add_dir_entry(img, parent_ino, ino, name, file_type::FILE))
    {
        return 0;
    }

    printf("Added file '%s' (inode %lu, %zu bytes)\n", name, ino, size);
    return ino;
}

/**
 * @brief Add a host file to the image root directory.
 *
 * @details
 * Reads the full file into memory and then calls @ref add_file. The destination
 * name is derived from the basename of the host path.
 *
 * @param img Disk image being constructed.
 * @param parent_ino Parent directory inode number (typically @ref ROOT_INODE).
 * @param path Host filesystem path to read.
 * @return New file inode number on success, or 0 on failure.
 */
u64 add_file_from_disk(DiskImage &img, u64 parent_ino, const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f)
    {
        perror(path);
        return 0;
    }

    fseek(f, 0, SEEK_END);
    size_t size = ftell(f);
    fseek(f, 0, SEEK_SET);

    std::vector<u8> data(size);
    if (size > 0)
    {
        size_t r = fread(data.data(), 1, size, f);
        if (r != size)
        {
            fprintf(stderr, "Short read from %s\n", path);
        }
    }
    fclose(f);

    // Get filename from path
    const char *name = strrchr(path, '/');
    if (name)
        name++;
    else
        name = path;

    return add_file(img, parent_ino, name, data.data(), data.size());
}

/**
 * @brief Add a host file to a specific destination path inside the image.
 *
 * @details
 * Reads the file at `src_path` and writes it to the image path `dest_path`.
 * Parent directories are created automatically as needed.
 *
 * The destination path uses image-internal separators (`/`). A leading `/` is
 * ignored and does not indicate an absolute host filesystem path.
 *
 * @param img Disk image being constructed.
 * @param src_path Host filesystem path to read.
 * @param dest_path Destination path inside the image (e.g., `SYS/certs/roots.der`).
 * @return New file inode number on success, or 0 on failure.
 */
u64 add_file_to_path(DiskImage &img, const char *src_path, const char *dest_path)
{
    FILE *f = fopen(src_path, "rb");
    if (!f)
    {
        perror(src_path);
        return 0;
    }

    fseek(f, 0, SEEK_END);
    size_t size = ftell(f);
    fseek(f, 0, SEEK_SET);

    std::vector<u8> data(size);
    if (size > 0)
    {
        size_t r = fread(data.data(), 1, size, f);
        if (r != size)
        {
            fprintf(stderr, "Short read from %s\n", src_path);
        }
    }
    fclose(f);

    // Parse destination path
    std::string dest = normalize_path(dest_path);
    std::string parent_path = get_parent_path(dest);
    std::string filename = get_basename(dest);

    // Ensure parent directory exists
    u64 parent_ino = ensure_directory_exists(img, parent_path);
    if (parent_ino == 0)
    {
        fprintf(stderr, "Failed to create parent directory for %s\n", dest_path);
        return 0;
    }

    printf("Adding %s -> %s\n", src_path, dest_path);
    return add_file(img, parent_ino, filename.c_str(), data.data(), data.size());
}

/**
 * @brief Print command line usage information.
 *
 * @param prog Program name (argv[0]).
 */
void print_usage(const char *prog)
{
    fprintf(stderr, "Usage: %s <image> <size_mb> [options...] [files...]\n", prog);
    fprintf(stderr, "\n");
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  --mkdir <path>         Create directory at path (e.g., SYS/certs)\n");
    fprintf(stderr, "  --add <src>:<dest>     Add file from src to dest path\n");
    fprintf(stderr, "  <file>                 Add file to root directory (legacy mode)\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Examples:\n");
    fprintf(
        stderr, "  %s disk.img 8 --mkdir SYS/certs --add roots.der:SYS/certs/roots.der\n", prog);
    fprintf(stderr, "  %s disk.img 8 vinit.sys --add app.prg:c/app.prg\n", prog);
}

/**
 * @brief Program entry point.
 *
 * @details
 * Creates an image file, initializes metadata, builds the root directory, and
 * then processes each command line argument:
 * - `--mkdir <path>` creates a directory (and parents).
 * - `--add <src>:<dest>` adds a file to the specified destination path.
 * - Any other argument is treated as a legacy "add to root" file path.
 *
 * After populating the filesystem tree, the bitmap and inode tables are
 * written to disk and the image file is extended to the full requested size.
 *
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return 0 on success, non-zero on error.
 */
int main(int argc, char **argv)
{
    if (argc < 3)
    {
        print_usage(argv[0]);
        return 1;
    }

    srand(time(nullptr));

    const char *image_path = argv[1];
    u64 size_mb = atoi(argv[2]);

    if (size_mb < 1)
    {
        fprintf(stderr, "Size must be at least 1 MB\n");
        return 1;
    }

    DiskImage img;
    if (!img.create(image_path, size_mb))
    {
        return 1;
    }

    // Create root directory
    create_root_dir(img);

    // Initialize root in directory map
    dir_inode_map[""] = ROOT_INODE;

    // Process arguments
    for (int i = 3; i < argc; i++)
    {
        if (strcmp(argv[i], "--mkdir") == 0)
        {
            // Create directory
            if (i + 1 >= argc)
            {
                fprintf(stderr, "Error: --mkdir requires a path argument\n");
                return 1;
            }
            i++;
            std::string path = normalize_path(argv[i]);
            printf("Creating directory: %s\n", path.c_str());
            if (ensure_directory_exists(img, path) == 0)
            {
                fprintf(stderr, "Failed to create directory: %s\n", argv[i]);
                return 1;
            }
        }
        else if (strcmp(argv[i], "--add") == 0)
        {
            // Add file to specific path
            if (i + 1 >= argc)
            {
                fprintf(stderr, "Error: --add requires a src:dest argument\n");
                return 1;
            }
            i++;

            // Parse src:dest
            const char *arg = argv[i];
            const char *colon = strchr(arg, ':');
            if (!colon)
            {
                fprintf(stderr, "Error: --add argument must be src:dest format\n");
                return 1;
            }

            std::string src(arg, colon - arg);
            std::string dest(colon + 1);

            if (add_file_to_path(img, src.c_str(), dest.c_str()) == 0)
            {
                fprintf(stderr, "Failed to add file: %s -> %s\n", src.c_str(), dest.c_str());
                return 1;
            }
        }
        else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0)
        {
            print_usage(argv[0]);
            return 0;
        }
        else
        {
            // Legacy mode: add file to root directory
            add_file_from_disk(img, ROOT_INODE, argv[i]);
        }
    }

    // Calculate layout again for finalize
    u64 bitmap_blocks = (img.total_blocks + BLOCK_SIZE * 8 - 1) / (BLOCK_SIZE * 8);
    u64 inode_table_blocks = (img.total_blocks / 64) + 1;
    if (inode_table_blocks < 4)
        inode_table_blocks = 4;
    u64 bitmap_start = 1;
    u64 inode_table_start = bitmap_start + bitmap_blocks;
    u64 data_start = inode_table_start + inode_table_blocks;

    // Count actual free blocks from bitmap
    u64 used_blocks = 0;
    for (u64 i = 0; i < img.total_blocks; i++)
    {
        if (img.bitmap[i / 8] & (1 << (i % 8)))
        {
            used_blocks++;
        }
    }
    u64 actual_free_blocks = img.total_blocks - used_blocks;

    // Update superblock with correct free_blocks count
    Superblock sb = {};
    sb.magic = VIPERFS_MAGIC;
    sb.version = VIPERFS_VERSION;
    sb.block_size = BLOCK_SIZE;
    sb.total_blocks = img.total_blocks;
    sb.free_blocks = actual_free_blocks;
    sb.inode_count = inode_table_blocks * INODES_PER_BLOCK;
    sb.root_inode = ROOT_INODE;
    sb.bitmap_start = bitmap_start;
    sb.bitmap_blocks = bitmap_blocks;
    sb.inode_table_start = inode_table_start;
    sb.inode_table_blocks = inode_table_blocks;
    sb.data_start = data_start;
    strncpy(sb.label, "ViperDOS", sizeof(sb.label) - 1);
    img.write_block(0, &sb);

    img.finalize(bitmap_start, inode_table_start);

    printf("Created %s (%lu MB, %lu blocks used, %lu free)\n",
           image_path, size_mb, used_blocks, actual_free_blocks);
    return 0;
}
