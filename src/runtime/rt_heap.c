//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the reference-counted heap allocator shared by the runtime string
// and array primitives.  The helpers in this file provide the canonical
// metadata layout, pointer conversions, and lifetime management operations so
// every subsystem (VM, native runtime, and host embedding) shares the exact same
// semantics.  Keeping the implementation here prevents subtle mismatches in
// ref-counting or header validation across translation units.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Reference-counted heap utilities for runtime payloads.
/// @details Defines the header layout used for dynamically allocated runtime
///          objects and supplies helpers for allocation, validation, and
///          reference management.  Callers interact solely with payload
///          pointers; this module performs the header bookkeeping behind the
///          scenes.

#include "rt_heap.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/// @brief Recover a heap header from a payload pointer.
/// @details Performs the inverse of @ref rt_heap_data by subtracting the header
///          size from the payload address.  The helper asserts that the header
///          carries the runtime magic tag so corrupted pointers are detected
///          early during development builds.
/// @param payload Pointer returned by @ref rt_heap_alloc or `NULL`.
/// @return Header describing the allocation, or `NULL` for null payloads.
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

/// @brief Sanity-check the invariants stored in a heap header.
/// @details Confirms the presence of the runtime magic tag, ensures the
///          reference count is not the sentinel value reserved for immortal
///          allocations, and validates that the heap kind enumerator is one of
///          the recognised values.  Assertions fire in debug builds to surface
///          memory corruptions or misuse of the allocator.
/// @param hdr Header pointer returned by @ref payload_to_hdr.
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

/// @brief Allocate a reference-counted heap block.
/// @details Reserves memory for the header plus payload, zero-initialises the
///          structure, and sets the initial reference count to one.  The helper
///          automatically grows the capacity to at least @p init_len elements
///          and guards against integer overflow when computing the payload size.
/// @param kind Logical category of the allocation (string, array, object).
/// @param elem_kind Element kind metadata stored for debugging/validation.
/// @param elem_size Size in bytes of a single payload element.
/// @param init_len Initial logical length written to the header.
/// @param init_cap Requested capacity measured in elements.
/// @return Pointer to the payload region, or `NULL` when allocation fails or
///         arguments are invalid.
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

/// @brief Increment the reference count for a payload.
/// @details Converts the payload to its header, validates the metadata, and
///          then increments the reference count.  Debug builds log the new count
///          when @c VIPER_RC_DEBUG is enabled, aiding leak investigations.
/// @param payload Shared payload pointer; `NULL` pointers are ignored.
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

/// @brief Release bookkeeping for a heap header with optional deallocation.
/// @details Shared helper that decrements the reference count and, when
///          @p free_when_zero is true, clears and frees the header once the
///          count reaches zero.  Debug builds log the resulting count when the
///          retain/release tracing macro is enabled.
/// @param hdr Heap header describing the allocation; may be `NULL`.
/// @param payload Payload pointer associated with @p hdr; used for logging.
/// @param free_when_zero Whether to free storage when the reference count hits
///        zero.
/// @return Updated reference count after the decrement, or zero when the block
///         was deallocated.
static size_t rt_heap_release_impl(rt_heap_hdr_t *hdr, void *payload, int free_when_zero)
{
    if (!hdr)
        return 0;
    rt_heap_validate_header(hdr);
    assert(hdr->refcnt > 0);
    size_t next = --hdr->refcnt;
#ifdef VIPER_RC_DEBUG
    fprintf(stderr, "rt_heap_release(%p) => %zu\n", payload, next);
#endif
    if (next == 0 && free_when_zero)
    {
        memset(hdr, 0, sizeof(*hdr));
        free(hdr);
        return 0;
    }
    return next;
}

/// @brief Decrement the reference count and free storage when it reaches zero.
/// @details Drops the reference count after validating the header.  When the
///          count hits zero the header and payload are cleared and freed,
///          returning the allocation to the system.  The return value enables
///          callers to observe whether they released the final reference.
/// @param payload Shared payload pointer; `NULL` pointers are ignored.
/// @return Reference count after the decrement, or zero when the block was
///         deallocated.
size_t rt_heap_release(void *payload)
{
    rt_heap_hdr_t *hdr = payload_to_hdr(payload);
    return rt_heap_release_impl(hdr, payload, /*free_when_zero=*/1);
}

/// @brief Decrement the reference count without freeing the payload.
/// @details Mirrors @ref rt_heap_release but preserves the header and payload
///          even when the updated reference count reaches zero.  Callers can use
///          this variant to run custom destructors while the allocation remains
///          valid before finally handing control back to the heap for
///          deallocation.
/// @param payload Shared payload pointer; `NULL` pointers are ignored.
/// @return Reference count after the decrement.
size_t rt_heap_release_deferred(void *payload)
{
    rt_heap_hdr_t *hdr = payload_to_hdr(payload);
    return rt_heap_release_impl(hdr, payload, /*free_when_zero=*/0);
}

/// @brief Free a heap allocation whose reference count already reached zero.
/// @details Validates the header and, when the reference count is zero, clears
///          and frees the allocation.  Non-zero reference counts leave the
///          payload untouched so callers can safely invoke the helper after
///          custom cleanup logic.
/// @param payload Shared payload pointer; `NULL` pointers are ignored.
void rt_heap_free_zero_ref(void *payload)
{
    rt_heap_hdr_t *hdr = payload_to_hdr(payload);
    if (!hdr)
        return;
    rt_heap_validate_header(hdr);
    if (hdr->refcnt != 0)
        return;
    memset(hdr, 0, sizeof(*hdr));
    free(hdr);
}

/// @brief Obtain a mutable header pointer for a payload.
/// @details Thin wrapper around @ref payload_to_hdr that exists for API
///          symmetry with @ref rt_heap_hdr_c.
/// @param payload Payload pointer produced by @ref rt_heap_alloc.
/// @return Mutable header pointer, or `NULL` when @p payload is `NULL`.
rt_heap_hdr_t *rt_heap_hdr(void *payload)
{
    return payload_to_hdr(payload);
}

/// @brief Convert a header pointer back to its payload address.
/// @details Validates the header before returning the byte immediately after
///          the metadata structure.  The returned pointer aliases the
///          allocation owned by the header and should not be freed directly.
/// @param h Header describing the allocation.
/// @return Pointer to the payload region, or `NULL` when @p h is `NULL`.
void *rt_heap_data(rt_heap_hdr_t *h)
{
    if (!h)
        return NULL;
    rt_heap_validate_header(h);
    return (void *)((uint8_t *)h + sizeof(rt_heap_hdr_t));
}

/// @brief Read the logical length stored alongside a payload.
/// @details Provides a safe accessor that tolerates `NULL` payloads by
///          returning zero, mirroring the behaviour expected by callers in the
///          runtime.
/// @param payload Payload pointer or `NULL`.
/// @return Logical element count tracked in the header.
size_t rt_heap_len(void *payload)
{
    rt_heap_hdr_t *hdr = payload_to_hdr(payload);
    if (!hdr)
        return 0;
    return hdr->len;
}

/// @brief Read the capacity stored alongside a payload.
/// @details Converts the payload to its header and returns the recorded number
///          of elements for which space is reserved.
/// @param payload Payload pointer or `NULL`.
/// @return Capacity value stored in the header.
size_t rt_heap_cap(void *payload)
{
    rt_heap_hdr_t *hdr = payload_to_hdr(payload);
    if (!hdr)
        return 0;
    return hdr->cap;
}

/// @brief Update the logical length associated with a payload.
/// @details Allows resizing operations to publish their new element count
///          without touching the allocation metadata directly.  `NULL` payloads
///          are ignored so callers can operate on optional handles without
///          defensive conditionals.
/// @param payload Payload pointer whose header should be updated.
/// @param new_len New logical length to store.
void rt_heap_set_len(void *payload, size_t new_len)
{
    rt_heap_hdr_t *hdr = payload_to_hdr(payload);
    if (!hdr)
        return;
    hdr->len = new_len;
}
