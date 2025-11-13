// File: src/runtime/rt_heap.h
// Purpose: Declare shared heap metadata utilities for runtime reference types.
// Key invariants: Heap allocations include a header preceding payload with magic validation.
// Ownership/Lifetime: Reference-counted; retain/release manage shared ownership.
// Links: docs/codemap.md

#pragma once

#include <stddef.h>
#include <stdint.h>

typedef enum
{
    RT_HEAP_STRING = 1,
    RT_HEAP_ARRAY = 2,
    RT_HEAP_OBJECT = 3,
} rt_heap_kind_t;

typedef enum
{
    RT_ELEM_NONE = 0,
    RT_ELEM_I32 = 1,
    RT_ELEM_I64 = 2,
    RT_ELEM_F64 = 3,
    RT_ELEM_U8 = 4,
    RT_ELEM_STR = 5, // String pointer (rt_string) requiring reference counting
} rt_elem_kind_t;

typedef struct rt_heap_hdr
{
    uint32_t magic;
    uint16_t kind;
    uint16_t elem_kind;
    size_t refcnt;
    size_t len;
    size_t cap;
} rt_heap_hdr_t;

#define RT_MAGIC 0x52504956u /* 'VIPR' little-endian */

#ifdef __cplusplus
extern "C"
{
#endif

    void *rt_heap_alloc(rt_heap_kind_t kind,
                        rt_elem_kind_t elem_kind,
                        size_t elem_size,
                        size_t init_len,
                        size_t init_cap);
    void rt_heap_retain(void *payload);
    size_t rt_heap_release(void *payload);
    size_t rt_heap_release_deferred(void *payload);
    void rt_heap_free_zero_ref(void *payload);
    rt_heap_hdr_t *rt_heap_hdr(void *payload);
    void *rt_heap_data(rt_heap_hdr_t *h);
    size_t rt_heap_len(void *payload);
    size_t rt_heap_cap(void *payload);
    void rt_heap_set_len(void *payload, size_t new_len);

#ifdef __cplusplus
}
#endif
