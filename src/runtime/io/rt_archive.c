//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/io/rt_archive.c
// Purpose: Implements ZIP archive reading and writing for the Viper.IO.Archive
//          class. Follows the PKWARE APPNOTE specification, supporting stored
//          entries (method 0), DEFLATE-compressed entries (method 8) via
//          rt_compress, directory entries, and CRC32 validation.
//
// Key invariants:
//   - Stored entries (method 0) are written verbatim; DEFLATE entries use
//     rt_compress and are only used when they produce smaller output.
//   - CRC32 is computed and validated for every entry on both read and write.
//   - The central directory is always written at the end of the ZIP file.
//   - Directory entries have zero data length and a trailing '/' in the name.
//   - All functions are thread-safe; no global mutable state is used.
//
// Ownership/Lifetime:
//   - Entry name strings and data buffers returned to callers are fresh
//     rt_string / rt_bytes allocations owned by the caller.
//   - The archive object retains no references to extracted entry data.
//
// Links: src/runtime/io/rt_archive.h (public API),
//        src/runtime/io/rt_compress.h (DEFLATE compression used for method 8),
//        src/runtime/rt_crc32.h (CRC32 checksum utility)
//
//===----------------------------------------------------------------------===//

#include "rt_archive.h"
#include "rt_archive_internal.h"

#include "rt_box.h"
#include "rt_bytes.h"
#include "rt_compress.h"
#include "rt_crc32.h"
#include "rt_dir.h"
#include "rt_file_path.h"
#include "rt_internal.h"
#include "rt_io_class_ids.h"
#include "rt_map.h"
#include "rt_object.h"
#include "rt_seq.h"
#include "rt_string.h"

#ifdef _WIN32
#ifndef _CRT_RAND_S
#define _CRT_RAND_S
#endif
#endif

#include <errno.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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

void rt_trap_set_recovery(jmp_buf *buf);
void rt_trap_clear_recovery(void);
const char *rt_trap_get_error(void);

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
#define ZIP_GP_FLAG_ENCRYPTED 0x0001u
#define ZIP_GP_FLAG_DATA_DESCRIPTOR 0x0008u
#define ZIP_GP_FLAG_STRONG_ENCRYPTION 0x0040u
#define ZIP_EXTRA_ZIP64 0x0001u

//=============================================================================
// Internal Bytes Access
//=============================================================================

// Defined here (not inline in rt_archive_internal.h) so the bodies' calls to
// the runtime.def symbols rt_bytes_data/rt_bytes_len stay out of rtgen's
// header scan, which would otherwise mis-generate their dispatch signatures.

/// @brief Return a direct pointer to the raw byte buffer of a Bytes GC object.
static inline uint8_t *bytes_data(void *obj) {
    return rt_bytes_data(obj);
}

/// @brief Return the byte count of a Bytes GC object.
static inline int64_t bytes_len(void *obj) {
    return rt_bytes_len(obj);
}

/// @brief Drop a temporary GC object whose refcount has dropped to zero.
///
/// Used after we've materialized intermediate buffers (decompressed
/// data, throw-away byte arrays) and want to release them eagerly
/// instead of waiting for the next GC sweep.
void archive_release_temp_object(void *obj) {
    if (obj && rt_obj_release_check0(obj))
        rt_obj_free(obj);
}

void archive_save_trap_error(char *buffer, size_t buffer_size, const char *fallback) {
    const char *err = rt_trap_get_error();
    snprintf(buffer, buffer_size, "%s", err && err[0] ? err : fallback);
}

static void archive_add_with_temp_data(
    void *obj, rt_string name, void *data, const char *fallback) {
    void *volatile owned_data = data;
    jmp_buf recovery;
    rt_trap_set_recovery(&recovery);
    if (setjmp(recovery) != 0) {
        char saved_error[256];
        archive_save_trap_error(saved_error, sizeof(saved_error), fallback);
        rt_trap_clear_recovery();
        archive_release_temp_object((void *)owned_data);
        rt_trap(saved_error);
        return;
    }

    rt_archive_add(obj, name, (void *)owned_data);
    rt_trap_clear_recovery();
    archive_release_temp_object((void *)owned_data);
}

/// @brief Extract a UTF-8 C path from an `rt_string`, trapping on failure.
///
/// Converts a Viper string to a null-terminated C path via
/// `rt_file_path_from_vstr`. Traps with `context` if the path is
/// NULL, empty, or the conversion fails (e.g., invalid encoding).
///
/// @param path    Viper string containing the file path.
/// @param context Trap message to emit if the path is unusable.
/// @return Non-null, non-empty UTF-8 C string.
static const char *archive_require_path(rt_string path, const char *context) {
    const char *cpath = NULL;
    if (!rt_file_path_from_vstr((const ViperString *)path, &cpath) || !cpath || *cpath == '\0') {
        rt_trap(context);
        return "";
    }
    return cpath;
}

/// @brief Borrow a null-terminated view of an entry name from an `rt_string`.
///
/// Returns the raw UTF-8 pointer inside `name` as a C string without
/// copying. Returns NULL if `name` is empty, NULL, or contains an
/// embedded null byte (which would truncate the ZIP entry name).
///
/// @param name Viper string containing the entry name.
/// @return Borrowed C string pointer, or NULL if the name is invalid.
static const char *archive_entry_name_cstr(rt_string name) {
    const uint8_t *data = NULL;
    size_t len = rt_file_string_view((const ViperString *)name, &data);
    if (!data || len == 0)
        return NULL;
    if (memchr(data, '\0', len) != NULL)
        return NULL;
    return (const char *)data;
}


//=============================================================================
// ZIP Entry Structure
//=============================================================================

/// @brief Metadata for a single entry parsed from the ZIP central directory.
typedef struct zip_entry {
    char *name;                 ///< Entry name (heap-allocated, owned).
    uint32_t crc32;             ///< CRC-32 of the uncompressed data.
    uint32_t compressed_size;   ///< Stored size in the archive (bytes).
    uint32_t uncompressed_size; ///< Original uncompressed size (bytes).
    uint16_t method;            ///< Compression method: 0=stored, 8=DEFLATE.
    uint16_t flags;             ///< ZIP general-purpose bit flags.
    uint16_t version_needed;    ///< Minimum extractor version required.
    uint16_t mod_time;          ///< DOS-encoded modification time.
    uint16_t mod_date;          ///< DOS-encoded modification date.
    uint32_t local_offset;      ///< Byte offset of the local file header.
    bool is_directory;          ///< True when the entry name ends with '/'.
} zip_entry_t;

//=============================================================================
// Archive Structure
//=============================================================================

/// @brief Internal state for an open ZIP archive (read or write mode).
typedef struct rt_archive {
    rt_string path;   ///< File path string, or NULL for byte-backed archives.
    uint8_t *data;    ///< Full archive bytes (malloc'd copy or provided blob).
    size_t data_len;  ///< Length of `data` in bytes.
    bool owns_data;   ///< True when this object allocated `data` and must free it.
    bool is_writing;  ///< True when opened via `Archive.Create` (write mode).
    bool is_finished; ///< True after `Archive.Finish` has been called.

    // Read-side fields
    zip_entry_t *entries; ///< Array of parsed central-directory entries.
    int entry_count;      ///< Number of entries in `entries`.

    // Write-side fields
    int fd;                     ///< POSIX fd used for streaming writes (-1 if unused).
    uint8_t *write_buf;         ///< In-memory write accumulation buffer.
    size_t write_len;           ///< Current number of valid bytes in `write_buf`.
    size_t write_cap;           ///< Allocated capacity of `write_buf`.
    zip_entry_t *write_entries; ///< Metadata for each entry added so far.
    int write_entry_count;      ///< Number of entries in `write_entries`.
    int write_entry_cap;        ///< Allocated capacity of `write_entries`.
} rt_archive_t;

static rt_archive_t *archive_require(void *obj, const char *context) {
    if (!obj || rt_obj_class_id(obj) != RT_ARCHIVE_CLASS_ID) {
        rt_trap(context ? context : "Archive: invalid archive");
        return NULL;
    }
    return (rt_archive_t *)obj;
}

//=============================================================================
// Little-Endian Helpers
//=============================================================================

// ZIP integers are little-endian regardless of host byte order. These
// helpers do byte-level reads/writes so the parser/writer stays correct
// on big-endian hosts and avoids strict-aliasing pitfalls.

/// @brief Read a little-endian uint16 from `p` (no alignment required).
static inline uint16_t read_u16(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

/// @brief Read a little-endian uint32 from `p` (no alignment required).
static inline uint32_t read_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/// @brief Write `v` to `p` as little-endian uint16.
static inline void write_u16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
}

/// @brief Write `v` to `p` as little-endian uint32.
static inline void write_u32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}

static bool archive_extra_is_malformed_or_zip64(const uint8_t *extra, size_t extra_len) {
    size_t pos = 0;
    while (pos + 4 <= extra_len) {
        uint16_t header_id = read_u16(extra + pos);
        uint16_t data_size = read_u16(extra + pos + 2);
        pos += 4;
        if ((size_t)data_size > extra_len - pos)
            return true;
        if (header_id == ZIP_EXTRA_ZIP64)
            return true;
        pos += data_size;
    }
    return pos != extra_len;
}

//=============================================================================
// Archive Allocation
//=============================================================================

static void archive_finalize(void *obj);
static void archive_free_entries(rt_archive_t *ar);
static int archive_require_zip32_size(size_t size, const char *context);
static int archive_require_zip16_count(int count, const char *context);

/// @brief Allocate a zero-initialized archive object via the GC heap.
///
/// Hooks `archive_finalize` so that file paths, copied data, and entry
/// arrays are reclaimed when the GC drops the last reference. `fd` is
/// initialized to -1 (sentinel for "not open"). Traps on OOM.
///
/// @return Pointer to a fresh `rt_archive_t`, never NULL.
static rt_archive_t *archive_alloc(void) {
    size_t total = sizeof(rt_archive_t);
    rt_archive_t *ar = (rt_archive_t *)rt_obj_new_i64(RT_ARCHIVE_CLASS_ID, (int64_t)total);
    if (!ar) {
        rt_trap("Archive: memory allocation failed");
        return NULL;
    }
    memset(ar, 0, total);
    ar->fd = -1;
    rt_obj_set_finalizer(ar, archive_finalize);
    return ar;
}

static rt_archive_t *archive_alloc_or_free_data(uint8_t *data, const char *fallback) {
    jmp_buf recovery;
    rt_trap_set_recovery(&recovery);
    if (setjmp(recovery) != 0) {
        char saved_error[256];
        archive_save_trap_error(saved_error, sizeof(saved_error), fallback);
        rt_trap_clear_recovery();
        free(data);
        rt_trap(saved_error);
        return NULL;
    }

    rt_archive_t *ar = archive_alloc();
    if (!ar) {
        rt_trap_clear_recovery();
        free(data);
        return NULL;
    }
    rt_trap_clear_recovery();
    return ar;
}

static int archive_retain_path_or_release(rt_archive_t *ar, rt_string path, const char *fallback) {
    if (!ar)
        return 0;
    jmp_buf recovery;
    rt_trap_set_recovery(&recovery);
    if (setjmp(recovery) != 0) {
        char saved_error[256];
        archive_save_trap_error(saved_error, sizeof(saved_error), fallback);
        rt_trap_clear_recovery();
        archive_release_temp_object(ar);
        rt_trap(saved_error);
        return 0;
    }

    ar->path = rt_string_ref(path);
    rt_trap_clear_recovery();
    return 1;
}

/// @brief Free both read-side and write-side entry arrays.
///
/// Walks each entry releasing its `name` allocation, then frees the
/// containing array. Resets all counters to zero so the archive can
/// be re-parsed without leaking. Safe to call on a half-initialized
/// archive.
static void archive_free_entries(rt_archive_t *ar) {
    if (!ar)
        return;
    if (ar->entries && ar->entry_count > 0) {
        for (int i = 0; i < ar->entry_count; i++) {
#ifdef _MSC_VER
#pragma warning(suppress : 6001)
#endif
            if (ar->entries[i].name)
                free(ar->entries[i].name);
        }
    }
    if (ar->entries) {
        free(ar->entries);
        ar->entries = NULL;
        ar->entry_count = 0;
    }
    if (ar->write_entries && ar->write_entry_count > 0) {
        for (int i = 0; i < ar->write_entry_count; i++) {
#ifdef _MSC_VER
#pragma warning(suppress : 6001)
#endif
            if (ar->write_entries[i].name)
                free(ar->write_entries[i].name);
        }
    }
    if (ar->write_entries) {
        free(ar->write_entries);
        ar->write_entries = NULL;
        ar->write_entry_count = 0;
        ar->write_entry_cap = 0;
    }
}

/// @brief Free a partially-constructed entry array on parse failure.
///
/// Releases the `name` allocation for each of the first `count` entries,
/// then frees the array itself. Used as the cleanup path inside
/// `parse_central_directory` when an error is detected mid-parse.
///
/// @param entries Array of zip_entry_t to release (may be NULL).
/// @param count   Number of entries whose `name` fields are initialized.
static void archive_free_entry_array(zip_entry_t *entries, int count) {
    if (!entries)
        return;
    for (int i = 0; i < count; ++i)
#ifdef _MSC_VER
#pragma warning(suppress : 6001)
#endif
        if (entries[i].name)
            free(entries[i].name);
    free(entries);
}

/// @brief GC finalizer for archive objects.
///
/// Called when the GC drops the last reference. Releases the path
/// string, frees any owned data buffer, walks the entry arrays,
/// and disposes of any pending write buffer.
///
/// @param obj Archive pointer (may be NULL).
static void archive_finalize(void *obj) {
    rt_archive_t *ar = archive_require(obj, "Archive: invalid archive");
    if (!ar)
        return;

    if (ar->path) {
        rt_string_unref(ar->path);
        ar->path = NULL;
    }
    if (ar->owns_data && ar->data) {
        free(ar->data);
        ar->data = NULL;
    }
    archive_free_entries(ar);
    free(ar->write_buf);
    ar->write_buf = NULL;
    ar->write_len = 0;
    ar->write_cap = 0;
}

/// @brief Trap if `size` would overflow the 32-bit ZIP size fields.
///
/// ZIP versions <2.0 cap individual entries / archive offsets at 4GiB.
/// We don't yet implement ZIP64 (which uses 64-bit fields in extra
/// records), so any oversized payload is rejected up front with a
/// caller-provided message.
static int archive_require_zip32_size(size_t size, const char *context) {
    if (size > UINT32_MAX) {
        rt_trap(context);
        return 0;
    }
    return 1;
}

/// @brief Trap if `count` would overflow the 16-bit ZIP entry counter.
///
/// The pre-ZIP64 EOCD record stores total entry count in a uint16, so
/// archives with more than 65,535 entries are rejected.
static int archive_require_zip16_count(int count, const char *context) {
    if (count < 0 || count > UINT16_MAX) {
        rt_trap(context);
        return 0;
    }
    return 1;
}

//=============================================================================
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
static bool parse_central_directory(rt_archive_t *ar) {
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
static zip_entry_t *find_entry(rt_archive_t *ar, const char *name) {
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
static void *read_entry_data(rt_archive_t *ar, zip_entry_t *e) {
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
        // Create bytes with compressed data
        void *comp_bytes = rt_bytes_new(e->compressed_size);
        if (!comp_bytes)
            return NULL;
        memcpy(bytes_data(comp_bytes), compressed, e->compressed_size);

        // Inflate
        void *result = rt_compress_inflate_limit(comp_bytes, (int64_t)e->uncompressed_size);
        archive_release_temp_object(comp_bytes);
        if (!result) {
            rt_trap("Archive: failed to inflate entry");
            return NULL;
        }

        // Verify CRC
        uint32_t crc = rt_crc32_compute(bytes_data(result), bytes_len(result));
        if (crc != e->crc32) {
            archive_release_temp_object(result);
            rt_trap("Archive: CRC mismatch");
            return NULL;
        }

        // Verify size
        if (bytes_len(result) != e->uncompressed_size) {
            archive_release_temp_object(result);
            rt_trap("Archive: size mismatch");
            return NULL;
        }

        return result;
    }

    rt_trap("Archive: unsupported compression method");
    return NULL;
}

//=============================================================================
// Writing Helpers
//=============================================================================

/// @brief Grow the in-memory write buffer so `need` more bytes will fit.
///
/// Doubles the capacity (or jumps to `len + need + 4096` if doubling
/// is still too small) using `realloc`. Traps on OOM. Cheap when the
/// buffer is already large enough.
static int write_ensure(rt_archive_t *ar, size_t need) {
    if (!ar) {
        rt_trap("Archive: invalid archive");
        return 0;
    }
    if (need > SIZE_MAX - ar->write_len) {
        rt_trap("Archive: write buffer size overflow");
        return 0;
    }
    size_t required = ar->write_len + need;
    if (required <= ar->write_cap)
        return 1;

    size_t new_cap = ar->write_cap ? ar->write_cap : 4096;
    while (new_cap < required) {
        if (new_cap > SIZE_MAX / 2) {
            new_cap = required;
            break;
        }
        new_cap *= 2;
    }
    uint8_t *new_buf = (uint8_t *)realloc(ar->write_buf, new_cap);
    if (!new_buf) {
        rt_trap("Archive: memory allocation failed");
        return 0;
    }
    ar->write_buf = new_buf;
    ar->write_cap = new_cap;
    return 1;
}

/// @brief Append `len` bytes to the write buffer, growing it as needed.
///
/// Single-call wrapper around `write_ensure` + memcpy. Used as the
/// only path for appending bytes during archive construction so the
/// growth policy is centralized.
static int write_bytes(rt_archive_t *ar, const uint8_t *data, size_t len) {
    if (!write_ensure(ar, len))
        return 0;
    memcpy(ar->write_buf + ar->write_len, data, len);
    ar->write_len += len;
    return 1;
}

/// @brief Append a `zip_entry_t` to the writing-side entry table.
///
/// Verifies the new total stays within the 16-bit ZIP entry limit
/// (or traps with a ZIP64 message), then doubles the capacity of
/// `write_entries` if full. The supplied `*e` is copied by value —
/// the caller may reuse the source struct after the call.
static int add_write_entry(rt_archive_t *ar, zip_entry_t *e) {
    if (!ar || !e) {
        rt_trap("Archive: invalid archive entry");
        return 0;
    }
    if (!archive_require_zip16_count(ar->write_entry_count + 1,
                                     "Archive: ZIP64 archives are not supported"))
        return 0;
    if (ar->write_entry_count >= ar->write_entry_cap) {
        int new_cap = ar->write_entry_cap == 0 ? 16 : ar->write_entry_cap * 2;
        zip_entry_t *new_entries =
            (zip_entry_t *)realloc(ar->write_entries, new_cap * sizeof(zip_entry_t));
        if (!new_entries) {
            rt_trap("Archive: memory allocation failed");
            return 0;
        }
        if (new_cap > ar->write_entry_cap) {
            memset(new_entries + ar->write_entry_cap,
                   0,
                   (size_t)(new_cap - ar->write_entry_cap) * sizeof(zip_entry_t));
        }
        ar->write_entries = new_entries;
        ar->write_entry_cap = new_cap;
    }
    ar->write_entries[ar->write_entry_count++] = *e;
    return 1;
}

/// @brief Return 1 if the write-side entry table already contains an entry with the given name.
static int archive_write_has_entry(rt_archive_t *ar, const char *name) {
    if (!ar || !name)
        return 0;
    for (int i = 0; i < ar->write_entry_count; ++i) {
        if (ar->write_entries[i].name && strcmp(ar->write_entries[i].name, name) == 0)
            return 1;
    }
    return 0;
}

/// @brief Result enum for `normalize_name` — distinguishes invalid input
///        from out-of-memory so callers can pick the right trap message.
typedef enum { NAME_OK = 0, NAME_INVALID, NAME_OOM } name_result_t;

/// @brief Canonicalize an entry name to ZIP-safe POSIX-style form.
///
/// Rejects absolute paths (`/foo`, `\\foo`, `C:foo`) and any segment
/// equal to `..` (path-traversal guard for `Extract`). Drops `.` and
/// empty segments, collapses `\\` to `/`, and emits `/`-separated
/// output in `*out`. The caller takes ownership of `*out` (free with
/// `free()`).
///
/// @param name Caller-supplied entry name (UTF-8).
/// @param out  Out-parameter for the normalized buffer.
/// @return NAME_OK on success, NAME_INVALID for forbidden inputs,
///         NAME_OOM on allocation failure.
static name_result_t normalize_name(const char *name, char **out) {
    if (!name || !*name)
        return NAME_INVALID;

    // Reject absolute paths and drive letters.
    if (name[0] == '/' || name[0] == '\\')
        return NAME_INVALID;
    if (strlen(name) >= 2 && name[1] == ':')
        return NAME_INVALID;

    size_t len = strlen(name);
    char *result = (char *)malloc(len + 1);
    if (!result)
        return NAME_OOM;

    size_t j = 0;
    const char *p = name;
    while (*p) {
        while (*p == '/' || *p == '\\')
            ++p;
        if (!*p)
            break;

        const char *seg_start = p;
        while (*p && *p != '/' && *p != '\\')
            ++p;
        size_t seg_len = (size_t)(p - seg_start);
        if (seg_len == 0)
            continue;

        if (seg_len == 1 && seg_start[0] == '.')
            continue;
        if (seg_len == 2 && seg_start[0] == '.' && seg_start[1] == '.') {
            free(result);
            return NAME_INVALID;
        }
        if (memchr(seg_start, ':', seg_len)) {
            free(result);
            return NAME_INVALID;
        }

        if (j > 0)
            result[j++] = '/';
        memcpy(result + j, seg_start, seg_len);
        j += seg_len;
    }

    if (j == 0) {
        free(result);
        return NAME_INVALID;
    }

    result[j] = '\0';
    *out = result;
    return NAME_OK;
}

/// @brief Whether `name` ends in `/` or `\\` (i.e., looks like a directory).
///
/// Used to disambiguate `Has("foo")` (file) from `Has("foo/")`
/// (directory entry) since ZIP records the trailing slash.
static bool name_ends_with_sep(const char *name) {
    if (!name)
        return false;
    size_t len = strlen(name);
    if (len == 0)
        return false;
    return name[len - 1] == '/' || name[len - 1] == '\\';
}

/// @brief Ensure `name` ends with a `/`, reallocating in place if needed.
///
/// Takes ownership of `name` and either returns it unchanged (already
/// ends in `/`) or returns a fresh malloc'd buffer with the slash
/// appended. On allocation failure, frees `name` and traps — callers
/// don't need to clean up.
///
/// @param name Caller-owned name buffer.
/// @return The (possibly reallocated) name with trailing `/`.
static char *ensure_trailing_slash(char *name) {
    size_t len = strlen(name);
    if (len > 0 && name[len - 1] == '/')
        return name;
    if (len > SIZE_MAX - 2) {
        free(name);
        rt_trap("Archive: entry name too long");
        return NULL;
    }
    char *new_name = (char *)realloc(name, len + 2);
    if (!new_name) {
        free(name);
        rt_trap("Archive: memory allocation failed");
        return NULL;
    }
    new_name[len] = '/';
    new_name[len + 1] = '\0';
    return new_name;
}

/// @brief Insert a pre-boxed value into a map under a C-string key.
///
/// Wraps the C string in a const `rt_string` (no allocation), calls
/// `rt_map_set`, then releases the temporary key string. Convenience
/// for the `Info` implementation which builds a flat map of boxed
/// primitives.
///
/// @param map      Target `rt_map` object.
/// @param key_cstr Null-terminated key string (borrowed for the call).
/// @param boxed    Boxed primitive value (Map takes a reference).
static void archive_map_set_boxed(void *map, const char *key_cstr, void *boxed) {
    if (!map || !key_cstr)
        return;
    rt_string key = rt_const_cstr(key_cstr);
    rt_map_set(map, key, boxed);
    rt_string_unref(key);
}

/// @brief Produce DOS time/date words for the local-header timestamp.
///
/// Currently emits a fixed `2001-01-01 00:00:00` so that archives are
/// reproducible (byte-for-byte identical given the same inputs). If
/// per-file modification timestamps are ever needed, this is the spot
/// to switch to `time(NULL)` + the DOS encoding (date: bits 0-4 day,
/// 5-8 month, 9-15 year-since-1980; time: bits 0-4 sec/2, 5-10 min,
/// 11-15 hour).
static void get_dos_time(uint16_t *time, uint16_t *date) {
    // Use a fixed time for reproducibility (could use actual time)
    *time = 0;                        // 00:00:00
    *date = (21 << 9) | (1 << 5) | 1; // 2001-01-01
}

/// @brief Compute a proleptic Gregorian day number from a civil (year, month, day) triple.
static int64_t archive_days_from_civil(int year, unsigned month, unsigned day) {
    year -= month <= 2;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yoe = (unsigned)(year - era * 400);
    const unsigned mp = month > 2 ? month - 3 : month + 9;
    const unsigned doy = (153 * mp + 2) / 5 + day - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return (int64_t)era * 146097 + (int64_t)doe - 719468;
}

/// @brief Convert a DOS (FAT) time+date pair to a Unix timestamp (seconds since epoch).
static int64_t archive_dos_datetime_to_unix(uint16_t dos_time, uint16_t dos_date) {
    int year = ((dos_date >> 9) & 0x7F) + 1980;
    unsigned month = (unsigned)((dos_date >> 5) & 0xF);
    unsigned day = (unsigned)(dos_date & 0x1F);
    unsigned hour = (unsigned)((dos_time >> 11) & 0x1F);
    unsigned minute = (unsigned)((dos_time >> 5) & 0x3F);
    unsigned second = (unsigned)((dos_time & 0x1F) * 2);

    static const unsigned month_days[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int leap = (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
    unsigned max_day = month >= 1 && month <= 12 ? month_days[month - 1] : 0;
    if (month == 2 && leap)
        max_day = 29;
    if (month < 1 || month > 12 || day < 1 || day > max_day || hour > 23 || minute > 59 ||
        second > 59)
        return 0;

    int64_t days = archive_days_from_civil(year, month, day);
    return days * 86400 + (int64_t)hour * 3600 + (int64_t)minute * 60 + (int64_t)second;
}

//=============================================================================
// Public API - Creation/Opening
//=============================================================================

/// @brief `Archive.Open(path)` — open an existing ZIP file for reading.
///
/// Reads the entire file into memory (no streaming yet) and parses the
/// central directory. The path is captured (refcount-bumped) so the
/// archive object survives the caller's string. Traps on:
///   - empty/NULL path
///   - file-not-found / permission errors
///   - unreasonably large files (> SIZE_MAX)
///   - invalid ZIP structure
///
/// @param path UTF-8 file path.
/// @return Owned `Archive` handle in read mode.
void *rt_archive_open(rt_string path) {
    const char *cpath = archive_require_path(path, "Archive: invalid path");
    if (!cpath || *cpath == '\0')
        return NULL;

    // Open and read file
#ifdef _WIN32
    HANDLE h = archive_open_win_path(cpath, GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING);
    if (h == INVALID_HANDLE_VALUE) {
        rt_trap("Archive: file not found");
        return NULL;
    }

    LARGE_INTEGER size;
    if (!GetFileSizeEx(h, &size)) {
        CloseHandle(h);
        rt_trap("Archive: failed to get file size");
        return NULL;
    }
    BY_HANDLE_FILE_INFORMATION info;
    if (!GetFileInformationByHandle(h, &info) ||
        (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        CloseHandle(h);
        rt_trap("Archive: path is not a regular file");
        return NULL;
    }

    if (size.QuadPart < 0 || (uint64_t)size.QuadPart > SIZE_MAX) {
        CloseHandle(h);
        rt_trap("Archive: file too large");
        return NULL;
    }

    size_t data_len = (size_t)size.QuadPart;
    uint8_t *data = data_len > 0 ? (uint8_t *)malloc(data_len) : NULL;
    if (data_len > 0 && !data) {
        CloseHandle(h);
        rt_trap("Archive: memory allocation failed");
        return NULL;
    }
    if (!archive_read_exact_win_or_free(h, data, data_len, "Archive: failed to read file"))
        return NULL;
#else
    int fd = archive_open_posix(cpath, O_RDONLY, 0);
    if (fd < 0) {
        rt_trap("Archive: file not found");
        return NULL;
    }

    struct stat st;
    if (fstat(fd, &st) != 0) {
        close(fd);
        rt_trap("Archive: failed to get file size");
        return NULL;
    }
    if (!S_ISREG(st.st_mode)) {
        close(fd);
        rt_trap("Archive: path is not a regular file");
        return NULL;
    }

    if (st.st_size < 0 || (uint64_t)st.st_size > (uint64_t)SIZE_MAX) {
        close(fd);
        rt_trap("Archive: file too large");
        return NULL;
    }

    size_t data_len = (size_t)st.st_size;
    uint8_t *data = data_len > 0 ? (uint8_t *)malloc(data_len) : NULL;
    if (data_len > 0 && !data) {
        close(fd);
        rt_trap("Archive: memory allocation failed");
        return NULL;
    }
    if (!archive_read_exact_posix_or_free(fd, data, data_len, "Archive: failed to read file"))
        return NULL;
#endif

    rt_archive_t *ar = archive_alloc_or_free_data(data, "Archive: memory allocation failed");
    if (!ar)
        return NULL;
    ar->data = data;
    ar->data_len = data_len;
    ar->owns_data = true;
    ar->is_writing = false;
    if (!archive_retain_path_or_release(ar, path, "Archive: path retain failed"))
        return NULL;

    if (!parse_central_directory(ar)) {
        archive_release_temp_object(ar);
        rt_trap("Archive: not a valid ZIP file");
        return NULL;
    }

    return ar;
}

/// @brief `Archive.Create(path)` — start a new ZIP file for writing.
///
/// Probes that the destination directory is writable without truncating
/// an existing archive. All entry data is buffered in `write_buf` until
/// `Finish`, which atomically replaces the destination. Traps on invalid
/// path or allocation failure.
///
/// @param path UTF-8 destination path.
/// @return Owned `Archive` handle in write mode.
void *rt_archive_create(rt_string path) {
    const char *cpath = archive_require_path(path, "Archive: invalid path");
    if (!cpath || *cpath == '\0')
        return NULL;

#ifdef _WIN32
    char *probe_tmp = NULL;
    HANDLE h = INVALID_HANDLE_VALUE;
    for (unsigned attempt = 0; attempt < 128; ++attempt) {
        probe_tmp = archive_make_temp_path(cpath, attempt);
        if (!probe_tmp) {
            rt_trap("Archive: memory allocation failed");
            return NULL;
        }
        h = archive_open_win_path(probe_tmp, GENERIC_WRITE, 0, CREATE_NEW);
        if (h != INVALID_HANDLE_VALUE)
            break;
        free(probe_tmp);
        probe_tmp = NULL;
        DWORD err = GetLastError();
        if (err != ERROR_FILE_EXISTS && err != ERROR_ALREADY_EXISTS)
            break;
    }
    if (h == INVALID_HANDLE_VALUE || !probe_tmp) {
        free(probe_tmp);
        rt_trap("Archive: failed to create file");
        return NULL;
    }
    CloseHandle(h);
    archive_unlink_utf8(probe_tmp);
    free(probe_tmp);
#else
    char *probe_tmp = NULL;
    int fd = -1;
    for (unsigned attempt = 0; attempt < 128; ++attempt) {
        probe_tmp = archive_make_temp_path(cpath, attempt);
        if (!probe_tmp) {
            rt_trap("Archive: memory allocation failed");
            return NULL;
        }
        fd = archive_open_posix(probe_tmp, O_WRONLY | O_CREAT | O_EXCL, 0644);
        if (fd >= 0)
            break;
        int err = errno;
        free(probe_tmp);
        probe_tmp = NULL;
        if (err != EEXIST)
            break;
    }
    if (fd < 0 || !probe_tmp) {
        free(probe_tmp);
        rt_trap("Archive: failed to create file");
        return NULL;
    }
    close(fd);
    unlink(probe_tmp);
    free(probe_tmp);
#endif

    rt_archive_t *ar = archive_alloc();
    if (!ar)
        return NULL;
    if (!archive_retain_path_or_release(ar, path, "Archive: path retain failed"))
        return NULL;
    ar->is_writing = true;
    ar->write_cap = 4096;
    ar->write_buf = (uint8_t *)malloc(ar->write_cap);
    if (!ar->write_buf) {
        archive_release_temp_object(ar);
        rt_trap("Archive: memory allocation failed");
        return NULL;
    }

    return ar;
}

/// @brief `Archive.FromBytes(data)` — open an in-memory ZIP for reading.
///
/// Used when ZIP data lives in a Bytes buffer (e.g., downloaded over
/// HTTP, embedded as an asset). The bytes are *copied* internally so
/// the archive owns its own buffer — the source can be freed
/// immediately after the call returns. Traps on NULL data, OOM,
/// or invalid ZIP structure.
///
/// @param data Source `rt_bytes` containing a complete ZIP archive.
/// @return Owned `Archive` handle in read mode (no path).
void *rt_archive_from_bytes(void *data) {
    if (!data) {
        rt_trap("Archive: NULL data");
        return NULL;
    }

    int64_t len = bytes_len(data);
    if (len < 0) {
        rt_trap("Archive: invalid data length");
        return NULL;
    }
    uint8_t *src = bytes_data(data);
    if (len > 0 && !src) {
        rt_trap("Archive: invalid data");
        return NULL;
    }

    // Copy the data
    uint8_t *copy = NULL;
    if (len > 0) {
        copy = (uint8_t *)malloc((size_t)len);
    }
    if (len > 0 && !copy) {
        rt_trap("Archive: memory allocation failed");
        return NULL;
    }
    if (len > 0)
        memcpy(copy, src, (size_t)len);

    rt_archive_t *ar = archive_alloc_or_free_data(copy, "Archive: memory allocation failed");
    if (!ar)
        return NULL;
    ar->path = NULL;
    ar->data = copy;
    ar->data_len = (size_t)len;
    ar->owns_data = true;
    ar->is_writing = false;

    if (!parse_central_directory(ar)) {
        archive_release_temp_object(ar);
        rt_trap("Archive: not a valid ZIP archive");
        return NULL;
    }

    return ar;
}

//=============================================================================
// Properties
//=============================================================================

/// @brief `Archive.Path` — return the path the archive was opened from.
///
/// Returns the empty string for `FromBytes`-constructed archives
/// (which have no associated path) or for a NULL receiver.
///
/// @param obj Archive handle.
/// @return Owned `rt_string` containing the path, or `""`.
rt_string rt_archive_path(void *obj) {
    rt_archive_t *ar = archive_require(obj, "Archive: invalid archive");
    if (!ar)
        return rt_str_empty();
    return ar->path ? rt_string_ref(ar->path) : rt_str_empty();
}

/// @brief `Archive.Count` — number of entries (files + directories).
///
/// Returns the size of `entries` for read-mode archives or
/// `write_entries` for write-mode. Both increase as entries are
/// added; neither shrinks.
///
/// @param obj Archive handle.
/// @return Entry count, or 0 for NULL.
int64_t rt_archive_count(void *obj) {
    rt_archive_t *ar = archive_require(obj, "Archive: invalid archive");
    if (!ar)
        return 0;
    if (ar->is_writing)
        return ar->write_entry_count;
    return ar->entry_count;
}

/// @brief `Archive.Names` — return all entry names as a `seq<str>`.
///
/// Walks the appropriate entry table (read- or write-mode) and pushes
/// each name as a `const_cstr`-flagged string (cheap, doesn't copy).
/// Always returns a fresh seq, even for NULL or empty archives, so the
/// caller can safely iterate.
///
/// @param obj Archive handle (may be NULL).
/// @return Owned seq of entry names in archive order.
void *rt_archive_names(void *obj) {
    rt_archive_t *ar = archive_require(obj, "Archive: invalid archive");
    void *seq = rt_seq_new();
    if (!seq)
        return NULL;
    rt_seq_set_owns_elements(seq, 1);

    if (!ar)
        return seq;

    if (ar->is_writing) {
        for (int i = 0; i < ar->write_entry_count; i++) {
            rt_string name = rt_const_cstr(ar->write_entries[i].name);
            rt_seq_push(seq, name);
            rt_string_unref(name);
        }
    } else {
        for (int i = 0; i < ar->entry_count; i++) {
            rt_string name = rt_const_cstr(ar->entries[i].name);
            rt_seq_push(seq, name);
            rt_string_unref(name);
        }
    }

    return seq;
}

//=============================================================================
// Reading Methods
//=============================================================================

/// @brief `Archive.Has(name)` — test whether an entry exists.
///
/// Normalizes the name (path-traversal guard, separator collapse).
/// If the original name ended with a `/` we add the trailing slash
/// back after normalization so callers can distinguish file vs
/// directory entries. Returns 0 for write-mode archives (which have
/// no readable entries yet) or any normalization failure.
///
/// @param obj  Archive handle.
/// @param name Entry name to look up.
/// @return 1 if found, 0 otherwise.
int8_t rt_archive_has(void *obj, rt_string name) {
    rt_archive_t *ar = archive_require(obj, "Archive: invalid archive");
    if (!ar || ar->is_writing)
        return 0;

    const char *cname = archive_entry_name_cstr(name);
    if (!cname)
        return 0;
    const bool wants_dir = name_ends_with_sep(cname);

    char *norm_name = NULL;
    name_result_t norm_res = normalize_name(cname, &norm_name);
    if (norm_res == NAME_OOM) {
        rt_trap("Archive: memory allocation failed");
        return 0;
    }
    if (norm_res == NAME_INVALID)
        return 0;
    if (wants_dir) {
        norm_name = ensure_trailing_slash(norm_name);
        if (!norm_name)
            return 0;
    }

    int8_t found = find_entry(ar, norm_name) != NULL ? 1 : 0;
    free(norm_name);
    return found;
}

/// @brief `Archive.Read(name)` — extract entry contents as Bytes.
///
/// Normalizes the name then delegates to `read_entry_data`, which
/// handles both stored and DEFLATE methods. Traps on a missing entry,
/// invalid name, write-only archive, or any structural inconsistency.
///
/// @param obj  Archive handle.
/// @param name Entry name.
/// @return Owned `rt_bytes` with the uncompressed payload.
void *rt_archive_read(void *obj, rt_string name) {
    rt_archive_t *ar = archive_require(obj, "Archive: invalid archive");
    if (!ar) {
        rt_trap("Archive: NULL archive");
        return NULL;
    }
    if (ar->is_writing) {
        rt_trap("Archive: cannot read from write-only archive");
        return NULL;
    }

    const char *cname = archive_entry_name_cstr(name);
    if (!cname) {
        rt_trap("Archive: NULL entry name");
        return NULL;
    }
    const bool wants_dir = name_ends_with_sep(cname);

    char *norm_name = NULL;
    name_result_t norm_res = normalize_name(cname, &norm_name);
    if (norm_res == NAME_INVALID) {
        rt_trap("Archive: invalid entry name");
        return NULL;
    }
    if (norm_res == NAME_OOM) {
        rt_trap("Archive: memory allocation failed");
        return NULL;
    }
    if (wants_dir) {
        norm_name = ensure_trailing_slash(norm_name);
        if (!norm_name)
            return NULL;
    }

    zip_entry_t *e = find_entry(ar, norm_name);
    free(norm_name);
    if (!e) {
        rt_trap("Archive: entry not found");
        return NULL;
    }

    return read_entry_data(ar, e);
}

/// @brief `Archive.ReadStr(name)` — extract entry contents as a string.
///
/// Convenience wrapper that reads the entry as Bytes then runs the
/// bytes-to-string converter (which validates UTF-8). Traps on the
/// same conditions as `Read`, plus any decode failure.
///
/// @param obj  Archive handle.
/// @param name Entry name.
/// @return Owned `rt_string` containing the entry text.
rt_string rt_archive_read_str(void *obj, rt_string name) {
    void *data = rt_archive_read(obj, name);
    if (!data)
        return rt_str_empty();
    rt_string result = rt_bytes_to_str(data);
    archive_release_temp_object(data);
    return result;
}

/// @brief `Archive.Extract(name, destPath)` — write a single entry to disk.
///
/// Reads the entry into memory then writes it to `destPath`. The
/// destination directory must already exist (we do not auto-create
/// parents — that's `ExtractAll`'s job). Traps on any I/O error.
/// The temporary `rt_bytes` is released eagerly to avoid retaining
/// large entry payloads beyond this call.
///
/// @param obj       Archive handle.
/// @param name      Entry to extract.
/// @param dest_path UTF-8 destination file path.
void rt_archive_extract(void *obj, rt_string name, rt_string dest_path) {
    const char *cpath = archive_require_path(dest_path, "Archive: invalid destination path");
    if (!cpath || *cpath == '\0')
        return;

    void *data = rt_archive_read(obj, name);
    if (!data)
        return;
    archive_write_bytes_to_path(cpath, data);
    archive_release_temp_object(data);
}

/// @brief `Archive.ExtractAll(destDir)` — explode the archive onto disk.
///
/// Creates `destDir` (and any missing parents), then iterates every
/// entry: directory entries are created via `rt_dir_make_all`; file
/// entries have their parent directory created on the fly (so deep
/// hierarchies don't need a separate directory entry) and are written
/// with `archive_write_bytes_to_path`. Forward slashes in entry names
/// are translated to the platform path separator. Path-traversal
/// attacks are blocked by `normalize_name` rejecting `..` segments.
///
/// @param obj      Archive handle.
/// @param dest_dir UTF-8 destination directory.
void rt_archive_extract_all(void *obj, rt_string dest_dir) {
    rt_archive_t *ar = archive_require(obj, "Archive: invalid archive");
    if (!ar) {
        rt_trap("Archive: NULL archive");
        return;
    }
    if (ar->is_writing) {
        rt_trap("Archive: cannot extract from write-only archive");
        return;
    }

    const char *cdir = archive_require_path(dest_dir, "Archive: invalid destination directory");
    if (!cdir || *cdir == '\0')
        return;

    size_t dir_len = strlen(cdir);
    size_t root_len = archive_trim_trailing_seps(cdir, dir_len);
    archive_reject_symlink_components(cdir, root_len, 1);
    rt_dir_make_all(dest_dir);
    archive_reject_symlink_components(cdir, root_len, 1);

#if !defined(_WIN32) && !defined(__viperdos__)
    int root_fd = archive_open_root_dir_posix(cdir);
    if (root_fd < 0)
        return;
    archive_reject_symlink_components(cdir, root_len, 1);

    for (int i = 0; i < ar->entry_count; i++) {
        zip_entry_t *e = &ar->entries[i];
        char *norm_name = NULL;
        name_result_t norm_res = normalize_name(e->name, &norm_name);
        if (norm_res == NAME_INVALID) {
            rt_trap("Archive: invalid entry name");
            close(root_fd);
            return;
        }
        if (norm_res == NAME_OOM) {
            rt_trap("Archive: memory allocation failed");
            close(root_fd);
            return;
        }

        if (e->is_directory) {
            archive_make_dirs_posix_at(root_fd, norm_name);
        } else {
            char *leaf = NULL;
            int parent_fd = archive_open_parent_for_file_posix(root_fd, norm_name, &leaf);
            void *data = read_entry_data(ar, e);
            if (!data) {
                if (parent_fd >= 0)
                    close(parent_fd);
                free(leaf);
                free(norm_name);
                close(root_fd);
                return;
            }
            archive_write_bytes_to_dirfd_posix(parent_fd, leaf, data);
            archive_release_temp_object(data);
            close(parent_fd);
            free(leaf);
        }
        free(norm_name);
    }

    close(root_fd);
    return;
#else
    for (int i = 0; i < ar->entry_count; i++) {
        zip_entry_t *e = &ar->entries[i];

        char *norm_name = NULL;
        name_result_t norm_res = normalize_name(e->name, &norm_name);
        if (norm_res == NAME_INVALID) {
            rt_trap("Archive: invalid entry name");
            return;
        }
        if (norm_res == NAME_OOM) {
            rt_trap("Archive: memory allocation failed");
            return;
        }

        // Build full path
        size_t name_len = strlen(norm_name);
        if (dir_len > SIZE_MAX - 1 - name_len) {
            free(norm_name);
            rt_trap("Archive: destination path too long");
            return;
        }
        size_t path_len = dir_len + 1 + name_len;
        char *full_path = (char *)malloc(path_len + 1);
        if (!full_path) {
            free(norm_name);
            rt_trap("Archive: memory allocation failed");
            return;
        }

        memcpy(full_path, cdir, dir_len);
        full_path[dir_len] = PATH_SEP;
        memcpy(full_path + dir_len + 1, norm_name, name_len);
        full_path[path_len] = '\0';

        // Convert forward slashes to platform separator
        for (size_t j = dir_len + 1; j < path_len; j++) {
            if (full_path[j] == '/')
                full_path[j] = PATH_SEP;
        }

        if (e->is_directory) {
            // Create directory
            archive_reject_symlink_components(full_path, root_len, 1);
            rt_string dir_path = rt_const_cstr(full_path);
            rt_dir_make_all(dir_path);
            rt_string_unref(dir_path);
            archive_reject_symlink_components(full_path, root_len, 1);
        } else {
            // Create parent directory
            char *last_sep = strrchr(full_path, PATH_SEP);
            if (last_sep && last_sep > full_path + dir_len) {
                *last_sep = '\0';
                archive_reject_symlink_components(full_path, root_len, 1);
                rt_string parent = rt_const_cstr(full_path);
                rt_dir_make_all(parent);
                rt_string_unref(parent);
                archive_reject_symlink_components(full_path, root_len, 1);
                *last_sep = PATH_SEP;
            }

            archive_reject_symlink_components(full_path, root_len, 0);
            void *data = read_entry_data(ar, e);
            if (!data) {
                free(full_path);
                free(norm_name);
                return;
            }
            archive_write_bytes_to_path(full_path, data);
            archive_release_temp_object(data);
        }

        free(full_path);
        free(norm_name);
    }
#endif
}

/// @brief `Archive.Info(name)` — return a metadata map for one entry.
///
/// The map keys are `size`, `compressedSize`, `crc`, `method`,
/// `modifiedTime`, `isDirectory`, and `isDir` (alias). Values are
/// boxed primitives so they survive map storage. `modifiedTime` is
/// a Unix timestamp computed from the DOS date/time fields with a
/// simple year/month/day arithmetic — accurate to the second granularity
/// DOS can represent (2-second steps), but does not honour leap years
/// or DST.
///
/// @param obj  Archive handle.
/// @param name Entry name.
/// @return Owned `rt_map` of metadata.
void *rt_archive_info(void *obj, rt_string name) {
    rt_archive_t *ar = archive_require(obj, "Archive: invalid archive");
    if (!ar) {
        rt_trap("Archive: NULL archive");
        return NULL;
    }
    if (ar->is_writing) {
        rt_trap("Archive: cannot get info from write-only archive");
        return NULL;
    }

    const char *cname = archive_entry_name_cstr(name);
    if (!cname) {
        rt_trap("Archive: NULL entry name");
        return NULL;
    }
    const bool wants_dir = name_ends_with_sep(cname);

    char *norm_name = NULL;
    name_result_t norm_res = normalize_name(cname, &norm_name);
    if (norm_res == NAME_INVALID) {
        rt_trap("Archive: invalid entry name");
        return NULL;
    }
    if (norm_res == NAME_OOM) {
        rt_trap("Archive: memory allocation failed");
        return NULL;
    }
    if (wants_dir) {
        norm_name = ensure_trailing_slash(norm_name);
        if (!norm_name)
            return NULL;
    }

    zip_entry_t *e = find_entry(ar, norm_name);
    free(norm_name);
    if (!e) {
        rt_trap("Archive: entry not found");
        return NULL;
    }

    void *map = rt_map_new();
    if (!map)
        return NULL;
    void *boxed;

    // Add size
    boxed = rt_box_i64(e->uncompressed_size);
    archive_map_set_boxed(map, "size", boxed);
    if (boxed && rt_obj_release_check0(boxed))
        rt_obj_free(boxed);

    // Add compressed size
    boxed = rt_box_i64(e->compressed_size);
    archive_map_set_boxed(map, "compressedSize", boxed);
    if (boxed && rt_obj_release_check0(boxed))
        rt_obj_free(boxed);

    boxed = rt_box_i64((int64_t)e->crc32);
    archive_map_set_boxed(map, "crc", boxed);
    if (boxed && rt_obj_release_check0(boxed))
        rt_obj_free(boxed);

    boxed = rt_box_i64((int64_t)e->method);
    archive_map_set_boxed(map, "method", boxed);
    if (boxed && rt_obj_release_check0(boxed))
        rt_obj_free(boxed);

    int64_t timestamp = archive_dos_datetime_to_unix(e->mod_time, e->mod_date);
    boxed = rt_box_i64(timestamp);
    archive_map_set_boxed(map, "modifiedTime", boxed);
    if (boxed && rt_obj_release_check0(boxed))
        rt_obj_free(boxed);

    // Add isDirectory
    boxed = rt_box_i1(e->is_directory ? 1 : 0);
    archive_map_set_boxed(map, "isDirectory", boxed);
    archive_map_set_boxed(map, "isDir", boxed);
    if (boxed && rt_obj_release_check0(boxed))
        rt_obj_free(boxed);

    return map;
}

//=============================================================================
// Writing Methods
//=============================================================================

/// @brief `Archive.Add(name, data)` — append a file entry from a Bytes buffer.
///
/// CRC32 is computed over the raw payload. For payloads >64 bytes we
/// trial-deflate via `rt_compress_deflate` and pick whichever is
/// smaller (stored vs deflate) — small inputs are always stored to
/// avoid pathological cases where DEFLATE adds overhead. Local file
/// header + name + payload are appended to `write_buf`; a matching
/// `zip_entry_t` is recorded so `Finish` can emit the central directory.
/// Traps if the archive is read-only, already finished, or the name
/// is invalid.
///
/// @param obj  Archive handle (write mode).
/// @param name Entry name (will be normalized).
/// @param data Source `rt_bytes`.
void rt_archive_add(void *obj, rt_string name, void *data) {
    rt_archive_t *ar = archive_require(obj, "Archive: invalid archive");
    if (!ar) {
        rt_trap("Archive: NULL archive");
        return;
    }
    if (!ar->is_writing) {
        rt_trap("Archive: cannot add to read-only archive");
        return;
    }
    if (ar->is_finished) {
        rt_trap("Archive: archive already finished");
        return;
    }
    if (!data) {
        rt_trap("Archive: NULL data");
        return;
    }

    const char *cname = archive_entry_name_cstr(name);
    if (!cname || *cname == '\0') {
        rt_trap("Archive: invalid entry name");
        return;
    }

    char *norm_name = NULL;
    name_result_t norm_res = normalize_name(cname, &norm_name);
    if (norm_res == NAME_INVALID) {
        rt_trap("Archive: invalid entry name");
        return;
    }
    if (norm_res == NAME_OOM) {
        rt_trap("Archive: memory allocation failed");
        return;
    }
    if (archive_write_has_entry(ar, norm_name)) {
        free(norm_name);
        rt_trap("Archive: duplicate entry name");
        return;
    }

    int64_t raw_len_i64 = bytes_len(data);
    if (raw_len_i64 < 0) {
        free(norm_name);
        rt_trap("Archive: invalid data length");
        return;
    }
    uint8_t *raw_data = bytes_data(data);
    size_t raw_len = (size_t)raw_len_i64;
    if (raw_len > 0 && !raw_data) {
        free(norm_name);
        rt_trap("Archive: invalid data");
        return;
    }
    if (raw_len > UINT32_MAX) {
        free(norm_name);
        rt_trap("Archive: ZIP64 entries are not supported");
        return;
    }

    // Compute CRC
    uint32_t crc = rt_crc32_compute(raw_data, raw_len);

    // Decide whether to compress
    void *compressed = NULL;
    uint16_t method = ZIP_METHOD_STORED;
    const uint8_t *write_data = raw_data;
    size_t write_len = raw_len;

    if (raw_len > 64) {
        void *volatile compressed_owner = NULL;
        char *volatile norm_name_owner = norm_name;
        jmp_buf recovery;
        rt_trap_set_recovery(&recovery);
        if (setjmp(recovery) != 0) {
            char saved_error[256];
            archive_save_trap_error(saved_error, sizeof(saved_error), "Archive: failed to compress entry");
            rt_trap_clear_recovery();
            archive_release_temp_object((void *)compressed_owner);
            free((char *)norm_name_owner);
            rt_trap(saved_error);
            return;
        }

        compressed = rt_compress_deflate(data);
        compressed_owner = compressed;
        if (!compressed) {
            rt_trap_clear_recovery();
            free(norm_name);
            rt_trap("Archive: failed to compress entry");
            return;
        }
        int64_t comp_len_i64 = bytes_len(compressed);
        if (comp_len_i64 < 0 || (uint64_t)comp_len_i64 > (uint64_t)SIZE_MAX) {
            rt_trap("Archive: invalid compressed data length");
            return;
        }
        uint8_t *comp_data = bytes_data(compressed);
        if (comp_len_i64 > 0 && !comp_data) {
            rt_trap("Archive: invalid compressed data");
            return;
        }
        size_t comp_len = (size_t)comp_len_i64;
        if (comp_len < raw_len) {
            method = ZIP_METHOD_DEFLATE;
            write_data = comp_data;
            write_len = comp_len;
        }
        rt_trap_clear_recovery();
    }
    if (write_len > UINT32_MAX) {
        free(norm_name);
        archive_release_temp_object(compressed);
        rt_trap("Archive: ZIP64 entries are not supported");
        return;
    }
    if (ar->write_len > UINT32_MAX) {
        free(norm_name);
        archive_release_temp_object(compressed);
        rt_trap("Archive: ZIP64 archives are not supported");
        return;
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
    if (name_len > UINT16_MAX) {
        free(norm_name);
        archive_release_temp_object(compressed);
        rt_trap("Archive: entry name too long");
        return;
    }
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

    size_t write_start = ar->write_len;
    if (!write_bytes(ar, local_header, ZIP_LOCAL_HEADER_SIZE) ||
        !write_bytes(ar, (const uint8_t *)norm_name, name_len) ||
        !write_bytes(ar, write_data, write_len)) {
        ar->write_len = write_start;
        free(norm_name);
        archive_release_temp_object(compressed);
        return;
    }

    if (!add_write_entry(ar, &e)) {
        ar->write_len = write_start;
        free(norm_name);
        archive_release_temp_object(compressed);
        return;
    }
    archive_release_temp_object(compressed);
}

/// @brief `Archive.AddStr(name, text)` — convenience: store a string entry.
///
/// Converts `text` to UTF-8 Bytes via `rt_bytes_from_str` then delegates
/// to `Add`. The temporary Bytes is released eagerly.
///
/// @param obj  Archive handle.
/// @param name Entry name.
/// @param text String contents.
void rt_archive_add_str(void *obj, rt_string name, rt_string text) {
    void *data = rt_bytes_from_str(text);
    archive_add_with_temp_data(obj, name, data, "Archive: failed to add string entry");
}

/// @brief `Archive.AddFile(name, srcPath)` — copy a file from disk into the archive.
///
/// Reads `srcPath` fully into memory using `rt_bytes_new` + the
/// platform-appropriate exact-read helper, then delegates to `Add` for
/// CRC, compression, and entry recording. Traps on missing/invalid
/// source file. The temporary Bytes is released eagerly.
///
/// @param obj      Archive handle.
/// @param name     Name to use inside the archive.
/// @param src_path UTF-8 source file path on disk.
void rt_archive_add_file(void *obj, rt_string name, rt_string src_path) {
    const char *cpath = archive_require_path(src_path, "Archive: invalid source path");
    if (!cpath || *cpath == '\0')
        return;

    // Read file contents
#ifdef _WIN32
    HANDLE h = archive_open_win_path(cpath, GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING);
    if (h == INVALID_HANDLE_VALUE) {
        rt_trap("Archive: source file not found");
        return;
    }

    LARGE_INTEGER size;
    if (!GetFileSizeEx(h, &size)) {
        CloseHandle(h);
        rt_trap("Archive: failed to get file size");
        return;
    }
    BY_HANDLE_FILE_INFORMATION info;
    if (!GetFileInformationByHandle(h, &info) ||
        (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        CloseHandle(h);
        rt_trap("Archive: source path is not a regular file");
        return;
    }

    if (size.QuadPart < 0 || (uint64_t)size.QuadPart > INT64_MAX) {
        CloseHandle(h);
        rt_trap("Archive: source file too large");
        return;
    }

    void *data = archive_bytes_new_win_or_close(
        h, (int64_t)size.QuadPart, "Archive: memory allocation failed");
    if (!data)
        return;
    if (!archive_read_exact_win_or_release_object(
            h, data, (size_t)size.QuadPart, "Archive: failed to read source file"))
        return;
#else
    int fd = archive_open_posix(cpath, O_RDONLY, 0);
    if (fd < 0) {
        rt_trap("Archive: source file not found");
        return;
    }

    struct stat st;
    if (fstat(fd, &st) != 0) {
        close(fd);
        rt_trap("Archive: failed to get file size");
        return;
    }
    if (!S_ISREG(st.st_mode)) {
        close(fd);
        rt_trap("Archive: source path is not a regular file");
        return;
    }

    if (st.st_size < 0 || (uint64_t)st.st_size > (uint64_t)INT64_MAX ||
        (uint64_t)st.st_size > (uint64_t)SIZE_MAX) {
        close(fd);
        rt_trap("Archive: source file too large");
        return;
    }

    void *data = archive_bytes_new_posix_or_close(
        fd, (int64_t)st.st_size, "Archive: memory allocation failed");
    if (!data)
        return;
    if (!archive_read_exact_posix_or_release_object(
            fd, data, (size_t)st.st_size, "Archive: failed to read source file"))
        return;
#endif

    archive_add_with_temp_data(obj, name, data, "Archive: failed to add file entry");
}

/// @brief `Archive.AddDir(name)` — record an explicit directory entry.
///
/// Most ZIP tools infer directories from file paths, but explicit
/// directory entries (zero-length, name ending in `/`) are useful for
/// preserving empty directories. This function appends a stored-method
/// entry with no payload and a trailing-slash name. Traps if the
/// archive is read-only or finished.
///
/// @param obj  Archive handle.
/// @param name Directory name (trailing slash added if absent).
void rt_archive_add_dir(void *obj, rt_string name) {
    rt_archive_t *ar = archive_require(obj, "Archive: invalid archive");
    if (!ar) {
        rt_trap("Archive: NULL archive");
        return;
    }
    if (!ar->is_writing) {
        rt_trap("Archive: cannot add to read-only archive");
        return;
    }
    if (ar->is_finished) {
        rt_trap("Archive: archive already finished");
        return;
    }

    const char *cname = archive_entry_name_cstr(name);
    if (!cname || *cname == '\0') {
        rt_trap("Archive: invalid entry name");
        return;
    }

    char *norm_name = NULL;
    name_result_t norm_res = normalize_name(cname, &norm_name);
    if (norm_res == NAME_INVALID) {
        rt_trap("Archive: invalid entry name");
        return;
    }
    if (norm_res == NAME_OOM) {
        rt_trap("Archive: memory allocation failed");
        return;
    }

    // Ensure name ends with /
    size_t len = strlen(norm_name);
    if (len == 0 || norm_name[len - 1] != '/') {
        if (len > SIZE_MAX - 2) {
            free(norm_name);
            rt_trap("Archive: entry name too long");
            return;
        }
        char *new_name = (char *)realloc(norm_name, len + 2);
        if (!new_name) {
            free(norm_name);
            rt_trap("Archive: memory allocation failed");
            return;
        }
        norm_name = new_name;
        norm_name[len] = '/';
        norm_name[len + 1] = '\0';
        len++;
    }
    if (archive_write_has_entry(ar, norm_name)) {
        free(norm_name);
        rt_trap("Archive: duplicate entry name");
        return;
    }

    // Record entry info
    zip_entry_t e = {0};
    e.name = norm_name;
    e.crc32 = 0;
    e.compressed_size = 0;
    e.uncompressed_size = 0;
    e.method = ZIP_METHOD_STORED;
    get_dos_time(&e.mod_time, &e.mod_date);
    if (!archive_require_zip32_size(ar->write_len, "Archive: ZIP64 archives are not supported")) {
        free(norm_name);
        return;
    }
    e.local_offset = (uint32_t)ar->write_len;
    e.is_directory = true;

    // Write local file header
    if (len > UINT16_MAX) {
        rt_trap("Archive: entry name too long");
        free(norm_name);
        return;
    }
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

    size_t write_start = ar->write_len;
    if (!write_bytes(ar, local_header, ZIP_LOCAL_HEADER_SIZE) ||
        !write_bytes(ar, (const uint8_t *)norm_name, len)) {
        ar->write_len = write_start;
        free(norm_name);
        return;
    }

    if (!add_write_entry(ar, &e)) {
        ar->write_len = write_start;
        free(norm_name);
        return;
    }
}

/// @brief `Archive.Finish()` — emit the central directory and flush to disk.
///
/// Builds each central-directory header from the recorded entries
/// (including the back-pointer to the local header offset captured at
/// `Add` time), then writes the End of Central Directory record. Once
/// the in-memory layout is complete, the entire write buffer is
/// written to the destination path in a single shot. Sets `is_finished`
/// so subsequent `Add`/`Finish` calls trap. The write buffer is
/// released to free the memory immediately.
///
/// @param obj Archive handle (write mode).
void rt_archive_finish(void *obj) {
    rt_archive_t *ar = archive_require(obj, "Archive: invalid archive");
    if (!ar) {
        rt_trap("Archive: NULL archive");
        return;
    }
    if (!ar->is_writing) {
        rt_trap("Archive: cannot finish read-only archive");
        return;
    }
    if (ar->is_finished) {
        rt_trap("Archive: archive already finished");
        return;
    }
    if (!archive_require_zip16_count(ar->write_entry_count,
                                     "Archive: ZIP64 archives are not supported") ||
        !archive_require_zip32_size(ar->write_len, "Archive: ZIP64 archives are not supported"))
        return;

    // Record central directory offset
    uint32_t cd_offset = (uint32_t)ar->write_len;
    size_t finish_start = ar->write_len;

    // Write central directory
    for (int i = 0; i < ar->write_entry_count; i++) {
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
        write_u16(central_header + 30, 0);                          // Extra field length
        write_u16(central_header + 32, 0);                          // Comment length
        write_u16(central_header + 34, 0);                          // Disk number start
        write_u16(central_header + 36, 0);                          // Internal file attributes
        write_u32(central_header + 38, e->is_directory ? 0x10 : 0); // External attributes
        write_u32(central_header + 42, e->local_offset);

        if (!write_bytes(ar, central_header, ZIP_CENTRAL_HEADER_SIZE) ||
            !write_bytes(ar, (const uint8_t *)e->name, name_len)) {
            ar->write_len = finish_start;
            return;
        }
    }

    if (ar->write_len < (size_t)cd_offset || ar->write_len - (size_t)cd_offset > UINT32_MAX) {
        ar->write_len = finish_start;
        rt_trap("Archive: ZIP64 archives are not supported");
        return;
    }
    uint32_t cd_size = (uint32_t)(ar->write_len - (size_t)cd_offset);

    // Write end of central directory
    uint8_t eocd[ZIP_END_RECORD_SIZE];
    write_u32(eocd, ZIP_END_RECORD_SIG);
    write_u16(eocd + 4, 0); // Disk number
    write_u16(eocd + 6, 0); // Disk with central directory
    write_u16(eocd + 8, (uint16_t)ar->write_entry_count);
    write_u16(eocd + 10, (uint16_t)ar->write_entry_count);
    write_u32(eocd + 12, cd_size);
    write_u32(eocd + 16, cd_offset);
    write_u16(eocd + 20, 0); // Comment length

    if (!write_bytes(ar, eocd, ZIP_END_RECORD_SIZE) ||
        !archive_require_zip32_size(ar->write_len, "Archive: ZIP64 archives are not supported")) {
        ar->write_len = finish_start;
        return;
    }

    // Write to file
    const char *cpath = archive_require_path(ar->path, "Archive: invalid path");
    if (!cpath || *cpath == '\0')
        return;
    if (!archive_write_file_all_utf8(
            cpath, ar->write_buf, ar->write_len, "Archive: failed to write archive file")) {
        ar->write_len = finish_start;
        return;
    }

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

/// @brief `Archive.IsZip(path)` — fast probe for ZIP signature on disk.
///
/// Reads the first four bytes and tests for either the local-file-header
/// signature (typical archive) or the EOCD signature (empty archive
/// edge case where there are zero entries). Does NOT validate the
/// rest of the file — for that, call `Open` and let it trap.
/// Returns 0 for missing/unreadable files instead of trapping, so
/// scripts can probe untrusted paths safely.
///
/// @param path UTF-8 file path.
/// @return 1 if the file looks like a ZIP, 0 otherwise.
int8_t rt_archive_is_zip(rt_string path) {
    const char *cpath = NULL;
    if (!rt_file_path_from_vstr((const ViperString *)path, &cpath) || !cpath || *cpath == '\0')
        return 0;

#ifdef _WIN32
    HANDLE h = archive_open_win_path(cpath, GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING);
    if (h == INVALID_HANDLE_VALUE)
        return 0;

    uint8_t sig[4];
    DWORD read_count = 0;
    BOOL ok = ReadFile(h, sig, 4, &read_count, NULL);
    CloseHandle(h);

    if (!ok || read_count < 4)
        return 0;
#else
    int fd = archive_open_posix(cpath, O_RDONLY, 0);
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

/// @brief `Archive.IsZipBytes(data)` — same as `IsZip` but for an in-memory buffer.
///
/// Useful when checking downloaded payloads or asset blobs before
/// committing to a full `FromBytes` parse. Same caveat as `IsZip`:
/// only the first four bytes are inspected.
///
/// @param data `rt_bytes` candidate.
/// @return 1 if the buffer starts with a ZIP signature, 0 otherwise.
int8_t rt_archive_is_zip_bytes(void *data) {
    if (!data)
        return 0;

    int64_t len = bytes_len(data);
    if (len < 4)
        return 0;

    const uint8_t *src = bytes_data(data);
    if (!src)
        return 0;

    uint32_t magic = read_u32(src);
    return (magic == ZIP_LOCAL_HEADER_SIG || magic == ZIP_END_RECORD_SIG) ? 1 : 0;
}
