//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE in the project root for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the BASIC runtime's channel-based file helpers.  The routines
// manage a dynamic table of open files keyed by BASIC channel numbers, wrap the
// lower-level `RtFile` primitives, and translate failures into `Err_*`
// diagnostics so the VM and native implementations expose identical behaviour.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Channel management helpers for BASIC runtime file I/O.
/// @details Provides allocation, resolution, and I/O utilities that convert
///          between BASIC channel integers and the runtime's `RtFile`
///          structures while keeping error signalling consistent.

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

/// @brief Locate an existing channel entry without modifying the table.
/// @details Returns a pointer to the tracked entry when @p channel is valid and
///          active; otherwise returns `NULL` so callers can decide whether to
///          report `Err_InvalidOperation` or allocate a new slot.
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

/// @brief Ensure a table entry exists for @p channel, allocating if necessary.
/// @details Reuses an existing entry when present, otherwise grows the backing
///          array, initialises new slots, and returns the freshly reserved
///          entry.  Returns `NULL` when allocation fails or @p channel is
///          negative.
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

/// @brief Resolve @p channel to an open entry, returning a runtime error code.
/// @details Validates that the channel is non-negative, currently in use, and
///          has an open file descriptor.  When successful, stores the pointer in
///          @p out_entry and returns zero.
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

/// @brief Write @p len bytes to the channel, updating EOF tracking.
/// @details Delegates to @ref rt_file_write and clears the channel's EOF flag on
///          success.  Returns the runtime error code produced by the write.
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

/// @brief Open a BASIC channel for the path stored in a runtime string.
/// @details Converts the provided @ref ViperString to a host path, opens the
///          file using the computed mode string, and records the resulting
///          descriptor in the channel table.  Returns zero on success or a
///          runtime error code on failure.
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

/// @brief Close the file associated with @p channel.
/// @details Validates that the channel exists and is open, closes the underlying
///          descriptor, and resets bookkeeping state.  Returns zero on success or
///          propagates the runtime error code raised by @ref rt_file_close.
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

/// @brief Write the contents of @p s to a channel without appending a newline.
/// @details Resolves the channel, extracts the string's byte view, and delegates
///          to @ref rt_file_write_entry to perform the write.
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

/// @brief Write @p s followed by a newline to the specified channel.
/// @details Performs the same resolution as @ref rt_write_ch_err and then emits
///          a trailing `\n` character to mirror BASIC's PRINT semantics.
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

/// @brief Read a line of text from @p channel, allocating a runtime string.
/// @details Invokes @ref rt_file_read_line and returns the resulting string via
///          @p out.  EOF conditions are recorded on the channel and reported via
///          the returned error code.
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

/// @brief Retrieve the host file descriptor associated with @p channel.
/// @details Resolves the channel and copies the descriptor into @p out_fd when
///          non-null, returning zero on success.
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

/// @brief Query whether @p channel is currently positioned at EOF.
/// @details Reads the cached EOF flag updated by read operations and stores it
///          in @p out_at_eof when provided.
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

/// @brief Mutate the cached EOF state for @p channel.
/// @details Allows the runtime to synchronise its notion of EOF with external
///          operations such as SEEK.
int32_t rt_file_channel_set_eof(int32_t channel, bool at_eof)
{
    RtFileChannelEntry *entry = NULL;
    int32_t status = rt_file_resolve_channel(channel, &entry);
    if (status != 0)
        return status;
    entry->at_eof = at_eof;
    return 0;
}
