// File: src/runtime/rt_string_encode.c
// Purpose: Implements runtime helpers for parsing strings into numeric values.
// Error policy: Functions trap on invalid input and clear errno before libc calls.
// Ownership: Callers provide managed strings; functions never take ownership nor allocate results.

#include "rt_string.h"
#include "rt_internal.h"
#include "rt_numeric.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

static size_t rt_string_len_bytes(rt_string s)
{
    if (!s)
        return 0;
    if (!s->heap)
        return s->literal_len;
    return rt_heap_len(s->data);
}

int64_t rt_to_int(rt_string s)
{
    if (!s)
        rt_trap("rt_to_int: null");
    const char *p = s->data;
    size_t len = rt_string_len_bytes(s);
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
