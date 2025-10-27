//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE in the project root for license information.
//
//===----------------------------------------------------------------------===//
//
// Hosts the BASIC runtime's low-level I/O utilities, including trap handling,
// printing, console input, and file-channel bookkeeping helpers.  The
// translation unit centralises behaviour that must match the VM's semantics so
// native builds and interpreted execution surface identical diagnostics and
// boundary conditions.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief BASIC runtime I/O and trap helpers.
/// @details Provides trap bridges used by the VM, implements printing helpers
///          that mirror the runtime's formatting rules, and exposes channel-based
///          file APIs that wrap the shared @ref RtFile abstractions.  All
///          routines either abort or raise traps when invariants are violated so
///          callers never observe partial state.

#include "rt_file.h"
#include "rt_format.h"
#include "rt_int_format.h"
#include "rt_internal.h"

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

/// @brief Abort the process immediately after reporting a fatal runtime error.
/// @details Prints @p msg to @c stderr (falling back to the literal "Trap" when
///          the caller provides @c NULL) and terminates the process with exit
///          status @c 1.  The helper is used as the final sink for unrecoverable
///          runtime traps.
/// @param msg Optional UTF-8 diagnostic to surface before exiting.
/// @note This function never returns.
void rt_abort(const char *msg)
{
    if (msg && *msg)
        fprintf(stderr, "%s\n", msg);
    else
        fprintf(stderr, "Trap\n");
    exit(1);
}

/// @brief Default VM trap hook invoked by @ref rt_trap.
/// @details Marked @c weak so embedders can override it with custom trap
///          handling.  The default implementation simply delegates to
///          @ref rt_abort.
/// @param msg Diagnostic payload describing the trap condition.
/// @note This function never returns.
__attribute__((weak)) void vm_trap(const char *msg)
{
    rt_abort(msg);
}

/// @brief Raise a runtime trap observable by both the VM and native runtimes.
/// @details Forwards @p msg to @ref vm_trap so embedders can intercept traps.
///          The helper never returns and should be used by higher-level runtime
///          code whenever BASIC semantics require a fatal error.
/// @param msg Diagnostic payload describing the failure.
/// @note This function never returns.
void rt_trap(const char *msg)
{
    vm_trap(msg);
}

/// @brief Print a runtime string to @c stdout without a trailing newline.
/// @details Determines the payload length based on whether @p s references a
///          literal or heap-backed string and writes the bytes verbatim using
///          @c fwrite.  Null strings or empty payloads are ignored so callers can
///          safely forward optional values.
/// @param s Borrowed runtime string handle; may be @c NULL.
void rt_print_str(rt_string s)
{
    if (!s || !s->data)
        return;

    size_t len = 0;
    if (s->heap)
        len = rt_heap_len(s->data);
    else
        len = s->literal_len;

    if (len == 0)
        return;

    (void)fwrite(s->data, 1, len, stdout);
}

/// @brief Print a signed 64-bit integer in decimal form to @c stdout.
/// @details Attempts to format the value into a small stack buffer and
///          dynamically grows the buffer when the representation does not fit.
///          Formatting errors and allocation failures are reported via
///          @ref rt_trap so callers never observe truncated output.
/// @param v Integer to print.
void rt_print_i64(int64_t v)
{
    char stack_buf[32];
    char *buf = stack_buf;
    size_t cap = sizeof(stack_buf);
    char *heap_buf = NULL;

    size_t written = rt_i64_to_cstr(v, buf, cap);
    if (written == 0 && buf[0] == '\0')
        rt_trap("rt_print_i64: format");

    while (written + 1 >= cap)
    {
        if (cap > SIZE_MAX / 2)
        {
            free(heap_buf);
            rt_trap("rt_print_i64: overflow");
        }
        size_t new_cap = cap * 2;
        char *new_buf = (char *)malloc(new_cap);
        if (!new_buf)
        {
            free(heap_buf);
            rt_trap("rt_print_i64: alloc");
        }
        size_t new_written = rt_i64_to_cstr(v, new_buf, new_cap);
        if (new_written == 0 && new_buf[0] == '\0')
        {
            free(new_buf);
            free(heap_buf);
            rt_trap("rt_print_i64: format");
        }
        free(heap_buf);
        heap_buf = new_buf;
        buf = new_buf;
        cap = new_cap;
        written = new_written;
    }

    fwrite(buf, 1, written, stdout);
    free(heap_buf);
}

/// @brief Print a double-precision value using BASIC formatting rules.
/// @details Delegates to @ref rt_format_f64 to obtain a locale-independent
///          string representation and forwards the result to @c fputs.
/// @param v Floating-point value to print.
void rt_print_f64(double v)
{
    char buf[64];
    rt_format_f64(v, buf, sizeof(buf));
    fputs(buf, stdout);
}

/// @brief Grow an input buffer used by @ref rt_input_line.
/// @details Reallocates @p *buf to twice the previous capacity, updating both
///          the pointer and capacity on success.  Overflow or allocation
///          failures are reported via the @ref rt_input_grow_result enum so the
///          caller can raise a trap with an informative message.
/// @param buf Pointer to the input buffer pointer; must be non-null.
/// @param cap Pointer to the current capacity; must reference a valid size.
/// @return Result code indicating success, overflow, or allocation failure.
rt_input_grow_result rt_input_try_grow(char **buf, size_t *cap)
{
    if (!buf || !cap || !*buf)
        return RT_INPUT_GROW_ALLOC_FAILED;

    if (*cap > SIZE_MAX / 2)
        return RT_INPUT_GROW_OVERFLOW;

    size_t new_cap = (*cap) * 2;
    char *nbuf = (char *)realloc(*buf, new_cap);
    if (!nbuf)
        return RT_INPUT_GROW_ALLOC_FAILED;

    *buf = nbuf;
    *cap = new_cap;
    return RT_INPUT_GROW_OK;
}

/// @brief Read a single line of console input into a freshly allocated string.
/// @details Consumes bytes from @c stdin until a newline or EOF is observed,
///          growing the internal buffer via @ref rt_input_try_grow.  Trailing
///          carriage returns are stripped to match BASIC semantics.  When EOF is
///          encountered before any character is read, the function returns
///          @c NULL so callers can distinguish between end-of-file and empty
///          lines.
/// @return Runtime string containing the line contents, or @c NULL on EOF with
///         no data.
rt_string rt_input_line(void)
{
    size_t cap = 1024;
    size_t len = 0;
    char *buf = (char *)rt_alloc(cap);
    for (;;)
    {
        int ch = fgetc(stdin);
        if (ch == EOF)
        {
            if (len == 0)
            {
                free(buf);
                return NULL;
            }
            break;
        }
        if (ch == '\n')
            break;
        if (len + 1 >= cap)
        {
            rt_input_grow_result grow = rt_input_try_grow(&buf, &cap);
            if (grow == RT_INPUT_GROW_OVERFLOW)
            {
                free(buf);
                rt_trap("rt_input_line: overflow");
                return NULL;
            }
            if (grow != RT_INPUT_GROW_OK)
            {
                free(buf);
                rt_trap("out of memory");
                return NULL;
            }
        }
        buf[len++] = (char)ch;
    }
    if (len > 0 && buf[len - 1] == '\r')
        len--;
    buf[len] = '\0';
    rt_string s = (rt_string)rt_alloc(sizeof(*s));
    char *payload = (char *)rt_heap_alloc(RT_HEAP_STRING, RT_ELEM_NONE, 1, len, len + 1);
    if (!payload)
    {
        free(buf);
        rt_trap("out of memory");
        return NULL;
    }
    memcpy(payload, buf, len + 1);
    s->data = payload;
    rt_heap_hdr_t *hdr = rt_heap_hdr(payload);
    assert(hdr);
    assert(hdr->kind == RT_HEAP_STRING);
    s->heap = hdr;
    s->literal_len = 0;
    s->literal_refs = 0;
    free(buf);
    return s;
}

/// @brief Split a CSV-style BASIC input line into fields.
/// @details Parses @p line, handling quoted segments and doubled quote escapes.
///          Up to @p max_fields results are stored into @p out_fields; excess
///          fields continue to be counted so the caller can detect mismatches.
///          The function trims leading/trailing whitespace for unquoted fields
///          and allocates new runtime strings for each stored value.
/// @param line       Input line to split; may be @c NULL to denote empty.
/// @param out_fields Destination array that receives up to @p max_fields strings.
/// @param max_fields Maximum number of elements to write; negative values are
///                   treated as zero.
/// @return Total number of fields present in @p line.
int64_t rt_split_fields(rt_string line, rt_string *out_fields, int64_t max_fields)
{
    if (max_fields <= 0)
    {
        max_fields = 0;
    }
    else if (!out_fields)
    {
        rt_trap("rt_split_fields: null output");
    }

    const char *data = "";
    size_t len = 0;
    if (line && line->data)
    {
        data = line->data;
        if (line->heap)
            len = rt_heap_len(line->data);
        else
            len = line->literal_len;
    }

    int64_t stored = 0;
    int64_t total = 0;
    size_t start = 0;
    bool in_quotes = false;
    size_t i = 0;
    while (i <= len)
    {
        bool finalize = false;
        if (i == len)
        {
            finalize = true;
        }
        else
        {
            char ch = data[i];
            if (ch == '"')
            {
                if (in_quotes)
                {
                    if (i + 1 < len && data[i + 1] == '"')
                    {
                        ++i;
                    }
                    else
                    {
                        in_quotes = false;
                    }
                }
                else
                {
                    in_quotes = true;
                }
            }
            else if (ch == ',' && !in_quotes)
            {
                finalize = true;
            }
        }

        if (finalize)
        {
            size_t field_start = start;
            size_t field_end = i;
            while (field_start < field_end && isspace((unsigned char)data[field_start]))
                ++field_start;
            while (field_end > field_start && isspace((unsigned char)data[field_end - 1]))
                --field_end;
            bool had_quotes = false;
            if (field_end > field_start && data[field_start] == '"' && data[field_end - 1] == '"')
            {
                ++field_start;
                --field_end;
                had_quotes = true;
            }

            if (stored < max_fields)
            {
                if (field_end <= field_start)
                {
                    out_fields[stored++] = rt_str_empty();
                }
                else
                {
                    if (had_quotes)
                    {
                        size_t unescaped_len = 0;
                        size_t j = field_start;
                        while (j < field_end)
                        {
                            if (data[j] == '"' && j + 1 < field_end && data[j + 1] == '"')
                            {
                                ++j;
                            }
                            ++unescaped_len;
                            ++j;
                        }

                        char *tmp = (char *)malloc(unescaped_len);
                        if (!tmp && unescaped_len > 0)
                        {
                            rt_trap("out of memory");
                        }

                        j = field_start;
                        size_t write = 0;
                        while (j < field_end)
                        {
                            char ch = data[j];
                            if (ch == '"' && j + 1 < field_end && data[j + 1] == '"')
                            {
                                tmp[write++] = '"';
                                j += 2;
                            }
                            else
                            {
                                tmp[write++] = ch;
                                ++j;
                            }
                        }

                        out_fields[stored++] = rt_string_from_bytes(tmp, unescaped_len);
                        free(tmp);
                    }
                    else
                    {
                        out_fields[stored++] =
                            rt_string_from_bytes(data + field_start, field_end - field_start);
                    }
                }
            }
            ++total;
            start = i + 1;
        }

        ++i;
    }

    if (max_fields > 0 && total < max_fields)
    {
        char msg[128];
        snprintf(msg,
                 sizeof(msg),
                 "INPUT: expected %lld value%s, got %lld",
                 (long long)max_fields,
                 max_fields == 1 ? "" : "s",
                 (long long)total);
        rt_trap(msg);
    }

    return total;
}

/// @brief Compute BASIC EOF status for a file channel.
/// @details Consults the cached EOF bit before attempting to @c lseek the file
///          descriptor associated with @p ch.  On success the EOF cache is
///          updated and either @c -1 (EOF) or @c 0 (more data) is returned.  On
///          failure a BASIC error code is surfaced.
/// @param ch BASIC channel number previously opened via runtime APIs.
/// @return @c -1 when EOF has been reached, @c 0 when more data is available, or
///         a positive BASIC error code on failure.
int rt_eof_ch(int ch)
{
    int fd = -1;
    int32_t status = rt_file_channel_fd(ch, &fd);
    if (status != 0)
        return status;

    bool cached = false;
    status = rt_file_channel_get_eof(ch, &cached);
    if (status != 0)
        return status;

    errno = 0;
    off_t cur = lseek(fd, 0, SEEK_CUR);
    if (cur >= 0)
    {
        off_t end = lseek(fd, 0, SEEK_END);
        if (end < 0)
        {
            (void)lseek(fd, cur, SEEK_SET);
            rt_file_channel_set_eof(ch, false);
            return (int32_t)Err_IOError;
        }
        if (lseek(fd, cur, SEEK_SET) < 0)
        {
            rt_file_channel_set_eof(ch, false);
            return (int32_t)Err_IOError;
        }
        if (end <= cur)
        {
            rt_file_channel_set_eof(ch, true);
            return -1;
        }
        rt_file_channel_set_eof(ch, false);
        return 0;
    }

    if (errno == ESPIPE || errno == EINVAL)
        return cached ? -1 : 0;

    rt_file_channel_set_eof(ch, false);
    return (int32_t)Err_IOError;
}

/// @brief Compute the length of the file bound to a BASIC channel.
/// @details Prefers @c fstat for regular and block devices, falling back to
///          @c lseek-based probing for seekable descriptors.  Errors are reported
///          as negative BASIC error codes so callers can propagate diagnostics.
/// @param ch BASIC channel number.
/// @return Non-negative file length in bytes on success; otherwise a negative
///         BASIC error code.
int64_t rt_lof_ch(int ch)
{
    int fd = -1;
    int32_t status = rt_file_channel_fd(ch, &fd);
    if (status != 0)
        return -(int64_t)status;

    struct stat st;
    if (fstat(fd, &st) == 0)
    {
        if (S_ISREG(st.st_mode) || S_ISBLK(st.st_mode))
        {
            if (st.st_size >= 0)
                return (int64_t)st.st_size;
            return 0;
        }
    }

    errno = 0;
    off_t cur = lseek(fd, 0, SEEK_CUR);
    if (cur < 0)
    {
        if (errno == ESPIPE || errno == EINVAL)
            return -(int64_t)Err_InvalidOperation;
        return -(int64_t)Err_IOError;
    }

    off_t end = lseek(fd, 0, SEEK_END);
    if (end < 0)
    {
        (void)lseek(fd, cur, SEEK_SET);
        return -(int64_t)Err_IOError;
    }

    if (lseek(fd, cur, SEEK_SET) < 0)
        return -(int64_t)Err_IOError;

    if (end < 0)
        return 0;

    return (int64_t)end;
}

/// @brief Report the current file position for a BASIC channel.
/// @details Uses @c lseek to query the descriptor's offset.  For non-seekable
///          descriptors a BASIC invalid-operation error is returned.
/// @param ch BASIC channel number.
/// @return Non-negative byte offset on success; otherwise a negative BASIC
///         error code.
int64_t rt_loc_ch(int ch)
{
    int fd = -1;
    int32_t status = rt_file_channel_fd(ch, &fd);
    if (status != 0)
        return -(int64_t)status;

    errno = 0;
    off_t cur = lseek(fd, 0, SEEK_CUR);
    if (cur < 0)
    {
        if (errno == ESPIPE || errno == EINVAL)
            return -(int64_t)Err_InvalidOperation;
        return -(int64_t)Err_IOError;
    }

    if (cur < 0)
        return 0;

    return (int64_t)cur;
}

/// @brief Seek to an absolute file position for a BASIC channel.
/// @details Validates that @p pos is non-negative, converts it to an @c off_t,
///          and invokes @c lseek.  On success the cached EOF state for the
///          channel is cleared.
/// @param ch  BASIC channel number to reposition.
/// @param pos Desired byte offset from the beginning of the file.
/// @return @c 0 on success or a BASIC error code on failure.
int32_t rt_seek_ch_err(int ch, int64_t pos)
{
    if (pos < 0)
        return (int32_t)Err_InvalidOperation;

    int fd = -1;
    int32_t status = rt_file_channel_fd(ch, &fd);
    if (status != 0)
        return status;

    errno = 0;
    off_t target = (off_t)pos;
    if (target < 0)
        return (int32_t)Err_InvalidOperation;

    off_t res = lseek(fd, target, SEEK_SET);
    if (res == (off_t)-1)
    {
        if (errno == ESPIPE || errno == EINVAL)
            return (int32_t)Err_InvalidOperation;
        return (int32_t)Err_IOError;
    }

    (void)rt_file_channel_set_eof(ch, false);
    return 0;
}
