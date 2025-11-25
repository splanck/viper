//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/rt_io.c
// Purpose: Provide the native implementations of BASIC's PRINT/INPUT/file I/O
//          intrinsics, mirroring the VM's behaviour exactly.
// Key invariants: Trap handling always routes through rt_trap/vm_trap, newline
//                 conventions stay consistent with historical BASIC (CRLF
//                 tolerant input, LF output), channel bookkeeping preserves EOF
//                 semantics across seeks, and helpers never assume ownership of
//                 caller-supplied buffers.
// Links: docs/runtime/io.md
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief BASIC runtime I/O primitives shared by the VM and native builds.
/// @details Defines printing helpers, line-oriented input routines, CSV field
///          splitting, and file-channel positioning utilities.  Each entry
///          point validates arguments, converts OS errors into runtime error
///          codes, and coordinates with the channel cache to keep EOF and
///          position metadata coherent.

#include "rt_file.h"
#include "rt_format.h"
#include "rt_int_format.h"
#include "rt_internal.h"
#include "rt_string_builder.h"

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

/// @brief Terminate the runtime immediately due to a fatal condition.
/// @details Prints @p msg to stderr when provided, otherwise emits the generic
///          "Trap" sentinel before exiting with status code 1.  The function is
///          the last-resort termination path for unrecoverable runtime failures
///          and therefore never returns.
/// @param msg Optional diagnostic message describing the reason for the abort.
/// @return This function does not return.
void rt_abort(const char *msg)
{
    if (msg && *msg)
        fprintf(stderr, "%s\n", msg);
    else
        fprintf(stderr, "Trap\n");
    exit(1);
}

/// @brief Default trap handler invoked by helper routines.
/// @details Marked @c weak so embedders can override the implementation.  The
///          default delegates to @ref rt_abort so that traps terminate the
///          process with the provided diagnostic message.
/// @param msg Optional message describing the trap condition.
/// @return This function does not return.
__attribute__((weak)) void vm_trap(const char *msg)
{
    rt_abort(msg);
}

/// @brief Raise a runtime trap using the currently configured trap handler.
/// @details Simply forwards the message to @ref vm_trap so that tools or
///          embedders can install custom behaviour by overriding the weak
///          symbol.
/// @param msg Null-terminated string describing the trap condition.
/// @return This function does not return.
void rt_trap(const char *msg)
{
    vm_trap(msg);
}

/// @brief Write a runtime string to stdout without appending a newline.
/// @details Gracefully ignores null handles and strings with zero length.  Heap
///          backed strings compute their length via @ref rt_heap_len while
///          literal strings use the cached literal length.  The routine performs
///          no buffering beyond the direct @c fwrite call.
/// @param s Runtime string handle to print; may be null.
void rt_print_str(rt_string s)
{
    if (!s || !s->data)
        return;

    size_t len = 0;
    if (s->heap)
    {
        len = rt_heap_len(s->data);
    }
    else
    {
        len = s->literal_len;
    }

    if (len == 0)
        return;

    (void)fwrite(s->data, 1, len, stdout);
}

/// @brief Print a signed 64-bit integer to stdout in decimal form.
/// @details Formats the value using the runtime string builder to avoid
///          temporary heap allocations.  Formatting failures trap with a
///          descriptive message so misconfigurations become visible during
///          testing.
/// @param v Value to print.
void rt_print_i64(int64_t v)
{
    rt_string_builder sb;
    rt_sb_init(&sb);
    rt_sb_status status = rt_sb_append_int(&sb, v);
    if (status != RT_SB_OK)
    {
        const char *msg = "rt_print_i64: format";
        if (status == RT_SB_ERROR_ALLOC)
            msg = "rt_print_i64: alloc";
        else if (status == RT_SB_ERROR_OVERFLOW)
            msg = "rt_print_i64: overflow";
        else if (status == RT_SB_ERROR_INVALID)
            msg = "rt_print_i64: invalid";
        rt_sb_free(&sb);
        rt_trap(msg);
    }

    if (sb.len > 0)
        (void)fwrite(sb.data, 1, sb.len, stdout);
    rt_sb_free(&sb);
}

/// @brief Print a floating-point number to stdout.
/// @details Uses @ref rt_format_f64 to normalise decimal separators and handle
///          special values consistently before writing the formatted bytes to
///          stdout.
/// @param v Double precision value to print.
void rt_print_f64(double v)
{
    char buf[64];
    rt_format_f64(v, buf, sizeof(buf));
    fputs(buf, stdout);
}

/// @brief Grow an input buffer used by @ref rt_input_line.
/// @details Doubles the allocation when possible while guarding against
///          overflow and allocation failure.  The helper mutates @p buf and
///          @p cap in place and returns a status enumerator so callers can
///          distinguish between error conditions.
/// @param buf [in,out] Pointer to the buffer pointer to grow.
/// @param cap [in,out] Pointer to the capacity counter associated with @p buf.
/// @return Result enumerator describing whether the buffer was resized.
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

/// @brief Read a single line of input from stdin into a runtime string.
/// @details Allocates a temporary buffer, grows it as needed, strips the
///          trailing newline and optional carriage return, and returns a newly
///          allocated @ref rt_string that owns the resulting characters.  On
///          EOF before any bytes are read the function returns @c NULL to signal
///          end-of-input.
/// @return Newly allocated runtime string without the trailing newline, or
///         @c NULL on EOF before reading data.
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
    // Construct runtime string from the accumulated buffer and release temp.
    rt_string s = rt_string_from_bytes(buf, len);
    free(buf);
    return s;
}

/// @brief Split a comma-separated input line into runtime string fields.
/// @details Parses @p line while respecting quoted fields and doubled quotes.
///          Extracted fields are trimmed of leading and trailing whitespace and
///          materialised as runtime strings stored in @p out_fields until
///          @p max_fields entries have been populated.  When fewer fields are
///          present than expected the function traps with a descriptive error.
/// @param line Runtime string containing the raw input line.
/// @param out_fields Destination array receiving up to @p max_fields entries.
/// @param max_fields Maximum number of fields to populate; negative values are treated as zero.
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

/// @brief Determine whether a file channel has reached EOF.
/// @details Consults cached EOF information and falls back to probing the file
///          descriptor via @ref lseek when necessary.  Updates the cached state
///          to reflect the probed result and returns runtime error codes on
///          failure.
/// @param ch Channel identifier registered with the runtime file subsystem.
/// @return Negative @ref Err value on failure, -1 when at EOF, or 0 when more data is available.
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

/// @brief Query the length of the file bound to a channel.
/// @details Uses @ref fstat for regular files and blocks, falling back to
///          seeking to the end when necessary.  Errors are negated runtime error
///          codes so callers can propagate them through BASIC's error handling
///          conventions.
/// @param ch Channel identifier registered with the runtime file subsystem.
/// @return File length in bytes, or the negated runtime error code on failure.
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

/// @brief Report the current file position for the supplied channel.
/// @details Reads the file descriptor offset using @ref lseek and converts OS
///          failures into negated runtime error codes.  Special files such as
///          pipes yield @ref Err_InvalidOperation in keeping with BASIC's
///          semantics.
/// @param ch Channel identifier.
/// @return Current offset in bytes, or a negated runtime error code on failure.
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

/// @brief Seek to a byte offset on the channel's underlying file descriptor.
/// @details Validates @p pos, issues the seek via @ref lseek, clears the cached
///          EOF flag on success, and translates platform-specific failures into
///          BASIC runtime error codes.
/// @param ch Channel identifier.
/// @param pos Absolute offset to seek to; must be non-negative.
/// @return Zero on success or a runtime error code on failure.
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
