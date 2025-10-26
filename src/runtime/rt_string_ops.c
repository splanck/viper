// File: src/runtime/rt_string_ops.c
// Purpose: Implement core string operations for the BASIC runtime.
// Error handling: Functions trap via rt_trap on invalid inputs and never rely on errno.
// Allocation/Ownership: Newly created strings transfer ownership to the caller; callers must manage
// reference counts.

#include "rt_int_format.h"
#include "rt_internal.h"
#include "rt_string.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const size_t kImmortalRefcnt = SIZE_MAX - 1;

static rt_heap_hdr_t *rt_string_header(rt_string s)
{
    if (!s || !s->heap)
        return NULL;
    assert(s->heap->kind == RT_HEAP_STRING);
    return s->heap;
}

static size_t rt_string_len_bytes(rt_string s)
{
    if (!s)
        return 0;
    if (!s->heap)
        return s->literal_len;
    (void)rt_string_header(s);
    return rt_heap_len(s->data);
}

static int rt_string_is_immortal_hdr(const rt_heap_hdr_t *hdr)
{
    return hdr && hdr->refcnt >= kImmortalRefcnt;
}

static rt_string rt_string_wrap(char *payload)
{
    if (!payload)
        return NULL;
    rt_heap_hdr_t *hdr = rt_heap_hdr(payload);
    assert(hdr);
    assert(hdr->kind == RT_HEAP_STRING);
    rt_string s = (rt_string)rt_alloc(sizeof(*s));
    s->data = payload;
    s->heap = hdr;
    s->literal_len = 0;
    s->literal_refs = 0;
    return s;
}

static rt_string rt_string_alloc(size_t len, size_t cap)
{
    if (cap < len + 1)
        cap = len + 1;
    char *payload = (char *)rt_heap_alloc(RT_HEAP_STRING, RT_ELEM_NONE, 1, len, cap);
    if (!payload)
    {
        rt_trap("out of memory");
        return NULL;
    }
    payload[len] = '\0';
    return rt_string_wrap(payload);
}

static rt_string rt_empty_string(void)
{
    static rt_string empty = NULL;
    if (!empty)
    {
        char *payload = (char *)rt_heap_alloc(RT_HEAP_STRING, RT_ELEM_NONE, 1, 0, 1);
        if (!payload)
            rt_trap("rt_empty_string: alloc");
        payload[0] = '\0';
        rt_heap_hdr_t *hdr = rt_heap_hdr(payload);
        assert(hdr);
        assert(hdr->kind == RT_HEAP_STRING);
        hdr->refcnt = kImmortalRefcnt;
        empty = (rt_string)rt_alloc(sizeof(*empty));
        empty->data = payload;
        empty->heap = hdr;
        empty->literal_len = 0;
        empty->literal_refs = 0;
    }
    return empty;
}

rt_string rt_string_from_bytes(const char *bytes, size_t len)
{
    rt_string s = rt_string_alloc(len, len + 1);
    if (!s)
        return NULL;
    if (len > 0 && bytes)
        memcpy(s->data, bytes, len);
    s->data[len] = '\0';
    return s;
}

rt_string rt_string_ref(rt_string s)
{
    if (!s)
        return NULL;
    rt_heap_hdr_t *hdr = rt_string_header(s);
    if (!hdr)
    {
        if (s->literal_refs < SIZE_MAX)
            s->literal_refs++;
        return s;
    }
    if (rt_string_is_immortal_hdr(hdr))
        return s;
    rt_heap_retain(s->data);
    return s;
}

void rt_string_unref(rt_string s)
{
    if (!s)
        return;
    rt_heap_hdr_t *hdr = rt_string_header(s);
    if (!hdr)
    {
        if (s->literal_refs > 0 && --s->literal_refs == 0)
            free(s);
        return;
    }
    if (rt_string_is_immortal_hdr(hdr))
        return;
    size_t next = rt_heap_release(s->data);
    if (next == 0)
        free(s);
}

void rt_str_release_maybe(rt_string s)
{
    rt_string_unref(s);
}

void rt_str_retain_maybe(rt_string s)
{
    (void)rt_string_ref(s);
}

rt_string rt_str_empty(void)
{
    return rt_empty_string();
}

int64_t rt_len(rt_string s)
{
    return (int64_t)rt_string_len_bytes(s);
}

rt_string rt_concat(rt_string a, rt_string b)
{
    size_t len_a = rt_string_len_bytes(a);
    size_t len_b = rt_string_len_bytes(b);
    if (len_a > SIZE_MAX - len_b)
    {
        rt_trap("rt_concat: length overflow");
        return NULL;
    }
    size_t total = len_a + len_b;
    if (total == SIZE_MAX)
    {
        rt_trap("rt_concat: length overflow");
        return NULL;
    }

    rt_string out = rt_string_alloc(total, total + 1);
    if (!out)
        return NULL;

    if (a && a->data && len_a > 0)
        memcpy(out->data, a->data, len_a);
    if (b && b->data && len_b > 0)
        memcpy(out->data + len_a, b->data, len_b);

    out->data[total] = '\0';

    if (a)
        rt_string_unref(a);
    if (b)
        rt_string_unref(b);

    return out;
}

rt_string rt_substr(rt_string s, int64_t start, int64_t len)
{
    if (!s)
        rt_trap("rt_substr: null");
    if (start < 0)
        start = 0;
    if (len < 0)
        len = 0;
    size_t slen = rt_string_len_bytes(s);
    if ((uint64_t)start > slen)
        start = (int64_t)slen;
    size_t start_idx = (size_t)start;
    size_t avail = slen - start_idx;
    size_t copy_len = (size_t)len;
    if (copy_len > avail)
        copy_len = avail;
    if (copy_len == 0)
        return rt_empty_string();
    if (start_idx == 0 && copy_len == slen)
        return rt_string_ref(s);
    return rt_string_from_bytes(s->data + start_idx, copy_len);
}

rt_string rt_left(rt_string s, int64_t n)
{
    if (!s)
        rt_trap("LEFT$: null string");
    if (n < 0)
    {
        char buf[64];
        char numbuf[32];
        rt_i64_to_cstr(n, numbuf, sizeof(numbuf));
        snprintf(buf, sizeof(buf), "LEFT$: len must be >= 0 (got %s)", numbuf);
        rt_trap(buf);
    }
    size_t slen = rt_string_len_bytes(s);
    if (n == 0)
        return rt_empty_string();
    if ((size_t)n >= slen)
        return rt_string_ref(s);
    return rt_substr(s, 0, n);
}

rt_string rt_right(rt_string s, int64_t n)
{
    if (!s)
        rt_trap("RIGHT$: null string");
    if (n < 0)
    {
        char buf[64];
        char numbuf[32];
        rt_i64_to_cstr(n, numbuf, sizeof(numbuf));
        snprintf(buf, sizeof(buf), "RIGHT$: len must be >= 0 (got %s)", numbuf);
        rt_trap(buf);
    }
    size_t len = rt_string_len_bytes(s);
    if (n == 0)
        return rt_empty_string();
    if ((size_t)n >= len)
        return rt_string_ref(s);
    size_t start = len - (size_t)n;
    return rt_substr(s, (int64_t)start, n);
}

rt_string rt_mid2(rt_string s, int64_t start)
{
    if (!s)
        rt_trap("MID$: null string");
    if (start < 1)
    {
        char buf[64];
        char numbuf[32];
        rt_i64_to_cstr(start, numbuf, sizeof(numbuf));
        snprintf(buf, sizeof(buf), "MID$: start must be >= 1 (got %s)", numbuf);
        rt_trap(buf);
    }
    size_t len = rt_string_len_bytes(s);
    if (start == 1)
        return rt_string_ref(s);
    uint64_t start_idx_u = (uint64_t)(start - 1);
    if (start_idx_u >= len)
        return rt_empty_string();
    size_t start_idx = (size_t)start_idx_u;
    size_t n = len - start_idx;
    return rt_substr(s, (int64_t)start_idx, (int64_t)n);
}

rt_string rt_mid3(rt_string s, int64_t start, int64_t len)
{
    if (!s)
        rt_trap("MID$: null string");
    if (start < 1)
    {
        char buf[64];
        char numbuf[32];
        rt_i64_to_cstr(start, numbuf, sizeof(numbuf));
        snprintf(buf, sizeof(buf), "MID$: start must be >= 1 (got %s)", numbuf);
        rt_trap(buf);
    }
    if (len < 0)
    {
        char buf[64];
        char numbuf[32];
        rt_i64_to_cstr(len, numbuf, sizeof(numbuf));
        snprintf(buf, sizeof(buf), "MID$: len must be >= 0 (got %s)", numbuf);
        rt_trap(buf);
    }
    size_t slen = rt_string_len_bytes(s);
    if (len == 0)
        return rt_empty_string();
    uint64_t start_idx_u = (uint64_t)(start - 1);
    if (start_idx_u >= slen)
        return rt_empty_string();
    size_t start_idx = (size_t)start_idx_u;
    if (start_idx == 0 && (size_t)len >= slen)
        return rt_string_ref(s);
    size_t avail = slen - start_idx;
    if ((uint64_t)len > avail)
        len = (int64_t)avail;
    return rt_substr(s, (int64_t)start_idx, len);
}

static int64_t rt_find(rt_string hay, int64_t start, rt_string needle)
{
    if (!hay || !needle)
        return 0;
    if (start < 0)
        start = 0;
    size_t hay_len = rt_string_len_bytes(hay);
    size_t needle_len = rt_string_len_bytes(needle);
    if ((uint64_t)start > hay_len)
        start = (int64_t)hay_len;
    size_t start_idx = (size_t)start;
    if (needle_len > hay_len - start_idx)
        return 0;
    for (size_t i = start_idx; i + needle_len <= hay_len; ++i)
        if (memcmp(hay->data + i, needle->data, needle_len) == 0)
            return (int64_t)(i + 1);
    return 0;
}

int64_t rt_instr2(rt_string hay, rt_string needle)
{
    if (!hay || !needle)
        return 0;
    size_t needle_len = rt_string_len_bytes(needle);
    if (needle_len == 0)
        return 1;
    return rt_find(hay, 0, needle);
}

int64_t rt_instr3(int64_t start, rt_string hay, rt_string needle)
{
    if (!hay || !needle)
        return 0;
    size_t len = rt_string_len_bytes(hay);
    int64_t pos = start <= 1 ? 0 : start - 1;
    if ((uint64_t)pos > len)
        pos = (int64_t)len;
    size_t needle_len = rt_string_len_bytes(needle);
    if (needle_len == 0)
        return pos + 1;
    return rt_find(hay, pos, needle);
}

rt_string rt_ltrim(rt_string s)
{
    if (!s)
        rt_trap("rt_ltrim: null");
    size_t slen = rt_string_len_bytes(s);
    size_t i = 0;
    while (i < slen && (s->data[i] == ' ' || s->data[i] == '\t'))
        ++i;
    return rt_substr(s, (int64_t)i, (int64_t)(slen - i));
}

rt_string rt_rtrim(rt_string s)
{
    if (!s)
        rt_trap("rt_rtrim: null");
    size_t end = rt_string_len_bytes(s);
    while (end > 0 && (s->data[end - 1] == ' ' || s->data[end - 1] == '\t'))
        --end;
    return rt_substr(s, 0, (int64_t)end);
}

rt_string rt_trim(rt_string s)
{
    if (!s)
        rt_trap("rt_trim: null");
    size_t slen = rt_string_len_bytes(s);
    size_t start = 0;
    size_t end = slen;
    while (start < end && (s->data[start] == ' ' || s->data[start] == '\t'))
        ++start;
    while (end > start && (s->data[end - 1] == ' ' || s->data[end - 1] == '\t'))
        --end;
    return rt_substr(s, (int64_t)start, (int64_t)(end - start));
}

rt_string rt_ucase(rt_string s)
{
    if (!s)
        rt_trap("rt_ucase: null");
    size_t len = rt_string_len_bytes(s);
    rt_string r = rt_string_alloc(len, len + 1);
    for (size_t i = 0; i < len; ++i)
    {
        unsigned char c = (unsigned char)s->data[i];
        if (c >= 'a' && c <= 'z')
            c = (unsigned char)(c - 'a' + 'A');
        r->data[i] = (char)c;
    }
    r->data[len] = '\0';
    return r;
}

rt_string rt_lcase(rt_string s)
{
    if (!s)
        rt_trap("rt_lcase: null");
    size_t len = rt_string_len_bytes(s);
    rt_string r = rt_string_alloc(len, len + 1);
    for (size_t i = 0; i < len; ++i)
    {
        unsigned char c = (unsigned char)s->data[i];
        if (c >= 'A' && c <= 'Z')
            c = (unsigned char)(c - 'A' + 'a');
        r->data[i] = (char)c;
    }
    r->data[len] = '\0';
    return r;
}

int64_t rt_str_eq(rt_string a, rt_string b)
{
    if (!a || !b)
        return 0;
    if (a == b)
        return 1;
    size_t alen = rt_string_len_bytes(a);
    size_t blen = rt_string_len_bytes(b);
    if (alen != blen)
        return 0;
    return memcmp(a->data, b->data, alen) == 0;
}
