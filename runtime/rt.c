// File: runtime/rt.c
// Purpose: Implements BASIC runtime helpers for strings and I/O.
// Key invariants: Strings use reference counts; print functions do not append newlines.
// Ownership/Lifetime: Caller manages returned strings.
// Links: docs/class-catalog.md

#include "rt.hpp"
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void rt_abort(const char *msg)
{
    if (msg)
        fprintf(stderr, "runtime trap: %s\n", msg);
    else
        fprintf(stderr, "runtime trap\n");
    exit(1);
}

__attribute__((weak)) void vm_trap(const char *msg)
{
    rt_abort(msg);
}

void rt_trap(const char *msg)
{
    vm_trap(msg);
}

void *rt_alloc(int64_t bytes)
{
    void *p = malloc((size_t)bytes);
    if (!p)
        rt_trap("out of memory");
    return p;
}

rt_string rt_const_cstr(const char *c)
{
    if (!c)
        return NULL;
    rt_string s = (rt_string)rt_alloc(sizeof(*s));
    s->refcnt = 1;
    s->size = (int64_t)strlen(c);
    s->capacity = s->size;
    s->data = (char *)c;
    return s;
}

void rt_print_str(rt_string s)
{
    if (s && s->data)
        fwrite(s->data, 1, (size_t)s->size, stdout);
}

void rt_print_i64(int64_t v)
{
    printf("%lld", (long long)v);
}

void rt_print_f64(double v)
{
    printf("%g", v);
}

rt_string rt_input_line(void)
{
    char buf[1024];
    if (!fgets(buf, sizeof(buf), stdin))
        return NULL;
    size_t len = strlen(buf);
    if (len && buf[len - 1] == '\n')
        buf[--len] = '\0';
    rt_string s = (rt_string)rt_alloc(sizeof(*s));
    s->refcnt = 1;
    s->size = (int64_t)len;
    s->capacity = s->size;
    s->data = (char *)rt_alloc(len + 1);
    memcpy(s->data, buf, len + 1);
    return s;
}

int64_t rt_len(rt_string s)
{
    return s ? s->size : 0;
}

rt_string rt_concat(rt_string a, rt_string b)
{
    int64_t asz = a ? a->size : 0;
    int64_t bsz = b ? b->size : 0;
    rt_string s = (rt_string)rt_alloc(sizeof(*s));
    s->refcnt = 1;
    s->size = asz + bsz;
    s->capacity = s->size;
    s->data = (char *)rt_alloc(s->size + 1);
    if (a && a->data)
        memcpy(s->data, a->data, asz);
    if (b && b->data)
        memcpy(s->data + asz, b->data, bsz);
    s->data[s->size] = '\0';
    return s;
}

rt_string rt_substr(rt_string s, int64_t start, int64_t len)
{
    if (!s)
        rt_trap("rt_substr: null");
    if (start < 0)
        start = 0;
    if (len < 0)
        len = 0;
    if (start > s->size)
        start = s->size;
    if (start + len > s->size)
        len = s->size - start;
    rt_string r = (rt_string)rt_alloc(sizeof(*r));
    r->refcnt = 1;
    r->size = len;
    r->capacity = len;
    r->data = (char *)rt_alloc(len + 1);
    if (len > 0)
        memcpy(r->data, s->data + start, len);
    r->data[len] = '\0';
    return r;
}

rt_string rt_left(rt_string s, int64_t n)
{
    if (!s)
        rt_trap("rt_left: null");
    if (n < 0)
        rt_trap("rt_left: negative");
    if (n > s->size)
        n = s->size;
    return rt_substr(s, 0, n);
}

rt_string rt_right(rt_string s, int64_t n)
{
    if (!s)
        rt_trap("rt_right: null");
    if (n < 0)
        rt_trap("rt_right: negative");
    int64_t len = s->size;
    if (n > len)
        n = len;
    int64_t start = len - n;
    return rt_substr(s, start, n);
}

rt_string rt_mid2(rt_string s, int64_t start)
{
    if (!s)
        rt_trap("rt_mid2: null");
    if (start < 0)
        rt_trap("rt_mid2: negative");
    int64_t len = s->size;
    if (start > len)
        start = len;
    int64_t n = len - start;
    return rt_substr(s, start, n);
}

rt_string rt_mid3(rt_string s, int64_t start, int64_t len)
{
    if (!s)
        rt_trap("rt_mid3: null");
    if (start < 0 || len < 0)
        rt_trap("rt_mid3: negative");
    int64_t slen = s->size;
    if (start > slen)
        start = slen;
    if (len > slen - start)
        len = slen - start;
    return rt_substr(s, start, len);
}

static int64_t rt_find(rt_string hay, int64_t start, rt_string needle)
{
    if (!hay || !needle)
        return 0;
    if (start < 0)
        start = 0;
    if (start > hay->size)
        start = hay->size;
    for (int64_t i = start; i + needle->size <= hay->size; ++i)
        if (memcmp(hay->data + i, needle->data, (size_t)needle->size) == 0)
            return i + 1;
    return 0;
}

int64_t rt_instr2(rt_string hay, rt_string needle)
{
    if (!hay || !needle)
        return 0;
    if (needle->size == 0)
        return 1;
    return rt_find(hay, 0, needle);
}

int64_t rt_instr3(int64_t start, rt_string hay, rt_string needle)
{
    if (!hay || !needle)
        return 0;
    int64_t len = hay->size;
    if (start < 1)
        start = 1;
    if (start > len + 1)
        start = len + 1;
    if (needle->size == 0)
        return start;
    return rt_find(hay, start, needle);
}

rt_string rt_ltrim(rt_string s)
{
    if (!s)
        rt_trap("rt_ltrim: null");
    int64_t i = 0;
    while (i < s->size && (s->data[i] == ' ' || s->data[i] == '\t'))
        ++i;
    return rt_substr(s, i, s->size - i);
}

rt_string rt_rtrim(rt_string s)
{
    if (!s)
        rt_trap("rt_rtrim: null");
    int64_t end = s->size;
    while (end > 0 && (s->data[end - 1] == ' ' || s->data[end - 1] == '\t'))
        --end;
    return rt_substr(s, 0, end);
}

rt_string rt_trim(rt_string s)
{
    if (!s)
        rt_trap("rt_trim: null");
    int64_t start = 0;
    int64_t end = s->size;
    while (start < end && (s->data[start] == ' ' || s->data[start] == '\t'))
        ++start;
    while (end > start && (s->data[end - 1] == ' ' || s->data[end - 1] == '\t'))
        --end;
    return rt_substr(s, start, end - start);
}

rt_string rt_ucase(rt_string s)
{
    if (!s)
        rt_trap("rt_ucase: null");
    rt_string r = (rt_string)rt_alloc(sizeof(*r));
    r->refcnt = 1;
    r->size = s->size;
    r->capacity = r->size;
    r->data = (char *)rt_alloc(r->size + 1);
    for (int64_t i = 0; i < r->size; ++i)
    {
        unsigned char c = (unsigned char)s->data[i];
        if (c >= 'a' && c <= 'z')
            c = (unsigned char)(c - 'a' + 'A');
        r->data[i] = (char)c;
    }
    r->data[r->size] = '\0';
    return r;
}

rt_string rt_lcase(rt_string s)
{
    if (!s)
        rt_trap("rt_lcase: null");
    rt_string r = (rt_string)rt_alloc(sizeof(*r));
    r->refcnt = 1;
    r->size = s->size;
    r->capacity = r->size;
    r->data = (char *)rt_alloc(r->size + 1);
    for (int64_t i = 0; i < r->size; ++i)
    {
        unsigned char c = (unsigned char)s->data[i];
        if (c >= 'A' && c <= 'Z')
            c = (unsigned char)(c - 'A' + 'a');
        r->data[i] = (char)c;
    }
    r->data[r->size] = '\0';
    return r;
}

rt_string rt_chr(int64_t code)
{
    if (code < 0 || code > 255)
        rt_trap("rt_chr: range");
    rt_string s = (rt_string)rt_alloc(sizeof(*s));
    s->refcnt = 1;
    s->size = 1;
    s->capacity = 1;
    s->data = (char *)rt_alloc(2);
    s->data[0] = (char)(unsigned char)code;
    s->data[1] = '\0';
    return s;
}

int64_t rt_asc(rt_string s)
{
    if (!s)
        rt_trap("rt_asc: null");
    if (s->size <= 0 || !s->data)
        return 0;
    return (int64_t)(unsigned char)s->data[0];
}

int64_t rt_str_eq(rt_string a, rt_string b)
{
    if (!a || !b)
        return 0;
    if (a->size != b->size)
        return 0;
    return memcmp(a->data, b->data, (size_t)a->size) == 0;
}

int64_t rt_to_int(rt_string s)
{
    if (!s)
        rt_trap("rt_to_int: null");
    const char *p = s->data;
    size_t len = (size_t)s->size;
    size_t i = 0;
    while (i < len && isspace((unsigned char)p[i]))
        ++i;
    size_t j = len;
    while (j > i && isspace((unsigned char)p[j - 1]))
        --j;
    if (i == j)
        rt_trap("rt_to_int: empty");
    size_t sz = j - i;
    char *buf = (char *)rt_alloc(sz + 1);
    memcpy(buf, p + i, sz);
    buf[sz] = '\0';
    errno = 0;
    char *endp = NULL;
    long long v = strtoll(buf, &endp, 10);
    if (errno == ERANGE || !endp || *endp != '\0')
    {
        free(buf);
        rt_trap("rt_to_int: invalid");
    }
    free(buf);
    return (int64_t)v;
}

rt_string rt_int_to_str(int64_t v)
{
    char buf[32];
    int n = snprintf(buf, sizeof(buf), "%lld", (long long)v);
    if (n < 0)
        rt_trap("rt_int_to_str: format");
    rt_string s = (rt_string)rt_alloc(sizeof(*s));
    s->refcnt = 1;
    s->size = n;
    s->capacity = n;
    s->data = (char *)rt_alloc(n + 1);
    memcpy(s->data, buf, (size_t)n + 1);
    return s;
}

rt_string rt_f64_to_str(double v)
{
    char buf[32];
    int n = snprintf(buf, sizeof(buf), "%g", v);
    if (n < 0)
        rt_trap("rt_f64_to_str: format");
    rt_string s = (rt_string)rt_alloc(sizeof(*s));
    s->refcnt = 1;
    s->size = n;
    s->capacity = n;
    s->data = (char *)rt_alloc(n + 1);
    memcpy(s->data, buf, (size_t)n + 1);
    return s;
}

double rt_val(rt_string s)
{
    if (!s)
        rt_trap("rt_val: null");
    char *endp = NULL;
    double v = strtod(s->data, &endp);
    if (endp == s->data)
        return 0.0;
    return v;
}

rt_string rt_str(double v)
{
    return rt_f64_to_str(v);
}
