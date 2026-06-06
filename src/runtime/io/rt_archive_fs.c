//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/io/rt_archive_fs.c
// Purpose: Filesystem and atomic-write adapter for the archive runtime. Holds
//          the Win32/POSIX file I/O primitives (exact reads, temp-file create +
//          rename, directory creation, symlink rejection) used by archive
//          reading/extraction. Split out of rt_archive.c.
//
// Key invariants:
//   - Approved io/ platform-adapter layer: raw _WIN32 branching is permitted.
//   - Atomic writes go through a temp file + rename and fsync the parent dir.
//   - Path components are checked for reparse points / symlinks so extraction
//     cannot escape the destination root.
//
// Ownership/Lifetime:
//   - Borrows caller-owned paths/handles; Bytes-producing helpers return fresh
//     GC objects owned by the caller. Closes any handle/fd it opens.
//
// Links: src/runtime/io/rt_archive.c (core/ZIP/API),
//        src/runtime/io/rt_archive_internal.h (shared contract)
//
//===----------------------------------------------------------------------===//

#include "rt_archive.h"
#include "rt_archive_internal.h"

#include "rt_box.h"
#include "rt_bytes.h"
#include "rt_dir.h"
#include "rt_file_path.h"
#include "rt_internal.h"
#include "rt_object.h"
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

// Trivial Bytes accessors — defined per-TU as static inline (not shared via the
// internal header) to avoid generic-name link collisions and rtgen header scan.
/// @brief Direct pointer to the raw byte buffer of a Bytes GC object.
static inline uint8_t *bytes_data(void *obj) {
    return rt_bytes_data(obj);
}

/// @brief Byte count of a Bytes GC object.
static inline int64_t bytes_len(void *obj) {
    return rt_bytes_len(obj);
}

//=============================================================================
// Filesystem / atomic-write helpers
//=============================================================================

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
HANDLE archive_open_win_path(const char *cpath,
                                    DWORD access,
                                    DWORD share,
                                    DWORD create_disp) {
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
static int archive_read_exact_win(HANDLE h, uint8_t *dst, size_t total, const char *trap_msg) {
    size_t read_total = 0;
    while (read_total < total) {
        DWORD chunk = 0;
        size_t remaining = total - read_total;
        DWORD want = remaining > (size_t)UINT32_MAX ? (DWORD)UINT32_MAX : (DWORD)remaining;
        if (!ReadFile(h, dst + read_total, want, &chunk, NULL) || chunk == 0) {
            rt_trap(trap_msg);
            return 0;
        }
        read_total += (size_t)chunk;
    }
    return 1;
}

int archive_read_exact_win_or_free(HANDLE h,
                                          uint8_t *dst,
                                          size_t total,
                                          const char *trap_msg) {
    jmp_buf recovery;
    rt_trap_set_recovery(&recovery);
    if (setjmp(recovery) != 0) {
        char saved_error[256];
        archive_save_trap_error(saved_error, sizeof(saved_error), trap_msg);
        rt_trap_clear_recovery();
        CloseHandle(h);
        free(dst);
        rt_trap(saved_error);
        return 0;
    }

    if (!archive_read_exact_win(h, dst, total, trap_msg)) {
        rt_trap_clear_recovery();
        CloseHandle(h);
        free(dst);
        return 0;
    }
    rt_trap_clear_recovery();
    CloseHandle(h);
    return 1;
}

int archive_read_exact_win_or_release_object(HANDLE h,
                                                    void *bytes,
                                                    size_t total,
                                                    const char *trap_msg) {
    jmp_buf recovery;
    rt_trap_set_recovery(&recovery);
    if (setjmp(recovery) != 0) {
        char saved_error[256];
        archive_save_trap_error(saved_error, sizeof(saved_error), trap_msg);
        rt_trap_clear_recovery();
        CloseHandle(h);
        archive_release_temp_object(bytes);
        rt_trap(saved_error);
        return 0;
    }

    if (!bytes || !archive_read_exact_win(h, bytes_data(bytes), total, trap_msg)) {
        rt_trap_clear_recovery();
        CloseHandle(h);
        archive_release_temp_object(bytes);
        return 0;
    }
    rt_trap_clear_recovery();
    CloseHandle(h);
    return 1;
}

void *archive_bytes_new_win_or_close(HANDLE h, int64_t len, const char *fallback) {
    jmp_buf recovery;
    rt_trap_set_recovery(&recovery);
    if (setjmp(recovery) != 0) {
        char saved_error[256];
        archive_save_trap_error(saved_error, sizeof(saved_error), fallback);
        rt_trap_clear_recovery();
        CloseHandle(h);
        rt_trap(saved_error);
        return NULL;
    }

    void *data = rt_bytes_new(len);
    if (!data) {
        rt_trap_clear_recovery();
        CloseHandle(h);
        return NULL;
    }
    rt_trap_clear_recovery();
    return data;
}

/// @brief Write exactly `total` bytes to a Windows handle or trap.
///
/// Mirror of `archive_read_exact_win` for the write path. Loops over
/// WriteFile, chunking by DWORD_MAX, and traps on any short write or
/// failure so callers don't need to invent their own retry logic.
static int archive_write_exact_win(HANDLE h,
                                   const uint8_t *src,
                                   size_t total,
                                   const char *trap_msg) {
    size_t written_total = 0;
    while (written_total < total) {
        DWORD chunk = 0;
        size_t remaining = total - written_total;
        DWORD want = remaining > (size_t)UINT32_MAX ? (DWORD)UINT32_MAX : (DWORD)remaining;
        if (!WriteFile(h, src + written_total, want, &chunk, NULL) || chunk == 0) {
            rt_trap(trap_msg);
            return 0;
        }
        written_total += (size_t)chunk;
    }
    return 1;
}
#else
int archive_open_posix(const char *path, int flags, mode_t mode) {
#ifdef O_CLOEXEC
    flags |= O_CLOEXEC;
#endif
    int fd = open(path, flags, mode);
#if defined(FD_CLOEXEC) && !defined(O_CLOEXEC)
    if (fd >= 0) {
        int fd_flags = fcntl(fd, F_GETFD);
        if (fd_flags >= 0)
            (void)fcntl(fd, F_SETFD, fd_flags | FD_CLOEXEC);
    }
#endif
    return fd;
}

/// @brief Read exactly `total` bytes from a POSIX fd or trap.
///
/// Loops over `read(2)` retrying EINTR. A zero return is treated as
/// EOF and triggers the trap so partial archive reads cannot succeed
/// silently. The fd is left positioned just after the last byte read.
static int archive_read_exact_posix(int fd, uint8_t *dst, size_t total, const char *trap_msg) {
    size_t read_total = 0;
    while (read_total < total) {
        ssize_t n = read(fd, dst + read_total, total - read_total);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            rt_trap(trap_msg);
            return 0;
        }
        if (n == 0) {
            rt_trap(trap_msg);
            return 0;
        }
        read_total += (size_t)n;
    }
    return 1;
}

int archive_read_exact_posix_or_free(int fd,
                                            uint8_t *dst,
                                            size_t total,
                                            const char *trap_msg) {
    jmp_buf recovery;
    rt_trap_set_recovery(&recovery);
    if (setjmp(recovery) != 0) {
        char saved_error[256];
        archive_save_trap_error(saved_error, sizeof(saved_error), trap_msg);
        rt_trap_clear_recovery();
        close(fd);
        free(dst);
        rt_trap(saved_error);
        return 0;
    }

    if (!archive_read_exact_posix(fd, dst, total, trap_msg)) {
        rt_trap_clear_recovery();
        close(fd);
        free(dst);
        return 0;
    }
    rt_trap_clear_recovery();
    close(fd);
    return 1;
}

int archive_read_exact_posix_or_release_object(int fd,
                                                      void *bytes,
                                                      size_t total,
                                                      const char *trap_msg) {
    jmp_buf recovery;
    rt_trap_set_recovery(&recovery);
    if (setjmp(recovery) != 0) {
        char saved_error[256];
        archive_save_trap_error(saved_error, sizeof(saved_error), trap_msg);
        rt_trap_clear_recovery();
        close(fd);
        archive_release_temp_object(bytes);
        rt_trap(saved_error);
        return 0;
    }

    if (!bytes || !archive_read_exact_posix(fd, bytes_data(bytes), total, trap_msg)) {
        rt_trap_clear_recovery();
        close(fd);
        archive_release_temp_object(bytes);
        return 0;
    }
    rt_trap_clear_recovery();
    close(fd);
    return 1;
}

void *archive_bytes_new_posix_or_close(int fd, int64_t len, const char *fallback) {
    jmp_buf recovery;
    rt_trap_set_recovery(&recovery);
    if (setjmp(recovery) != 0) {
        char saved_error[256];
        archive_save_trap_error(saved_error, sizeof(saved_error), fallback);
        rt_trap_clear_recovery();
        close(fd);
        rt_trap(saved_error);
        return NULL;
    }

    void *data = rt_bytes_new(len);
    if (!data) {
        rt_trap_clear_recovery();
        close(fd);
        return NULL;
    }
    rt_trap_clear_recovery();
    return data;
}

/// @brief Write exactly `total` bytes to a POSIX fd or trap.
///
/// Mirror of `archive_read_exact_posix` for writes. Retries EINTR,
/// traps on error or unexpected zero-byte writes (the kernel never
/// returns zero on a regular file write unless the disk is full).
static int archive_write_exact_posix(int fd,
                                     const uint8_t *src,
                                     size_t total,
                                     const char *trap_msg) {
    size_t written_total = 0;
    while (written_total < total) {
        ssize_t n = write(fd, src + written_total, total - written_total);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            rt_trap(trap_msg);
            return 0;
        }
        if (n == 0) {
            rt_trap(trap_msg);
            return 0;
        }
        written_total += (size_t)n;
    }
    return 1;
}
#endif

static uint64_t archive_random_u64(unsigned attempt) {
    uint64_t value = 0;
#ifdef _WIN32
    value = (uint64_t)GetCurrentProcessId() ^ ((uint64_t)GetTickCount64() << 16) ^
            ((uint64_t)attempt << 48);
    unsigned int rand_value = 0;
    if (rand_s(&rand_value) == 0)
        value ^= ((uint64_t)rand_value << 32) | rand_value;
#else
    int fd = archive_open_posix("/dev/urandom", O_RDONLY, 0);
    if (fd >= 0) {
        size_t got = 0;
        while (got < sizeof(value)) {
            ssize_t n = read(fd, ((uint8_t *)&value) + got, sizeof(value) - got);
            if (n < 0 && errno == EINTR)
                continue;
            if (n <= 0)
                break;
            got += (size_t)n;
        }
        close(fd);
    }
    if (value == 0)
        value = (uint64_t)getpid() ^ ((uint64_t)time(NULL) << 32) ^ ((uint64_t)attempt << 48);
#endif
    return value;
}

/// @brief Build a unique temporary file path adjacent to `path`.
///
/// Constructs a sidecar name in the same directory as `path` using the
/// current process ID, random entropy, and `attempt` as collision guard.
/// The caller must `free()` the returned string. Returns NULL on allocation
/// failure or path length overflow.
///
/// @param path    Base path whose parent directory receives the temp file.
/// @param attempt Iteration counter to differentiate multiple tries.
/// @return Heap-allocated temp path, or NULL on failure.
char *archive_make_temp_path(const char *path, unsigned attempt) {
    size_t path_len = strlen(path);
    size_t parent_len = 0;
    for (size_t i = 0; i < path_len; ++i) {
#ifdef _WIN32
        if (path[i] == '/' || path[i] == '\\')
#else
        if (path[i] == '/')
#endif
            parent_len = i + 1;
    }

    char nonce[17];
    snprintf(nonce, sizeof(nonce), "%016llx", (unsigned long long)archive_random_u64(attempt));
    size_t nonce_len = strlen(nonce);
    if (parent_len > SIZE_MAX - nonce_len - 64)
        return NULL;
    size_t cap = parent_len + nonce_len + 64;
    if (parent_len >= cap)
        return NULL;
    char *tmp = (char *)malloc(cap);
    if (!tmp)
        return NULL;
    if (parent_len > 0)
        memcpy(tmp, path, parent_len);
#ifdef _WIN32
    unsigned long pid = (unsigned long)GetCurrentProcessId();
#else
    unsigned long pid = (unsigned long)getpid();
#endif
    int written = snprintf(
        tmp + parent_len, cap - parent_len, ".viper-archive-tmp.%lu.%s.%u", pid, nonce, attempt);
    if (written < 0 || (size_t)written >= cap - parent_len) {
        free(tmp);
        return NULL;
    }
    return tmp;
}

/// @brief Atomically rename `src` to `dst`, replacing any existing file.
///
/// On Windows uses `MoveFileExW` with `MOVEFILE_REPLACE_EXISTING |
/// MOVEFILE_WRITE_THROUGH`; on POSIX uses `rename(2)`. Both are
/// atomic on a single filesystem.
///
/// @param src UTF-8 source path.
/// @param dst UTF-8 destination path.
/// @return 1 on success, 0 on failure.
static int archive_replace_utf8(const char *src, const char *dst) {
#ifdef _WIN32
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
#else
    return rename(src, dst) == 0 ? 1 : 0;
#endif
}

/// @brief Delete a file at a UTF-8 path, ignoring errors.
///
/// Cross-platform wrapper: `DeleteFileW` on Windows (after wide
/// conversion), `unlink(2)` on POSIX. Used for temp-file cleanup
/// where best-effort removal is sufficient.
///
/// @param path UTF-8 path of the file to delete.
void archive_unlink_utf8(const char *path) {
#ifdef _WIN32
    wchar_t *wide = rt_file_path_utf8_to_wide(path);
    if (wide) {
        (void)DeleteFileW(wide);
        free(wide);
    }
#else
    (void)unlink(path);
#endif
}

/// @brief fsync the directory that contains `path` (POSIX only).
///
/// Required on Linux/macOS to guarantee the directory entry for a
/// newly renamed file survives a crash. No-op on Windows (the
/// `MOVEFILE_WRITE_THROUGH` flag handles this there). Returns 1 on
/// success, 0 on any error.
///
/// @param path UTF-8 path of the newly placed file.
/// @return 1 if the parent directory was fsynced successfully, 0 otherwise.
static int archive_sync_parent_dir(const char *path) {
#ifdef _WIN32
    (void)path;
    return 1;
#else
    const char *last = strrchr(path, '/');
    const char *parent = ".";
    char *owned = NULL;
    if (last) {
        size_t len = (size_t)(last - path);
        if (len == 0) {
            parent = "/";
        } else {
            owned = (char *)malloc(len + 1);
            if (!owned)
                return 0;
            memcpy(owned, path, len);
            owned[len] = '\0';
            parent = owned;
        }
    }
    int flags = O_RDONLY;
#ifdef O_DIRECTORY
    flags |= O_DIRECTORY;
#endif
    int fd = archive_open_posix(parent, flags, 0);
    free(owned);
    if (fd < 0)
        return 0;
    int ok = fsync(fd) == 0 ? 1 : 0;
    if (close(fd) != 0)
        ok = 0;
    return ok;
#endif
}

/// @brief Atomically replace a file at a UTF-8 path with `total` bytes.
///
/// Cross-platform helper used to flush the assembled write buffer
/// during `Finish` and to drop extracted entries onto disk during
/// `Extract`/`ExtractAll`. Writes an exclusive temp sidecar first,
/// flushes it, then replaces the destination so readers never observe
/// a partially-written archive or extracted file.
///
/// @param cpath    UTF-8 destination file path.
/// @param src      Source byte buffer.
/// @param total    Number of bytes to write.
/// @param trap_msg Trap message used on any failure.
int archive_write_file_all_utf8(const char *cpath,
                                       const uint8_t *src,
                                       size_t total,
                                       const char *trap_msg) {
#ifdef _WIN32
    char *tmp = NULL;
    HANDLE h = INVALID_HANDLE_VALUE;
    for (unsigned attempt = 0; attempt < 128; ++attempt) {
        tmp = archive_make_temp_path(cpath, attempt);
        if (!tmp) {
            rt_trap(trap_msg);
            return 0;
        }
        h = archive_open_win_path(tmp, GENERIC_WRITE, 0, CREATE_NEW);
        if (h != INVALID_HANDLE_VALUE)
            break;
        free(tmp);
        tmp = NULL;
        DWORD err = GetLastError();
        if (err != ERROR_FILE_EXISTS && err != ERROR_ALREADY_EXISTS)
            break;
    }
    if (h == INVALID_HANDLE_VALUE || !tmp) {
        free(tmp);
        rt_trap(trap_msg);
        return 0;
    }
    if (!archive_write_exact_win(h, src, total, trap_msg)) {
        CloseHandle(h);
        archive_unlink_utf8(tmp);
        free(tmp);
        return 0;
    }
    int ok = FlushFileBuffers(h) ? 1 : 0;
    if (!CloseHandle(h))
        ok = 0;
    if (ok)
        ok = archive_replace_utf8(tmp, cpath);
    if (!ok) {
        archive_unlink_utf8(tmp);
        free(tmp);
        rt_trap(trap_msg);
        return 0;
    }
    free(tmp);
#else
    char *tmp = NULL;
    int fd = -1;
    for (unsigned attempt = 0; attempt < 128; ++attempt) {
        tmp = archive_make_temp_path(cpath, attempt);
        if (!tmp) {
            rt_trap(trap_msg);
            return 0;
        }
        int flags = O_WRONLY | O_CREAT | O_EXCL;
#ifdef O_NOFOLLOW
        flags |= O_NOFOLLOW;
#endif
        fd = archive_open_posix(tmp, flags, 0644);
        if (fd >= 0)
            break;
        int err = errno;
        free(tmp);
        tmp = NULL;
        if (err != EEXIST)
            break;
    }
    if (fd < 0 || !tmp) {
        free(tmp);
        rt_trap(trap_msg);
        return 0;
    }
    if (!archive_write_exact_posix(fd, src, total, trap_msg)) {
        close(fd);
        archive_unlink_utf8(tmp);
        free(tmp);
        return 0;
    }
    int ok = fsync(fd) == 0 ? 1 : 0;
    if (close(fd) != 0)
        ok = 0;
    if (ok)
        ok = archive_replace_utf8(tmp, cpath);
    if (ok)
        ok = archive_sync_parent_dir(cpath);
    if (!ok) {
        archive_unlink_utf8(tmp);
        free(tmp);
        rt_trap(trap_msg);
        return 0;
    }
    free(tmp);
#endif
    return 1;
}

/// @brief Write the contents of an `rt_bytes` handle to a UTF-8 path.
///
/// Thin adapter over `archive_write_file_all_utf8` that unwraps the
/// bytes handle. Traps if the path is empty or NULL.
///
/// @param cpath UTF-8 destination path.
/// @param data  Source `rt_bytes` handle.
void archive_write_bytes_to_path(const char *cpath, void *data) {
    if (!cpath || *cpath == '\0') {
        rt_trap("Archive: invalid destination path");
        return;
    }
    if (!data) {
        rt_trap("Archive: NULL data");
        return;
    }

    const uint8_t *src = bytes_data(data);
    size_t total = (size_t)bytes_len(data);
    (void)archive_write_file_all_utf8(
        cpath, src, total, "Archive: failed to write destination file");
}

#if !defined(_WIN32) && !defined(__viperdos__)
static int archive_openat_posix(int parent_fd, const char *name, int flags, mode_t mode) {
#ifdef O_CLOEXEC
    flags |= O_CLOEXEC;
#endif
    int fd = openat(parent_fd, name, flags, mode);
#if defined(FD_CLOEXEC) && !defined(O_CLOEXEC)
    if (fd >= 0) {
        int fd_flags = fcntl(fd, F_GETFD);
        if (fd_flags >= 0)
            (void)fcntl(fd, F_SETFD, fd_flags | FD_CLOEXEC);
    }
#endif
    return fd;
}

static int archive_dup_fd_posix(int fd) {
    int dup_fd = -1;
#ifdef F_DUPFD_CLOEXEC
    dup_fd = fcntl(fd, F_DUPFD_CLOEXEC, 3);
    if (dup_fd >= 0)
        return dup_fd;
#endif
    dup_fd = dup(fd);
#ifdef FD_CLOEXEC
    if (dup_fd >= 0) {
        int flags = fcntl(dup_fd, F_GETFD);
        if (flags >= 0)
            (void)fcntl(dup_fd, F_SETFD, flags | FD_CLOEXEC);
    }
#endif
    return dup_fd;
}

static int archive_open_child_dir_posix(int parent_fd, const char *name, int create) {
    if (!name || *name == '\0') {
        rt_trap("Archive: invalid directory entry");
        return -1;
    }
    if (create && mkdirat(parent_fd, name, 0755) != 0 && errno != EEXIST) {
        rt_trap("Archive: failed to create directory");
        return -1;
    }

    struct stat st;
    if (fstatat(parent_fd, name, &st, AT_SYMLINK_NOFOLLOW) != 0 || !S_ISDIR(st.st_mode)) {
        rt_trap("Archive: refusing to extract through symlink");
        return -1;
    }

    int flags = O_RDONLY;
#ifdef O_DIRECTORY
    flags |= O_DIRECTORY;
#endif
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    int fd = archive_openat_posix(parent_fd, name, flags, 0);
    if (fd < 0) {
        rt_trap("Archive: failed to open destination directory");
        return -1;
    }
    if (fstat(fd, &st) != 0 || !S_ISDIR(st.st_mode)) {
        close(fd);
        rt_trap("Archive: destination component is not a directory");
        return -1;
    }
    return fd;
}

static int archive_open_dir_path_posix(int root_fd, const char *path, int create) {
    int current = archive_dup_fd_posix(root_fd);
    if (current < 0) {
        rt_trap("Archive: failed to open destination directory");
        return -1;
    }
    if (!path || *path == '\0')
        return current;

    char *copy = strdup(path);
    if (!copy) {
        close(current);
        rt_trap("Archive: memory allocation failed");
        return -1;
    }
    size_t len = strlen(copy);
    while (len > 0 && copy[len - 1] == '/')
        copy[--len] = '\0';

    char *segment = copy;
    while (*segment) {
        char *slash = strchr(segment, '/');
        if (slash)
            *slash = '\0';
        int next = archive_open_child_dir_posix(current, segment, create);
        close(current);
        if (next < 0) {
            free(copy);
            return -1;
        }
        current = next;
        if (!slash)
            break;
        segment = slash + 1;
    }

    free(copy);
    return current;
}

void archive_make_dirs_posix_at(int root_fd, const char *path) {
    int fd = archive_open_dir_path_posix(root_fd, path, 1);
    if (fd >= 0)
        close(fd);
}

int archive_open_parent_for_file_posix(int root_fd, const char *name, char **out_leaf) {
    if (out_leaf)
        *out_leaf = NULL;
    char *copy = strdup(name);
    if (!copy) {
        rt_trap("Archive: memory allocation failed");
        return -1;
    }
    char *last = strrchr(copy, '/');
    char *leaf_src = copy;
    int parent_fd = -1;
    if (last) {
        *last = '\0';
        leaf_src = last + 1;
        parent_fd = archive_open_dir_path_posix(root_fd, copy, 1);
    } else {
        parent_fd = archive_dup_fd_posix(root_fd);
    }
    if (parent_fd < 0) {
        free(copy);
        rt_trap("Archive: failed to open destination directory");
        return -1;
    }
    if (!leaf_src || *leaf_src == '\0') {
        close(parent_fd);
        free(copy);
        rt_trap("Archive: invalid file entry");
        return -1;
    }
    char *leaf = strdup(leaf_src);
    free(copy);
    if (!leaf) {
        close(parent_fd);
        rt_trap("Archive: memory allocation failed");
        return -1;
    }
    *out_leaf = leaf;
    return parent_fd;
}

void archive_write_bytes_to_dirfd_posix(int parent_fd, const char *leaf, void *data) {
    if (parent_fd < 0 || !leaf || !data) {
        rt_trap("Archive: failed to write destination file");
        return;
    }
    const uint8_t *src = bytes_data(data);
    size_t total = (size_t)bytes_len(data);
    char tmp_name[128];
    int fd = -1;
    for (unsigned attempt = 0; attempt < 128; ++attempt) {
        snprintf(tmp_name,
                 sizeof(tmp_name),
                 ".viper-archive-extract-tmp.%lu.%016llx.%u",
                 (unsigned long)getpid(),
                 (unsigned long long)archive_random_u64(attempt),
                 attempt);
        int flags = O_WRONLY | O_CREAT | O_EXCL;
#ifdef O_NOFOLLOW
        flags |= O_NOFOLLOW;
#endif
        fd = archive_openat_posix(parent_fd, tmp_name, flags, 0644);
        if (fd >= 0)
            break;
        if (errno != EEXIST) {
            rt_trap("Archive: failed to write destination file");
            return;
        }
    }
    if (fd < 0) {
        rt_trap("Archive: failed to write destination file");
        return;
    }

    if (!archive_write_exact_posix(fd, src, total, "Archive: failed to write destination file")) {
        close(fd);
        (void)unlinkat(parent_fd, tmp_name, 0);
        return;
    }
    int ok = fsync(fd) == 0 ? 1 : 0;
    if (close(fd) != 0)
        ok = 0;
    if (ok && renameat(parent_fd, tmp_name, parent_fd, leaf) != 0)
        ok = 0;
    if (ok && fsync(parent_fd) != 0)
        ok = 0;
    if (!ok) {
        (void)unlinkat(parent_fd, tmp_name, 0);
        rt_trap("Archive: failed to write destination file");
        return;
    }
}

int archive_open_root_dir_posix(const char *cdir) {
    int flags = O_RDONLY;
#ifdef O_DIRECTORY
    flags |= O_DIRECTORY;
#endif
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    int fd = archive_open_posix(cdir, flags, 0);
    if (fd < 0) {
        rt_trap("Archive: failed to open destination directory");
        return -1;
    }
    struct stat st;
    if (fstat(fd, &st) != 0 || !S_ISDIR(st.st_mode)) {
        close(fd);
        rt_trap("Archive: destination is not a directory");
        return -1;
    }
    return fd;
}
#endif

/// @brief Return 1 if `c` is a path separator (`/` or `\`), 0 otherwise.
static int archive_is_sep(char c) {
    return c == '/' || c == '\\';
}

/// @brief Return the length of `path` with trailing separators stripped.
///
/// Leaves at least one character so that a path consisting of only
/// separators (e.g., `"/"`) is not reduced to zero length.
///
/// @param path String to trim (not modified).
/// @param len  Initial length to trim down from.
/// @return Trimmed length (>= 1).
size_t archive_trim_trailing_seps(const char *path, size_t len) {
    while (len > 1 && archive_is_sep(path[len - 1]))
        --len;
    return len;
}

/// @brief Return 1 if `path` is a symlink or reparse point, 0 otherwise.
///
/// Uses `lstat(2)` on POSIX and `GetFileAttributesW` on Windows. Does
/// not follow the link — the check is on the link itself, which is
/// exactly what the traversal guard needs.
///
/// @param path UTF-8 path to inspect.
/// @return 1 if the entry is a symlink / reparse point, 0 otherwise.
static int archive_path_is_reparse_or_symlink(const char *path) {
#ifdef _WIN32
    wchar_t *wide = rt_file_path_utf8_to_wide(path);
    if (!wide)
        return 0;
    DWORD attrs = GetFileAttributesW(wide);
    free(wide);
    if (attrs == INVALID_FILE_ATTRIBUTES)
        return 0;
    return (attrs & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
#else
    struct stat st;
    if (lstat(path, &st) != 0)
        return 0;
    return S_ISLNK(st.st_mode) ? 1 : 0;
#endif
}

/// @brief Trap if any path component (or the leaf) is a symlink / reparse point.
///
/// Walks each intermediate prefix of `path` starting after `root_len`
/// characters (the known-safe root, e.g., the extraction directory).
/// If `include_leaf` is true, the full path is also checked. This
/// defends against TOCTOU symlink-swapping attacks during extraction.
///
/// @param path         Full UTF-8 path to inspect.
/// @param root_len     Number of leading bytes that are already trusted.
/// @param include_leaf If non-zero, also check the final path component.
void archive_reject_symlink_components(const char *path, size_t root_len, int include_leaf) {
    if (!path)
        return;
    size_t len = strlen(path);
    if (root_len > len)
        root_len = len;
    root_len = archive_trim_trailing_seps(path, root_len);
    if (len > SIZE_MAX - 2) {
        rt_trap("Archive: destination path too long");
        return;
    }

    char *scratch = (char *)malloc(len + 2);
    if (!scratch) {
        rt_trap("Archive: memory allocation failed");
        return;
    }

    if (root_len > 0) {
        memcpy(scratch, path, root_len);
        scratch[root_len] = '\0';
        if (archive_path_is_reparse_or_symlink(scratch)) {
            free(scratch);
            rt_trap("Archive: refusing to extract through symlink");
            return;
        }
    }

    size_t i = root_len;
    while (i < len) {
        while (i < len && archive_is_sep(path[i]))
            ++i;
        if (i >= len)
            break;
        while (i < len && !archive_is_sep(path[i]))
            ++i;
        if (i == len && !include_leaf)
            break;

        if (i > len) {
            free(scratch);
            rt_trap("Archive: destination path too long");
            return;
        }
        memcpy(scratch, path, i);
        scratch[i] = '\0';
        if (archive_path_is_reparse_or_symlink(scratch)) {
            free(scratch);
            rt_trap("Archive: refusing to extract through symlink");
            return;
        }
    }

    free(scratch);
}
