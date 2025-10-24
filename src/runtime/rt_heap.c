// File: src/runtime/rt_heap.c
// Purpose: Provide reference-counted heap allocation helpers shared by runtime strings and arrays.
// Key invariants: Each payload pointer is preceded by a validated header containing metadata and a
// magic tag. Ownership/Lifetime: Callers retain/release to manage shared ownership; block freed
// automatically when refcount hits zero. Links: docs/codemap.md

#include "rt_heap.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static rt_heap_hdr_t *payload_to_hdr(void *payload)
{
    if (!payload)
        return NULL;
    uint8_t *raw = (uint8_t *)payload;
    rt_heap_hdr_t *hdr = (rt_heap_hdr_t *)(raw - sizeof(rt_heap_hdr_t));
    assert(hdr->magic == RT_MAGIC);
    assert(hdr->refcnt != (size_t)-1);
    return hdr;
}

static void rt_heap_validate_header(const rt_heap_hdr_t *hdr)
{
    assert(hdr);
    assert(hdr->magic == RT_MAGIC);
    assert(hdr->refcnt != (size_t)-1);
    switch ((rt_heap_kind_t)hdr->kind)
    {
        case RT_HEAP_STRING:
        case RT_HEAP_ARRAY:
        case RT_HEAP_OBJECT:
            break;
        default:
            assert(!"rt_heap_validate_header: unknown heap kind");
    }
}

void *rt_heap_alloc(rt_heap_kind_t kind,
                    rt_elem_kind_t elem_kind,
                    size_t elem_size,
                    size_t init_len,
                    size_t init_cap)
{
    size_t cap = init_cap;
    if (cap < init_len)
        cap = init_len;
    if (elem_size == 0 && cap > 0)
        return NULL;

    size_t payload_bytes = 0;
    if (cap > 0)
    {
        if (elem_size && cap > (SIZE_MAX - sizeof(rt_heap_hdr_t)) / elem_size)
            return NULL;
        payload_bytes = cap * elem_size;
    }
    size_t total_bytes = sizeof(rt_heap_hdr_t) + payload_bytes;
    rt_heap_hdr_t *hdr = (rt_heap_hdr_t *)malloc(total_bytes);
    if (!hdr)
        return NULL;
    memset(hdr, 0, sizeof(*hdr));
    hdr->magic = RT_MAGIC;
    hdr->kind = (uint16_t)kind;
    hdr->elem_kind = (uint16_t)elem_kind;
    hdr->refcnt = 1;
    hdr->len = init_len;
    hdr->cap = cap;
    if (payload_bytes > 0)
    {
        void *payload = rt_heap_data(hdr);
        memset(payload, 0, payload_bytes);
    }
    return rt_heap_data(hdr);
}

void rt_heap_retain(void *payload)
{
    rt_heap_hdr_t *hdr = payload_to_hdr(payload);
    if (!hdr)
        return;
    rt_heap_validate_header(hdr);
    assert(hdr->refcnt > 0);
    hdr->refcnt++;
#ifdef VIPER_RC_DEBUG
    fprintf(stderr, "rt_heap_retain(%p) => %zu\n", payload, hdr->refcnt);
#endif
}

size_t rt_heap_release(void *payload)
{
    rt_heap_hdr_t *hdr = payload_to_hdr(payload);
    if (!hdr)
        return 0;
    rt_heap_validate_header(hdr);
    assert(hdr->refcnt > 0);
    size_t next = --hdr->refcnt;
#ifdef VIPER_RC_DEBUG
    fprintf(stderr, "rt_heap_release(%p) => %zu\n", payload, next);
#endif
    if (next == 0)
    {
        memset(hdr, 0, sizeof(*hdr));
        free(hdr);
        return 0;
    }
    return next;
}

rt_heap_hdr_t *rt_heap_hdr(void *payload)
{
    return payload_to_hdr(payload);
}

void *rt_heap_data(rt_heap_hdr_t *h)
{
    if (!h)
        return NULL;
    rt_heap_validate_header(h);
    return (void *)((uint8_t *)h + sizeof(rt_heap_hdr_t));
}

size_t rt_heap_len(void *payload)
{
    rt_heap_hdr_t *hdr = payload_to_hdr(payload);
    if (!hdr)
        return 0;
    return hdr->len;
}

size_t rt_heap_cap(void *payload)
{
    rt_heap_hdr_t *hdr = payload_to_hdr(payload);
    if (!hdr)
        return 0;
    return hdr->cap;
}

void rt_heap_set_len(void *payload, size_t new_len)
{
    rt_heap_hdr_t *hdr = payload_to_hdr(payload);
    if (!hdr)
        return;
    hdr->len = new_len;
}
