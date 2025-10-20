// File: src/runtime/rt_string_encode.c
// Purpose: Bridge runtime strings with raw byte encodings and literals.
// Error handling: Functions trap on invalid arguments; errno is untouched.
// Allocation/Ownership: Newly created strings are owned by the caller; literal wrappers do not take ownership of input buffers.

#include "rt_string.h"
#include "rt_internal.h"
#include "rt_int_format.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

rt_string rt_chr(int64_t code)
{
    if (code < 0 || code > 255)
    {
        char buf[64];
        char numbuf[32];
        rt_i64_to_cstr(code, numbuf, sizeof(numbuf));
        snprintf(buf, sizeof(buf), "CHR$: code must be 0-255 (got %s)", numbuf);
        rt_trap(buf);
    }
    char ch = (char)(unsigned char)code;
    return rt_string_from_bytes(&ch, 1);
}

int64_t rt_asc(rt_string s)
{
    if (!s)
        rt_trap("rt_asc: null");
    size_t len = (size_t)rt_len(s);
    if (len == 0 || !s->data)
        return 0;
    return (int64_t)(unsigned char)s->data[0];
}

const char *rt_string_cstr(rt_string s)
{
    if (!s)
    {
        rt_trap("rt_string_cstr: null string");
        return "";
    }
    if (!s->data)
    {
        rt_trap("rt_string_cstr: null data");
        return "";
    }
    return s->data;
}

rt_string rt_const_cstr(const char *c)
{
    if (!c)
        return NULL;
    rt_string s = (rt_string)rt_alloc(sizeof(*s));
    s->data = (char *)c;
    s->heap = NULL;
    s->literal_len = strlen(c);
    s->literal_refs = 1;
    return s;
}

