//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/io/rt_archive_read.c
// Purpose: ZIP central-directory parsing and entry extraction (the read side of
//          the archive runtime): parse_central_directory, find_entry, and
//          read_entry_data (stored + DEFLATE). Split out of rt_archive.c; shares
//          the ZIP types/constants via rt_archive_internal.h.
//
// Key invariants:
//   - Parsing validates offsets/sizes against the buffer length and rejects
//     encrypted / ZIP64 / malformed extra fields before extraction.
//   - read_entry_data verifies CRC-32 and the uncompressed size after inflate.
//
// Ownership/Lifetime:
//   - parse_central_directory fills the archive's entry array (owned by it);
//     read_entry_data returns a fresh Bytes object owned by the caller.
//
// Links: src/runtime/io/rt_archive.c (core/API/write), rt_archive_internal.h
//
//===----------------------------------------------------------------------===//

#include "rt_archive.h"
#include "rt_archive_internal.h"

#include "rt_bytes.h"
#include "rt_compress.h"
#include "rt_crc32.h"
#include "rt_internal.h"
#include "rt_object.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/// @brief Read a little-endian uint16 from `p` (file-local copy).
static inline uint16_t read_u16(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

/// @brief Read a little-endian uint32 from `p` (file-local copy).
static inline uint32_t read_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/// @brief Direct pointer to a Bytes GC object's buffer (file-local copy).
static inline uint8_t *bytes_data(void *obj) {
    return rt_bytes_data(obj);
}

// ZIP Parsing (for reading)
//=============================================================================

/// @brief Locate the End of Central Directory (EOCD) record within a ZIP buffer.
///
/// The EOCD is at the tail of every ZIP file, just before any optional
/// archive comment (max 65,535 bytes per spec). We scan backwards from
/// the end up to comment-max + EOCD-size for the 4-byte signature.
/// On match, `*eocd_offset` is set; the caller reads fields off it.
///
/// @param data        Pointer to the full ZIP byte buffer.
/// @param len         Length of `data`.
/// @param eocd_offset Out-parameter: offset of the EOCD signature.
/// @return True if EOCD found, false otherwise.
static bool find_eocd(const uint8_t *data, size_t len, size_t *eocd_offset) {
    if (len < ZIP_END_RECORD_SIZE)
        return false;

    // Search backwards for EOCD signature (handles comments)
    size_t max_comment = 65535;
    size_t search_len =
        len < (ZIP_END_RECORD_SIZE + max_comment) ? len : (ZIP_END_RECORD_SIZE + max_comment);

    for (size_t i = ZIP_END_RECORD_SIZE; i <= search_len; i++) {
        size_t offset = len - i;
        if (read_u32(data + offset) == ZIP_END_RECORD_SIG && offset <= len - ZIP_END_RECORD_SIZE) {
            uint16_t comment_len = read_u16(data + offset + 20);
            if (offset + ZIP_END_RECORD_SIZE + (size_t)comment_len != len)
                continue;
            *eocd_offset = offset;
            return true;
        }
    }
    return false;
}

/// @brief Parse the central directory and populate `ar->entries`.
///
/// Locates the EOCD (`find_eocd`), validates it isn't a multi-disk
/// archive (we don't support that), then walks each central-directory
/// header: copying out method, CRC, sizes, mod time, local-header
/// offset, and the entry name. Detects directories via trailing `/`.
/// Returns false (without trapping) if the central directory is
/// missing, malformed, multi-disk, or out of bounds — the caller's
/// trap message lets us distinguish "not a ZIP" from other errors.
///
/// @param ar Archive whose `data`/`data_len` is already populated.
/// @return True on successful parse, false on any structural problem.
bool parse_central_directory(rt_archive_t *ar) {
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
    if (disk_entries == UINT16_MAX || total_entries == UINT16_MAX || cd_size == UINT32_MAX ||
        cd_offset == UINT32_MAX)
        return false;

    // Validate central directory bounds
    if ((size_t)cd_offset > eocd_offset || (size_t)cd_size > eocd_offset - (size_t)cd_offset)
        return false;

    zip_entry_t *entries = NULL;
    if (total_entries > 0) {
        entries = (zip_entry_t *)calloc(total_entries, sizeof(zip_entry_t));
        if (!entries)
            return false;
    }

    // Parse each central directory entry
    const uint8_t *p = ar->data + cd_offset;
    const uint8_t *cd_end = p + cd_size;

    int parsed = 0;
    for (; parsed < total_entries; parsed++) {
        if ((size_t)(cd_end - p) < ZIP_CENTRAL_HEADER_SIZE) {
            archive_free_entry_array(entries, parsed);
            return false;
        }
        if (read_u32(p) != ZIP_CENTRAL_HEADER_SIG) {
            archive_free_entry_array(entries, parsed);
            return false;
        }

        uint16_t name_len = read_u16(p + 28);
        uint16_t extra_len = read_u16(p + 30);
        uint16_t comment_len = read_u16(p + 32);
        size_t record_len =
            ZIP_CENTRAL_HEADER_SIZE + (size_t)name_len + (size_t)extra_len + (size_t)comment_len;

        if ((size_t)(cd_end - p) < record_len) {
            archive_free_entry_array(entries, parsed);
            return false;
        }
        if (name_len == 0 || memchr(p + ZIP_CENTRAL_HEADER_SIZE, '\0', name_len) != NULL) {
            archive_free_entry_array(entries, parsed);
            return false;
        }

        zip_entry_t *e = &entries[parsed];
        e->version_needed = read_u16(p + 6);
        e->flags = read_u16(p + 8);
        e->method = read_u16(p + 10);
        e->mod_time = read_u16(p + 12);
        e->mod_date = read_u16(p + 14);
        e->crc32 = read_u32(p + 16);
        e->compressed_size = read_u32(p + 20);
        e->uncompressed_size = read_u32(p + 24);
        e->local_offset = read_u32(p + 42);
        if ((e->flags & (ZIP_GP_FLAG_ENCRYPTED | ZIP_GP_FLAG_DATA_DESCRIPTOR |
                         ZIP_GP_FLAG_STRONG_ENCRYPTION)) != 0 ||
            e->version_needed >= 45 || e->compressed_size == UINT32_MAX ||
            e->uncompressed_size == UINT32_MAX || e->local_offset == UINT32_MAX ||
            archive_extra_is_malformed_or_zip64(p + ZIP_CENTRAL_HEADER_SIZE + name_len,
                                                extra_len)) {
            archive_free_entry_array(entries, parsed);
            return false;
        }

        // Copy name
        e->name = (char *)malloc(name_len + 1);
        if (!e->name) {
            archive_free_entry_array(entries, parsed);
            return false;
        }
        memcpy(e->name, p + ZIP_CENTRAL_HEADER_SIZE, name_len);
        e->name[name_len] = '\0';
        for (int prior = 0; prior < parsed; ++prior) {
            if (entries[prior].name && strcmp(entries[prior].name, e->name) == 0) {
                archive_free_entry_array(entries, parsed + 1);
                return false;
            }
        }

        // Check if directory
        e->is_directory = (name_len > 0 && e->name[name_len - 1] == '/');

        p += record_len;
    }

    if (parsed != total_entries || p != cd_end) {
        archive_free_entry_array(entries, parsed);
        return false;
    }

    ar->entries = entries;
    ar->entry_count = parsed;
    return true;
}

/// @brief Linear-scan lookup of a central-directory entry by exact name.
///
/// Names are compared via `strcmp` — the caller must normalize ahead
/// of time (path separators, leading `./`, etc.). Returns NULL when no
/// matching entry exists. O(n); acceptable for typical ZIPs where n is
/// small. If we ever need fast lookup we'd add a hash side-table here.
///
/// @param ar   Read-mode archive with parsed `entries`.
/// @param name Normalized entry name.
/// @return Pointer to the entry, or NULL if not found.
zip_entry_t *find_entry(rt_archive_t *ar, const char *name) {
    if (!ar || !name)
        return NULL;
    for (int i = 0; i < ar->entry_count; i++) {
        if (ar->entries[i].name && strcmp(ar->entries[i].name, name) == 0)
            return &ar->entries[i];
    }
    return NULL;
}

/// @brief Materialize a single entry's payload into a fresh `rt_bytes`.
///
/// Re-validates the local file header signature (defends against
/// archives whose central-directory offsets point at garbage), skips
/// the variable-length name + extra fields, then either:
///   - copies stored data verbatim (method 0), or
///   - inflates DEFLATE-compressed data via `rt_compress_inflate`
///     (method 8).
/// Both branches verify the CRC32 against the header. The DEFLATE
/// branch additionally verifies the inflated size matches the
/// uncompressed-size field. Traps on any mismatch.
///
/// @param ar Read-mode archive.
/// @param e  Entry from `ar->entries`.
/// @return Owned `rt_bytes` containing the uncompressed payload.
void *read_entry_data(rt_archive_t *ar, zip_entry_t *e) {
    // Find local header
    size_t local_offset = (size_t)e->local_offset;
    if (local_offset > ar->data_len || ar->data_len - local_offset < ZIP_LOCAL_HEADER_SIZE) {
        rt_trap("Archive: corrupt local header offset");
        return NULL;
    }

    const uint8_t *local = ar->data + local_offset;
    if (read_u32(local) != ZIP_LOCAL_HEADER_SIG) {
        rt_trap("Archive: invalid local header signature");
        return NULL;
    }

    uint16_t local_flags = read_u16(local + 6);
    uint16_t local_method = read_u16(local + 8);
    uint32_t local_crc = read_u32(local + 14);
    uint32_t local_compressed_size = read_u32(local + 18);
    uint32_t local_uncompressed_size = read_u32(local + 22);
    uint16_t name_len = read_u16(local + 26);
    uint16_t extra_len = read_u16(local + 28);
    size_t header_total = ZIP_LOCAL_HEADER_SIZE + (size_t)name_len + (size_t)extra_len;
    if (local_offset > ar->data_len || ar->data_len - local_offset < header_total) {
        rt_trap("Archive: corrupt entry data");
        return NULL;
    }
    if (local_flags != e->flags || local_method != e->method ||
        (local_flags & ZIP_GP_FLAG_DATA_DESCRIPTOR) != 0 ||
        archive_extra_is_malformed_or_zip64(local + ZIP_LOCAL_HEADER_SIZE + name_len, extra_len)) {
        rt_trap("Archive: unsupported local header");
        return NULL;
    }
    if (local_crc != e->crc32 || local_compressed_size != e->compressed_size ||
        local_uncompressed_size != e->uncompressed_size) {
        rt_trap("Archive: local header metadata mismatch");
        return NULL;
    }
    size_t data_offset = local_offset + header_total;
    if ((size_t)e->compressed_size > ar->data_len - data_offset) {
        rt_trap("Archive: corrupt entry data");
        return NULL;
    }

    const uint8_t *compressed = ar->data + data_offset;

    // Handle uncompressed (stored) data
    if (e->method == ZIP_METHOD_STORED) {
        if (e->compressed_size != e->uncompressed_size) {
            rt_trap("Archive: stored entry size mismatch");
            return NULL;
        }
        // Verify CRC
        uint32_t crc = rt_crc32_compute(compressed, e->uncompressed_size);
        if (crc != e->crc32) {
            rt_trap("Archive: CRC mismatch");
            return NULL;
        }

        void *result = rt_bytes_new(e->uncompressed_size);
        if (!result)
            return NULL;
        memcpy(bytes_data(result), compressed, e->uncompressed_size);
        return result;
    }

    // Handle deflated data
    if (e->method == ZIP_METHOD_DEFLATE) {
        uint8_t *inflated = NULL;
        size_t inflated_len = 0;
        if (!rt_compress_inflate_raw(
                compressed, e->compressed_size, e->uncompressed_size, &inflated, &inflated_len)) {
            rt_trap("Archive: failed to inflate entry");
            return NULL;
        }

        // Verify CRC
        uint32_t crc = rt_crc32_compute(inflated, inflated_len);
        if (crc != e->crc32) {
            free(inflated);
            rt_trap("Archive: CRC mismatch");
            return NULL;
        }

        // Verify size
        if (inflated_len != e->uncompressed_size) {
            free(inflated);
            rt_trap("Archive: size mismatch");
            return NULL;
        }

        void *result = rt_bytes_new(e->uncompressed_size);
        if (!result) {
            free(inflated);
            return NULL;
        }
        memcpy(bytes_data(result), inflated, e->uncompressed_size);
        free(inflated);
        return result;
    }

    rt_trap("Archive: unsupported compression method");
    return NULL;
}

//=============================================================================
