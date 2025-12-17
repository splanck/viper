//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/rt_linereader.c
// Purpose: Implement line-by-line text file reading.
//
// Handles CR, LF, and CRLF line endings:
// - LF (\n): Unix/Linux/macOS
// - CR (\r): Classic Mac
// - CRLF (\r\n): Windows
//
//===----------------------------------------------------------------------===//

#include "rt_linereader.h"

#include "rt_internal.h"
#include "rt_object.h"
#include "rt_string.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/// @brief LineReader implementation structure.
typedef struct rt_linereader_impl
{
    FILE *fp;       ///< File pointer.
    int8_t eof;     ///< EOF flag.
    int8_t closed;  ///< Closed flag.
    int peeked;     ///< Peeked character (-1 if none, or 0-255).
    int has_peeked; ///< Whether we have a peeked character.
} rt_linereader_impl;

static void rt_linereader_finalize(void *obj)
{
    if (!obj)
        return;
    rt_linereader_impl *lr = (rt_linereader_impl *)obj;
    if (lr->fp && !lr->closed)
    {
        fclose(lr->fp);
        lr->fp = NULL;
        lr->closed = 1;
    }
}

void *rt_linereader_open(rt_string path)
{
    if (!path)
    {
        rt_trap("LineReader.Open: null path");
        return NULL;
    }

    const char *path_str = rt_string_cstr(path);
    if (!path_str)
    {
        rt_trap("LineReader.Open: invalid path");
        return NULL;
    }

    FILE *fp = fopen(path_str, "r");
    if (!fp)
    {
        rt_trap("LineReader.Open: failed to open file");
        return NULL;
    }

    rt_linereader_impl *lr =
        (rt_linereader_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_linereader_impl));
    if (!lr)
    {
        fclose(fp);
        rt_trap("LineReader.Open: memory allocation failed");
        return NULL;
    }

    lr->fp = fp;
    lr->eof = 0;
    lr->closed = 0;
    lr->peeked = -1;
    lr->has_peeked = 0;
    rt_obj_set_finalizer(lr, rt_linereader_finalize);

    return lr;
}

void rt_linereader_close(void *obj)
{
    if (!obj)
        return;

    rt_linereader_impl *lr = (rt_linereader_impl *)obj;
    if (lr->fp && !lr->closed)
    {
        fclose(lr->fp);
        lr->fp = NULL;
        lr->closed = 1;
    }
}

/// @brief Internal: get next character, handling peek buffer.
static int lr_getc(rt_linereader_impl *lr)
{
    if (lr->has_peeked)
    {
        lr->has_peeked = 0;
        return lr->peeked;
    }
    return fgetc(lr->fp);
}

rt_string rt_linereader_read(void *obj)
{
    if (!obj)
    {
        rt_trap("LineReader.Read: null reader");
        return rt_string_from_bytes("", 0);
    }

    rt_linereader_impl *lr = (rt_linereader_impl *)obj;
    if (!lr->fp || lr->closed)
    {
        rt_trap("LineReader.Read: reader is closed");
        return rt_string_from_bytes("", 0);
    }

    // Dynamic buffer for the line
    size_t cap = 256;
    size_t len = 0;
    char *buf = (char *)malloc(cap);
    if (!buf)
    {
        rt_trap("LineReader.Read: memory allocation failed");
        return rt_string_from_bytes("", 0);
    }

    int c;
    while ((c = lr_getc(lr)) != EOF)
    {
        if (c == '\n')
        {
            // LF or end of CRLF - line complete
            break;
        }
        else if (c == '\r')
        {
            // CR - check for CRLF
            int next = fgetc(lr->fp);
            if (next != '\n' && next != EOF)
            {
                // Standalone CR, put back the next char
                lr->peeked = next;
                lr->has_peeked = 1;
            }
            // Either way, line is complete
            break;
        }
        else
        {
            // Regular character - add to buffer
            if (len >= cap - 1)
            {
                cap *= 2;
                char *new_buf = (char *)realloc(buf, cap);
                if (!new_buf)
                {
                    free(buf);
                    rt_trap("LineReader.Read: memory allocation failed");
                    return rt_string_from_bytes("", 0);
                }
                buf = new_buf;
            }
            buf[len++] = (char)c;
        }
    }

    if (c == EOF && len == 0)
    {
        // EOF with no content - set EOF flag
        lr->eof = 1;
        free(buf);
        return rt_string_from_bytes("", 0);
    }

    if (c == EOF)
    {
        // Got content but hit EOF
        lr->eof = 1;
    }

    rt_string result = rt_string_from_bytes(buf, len);
    free(buf);
    return result;
}

int64_t rt_linereader_read_char(void *obj)
{
    if (!obj)
    {
        rt_trap("LineReader.ReadChar: null reader");
        return -1;
    }

    rt_linereader_impl *lr = (rt_linereader_impl *)obj;
    if (!lr->fp || lr->closed)
    {
        rt_trap("LineReader.ReadChar: reader is closed");
        return -1;
    }

    int c = lr_getc(lr);
    if (c == EOF)
    {
        lr->eof = 1;
        return -1;
    }

    return (int64_t)(unsigned char)c;
}

int64_t rt_linereader_peek_char(void *obj)
{
    if (!obj)
    {
        rt_trap("LineReader.PeekChar: null reader");
        return -1;
    }

    rt_linereader_impl *lr = (rt_linereader_impl *)obj;
    if (!lr->fp || lr->closed)
    {
        rt_trap("LineReader.PeekChar: reader is closed");
        return -1;
    }

    if (lr->has_peeked)
    {
        return (int64_t)lr->peeked;
    }

    int c = fgetc(lr->fp);
    if (c == EOF)
    {
        lr->eof = 1;
        return -1;
    }

    lr->peeked = c;
    lr->has_peeked = 1;
    return (int64_t)(unsigned char)c;
}

rt_string rt_linereader_read_all(void *obj)
{
    if (!obj)
    {
        rt_trap("LineReader.ReadAll: null reader");
        return rt_string_from_bytes("", 0);
    }

    rt_linereader_impl *lr = (rt_linereader_impl *)obj;
    if (!lr->fp || lr->closed)
    {
        rt_trap("LineReader.ReadAll: reader is closed");
        return rt_string_from_bytes("", 0);
    }

    // Get current position and file size
    long pos = ftell(lr->fp);
    if (pos < 0)
        pos = 0;

    fseek(lr->fp, 0, SEEK_END);
    long end = ftell(lr->fp);
    fseek(lr->fp, pos, SEEK_SET);

    size_t remaining = (end > pos) ? (size_t)(end - pos) : 0;

    // Account for any peeked character
    size_t extra = lr->has_peeked ? 1 : 0;
    size_t total = remaining + extra;

    if (total == 0)
    {
        lr->eof = 1;
        return rt_string_from_bytes("", 0);
    }

    char *buf = (char *)malloc(total);
    if (!buf)
    {
        rt_trap("LineReader.ReadAll: memory allocation failed");
        return rt_string_from_bytes("", 0);
    }

    size_t offset = 0;

    // Add peeked character first
    if (lr->has_peeked)
    {
        buf[offset++] = (char)lr->peeked;
        lr->has_peeked = 0;
    }

    // Read the rest
    size_t read = fread(buf + offset, 1, remaining, lr->fp);
    total = offset + read;

    lr->eof = 1;

    rt_string result = rt_string_from_bytes(buf, total);
    free(buf);
    return result;
}

int8_t rt_linereader_eof(void *obj)
{
    if (!obj)
        return 1;

    rt_linereader_impl *lr = (rt_linereader_impl *)obj;
    if (!lr->fp || lr->closed)
        return 1;

    return lr->eof;
}
