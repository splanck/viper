//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE in the project root for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/rt_file.c
// Purpose: Maintain the BASIC runtime's channel table and expose the legacy
//          file I/O ABI in terms of runtime error codes.
// Key invariants: Channel identifiers map 1:1 to table entries, each entry
//                 tracks whether a file descriptor is open, EOF state is cached
//                 eagerly to emulate the VM, and all failures are reported as
//                 Err_* enumerators.  Table growth doubles capacity to amortise
//                 allocations while keeping handles stable.
// Links: docs/runtime/files.md#channels
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Channel management helpers for BASIC runtime file I/O.
/// @details Provides allocation, resolution, and I/O utilities that convert
///          between BASIC channel integers and the runtime's `RtFile`
///          structures while keeping error signalling consistent.

#include "rt_file.h"

#include "rt_file_path.h"

#include "rt_context.h"
#include "rt_internal.h"
#include <stdbool.h>
#include <stdlib.h>

typedef struct RtFileChannelEntry
{
    int32_t channel;
    RtFile file;
    bool in_use;
    bool at_eof;
} RtFileChannelEntry;

void rt_file_state_cleanup(RtContext *ctx)
{
    if (!ctx)
        return;

    RtFileChannelEntry *entries = (RtFileChannelEntry *)ctx->file_state.entries;
    size_t count = ctx->file_state.count;
    if (entries)
    {
        for (size_t i = 0; i < count; ++i)
        {
            RtFileChannelEntry *entry = &entries[i];
            if (entry->in_use)
            {
                RtError err = RT_ERROR_NONE;
                (void)rt_file_close(&entry->file, &err);
                entry->in_use = false;
                entry->at_eof = false;
                rt_file_init(&entry->file);
            }
        }
        free(entries);
    }

    ctx->file_state.entries = NULL;
    ctx->file_state.count = 0;
    ctx->file_state.capacity = 0;
}

static inline RtContext *rt_get_or_legacy(void)
{
    RtContext *ctx = rt_get_current_context();
    if (!ctx)
        return rt_legacy_context();
    return ctx;
}

static inline RtFileChannelEntry *rtf_entries(void)
{
    RtContext *ctx = rt_get_or_legacy();
    return (RtFileChannelEntry *)ctx->file_state.entries;
}

static inline size_t *rtf_count(void)
{
    RtContext *ctx = rt_get_or_legacy();
    return &ctx->file_state.count;
}

static inline size_t *rtf_capacity(void)
{
    RtContext *ctx = rt_get_or_legacy();
    return &ctx->file_state.capacity;
}

static inline void rtf_set_entries(RtFileChannelEntry *ptr)
{
    RtContext *ctx = rt_get_or_legacy();
    ctx->file_state.entries = ptr;
}

/// @brief Locate an existing channel entry without modifying the table.
/// @details Performs a linear scan over the populated prefix of the table so
///          channel handles remain stable even after reallocations.  Negative
///          identifiers are rejected immediately.  Callers can reuse the result
///          to inspect channel state or decide whether a new slot must be
///          materialised.
static RtFileChannelEntry *rt_file_find_channel(int32_t channel)
{
    if (channel < 0)
        return NULL;
    RtFileChannelEntry *entries = rtf_entries();
    size_t count = entries ? *rtf_count() : 0;
    for (size_t i = 0; i < count; ++i)
    {
        if (entries[i].channel == channel)
            return &entries[i];
    }
    return NULL;
}

/// @brief Ensure a table entry exists for @p channel, allocating if necessary.
/// @details Reuses an existing entry when one already tracks the identifier.
///          Otherwise the table grows geometrically, new slots are initialised
///          via @ref rt_file_init, and the freshly provisioned entry is returned
///          to the caller.  Allocation failures bubble up as @c NULL so callers
///          can surface @ref Err_RuntimeError.
static RtFileChannelEntry *rt_file_prepare_channel(int32_t channel)
{
    if (channel < 0)
        return NULL;
    RtFileChannelEntry *entry = rt_file_find_channel(channel);
    if (entry)
        return entry;

    RtFileChannelEntry *entries = rtf_entries();
    size_t *pcount = rtf_count();
    size_t *pcap = rtf_capacity();
    if (!pcount || !pcap)
        return NULL;
    if (*pcount == *pcap)
    {
        size_t new_capacity = *pcap ? (*pcap) * 2 : 4;
        RtFileChannelEntry *new_entries =
            (RtFileChannelEntry *)realloc(entries, new_capacity * sizeof(*new_entries));
        if (!new_entries)
            return NULL;
        for (size_t i = *pcap; i < new_capacity; ++i)
        {
            new_entries[i].channel = 0;
            new_entries[i].in_use = false;
            new_entries[i].at_eof = false;
            rt_file_init(&new_entries[i].file);
        }
        rtf_set_entries(new_entries);
        *pcap = new_capacity;
    }
    entries = rtf_entries();
    entry = &entries[(*pcount)++];
    entry->channel = channel;
    entry->in_use = false;
    entry->at_eof = false;
    rt_file_init(&entry->file);
    return entry;
}

/// @brief Resolve @p channel to an open entry, returning a runtime error code.
/// @details Validates the channel identifier, ensures the entry is actively in
///          use, and confirms that the cached @ref RtFile still owns a live
///          descriptor.  When successful the resolved entry is stored in
///          @p out_entry so callers can perform further operations without a
///          second lookup.
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
/// @details Validates pointers, forwards the call to @ref rt_file_write, clears
///          the cached EOF state when the write succeeds, and translates any
///          failure into the corresponding Err_* value.
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
/// @details Translates the BASIC mode enum into a host mode string, converts the
///          runtime string path into a filesystem path, allocates or reuses a
///          channel entry, and invokes @ref rt_file_open.  When the open
///          succeeds the entry is flagged in-use and its EOF indicator cleared;
///          failures propagate the error kind from the lower layer.
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
    if (!rt_file_open(&entry->file, path_str, mode_str, mode, &err))
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
/// @details Resolves the channel, obtains a byte slice via
///          @ref rt_file_string_view, and then calls
///          @ref rt_file_write_entry so EOF caching and error translation remain
///          centralised in one helper.
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
/// @details Resolves the channel, writes the provided bytes, and finally emits a
///          single newline so the behaviour matches PRINT without a trailing
///          semicolon in traditional BASIC.
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
/// @details Resolves the channel, delegates to @ref rt_file_read_line to perform
///          the blocking read, marks the cached EOF flag when the helper reports
///          end-of-file, and on success transfers ownership of the allocated
///          runtime string to @p out.
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
/// @details Resolves the channel and copies the descriptor into @p out_fd, if
///          provided, so embedders can integrate with poll/select loops using the
///          underlying OS handle.
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
/// @details Resolves the channel and exposes the cached EOF flag maintained by
///          read helpers, mirroring the VM's "sticky" EOF semantics.
int32_t rt_file_channel_get_eof(int32_t channel, int8_t *out_at_eof)
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
/// @details Resolves the channel and updates the cached flag, enabling seek
///          helpers to force EOF on or off without performing another read.
int32_t rt_file_channel_set_eof(int32_t channel, int8_t at_eof)
{
    RtFileChannelEntry *entry = NULL;
    int32_t status = rt_file_resolve_channel(channel, &entry);
    if (status != 0)
        return status;
    entry->at_eof = at_eof;
    return 0;
}
