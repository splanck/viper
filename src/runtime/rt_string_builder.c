//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the runtime string builder helper declared in
// rt_string_builder.h.  The utility centralises the growable buffer logic used
// by printf-style formatting and numeric conversions, ensuring consistent error
// handling and a small-buffer fast path for common cases.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implementation of the runtime string builder helper.
/// @details Provides reserve and append helpers that keep the buffer
///          null-terminated and surface allocation/overflow failures back to
///          callers instead of trapping directly.

#include "rt_string_builder.h"

#include "rt_format.h"
#include "rt_int_format.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool rt_sb_is_inline(const rt_string_builder *sb)
{
    return sb && sb->data == sb->inline_buffer;
}

void rt_sb_init(rt_string_builder *sb)
{
    if (!sb)
        return;
    sb->data = sb->inline_buffer;
    sb->len = 0;
    sb->cap = RT_SB_INLINE_CAPACITY;
    sb->inline_buffer[0] = '\0';
}

void rt_sb_free(rt_string_builder *sb)
{
    if (!sb)
        return;
    if (!rt_sb_is_inline(sb) && sb->data)
        free(sb->data);
    sb->data = sb->inline_buffer;
    sb->len = 0;
    sb->cap = RT_SB_INLINE_CAPACITY;
    sb->inline_buffer[0] = '\0';
}

static rt_sb_status rt_sb_grow(rt_string_builder *sb, size_t new_cap)
{
    if (!sb)
        return RT_SB_ERROR_INVALID;
    if (new_cap <= sb->cap)
        return RT_SB_OK;

    if (new_cap < sb->cap)
        return RT_SB_ERROR_OVERFLOW;

    char *new_data = NULL;
    if (rt_sb_is_inline(sb))
    {
        new_data = (char *)malloc(new_cap);
        if (!new_data)
            return RT_SB_ERROR_ALLOC;
        memcpy(new_data, sb->inline_buffer, sb->len + 1);
    }
    else
    {
        new_data = (char *)realloc(sb->data, new_cap);
        if (!new_data)
            return RT_SB_ERROR_ALLOC;
    }

    sb->data = new_data;
    sb->cap = new_cap;
    return RT_SB_OK;
}

rt_sb_status rt_sb_reserve(rt_string_builder *sb, size_t required)
{
    if (!sb)
        return RT_SB_ERROR_INVALID;
    if (required <= sb->len + 1)
        required = sb->len + 1;
    if (required <= sb->cap)
        return RT_SB_OK;

    size_t new_cap = sb->cap;
    if (new_cap == 0)
        new_cap = RT_SB_INLINE_CAPACITY;

    while (new_cap < required)
    {
        if (new_cap > SIZE_MAX / 2)
        {
            new_cap = required;
            break;
        }
        new_cap *= 2;
    }

    if (new_cap < required)
        new_cap = required;

    return rt_sb_grow(sb, new_cap);
}

static rt_sb_status rt_sb_append_bytes(rt_string_builder *sb, const char *text, size_t len)
{
    if (!sb || (!text && len > 0))
        return RT_SB_ERROR_INVALID;
    if (len == 0)
        return RT_SB_OK;

    if (len > SIZE_MAX - sb->len - 1)
        return RT_SB_ERROR_OVERFLOW;

    rt_sb_status status = rt_sb_reserve(sb, sb->len + len + 1);
    if (status != RT_SB_OK)
        return status;

    memcpy(sb->data + sb->len, text, len);
    sb->len += len;
    sb->data[sb->len] = '\0';
    return RT_SB_OK;
}

rt_sb_status rt_sb_append_cstr(rt_string_builder *sb, const char *text)
{
    if (!text)
        return RT_SB_ERROR_INVALID;
    return rt_sb_append_bytes(sb, text, strlen(text));
}

rt_sb_status rt_sb_append_int(rt_string_builder *sb, int64_t value)
{
    if (!sb)
        return RT_SB_ERROR_INVALID;

    const size_t extra = 32;
    if (extra > SIZE_MAX - sb->len - 1)
        return RT_SB_ERROR_OVERFLOW;

    rt_sb_status status = rt_sb_reserve(sb, sb->len + extra);
    if (status != RT_SB_OK)
        return status;

    size_t avail = sb->cap - sb->len;
    size_t written = rt_i64_to_cstr(value, sb->data + sb->len, avail);
    if (written == 0 && sb->data[sb->len] == '\0')
        return RT_SB_ERROR_FORMAT;
    if (written + 1 > avail)
        return RT_SB_ERROR_OVERFLOW;

    sb->len += written;
    sb->data[sb->len] = '\0';
    return RT_SB_OK;
}

rt_sb_status rt_sb_append_double(rt_string_builder *sb, double value)
{
    if (!sb)
        return RT_SB_ERROR_INVALID;

    const size_t extra = 64;
    if (extra > SIZE_MAX - sb->len - 1)
        return RT_SB_ERROR_OVERFLOW;

    rt_sb_status status = rt_sb_reserve(sb, sb->len + extra);
    if (status != RT_SB_OK)
        return status;

    size_t avail = sb->cap - sb->len;
    rt_format_f64(value, sb->data + sb->len, avail);
    size_t appended = strlen(sb->data + sb->len);
    sb->len += appended;
    if (sb->len >= sb->cap)
        return RT_SB_ERROR_OVERFLOW;
    sb->data[sb->len] = '\0';
    return RT_SB_OK;
}

static rt_sb_status rt_sb_vprintf_internal(rt_string_builder *sb, const char *fmt, va_list args)
{
    if (!sb || !fmt)
        return RT_SB_ERROR_INVALID;

    va_list copy;
    while (1)
    {
        size_t avail = (sb->cap > sb->len) ? (sb->cap - sb->len) : 0;
        if (avail < 2)
        {
            size_t target = sb->cap ? sb->cap * 2 : RT_SB_INLINE_CAPACITY;
            if (sb->cap > SIZE_MAX / 2)
                target = SIZE_MAX;
            rt_sb_status status = rt_sb_reserve(sb, target);
            if (status != RT_SB_OK)
                return status;
            continue;
        }

        va_copy(copy, args);
        int written = vsnprintf(sb->data + sb->len, avail, fmt, copy);
        va_end(copy);

        if (written < 0)
            return RT_SB_ERROR_FORMAT;

        if ((size_t)written + 1 > avail)
        {
            size_t needed = sb->len + (size_t)written + 1;
            if (needed < sb->len)
                return RT_SB_ERROR_OVERFLOW;
            rt_sb_status status = rt_sb_reserve(sb, needed);
            if (status != RT_SB_OK)
                return status;
            continue;
        }

        sb->len += (size_t)written;
        sb->data[sb->len] = '\0';
        return RT_SB_OK;
    }
}

rt_sb_status rt_sb_printf(rt_string_builder *sb, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    rt_sb_status status = rt_sb_vprintf_internal(sb, fmt, args);
    va_end(args);
    return status;
}
