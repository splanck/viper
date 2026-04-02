//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/io/rt_vpa_reader.c
// Purpose: Runtime reader for VPA (Viper Pack Archive) format. Parses archives
//          from memory buffers (embedded in .rodata) or files (.vpa on disk).
//          Supports per-entry DEFLATE decompression.
//
// Key invariants:
//   - Memory-backed archives do not copy the blob; the caller keeps it alive.
//   - File-backed archives read the TOC into memory but read entry data on demand.
//   - TOC entries are sorted by name after parsing for binary search.
//   - Returned data buffers are heap-allocated and caller-owned.
//
// Ownership/Lifetime:
//   - vpa_archive_t owns its entries array and file handle.
//   - vpa_read_entry returns malloc'd buffers.
//
// Links: VpaWriter.cpp (build-time writer), rt_asset.c (consumer)
//
//===----------------------------------------------------------------------===//

#include "rt_vpa_reader.h"

#include <stdlib.h>
#include <string.h>

// ─── Runtime decompression ──────────────────────────────────────────────────
// We use the runtime's Bytes-based inflate and raw extraction helpers.
// Defined in rt_compress.c, rt_bytes.c, and rt_internal.h respectively.

extern void *rt_bytes_from_raw(const uint8_t *data, size_t len);
extern void *rt_compress_inflate(void *data);
extern uint8_t *rt_bytes_extract_raw(void *bytes, size_t *out_len);

// ─── VPA format constants ───────────────────────────────────────────────────

static const uint8_t kMagic[4] = {'V', 'P', 'A', '1'};
static const size_t kHeaderSize = 32;

// ─── Little-endian read helpers ─────────────────────────────────────────────

static uint16_t read16LE(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t read32LE(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

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
/// @return Heap-allocated array of vpa_entry_t, or NULL on error.
static vpa_entry_t *parse_toc(const uint8_t *toc_data, size_t toc_size,
                              uint32_t expected_count, uint32_t *out_count) {
    if (!toc_data || toc_size == 0) {
        *out_count = 0;
        return NULL;
    }

    vpa_entry_t *entries =
        (vpa_entry_t *)calloc(expected_count, sizeof(vpa_entry_t));
    if (!entries) {
        *out_count = 0;
        return NULL;
    }

    const uint8_t *p = toc_data;
    const uint8_t *end = toc_data + toc_size;
    uint32_t i = 0;

    while (i < expected_count && p + 2 <= end) {
        // name_len (2 bytes)
        uint16_t name_len = read16LE(p);
        p += 2;

        if (p + name_len + 28 > end) {
            // Truncated entry — stop parsing.
            break;
        }

        // name (UTF-8 bytes)
        entries[i].name = (char *)malloc(name_len + 1);
        if (!entries[i].name)
            break;
        memcpy(entries[i].name, p, name_len);
        entries[i].name[name_len] = '\0';
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

    *out_count = i;
    return entries;
}

// ─── Sort comparator for binary search ──────────────────────────────────────

static int entry_cmp(const void *a, const void *b) {
    const vpa_entry_t *ea = (const vpa_entry_t *)a;
    const vpa_entry_t *eb = (const vpa_entry_t *)b;
    return strcmp(ea->name, eb->name);
}

// ─── vpa_open_memory ────────────────────────────────────────────────────────

/// @brief Open a VPA archive from an in-memory buffer (e.g., embedded .rodata blob).
/// @details The buffer is borrowed, not copied — the caller must keep it alive
///          for the lifetime of the archive. Parses the header and TOC, then
///          sorts entries by name for binary search in vpa_find.
vpa_archive_t *vpa_open_memory(const uint8_t *data, size_t size) {
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
    if (toc_offset + toc_size > size)
        return NULL;

    // Parse TOC
    uint32_t parsed_count = 0;
    vpa_entry_t *entries =
        parse_toc(data + toc_offset, (size_t)toc_size, entry_count, &parsed_count);

    // Create archive
    vpa_archive_t *archive = (vpa_archive_t *)calloc(1, sizeof(vpa_archive_t));
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
        qsort(archive->entries, archive->count, sizeof(vpa_entry_t), entry_cmp);

    return archive;
}

// ─── vpa_open_file ──────────────────────────────────────────────────────────

/// @brief Open a VPA archive from a .vpa file on disk.
/// @details Reads the header and TOC into memory. Entry data is read on demand
///          via vpa_read_entry. The file handle is kept open until vpa_close.
vpa_archive_t *vpa_open_file(const char *path) {
    if (!path)
        return NULL;

    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;

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
    if (toc_size == 0 || toc_size > 64 * 1024 * 1024) { // 64MB TOC limit
        fclose(f);
        return NULL;
    }

    uint8_t *toc_buf = (uint8_t *)malloc((size_t)toc_size);
    if (!toc_buf) {
        fclose(f);
        return NULL;
    }

    if (fseek(f, (long)toc_offset, SEEK_SET) != 0) {
        free(toc_buf);
        fclose(f);
        return NULL;
    }

    if (fread(toc_buf, 1, (size_t)toc_size, f) != (size_t)toc_size) {
        free(toc_buf);
        fclose(f);
        return NULL;
    }

    // Parse TOC
    uint32_t parsed_count = 0;
    vpa_entry_t *entries = parse_toc(toc_buf, (size_t)toc_size, entry_count, &parsed_count);
    free(toc_buf);

    // Create archive
    vpa_archive_t *archive = (vpa_archive_t *)calloc(1, sizeof(vpa_archive_t));
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

    // Sort entries by name for binary search
    if (archive->count > 1)
        qsort(archive->entries, archive->count, sizeof(vpa_entry_t), entry_cmp);

    return archive;
}

// ─── vpa_find ───────────────────────────────────────────────────────────────

/// @brief Find an entry by name using binary search on the sorted TOC.
const vpa_entry_t *vpa_find(const vpa_archive_t *archive, const char *name) {
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

// ─── vpa_read_entry ─────────────────────────────────────────────────────────

/// @brief Read and decompress an entry's data (returns malloc'd buffer, caller frees).
/// @details For compressed entries, reads the compressed data then inflates via
///          DEFLATE. For uncompressed entries, copies raw bytes from the archive.
uint8_t *vpa_read_entry(const vpa_archive_t *archive, const vpa_entry_t *entry,
                        size_t *out_size) {
    if (!archive || !entry || !out_size)
        return NULL;

    uint8_t *stored = NULL;
    size_t stored_len = (size_t)entry->stored_size;

    if (archive->blob) {
        // Memory-backed: data is directly in the blob
        if (entry->data_offset + stored_len > archive->blob_size)
            return NULL;
        stored = (uint8_t *)malloc(stored_len);
        if (!stored)
            return NULL;
        memcpy(stored, archive->blob + entry->data_offset, stored_len);
    } else if (archive->file) {
        // File-backed: seek and read
        stored = (uint8_t *)malloc(stored_len);
        if (!stored)
            return NULL;
        if (fseek(archive->file, (long)entry->data_offset, SEEK_SET) != 0) {
            free(stored);
            return NULL;
        }
        if (fread(stored, 1, stored_len, archive->file) != stored_len) {
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
        if (!inflated)
            return NULL;

        // Extract to malloc'd buffer (caller-owned)
        size_t result_len = 0;
        uint8_t *result = rt_bytes_extract_raw(inflated, &result_len);
        if (!result)
            return NULL;
        *out_size = result_len;
        return result;
    }

    // Uncompressed — return stored data directly
    *out_size = (size_t)entry->data_size;
    return stored;
}

// ─── vpa_close ──────────────────────────────────────────────────────────────

/// @brief Close a VPA archive, freeing the TOC entries and closing any file handle.
void vpa_close(vpa_archive_t *archive) {
    if (!archive)
        return;

    if (archive->entries) {
        for (uint32_t i = 0; i < archive->count; i++)
            free(archive->entries[i].name);
        free(archive->entries);
    }

    if (archive->file)
        fclose(archive->file);

    free(archive);
}
