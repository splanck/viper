//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE in the project root for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the BASIC runtime's object allocation façade.  The helpers bridge
// the public C ABI to the shared heap so runtime clients can request
// zero-initialised payloads, participate in reference counting, and release
// objects without needing direct knowledge of the heap metadata layout.  Keeping
// the wrappers here guarantees that both VM and native backends observe
// identical retain/release semantics.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Object allocation and lifetime management utilities for the runtime.
/// @details Defines the ABI-surface functions that allocate tagged payloads on
///          the shared heap, increment or decrement reference counts, and expose
///          BASIC-compatible retain/release helpers.

#include "rt_object.h"
#include "rt_heap.h"
#include "rt_oop.h"
#include "rt_string.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/// @brief Allocate a zeroed payload tagged as a heap object.
/// @details Requests storage from @ref rt_heap_alloc with the
///          @ref RT_HEAP_OBJECT tag so that reference counting and deallocation
///          semantics match other heap-managed entities.  The helper preserves
///          the caller-supplied payload size without introducing additional
///          metadata.
/// @param bytes Number of payload bytes to allocate.
/// @return Pointer to the freshly zeroed payload, or @c NULL when the heap
///         cannot satisfy the request.
static inline void *alloc_payload(size_t bytes)
{
    size_t len = bytes;
    size_t cap = bytes;
    return rt_heap_alloc(RT_HEAP_OBJECT, RT_ELEM_NONE, 1, len, cap);
}

/// @brief Allocate a new object payload with runtime heap bookkeeping.
/// @details Ignores the BASIC class identifier (reserved for future use) and
///          delegates to @ref alloc_payload so the resulting pointer participates
///          in the shared retain/release discipline.
/// @param class_id Runtime class identifier supplied by the caller (unused).
/// @param byte_size Requested payload size in bytes.
/// @return Pointer to zeroed storage when successful; otherwise @c NULL.
void *rt_obj_new_i64(int64_t class_id, int64_t byte_size)
{
    (void)class_id;
    return alloc_payload((size_t)byte_size);
}

/// @brief Increment the reference count for a runtime-managed object.
/// @details Defensively ignores null pointers so callers can unconditionally
///          forward potential object references.  Non-null payloads delegate to
///          @ref rt_heap_retain, keeping the retain logic centralised in the
///          heap subsystem.
/// @param p Object payload pointer returned by @ref rt_obj_new_i64 or another
///          heap-managed API.
void rt_obj_retain_maybe(void *p)
{
    if (p)
        rt_heap_retain(p);
}

/// @brief Decrement the reference count and report last-user semantics.
/// @details Mirrors BASIC string behaviour by returning non-zero when the
///          underlying retain count reaches zero.  Null payloads are ignored to
///          simplify call sites that blindly forward optional objects.
/// @param p Object payload pointer managed by the runtime heap.
/// @return Non-zero when the retain count dropped to zero; otherwise zero.
int32_t rt_obj_release_check0(void *p)
{
    if (!p)
        return 0;
    return (int32_t)(rt_heap_release_deferred(p) == 0);
}

/// @brief Compatibility shim matching the string free entry point.
/// @details Releases storage for objects whose reference count already dropped
///          to zero.  The runtime heap performs the actual deallocation once
///          @ref rt_heap_free_zero_ref observes the zero count, mirroring the
///          BASIC string API while keeping the payload valid for user-defined
///          destructors until this helper runs.
/// @param p Object payload pointer; ignored when @c NULL.
void rt_obj_free(void *p)
{
    if (!p)
        return;
    rt_heap_free_zero_ref(p);
}

// --- System.Object surface implementations ---

int64_t rt_obj_reference_equals(void *a, void *b)
{
    return a == b ? 1 : 0;
}

int64_t rt_obj_equals(void *self, void *other)
{
    // Default: reference equality
    return self == other ? 1 : 0;
}

int64_t rt_obj_get_hash_code(void *self)
{
    // Use pointer value truncated/extended to 64-bit as stable hash
    uintptr_t v = (uintptr_t)self;
    return (int64_t)v;
}

rt_string rt_obj_to_string(void *self)
{
    if (!self)
        return rt_string_from_bytes("<null>", 6);
    rt_object *obj = (rt_object *)self;
    const rt_class_info *ci = rt_get_class_info_from_vptr(obj->vptr);
    if (!ci || !ci->qname)
        return rt_string_from_bytes("Object", 6);
    // Return the class qualified name as the default string representation
    // (no address to keep determinism and test stability)
    const char *name = ci->qname;
    size_t len = 0;
    while (name[len] != '\0')
        ++len;
    return rt_string_from_bytes(name, len);
}
