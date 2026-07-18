//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/io/rt_archive_internal.h
// Purpose: Internal contract between the archive core (rt_archive.c) and the
//          filesystem / atomic-write adapter layer (rt_archive_fs.c). Carries
//          the small Bytes accessors plus the platform (Win32/POSIX) file I/O
//          helpers shared across the two translation units.
//
// Key invariants:
//   - Engine-internal; must not be included outside the io/ directory.
//   - Platform-specific declarations are guarded to match their definitions in
//     rt_archive_fs.c (Win32 handle helpers vs. POSIX fd helpers).
//   - The atomic-write helpers reject symlinked/reparse path components so an
//     archive extraction cannot escape its destination root.
//
// Ownership/Lifetime:
//   - Helpers borrow caller-owned paths/handles; Bytes-producing helpers return
//     fresh GC objects owned by the caller.
//
// Links: src/runtime/io/rt_archive.c (core/ZIP/API),
//        src/runtime/io/rt_archive_fs.c (definitions)
//
//===----------------------------------------------------------------------===//
#pragma once

#include "rt_archive.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// NOTE: the bytes_data/bytes_len wrappers are intentionally NOT shared here.
// They are trivial static-inline wrappers around the runtime.def symbols
// rt_bytes_data/rt_bytes_len; each consuming .c defines its own copy. Declaring
// them here (or defining them inline) would either collide at link time (the
// names are generic) or be mis-parsed by rtgen's header scan.

// Core-defined helpers consumed by the fs adapter (defined in rt_archive.c).
void archive_release_temp_object(void *obj);
void archive_save_trap_error(char *buffer, size_t buffer_size, const char *fallback);

//=============================================================================
// Filesystem / atomic-write adapter (defined in rt_archive_fs.c)
//=============================================================================

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

HANDLE archive_open_win_path(const char *cpath, DWORD access, DWORD share, DWORD create_disp);
int archive_read_exact_win_or_free(HANDLE h, uint8_t *dst, size_t total, const char *trap_msg);
int archive_read_exact_win_or_release_object(HANDLE h,
                                             void *bytes,
                                             size_t total,
                                             const char *trap_msg);
void *archive_bytes_new_win_or_close(HANDLE h, int64_t len, const char *fallback);
#else
#include <sys/types.h>

int archive_open_posix(const char *path, int flags, mode_t mode);
int archive_read_exact_posix_or_free(int fd, uint8_t *dst, size_t total, const char *trap_msg);
int archive_read_exact_posix_or_release_object(int fd,
                                               void *bytes,
                                               size_t total,
                                               const char *trap_msg);
void *archive_bytes_new_posix_or_close(int fd, int64_t len, const char *fallback);
void archive_make_dirs_posix_at(int root_fd, const char *path);
int archive_open_parent_for_file_posix(int root_fd, const char *name, char **out_leaf);
void archive_write_bytes_to_dirfd_posix(int parent_fd, const char *leaf, void *data);
int archive_open_root_dir_posix(const char *cdir);
#endif

// Cross-platform atomic-write / path helpers.
/// @brief Allocate and initialize the native per-archive reader-writer lock.
/// @details The platform adapter uses an SRW lock on Windows and a POSIX
///          pthread reader-writer lock elsewhere. The returned allocation has
///          no managed references and must be destroyed after archive
///          quiescence with @ref archive_rwlock_destroy.
/// @return Opaque initialized lock, or NULL when allocation/initialization fails.
void *archive_rwlock_create(void);

/// @brief Destroy a native archive lock after the final archive reference is gone.
/// @param lock Opaque lock from @ref archive_rwlock_create; NULL is a no-op.
/// @pre No thread may hold or be waiting for @p lock.
void archive_rwlock_destroy(void *lock);

/// @brief Enter shared access to an archive's stable read/property state.
/// @param lock Valid opaque archive lock.
/// @post The caller owns one shared lock acquisition until
///       @ref archive_rwlock_read_exit.
void archive_rwlock_read_enter(void *lock);

/// @brief Leave a shared archive lock acquisition.
/// @param lock Valid lock currently held once for reading by this thread.
void archive_rwlock_read_exit(void *lock);

/// @brief Enter exclusive access to archive writer buffers, indexes, and state.
/// @details Writer APIs hold this lock across their transactional mutation so
///          concurrent Add, AddDir, Finish, Count, and Names calls observe only
///          complete entry-table states.
/// @param lock Valid opaque archive lock.
void archive_rwlock_write_enter(void *lock);

/// @brief Leave an exclusive archive lock acquisition.
/// @param lock Valid lock currently held once for writing by this thread.
void archive_rwlock_write_exit(void *lock);
char *archive_make_temp_path(const char *path, unsigned attempt);
void archive_unlink_utf8(const char *path);
int archive_write_file_all_utf8(const char *cpath,
                                const uint8_t *src,
                                size_t total,
                                const char *trap_msg);
void archive_write_bytes_to_path(const char *cpath, void *data);
size_t archive_trim_trailing_seps(const char *path, size_t len);
void archive_reject_symlink_components(const char *path, size_t root_len, int include_leaf);

//=============================================================================
// ZIP format constants + entry/archive types (shared read/write)
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

typedef struct rt_archive {
    rt_string path;   ///< File path string, or NULL for byte-backed archives.
    uint8_t *data;    ///< Full archive bytes (malloc'd copy or provided blob).
    size_t data_len;  ///< Length of `data` in bytes.
    bool owns_data;   ///< True when this object allocated `data` and must free it.
    bool is_writing;  ///< True when opened via `Archive.Create` (write mode).
    bool is_finished; ///< True after `Archive.Finish` has been called.
    void *rw_lock;    ///< Native reader-writer lock owned by this archive.

    uint64_t max_file_bytes;        ///< Maximum encoded archive image accepted/produced.
    uint64_t max_entry_bytes;       ///< Per-entry uncompressed allocation ceiling.
    uint64_t max_total_entry_bytes; ///< Sum of declared uncompressed entry sizes allowed.
    int parse_error;                ///< `archive_parse_error_t` from the most recent parse.

    // Read-side fields
    zip_entry_t *entries;         ///< Array of parsed central-directory entries.
    int entry_count;              ///< Number of entries in `entries`.
    size_t central_offset;        ///< First byte of the validated central directory.
    int32_t *entry_name_slots;    ///< Open-addressed name index (`entry index + 1`).
    size_t entry_name_slot_count; ///< Power-of-two capacity of `entry_name_slots`.

    // Write-side fields
    int fd;                           ///< POSIX fd used for streaming writes (-1 if unused).
    uint8_t *write_buf;               ///< In-memory write accumulation buffer.
    size_t write_len;                 ///< Current number of valid bytes in `write_buf`.
    size_t write_cap;                 ///< Allocated capacity of `write_buf`.
    zip_entry_t *write_entries;       ///< Metadata for each entry added so far.
    int write_entry_count;            ///< Number of entries in `write_entries`.
    int write_entry_cap;              ///< Allocated capacity of `write_entries`.
    uint64_t write_total_entry_bytes; ///< Sum of uncompressed sizes committed by Add.
    int32_t *write_name_slots;        ///< Open-addressed write-name index (`entry index + 1`).
    size_t write_name_slot_count;     ///< Power-of-two capacity of `write_name_slots`.
} rt_archive_t;

typedef enum { NAME_OK = 0, NAME_INVALID, NAME_OOM } name_result_t;

/// @brief Structured reason returned by the non-trapping ZIP parser.
typedef enum archive_parse_error {
    ARCHIVE_PARSE_ERROR_INVALID = 0, ///< Malformed or unsupported ZIP structure.
    ARCHIVE_PARSE_ERROR_OOM = 1,     ///< Parser-side allocation failed.
    ARCHIVE_PARSE_ERROR_LIMIT = 2,   ///< Configured resource ceiling was exceeded.
} archive_parse_error_t;

// Read-parser entry points (defined in rt_archive_read.c).
bool parse_central_directory(rt_archive_t *ar);
zip_entry_t *find_entry(rt_archive_t *ar, const char *name);
void *read_entry_data(rt_archive_t *ar, zip_entry_t *e);
// Shared helpers (defined in rt_archive.c).
bool archive_extra_is_malformed_or_zip64(const uint8_t *extra, size_t extra_len);
void archive_free_entry_array(zip_entry_t *entries, int count);
