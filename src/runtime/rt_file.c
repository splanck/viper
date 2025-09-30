// File: src/runtime/rt_file.c
// Purpose: Implement BASIC runtime file helpers using POSIX descriptors.
// Key invariants: Negative descriptor marks closed handles; helpers never abort on I/O failures.
// Ownership/Lifetime: Callers manage RtFile lifetime and must close open descriptors.
// Links: docs/specs/errors.md

#include "rt_file.h"

#include "rt_internal.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif

static int32_t rt_file_clamp_errno(int err)
{
    if (err > INT32_MAX)
        return INT32_MAX;
    if (err < INT32_MIN)
        return INT32_MIN;
    return (int32_t)err;
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

static bool rt_file_parse_mode(const char *mode, int *flags_out)
{
    if (!mode || !mode[0])
        return false;
    int flags = 0;
    switch (mode[0])
    {
    case 'r':
        flags = O_RDONLY;
        break;
    case 'w':
        flags = O_WRONLY | O_CREAT | O_TRUNC;
        break;
    case 'a':
        flags = O_WRONLY | O_CREAT | O_APPEND;
        break;
    default:
        return false;
    }

    bool plus = false;
    for (const char *p = mode + 1; *p; ++p)
    {
        if (*p == '+')
            plus = true;
        else if (*p == 'b' || *p == 't')
            continue;
        else
            return false;
    }
    if (plus)
    {
        flags &= ~(O_RDONLY | O_WRONLY);
        flags |= O_RDWR;
    }
    flags |= O_CLOEXEC;
    *flags_out = flags;
    return true;
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
    if (!rt_file_parse_mode(mode, &flags))
    {
        rt_file_set_error(out_err, Err_InvalidOperation, 0);
        file->fd = -1;
        return false;
    }

    mode_t perms = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;
    errno = 0;
    int fd = (flags & O_CREAT) ? open(path, flags, perms) : open(path, flags);
    if (fd < 0)
    {
        int err = errno;
        if (err == ENOENT)
            rt_file_set_error(out_err, Err_FileNotFound, err);
        else
            rt_file_set_error(out_err, Err_IOError, err ? err : EIO);
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
        rt_file_set_error(out_err, Err_IOError, errno ? errno : EIO);
        return false;
    }
    file->fd = -1;
    rt_file_set_ok(out_err);
    return true;
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
        rt_file_set_error(out_err, Err_IOError, errno ? errno : EIO);
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
            if (len + 1 >= cap)
            {
                size_t new_cap = cap * 2;
                char *nbuf = (char *)realloc(buffer, new_cap);
                if (!nbuf)
                {
                    free(buffer);
                    rt_file_set_error(out_err, Err_RuntimeError, ENOMEM);
                    return false;
                }
                buffer = nbuf;
                cap = new_cap;
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
        free(buffer);
        rt_file_set_error(out_err, Err_IOError, errno ? errno : EIO);
        return false;
    }

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
        rt_file_set_error(out_err, Err_IOError, errno ? errno : EIO);
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
            rt_file_set_error(out_err, Err_IOError, errno ? errno : EIO);
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

