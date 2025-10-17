// File: src/runtime/rt_object.c
// Purpose: Implements allocation and reference-count helpers for runtime-managed objects.
// Key invariants: Heap allocations use rt_heap metadata; reference counts never underflow.
// Ownership/Lifetime: Callers own returned pointers and must balance retain/release operations.
// Links: docs/codemap.md

#include "support/feature_flags.hpp"
#include "rt_object.h"

#if VIPER_ENABLE_OOP

#include "rt_heap.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static inline void *alloc_payload(size_t bytes)
{
    /* header+payload allocated by rt_heap_alloc; encode kind=RT_HEAP_OBJECT;
       no element type, len/cap=0 for objects; return payload pointer. */
    return rt_heap_alloc(RT_HEAP_OBJECT, RT_ELEM_NONE, 0, 0, bytes);
}

/// @brief Allocate a zeroed object payload of @p byte_size bytes.
/// @param class_id Runtime class identifier (reserved for future use).
/// @param byte_size Number of bytes to allocate for the object payload.
/// @return Pointer to the allocated payload or NULL on allocation failure.
void *rt_obj_new_i64(int64_t class_id, int64_t byte_size)
{
    (void)class_id; /* stored later if we add a user_tag; not required for v1 */
    void *p = alloc_payload((size_t)byte_size);
    if (p)
        memset(p, 0, (size_t)byte_size);
    return p;
}

/// @brief Retain @p p when non-null.
/// @param p Pointer to a runtime-managed object; NULL pointers are ignored.
void rt_obj_retain_maybe(void *p)
{
    if (p)
        rt_heap_retain(p);
}

/// @brief Release @p p when non-null and report when the refcount reaches zero.
/// @param p Pointer to a runtime-managed object.
/// @return 1 when the reference count reaches zero, otherwise 0.
int32_t rt_obj_release_check0(void *p)
{
    if (!p)
        return 0;
    return (int32_t)(rt_heap_release(p) == 0);
}

/// @brief Free @p p when managed by the heap runtime.
/// @param p Pointer to a runtime-managed object; NULL pointers are ignored.
void rt_obj_free(void *p)
{
    if (!p)
        return;
    /* rt_heap_hdr(p) gives header; freeing is performed by the heap when refcount==0.
       To keep symmetry with strings, expose explicit free to caller for objects. */
    /* Nothing extra to do if heap manages free on zero; otherwise free header here. */
}

#else

#include <stddef.h>
#include <stdint.h>

void *rt_obj_new_i64(int64_t class_id, int64_t byte_size)
{
    (void)class_id;
    (void)byte_size;
    return NULL;
}

void rt_obj_retain_maybe(void *p)
{
    (void)p;
}

int32_t rt_obj_release_check0(void *p)
{
    (void)p;
    return 0;
}

void rt_obj_free(void *p)
{
    (void)p;
}

#endif
