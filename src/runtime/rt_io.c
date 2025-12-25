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
#include "rt_output.h"
#include "rt_string_builder.h"

#include "rt_platform.h"

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
#if !RT_PLATFORM_WINDOWS
#include <unistd.h>
#endif

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
#if RT_PLATFORM_WINDOWS
// On Windows, define vm_trap with alternatename fallback.
// Tests can define their own vm_trap to override this.
// The alternatename directive provides the fallback when vm_trap is not
// explicitly defined by the application.
void vm_trap_default(const char *msg)
{
    rt_abort(msg);
}
// Forward declare vm_trap - resolved via alternatename or by test/app definition
extern void vm_trap(const char *msg);
#if defined(_MSC_VER) || defined(__clang__)
#pragma comment(linker, "/alternatename:vm_trap=vm_trap_default")
#endif
#else
// On Unix, use weak linkage attribute for override capability
RT_WEAK void vm_trap(const char *msg)
{
    rt_abort(msg);
}
#endif

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

// =============================================================================
// Helper Functions
// =============================================================================

/// @brief Get the length of a runtime string safely.
/// @param s Runtime string handle; may be null.
/// @return Length in bytes, or 0 if s is null or has null data.
static inline size_t rt_string_safe_len(rt_string s)
{
    if (!s || !s->data)
        return 0;
    return s->heap ? rt_heap_len(s->data) : s->literal_len;
}

/// @brief Handle string builder errors with consistent trap messages.
/// @param sb String builder to free on error.
/// @param op_name Name of the operation for error message.
/// @param status Error status from string builder operation.
static void rt_sb_check_status(rt_string_builder *sb, const char *op_name, rt_sb_status status)
{
    if (status == RT_SB_OK)
        return;

    const char *msg = op_name;
    if (status == RT_SB_ERROR_ALLOC)
    {
        // Use a static buffer to avoid allocation in error path
        static char alloc_msg[64];
        snprintf(alloc_msg, sizeof(alloc_msg), "%s: alloc", op_name);
        msg = alloc_msg;
    }
    else if (status == RT_SB_ERROR_OVERFLOW)
    {
        static char overflow_msg[64];
        snprintf(overflow_msg, sizeof(overflow_msg), "%s: overflow", op_name);
        msg = overflow_msg;
    }
    else if (status == RT_SB_ERROR_INVALID)
    {
        static char invalid_msg[64];
        snprintf(invalid_msg, sizeof(invalid_msg), "%s: invalid", op_name);
        msg = invalid_msg;
    }

    rt_sb_free(sb);
    rt_trap(msg);
}

/// @brief Write a runtime string to stdout without appending a newline.
/// @details Gracefully ignores null handles and strings with zero length.  Uses
///          the centralized output buffering system for improved performance.
///          When batch mode is active, output accumulates until the batch ends.
/// @param s Runtime string handle to print; may be null.
void rt_print_str(rt_string s)
{
    size_t len = rt_string_safe_len(s);
    if (len == 0)
        return;

    rt_output_strn(s->data, len);
}

/// @brief Print a signed 64-bit integer to stdout in decimal form.
/// @details Formats the value using the runtime string builder to avoid
///          temporary heap allocations.  Uses centralized output buffering
///          for improved performance. Formatting failures trap with a
///          descriptive message so misconfigurations become visible during
///          testing.
/// @param v Value to print.
void rt_print_i64(int64_t v)
{
    rt_string_builder sb;
    rt_sb_init(&sb);
    rt_sb_status status = rt_sb_append_int(&sb, v);
    rt_sb_check_status(&sb, "rt_print_i64", status);

    if (sb.len > 0)
        rt_output_strn(sb.data, sb.len);
    rt_sb_free(&sb);
}

/// @brief Print a floating-point number to stdout.
/// @details Uses @ref rt_format_f64 to normalise decimal separators and handle
///          special values consistently. Uses centralized output buffering for
///          improved performance.
/// @param v Double precision value to print.
void rt_print_f64(double v)
{
    char buf[64];
    rt_format_f64(v, buf, sizeof(buf));
    rt_output_str(buf);
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
///          end-of-input. Flushes output first to ensure prompts are visible.
/// @return Newly allocated runtime string without the trailing newline, or
///         @c NULL on EOF before reading data.
rt_string rt_input_line(void)
{
    // Flush output before reading input so prompts are visible
    rt_output_flush();

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

// =============================================================================
// Viper.Terminal I/O Functions
// =============================================================================

/// @brief Print a string followed by a newline.
/// @param s Runtime string to print; may be null.
void rt_term_say(rt_string s)
{
    rt_print_str(s);
    rt_output_str("\n");
}

/// @brief Print an integer followed by a newline.
/// @param v Integer value to print.
void rt_term_say_i64(int64_t v)
{
    rt_print_i64(v);
    rt_output_str("\n");
}

/// @brief Print a floating-point number followed by a newline.
/// @param v Double value to print.
void rt_term_say_f64(double v)
{
    rt_print_f64(v);
    rt_output_str("\n");
}

/// @brief Print a boolean as "true" or "false" followed by a newline.
/// @param v Boolean value (0 = false, non-zero = true).
void rt_term_say_bool(int8_t v)
{
    rt_output_str(v ? "true\n" : "false\n");
}

/// @brief Print a string without a trailing newline.
/// @param s Runtime string to print; may be null.
void rt_term_print(rt_string s)
{
    rt_print_str(s);
    rt_output_flush();
}

/// @brief Print an integer without a trailing newline.
/// @param v Integer value to print.
void rt_term_print_i64(int64_t v)
{
    rt_print_i64(v);
    rt_output_flush();
}

/// @brief Print a floating-point number without a trailing newline.
/// @param v Double value to print.
void rt_term_print_f64(double v)
{
    rt_print_f64(v);
    rt_output_flush();
}

/// @brief Print a prompt and read a line of input.
/// @param prompt Runtime string to display before reading input.
/// @return Newly allocated runtime string containing the user's input.
rt_string rt_term_ask(rt_string prompt)
{
    rt_print_str(prompt);
    rt_output_flush();
    return rt_input_line();
}

/// @brief Read a line of input from stdin.
/// @return Newly allocated runtime string containing the input line.
rt_string rt_term_read_line(void)
{
    return rt_input_line();
}
