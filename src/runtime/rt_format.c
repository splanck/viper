// File: src/runtime/rt_format.c
// Purpose: Implements deterministic numeric formatting helpers for the BASIC runtime.
// Key invariants: Output uses '.' as the decimal separator and normalizes special values.
// Ownership/Lifetime: Callers supply buffers; this module does not manage memory.
// Links: docs/codemap.md

#include "rt_format.h"
#include "rt_internal.h"

#include <locale.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void rt_format_write(const char *text, char *buffer, size_t capacity)
{
    if (!buffer || capacity == 0)
        rt_trap("rt_format_f64: invalid buffer");
    size_t len = strlen(text);
    if (len + 1 > capacity)
        rt_trap("rt_format_f64: truncated");
    memcpy(buffer, text, len + 1);
}

static void rt_format_normalize_decimal(char *buffer, const char *decimal_point)
{
    if (!buffer || !decimal_point)
        return;
    if (decimal_point[0] == '\0' || (decimal_point[0] == '.' && decimal_point[1] == '\0'))
        return;
    size_t dp_len = strlen(decimal_point);
    if (dp_len == 0)
        return;

    char *pos = strstr(buffer, decimal_point);
    if (!pos)
        return;

    pos[0] = '.';
    if (dp_len > 1)
    {
        char *src = pos + dp_len;
        memmove(pos + 1, src, strlen(src) + 1);
    }
}

void rt_format_f64(double value, char *buffer, size_t capacity)
{
    if (!buffer || capacity == 0)
        rt_trap("rt_format_f64: invalid buffer");

    if (isnan(value))
    {
        rt_format_write("NaN", buffer, capacity);
        return;
    }
    if (isinf(value))
    {
        if (signbit(value))
            rt_format_write("-Inf", buffer, capacity);
        else
            rt_format_write("Inf", buffer, capacity);
        return;
    }

    int written = snprintf(buffer, capacity, "%.15g", value);
    if (written < 0)
        rt_trap("rt_format_f64: format error");
    if ((size_t)written >= capacity)
        rt_trap("rt_format_f64: truncated");

    struct lconv *info = localeconv();
    const char *decimal_point = info ? info->decimal_point : NULL;
    if (decimal_point)
        rt_format_normalize_decimal(buffer, decimal_point);
}

rt_string rt_csv_quote_alloc(rt_string value)
{
    const char *data = "";
    size_t len = 0;
    if (value)
    {
        data = rt_string_cstr(value);
        len = (size_t)rt_len(value);
    }

    size_t extra = 0;
    for (size_t i = 0; i < len; ++i)
    {
        if (data[i] == '"')
            ++extra;
    }

    size_t total = len + extra + 2; // leading and trailing quotes
    char *buffer = (char *)malloc(total + 1);
    if (!buffer)
        rt_trap("rt_csv_quote_alloc: out of memory");

    size_t pos = 0;
    buffer[pos++] = '"';
    for (size_t i = 0; i < len; ++i)
    {
        char ch = data[i];
        buffer[pos++] = ch;
        if (ch == '"')
        {
            buffer[pos++] = '"';
        }
    }
    buffer[pos++] = '"';
    buffer[pos] = '\0';

    rt_string result = rt_string_from_bytes(buffer, total);
    free(buffer);
    return result;
}
