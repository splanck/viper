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

typedef struct RtFileChannelEntry
{
    int32_t channel;
    RtFile file;
    bool in_use;
    bool at_eof;
} RtFileChannelEntry;

static RtFileChannelEntry *g_channel_entries = NULL;
static size_t g_channel_count = 0;
static size_t g_channel_capacity = 0;

static int32_t rt_file_clamp_errno(int err)
{
    if (err > INT32_MAX)
        return INT32_MAX;
    if (err < INT32_MIN)
        return INT32_MIN;
    return (int32_t)err;
}

static RtFileChannelEntry *rt_file_find_channel(int32_t channel)
{
    if (channel < 0)
        return NULL;
    for (size_t i = 0; i < g_channel_count; ++i)
    {
        if (g_channel_entries[i].channel == channel)
            return &g_channel_entries[i];
    }
    return NULL;
}

static RtFileChannelEntry *rt_file_prepare_channel(int32_t channel)
{
    if (channel < 0)
        return NULL;
    RtFileChannelEntry *entry = rt_file_find_channel(channel);
    if (entry)
        return entry;

    if (g_channel_count == g_channel_capacity)
    {
        size_t new_capacity = g_channel_capacity ? g_channel_capacity * 2 : 4;
        RtFileChannelEntry *new_entries =
            (RtFileChannelEntry *)realloc(g_channel_entries, new_capacity * sizeof(*new_entries));
        if (!new_entries)
            return NULL;
        for (size_t i = g_channel_capacity; i < new_capacity; ++i)
        {
            new_entries[i].channel = 0;
            new_entries[i].in_use = false;
            new_entries[i].at_eof = false;
            rt_file_init(&new_entries[i].file);
        }
        g_channel_entries = new_entries;
        g_channel_capacity = new_capacity;
    }

    entry = &g_channel_entries[g_channel_count++];
    entry->channel = channel;
    entry->in_use = false;
    entry->at_eof = false;
    rt_file_init(&entry->file);
    return entry;
}

static const char *rt_file_mode_string(int32_t mode)
{
    switch (mode)
    {
    case RT_F_INPUT:
        return "r";
    case RT_F_OUTPUT:
        return "w";
    case RT_F_APPEND:
        return "a";
    case RT_F_BINARY:
        return "rbc+";
    case RT_F_RANDOM:
        return "rbc+";
    default:
        return NULL;
    }
}

static const char *rt_file_path_from_vstr(const ViperString *path)
{
    if (!path || !path->data)
        return NULL;
    return path->data;
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

static size_t rt_file_string_view(const ViperString *s, const uint8_t **data_out)
{
    if (data_out)
        *data_out = NULL;
    if (!s || !s->data)
        return 0;
    if (data_out)
        *data_out = (const uint8_t *)s->data;
    if (s->heap)
        return rt_heap_len(s->data);
    return s->literal_len;
}

static int32_t rt_file_resolve_channel(int32_t channel, RtFileChannelEntry **out_entry)
{
    if (out_entry)
        *out_entry = NULL;
    if (channel < 0)
        return (int32_t)Err_InvalidOperation;
    RtFileChannelEntry *entry = rt_file_find_channel(channel);
    if (!entry || !entry->in_use)
        return (int32_t)Err_InvalidOperation;
    if (entry->file.fd < 0)
        return (int32_t)Err_IOError;
    if (out_entry)
        *out_entry = entry;
    return 0;
}

static int32_t rt_file_write_entry(RtFileChannelEntry *entry, const uint8_t *data, size_t len)
{
    if (!entry || len == 0)
        return 0;
    if (!data)
        return (int32_t)Err_InvalidOperation;
    RtError err = RT_ERROR_NONE;
    if (!rt_file_write(&entry->file, data, len, &err))
        return (int32_t)err.kind;
    entry->at_eof = false;
    return 0;
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
    bool create = false;
    for (const char *p = mode + 1; *p; ++p)
    {
        if (*p == '+')
        {
            plus = true;
        }
        else if (*p == 'b')
        {
#ifdef O_BINARY
            flags |= O_BINARY;
#endif
        }
        else if (*p == 't')
        {
            continue;
        }
        else if (*p == 'c')
        {
            create = true;
        }
        else
        {
            return false;
        }
    }
    if (plus)
    {
        flags &= ~(O_RDONLY | O_WRONLY);
        flags |= O_RDWR;
    }
    if (create)
        flags |= O_CREAT;
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

int32_t rt_open_err_vstr(ViperString *path, int32_t mode, int32_t channel)
{
    const char *mode_str = rt_file_mode_string(mode);
    const char *path_str = rt_file_path_from_vstr(path);
    if (!mode_str || !path_str || channel < 0)
        return (int32_t)Err_InvalidOperation;

    RtFileChannelEntry *entry = rt_file_prepare_channel(channel);
    if (!entry)
        return (int32_t)Err_RuntimeError;
    if (entry->in_use)
        return (int32_t)Err_InvalidOperation;

    rt_file_init(&entry->file);
    RtError err = RT_ERROR_NONE;
    if (!rt_file_open(&entry->file, path_str, mode_str, &err))
    {
        entry->in_use = false;
        return (int32_t)err.kind;
    }

    entry->in_use = true;
    entry->at_eof = false;
    return 0;
}

int32_t rt_close_err(int32_t channel)
{
    if (channel < 0)
        return (int32_t)Err_InvalidOperation;
    RtFileChannelEntry *entry = rt_file_find_channel(channel);
    if (!entry || !entry->in_use)
        return (int32_t)Err_InvalidOperation;

    RtError err = RT_ERROR_NONE;
    if (!rt_file_close(&entry->file, &err))
    {
        entry->in_use = false;
        entry->at_eof = false;
        rt_file_init(&entry->file);
        return (int32_t)err.kind;
    }

    entry->in_use = false;
    entry->at_eof = false;
    rt_file_init(&entry->file);
    return 0;
}

int32_t rt_write_ch_err(int32_t channel, ViperString *s)
{
    RtFileChannelEntry *entry = NULL;
    int32_t status = rt_file_resolve_channel(channel, &entry);
    if (status != 0)
        return status;

    const uint8_t *data = NULL;
    size_t len = rt_file_string_view(s, &data);
    return rt_file_write_entry(entry, data, len);
}

int32_t rt_println_ch_err(int32_t channel, ViperString *s)
{
    RtFileChannelEntry *entry = NULL;
    int32_t status = rt_file_resolve_channel(channel, &entry);
    if (status != 0)
        return status;

    const uint8_t *data = NULL;
    size_t len = rt_file_string_view(s, &data);
    status = rt_file_write_entry(entry, data, len);
    if (status != 0)
        return status;

    const uint8_t newline = (uint8_t)'\n';
    return rt_file_write_entry(entry, &newline, 1);
}

int32_t rt_line_input_ch_err(int32_t channel, ViperString **out)
{
    if (!out)
        return (int32_t)Err_InvalidOperation;
    *out = NULL;

    RtFileChannelEntry *entry = NULL;
    int32_t status = rt_file_resolve_channel(channel, &entry);
    if (status != 0)
        return status;

    rt_string line = NULL;
    RtError err = RT_ERROR_NONE;
    if (!rt_file_read_line(&entry->file, &line, &err))
    {
        if (err.kind == Err_EOF)
            entry->at_eof = true;
        return (int32_t)err.kind;
    }

    entry->at_eof = false;

    *out = line;
    return 0;
}

int32_t rt_file_channel_fd(int32_t channel, int *out_fd)
{
    RtFileChannelEntry *entry = NULL;
    int32_t status = rt_file_resolve_channel(channel, &entry);
    if (status != 0)
        return status;
    if (out_fd)
        *out_fd = entry->file.fd;
    return 0;
}

int32_t rt_file_channel_get_eof(int32_t channel, bool *out_at_eof)
{
    RtFileChannelEntry *entry = NULL;
    int32_t status = rt_file_resolve_channel(channel, &entry);
    if (status != 0)
        return status;
    if (out_at_eof)
        *out_at_eof = entry->at_eof;
    return 0;
}

int32_t rt_file_channel_set_eof(int32_t channel, bool at_eof)
{
    RtFileChannelEntry *entry = NULL;
    int32_t status = rt_file_resolve_channel(channel, &entry);
    if (status != 0)
        return status;
    entry->at_eof = at_eof;
    return 0;
}

