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

#include "rt_box.h"
#include "rt_bytes.h"
#include "rt_compress.h"
#include "rt_crc32.h"
#include "rt_dir.h"
#include "rt_file_path.h"
#include "rt_internal.h"
#include "rt_map.h"
#include "rt_object.h"
#include "rt_seq.h"
#include "rt_string.h"

#include <errno.h>
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

typedef struct {
    int64_t len;
    uint8_t *data;
} bytes_impl;

/// @brief Pointer to the raw byte buffer backing an `rt_bytes` handle.
///
/// The underlying layout is duplicated locally as `bytes_impl` to avoid
/// pulling internal headers; this matches the public ABI of
/// `rt_bytes_new`. Returns NULL for a NULL handle.
static inline uint8_t *bytes_data(void *obj) {
    if (!obj)
        return NULL;
    return ((bytes_impl *)obj)->data;
}

/// @brief Length in bytes of an `rt_bytes` handle, or 0 if NULL.
static inline int64_t bytes_len(void *obj) {
    if (!obj)
        return 0;
    return ((bytes_impl *)obj)->len;
}

/// @brief Drop a temporary GC object whose refcount has dropped to zero.
///
/// Used after we've materialized intermediate buffers (decompressed
/// data, throw-away byte arrays) and want to release them eagerly
/// instead of waiting for the next GC sweep.
static void archive_release_temp_object(void *obj) {
    if (obj && rt_obj_release_check0(obj))
        rt_obj_free(obj);
}

#ifdef _WIN32
/// @brief Open a UTF-8 path on Windows via the wide-string CreateFileW API.
///
/// Translates the UTF-8 input to UTF-16 with the long-path-aware helper
/// from `rt_file_path`, calls CreateFileW, and frees the wide buffer.
/// Returns INVALID_HANDLE_VALUE on conversion failure or when the
/// underlying open fails — the caller is expected to check.
///
/// @param cpath        UTF-8 file path.
/// @param access       Win32 access flags (e.g., GENERIC_READ).
/// @param share        Win32 share mode.
/// @param create_disp  Win32 creation disposition (OPEN_EXISTING, CREATE_ALWAYS, ...).
/// @return Open Windows file handle, or INVALID_HANDLE_VALUE on failure.
static HANDLE archive_open_win_path(const char *cpath, DWORD access, DWORD share, DWORD create_disp) {
    wchar_t *wide = rt_file_path_utf8_to_wide(cpath);
    if (!wide)
        return INVALID_HANDLE_VALUE;
    HANDLE h = CreateFileW(wide, access, share, NULL, create_disp, FILE_ATTRIBUTE_NORMAL, NULL);
    free(wide);
    return h;
}

/// @brief Read exactly `total` bytes from a Windows handle or trap.
///
/// Loops over `ReadFile` until the requested count is satisfied,
/// chunking each call to fit within DWORD_MAX. A short read that
/// reports zero bytes is treated as EOF and triggers the supplied
/// trap message — the archive parser must read complete records.
///
/// @param h        Open Windows file handle (positioned by caller).
/// @param dst      Destination buffer of at least `total` bytes.
/// @param total    Total number of bytes to read.
/// @param trap_msg Trap message used on read failure / premature EOF.
static void archive_read_exact_win(HANDLE h, uint8_t *dst, size_t total, const char *trap_msg) {
    size_t read_total = 0;
    while (read_total < total) {
        DWORD chunk = 0;
        size_t remaining = total - read_total;
        DWORD want = remaining > (size_t)DWORD_MAX ? DWORD_MAX : (DWORD)remaining;
        if (!ReadFile(h, dst + read_total, want, &chunk, NULL) || chunk == 0)
            rt_trap(trap_msg);
        read_total += (size_t)chunk;
    }
}

/// @brief Write exactly `total` bytes to a Windows handle or trap.
///
/// Mirror of `archive_read_exact_win` for the write path. Loops over
/// WriteFile, chunking by DWORD_MAX, and traps on any short write or
/// failure so callers don't need to invent their own retry logic.
static void archive_write_exact_win(HANDLE h, const uint8_t *src, size_t total, const char *trap_msg) {
    size_t written_total = 0;
    while (written_total < total) {
        DWORD chunk = 0;
        size_t remaining = total - written_total;
        DWORD want = remaining > (size_t)DWORD_MAX ? DWORD_MAX : (DWORD)remaining;
        if (!WriteFile(h, src + written_total, want, &chunk, NULL) || chunk == 0)
            rt_trap(trap_msg);
        written_total += (size_t)chunk;
    }
}
#else
/// @brief Read exactly `total` bytes from a POSIX fd or trap.
///
/// Loops over `read(2)` retrying EINTR. A zero return is treated as
/// EOF and triggers the trap so partial archive reads cannot succeed
/// silently. The fd is left positioned just after the last byte read.
static void archive_read_exact_posix(int fd, uint8_t *dst, size_t total, const char *trap_msg) {
    size_t read_total = 0;
    while (read_total < total) {
        ssize_t n = read(fd, dst + read_total, total - read_total);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            rt_trap(trap_msg);
        }
        if (n == 0)
            rt_trap(trap_msg);
        read_total += (size_t)n;
    }
}

/// @brief Write exactly `total` bytes to a POSIX fd or trap.
///
/// Mirror of `archive_read_exact_posix` for writes. Retries EINTR,
/// traps on error or unexpected zero-byte writes (the kernel never
/// returns zero on a regular file write unless the disk is full).
static void archive_write_exact_posix(int fd, const uint8_t *src, size_t total, const char *trap_msg) {
    size_t written_total = 0;
    while (written_total < total) {
        ssize_t n = write(fd, src + written_total, total - written_total);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            rt_trap(trap_msg);
        }
        if (n == 0)
            rt_trap(trap_msg);
        written_total += (size_t)n;
    }
}
#endif

/// @brief Truncate-create a file at a UTF-8 path and write `total` bytes.
///
/// Cross-platform helper used to flush the assembled write buffer
/// during `Finish` and to drop extracted entries onto disk during
/// `Extract`/`ExtractAll`. Always uses `CREATE_ALWAYS` (Windows) or
/// `O_TRUNC` (POSIX) — the destination is replaced unconditionally.
///
/// @param cpath    UTF-8 destination file path.
/// @param src      Source byte buffer.
/// @param total    Number of bytes to write.
/// @param trap_msg Trap message used on any failure.
static void archive_write_file_all_utf8(const char *cpath, const uint8_t *src, size_t total, const char *trap_msg) {
#ifdef _WIN32
    HANDLE h = archive_open_win_path(cpath, GENERIC_WRITE, 0, CREATE_ALWAYS);
    if (h == INVALID_HANDLE_VALUE)
        rt_trap(trap_msg);
    archive_write_exact_win(h, src, total, trap_msg);
    CloseHandle(h);
#else
    int fd = open(cpath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
        rt_trap(trap_msg);
    archive_write_exact_posix(fd, src, total, trap_msg);
    close(fd);
#endif
}

/// @brief Write the contents of an `rt_bytes` handle to a UTF-8 path.
///
/// Thin adapter over `archive_write_file_all_utf8` that unwraps the
/// bytes handle. Traps if the path is empty or NULL.
///
/// @param cpath UTF-8 destination path.
/// @param data  Source `rt_bytes` handle.
static void archive_write_bytes_to_path(const char *cpath, void *data) {
    if (!cpath || *cpath == '\0')
        rt_trap("Archive: invalid destination path");

    const uint8_t *src = bytes_data(data);
    size_t total = (size_t)bytes_len(data);
    archive_write_file_all_utf8(cpath, src, total, "Archive: failed to write destination file");
}

//=============================================================================
// ZIP Entry Structure
//=============================================================================

typedef struct zip_entry {
    char *name;                 // Entry name (allocated)
    uint32_t crc32;             // CRC-32 of uncompressed data
    uint32_t compressed_size;   // Size after compression
    uint32_t uncompressed_size; // Original size
    uint16_t method;            // Compression method (0 or 8)
    uint16_t mod_time;          // DOS time
    uint16_t mod_date;          // DOS date
    uint32_t local_offset;      // Offset of local header in file
    bool is_directory;          // True if directory entry
} zip_entry_t;

//=============================================================================
// Archive Structure
//=============================================================================

typedef struct rt_archive {
    rt_string path;   // File path or NULL if from bytes
    uint8_t *data;    // Archive data (mmap or copy)
    size_t data_len;  // Length of data
    bool owns_data;   // True if we allocated data
    bool is_writing;  // True if opened for writing
    bool is_finished; // True if Finish() was called

    // For reading
    zip_entry_t *entries; // Array of entries
    int entry_count;      // Number of entries

    // For writing
    int fd;                     // File descriptor for writing
    uint8_t *write_buf;         // Write buffer
    size_t write_len;           // Current length
    size_t write_cap;           // Buffer capacity
    zip_entry_t *write_entries; // Entries being written
    int write_entry_count;      // Number of entries written
    int write_entry_cap;        // Capacity
} rt_archive_t;

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

//=============================================================================
// Archive Allocation
//=============================================================================

static void archive_finalize(void *obj);
static void archive_free_entries(rt_archive_t *ar);
static void archive_require_zip32_size(size_t size, const char *context);
static void archive_require_zip16_count(int count, const char *context);

/// @brief Allocate a zero-initialized archive object via the GC heap.
///
/// Hooks `archive_finalize` so that file paths, copied data, and entry
/// arrays are reclaimed when the GC drops the last reference. `fd` is
/// initialized to -1 (sentinel for "not open"). Traps on OOM.
///
/// @return Pointer to a fresh `rt_archive_t`, never NULL.
static rt_archive_t *archive_alloc(void) {
    size_t total = sizeof(rt_archive_t);
    rt_archive_t *ar = (rt_archive_t *)rt_obj_new_i64(0, (int64_t)total);
    if (!ar)
        rt_trap("Archive: memory allocation failed");
    memset(ar, 0, total);
    ar->fd = -1;
    rt_obj_set_finalizer(ar, archive_finalize);
    return ar;
}

/// @brief Free both read-side and write-side entry arrays.
///
/// Walks each entry releasing its `name` allocation, then frees the
/// containing array. Resets all counters to zero so the archive can
/// be re-parsed without leaking. Safe to call on a half-initialized
/// archive.
static void archive_free_entries(rt_archive_t *ar) {
    if (ar->entries) {
        for (int i = 0; i < ar->entry_count; i++) {
            free(ar->entries[i].name);
        }
        free(ar->entries);
        ar->entries = NULL;
        ar->entry_count = 0;
    }
    if (ar->write_entries) {
        for (int i = 0; i < ar->write_entry_count; i++) {
            free(ar->write_entries[i].name);
        }
        free(ar->write_entries);
        ar->write_entries = NULL;
        ar->write_entry_count = 0;
        ar->write_entry_cap = 0;
    }
}

/// @brief GC finalizer for archive objects.
///
/// Called when the GC drops the last reference. Releases the path
/// string, frees any owned data buffer, walks the entry arrays,
/// and disposes of any pending write buffer.
///
/// @param obj Archive pointer (may be NULL).
static void archive_finalize(void *obj) {
    rt_archive_t *ar = (rt_archive_t *)obj;
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
static void archive_require_zip32_size(size_t size, const char *context) {
    if (size > UINT32_MAX)
        rt_trap(context);
}

/// @brief Trap if `count` would overflow the 16-bit ZIP entry counter.
///
/// The pre-ZIP64 EOCD record stores total entry count in a uint16, so
/// archives with more than 65,535 entries are rejected.
static void archive_require_zip16_count(int count, const char *context) {
    if (count < 0 || count > UINT16_MAX)
        rt_trap(context);
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
        if (read_u32(data + offset) == ZIP_END_RECORD_SIG) {
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

    // Validate central directory bounds
    if ((size_t)cd_offset + cd_size > eocd_offset)
        return false;

    ar->entry_count = total_entries;
    if (total_entries > 0) {
        ar->entries = (zip_entry_t *)calloc(total_entries, sizeof(zip_entry_t));
        if (!ar->entries)
            return false;
    }

    // Parse each central directory entry
    const uint8_t *p = ar->data + cd_offset;
    const uint8_t *cd_end = p + cd_size;

    for (int i = 0; i < total_entries && p + ZIP_CENTRAL_HEADER_SIZE <= cd_end; i++) {
        if (read_u32(p) != ZIP_CENTRAL_HEADER_SIG) {
            archive_free_entries(ar);
            return false;
        }

        uint16_t name_len = read_u16(p + 28);
        uint16_t extra_len = read_u16(p + 30);
        uint16_t comment_len = read_u16(p + 32);

        if (p + ZIP_CENTRAL_HEADER_SIZE + name_len + extra_len + comment_len > cd_end) {
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
        if (!e->name) {
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
    for (int i = 0; i < ar->entry_count; i++) {
        if (strcmp(ar->entries[i].name, name) == 0)
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
    if (e->method == ZIP_METHOD_STORED) {
        // Verify CRC
        uint32_t crc = rt_crc32_compute(compressed, e->uncompressed_size);
        if (crc != e->crc32)
            rt_trap("Archive: CRC mismatch");

        void *result = rt_bytes_new(e->uncompressed_size);
        memcpy(bytes_data(result), compressed, e->uncompressed_size);
        return result;
    }

    // Handle deflated data
    if (e->method == ZIP_METHOD_DEFLATE) {
        // Create bytes with compressed data
        void *comp_bytes = rt_bytes_new(e->compressed_size);
        memcpy(bytes_data(comp_bytes), compressed, e->compressed_size);

        // Inflate
        void *result = rt_compress_inflate(comp_bytes);

        // Verify CRC
        uint32_t crc = rt_crc32_compute(bytes_data(result), bytes_len(result));
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

/// @brief Grow the in-memory write buffer so `need` more bytes will fit.
///
/// Doubles the capacity (or jumps to `len + need + 4096` if doubling
/// is still too small) using `realloc`. Traps on OOM. Cheap when the
/// buffer is already large enough.
static void write_ensure(rt_archive_t *ar, size_t need) {
    if (ar->write_len + need > ar->write_cap) {
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

/// @brief Append `len` bytes to the write buffer, growing it as needed.
///
/// Single-call wrapper around `write_ensure` + memcpy. Used as the
/// only path for appending bytes during archive construction so the
/// growth policy is centralized.
static void write_bytes(rt_archive_t *ar, const uint8_t *data, size_t len) {
    write_ensure(ar, len);
    memcpy(ar->write_buf + ar->write_len, data, len);
    ar->write_len += len;
}

/// @brief Append a `zip_entry_t` to the writing-side entry table.
///
/// Verifies the new total stays within the 16-bit ZIP entry limit
/// (or traps with a ZIP64 message), then doubles the capacity of
/// `write_entries` if full. The supplied `*e` is copied by value —
/// the caller may reuse the source struct after the call.
static void add_write_entry(rt_archive_t *ar, zip_entry_t *e) {
    archive_require_zip16_count(ar->write_entry_count + 1, "Archive: ZIP64 archives are not supported");
    if (ar->write_entry_count >= ar->write_entry_cap) {
        int new_cap = ar->write_entry_cap == 0 ? 16 : ar->write_entry_cap * 2;
        zip_entry_t *new_entries =
            (zip_entry_t *)realloc(ar->write_entries, new_cap * sizeof(zip_entry_t));
        if (!new_entries)
            rt_trap("Archive: memory allocation failed");
        ar->write_entries = new_entries;
        ar->write_entry_cap = new_cap;
    }
    ar->write_entries[ar->write_entry_count++] = *e;
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
    char *new_name = (char *)realloc(name, len + 2);
    if (!new_name) {
        free(name);
        rt_trap("Archive: memory allocation failed");
    }
    new_name[len] = '/';
    new_name[len + 1] = '\0';
    return new_name;
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
    const char *cpath = rt_string_cstr(path);
    if (!cpath || *cpath == '\0')
        rt_trap("Archive: invalid path");

    // Open and read file
#ifdef _WIN32
    HANDLE h = archive_open_win_path(cpath, GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING);
    if (h == INVALID_HANDLE_VALUE)
        rt_trap("Archive: file not found");

    LARGE_INTEGER size;
    if (!GetFileSizeEx(h, &size)) {
        CloseHandle(h);
        rt_trap("Archive: failed to get file size");
    }

    if (size.QuadPart < 0 || (uint64_t)size.QuadPart > SIZE_MAX) {
        CloseHandle(h);
        rt_trap("Archive: file too large");
    }

    size_t data_len = (size_t)size.QuadPart;
    uint8_t *data = (uint8_t *)malloc(data_len);
    if (!data) {
        CloseHandle(h);
        rt_trap("Archive: memory allocation failed");
    }
    archive_read_exact_win(h, data, data_len, "Archive: failed to read file");
    CloseHandle(h);
#else
    int fd = open(cpath, O_RDONLY);
    if (fd < 0)
        rt_trap("Archive: file not found");

    struct stat st;
    if (fstat(fd, &st) != 0) {
        close(fd);
        rt_trap("Archive: failed to get file size");
    }

    if (st.st_size < 0) {
        close(fd);
        rt_trap("Archive: invalid file size");
    }

    size_t data_len = (size_t)st.st_size;
    uint8_t *data = (uint8_t *)malloc(data_len);
    if (!data) {
        close(fd);
        rt_trap("Archive: memory allocation failed");
    }
    archive_read_exact_posix(fd, data, data_len, "Archive: failed to read file");
    close(fd);
#endif

    rt_archive_t *ar = archive_alloc();
    ar->path = rt_string_ref(path);
    ar->data = data;
    ar->data_len = data_len;
    ar->owns_data = true;
    ar->is_writing = false;

    if (!parse_central_directory(ar)) {
        free(ar->data);
        rt_trap("Archive: not a valid ZIP file");
    }

    return ar;
}

/// @brief `Archive.Create(path)` — start a new ZIP file for writing.
///
/// Truncates / creates the destination file immediately to fail-fast on
/// permission errors, then closes the handle (we re-open at `Finish`
/// time once the in-memory layout is complete). All entry data is
/// buffered in `write_buf` until then. Traps on invalid path or
/// allocation failure.
///
/// @param path UTF-8 destination path.
/// @return Owned `Archive` handle in write mode.
void *rt_archive_create(rt_string path) {
    const char *cpath = rt_string_cstr(path);
    if (!cpath || *cpath == '\0')
        rt_trap("Archive: invalid path");

#ifdef _WIN32
    HANDLE h = archive_open_win_path(cpath, GENERIC_WRITE, 0, CREATE_ALWAYS);
    if (h == INVALID_HANDLE_VALUE)
        rt_trap("Archive: failed to create file");
    CloseHandle(h);
#else
    int fd = open(cpath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
        rt_trap("Archive: failed to create file");
    close(fd);
    fd = -1; // We'll reopen at Finish time
#endif

    rt_archive_t *ar = archive_alloc();
    ar->path = rt_string_ref(path);
    ar->is_writing = true;
    ar->write_cap = 4096;
    ar->write_buf = (uint8_t *)malloc(ar->write_cap);
    if (!ar->write_buf)
        rt_trap("Archive: memory allocation failed");

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

    if (!parse_central_directory(ar)) {
        free(ar->data);
        rt_trap("Archive: not a valid ZIP archive");
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
    rt_archive_t *ar = (rt_archive_t *)obj;
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
    rt_archive_t *ar = (rt_archive_t *)obj;
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
    rt_archive_t *ar = (rt_archive_t *)obj;
    void *seq = rt_seq_new();

    if (!ar)
        return seq;

    if (ar->is_writing) {
        for (int i = 0; i < ar->write_entry_count; i++) {
            rt_string name = rt_const_cstr(ar->write_entries[i].name);
            rt_seq_push(seq, name);
        }
    } else {
        for (int i = 0; i < ar->entry_count; i++) {
            rt_string name = rt_const_cstr(ar->entries[i].name);
            rt_seq_push(seq, name);
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
    rt_archive_t *ar = (rt_archive_t *)obj;
    if (!ar || ar->is_writing)
        return 0;

    const char *cname = rt_string_cstr(name);
    if (!cname)
        return 0;
    const bool wants_dir = name_ends_with_sep(cname);

    char *norm_name = NULL;
    name_result_t norm_res = normalize_name(cname, &norm_name);
    if (norm_res == NAME_OOM)
        rt_trap("Archive: memory allocation failed");
    if (norm_res == NAME_INVALID)
        return 0;
    if (wants_dir)
        norm_name = ensure_trailing_slash(norm_name);

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
    rt_archive_t *ar = (rt_archive_t *)obj;
    if (!ar)
        rt_trap("Archive: NULL archive");
    if (ar->is_writing)
        rt_trap("Archive: cannot read from write-only archive");

    const char *cname = rt_string_cstr(name);
    if (!cname)
        rt_trap("Archive: NULL entry name");

    char *norm_name = NULL;
    name_result_t norm_res = normalize_name(cname, &norm_name);
    if (norm_res == NAME_INVALID)
        rt_trap("Archive: invalid entry name");
    if (norm_res == NAME_OOM)
        rt_trap("Archive: memory allocation failed");

    zip_entry_t *e = find_entry(ar, norm_name);
    free(norm_name);
    if (!e)
        rt_trap("Archive: entry not found");

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
    return rt_bytes_to_str(data);
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
    void *data = rt_archive_read(obj, name);

    const char *cpath = rt_string_cstr(dest_path);
    if (!cpath || *cpath == '\0')
        rt_trap("Archive: invalid destination path");

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
    rt_archive_t *ar = (rt_archive_t *)obj;
    if (!ar)
        rt_trap("Archive: NULL archive");
    if (ar->is_writing)
        rt_trap("Archive: cannot extract from write-only archive");

    const char *cdir = rt_string_cstr(dest_dir);
    if (!cdir || *cdir == '\0')
        rt_trap("Archive: invalid destination directory");

    rt_dir_make_all(dest_dir);
    size_t dir_len = strlen(cdir);

    for (int i = 0; i < ar->entry_count; i++) {
        zip_entry_t *e = &ar->entries[i];

        char *norm_name = NULL;
        name_result_t norm_res = normalize_name(e->name, &norm_name);
        if (norm_res == NAME_INVALID)
            rt_trap("Archive: invalid entry name");
        if (norm_res == NAME_OOM)
            rt_trap("Archive: memory allocation failed");

        // Build full path
        size_t name_len = strlen(norm_name);
        size_t path_len = dir_len + 1 + name_len;
        char *full_path = (char *)malloc(path_len + 1);
        if (!full_path)
            rt_trap("Archive: memory allocation failed");

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
            rt_string dir_path = rt_const_cstr(full_path);
            rt_dir_make_all(dir_path);
        } else {
            // Create parent directory
            char *last_sep = strrchr(full_path, PATH_SEP);
            if (last_sep && last_sep > full_path + dir_len) {
                *last_sep = '\0';
                rt_string parent = rt_const_cstr(full_path);
                rt_dir_make_all(parent);
                *last_sep = PATH_SEP;
            }

            void *data = read_entry_data(ar, e);
            archive_write_bytes_to_path(full_path, data);
            archive_release_temp_object(data);
        }

        free(full_path);
        free(norm_name);
    }
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
    rt_archive_t *ar = (rt_archive_t *)obj;
    if (!ar)
        rt_trap("Archive: NULL archive");
    if (ar->is_writing)
        rt_trap("Archive: cannot get info from write-only archive");

    const char *cname = rt_string_cstr(name);
    if (!cname)
        rt_trap("Archive: NULL entry name");
    const bool wants_dir = name_ends_with_sep(cname);

    char *norm_name = NULL;
    name_result_t norm_res = normalize_name(cname, &norm_name);
    if (norm_res == NAME_INVALID)
        rt_trap("Archive: invalid entry name");
    if (norm_res == NAME_OOM)
        rt_trap("Archive: memory allocation failed");
    if (wants_dir)
        norm_name = ensure_trailing_slash(norm_name);

    zip_entry_t *e = find_entry(ar, norm_name);
    free(norm_name);
    if (!e)
        rt_trap("Archive: entry not found");

    void *map = rt_map_new();
    void *boxed;

    // Add size
    boxed = rt_box_i64(e->uncompressed_size);
    rt_map_set(map, rt_const_cstr("size"), boxed);
    if (boxed && rt_obj_release_check0(boxed))
        rt_obj_free(boxed);

    // Add compressed size
    boxed = rt_box_i64(e->compressed_size);
    rt_map_set(map, rt_const_cstr("compressedSize"), boxed);
    if (boxed && rt_obj_release_check0(boxed))
        rt_obj_free(boxed);

    boxed = rt_box_i64((int64_t)e->crc32);
    rt_map_set(map, rt_const_cstr("crc"), boxed);
    if (boxed && rt_obj_release_check0(boxed))
        rt_obj_free(boxed);

    boxed = rt_box_i64((int64_t)e->method);
    rt_map_set(map, rt_const_cstr("method"), boxed);
    if (boxed && rt_obj_release_check0(boxed))
        rt_obj_free(boxed);

    // Add modified time (convert DOS time to Unix timestamp)
    // DOS date: bits 0-4 = day, 5-8 = month, 9-15 = year from 1980
    // DOS time: bits 0-4 = seconds/2, 5-10 = minutes, 11-15 = hours
    int year = ((e->mod_date >> 9) & 0x7F) + 1980;
    int month = (e->mod_date >> 5) & 0xF;
    int day = e->mod_date & 0x1F;
    int hour = (e->mod_time >> 11) & 0x1F;
    int minute = (e->mod_time >> 5) & 0x3F;
    int second = (e->mod_time & 0x1F) * 2;

    // Simple approximation of Unix timestamp (not accounting for leap years properly).
    // DOS time has 2-second granularity (seconds/2 in bits 0-4); odd seconds are lost.
    int64_t timestamp = (int64_t)(year - 1970) * 365 * 24 * 3600;
    timestamp += (int64_t)(month - 1) * 30 * 24 * 3600;
    timestamp += (int64_t)(day - 1) * 24 * 3600;
    timestamp += (int64_t)hour * 3600;
    timestamp += (int64_t)minute * 60;
    timestamp += second;

    boxed = rt_box_i64(timestamp);
    rt_map_set(map, rt_const_cstr("modifiedTime"), boxed);
    if (boxed && rt_obj_release_check0(boxed))
        rt_obj_free(boxed);

    // Add isDirectory
    boxed = rt_box_i1(e->is_directory ? 1 : 0);
    rt_map_set(map, rt_const_cstr("isDirectory"), boxed);
    rt_map_set(map, rt_const_cstr("isDir"), boxed);
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

    char *norm_name = NULL;
    name_result_t norm_res = normalize_name(cname, &norm_name);
    if (norm_res == NAME_INVALID)
        rt_trap("Archive: invalid entry name");
    if (norm_res == NAME_OOM)
        rt_trap("Archive: memory allocation failed");

    uint8_t *raw_data = bytes_data(data);
    size_t raw_len = (size_t)bytes_len(data);
    archive_require_zip32_size(raw_len, "Archive: ZIP64 entries are not supported");

    // Compute CRC
    uint32_t crc = rt_crc32_compute(raw_data, raw_len);

    // Decide whether to compress
    void *compressed = NULL;
    uint16_t method = ZIP_METHOD_STORED;
    const uint8_t *write_data = raw_data;
    size_t write_len = raw_len;

    if (raw_len > 64) {
        compressed = rt_compress_deflate(data);
        size_t comp_len = (size_t)bytes_len(compressed);
        if (comp_len < raw_len) {
            method = ZIP_METHOD_DEFLATE;
            write_data = bytes_data(compressed);
            write_len = comp_len;
        }
    }
    archive_require_zip32_size(write_len, "Archive: ZIP64 entries are not supported");
    archive_require_zip32_size(ar->write_len, "Archive: ZIP64 archives are not supported");

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
    if (name_len > UINT16_MAX)
        rt_trap("Archive: entry name too long");
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
    rt_archive_add(obj, name, data);
    archive_release_temp_object(data);
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
    const char *cpath = rt_string_cstr(src_path);
    if (!cpath || *cpath == '\0')
        rt_trap("Archive: invalid source path");

    // Read file contents
#ifdef _WIN32
    HANDLE h = archive_open_win_path(cpath, GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING);
    if (h == INVALID_HANDLE_VALUE)
        rt_trap("Archive: source file not found");

    LARGE_INTEGER size;
    if (!GetFileSizeEx(h, &size)) {
        CloseHandle(h);
        rt_trap("Archive: failed to get file size");
    }

    if (size.QuadPart < 0 || (uint64_t)size.QuadPart > INT64_MAX) {
        CloseHandle(h);
        rt_trap("Archive: source file too large");
    }

    void *data = rt_bytes_new((int64_t)size.QuadPart);
    archive_read_exact_win(
        h, bytes_data(data), (size_t)size.QuadPart, "Archive: failed to read source file");
    CloseHandle(h);
#else
    int fd = open(cpath, O_RDONLY);
    if (fd < 0)
        rt_trap("Archive: source file not found");

    struct stat st;
    if (fstat(fd, &st) != 0) {
        close(fd);
        rt_trap("Archive: failed to get file size");
    }

    if (st.st_size < 0) {
        close(fd);
        rt_trap("Archive: invalid source file size");
    }

    void *data = rt_bytes_new((int64_t)st.st_size);
    archive_read_exact_posix(
        fd, bytes_data(data), (size_t)st.st_size, "Archive: failed to read source file");
    close(fd);
#endif

    rt_archive_add(obj, name, data);
    archive_release_temp_object(data);
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

    char *norm_name = NULL;
    name_result_t norm_res = normalize_name(cname, &norm_name);
    if (norm_res == NAME_INVALID)
        rt_trap("Archive: invalid entry name");
    if (norm_res == NAME_OOM)
        rt_trap("Archive: memory allocation failed");

    // Ensure name ends with /
    size_t len = strlen(norm_name);
    if (len == 0 || norm_name[len - 1] != '/') {
        char *new_name = (char *)realloc(norm_name, len + 2);
        if (!new_name) {
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
    archive_require_zip32_size(ar->write_len, "Archive: ZIP64 archives are not supported");
    e.local_offset = (uint32_t)ar->write_len;
    e.is_directory = true;

    // Write local file header
    if (len > UINT16_MAX)
        rt_trap("Archive: entry name too long");
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
    rt_archive_t *ar = (rt_archive_t *)obj;
    if (!ar)
        rt_trap("Archive: NULL archive");
    if (!ar->is_writing)
        rt_trap("Archive: cannot finish read-only archive");
    if (ar->is_finished)
        rt_trap("Archive: archive already finished");
    archive_require_zip16_count(ar->write_entry_count, "Archive: ZIP64 archives are not supported");
    archive_require_zip32_size(ar->write_len, "Archive: ZIP64 archives are not supported");

    // Record central directory offset
    uint32_t cd_offset = (uint32_t)ar->write_len;

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

        write_bytes(ar, central_header, ZIP_CENTRAL_HEADER_SIZE);
        write_bytes(ar, (const uint8_t *)e->name, name_len);
    }

    uint32_t cd_size = (uint32_t)ar->write_len - cd_offset;

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

    write_bytes(ar, eocd, ZIP_END_RECORD_SIZE);

    // Write to file
    const char *cpath = rt_string_cstr(ar->path);
    archive_write_file_all_utf8(cpath, ar->write_buf, ar->write_len, "Archive: failed to write archive file");

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
    const char *cpath = rt_string_cstr(path);
    if (!cpath || *cpath == '\0')
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

    if (bytes_len(data) < 4)
        return 0;

    uint32_t magic = read_u32(bytes_data(data));
    return (magic == ZIP_LOCAL_HEADER_SIG || magic == ZIP_END_RECORD_SIG) ? 1 : 0;
}
