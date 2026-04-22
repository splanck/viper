//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/io/rt_file_ext.c
// Purpose: High-level file helpers backing the Viper.IO.File static methods.
//          Implements ReadAllText, WriteAllText, ReadAllBytes, WriteAllBytes,
//          ReadAllLines, AppendAllText, Copy, Move, Delete, Exists, GetSize,
//          and related operations by bridging OOP-style calls to the runtime
//          file and string utilities.
//
// Key invariants:
//   - ReadAllText/ReadAllBytes read the entire file into memory in one call.
//   - WriteAllText/WriteAllBytes/WriteLines replace files atomically.
//   - Exists returns false for directories; use Dir.Exists for those.
//   - Copy does not overwrite the destination unless explicitly requested.
//   - All functions handle both POSIX and Windows file APIs transparently.
//   - Internal bytes layout is accessed directly to avoid per-byte overhead.
//
// Ownership/Lifetime:
//   - Returned strings and bytes buffers are fresh allocations owned by callers.
//   - Input strings are borrowed; this module does not retain string references.
//
// Links: src/runtime/io/rt_file_ext.h (public API),
//        src/runtime/io/rt_file.h (low-level RtFile handle and channel table),
//        src/runtime/io/rt_file_path.h (mode string conversion)
//
//===----------------------------------------------------------------------===//

#include "rt_bytes.h"
#include "rt_file.h"

/* O-02: Internal bytes layout for direct data access (avoids per-byte rt_bytes_get) */
typedef struct {
    int64_t len;
    uint8_t *data;
} file_bytes_impl;

static inline uint8_t *file_bytes_data(void *obj) {
    return obj ? ((file_bytes_impl *)obj)->data : NULL;
}

#include "rt_file_path.h"
#include "rt_internal.h"
#include "rt_seq.h"
#include "rt_string.h"

#include "rt_platform.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#if !RT_PLATFORM_WINDOWS
#include <unistd.h>
#endif

#if RT_PLATFORM_WINDOWS
#include <io.h>
#include <process.h>
#include <sys/utime.h>
#include <windows.h>
#include <wchar.h>
#else
#include <utime.h>
#endif

#if defined(O_BINARY)
#define RT_FILE_O_BINARY O_BINARY
#elif defined(_O_BINARY)
#define RT_FILE_O_BINARY _O_BINARY
#else
#define RT_FILE_O_BINARY 0
#endif

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

static char *rt_fileext_make_parent_temp_path(const char *path, const char *prefix, unsigned attempt) {
    size_t path_len = strlen(path);
    size_t parent_len = 0;
    for (size_t i = 0; i < path_len; ++i) {
#if RT_PLATFORM_WINDOWS
        if (path[i] == '/' || path[i] == '\\')
#else
        if (path[i] == '/')
#endif
            parent_len = i + 1;
    }

    size_t cap = parent_len + 96;
    char *tmp = (char *)malloc(cap);
    if (!tmp)
        return NULL;
    if (parent_len > 0)
        memcpy(tmp, path, parent_len);
#if RT_PLATFORM_WINDOWS
    unsigned long pid = (unsigned long)_getpid();
#else
    unsigned long pid = (unsigned long)getpid();
#endif
    int written =
        snprintf(tmp + parent_len, cap - parent_len, "%s%lu.%p.%u", prefix, pid, (const void *)path, attempt);
    if (written < 0 || (size_t)written >= cap - parent_len) {
        free(tmp);
        return NULL;
    }
    return tmp;
}

// =============================================================================
// Platform shims
// Each cross-platform helper wraps the OS-specific primitive with a uniform
// signature: open / stat / unlink / utime work on UTF-8 paths regardless of
// platform (Win32 detours through `rt_file_path_utf8_to_wide` so emoji in
// filenames work). All functions below use these shims rather than calling
// open/stat/etc. directly. The two #if/#else blocks deliberately duplicate
// rt_fileext_write_all_fd / _make_temp_path / _replace_utf8 — Win32 uses
// MoveFileExW for atomic-replace; POSIX uses rename(2) which is already atomic.
// =============================================================================

// On Windows, _read/_write take unsigned int; on POSIX they take size_t.
// These wrappers suppress C4267 truncation warnings on MSVC.
#if RT_PLATFORM_WINDOWS
static inline ssize_t rt_posix_read(int fd, void *buf, size_t count) {
    unsigned int chunk = count > (size_t)UINT_MAX ? UINT_MAX : (unsigned int)count;
    return read(fd, buf, chunk);
}

static inline ssize_t rt_posix_write(int fd, const void *buf, size_t count) {
    unsigned int chunk = count > (size_t)UINT_MAX ? UINT_MAX : (unsigned int)count;
    return write(fd, buf, chunk);
}
#else
#define rt_posix_read read
#define rt_posix_write write
#endif

#if RT_PLATFORM_WINDOWS
static int rt_fileext_open(const char *path, int flags, int pmode) {
    wchar_t *wide = rt_file_path_utf8_to_wide(path);
    if (!wide)
        return -1;
    int fd = _wopen(wide, flags, pmode);
    free(wide);
    return fd;
}

static int rt_fileext_stat_path(const char *path, struct stat *st) {
    wchar_t *wide = rt_file_path_utf8_to_wide(path);
    if (!wide)
        return -1;
    int rc = _wstat(wide, st);
    free(wide);
    return rc;
}

static int rt_fileext_unlink(const char *path) {
    wchar_t *wide = rt_file_path_utf8_to_wide(path);
    if (!wide)
        return -1;
    int rc = _wunlink(wide);
    free(wide);
    return rc;
}

static int rt_fileext_utime(const char *path, struct _utimbuf *times) {
    wchar_t *wide = rt_file_path_utf8_to_wide(path);
    if (!wide)
        return -1;
    int rc = _wutime(wide, times);
    free(wide);
    return rc;
}

/// @brief Write `len` bytes to `fd`, looping through short writes and EINTR.
///
/// `write()` may return fewer bytes than requested on regular files
/// (rare) or fatal-signal interrupts (EINTR), so the loop retries
/// from the running offset. A zero return (neither progress nor
/// error) is treated as failure so we don't spin forever. Returns 1
/// on a complete write, 0 on any error.
static int rt_fileext_write_all_fd(int fd, const uint8_t *data, size_t len) {
    size_t written = 0;
    while (written < len) {
        ssize_t n = rt_posix_write(fd, data + written, len - written);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return 0;
        }
        if (n == 0)
            return 0;
        written += (size_t)n;
    }
    return 1;
}

static char *rt_fileext_make_temp_path(const char *path, unsigned attempt) {
    return rt_fileext_make_parent_temp_path(path, ".viper-tmp.", attempt);
}

static int rt_fileext_replace_utf8(const char *src, const char *dst) {
    wchar_t *wsrc = rt_file_path_utf8_to_wide(src);
    wchar_t *wdst = rt_file_path_utf8_to_wide(dst);
    if (!wsrc || !wdst) {
        free(wsrc);
        free(wdst);
        return 0;
    }
    BOOL ok = MoveFileExW(wsrc, wdst, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    free(wsrc);
    free(wdst);
    return ok ? 1 : 0;
}
#else
#define rt_fileext_open open
#define rt_fileext_stat_path stat
#define rt_fileext_unlink unlink
#define rt_fileext_utime utime

/// @brief Write `len` bytes to `fd`, looping through short writes and EINTR.
///
/// `write()` may return fewer bytes than requested on regular files
/// (rare) or fatal-signal interrupts (EINTR), so the loop retries
/// from the running offset. A zero return (neither progress nor
/// error) is treated as failure so we don't spin forever. Returns 1
/// on a complete write, 0 on any error.
static int rt_fileext_write_all_fd(int fd, const uint8_t *data, size_t len) {
    size_t written = 0;
    while (written < len) {
        ssize_t n = rt_posix_write(fd, data + written, len - written);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return 0;
        }
        if (n == 0)
            return 0;
        written += (size_t)n;
    }
    return 1;
}

static char *rt_fileext_make_temp_path(const char *path, unsigned attempt) {
    return rt_fileext_make_parent_temp_path(path, ".viper-tmp.", attempt);
}

static int rt_fileext_replace_utf8(const char *src, const char *dst) {
    return rename(src, dst) == 0 ? 1 : 0;
}
#endif

/// @brief Move the staged temp file to its final destination.
///
/// When `replace=1`, clobbers any existing destination (used by
/// `WriteAllText`/`WriteAllBytes` where the user expects overwrite).
/// When `replace=0`, refuses to overwrite (used by `Move` and similar
/// no-clobber operations): Windows uses `MoveFileExW` without
/// REPLACE_EXISTING; POSIX uses `link()` + `unlink()` since `rename`
/// overwrites unconditionally. The link/unlink dance atomically
/// reserves the new name and cleans up the source, rolling back on
/// failure.
static int rt_fileext_commit_utf8(const char *src, const char *dst, int replace) {
    if (replace)
        return rt_fileext_replace_utf8(src, dst);
#if RT_PLATFORM_WINDOWS
    wchar_t *wsrc = rt_file_path_utf8_to_wide(src);
    wchar_t *wdst = rt_file_path_utf8_to_wide(dst);
    if (!wsrc || !wdst) {
        free(wsrc);
        free(wdst);
        return 0;
    }
    BOOL ok = MoveFileExW(wsrc, wdst, MOVEFILE_WRITE_THROUGH);
    free(wsrc);
    free(wdst);
    return ok ? 1 : 0;
#else
    if (link(src, dst) != 0)
        return 0;
    if (unlink(src) != 0) {
        int saved = errno;
        (void)unlink(dst);
        errno = saved;
        return 0;
    }
    return 1;
#endif
}

/// @brief Open an exclusive temp file beside `path` for atomic-write staging.
///
/// Tries up to 128 distinct `.viper-tmp.<pid>.<attempt>` sidecar names
/// until O_EXCL succeeds. O_NOFOLLOW (when available) prevents a
/// symlink attack that would redirect the write elsewhere. On success,
/// writes the chosen temp path into `*out_tmp` for the caller to rename
/// (or unlink on error). Returns the open fd, or -1 on failure with
/// errno preserved.
static int rt_fileext_open_temp_utf8(const char *path, int binary, char **out_tmp) {
    if (out_tmp)
        *out_tmp = NULL;
    for (unsigned attempt = 0; attempt < 128; ++attempt) {
        char *tmp = rt_fileext_make_temp_path(path, attempt);
        if (!tmp)
            return -1;

        int flags = O_WRONLY | O_CREAT | O_EXCL | (binary ? RT_FILE_O_BINARY : 0);
#if defined(O_NOFOLLOW)
        flags |= O_NOFOLLOW;
#endif
        int fd = rt_fileext_open(tmp, flags, 0666);
        if (fd >= 0) {
            if (out_tmp)
                *out_tmp = tmp;
            else
                free(tmp);
            return fd;
        }
        int err = errno;
        free(tmp);
        if (err != EEXIST)
            return -1;
    }
    errno = EEXIST;
    return -1;
}

/// @brief Fsync the directory containing `path` so a rename is crash-durable.
///
/// On POSIX, the rename itself hits the filesystem journal but the
/// parent directory's updated dirent doesn't necessarily reach disk
/// until its inode is fsync'd. Opens the parent with O_DIRECTORY
/// (where supported) to avoid accidentally sync'ing a regular file.
/// On Windows this is a no-op because `MoveFileExW | WRITE_THROUGH`
/// already handles durability.
static int rt_fileext_sync_parent_dir(const char *path) {
#if RT_PLATFORM_WINDOWS
    (void)path;
    return 1;
#else
    const char *last = strrchr(path, '/');
    char stack_buf[PATH_MAX];
    const char *parent = ".";
    char *heap_buf = NULL;

    if (last) {
        size_t len = (size_t)(last - path);
        if (len == 0) {
            parent = "/";
        } else if (len < sizeof(stack_buf)) {
            memcpy(stack_buf, path, len);
            stack_buf[len] = '\0';
            parent = stack_buf;
        } else {
            heap_buf = (char *)malloc(len + 1);
            if (!heap_buf)
                return 0;
            memcpy(heap_buf, path, len);
            heap_buf[len] = '\0';
            parent = heap_buf;
        }
    }

    int flags = O_RDONLY;
#ifdef O_DIRECTORY
    flags |= O_DIRECTORY;
#endif
    int fd = open(parent, flags);
    free(heap_buf);
    if (fd < 0)
        return 0;
    int ok = fsync(fd) == 0 ? 1 : 0;
    if (close(fd) != 0)
        ok = 0;
    return ok;
#endif
}

static void rt_fileext_close_or_trap(int fd, const char *context) {
    if (close(fd) != 0)
        rt_trap(context);
}

/// @brief Test whether two paths refer to the same on-disk inode/file.
///
/// Used by `Copy` to short-circuit self-copies (which would truncate
/// the file when the temp-rename step overwrote the source). On POSIX
/// compares (dev, ino); on Windows compares
/// (volume, fileIndexHigh, fileIndexLow). Returns 0 if either path
/// can't be stat'd — treating inaccessible paths as distinct so the
/// copy attempt can fail with a clearer error.
static int rt_fileext_same_existing_file(const char *src_path, const char *dst_path) {
#if RT_PLATFORM_WINDOWS
    wchar_t *wsrc = rt_file_path_utf8_to_wide(src_path);
    wchar_t *wdst = rt_file_path_utf8_to_wide(dst_path);
    if (!wsrc || !wdst) {
        free(wsrc);
        free(wdst);
        return 0;
    }

    HANDLE src = CreateFileW(wsrc,
                             0,
                             FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                             NULL,
                             OPEN_EXISTING,
                             FILE_ATTRIBUTE_NORMAL,
                             NULL);
    HANDLE dst = CreateFileW(wdst,
                             0,
                             FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                             NULL,
                             OPEN_EXISTING,
                             FILE_ATTRIBUTE_NORMAL,
                             NULL);
    free(wsrc);
    free(wdst);
    if (src == INVALID_HANDLE_VALUE) {
        if (dst != INVALID_HANDLE_VALUE)
            CloseHandle(dst);
        return 0;
    }
    if (dst == INVALID_HANDLE_VALUE) {
        CloseHandle(src);
        return 0;
    }

    BY_HANDLE_FILE_INFORMATION si;
    BY_HANDLE_FILE_INFORMATION di;
    int same = 0;
    if (GetFileInformationByHandle(src, &si) && GetFileInformationByHandle(dst, &di)) {
        same = si.dwVolumeSerialNumber == di.dwVolumeSerialNumber &&
               si.nFileIndexHigh == di.nFileIndexHigh && si.nFileIndexLow == di.nFileIndexLow;
    }
    CloseHandle(src);
    CloseHandle(dst);
    return same;
#else
    struct stat src_st;
    struct stat dst_st;
    if (stat(src_path, &src_st) != 0)
        return 0;
    if (stat(dst_path, &dst_st) != 0)
        return 0;
    return src_st.st_dev == dst_st.st_dev && src_st.st_ino == dst_st.st_ino;
#endif
}

static int rt_fileext_is_regular_mode(mode_t mode) {
#if RT_PLATFORM_WINDOWS
    return (mode & _S_IFREG) != 0;
#else
    return S_ISREG(mode) ? 1 : 0;
#endif
}

/// @brief **Atomic-write to disk:** write to an exclusive temp sidecar, fsync (POSIX) or
/// _commit (Win32), close, atomically rename over the destination, and fsync the parent
/// directory on POSIX so the name replacement is crash-durable.
static int rt_fileext_write_atomic_utf8(const char *path, const uint8_t *data, size_t len, int binary) {
    char *tmp = NULL;
    int fd = rt_fileext_open_temp_utf8(path, binary, &tmp);
    if (fd < 0)
        return 0;

    int ok = rt_fileext_write_all_fd(fd, data, len);
#if RT_PLATFORM_WINDOWS
    if (ok && _commit(fd) != 0)
        ok = 0;
#else
    if (ok && fsync(fd) != 0)
        ok = 0;
#endif
    if (close(fd) != 0)
        ok = 0;
    if (ok)
        ok = rt_fileext_replace_utf8(tmp, path);
    if (ok)
        ok = rt_fileext_sync_parent_dir(path);
    if (!ok)
        (void)rt_fileext_unlink(tmp);
    free(tmp);
    return ok;
}

/// @brief Convert a runtime string path to a host path; traps on failure.
/// @param path Runtime string containing the path.
/// @param context Trap message to use when conversion fails.
/// @return Null-terminated host path.
/// @brief Validate a path argument: trap with `context` on NULL/empty input. Used as the first
/// line of every public file operation to give specific error messages.
static const char *rt_io_file_require_path(rt_string path, const char *context) {
    const char *cpath = NULL;
    if (!rt_file_path_from_vstr(path, &cpath) || !cpath)
        rt_trap(context);
    return cpath;
}

/// What: Return 1 if the file at @p path exists, 0 otherwise.
/// Why:  Support Viper.IO.File.Exists semantics from the runtime.
/// How:  Converts @p path to a host path and calls stat().
/// @brief Returns 1 if `path` exists (file or directory), 0 otherwise. Single `stat` call.
int64_t rt_io_file_exists(rt_string path) {
    const char *cpath = NULL;
    if (!rt_file_path_from_vstr(path, &cpath) || !cpath)
        return 0;
    struct stat st;
    if (rt_fileext_stat_path(cpath, &st) == 0) {
#ifdef _WIN32
        return (st.st_mode & _S_IFREG) != 0;
#else
        return S_ISREG(st.st_mode);
#endif
    }
    return 0;
}

/// What: Read entire file into a runtime string. Return empty on error.
/// Why:  Provide a convenience API for small text files in examples/tests.
/// How:  Opens the file, reads all bytes, returns an rt_string view of them.
/// @brief Read the entire file as UTF-8 text. Atomically streams the file into a single
/// rt_string allocation. Traps on NULL path or read failure.
rt_string rt_io_file_read_all_text(rt_string path) {
    const char *cpath =
        rt_io_file_require_path(path, "Viper.IO.File.ReadAllText: invalid file path");

    int fd = rt_fileext_open(cpath, O_RDONLY | RT_FILE_O_BINARY, 0);
    if (fd < 0)
        rt_trap("Viper.IO.File.ReadAllText: failed to open file");

    struct stat st;
    if (fstat(fd, &st) != 0) {
        close(fd);
        rt_trap("Viper.IO.File.ReadAllText: failed to stat file");
    }
    if (!rt_fileext_is_regular_mode(st.st_mode)) {
        close(fd);
        rt_trap("Viper.IO.File.ReadAllText: path is not a regular file");
    }
    if (st.st_size < 0 || (uint64_t)st.st_size > (uint64_t)SIZE_MAX) {
        close(fd);
        rt_trap("Viper.IO.File.ReadAllText: file too large");
    }
    size_t size = (st.st_size > 0) ? (size_t)st.st_size : 0;
    // Handle empty files
    if (size == 0) {
        close(fd);
        return rt_str_empty();
    }

    char *buf = (char *)malloc(size);
    if (!buf) {
        close(fd);
        rt_trap("Viper.IO.File.ReadAllText: allocation failed");
    }

    size_t off = 0;
    while (off < size) {
        ssize_t n = rt_posix_read(fd, buf + off, size - off);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            free(buf);
            close(fd);
            rt_trap("Viper.IO.File.ReadAllText: failed to read file");
        }
        if (n == 0)
            break;
        off += (size_t)n;
    }
    close(fd);
    // If short read, shrink to actual bytes read.
    rt_string s = rt_string_from_bytes(buf, off);
    free(buf);
    if (!s)
        rt_trap("Viper.IO.File.ReadAllText: allocation failed");
    return s;
}

/// What: Write @p contents to @p path, replacing the file atomically.
/// Why:  Complement read_all_text with a simple write primitive.
/// How:  Writes an exclusive temp sidecar, flushes it, then replaces the destination.
/// @brief Atomically write `contents` (UTF-8) to `path`, replacing any existing file. Uses
/// `_write_atomic_utf8` so an interrupted write can never corrupt the destination — readers
/// see either the old file or the new file, never a partial write.
void rt_io_file_write_all_text(rt_string path, rt_string contents) {
    const char *cpath =
        rt_io_file_require_path(path, "Viper.IO.File.WriteAllText: invalid file path");

    const uint8_t *data = NULL;
    size_t len = rt_file_string_view(contents, &data);
    if (!rt_fileext_write_atomic_utf8(cpath, data, len, 1))
        rt_trap("Viper.IO.File.WriteAllText: failed to write file");
}

/// What: Append @p text and a newline to @p path (creating it when missing).
/// Why:  Provide a convenient "append line" helper for Viper.IO.File.
/// How:  Opens with O_APPEND and writes the UTF-8 bytes followed by '\n'.
/// @brief Append `text` (with auto-added LF) to the end of a file. Open with O_APPEND so
/// concurrent appends from multiple processes don't interleave (POSIX guarantees atomicity for
/// writes < PIPE_BUF). Creates the file if it doesn't exist.
void rt_io_file_append_line(rt_string path, rt_string text) {
    const char *cpath =
        rt_io_file_require_path(path, "Viper.IO.File.AppendLine: invalid file path");

    int fd = rt_fileext_open(cpath, O_WRONLY | O_CREAT | O_APPEND | RT_FILE_O_BINARY, 0666);
    if (fd < 0)
        rt_trap("Viper.IO.File.AppendLine: failed to open file");

    const uint8_t *data = NULL;
    size_t len = rt_file_string_view(text, &data);
    size_t written = 0;
    while (written < len) {
        ssize_t n = rt_posix_write(fd, data + written, len - written);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            (void)close(fd);
            rt_trap("Viper.IO.File.AppendLine: failed to write file");
        }
        if (n == 0) {
            (void)close(fd);
            rt_trap("Viper.IO.File.AppendLine: failed to write file");
        }
        written += (size_t)n;
    }

    char nl = '\n';
    ssize_t n;
    do {
        n = rt_posix_write(fd, &nl, 1);
    } while (n < 0 && errno == EINTR);
    if (n != 1) {
        (void)close(fd);
        rt_trap("Viper.IO.File.AppendLine: failed to write newline");
    }

    rt_fileext_close_or_trap(fd, "Viper.IO.File.AppendLine: failed to close file");
}

/// What: Read the entire file at @p path as a Bytes object.
/// Why:  Provide binary file input for Viper.IO.File.ReadAllBytes.
/// How:  Reads the file into a temporary buffer and copies it into a new Bytes.
/// @brief Read the entire file as raw Bytes (no text decoding). Returns a Bytes object sized to
/// match the actual file length on disk.
void *rt_io_file_read_all_bytes(rt_string path) {
    const char *cpath =
        rt_io_file_require_path(path, "Viper.IO.File.ReadAllBytes: invalid file path");

    int fd = rt_fileext_open(cpath, O_RDONLY | RT_FILE_O_BINARY, 0);
    if (fd < 0)
        rt_trap("Viper.IO.File.ReadAllBytes: failed to open file");

    struct stat st;
    if (fstat(fd, &st) != 0) {
        (void)close(fd);
        rt_trap("Viper.IO.File.ReadAllBytes: failed to stat file");
    }
    if (!rt_fileext_is_regular_mode(st.st_mode)) {
        (void)close(fd);
        rt_trap("Viper.IO.File.ReadAllBytes: path is not a regular file");
    }
    if (st.st_size < 0 || (uint64_t)st.st_size > (uint64_t)SIZE_MAX) {
        (void)close(fd);
        rt_trap("Viper.IO.File.ReadAllBytes: file too large");
    }

    size_t size = (st.st_size > 0) ? (size_t)st.st_size : 0;
    if (size == 0) {
        (void)close(fd);
        return rt_bytes_new(0);
    }

    uint8_t *buf = (uint8_t *)malloc(size);
    if (!buf) {
        (void)close(fd);
        rt_trap("Viper.IO.File.ReadAllBytes: allocation failed");
    }

    size_t off = 0;
    while (off < size) {
        ssize_t n = rt_posix_read(fd, buf + off, size - off);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            free(buf);
            (void)close(fd);
            rt_trap("Viper.IO.File.ReadAllBytes: failed to read file");
        }
        if (n == 0)
            break;
        off += (size_t)n;
    }
    (void)close(fd);

    /* O-02: Use memcpy into the raw bytes buffer instead of per-byte rt_bytes_set */
    void *bytes = rt_bytes_new((int64_t)off);
    uint8_t *dst = file_bytes_data(bytes);
    if (dst)
        memcpy(dst, buf, off);

    free(buf);
    return bytes;
}

/// What: Write an entire Bytes object to @p path, overwriting the file.
/// Why:  Provide binary file output for Viper.IO.File.WriteAllBytes.
/// How:  Writes bytes in chunks to avoid per-byte syscalls.
/// @brief Atomically write raw Bytes to `path`. Same atomic-replace semantics as `_write_all_text`
/// but skips text encoding conversion.
void rt_io_file_write_all_bytes(rt_string path, void *bytes) {
    const char *cpath =
        rt_io_file_require_path(path, "Viper.IO.File.WriteAllBytes: invalid file path");

    if (!bytes)
        rt_trap("Viper.IO.File.WriteAllBytes: null Bytes");

    /* IO-H-1: use raw data pointer instead of per-byte rt_bytes_get() —
       eliminates O(n) function calls in favour of a single write() */
    int64_t len = rt_bytes_len(bytes);
    const uint8_t *src = file_bytes_data(bytes);
    if (!rt_fileext_write_atomic_utf8(cpath, src, (size_t)len, 1))
        rt_trap("Viper.IO.File.WriteAllBytes: failed to write file");
}

/// What: Read a text file and return a Seq of lines.
/// Why:  Provide convenient line-based file input for Viper.IO.File.ReadAllLines.
/// How:  Reads the file and splits on '\n' and '\r\n', stripping line terminators.
/// @brief Read a file, split on LF/CRLF, return a Seq of UTF-8 rt_strings (one per line, no
/// trailing newline). Empty trailing lines are preserved (a file ending in `\n\n` yields a
/// trailing empty string).
void *rt_io_file_read_all_lines(rt_string path) {
    const char *cpath =
        rt_io_file_require_path(path, "Viper.IO.File.ReadAllLines: invalid file path");

    int fd = rt_fileext_open(cpath, O_RDONLY | RT_FILE_O_BINARY, 0);
    if (fd < 0)
        rt_trap("Viper.IO.File.ReadAllLines: failed to open file");

    struct stat st;
    if (fstat(fd, &st) != 0) {
        (void)close(fd);
        rt_trap("Viper.IO.File.ReadAllLines: failed to stat file");
    }
    if (!rt_fileext_is_regular_mode(st.st_mode)) {
        (void)close(fd);
        rt_trap("Viper.IO.File.ReadAllLines: path is not a regular file");
    }
    if (st.st_size < 0 || (uint64_t)st.st_size > (uint64_t)SIZE_MAX) {
        (void)close(fd);
        rt_trap("Viper.IO.File.ReadAllLines: file too large");
    }

    size_t size = (st.st_size > 0) ? (size_t)st.st_size : 0;
    if (size == 0) {
        (void)close(fd);
        void *empty = rt_seq_new();
        rt_seq_set_owns_elements(empty, 1);
        return empty;
    }

    char *buf = (char *)malloc(size);
    if (!buf) {
        (void)close(fd);
        rt_trap("Viper.IO.File.ReadAllLines: allocation failed");
    }

    size_t off = 0;
    while (off < size) {
        ssize_t n = rt_posix_read(fd, buf + off, size - off);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            free(buf);
            (void)close(fd);
            rt_trap("Viper.IO.File.ReadAllLines: failed to read file");
        }
        if (n == 0)
            break;
        off += (size_t)n;
    }
    (void)close(fd);

    void *seq = rt_seq_new();
    rt_seq_set_owns_elements(seq, 1);
    size_t i = 0;
    while (i < off) {
        size_t start = i;
        while (i < off && buf[i] != '\n' && buf[i] != '\r')
            ++i;
        size_t end = i;

        rt_string line =
            (end == start) ? rt_str_empty() : rt_string_from_bytes(buf + start, end - start);
        rt_seq_push(seq, line);
        rt_string_unref(line);

        if (i >= off)
            break;
        if (buf[i] == '\r') {
            if (i + 1 < off && buf[i + 1] == '\n')
                i += 2;
            else
                i += 1;
        } else {
            ++i; // '\n'
        }
        if (i == off) {
            rt_string trailing = rt_str_empty();
            rt_seq_push(seq, trailing);
            rt_string_unref(trailing);
        }
    }

    free(buf);
    return seq;
}

/// What: Delete the file at @p path.
/// Why:  Allow simple cleanup without surfacing platform-specific APIs.
/// How:  Converts to host path and calls unlink(); errors are ignored.
/// @brief Delete a file. Trap if the path is null/empty; silently succeeds if the file is missing.
void rt_io_file_delete(rt_string path) {
    const char *cpath =
        rt_io_file_require_path(path, "Viper.IO.File.Delete: invalid file path");
    if (rt_fileext_unlink(cpath) != 0 && errno != ENOENT)
        rt_trap("Viper.IO.File.Delete: failed to delete file");
}

/// @brief Core file-copy routine used by `File.Copy` and `File.CopyOver`.
///
/// Copies via a staging temp file + atomic rename so a crash mid-copy
/// never leaves a truncated destination. Path validation, same-file
/// short-circuit, regular-file check, and non-clobber policing all
/// happen up front before any write. The transfer itself uses an 8KB
/// stack buffer — large enough to amortize syscall overhead on fast
/// storage, small enough to keep stack use predictable. `replace` chooses
/// between overwrite (`Copy`) and fail-if-exists (`CopyOver` inverted).
static void rt_file_copy_impl(rt_string src, rt_string dst, int replace) {
    const char *src_path = rt_io_file_require_path(src, "File.Copy: invalid source path");
    const char *dst_path = rt_io_file_require_path(dst, "File.Copy: invalid destination path");

    int src_fd = rt_fileext_open(src_path, O_RDONLY | RT_FILE_O_BINARY, 0);
    if (src_fd < 0) {
        char msg[512];
        snprintf(
            msg, sizeof(msg), "File.Copy: cannot open source '%s': %s", src_path, strerror(errno));
        rt_trap(msg);
        return;
    }

    if (rt_fileext_same_existing_file(src_path, dst_path)) {
        close(src_fd);
        rt_trap("File.Copy: source and destination are the same file");
        return;
    }

    struct stat src_st;
    if (fstat(src_fd, &src_st) != 0 || !rt_fileext_is_regular_mode(src_st.st_mode)) {
        close(src_fd);
        rt_trap("File.Copy: source is not a regular file");
        return;
    }

    if (!replace) {
        struct stat dst_st;
        if (rt_fileext_stat_path(dst_path, &dst_st) == 0) {
            close(src_fd);
            rt_trap("File.Copy: destination already exists");
            return;
        }
    }

    char *tmp_path = NULL;
    int dst_fd = rt_fileext_open_temp_utf8(dst_path, 1, &tmp_path);
    if (dst_fd < 0) {
        close(src_fd);
        char msg[512];
        snprintf(msg,
                 sizeof(msg),
                 "File.Copy: cannot create temporary destination for '%s': %s",
                 dst_path,
                 strerror(errno));
        rt_trap(msg);
        return;
    }

    char buf[8192];
    for (;;) {
        ssize_t n = rt_posix_read(src_fd, buf, sizeof(buf));
        if (n < 0) {
            if (errno == EINTR)
                continue;
            close(src_fd);
            close(dst_fd);
            if (tmp_path)
                (void)rt_fileext_unlink(tmp_path);
            free(tmp_path);
            rt_trap("File.Copy: read error");
            return;
        }
        if (n == 0)
            break;
        size_t written = 0;
        while (written < (size_t)n) {
            ssize_t w = rt_posix_write(dst_fd, buf + written, (size_t)n - written);
            if (w < 0) {
                if (errno == EINTR)
                    continue;
                close(src_fd);
                close(dst_fd);
                if (tmp_path)
                    (void)rt_fileext_unlink(tmp_path);
                free(tmp_path);
                rt_trap("File.Copy: write error (disk full or I/O error)");
                return;
            }
            if (w == 0) {
                close(src_fd);
                close(dst_fd);
                if (tmp_path)
                    (void)rt_fileext_unlink(tmp_path);
                free(tmp_path);
                rt_trap("File.Copy: write error (zero-byte write)");
                return;
            }
            written += (size_t)w;
        }
    }

    int ok = 1;
#if RT_PLATFORM_WINDOWS
    if (_commit(dst_fd) != 0)
        ok = 0;
#else
    if (fsync(dst_fd) != 0)
        ok = 0;
#endif
    if (close(src_fd) != 0)
        ok = 0;
    if (close(dst_fd) != 0)
        ok = 0;
    if (ok)
        ok = rt_fileext_commit_utf8(tmp_path, dst_path, replace);
    if (ok)
        ok = rt_fileext_sync_parent_dir(dst_path);
    if (!ok) {
        if (tmp_path)
            (void)rt_fileext_unlink(tmp_path);
        free(tmp_path);
        rt_trap("File.Copy: failed to commit destination");
        return;
    }
    free(tmp_path);
}

/// What: Copy a file from @p src to @p dst.
/// Why:  Allow file duplication without platform-specific APIs.
/// How:  Reads src file and writes to dst file.
/// @brief Copy file `src` to `dst`. Streams in chunks to avoid loading the whole file into RAM
/// (important for large files). Traps if the destination already exists.
void rt_file_copy(rt_string src, rt_string dst) {
    rt_file_copy_impl(src, dst, 0);
}

/// What: Move/rename a file from @p src to @p dst.
/// Why:  Allow file relocation without platform-specific APIs.
/// How:  Uses rename(); falls back to copy+delete if needed.
/// @brief Move file `src` to `dst`. Tries `rename` first (atomic, same filesystem); falls back to
/// copy + delete on EXDEV (cross-filesystem rename failure).
void rt_file_move(rt_string src, rt_string dst) {
    const char *src_path =
        rt_io_file_require_path(src, "Viper.IO.File.Move: invalid source path");
    const char *dst_path =
        rt_io_file_require_path(dst, "Viper.IO.File.Move: invalid destination path");

    if (rt_fileext_replace_utf8(src_path, dst_path))
        return;

#if RT_PLATFORM_WINDOWS
    DWORD move_err = GetLastError();
    if (move_err != ERROR_NOT_SAME_DEVICE) {
        rt_trap("File.Move: failed to move file");
        return;
    }
#else
    if (errno != EXDEV) {
        rt_trap("File.Move: failed to move file");
        return;
    }
#endif

    rt_file_copy_impl(src, dst, 1);
    if (rt_fileext_unlink(src_path) != 0)
        rt_trap("File.Move: failed to remove source after cross-device copy");
}

/// What: Get the size of a file in bytes.
/// Why:  Allow querying file size without opening the file.
/// How:  Uses stat() to get file size.
/// @brief Return the size of `path` in bytes (via `stat`). 0 on missing path.
int64_t rt_file_size(rt_string path) {
    const char *cpath = NULL;
    if (!rt_file_path_from_vstr(path, &cpath) || !cpath)
        return -1;

    struct stat st;
    if (rt_fileext_stat_path(cpath, &st) != 0)
        return -1;
    if (!rt_fileext_is_regular_mode(st.st_mode))
        return -1;

    return (int64_t)st.st_size;
}

/// What: Read entire file as a Bytes object.
/// Why:  Support binary file reading.
/// How:  Opens file, reads all bytes, returns Bytes object.
/// @brief Alias for `rt_io_file_read_all_bytes` — reads the whole file as a Bytes object.
void *rt_file_read_bytes(rt_string path) {
    return rt_io_file_read_all_bytes(path);
}

/// What: Write a Bytes object to a file.
/// Why:  Support binary file writing.
/// How:  Opens file, writes all bytes from Bytes object using chunked writes.
/// @brief Alias for `rt_io_file_write_all_bytes` — atomically write Bytes to disk.
void rt_file_write_bytes(rt_string path, void *bytes) {
    rt_io_file_write_all_bytes(path, bytes);
}

/// What: Read entire file as a sequence of lines.
/// Why:  Support line-by-line text file reading.
/// How:  Reads file, splits by newlines, returns Seq of strings.
/// @brief Alias for `rt_io_file_read_all_lines` — read the file split into lines.
void *rt_file_read_lines(rt_string path) {
    return rt_io_file_read_all_lines(path);
}

/// What: Write a sequence of strings to a file as lines.
/// Why:  Support line-by-line text file writing.
/// How:  Writes each string followed by newline.
/// @brief Atomically write a Seq of strings as lines (joined with LF). Each element becomes one
/// line; trailing newline is added to the final line so future appends concatenate correctly.
void rt_file_write_lines(rt_string path, void *lines) {
    const char *cpath =
        rt_io_file_require_path(path, "Viper.IO.File.WriteLines: invalid file path");

    if (!lines) {
        rt_trap("Viper.IO.File.WriteLines: null lines");
        return;
    }

    char *tmp_path = NULL;
    int fd = rt_fileext_open_temp_utf8(cpath, 1, &tmp_path);
    if (fd < 0) {
        rt_trap("Viper.IO.File.WriteLines: failed to open file");
        return;
    }

    int ok = 1;
    int64_t count = rt_seq_len(lines);
    for (int64_t i = 0; i < count; i++) {
        rt_string line = (rt_string)rt_seq_get(lines, i);
        if (line) {
            const uint8_t *data = NULL;
            size_t len = rt_file_string_view(line, &data);
            if (!rt_fileext_write_all_fd(fd, data, len)) {
                ok = 0;
                break;
            }
        }
        // Write newline
        char nl = '\n';
        if (!rt_fileext_write_all_fd(fd, (const uint8_t *)&nl, 1)) {
            ok = 0;
            break;
        }
    }

#if RT_PLATFORM_WINDOWS
    if (ok && _commit(fd) != 0)
        ok = 0;
#else
    if (ok && fsync(fd) != 0)
        ok = 0;
#endif
    if (close(fd) != 0)
        ok = 0;
    if (ok)
        ok = rt_fileext_replace_utf8(tmp_path, cpath);
    if (ok)
        ok = rt_fileext_sync_parent_dir(cpath);
    if (!ok) {
        (void)rt_fileext_unlink(tmp_path);
        free(tmp_path);
        rt_trap("Viper.IO.File.WriteLines: failed to write file");
        return;
    }
    free(tmp_path);
}

/// What: Append text to an existing file.
/// Why:  Support appending without reading+writing entire file.
/// How:  Opens file with O_APPEND and writes text.
/// @brief Append `text` (no newline added) to the end of a file. Like `_append_line` but doesn't
/// add a trailing LF — useful for binary-style appends.
void rt_file_append(rt_string path, rt_string text) {
    const char *cpath =
        rt_io_file_require_path(path, "Viper.IO.File.Append: invalid file path");

    int fd = rt_fileext_open(cpath, O_WRONLY | O_CREAT | O_APPEND | RT_FILE_O_BINARY, 0666);
    if (fd < 0)
        rt_trap("Viper.IO.File.Append: failed to open file");

    const uint8_t *data = NULL;
    size_t len = rt_file_string_view(text, &data);
    if (!rt_fileext_write_all_fd(fd, data, len)) {
        close(fd);
        rt_trap("Viper.IO.File.Append: failed to write file");
    }

    rt_fileext_close_or_trap(fd, "Viper.IO.File.Append: failed to close file");
}

/// What: Get file modification time as Unix timestamp.
/// Why:  Support querying when a file was last modified.
/// How:  Uses stat() to get mtime.
/// @brief Return the file's mtime as Unix epoch seconds (`stat.st_mtime`). 0 if missing.
int64_t rt_file_modified(rt_string path) {
    const char *cpath = NULL;
    if (!rt_file_path_from_vstr(path, &cpath) || !cpath)
        return 0;

    struct stat st;
    if (rt_fileext_stat_path(cpath, &st) != 0)
        return 0;

    return (int64_t)st.st_mtime;
}

/// What: Create file or update modification time.
/// Why:  Support "touch" semantics from Unix.
/// How:  Creates file if not exists, updates mtime if exists.
/// @brief Update the file's mtime+atime to "now" (`utime(NULL)`). Creates an empty file if it
/// doesn't exist. Mirrors the Unix `touch` command.
void rt_file_touch(rt_string path) {
    const char *cpath = rt_io_file_require_path(path, "Viper.IO.File.Touch: invalid file path");

    // Try to update mtime (works if file exists)
    if (rt_fileext_utime(cpath, NULL) == 0)
        return;
    if (errno != ENOENT)
        rt_trap("Viper.IO.File.Touch: failed to update file time");

    // File doesn't exist, create it
    int fd = rt_fileext_open(cpath, O_WRONLY | O_CREAT | RT_FILE_O_BINARY, 0666);
    if (fd < 0)
        rt_trap("Viper.IO.File.Touch: failed to create file");
    rt_fileext_close_or_trap(fd, "Viper.IO.File.Touch: failed to close file");
}
