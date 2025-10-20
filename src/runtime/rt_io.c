// File: src/runtime/rt_io.c
// Purpose: Implements I/O utilities and trap handling for the BASIC runtime.
// Key invariants: Output routines do not append newlines unless specified.
// Ownership/Lifetime: Caller manages strings passed to printing routines.
// Links: docs/codemap.md

#include "rt_internal.h"
#include "rt_format.h"
#include "rt_int_format.h"
#include "rt_file.h"

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>

/**
 * Terminate the program immediately due to a fatal runtime error.
 *
 * @param msg Optional message describing the reason for the abort.
 * @return Never returns.
 */
void rt_abort(const char *msg)
{
    if (msg && *msg)
        fprintf(stderr, "%s\n", msg);
    else
        fprintf(stderr, "Trap\n");
    exit(1);
}

/**
 * Trap handler used by the VM layer. Can be overridden by hosts.
 *
 * @param msg Optional message describing the trap condition.
 * @return Never returns.
 */
__attribute__((weak)) void vm_trap(const char *msg)
{
    rt_abort(msg);
}

/**
 * Entry point for raising runtime traps from helper routines.
 *
 * @param msg Message describing the trap condition.
 * @return Never returns.
 */
void rt_trap(const char *msg)
{
    vm_trap(msg);
}

/**
 * Write a runtime string to standard output without a trailing newline.
 *
 * @param s String to print; NULL strings are ignored.
 */
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

/**
 * Print a 64-bit integer in decimal form to stdout.
 *
 * @param v Value to print.
 */
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
            if (heap_buf)
                free(heap_buf);
            rt_trap("rt_print_i64: overflow");
        }
        size_t new_cap = cap * 2;
        char *new_buf = (char *)malloc(new_cap);
        if (!new_buf)
        {
            if (heap_buf)
                free(heap_buf);
            rt_trap("rt_print_i64: alloc");
        }
        size_t new_written = rt_i64_to_cstr(v, new_buf, new_cap);
        if (new_written == 0 && new_buf[0] == '\0')
        {
            free(new_buf);
            if (heap_buf)
                free(heap_buf);
            rt_trap("rt_print_i64: format");
        }
        if (heap_buf)
            free(heap_buf);
        heap_buf = new_buf;
        buf = new_buf;
        cap = new_cap;
        written = new_written;
    }

    fwrite(buf, 1, written, stdout);
    if (heap_buf)
        free(heap_buf);
}

/**
 * Print a double-precision floating-point number to stdout.
 *
 * @param v Value to print.
 */
void rt_print_f64(double v)
{
    char buf[64];
    rt_format_f64(v, buf, sizeof(buf));
    fputs(buf, stdout);
}

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

/**
 * Read a single line of input from stdin into a runtime string.
 *
 * @return Newly allocated runtime string containing the line without the
 * trailing newline, or NULL on EOF before any characters are read.
 */
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

/**
 * Split a comma-separated input line into individual fields.
 *
 * @param line       Runtime string containing the raw input line.
 * @param out_fields Destination array receiving up to @p max_fields entries.
 * @param max_fields Maximum number of fields to populate; negative treated as zero.
 * @return Total number of fields present in @p line.
 */
int64_t rt_split_fields(rt_string line, rt_string *out_fields, int64_t max_fields)
{
    if (!out_fields)
        rt_trap("rt_split_fields: null output");
    if (max_fields <= 0)
        return 0;

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
            if (field_end > field_start && data[field_start] == '"' && data[field_end - 1] == '"')
            {
                ++field_start;
                --field_end;
            }

            if (stored < max_fields)
            {
                if (field_end <= field_start)
                {
                    out_fields[stored++] = rt_str_empty();
                }
                else
                {
                    out_fields[stored++] =
                        rt_string_from_bytes(data + field_start, field_end - field_start);
                }
            }
            ++total;
            start = i + 1;
        }

        ++i;
    }

    if (total < max_fields)
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
