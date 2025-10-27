// File: src/runtime/rt_file.c
// Purpose: Implement BASIC runtime file helpers using POSIX descriptors.
// Key invariants: Negative descriptor marks closed handles; helpers never abort on I/O failures.
// Ownership/Lifetime: Callers manage RtFile lifetime and must close open descriptors.
// Links: docs/specs/errors.md

#include "rt_file.h"

#include "rt_file_path.h"

#include <stdlib.h>

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

int32_t rt_open_err_vstr(ViperString *path, int32_t mode, int32_t channel)
{
    const char *mode_str = rt_file_mode_string(mode);
    const char *path_str = NULL;
    if (!mode_str || !rt_file_path_from_vstr(path, &path_str) || channel < 0)
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
        return (int32_t)err.kind;

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
