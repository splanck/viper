// File: src/runtime/rt_string_format.c
// Purpose: Provide numeric formatting and parsing utilities for runtime strings.
// Error handling: Functions trap on invalid input and reset errno before calling strtoll.
// Allocation/Ownership: Returned strings are freshly allocated and owned by the caller.

#include "rt_string.h"
#include "rt_internal.h"
#include "rt_numeric.h"
#include "rt_format.h"
#include "rt_int_format.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

int64_t rt_to_int(rt_string s)
{
    if (!s)
        rt_trap("rt_to_int: null");
    const char *p = s->data;
    size_t len = (size_t)rt_len(s);
    size_t i = 0;
    while (i < len && isspace((unsigned char)p[i]))
        ++i;
    size_t j = len;
    while (j > i && isspace((unsigned char)p[j - 1]))
        --j;
    if (i == j)
        rt_trap("INPUT: expected numeric value");
    size_t sz = j - i;
    char *buf = (char *)rt_alloc(sz + 1);
    memcpy(buf, p + i, sz);
    buf[sz] = '\0';
    errno = 0;
    char *endp = NULL;
    long long v = strtoll(buf, &endp, 10);
    if (errno == ERANGE)
    {
        free(buf);
        rt_trap("INPUT: numeric overflow");
    }
    if (!endp || *endp != '\0')
    {
        free(buf);
        rt_trap("INPUT: expected numeric value");
    }
    free(buf);
    return (int64_t)v;
}

double rt_to_double(rt_string s)
{
    if (!s)
        rt_trap("rt_to_double: null");
    bool ok = true;
    double value = rt_val_to_double(s->data, &ok);
    if (!ok)
    {
        if (!isfinite(value))
            rt_trap("INPUT: numeric overflow");
        rt_trap("INPUT: expected numeric value");
    }
    return value;
}

rt_string rt_int_to_str(int64_t v)
{
    char stack_buf[32];
    char *buf = stack_buf;
    size_t cap = sizeof(stack_buf);
    char *heap_buf = NULL;

    size_t written = rt_i64_to_cstr(v, buf, cap);
    if (written == 0 && buf[0] == '\0')
        rt_trap("rt_int_to_str: format");

    while (written + 1 >= cap)
    {
        if (cap > SIZE_MAX / 2)
        {
            if (heap_buf)
                free(heap_buf);
            rt_trap("rt_int_to_str: overflow");
        }
        size_t new_cap = cap * 2;
        char *new_buf = (char *)malloc(new_cap);
        if (!new_buf)
        {
            if (heap_buf)
                free(heap_buf);
            rt_trap("rt_int_to_str: alloc");
        }
        size_t new_written = rt_i64_to_cstr(v, new_buf, new_cap);
        if (new_written == 0 && new_buf[0] == '\0')
        {
            free(new_buf);
            if (heap_buf)
                free(heap_buf);
            rt_trap("rt_int_to_str: format");
        }
        if (heap_buf)
            free(heap_buf);
        heap_buf = new_buf;
        buf = new_buf;
        cap = new_cap;
        written = new_written;
    }

    rt_string s = rt_string_from_bytes(buf, written);
    if (heap_buf)
        free(heap_buf);
    return s;
}

rt_string rt_f64_to_str(double v)
{
    char buf[64];
    rt_format_f64(v, buf, sizeof(buf));
    return rt_string_from_bytes(buf, strlen(buf));
}

rt_string rt_str_d_alloc(double v)
{
    char buf[64];
    rt_format_f64(v, buf, sizeof(buf));
    return rt_string_from_bytes(buf, strlen(buf));
}

rt_string rt_str_f_alloc(float v)
{
    char buf[64];
    rt_format_f64((double)v, buf, sizeof(buf));
    return rt_string_from_bytes(buf, strlen(buf));
}

rt_string rt_str_i32_alloc(int32_t v)
{
    char buf[32];
    rt_str_from_i32(v, buf, sizeof(buf), NULL);
    return rt_string_from_bytes(buf, strlen(buf));
}

rt_string rt_str_i16_alloc(int16_t v)
{
    char buf[16];
    rt_str_from_i16(v, buf, sizeof(buf), NULL);
    return rt_string_from_bytes(buf, strlen(buf));
}

double rt_val(rt_string s)
{
    if (!s)
        rt_trap("rt_val: null");
    bool ok = true;
    double value = rt_val_to_double(s->data, &ok);
    if (!ok)
    {
        if (!isfinite(value))
            rt_trap("rt_val: overflow");
        return value;
    }
    return value;
}

rt_string rt_str(double v)
{
    return rt_f64_to_str(v);
}

