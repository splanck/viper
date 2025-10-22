// File: src/runtime/rt_file_io.c
// Purpose: Provide POSIX-backed file I/O helpers for the BASIC runtime.
// Key invariants: Operations never leave RtFile handles in an indeterminate state on failure.
// Ownership/Lifetime: Callers own RtFile structures and release heap allocations via runtime helpers.
// Links: docs/specs/errors.md

#include "rt_file.h"

#include "rt_file_path.h"
#include "rt_internal.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static int32_t rt_file_clamp_errno(int err)
{
    if (err > INT32_MAX)
        return INT32_MAX;
    if (err < INT32_MIN)
        return INT32_MIN;
    return (int32_t)err;
}

static enum Err rt_file_err_from_errno(int err, enum Err fallback)
{
    if (err == 0)
        return fallback;
    switch (err)
    {
    case ENOENT:
        return Err_FileNotFound;
    default:
        return fallback;
    }
}

static void rt_file_set_error(RtError *out_err, enum Err kind, int err)
{
    if (!out_err)
        return;
    out_err->kind = kind;
    out_err->code = rt_file_clamp_errno(err);
}

static void rt_file_set_ok(RtError *out_err)
{
    if (out_err)
        *out_err = RT_ERROR_NONE;
}

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

void rt_file_init(RtFile *file)
{
    if (!file)
        return;
    file->fd = -1;
}

bool rt_file_open(RtFile *file, const char *path, const char *mode, RtError *out_err)
{
    if (!file || !path || !mode)
    {
        rt_file_set_error(out_err, Err_InvalidOperation, 0);
        return false;
    }

    int flags = 0;
    if (!rt_file_mode_to_flags(mode, &flags))
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
                    return false;
            }
            buffer[len++] = ch;
            continue;
        }
        if (n == 0)
        {
            if (len == 0)
            {
                free(buffer);
                rt_file_set_error(out_err, Err_EOF, 0);
                return false;
            }
            break;
        }
        if (n < 0 && errno == EINTR)
            continue;

        int err = errno ? errno : EIO;
        free(buffer);
        rt_file_set_error(out_err, rt_file_err_from_errno(err, Err_IOError), err);
        return false;
    }

    if (len > 0 && buffer[len - 1] == '\r')
        --len;

    if (len + 1 > cap)
    {
        char *nbuf = (char *)realloc(buffer, len + 1);
        if (!nbuf)
        {
            free(buffer);
            rt_file_set_error(out_err, Err_RuntimeError, ENOMEM);
            return false;
        }
        buffer = nbuf;
    }
    buffer[len] = '\0';

    rt_string s = (rt_string)calloc(1, sizeof(*s));
    if (!s)
    {
        free(buffer);
        rt_file_set_error(out_err, Err_RuntimeError, ENOMEM);
        return false;
    }

    char *payload = (char *)rt_heap_alloc(RT_HEAP_STRING, RT_ELEM_NONE, 1, len, len + 1);
    if (!payload)
    {
        free(buffer);
        free(s);
        rt_file_set_error(out_err, Err_RuntimeError, ENOMEM);
        return false;
    }

    memcpy(payload, buffer, len + 1);
    free(buffer);

    s->data = payload;
    s->heap = rt_heap_hdr(payload);
    s->literal_len = 0;
    s->literal_refs = 0;

    *out_line = s;
    rt_file_set_ok(out_err);
    return true;
}

bool rt_file_seek(RtFile *file, int64_t offset, int origin, RtError *out_err)
{
    if (!rt_file_check_fd(file, out_err))
        return false;

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
        ssize_t n = write(file->fd, data + written, len - written);
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

