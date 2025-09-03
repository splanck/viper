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

rt_str rt_const_cstr(const char *c)
{
    if (!c)
        return NULL;
    rt_str s = (rt_str)rt_alloc(sizeof(*s));
    s->refcnt = 1;
    s->size = (int64_t)strlen(c);
    s->capacity = s->size;
    s->data = (char *)c;
    return s;
}

void rt_print_str(rt_str s)
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

rt_str rt_input_line(void)
{
    char buf[1024];
    if (!fgets(buf, sizeof(buf), stdin))
        return NULL;
    size_t len = strlen(buf);
    if (len && buf[len - 1] == '\n')
        buf[--len] = '\0';
    rt_str s = (rt_str)rt_alloc(sizeof(*s));
    s->refcnt = 1;
    s->size = (int64_t)len;
    s->capacity = s->size;
    s->data = (char *)rt_alloc(len + 1);
    memcpy(s->data, buf, len + 1);
    return s;
}

int64_t rt_len(rt_str s)
{
    return s ? s->size : 0;
}

rt_str rt_concat(rt_str a, rt_str b)
{
    int64_t asz = a ? a->size : 0;
    int64_t bsz = b ? b->size : 0;
    rt_str s = (rt_str)rt_alloc(sizeof(*s));
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

rt_str rt_substr(rt_str s, int64_t start, int64_t len)
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
    rt_str r = (rt_str)rt_alloc(sizeof(*r));
    r->refcnt = 1;
    r->size = len;
    r->capacity = len;
    r->data = (char *)rt_alloc(len + 1);
    if (len > 0)
        memcpy(r->data, s->data + start, len);
    r->data[len] = '\0';
    return r;
}

int64_t rt_str_eq(rt_str a, rt_str b)
{
    if (!a || !b)
        return 0;
    if (a->size != b->size)
        return 0;
    return memcmp(a->data, b->data, (size_t)a->size) == 0;
}

int64_t rt_to_int(rt_str s)
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

rt_str rt_int_to_str(int64_t v)
{
    char buf[32];
    int n = snprintf(buf, sizeof(buf), "%lld", (long long)v);
    if (n < 0)
        rt_trap("rt_int_to_str: format");
    rt_str s = (rt_str)rt_alloc(sizeof(*s));
    s->refcnt = 1;
    s->size = n;
    s->capacity = n;
    s->data = (char *)rt_alloc(n + 1);
    memcpy(s->data, buf, (size_t)n + 1);
    return s;
}

rt_str rt_f64_to_str(double v)
{
    char buf[32];
    int n = snprintf(buf, sizeof(buf), "%g", v);
    if (n < 0)
        rt_trap("rt_f64_to_str: format");
    rt_str s = (rt_str)rt_alloc(sizeof(*s));
    s->refcnt = 1;
    s->size = n;
    s->capacity = n;
    s->data = (char *)rt_alloc(n + 1);
    memcpy(s->data, buf, (size_t)n + 1);
    return s;
}
