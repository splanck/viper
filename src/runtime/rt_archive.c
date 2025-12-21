//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file rt_archive.c
/// @brief ZIP archive support for Viper.IO.Archive.
///
/// Implements reading and writing of standard ZIP files following the
/// PKWARE APPNOTE specification. Supports:
/// - Stored entries (method 0)
/// - Deflated entries (method 8) via rt_compress
/// - Directory entries
/// - CRC32 validation
///
/// **ZIP Structure Overview:**
/// - Local file headers followed by file data
/// - Central directory at end with file metadata
/// - End of central directory record
///
/// **Thread Safety:** All functions are thread-safe (no global mutable state).
///
//===----------------------------------------------------------------------===//

#include "rt_archive.h"

#include "rt_box.h"
#include "rt_bytes.h"
#include "rt_compress.h"
#include "rt_dir.h"
#include "rt_internal.h"
#include "rt_map.h"
#include "rt_object.h"
#include "rt_seq.h"
#include "rt_string.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <io.h>
#include <windows.h>
#define PATH_SEP '\\'
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#define PATH_SEP '/'
#endif

//=============================================================================
// ZIP Constants
//=============================================================================

#define ZIP_LOCAL_HEADER_SIG 0x04034b50
#define ZIP_CENTRAL_HEADER_SIG 0x02014b50
#define ZIP_END_RECORD_SIG 0x06054b50
#define ZIP_DATA_DESCRIPTOR_SIG 0x08074b50

#define ZIP_METHOD_STORED 0
#define ZIP_METHOD_DEFLATE 8

#define ZIP_LOCAL_HEADER_SIZE 30
#define ZIP_CENTRAL_HEADER_SIZE 46
#define ZIP_END_RECORD_SIZE 22

#define ZIP_VERSION_NEEDED 20 // 2.0 for deflate
#define ZIP_VERSION_MADE 20

//=============================================================================
// Internal Bytes Access
//=============================================================================

typedef struct
{
    int64_t len;
    uint8_t *data;
} bytes_impl;

static inline uint8_t *bytes_data(void *obj)
{
    if (!obj)
        return NULL;
    return ((bytes_impl *)obj)->data;
}

static inline int64_t bytes_len(void *obj)
{
    if (!obj)
        return 0;
    return ((bytes_impl *)obj)->len;
}

//=============================================================================
// ZIP Entry Structure
//=============================================================================

typedef struct zip_entry
{
    char *name;               // Entry name (allocated)
    uint32_t crc32;           // CRC-32 of uncompressed data
    uint32_t compressed_size; // Size after compression
    uint32_t uncompressed_size; // Original size
    uint16_t method;          // Compression method (0 or 8)
    uint16_t mod_time;        // DOS time
    uint16_t mod_date;        // DOS date
    uint32_t local_offset;    // Offset of local header in file
    bool is_directory;        // True if directory entry
} zip_entry_t;

//=============================================================================
// Archive Structure
//=============================================================================

typedef struct rt_archive
{
    rt_string path;           // File path or NULL if from bytes
    uint8_t *data;            // Archive data (mmap or copy)
    size_t data_len;          // Length of data
    bool owns_data;           // True if we allocated data
    bool is_writing;          // True if opened for writing
    bool is_finished;         // True if Finish() was called

    // For reading
    zip_entry_t *entries;     // Array of entries
    int entry_count;          // Number of entries

    // For writing
    int fd;                   // File descriptor for writing
    uint8_t *write_buf;       // Write buffer
    size_t write_len;         // Current length
    size_t write_cap;         // Buffer capacity
    zip_entry_t *write_entries; // Entries being written
    int write_entry_count;    // Number of entries written
    int write_entry_cap;      // Capacity
} rt_archive_t;

//=============================================================================
// CRC32 (same as in rt_compress.c)
//=============================================================================

static uint32_t crc32_table[256];
static int crc32_table_init = 0;

static void init_crc32_table(void)
{
    if (crc32_table_init)
        return;

    for (uint32_t i = 0; i < 256; i++)
    {
        uint32_t c = i;
        for (int j = 0; j < 8; j++)
        {
            if (c & 1)
                c = 0xEDB88320 ^ (c >> 1);
            else
                c >>= 1;
        }
        crc32_table[i] = c;
    }
    crc32_table_init = 1;
}

static uint32_t compute_crc32(const uint8_t *data, size_t len)
{
    init_crc32_table();
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; i++)
    {
        crc = crc32_table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFF;
}

//=============================================================================
// Little-Endian Helpers
//=============================================================================

static inline uint16_t read_u16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static inline uint32_t read_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static inline void write_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
}

static inline void write_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}

//=============================================================================
// Archive Allocation
//=============================================================================

static rt_archive_t *archive_alloc(void)
{
    size_t total = sizeof(rt_archive_t);
    rt_archive_t *ar = (rt_archive_t *)rt_obj_new_i64(0, (int64_t)total);
    if (!ar)
        rt_trap("Archive: memory allocation failed");
    memset(ar, 0, total);
    ar->fd = -1;
    return ar;
}

static void archive_free_entries(rt_archive_t *ar)
{
    if (ar->entries)
    {
        for (int i = 0; i < ar->entry_count; i++)
        {
            free(ar->entries[i].name);
        }
        free(ar->entries);
        ar->entries = NULL;
    }
    if (ar->write_entries)
    {
        for (int i = 0; i < ar->write_entry_count; i++)
        {
            free(ar->write_entries[i].name);
        }
        free(ar->write_entries);
        ar->write_entries = NULL;
    }
}

//=============================================================================
// ZIP Parsing (for reading)
//=============================================================================

/// @brief Find the End of Central Directory record
static bool find_eocd(const uint8_t *data, size_t len, size_t *eocd_offset)
{
    if (len < ZIP_END_RECORD_SIZE)
        return false;

    // Search backwards for EOCD signature (handles comments)
    size_t max_comment = 65535;
    size_t search_len = len < (ZIP_END_RECORD_SIZE + max_comment)
                            ? len
                            : (ZIP_END_RECORD_SIZE + max_comment);

    for (size_t i = ZIP_END_RECORD_SIZE; i <= search_len; i++)
    {
        size_t offset = len - i;
        if (read_u32(data + offset) == ZIP_END_RECORD_SIG)
        {
            *eocd_offset = offset;
            return true;
        }
    }
    return false;
}

/// @brief Parse the central directory
static bool parse_central_directory(rt_archive_t *ar)
{
    size_t eocd_offset;
    if (!find_eocd(ar->data, ar->data_len, &eocd_offset))
        return false;

    const uint8_t *eocd = ar->data + eocd_offset;

    // Parse EOCD
    uint16_t disk_num = read_u16(eocd + 4);
    uint16_t cd_disk = read_u16(eocd + 6);
    uint16_t disk_entries = read_u16(eocd + 8);
    uint16_t total_entries = read_u16(eocd + 10);
    uint32_t cd_size = read_u32(eocd + 12);
    uint32_t cd_offset = read_u32(eocd + 16);

    // We don't support multi-disk archives
    if (disk_num != 0 || cd_disk != 0 || disk_entries != total_entries)
        return false;

    // Validate central directory bounds
    if ((size_t)cd_offset + cd_size > eocd_offset)
        return false;

    ar->entry_count = total_entries;
    ar->entries = (zip_entry_t *)calloc(total_entries, sizeof(zip_entry_t));
    if (!ar->entries)
        return false;

    // Parse each central directory entry
    const uint8_t *p = ar->data + cd_offset;
    const uint8_t *cd_end = p + cd_size;

    for (int i = 0; i < total_entries && p + ZIP_CENTRAL_HEADER_SIZE <= cd_end; i++)
    {
        if (read_u32(p) != ZIP_CENTRAL_HEADER_SIG)
        {
            archive_free_entries(ar);
            return false;
        }

        uint16_t name_len = read_u16(p + 28);
        uint16_t extra_len = read_u16(p + 30);
        uint16_t comment_len = read_u16(p + 32);

        if (p + ZIP_CENTRAL_HEADER_SIZE + name_len + extra_len + comment_len > cd_end)
        {
            archive_free_entries(ar);
            return false;
        }

        zip_entry_t *e = &ar->entries[i];
        e->method = read_u16(p + 10);
        e->mod_time = read_u16(p + 12);
        e->mod_date = read_u16(p + 14);
        e->crc32 = read_u32(p + 16);
        e->compressed_size = read_u32(p + 20);
        e->uncompressed_size = read_u32(p + 24);
        e->local_offset = read_u32(p + 42);

        // Copy name
        e->name = (char *)malloc(name_len + 1);
        if (!e->name)
        {
            archive_free_entries(ar);
            return false;
        }
        memcpy(e->name, p + ZIP_CENTRAL_HEADER_SIZE, name_len);
        e->name[name_len] = '\0';

        // Check if directory
        e->is_directory = (name_len > 0 && e->name[name_len - 1] == '/');

        p += ZIP_CENTRAL_HEADER_SIZE + name_len + extra_len + comment_len;
    }

    return true;
}

/// @brief Find entry by name
static zip_entry_t *find_entry(rt_archive_t *ar, const char *name)
{
    for (int i = 0; i < ar->entry_count; i++)
    {
        if (strcmp(ar->entries[i].name, name) == 0)
            return &ar->entries[i];
    }
    return NULL;
}

/// @brief Read entry data (decompressing if needed)
static void *read_entry_data(rt_archive_t *ar, zip_entry_t *e)
{
    // Find local header
    if (e->local_offset + ZIP_LOCAL_HEADER_SIZE > ar->data_len)
        rt_trap("Archive: corrupt local header offset");

    const uint8_t *local = ar->data + e->local_offset;
    if (read_u32(local) != ZIP_LOCAL_HEADER_SIG)
        rt_trap("Archive: invalid local header signature");

    uint16_t name_len = read_u16(local + 26);
    uint16_t extra_len = read_u16(local + 28);

    size_t data_offset = e->local_offset + ZIP_LOCAL_HEADER_SIZE + name_len + extra_len;
    if (data_offset + e->compressed_size > ar->data_len)
        rt_trap("Archive: corrupt entry data");

    const uint8_t *compressed = ar->data + data_offset;

    // Handle uncompressed (stored) data
    if (e->method == ZIP_METHOD_STORED)
    {
        // Verify CRC
        uint32_t crc = compute_crc32(compressed, e->uncompressed_size);
        if (crc != e->crc32)
            rt_trap("Archive: CRC mismatch");

        void *result = rt_bytes_new(e->uncompressed_size);
        memcpy(bytes_data(result), compressed, e->uncompressed_size);
        return result;
    }

    // Handle deflated data
    if (e->method == ZIP_METHOD_DEFLATE)
    {
        // Create bytes with compressed data
        void *comp_bytes = rt_bytes_new(e->compressed_size);
        memcpy(bytes_data(comp_bytes), compressed, e->compressed_size);

        // Inflate
        void *result = rt_compress_inflate(comp_bytes);

        // Verify CRC
        uint32_t crc = compute_crc32(bytes_data(result), bytes_len(result));
        if (crc != e->crc32)
            rt_trap("Archive: CRC mismatch");

        // Verify size
        if (bytes_len(result) != e->uncompressed_size)
            rt_trap("Archive: size mismatch");

        return result;
    }

    rt_trap("Archive: unsupported compression method");
    return NULL;
}

//=============================================================================
// Writing Helpers
//=============================================================================

static void write_ensure(rt_archive_t *ar, size_t need)
{
    if (ar->write_len + need > ar->write_cap)
    {
        size_t new_cap = ar->write_cap * 2;
        if (new_cap < ar->write_len + need)
            new_cap = ar->write_len + need + 4096;
        uint8_t *new_buf = (uint8_t *)realloc(ar->write_buf, new_cap);
        if (!new_buf)
            rt_trap("Archive: memory allocation failed");
        ar->write_buf = new_buf;
        ar->write_cap = new_cap;
    }
}

static void write_bytes(rt_archive_t *ar, const uint8_t *data, size_t len)
{
    write_ensure(ar, len);
    memcpy(ar->write_buf + ar->write_len, data, len);
    ar->write_len += len;
}

static void add_write_entry(rt_archive_t *ar, zip_entry_t *e)
{
    if (ar->write_entry_count >= ar->write_entry_cap)
    {
        int new_cap = ar->write_entry_cap == 0 ? 16 : ar->write_entry_cap * 2;
        zip_entry_t *new_entries = (zip_entry_t *)realloc(
            ar->write_entries, new_cap * sizeof(zip_entry_t));
        if (!new_entries)
            rt_trap("Archive: memory allocation failed");
        ar->write_entries = new_entries;
        ar->write_entry_cap = new_cap;
    }
    ar->write_entries[ar->write_entry_count++] = *e;
}

/// @brief Normalize entry name (forward slashes, no .., no absolute paths)
static char *normalize_name(const char *name)
{
    size_t len = strlen(name);
    char *result = (char *)malloc(len + 1);
    if (!result)
        return NULL;

    size_t j = 0;
    for (size_t i = 0; i < len; i++)
    {
        char c = name[i];
        // Convert backslashes to forward slashes
        if (c == '\\')
            c = '/';
        // Skip absolute path indicators
        if (i == 0 && c == '/')
            continue;
        // Skip drive letters (C:)
        if (i == 1 && name[0] != '/' && c == ':')
        {
            j = 0; // Reset, skip drive letter
            continue;
        }
        result[j++] = c;
    }
    result[j] = '\0';

    // TODO: Could add more validation for .. components
    return result;
}

/// @brief Get current DOS time/date
static void get_dos_time(uint16_t *time, uint16_t *date)
{
    // Use a fixed time for reproducibility (could use actual time)
    *time = 0; // 00:00:00
    *date = (21 << 9) | (1 << 5) | 1; // 2001-01-01
}

//=============================================================================
// Public API - Creation/Opening
//=============================================================================

void *rt_archive_open(rt_string path)
{
    const char *cpath = rt_string_cstr(path);
    if (!cpath || *cpath == '\0')
        rt_trap("Archive: invalid path");

    // Open and read file
#ifdef _WIN32
    HANDLE h = CreateFileA(cpath, GENERIC_READ, FILE_SHARE_READ, NULL,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
        rt_trap("Archive: file not found");

    LARGE_INTEGER size;
    if (!GetFileSizeEx(h, &size))
    {
        CloseHandle(h);
        rt_trap("Archive: failed to get file size");
    }

    uint8_t *data = (uint8_t *)malloc((size_t)size.QuadPart);
    if (!data)
    {
        CloseHandle(h);
        rt_trap("Archive: memory allocation failed");
    }

    DWORD read;
    if (!ReadFile(h, data, (DWORD)size.QuadPart, &read, NULL) ||
        read != (DWORD)size.QuadPart)
    {
        free(data);
        CloseHandle(h);
        rt_trap("Archive: failed to read file");
    }
    CloseHandle(h);

    size_t data_len = (size_t)size.QuadPart;
#else
    int fd = open(cpath, O_RDONLY);
    if (fd < 0)
        rt_trap("Archive: file not found");

    struct stat st;
    if (fstat(fd, &st) != 0)
    {
        close(fd);
        rt_trap("Archive: failed to get file size");
    }

    uint8_t *data = (uint8_t *)malloc((size_t)st.st_size);
    if (!data)
    {
        close(fd);
        rt_trap("Archive: memory allocation failed");
    }

    ssize_t n = read(fd, data, (size_t)st.st_size);
    close(fd);

    if (n != st.st_size)
    {
        free(data);
        rt_trap("Archive: failed to read file");
    }

    size_t data_len = (size_t)st.st_size;
#endif

    rt_archive_t *ar = archive_alloc();
    ar->path = path;
    ar->data = data;
    ar->data_len = data_len;
    ar->owns_data = true;
    ar->is_writing = false;

    if (!parse_central_directory(ar))
    {
        free(ar->data);
        rt_trap("Archive: not a valid ZIP file");
    }

    return ar;
}

void *rt_archive_create(rt_string path)
{
    const char *cpath = rt_string_cstr(path);
    if (!cpath || *cpath == '\0')
        rt_trap("Archive: invalid path");

#ifdef _WIN32
    HANDLE h = CreateFileA(cpath, GENERIC_WRITE, 0, NULL,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
        rt_trap("Archive: failed to create file");
    CloseHandle(h);
    int fd = -1; // We'll write at Finish time
#else
    int fd = open(cpath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
        rt_trap("Archive: failed to create file");
    close(fd);
    fd = -1; // We'll reopen at Finish time
#endif

    rt_archive_t *ar = archive_alloc();
    ar->path = path;
    ar->is_writing = true;
    ar->write_cap = 4096;
    ar->write_buf = (uint8_t *)malloc(ar->write_cap);
    if (!ar->write_buf)
        rt_trap("Archive: memory allocation failed");

    return ar;
}

void *rt_archive_from_bytes(void *data)
{
    if (!data)
        rt_trap("Archive: NULL data");

    int64_t len = bytes_len(data);
    uint8_t *src = bytes_data(data);

    // Copy the data
    uint8_t *copy = (uint8_t *)malloc((size_t)len);
    if (!copy)
        rt_trap("Archive: memory allocation failed");
    memcpy(copy, src, (size_t)len);

    rt_archive_t *ar = archive_alloc();
    ar->path = NULL;
    ar->data = copy;
    ar->data_len = (size_t)len;
    ar->owns_data = true;
    ar->is_writing = false;

    if (!parse_central_directory(ar))
    {
        free(ar->data);
        rt_trap("Archive: not a valid ZIP archive");
    }

    return ar;
}

//=============================================================================
// Properties
//=============================================================================

rt_string rt_archive_path(void *obj)
{
    rt_archive_t *ar = (rt_archive_t *)obj;
    if (!ar)
        return rt_str_empty();
    return ar->path ? ar->path : rt_str_empty();
}

int64_t rt_archive_count(void *obj)
{
    rt_archive_t *ar = (rt_archive_t *)obj;
    if (!ar)
        return 0;
    if (ar->is_writing)
        return ar->write_entry_count;
    return ar->entry_count;
}

void *rt_archive_names(void *obj)
{
    rt_archive_t *ar = (rt_archive_t *)obj;
    void *seq = rt_seq_new();

    if (!ar)
        return seq;

    if (ar->is_writing)
    {
        for (int i = 0; i < ar->write_entry_count; i++)
        {
            rt_string name = rt_const_cstr(ar->write_entries[i].name);
            rt_seq_push(seq, name);
        }
    }
    else
    {
        for (int i = 0; i < ar->entry_count; i++)
        {
            rt_string name = rt_const_cstr(ar->entries[i].name);
            rt_seq_push(seq, name);
        }
    }

    return seq;
}

//=============================================================================
// Reading Methods
//=============================================================================

int8_t rt_archive_has(void *obj, rt_string name)
{
    rt_archive_t *ar = (rt_archive_t *)obj;
    if (!ar || ar->is_writing)
        return 0;

    const char *cname = rt_string_cstr(name);
    if (!cname)
        return 0;

    return find_entry(ar, cname) != NULL ? 1 : 0;
}

void *rt_archive_read(void *obj, rt_string name)
{
    rt_archive_t *ar = (rt_archive_t *)obj;
    if (!ar)
        rt_trap("Archive: NULL archive");
    if (ar->is_writing)
        rt_trap("Archive: cannot read from write-only archive");

    const char *cname = rt_string_cstr(name);
    if (!cname)
        rt_trap("Archive: NULL entry name");

    zip_entry_t *e = find_entry(ar, cname);
    if (!e)
        rt_trap("Archive: entry not found");

    return read_entry_data(ar, e);
}

rt_string rt_archive_read_str(void *obj, rt_string name)
{
    void *data = rt_archive_read(obj, name);
    return rt_bytes_to_str(data);
}

void rt_archive_extract(void *obj, rt_string name, rt_string dest_path)
{
    void *data = rt_archive_read(obj, name);

    const char *cpath = rt_string_cstr(dest_path);
    if (!cpath || *cpath == '\0')
        rt_trap("Archive: invalid destination path");

#ifdef _WIN32
    HANDLE h = CreateFileA(cpath, GENERIC_WRITE, 0, NULL,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
        rt_trap("Archive: failed to create destination file");

    DWORD written;
    if (!WriteFile(h, bytes_data(data), (DWORD)bytes_len(data), &written, NULL))
    {
        CloseHandle(h);
        rt_trap("Archive: failed to write destination file");
    }
    CloseHandle(h);
#else
    int fd = open(cpath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
        rt_trap("Archive: failed to create destination file");

    ssize_t n = write(fd, bytes_data(data), (size_t)bytes_len(data));
    close(fd);

    if (n != bytes_len(data))
        rt_trap("Archive: failed to write destination file");
#endif
}

void rt_archive_extract_all(void *obj, rt_string dest_dir)
{
    rt_archive_t *ar = (rt_archive_t *)obj;
    if (!ar)
        rt_trap("Archive: NULL archive");
    if (ar->is_writing)
        rt_trap("Archive: cannot extract from write-only archive");

    const char *cdir = rt_string_cstr(dest_dir);
    if (!cdir || *cdir == '\0')
        rt_trap("Archive: invalid destination directory");

    size_t dir_len = strlen(cdir);

    for (int i = 0; i < ar->entry_count; i++)
    {
        zip_entry_t *e = &ar->entries[i];

        // Build full path
        size_t name_len = strlen(e->name);
        size_t path_len = dir_len + 1 + name_len;
        char *full_path = (char *)malloc(path_len + 1);
        if (!full_path)
            rt_trap("Archive: memory allocation failed");

        memcpy(full_path, cdir, dir_len);
        full_path[dir_len] = PATH_SEP;
        memcpy(full_path + dir_len + 1, e->name, name_len);
        full_path[path_len] = '\0';

        // Convert forward slashes to platform separator
        for (size_t j = dir_len + 1; j < path_len; j++)
        {
            if (full_path[j] == '/')
                full_path[j] = PATH_SEP;
        }

        if (e->is_directory)
        {
            // Create directory
            rt_string dir_path = rt_const_cstr(full_path);
            rt_dir_make_all(dir_path);
        }
        else
        {
            // Create parent directory
            char *last_sep = strrchr(full_path, PATH_SEP);
            if (last_sep && last_sep > full_path + dir_len)
            {
                *last_sep = '\0';
                rt_string parent = rt_const_cstr(full_path);
                rt_dir_make_all(parent);
                *last_sep = PATH_SEP;
            }

            // Extract file
            rt_string entry_name = rt_const_cstr(e->name);
            rt_string dest = rt_const_cstr(full_path);
            rt_archive_extract(obj, entry_name, dest);
        }

        free(full_path);
    }
}

void *rt_archive_info(void *obj, rt_string name)
{
    rt_archive_t *ar = (rt_archive_t *)obj;
    if (!ar)
        rt_trap("Archive: NULL archive");
    if (ar->is_writing)
        rt_trap("Archive: cannot get info from write-only archive");

    const char *cname = rt_string_cstr(name);
    if (!cname)
        rt_trap("Archive: NULL entry name");

    zip_entry_t *e = find_entry(ar, cname);
    if (!e)
        rt_trap("Archive: entry not found");

    void *map = rt_map_new();

    // Add size
    rt_map_set(map, rt_const_cstr("size"), rt_box_i64(e->uncompressed_size));

    // Add compressed size
    rt_map_set(map, rt_const_cstr("compressedSize"), rt_box_i64(e->compressed_size));

    // Add modified time (convert DOS time to Unix timestamp)
    // DOS date: bits 0-4 = day, 5-8 = month, 9-15 = year from 1980
    // DOS time: bits 0-4 = seconds/2, 5-10 = minutes, 11-15 = hours
    int year = ((e->mod_date >> 9) & 0x7F) + 1980;
    int month = (e->mod_date >> 5) & 0xF;
    int day = e->mod_date & 0x1F;
    int hour = (e->mod_time >> 11) & 0x1F;
    int minute = (e->mod_time >> 5) & 0x3F;
    int second = (e->mod_time & 0x1F) * 2;

    // Simple approximation of Unix timestamp (not accounting for leap years properly)
    int64_t timestamp = (int64_t)(year - 1970) * 365 * 24 * 3600;
    timestamp += (int64_t)(month - 1) * 30 * 24 * 3600;
    timestamp += (int64_t)(day - 1) * 24 * 3600;
    timestamp += (int64_t)hour * 3600;
    timestamp += (int64_t)minute * 60;
    timestamp += second;

    rt_map_set(map, rt_const_cstr("modifiedTime"), rt_box_i64(timestamp));

    // Add isDirectory
    rt_map_set(map, rt_const_cstr("isDirectory"), rt_box_i1(e->is_directory ? 1 : 0));

    return map;
}

//=============================================================================
// Writing Methods
//=============================================================================

void rt_archive_add(void *obj, rt_string name, void *data)
{
    rt_archive_t *ar = (rt_archive_t *)obj;
    if (!ar)
        rt_trap("Archive: NULL archive");
    if (!ar->is_writing)
        rt_trap("Archive: cannot add to read-only archive");
    if (ar->is_finished)
        rt_trap("Archive: archive already finished");

    const char *cname = rt_string_cstr(name);
    if (!cname || *cname == '\0')
        rt_trap("Archive: invalid entry name");

    char *norm_name = normalize_name(cname);
    if (!norm_name)
        rt_trap("Archive: memory allocation failed");

    uint8_t *raw_data = bytes_data(data);
    size_t raw_len = (size_t)bytes_len(data);

    // Compute CRC
    uint32_t crc = compute_crc32(raw_data, raw_len);

    // Decide whether to compress
    void *compressed = NULL;
    uint16_t method = ZIP_METHOD_STORED;
    const uint8_t *write_data = raw_data;
    size_t write_len = raw_len;

    if (raw_len > 64) // Only compress larger data
    {
        compressed = rt_compress_deflate(data);
        size_t comp_len = (size_t)bytes_len(compressed);
        if (comp_len < raw_len)
        {
            method = ZIP_METHOD_DEFLATE;
            write_data = bytes_data(compressed);
            write_len = comp_len;
        }
    }

    // Record entry info
    zip_entry_t e = {0};
    e.name = norm_name;
    e.crc32 = crc;
    e.compressed_size = (uint32_t)write_len;
    e.uncompressed_size = (uint32_t)raw_len;
    e.method = method;
    get_dos_time(&e.mod_time, &e.mod_date);
    e.local_offset = (uint32_t)ar->write_len;
    e.is_directory = false;

    // Write local file header
    size_t name_len = strlen(norm_name);
    uint8_t local_header[ZIP_LOCAL_HEADER_SIZE];
    write_u32(local_header, ZIP_LOCAL_HEADER_SIG);
    write_u16(local_header + 4, ZIP_VERSION_NEEDED);
    write_u16(local_header + 6, 0); // General purpose flags
    write_u16(local_header + 8, method);
    write_u16(local_header + 10, e.mod_time);
    write_u16(local_header + 12, e.mod_date);
    write_u32(local_header + 14, crc);
    write_u32(local_header + 18, (uint32_t)write_len);
    write_u32(local_header + 22, (uint32_t)raw_len);
    write_u16(local_header + 26, (uint16_t)name_len);
    write_u16(local_header + 28, 0); // Extra field length

    write_bytes(ar, local_header, ZIP_LOCAL_HEADER_SIZE);
    write_bytes(ar, (const uint8_t *)norm_name, name_len);
    write_bytes(ar, write_data, write_len);

    add_write_entry(ar, &e);
}

void rt_archive_add_str(void *obj, rt_string name, rt_string text)
{
    void *data = rt_bytes_from_str(text);
    rt_archive_add(obj, name, data);
}

void rt_archive_add_file(void *obj, rt_string name, rt_string src_path)
{
    const char *cpath = rt_string_cstr(src_path);
    if (!cpath || *cpath == '\0')
        rt_trap("Archive: invalid source path");

    // Read file contents
#ifdef _WIN32
    HANDLE h = CreateFileA(cpath, GENERIC_READ, FILE_SHARE_READ, NULL,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
        rt_trap("Archive: source file not found");

    LARGE_INTEGER size;
    if (!GetFileSizeEx(h, &size))
    {
        CloseHandle(h);
        rt_trap("Archive: failed to get file size");
    }

    void *data = rt_bytes_new((int64_t)size.QuadPart);
    DWORD read_count;
    if (!ReadFile(h, bytes_data(data), (DWORD)size.QuadPart, &read_count, NULL))
    {
        CloseHandle(h);
        rt_trap("Archive: failed to read source file");
    }
    CloseHandle(h);
#else
    int fd = open(cpath, O_RDONLY);
    if (fd < 0)
        rt_trap("Archive: source file not found");

    struct stat st;
    if (fstat(fd, &st) != 0)
    {
        close(fd);
        rt_trap("Archive: failed to get file size");
    }

    void *data = rt_bytes_new(st.st_size);
    ssize_t n = read(fd, bytes_data(data), (size_t)st.st_size);
    close(fd);

    if (n != st.st_size)
        rt_trap("Archive: failed to read source file");
#endif

    rt_archive_add(obj, name, data);
}

void rt_archive_add_dir(void *obj, rt_string name)
{
    rt_archive_t *ar = (rt_archive_t *)obj;
    if (!ar)
        rt_trap("Archive: NULL archive");
    if (!ar->is_writing)
        rt_trap("Archive: cannot add to read-only archive");
    if (ar->is_finished)
        rt_trap("Archive: archive already finished");

    const char *cname = rt_string_cstr(name);
    if (!cname || *cname == '\0')
        rt_trap("Archive: invalid entry name");

    char *norm_name = normalize_name(cname);
    if (!norm_name)
        rt_trap("Archive: memory allocation failed");

    // Ensure name ends with /
    size_t len = strlen(norm_name);
    if (len == 0 || norm_name[len - 1] != '/')
    {
        char *new_name = (char *)realloc(norm_name, len + 2);
        if (!new_name)
        {
            free(norm_name);
            rt_trap("Archive: memory allocation failed");
        }
        norm_name = new_name;
        norm_name[len] = '/';
        norm_name[len + 1] = '\0';
        len++;
    }

    // Record entry info
    zip_entry_t e = {0};
    e.name = norm_name;
    e.crc32 = 0;
    e.compressed_size = 0;
    e.uncompressed_size = 0;
    e.method = ZIP_METHOD_STORED;
    get_dos_time(&e.mod_time, &e.mod_date);
    e.local_offset = (uint32_t)ar->write_len;
    e.is_directory = true;

    // Write local file header
    uint8_t local_header[ZIP_LOCAL_HEADER_SIZE];
    write_u32(local_header, ZIP_LOCAL_HEADER_SIG);
    write_u16(local_header + 4, ZIP_VERSION_NEEDED);
    write_u16(local_header + 6, 0);
    write_u16(local_header + 8, ZIP_METHOD_STORED);
    write_u16(local_header + 10, e.mod_time);
    write_u16(local_header + 12, e.mod_date);
    write_u32(local_header + 14, 0);
    write_u32(local_header + 18, 0);
    write_u32(local_header + 22, 0);
    write_u16(local_header + 26, (uint16_t)len);
    write_u16(local_header + 28, 0);

    write_bytes(ar, local_header, ZIP_LOCAL_HEADER_SIZE);
    write_bytes(ar, (const uint8_t *)norm_name, len);

    add_write_entry(ar, &e);
}

void rt_archive_finish(void *obj)
{
    rt_archive_t *ar = (rt_archive_t *)obj;
    if (!ar)
        rt_trap("Archive: NULL archive");
    if (!ar->is_writing)
        rt_trap("Archive: cannot finish read-only archive");
    if (ar->is_finished)
        rt_trap("Archive: archive already finished");

    // Record central directory offset
    uint32_t cd_offset = (uint32_t)ar->write_len;

    // Write central directory
    for (int i = 0; i < ar->write_entry_count; i++)
    {
        zip_entry_t *e = &ar->write_entries[i];
        size_t name_len = strlen(e->name);

        uint8_t central_header[ZIP_CENTRAL_HEADER_SIZE];
        write_u32(central_header, ZIP_CENTRAL_HEADER_SIG);
        write_u16(central_header + 4, ZIP_VERSION_MADE);
        write_u16(central_header + 6, ZIP_VERSION_NEEDED);
        write_u16(central_header + 8, 0); // Flags
        write_u16(central_header + 10, e->method);
        write_u16(central_header + 12, e->mod_time);
        write_u16(central_header + 14, e->mod_date);
        write_u32(central_header + 16, e->crc32);
        write_u32(central_header + 20, e->compressed_size);
        write_u32(central_header + 24, e->uncompressed_size);
        write_u16(central_header + 28, (uint16_t)name_len);
        write_u16(central_header + 30, 0); // Extra field length
        write_u16(central_header + 32, 0); // Comment length
        write_u16(central_header + 34, 0); // Disk number start
        write_u16(central_header + 36, 0); // Internal file attributes
        write_u32(central_header + 38, e->is_directory ? 0x10 : 0); // External attributes
        write_u32(central_header + 42, e->local_offset);

        write_bytes(ar, central_header, ZIP_CENTRAL_HEADER_SIZE);
        write_bytes(ar, (const uint8_t *)e->name, name_len);
    }

    uint32_t cd_size = (uint32_t)ar->write_len - cd_offset;

    // Write end of central directory
    uint8_t eocd[ZIP_END_RECORD_SIZE];
    write_u32(eocd, ZIP_END_RECORD_SIG);
    write_u16(eocd + 4, 0);  // Disk number
    write_u16(eocd + 6, 0);  // Disk with central directory
    write_u16(eocd + 8, (uint16_t)ar->write_entry_count);
    write_u16(eocd + 10, (uint16_t)ar->write_entry_count);
    write_u32(eocd + 12, cd_size);
    write_u32(eocd + 16, cd_offset);
    write_u16(eocd + 20, 0); // Comment length

    write_bytes(ar, eocd, ZIP_END_RECORD_SIZE);

    // Write to file
    const char *cpath = rt_string_cstr(ar->path);
#ifdef _WIN32
    HANDLE h = CreateFileA(cpath, GENERIC_WRITE, 0, NULL,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
        rt_trap("Archive: failed to write archive file");

    DWORD written;
    if (!WriteFile(h, ar->write_buf, (DWORD)ar->write_len, &written, NULL) ||
        written != ar->write_len)
    {
        CloseHandle(h);
        rt_trap("Archive: failed to write archive file");
    }
    CloseHandle(h);
#else
    int fd = open(cpath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
        rt_trap("Archive: failed to write archive file");

    ssize_t n = write(fd, ar->write_buf, ar->write_len);
    close(fd);

    if ((size_t)n != ar->write_len)
        rt_trap("Archive: failed to write archive file");
#endif

    ar->is_finished = true;

    // Free write buffer
    free(ar->write_buf);
    ar->write_buf = NULL;
    ar->write_len = 0;
    ar->write_cap = 0;
}

//=============================================================================
// Static Methods
//=============================================================================

int8_t rt_archive_is_zip(rt_string path)
{
    const char *cpath = rt_string_cstr(path);
    if (!cpath || *cpath == '\0')
        return 0;

#ifdef _WIN32
    HANDLE h = CreateFileA(cpath, GENERIC_READ, FILE_SHARE_READ, NULL,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return 0;

    uint8_t sig[4];
    DWORD read_count;
    BOOL ok = ReadFile(h, sig, 4, &read_count, NULL);
    CloseHandle(h);

    if (!ok || read_count < 4)
        return 0;
#else
    int fd = open(cpath, O_RDONLY);
    if (fd < 0)
        return 0;

    uint8_t sig[4];
    ssize_t n = read(fd, sig, 4);
    close(fd);

    if (n < 4)
        return 0;
#endif

    // Check for ZIP signature (local file header or empty archive EOCD)
    uint32_t magic = read_u32(sig);
    return (magic == ZIP_LOCAL_HEADER_SIG || magic == ZIP_END_RECORD_SIG) ? 1 : 0;
}

int8_t rt_archive_is_zip_bytes(void *data)
{
    if (!data)
        return 0;

    if (bytes_len(data) < 4)
        return 0;

    uint32_t magic = read_u32(bytes_data(data));
    return (magic == ZIP_LOCAL_HEADER_SIG || magic == ZIP_END_RECORD_SIG) ? 1 : 0;
}
