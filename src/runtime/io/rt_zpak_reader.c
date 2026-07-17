//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/io/rt_zpak_reader.c
// Purpose: Runtime reader for ZPAK (Zanna Pack Archive) format. Parses archives
//          from memory buffers (embedded in .rodata) or files (.zpak on disk).
//          Supports per-entry DEFLATE decompression.
//
// Key invariants:
//   - Memory-backed archives do not copy the blob; the caller keeps it alive.
//   - File-backed archives read the TOC into memory but read entry data on demand.
//   - TOC entries are sorted by name after parsing for binary search.
//   - Returned data buffers are heap-allocated and caller-owned.
//
// Ownership/Lifetime:
//   - zpak_archive_t owns its entries array and file handle.
//   - zpak_read_entry returns malloc'd buffers.
//
// Links: ZpakWriter.cpp (build-time writer), rt_asset.c (consumer)
//
//===----------------------------------------------------------------------===//

#include "rt_zpak_reader.h"

#include "rt_file_stdio.h"
#include "rt_object.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define zpak_fseek(fp, off, whence) _fseeki64((fp), (__int64)(off), (whence))
#define zpak_ftell(fp) _ftelli64((fp))
#else
#define zpak_fseek(fp, off, whence) fseeko((fp), (off_t)(off), (whence))
#define zpak_ftell(fp) ftello((fp))
#endif

// ─── Runtime decompression ──────────────────────────────────────────────────
// We use the runtime's Bytes-based inflate and raw extraction helpers.
// Defined in rt_compress.c, rt_bytes.c, and rt_internal.h respectively.

extern void *rt_bytes_from_raw(const uint8_t *data, size_t len);
extern void *rt_compress_inflate(void *data);
extern uint8_t *rt_bytes_extract_raw(void *bytes, size_t *out_len);

/// @brief Decrement the refcount on a GC object and free it when it reaches zero.
static void zpak_release_object(void *obj) {
    if (obj && rt_obj_release_check0(obj))
        rt_obj_free(obj);
}

// ─── ZPAK format constants ───────────────────────────────────────────────────

static const uint8_t kMagic[4] = {'Z', 'P', 'A', 'K'};
static const size_t kHeaderSize = 32;

// ─── Little-endian read helpers ─────────────────────────────────────────────

/// @brief Read a 2-byte little-endian unsigned integer from an unaligned byte pointer.
static uint16_t read16LE(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

/// @brief Read a 4-byte little-endian unsigned integer from an unaligned byte pointer.
static uint32_t read32LE(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/// @brief Read an 8-byte little-endian unsigned integer from an unaligned byte pointer.
static uint64_t read64LE(const uint8_t *p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i)
        v |= (uint64_t)p[i] << (i * 8);
    return v;
}

// ─── TOC parsing (shared by memory and file paths) ──────────────────────────

/// @brief Parse the TOC region from a byte buffer.
/// @param toc_data  Pointer to start of TOC bytes.
/// @param toc_size  Size of TOC region in bytes.
/// @param out_count Set to number of entries parsed.
/// @return Heap-allocated array of zpak_entry_t, or NULL on error.
static zpak_entry_t *parse_toc(const uint8_t *toc_data,
                              size_t toc_size,
                              uint32_t expected_count,
                              uint32_t *out_count) {
    *out_count = 0;
    if (expected_count == 0)
        return NULL;
    if (!toc_data || toc_size == 0) {
        *out_count = 0;
        return NULL;
    }

    zpak_entry_t *entries = (zpak_entry_t *)calloc(expected_count, sizeof(zpak_entry_t));
    if (!entries) {
        *out_count = 0;
        return NULL;
    }

    const uint8_t *p = toc_data;
    const uint8_t *end = toc_data + toc_size;
    uint32_t i = 0;
    uint32_t initialized = 0;

    while (i < expected_count) {
        if ((size_t)(end - p) < 2)
            goto fail;
        // name_len (2 bytes)
        uint16_t name_len = read16LE(p);
        p += 2;

        if (name_len == 0 || (size_t)(end - p) < (size_t)name_len + 28)
            goto fail;

        // name (UTF-8 bytes)
        entries[i].name = (char *)malloc(name_len + 1);
        if (!entries[i].name)
            goto fail;
        initialized = i + 1;
        memcpy(entries[i].name, p, name_len);
        entries[i].name[name_len] = '\0';
        for (uint32_t prior = 0; prior < i; ++prior) {
            if (entries[prior].name && strcmp(entries[prior].name, entries[i].name) == 0)
                goto fail;
        }
        p += name_len;

        // data_offset (8 bytes)
        entries[i].data_offset = read64LE(p);
        p += 8;

        // data_size — original uncompressed (8 bytes)
        entries[i].data_size = read64LE(p);
        p += 8;

        // stored_size (8 bytes)
        entries[i].stored_size = read64LE(p);
        p += 8;

        // flags (2 bytes)
        uint16_t flags = read16LE(p);
        p += 2;
        entries[i].compressed = (flags & 1) ? 1 : 0;

        // reserved (2 bytes)
        p += 2;

        i++;
    }

    if (i != expected_count || p != end)
        goto fail;

    *out_count = i;
    return entries;

fail:
    for (uint32_t j = 0; j < initialized; ++j) {
#ifdef _MSC_VER
#pragma warning(suppress : 6001)
#endif
        if (entries[j].name)
            free(entries[j].name);
    }
    free(entries);
    *out_count = 0;
    return NULL;
}

// ─── Sort comparator for binary search ──────────────────────────────────────

/// @brief qsort comparator for zpak_entry_t — orders entries lexicographically by name.
static int entry_cmp(const void *a, const void *b) {
    const zpak_entry_t *ea = (const zpak_entry_t *)a;
    const zpak_entry_t *eb = (const zpak_entry_t *)b;
    return strcmp(ea->name, eb->name);
}

static int zpak_file_size(FILE *file, uint64_t *out_size) {
    if (!file || !out_size)
        return 0;
    if (zpak_fseek(file, 0, SEEK_END) != 0)
        return 0;
    int64_t pos = (int64_t)zpak_ftell(file);
    if (pos < 0)
        return 0;
    if (zpak_fseek(file, 0, SEEK_SET) != 0)
        return 0;
    *out_size = (uint64_t)pos;
    return 1;
}

// ─── zpak_open_memory ────────────────────────────────────────────────────────

/// @brief Open a ZPAK archive from an in-memory buffer (e.g., embedded .rodata blob).
/// @details The buffer is borrowed, not copied — the caller must keep it alive
///          for the lifetime of the archive. Parses the header and TOC, then
///          sorts entries by name for binary search in zpak_find.
zpak_archive_t *zpak_open_memory(const uint8_t *data, size_t size) {
    if (!data || size < kHeaderSize)
        return NULL;

    // Validate magic
    if (memcmp(data, kMagic, 4) != 0)
        return NULL;

    // Parse header
    // uint16_t version = read16LE(data + 4);  // unused for now
    // uint16_t flags = read16LE(data + 6);     // unused for now
    uint32_t entry_count = read32LE(data + 8);
    uint64_t toc_offset = read64LE(data + 12);
    uint64_t toc_size = read64LE(data + 20);

    // Validate TOC bounds
    if (toc_size > (uint64_t)SIZE_MAX || toc_offset > (uint64_t)size ||
        toc_size > (uint64_t)size - toc_offset)
        return NULL;
    if (entry_count == 0 && toc_size != 0)
        return NULL;

    // Parse TOC
    uint32_t parsed_count = 0;
    zpak_entry_t *entries =
        parse_toc(data + toc_offset, (size_t)toc_size, entry_count, &parsed_count);
    if (parsed_count != entry_count || (entry_count > 0 && !entries))
        return NULL;

    // Create archive
    zpak_archive_t *archive = (zpak_archive_t *)calloc(1, sizeof(zpak_archive_t));
    if (!archive) {
        for (uint32_t i = 0; i < parsed_count; i++)
            free(entries[i].name);
        free(entries);
        return NULL;
    }

    archive->entries = entries;
    archive->count = parsed_count;
    archive->blob = data;
    archive->blob_size = size;
    archive->file = NULL;

    // Sort entries by name for binary search
    if (archive->count > 1)
        qsort(archive->entries, archive->count, sizeof(zpak_entry_t), entry_cmp);

    return archive;
}

// ─── zpak_open_file ──────────────────────────────────────────────────────────

/// @brief Open a ZPAK archive from a .zpak file on disk.
/// @details Reads the header and TOC into memory. Entry data is read on demand
///          via zpak_read_entry. The file handle is kept open until zpak_close.
static zpak_archive_t *zpak_open_file_impl(const char *path, int no_follow) {
    if (!path)
        return NULL;

#if !defined(_WIN32)
    FILE *f = NULL;
    if (no_follow) {
        struct stat lst;
        if (lstat(path, &lst) != 0 || !S_ISREG(lst.st_mode))
            return NULL;
        int flags = O_RDONLY;
#ifdef O_CLOEXEC
        flags |= O_CLOEXEC;
#endif
#ifdef O_NOFOLLOW
        flags |= O_NOFOLLOW;
#endif
        int fd = open(path, flags);
        if (fd < 0)
            return NULL;
#if defined(FD_CLOEXEC) && !defined(O_CLOEXEC)
        int fd_flags = fcntl(fd, F_GETFD);
        if (fd_flags >= 0)
            (void)fcntl(fd, F_SETFD, fd_flags | FD_CLOEXEC);
#endif
        f = fdopen(fd, "rb");
        if (!f) {
            close(fd);
            return NULL;
        }
    } else {
        f = rt_file_stdio_open_utf8(path, "rb");
    }
#else
    FILE *f = rt_file_stdio_open_utf8(path, "rb");
#endif
    if (!f)
        return NULL;

#if !defined(_WIN32)
    if (no_follow) {
        struct stat st;
        if (fstat(fileno(f), &st) != 0 || !S_ISREG(st.st_mode)) {
            fclose(f);
            return NULL;
        }
    }
#else
    (void)no_follow;
#endif

    uint64_t file_size = 0;
    if (!zpak_file_size(f, &file_size) || file_size < kHeaderSize) {
        fclose(f);
        return NULL;
    }

    // Read header
    uint8_t header[32];
    if (fread(header, 1, kHeaderSize, f) != kHeaderSize) {
        fclose(f);
        return NULL;
    }

    // Validate magic
    if (memcmp(header, kMagic, 4) != 0) {
        fclose(f);
        return NULL;
    }

    uint32_t entry_count = read32LE(header + 8);
    uint64_t toc_offset = read64LE(header + 12);
    uint64_t toc_size = read64LE(header + 20);

    // Read TOC into memory
    if ((entry_count > 0 && toc_size == 0) || (entry_count == 0 && toc_size != 0) ||
        toc_size > 64 * 1024 * 1024 || toc_size > (uint64_t)SIZE_MAX || toc_offset > file_size ||
        toc_size > file_size - toc_offset) { // 64MB TOC limit
        fclose(f);
        return NULL;
    }

    uint32_t parsed_count = 0;
    zpak_entry_t *entries = NULL;
    if (entry_count > 0) {
        uint8_t *toc_buf = (uint8_t *)malloc((size_t)toc_size);
        if (!toc_buf) {
            fclose(f);
            return NULL;
        }

        if (zpak_fseek(f, toc_offset, SEEK_SET) != 0) {
            free(toc_buf);
            fclose(f);
            return NULL;
        }

        if (fread(toc_buf, 1, (size_t)toc_size, f) != (size_t)toc_size) {
            free(toc_buf);
            fclose(f);
            return NULL;
        }

        entries = parse_toc(toc_buf, (size_t)toc_size, entry_count, &parsed_count);
        free(toc_buf);
        if (parsed_count != entry_count || !entries) {
            fclose(f);
            return NULL;
        }
    }

    // Create archive
    zpak_archive_t *archive = (zpak_archive_t *)calloc(1, sizeof(zpak_archive_t));
    if (!archive) {
        for (uint32_t i = 0; i < parsed_count; i++)
            free(entries[i].name);
        free(entries);
        fclose(f);
        return NULL;
    }

    archive->entries = entries;
    archive->count = parsed_count;
    archive->blob = NULL;
    archive->blob_size = 0;
    archive->file = f;
    archive->file_size = file_size;

    // Sort entries by name for binary search
    if (archive->count > 1)
        qsort(archive->entries, archive->count, sizeof(zpak_entry_t), entry_cmp);

    return archive;
}

zpak_archive_t *zpak_open_file(const char *path) {
    return zpak_open_file_impl(path, 0);
}

zpak_archive_t *zpak_open_file_no_follow(const char *path) {
    return zpak_open_file_impl(path, 1);
}

// ─── zpak_find ───────────────────────────────────────────────────────────────

/// @brief Find an entry by name using binary search on the sorted TOC.
const zpak_entry_t *zpak_find(const zpak_archive_t *archive, const char *name) {
    if (!archive || !name || archive->count == 0)
        return NULL;

    // Linear scan for small archives, binary search for larger ones.
    // Avoids const-cast issues with the key struct.
    if (archive->count <= 16) {
        for (uint32_t i = 0; i < archive->count; i++) {
            if (strcmp(archive->entries[i].name, name) == 0)
                return &archive->entries[i];
        }
        return NULL;
    }

    // Binary search for larger archives.
    uint32_t lo = 0, hi = archive->count;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        int cmp = strcmp(archive->entries[mid].name, name);
        if (cmp == 0)
            return &archive->entries[mid];
        if (cmp < 0)
            lo = mid + 1;
        else
            hi = mid;
    }
    return NULL;
}

// ─── zpak_read_entry ─────────────────────────────────────────────────────────

/// @brief Read and decompress an entry's data (returns malloc'd buffer, caller frees).
/// @details For compressed entries, reads the compressed data then inflates via
///          DEFLATE. For uncompressed entries, copies raw bytes from the archive.
uint8_t *zpak_read_entry(const zpak_archive_t *archive, const zpak_entry_t *entry, size_t *out_size) {
    if (!archive || !entry || !out_size)
        return NULL;
    *out_size = 0;
    if (entry->stored_size > (uint64_t)SIZE_MAX || entry->data_size > (uint64_t)SIZE_MAX)
        return NULL;

    uint8_t *stored = NULL;
    size_t stored_len = (size_t)entry->stored_size;
    size_t data_len = (size_t)entry->data_size;

    if (archive->blob) {
        // Memory-backed: data is directly in the blob
        if (entry->data_offset > (uint64_t)archive->blob_size ||
            stored_len > archive->blob_size - (size_t)entry->data_offset)
            return NULL;
        stored = (uint8_t *)malloc(stored_len > 0 ? stored_len : 1);
        if (!stored)
            return NULL;
        if (stored_len > 0)
            memcpy(stored, archive->blob + entry->data_offset, stored_len);
    } else if (archive->file) {
        // File-backed: seek and read
        if (entry->data_offset > archive->file_size ||
            stored_len > archive->file_size - entry->data_offset)
            return NULL;
        stored = (uint8_t *)malloc(stored_len > 0 ? stored_len : 1);
        if (!stored)
            return NULL;
        if (zpak_fseek(archive->file, entry->data_offset, SEEK_SET) != 0) {
            free(stored);
            return NULL;
        }
        if (stored_len > 0 && fread(stored, 1, stored_len, archive->file) != stored_len) {
            free(stored);
            return NULL;
        }
    } else {
        return NULL;
    }

    if (entry->compressed && stored_len > 0) {
        // Decompress via runtime DEFLATE: wrap in Bytes, inflate, extract raw
        void *bytes_obj = rt_bytes_from_raw(stored, stored_len);
        free(stored);
        if (!bytes_obj)
            return NULL;

        void *inflated = rt_compress_inflate(bytes_obj);
        zpak_release_object(bytes_obj);
        if (!inflated)
            return NULL;

        // Extract to malloc'd buffer (caller-owned)
        size_t result_len = 0;
        uint8_t *result = rt_bytes_extract_raw(inflated, &result_len);
        zpak_release_object(inflated);
        if (result_len != data_len) {
            free(result);
            return NULL;
        }
        if (!result) {
            result = (uint8_t *)malloc(1);
            if (!result)
                return NULL;
        }
        *out_size = result_len;
        return result;
    }

    // Uncompressed — return stored data directly
    if (stored_len != data_len) {
        free(stored);
        return NULL;
    }
    *out_size = stored_len;
    return stored;
}

// ─── zpak_close ──────────────────────────────────────────────────────────────

/// @brief Close a ZPAK archive, freeing the TOC entries and closing any file handle.
void zpak_close(zpak_archive_t *archive) {
    if (!archive)
        return;

    if (archive->entries) {
        for (uint32_t i = 0; i < archive->count; i++) {
#ifdef _MSC_VER
#pragma warning(suppress : 6001)
#endif
            if (archive->entries[i].name)
                free(archive->entries[i].name);
        }
        free(archive->entries);
    }

    if (archive->file)
        fclose(archive->file);

    free(archive);
}
