// File: src/runtime/rt_string_format.c
// Purpose: Provides runtime helpers for formatting numeric values into strings.
// Error policy: Functions trap on allocation failures or formatting errors without consulting errno.
// Ownership: Newly formatted strings are heap allocated and ownership transfers to the caller.

#include "rt_string.h"
#include "rt_internal.h"
#include "rt_format.h"
#include "rt_int_format.h"

#include <stdlib.h>
#include <string.h>

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

rt_string rt_str(double v)
{
    return rt_f64_to_str(v);
}
