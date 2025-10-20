// File: src/runtime/rt_object.c
// Purpose: Implements runtime-managed object allocation and retain/release helpers.
// Key invariants: Allocations are tagged as RT_HEAP_OBJECT and reference counts never underflow.
// Ownership/Lifetime: Callers receive zero-initialized payloads managed by the shared heap.
// Links: docs/codemap.md

#include "rt_object.h"
#include "rt_heap.h"

#include <stddef.h>

/**
 * @brief Allocate a payload-sized block tagged as an object on the runtime heap.
 * @param bytes Number of payload bytes requested.
 * @return Pointer to the zeroed payload on success, otherwise NULL.
 */
static inline void *alloc_payload(size_t bytes)
{
    size_t len = bytes;
    size_t cap = bytes;
    return rt_heap_alloc(RT_HEAP_OBJECT, RT_ELEM_NONE, 1, len, cap);
}

/**
 * @brief Allocate a new object payload with zeroed memory.
 * @param class_id Runtime class identifier supplied by the caller.
 * @param byte_size Total number of bytes to allocate for the object payload.
 * @return Pointer to the zeroed payload or NULL on allocation failure.
 */
void *rt_obj_new_i64(int64_t class_id, int64_t byte_size)
{
    (void)class_id;
    return alloc_payload((size_t)byte_size);
}

/**
 * @brief Retain the provided runtime-managed object pointer when non-null.
 * @param p Pointer to the object payload; NULL pointers are ignored.
 */
void rt_obj_retain_maybe(void *p)
{
    if (p)
        rt_heap_retain(p);
}

/**
 * @brief Release the provided object and report whether the refcount reached zero.
 * @param p Pointer to the object payload; NULL pointers are ignored.
 * @return Non-zero when the reference count becomes zero after release, else zero.
 */
int32_t rt_obj_release_check0(void *p)
{
    if (!p)
        return 0;
    return (int32_t)(rt_heap_release(p) == 0);
}

/**
 * @brief Explicit free helper mirroring string semantics; actual free handled by heap.
 * @param p Pointer to the object payload; NULL pointers are ignored.
 */
void rt_obj_free(void *p)
{
    if (!p)
        return;
    /* Heap will release storage when the reference count reaches zero. */
}
