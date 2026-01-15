//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Provides the file I/O layer used by the BASIC runtime.  The functions in this
// translation unit manage descriptor lifetimes, translate errno codes into the
// runtime's structured diagnostics, and implement higher-level helpers such as
// line reading and binary writes.  Centralising the logic keeps the VM and
// native runtimes in sync when interacting with the host filesystem.
//
// Platform support:
//   - POSIX (Linux, macOS): Uses standard POSIX APIs (open, read, write, etc.)
//   - Windows: Uses MSVC CRT APIs (_open, _read, _write, etc.)
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Cross-platform file I/O wrappers for the BASIC runtime.
/// @details Offers safe descriptor management, buffered line reads, and error
///          propagation utilities that convert errno into @ref RtError records.
///          All routines maintain invariants around @ref RtFile handles so
///          callers never observe partially-initialised state.

#include "rt_file.h"

#include "rt_file_path.h"
#include "rt_internal.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <io.h>
#include <share.h>
#include <sys/stat.h>
#include <sys/types.h>
// Windows doesn't define ssize_t, so we provide it
#if !defined(ssize_t)
#include <BaseTsd.h>
typedef SSIZE_T ssize_t;
#endif
// Map POSIX names to Windows CRT equivalents
#define open _open
#define close _close
#define read _read
#define write _write
#define lseek _lseeki64
// Windows file permission flags
#define S_IRUSR _S_IREAD
#define S_IWUSR _S_IWRITE
#define S_IRGRP 0
#define S_IWGRP 0
#define S_IROTH 0
#define S_IWOTH 0
// Windows doesn't have these errno values
#ifndef EISDIR
#define EISDIR ENOENT
#endif
#ifndef ENOTDIR
#define ENOTDIR ENOENT
#endif
#ifndef ENOSPC
#define ENOSPC EIO
#endif
#ifndef EFBIG
#define EFBIG EIO
#endif
#ifndef EPIPE
#define EPIPE EIO
#endif
#ifndef EAGAIN
#define EAGAIN EIO
#endif
#elif defined(__viperdos__)
// TODO: ViperDOS - include file I/O headers when available
// ViperDOS has POSIX-like file I/O syscalls
#include <sys/stat.h>
#include <sys/types.h>
typedef long ssize_t;
#define S_IRUSR 0400
#define S_IWUSR 0200
#define S_IRGRP 0040
#define S_IWGRP 0020
#define S_IROTH 0004
#define S_IWOTH 0002
#ifndef EINTR
#define EINTR 0 // Windows doesn't have EINTR; reads/writes don't get interrupted
#endif
// mode_t for Windows (off_t is defined in sys/types.h as _off_t)
typedef unsigned short mode_t;
#else
// POSIX systems
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

/// @brief Clamp errno values into the 32-bit range stored by @ref RtError.
/// @details Ensures large positive or negative errno values fit into the
///          runtime's signed 32-bit field without overflow.
/// @param err Errno value produced by the OS.
/// @return Clamped errno suitable for storage in @ref RtError::code.
static int32_t rt_file_clamp_errno(int err)
{
    if (err > INT32_MAX)
        return INT32_MAX;
    if (err < INT32_MIN)
        return INT32_MIN;
    return (int32_t)err;
}

/// @brief Map raw errno codes to runtime error kinds.
/// @details Converts common errno values into more specific runtime error
///          enumerations while falling back to @p fallback when no specialised
///          mapping exists.
/// @param err Errno value.
/// @param fallback Error kind to use when no mapping exists.
/// @return Runtime error kind describing the failure.
static enum Err rt_file_err_from_errno(int err, enum Err fallback)
{
    if (err == 0)
        return fallback;
    switch (err)
    {
        case ENOENT:
            return Err_FileNotFound;
        case EINVAL:
        case EISDIR:
        case ENOTDIR:
            // Operation invalid for current state or object kind
            return Err_InvalidOperation;
        case ENOSPC: // No space left on device
        case EFBIG:  // File too large
        case EIO:
        case EPIPE:
        case EAGAIN:
        case EACCES:
        case EPERM:
        case EBADF:
            // Permission, transient, or device/storage I/O errors
            return Err_IOError;
        default:
            return fallback;
    }
}

/// @brief Populate an @ref RtError structure with an error code.
/// @details Safely handles null output pointers and clamps errno before storage.
/// @param out_err Destination error pointer.
/// @param kind Runtime error classification.
/// @param err Errno value associated with the failure.
static void rt_file_set_error(RtError *out_err, enum Err kind, int err)
{
    if (!out_err)
        return;
    out_err->kind = kind;
    out_err->code = rt_file_clamp_errno(err);
}

/// @brief Reset an error structure to the success sentinel.
/// @details Writes @ref RT_ERROR_NONE when the output pointer is non-null.
/// @param out_err Destination error pointer.
static void rt_file_set_ok(RtError *out_err)
{
    if (out_err)
        *out_err = RT_ERROR_NONE;
}

/// @brief Determine whether an `int64_t` offset fits within the host's @ref off_t range.
/// @details Uses POSIX-provided limits when available and falls back to width-based
///          calculations otherwise.  The helper avoids undefined behaviour from
///          narrowing casts by performing bounds checks prior to conversion.
/// @param offset Proposed byte offset for @ref lseek.
/// @return `true` when @p offset is representable as @ref off_t; otherwise `false`.
static bool rt_file_offset_in_range(int64_t offset)
{
#if defined(OFF_MAX) && defined(OFF_MIN)
    if (sizeof(off_t) > sizeof(int64_t))
        return true;
    if (offset < (int64_t)OFF_MIN || offset > (int64_t)OFF_MAX)
        return false;
    return true;
#else
    const int bits = (int)(sizeof(off_t) * CHAR_BIT);
    const int int64_bits = (int)(sizeof(int64_t) * CHAR_BIT);
    if (bits >= int64_bits)
        return true;
    const int shift = bits - 1;
    if (shift <= 0)
        return offset == 0;
    if (shift >= 63)
    {
        const int64_t max = INT64_MAX;
        const int64_t min = INT64_MIN;
        return offset >= min && offset <= max;
    }
    const int64_t M = (INT64_MAX >> (63 - shift));
    const int64_t max = M;
    const int64_t min = -M - 1;
    return offset >= min && offset <= max;
#endif
}

/// @brief Validate that a file handle contains an open descriptor.
/// @details Ensures @p file is non-null and the descriptor is valid, populating
///          @p out_err on failure.
/// @param file File handle to inspect.
/// @param out_err Optional error destination.
/// @return `true` when the descriptor is usable; otherwise `false`.
static bool rt_file_check_fd(const RtFile *file, RtError *out_err)
{
    if (!file)
    {
        rt_file_set_error(out_err, Err_InvalidOperation, 0);
        return false;
    }
    if (file->fd < 0)
    {
        rt_file_set_error(out_err, Err_IOError, EBADF);
        return false;
    }
    return true;
}

/// @brief Double a dynamically-allocated line buffer while checking overflow.
/// @details Reallocates the caller-owned buffer, ensuring the new capacity
///          accommodates the requested length and reporting structured errors on
///          failure.  The helper releases the old buffer on error to avoid
///          leaks.
/// @param buffer Pointer to the caller's heap buffer pointer.
/// @param cap Pointer to the current capacity in bytes.
/// @param len Number of bytes currently stored in the buffer.
/// @param out_err Receives error details when resizing fails.
/// @return `true` when the buffer was grown successfully; otherwise `false`.
static bool rt_file_line_buffer_grow(char **buffer, size_t *cap, size_t len, RtError *out_err)
{
    if (!buffer || !*buffer || !cap)
    {
        rt_file_set_error(out_err, Err_InvalidOperation, 0);
        return false;
    }

    if (len == SIZE_MAX)
    {
        free(*buffer);
        *buffer = NULL;
        rt_file_set_error(out_err, Err_RuntimeError, ERANGE);
        return false;
    }

    if (*cap > SIZE_MAX / 2)
    {
        free(*buffer);
        *buffer = NULL;
        rt_file_set_error(out_err, Err_RuntimeError, ERANGE);
        return false;
    }

    size_t new_cap = (*cap) * 2;
    if (new_cap <= len)
    {
        free(*buffer);
        *buffer = NULL;
        rt_file_set_error(out_err, Err_RuntimeError, ERANGE);
        return false;
    }

    char *nbuf = (char *)realloc(*buffer, new_cap);
    if (!nbuf)
    {
        free(*buffer);
        *buffer = NULL;
        rt_file_set_error(out_err, Err_RuntimeError, ENOMEM);
        return false;
    }

    *buffer = nbuf;
    *cap = new_cap;
    return true;
}

/// @brief Test hook exposing the line buffer growth guard for regression coverage.
/// @param buffer Buffer pointer owned by the caller.
/// @param cap Current capacity in bytes.
/// @param len Logical payload length stored in @p buffer.
/// @param out_err Receives error details when growth fails.
/// @return True on successful growth; false when allocation or overflow prevents resizing.
bool rt_file_line_buffer_try_grow_for_test(char **buffer, size_t *cap, size_t len, RtError *out_err)
{
    return rt_file_line_buffer_grow(buffer, cap, len, out_err);
}

/// @brief Initialise a file handle to the closed state.
/// @details Sets the descriptor sentinel to -1 so subsequent operations can
///          detect whether the handle has been opened.
/// @param file Handle to initialise.
void rt_file_init(RtFile *file)
{
    if (!file)
        return;
    file->fd = -1;
}

/// @brief Open a file using BASIC runtime semantics.
/// @details Validates arguments, translates the textual mode into POSIX flags,
///          applies permissive default permissions, and records structured
///          errors when the system call fails.  On success the descriptor is
///          stored in @p file.
/// @param file Handle receiving the descriptor.
/// @param path File path to open.
/// @param mode BASIC mode string (e.g., "r", "w", "a").
/// @param out_err Optional error destination.
/// @return `true` on success; otherwise `false` with @p out_err populated.
bool rt_file_open(
    RtFile *file, const char *path, const char *mode, int32_t basic_mode, RtError *out_err)
{
    if (!file || !path || !mode)
    {
        rt_file_set_error(out_err, Err_InvalidOperation, 0);
        return false;
    }

    int flags = 0;
    if (!rt_file_mode_to_flags(mode, basic_mode, &flags))
    {
        file->fd = -1;
        rt_file_set_error(out_err, Err_InvalidOperation, 0);
        return false;
    }

    mode_t perms = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;
    errno = 0;
    int fd = (flags & O_CREAT) ? open(path, flags, perms) : open(path, flags);
    if (fd < 0)
    {
        int err = errno ? errno : EIO;
        rt_file_set_error(out_err, rt_file_err_from_errno(err, Err_IOError), err);
        file->fd = -1;
        return false;
    }

    file->fd = fd;
    rt_file_set_ok(out_err);
    return true;
}

/// @brief Close an open runtime file handle.
/// @details Treats already-closed handles as success, otherwise closes the
///          descriptor and reports any system errors through @p out_err.
/// @param file Handle to close.
/// @param out_err Optional error destination.
/// @return `true` when the handle is closed or already closed; otherwise `false`.
bool rt_file_close(RtFile *file, RtError *out_err)
{
    if (!file)
    {
        rt_file_set_error(out_err, Err_InvalidOperation, 0);
        return false;
    }
    if (file->fd < 0)
    {
        rt_file_set_ok(out_err);
        return true;
    }

    errno = 0;
    int rc = close(file->fd);
    if (rc < 0)
    {
        int err = errno ? errno : EIO;
        rt_file_set_error(out_err, rt_file_err_from_errno(err, Err_IOError), err);
        return false;
    }

    file->fd = -1;
    rt_file_set_ok(out_err);
    return true;
}

/// @brief Read a single byte from a file descriptor.
/// @details Retries on EINTR, reports EOF via @ref Err_EOF, and surfaces other
///          failures through @p out_err.  The byte is written to @p out_byte on
///          success.
/// @param file File handle to read from.
/// @param out_byte Destination for the read byte.
/// @param out_err Optional error destination.
/// @return `true` when a byte was read; `false` on EOF or error.
bool rt_file_read_byte(RtFile *file, uint8_t *out_byte, RtError *out_err)
{
    if (!out_byte)
    {
        rt_file_set_error(out_err, Err_InvalidOperation, 0);
        return false;
    }
    if (!rt_file_check_fd(file, out_err))
        return false;

    for (;;)
    {
        uint8_t byte = 0;
        errno = 0;
        ssize_t n = read(file->fd, &byte, 1);
        if (n == 1)
        {
            *out_byte = byte;
            rt_file_set_ok(out_err);
            return true;
        }
        if (n == 0)
        {
            rt_file_set_error(out_err, Err_EOF, 0);
            return false;
        }
        if (n < 0 && errno == EINTR)
            continue;

        int err = errno ? errno : EIO;
        rt_file_set_error(out_err, rt_file_err_from_errno(err, Err_IOError), err);
        return false;
    }
}

/// @brief Read a line of text terminated by `\n` from a file descriptor.
/// @details Allocates a growable buffer, strips trailing carriage returns,
///          handles EOF, and wraps the result in a runtime string object.
///          Structured errors are produced when allocation fails or the
///          descriptor encounters an I/O error.
/// @param file File handle to read from.
/// @param out_line Receives the allocated runtime string on success.
/// @param out_err Optional error destination.
/// @return `true` on success; `false` on EOF or error.
bool rt_file_read_line(RtFile *file, rt_string *out_line, RtError *out_err)
{
    if (out_line)
        *out_line = NULL;
    if (!out_line)
    {
        rt_file_set_error(out_err, Err_InvalidOperation, 0);
        return false;
    }
    if (!rt_file_check_fd(file, out_err))
        return false;

    size_t cap = 128;
    size_t len = 0;
    char *buffer = (char *)malloc(cap);
    if (!buffer)
    {
        rt_file_set_error(out_err, Err_RuntimeError, ENOMEM);
        return false;
    }

    bool ok = false;
    rt_string s = NULL;

    for (;;)
    {
        char ch = 0;
        errno = 0;
        ssize_t n = read(file->fd, &ch, 1);
        if (n == 1)
        {
            if (ch == '\n')
                break;
            if (len == SIZE_MAX || len + 1 >= cap)
            {
                if (!rt_file_line_buffer_grow(&buffer, &cap, len, out_err))
                    goto cleanup;
            }
            buffer[len++] = ch;
            continue;
        }
        if (n == 0)
        {
            if (len == 0)
            {
                rt_file_set_error(out_err, Err_EOF, 0);
                goto cleanup;
            }
            break;
        }
        if (n < 0 && errno == EINTR)
            continue;

        int err = errno ? errno : EIO;
        rt_file_set_error(out_err, rt_file_err_from_errno(err, Err_IOError), err);
        goto cleanup;
    }

    if (len > 0 && buffer[len - 1] == '\r')
        --len;

    if (len + 1 > cap)
    {
        char *nbuf = (char *)realloc(buffer, len + 1);
        if (!nbuf)
        {
            rt_file_set_error(out_err, Err_RuntimeError, ENOMEM);
            goto cleanup;
        }
        buffer = nbuf;
    }
    buffer[len] = '\0';

    s = (rt_string)calloc(1, sizeof(*s));
    if (!s)
    {
        rt_file_set_error(out_err, Err_RuntimeError, ENOMEM);
        goto cleanup;
    }

    {
        char *payload = (char *)rt_heap_alloc(RT_HEAP_STRING, RT_ELEM_NONE, 1, len, len + 1);
        if (!payload)
        {
            rt_file_set_error(out_err, Err_RuntimeError, ENOMEM);
            goto cleanup;
        }
        memcpy(payload, buffer, len + 1);

        s->data = payload;
        s->heap = rt_heap_hdr(payload);
        s->literal_len = 0;
        s->literal_refs = 0;
    }

    *out_line = s;
    rt_file_set_ok(out_err);
    ok = true;

cleanup:
    if (!ok)
    {
        if (s)
        {
            free(s);
            s = NULL;
        }
    }
    if (buffer)
    {
        free(buffer);
        buffer = NULL;
    }
    return ok;
}

/// @brief Adjust the file position indicator.
/// @details Wraps @ref lseek, converting offsets to `off_t` and reporting
///          failures through structured errors.
/// @param file File handle.
/// @param offset Byte offset relative to @p origin.
/// @param origin One of `SEEK_SET`, `SEEK_CUR`, or `SEEK_END`.
/// @param out_err Optional error destination.
/// @return `true` on success; otherwise `false`.
bool rt_file_seek(RtFile *file, int64_t offset, int origin, RtError *out_err)
{
    if (!rt_file_check_fd(file, out_err))
        return false;

    if (!rt_file_offset_in_range(offset))
    {
        rt_file_set_error(out_err, Err_InvalidOperation, ERANGE);
        return false;
    }

    errno = 0;
    off_t target = (off_t)offset;
    off_t pos = lseek(file->fd, target, origin);
    if (pos == (off_t)-1)
    {
        int err = errno ? errno : EIO;
        rt_file_set_error(out_err, rt_file_err_from_errno(err, Err_IOError), err);
        return false;
    }

    rt_file_set_ok(out_err);
    return true;
}

/// @brief Write a byte buffer to a file descriptor, retrying short writes.
/// @details Handles zero-length writes as success, validates the input pointer,
///          and loops until all bytes are written or an error occurs.  EINTR is
///          retried automatically.
/// @param file File handle to write to.
/// @param data Buffer containing bytes to write.
/// @param len Number of bytes to write.
/// @param out_err Optional error destination.
/// @return `true` when all bytes are written; otherwise `false`.
bool rt_file_write(RtFile *file, const uint8_t *data, size_t len, RtError *out_err)
{
    if (len == 0)
    {
        rt_file_set_ok(out_err);
        return true;
    }
    if (!data)
    {
        rt_file_set_error(out_err, Err_InvalidOperation, 0);
        return false;
    }
    if (!rt_file_check_fd(file, out_err))
        return false;

    size_t written = 0;
    while (written < len)
    {
        errno = 0;
        size_t chunk = len - written;
#if defined(_WIN32)
        // Windows _write takes unsigned int, clamp to avoid overflow
        if (chunk > UINT_MAX)
            chunk = UINT_MAX;
#endif
        ssize_t n = write(file->fd, data + written, chunk);
        if (n < 0)
        {
            if (errno == EINTR)
                continue;
            int err = errno ? errno : EIO;
            rt_file_set_error(out_err, rt_file_err_from_errno(err, Err_IOError), err);
            return false;
        }
        if (n == 0)
        {
            rt_file_set_error(out_err, Err_IOError, EIO);
            return false;
        }
        written += (size_t)n;
    }

    rt_file_set_ok(out_err);
    return true;
}
