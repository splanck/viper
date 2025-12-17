//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/rt_linewriter.c
// Purpose: Implement buffered text file writing.
//
// LineWriter supports:
// - Open: Create/overwrite file for writing
// - Append: Open for appending
// - Write/WriteLn: Output text with optional newline
// - WriteChar: Output single character
// - Configurable newline string (defaults to platform)
//
//===----------------------------------------------------------------------===//

#include "rt_linewriter.h"

#include "rt_internal.h"
#include "rt_object.h"
#include "rt_string.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/// @brief Platform-specific default newline.
#ifdef _WIN32
#define RT_DEFAULT_NEWLINE "\r\n"
#else
#define RT_DEFAULT_NEWLINE "\n"
#endif

/// @brief LineWriter implementation structure.
typedef struct rt_linewriter_impl
{
    FILE *fp;          ///< File pointer.
    int8_t closed;     ///< Closed flag.
    rt_string newline; ///< Newline string.
} rt_linewriter_impl;

static void rt_linewriter_finalize(void *obj)
{
    if (!obj)
        return;
    rt_linewriter_impl *lw = (rt_linewriter_impl *)obj;
    if (lw->fp && !lw->closed)
    {
        fclose(lw->fp);
        lw->fp = NULL;
        lw->closed = 1;
    }
    if (lw->newline)
    {
        rt_string_unref(lw->newline);
        lw->newline = NULL;
    }
}

/// @brief Internal helper to open file with given mode.
static void *rt_linewriter_open_mode(rt_string path, const char *mode)
{
    if (!path)
    {
        rt_trap("LineWriter: null path");
        return NULL;
    }

    const char *path_str = rt_string_cstr(path);
    if (!path_str)
    {
        rt_trap("LineWriter: invalid path");
        return NULL;
    }

    FILE *fp = fopen(path_str, mode);
    if (!fp)
    {
        rt_trap("LineWriter: failed to open file");
        return NULL;
    }

    rt_linewriter_impl *lw =
        (rt_linewriter_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_linewriter_impl));
    if (!lw)
    {
        fclose(fp);
        rt_trap("LineWriter: memory allocation failed");
        return NULL;
    }

    lw->fp = fp;
    lw->closed = 0;
    lw->newline = rt_string_from_bytes(RT_DEFAULT_NEWLINE, strlen(RT_DEFAULT_NEWLINE));
    rt_obj_set_finalizer(lw, rt_linewriter_finalize);

    return lw;
}

void *rt_linewriter_open(rt_string path)
{
    return rt_linewriter_open_mode(path, "w");
}

void *rt_linewriter_append(rt_string path)
{
    return rt_linewriter_open_mode(path, "a");
}

void rt_linewriter_close(void *obj)
{
    if (!obj)
        return;

    rt_linewriter_impl *lw = (rt_linewriter_impl *)obj;
    if (lw->fp && !lw->closed)
    {
        fclose(lw->fp);
        lw->fp = NULL;
        lw->closed = 1;
    }
}

void rt_linewriter_write(void *obj, rt_string text)
{
    if (!obj)
    {
        rt_trap("LineWriter.Write: null writer");
        return;
    }

    rt_linewriter_impl *lw = (rt_linewriter_impl *)obj;
    if (!lw->fp || lw->closed)
    {
        rt_trap("LineWriter.Write: writer is closed");
        return;
    }

    if (!text)
        return;

    const char *data = rt_string_cstr(text);
    int64_t len = rt_len(text);
    if (data && len > 0)
    {
        fwrite(data, 1, (size_t)len, lw->fp);
    }
}

void rt_linewriter_write_ln(void *obj, rt_string text)
{
    if (!obj)
    {
        rt_trap("LineWriter.WriteLn: null writer");
        return;
    }

    rt_linewriter_impl *lw = (rt_linewriter_impl *)obj;
    if (!lw->fp || lw->closed)
    {
        rt_trap("LineWriter.WriteLn: writer is closed");
        return;
    }

    // Write text if provided
    if (text)
    {
        const char *data = rt_string_cstr(text);
        int64_t len = rt_len(text);
        if (data && len > 0)
        {
            fwrite(data, 1, (size_t)len, lw->fp);
        }
    }

    // Write newline
    if (lw->newline)
    {
        const char *nl = rt_string_cstr(lw->newline);
        int64_t nl_len = rt_len(lw->newline);
        if (nl && nl_len > 0)
        {
            fwrite(nl, 1, (size_t)nl_len, lw->fp);
        }
    }
}

void rt_linewriter_write_char(void *obj, int64_t ch)
{
    if (!obj)
    {
        rt_trap("LineWriter.WriteChar: null writer");
        return;
    }

    rt_linewriter_impl *lw = (rt_linewriter_impl *)obj;
    if (!lw->fp || lw->closed)
    {
        rt_trap("LineWriter.WriteChar: writer is closed");
        return;
    }

    if (ch >= 0 && ch <= 255)
    {
        fputc((int)ch, lw->fp);
    }
}

void rt_linewriter_flush(void *obj)
{
    if (!obj)
        return;

    rt_linewriter_impl *lw = (rt_linewriter_impl *)obj;
    if (lw->fp && !lw->closed)
    {
        fflush(lw->fp);
    }
}

rt_string rt_linewriter_newline(void *obj)
{
    if (!obj)
    {
        return rt_string_from_bytes(RT_DEFAULT_NEWLINE, strlen(RT_DEFAULT_NEWLINE));
    }

    rt_linewriter_impl *lw = (rt_linewriter_impl *)obj;
    if (lw->newline)
    {
        return rt_string_ref(lw->newline);
    }
    return rt_string_from_bytes(RT_DEFAULT_NEWLINE, strlen(RT_DEFAULT_NEWLINE));
}

void rt_linewriter_set_newline(void *obj, rt_string nl)
{
    if (!obj)
    {
        rt_trap("LineWriter.set_NewLine: null writer");
        return;
    }

    rt_linewriter_impl *lw = (rt_linewriter_impl *)obj;

    // Release old newline
    if (lw->newline)
    {
        rt_string_unref(lw->newline);
    }

    // Set new newline (take reference)
    if (nl)
    {
        lw->newline = rt_string_ref(nl);
    }
    else
    {
        lw->newline = rt_string_from_bytes(RT_DEFAULT_NEWLINE, strlen(RT_DEFAULT_NEWLINE));
    }
}
