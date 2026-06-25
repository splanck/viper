//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/arrays/rt_array_obj.c
// Purpose: Implements dynamic arrays of object references (void* elements) for
//          generic collections. Each element is a runtime-managed object handle;
//          the array retains elements on insertion and releases them on overwrite
//          or teardown.
//
// Key invariants:
//   - The array retains a reference to each stored object; callers must not
//     release objects after handing them to the array.
//   - Overwriting an element releases the old object before storing the new one.
//   - Array teardown releases all held object references exactly once.
//   - The array itself is reference-counted through the heap allocator.
//   - Out-of-bounds accesses trigger rt_arr_oob_panic and abort.
//
// Ownership/Lifetime:
//   - The array holds strong references to all stored objects.
//   - The array itself is heap-allocated and reference-counted.
//   - On GC finalization, all element references are released and the heap
//     allocation is freed.
//
// Links: src/runtime/arrays/rt_array_obj.h (public API),
//        src/runtime/arrays/rt_array.h (int32 base module, oob_panic),
//        src/runtime/oop/rt_object.h (object retain/release)
//
//===----------------------------------------------------------------------===//

#include "rt_array_obj.h"

#include "rt_array.h" // for rt_arr_oob_panic
#include "rt_heap.h"
#include "rt_object.h"

#include <assert.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "rt_trap.h"

/// @brief Return the heap header associated with an object array payload.
/// @details The payload pointer is the first element of the array; the header
///          is stored immediately before it in the heap allocation.
/// @param payload Array payload pointer (element 0) or NULL.
/// @return Heap header pointer, or NULL if @p payload is NULL.
static rt_heap_hdr_t *rt_arr_obj_hdr(void **payload) {
    return payload ? rt_heap_hdr((void *)payload) : NULL;
}

/// @brief Validate that a heap header describes an object array.
/// @details Raises the standard object-array corruption trap when @p hdr is
///          missing or names a different heap allocation kind. The return
///          value lets callers stop immediately when a trap hook recovers
///          instead of continuing to read the corrupted header.
/// @param hdr Heap header to validate.
/// @return Non-zero when @p hdr is a valid object-array header; zero after
///         reporting a validation failure.
static int rt_arr_obj_header_valid(rt_heap_hdr_t *hdr) {
    if (!hdr || hdr->kind != RT_HEAP_ARRAY || hdr->elem_kind != RT_ELEM_OBJ) {
        rt_trap("rt_array_obj: corrupted header");
        return 0;
    }
    return 1;
}

/// @brief Return whether an object-array payload has multiple owners.
/// @details Shared object arrays must not be mutated in place because each
///          slot owns one retained element reference on behalf of the shared
///          backing allocation. Mutating a shared backing would alter aliases
///          without transferring element references correctly.
/// @param hdr Valid object-array heap header.
/// @return Non-zero when the backing allocation has more than one owner.
static int rt_arr_obj_is_shared(const rt_heap_hdr_t *hdr) {
    return hdr && __atomic_load_n(&hdr->refcnt, __ATOMIC_ACQUIRE) > 1;
}

/// @brief Assert that a heap header describes an object array.
/// @details Keeps debug assertions local to this module while using the
///          recoverable validator first, so release builds and trap-recovery
///          tests share the same safety behavior.
/// @param hdr Heap header to validate.
static void rt_arr_obj_assert_header(rt_heap_hdr_t *hdr) {
    if (!hdr || hdr->kind != RT_HEAP_ARRAY || hdr->elem_kind != RT_ELEM_OBJ)
        return;
    assert(hdr);
    assert(hdr->kind == RT_HEAP_ARRAY);
    assert(hdr->elem_kind == RT_ELEM_OBJ);
}

/// @brief Release one object-array element and free it if its refcount reaches zero.
/// @details Centralizes the retain/release handoff used by resize and teardown paths so
///          shrink rollback paths can decide exactly when truncated references are released.
/// @param p Runtime object reference stored in an array slot, or NULL for an empty slot.
static void rt_arr_obj_release_element(void *p) {
    if (p && rt_obj_release_check0(p))
        rt_obj_free(p);
}

/// @brief Copy retained object references into a fresh array.
/// @details Each non-NULL element copied from @p src is retained before being
///          stored in @p dst so the destination array owns independent strong
///          references. If a recovering trap hook returns after a retain error,
///          the partially-copied destination remains valid and can be released
///          normally by the caller.
/// @param dst Destination object-array payload.
/// @param src Source object-array payload.
/// @param count Number of elements to copy.
static void rt_arr_obj_copy_retained(void **dst, void **src, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        void *value = src[i];
        if (value)
            rt_obj_retain_maybe(value);
        dst[i] = value;
    }
}

/// @brief Allocate a new object array with logical length @p len.
/// @details The payload is zero-initialized so all elements start as NULL.
///          The returned pointer is the payload (element 0), not the header.
/// @param len Number of elements to allocate.
/// @return Payload pointer for the new array, or NULL on allocation failure.
void **rt_arr_obj_new(size_t len) {
    void **arr = (void **)rt_heap_alloc(RT_HEAP_ARRAY, RT_ELEM_OBJ, sizeof(void *), len, len);
    if (arr && len)
        memset(arr, 0, len * sizeof(void *));
    return arr;
}

/// @brief Return the logical length of the object array.
/// @details A NULL array is treated as length zero for convenience.
/// @param arr Object array payload pointer (may be NULL).
/// @return Number of elements in the array, or 0 if @p arr is NULL.
size_t rt_arr_obj_len(void **arr) {
    if (!arr)
        return 0;
    rt_heap_hdr_t *hdr = rt_arr_obj_hdr(arr);
    if (!rt_arr_obj_header_valid(hdr))
        return 0;
    rt_arr_obj_assert_header(hdr);
    return hdr->len;
}

/// @brief Retrieve an element and retain it for the caller.
/// @details The returned object reference is retained so the caller owns a
///          reference independent of subsequent array mutations. The function
///          asserts that @p arr is non-NULL and @p idx is in range.
/// @param arr Object array payload pointer (must be non-NULL).
/// @param idx Element index to read.
/// @return Retained object pointer (may be NULL if the slot is empty).
void *rt_arr_obj_get(void **arr, size_t idx) {
    if (!arr)
        rt_trap("rt_arr_obj_get: null array");
    if (!arr)
        return NULL;
    rt_heap_hdr_t *hdr = rt_arr_obj_hdr(arr);
    if (!rt_arr_obj_header_valid(hdr))
        return NULL;
    rt_arr_obj_assert_header(hdr);
    if (idx >= hdr->len)
        rt_arr_oob_panic(idx, hdr->len);
    if (idx >= hdr->len)
        return NULL;
    void *p = arr[idx];
    rt_obj_retain_maybe(p);
    return p;
}

/// @brief Store an object reference at the specified index.
/// @details Retains the new object before overwriting the slot so self-assignment
///          is safe. Releases the previous object reference and frees it if its
///          reference count drops to zero. The function asserts that @p arr is
///          non-NULL and @p idx is in range.
/// @param arr Object array payload pointer (must be non-NULL).
/// @param idx Element index to update.
/// @param obj Object reference to store (may be NULL).
void rt_arr_obj_put(void **arr, size_t idx, void *obj) {
    if (!arr)
        rt_trap("rt_arr_obj_put: null array");
    if (!arr)
        return;
    rt_heap_hdr_t *hdr = rt_arr_obj_hdr(arr);
    if (!rt_arr_obj_header_valid(hdr))
        return;
    rt_arr_obj_assert_header(hdr);
    if (rt_arr_obj_is_shared(hdr)) {
        rt_trap("rt_arr_obj_put: cannot mutate shared array");
        return;
    }
    if (idx >= hdr->len)
        rt_arr_oob_panic(idx, hdr->len);
    if (idx >= hdr->len)
        return;

    // Retain new first to handle self-assignment safely
    rt_obj_retain_maybe(obj);

    void *old = arr[idx];
    arr[idx] = obj;
    if (old) {
        if (rt_obj_release_check0(old))
            rt_obj_free(old);
    }
}

/// @brief Resize an object array to the requested length.
/// @details When growing, new elements are zero-initialized. When shrinking,
///          truncated elements are released before the buffer is reallocated.
///          Resizing to zero releases the array and returns NULL.
///          The array may move in memory due to reallocation.
/// @param arr Existing array payload pointer (may be NULL).
/// @param len New logical length.
/// @return Payload pointer for the resized array, or NULL on failure.
void **rt_arr_obj_resize(void **arr, size_t len) {
    if (!arr)
        return rt_arr_obj_new(len);

    rt_heap_hdr_t *hdr = rt_arr_obj_hdr(arr);
    if (!rt_arr_obj_header_valid(hdr))
        return NULL;
    rt_arr_obj_assert_header(hdr);

    size_t old_len = hdr->len;
    if (len == 0) {
        rt_arr_obj_release(arr);
        return NULL;
    }

    if (rt_arr_obj_is_shared(hdr)) {
        void **fresh = rt_arr_obj_new(len);
        if (!fresh)
            return NULL;
        size_t copy_len = old_len < len ? old_len : len;
        rt_arr_obj_copy_retained(fresh, arr, copy_len);
        rt_arr_obj_release(arr);
        return fresh;
    }

    void **truncated = NULL;
    size_t truncated_count = 0;
    if (len < old_len) {
        for (size_t i = len; i < old_len; i++) {
            if (arr[i])
                truncated_count++;
        }
        if (truncated_count > 0) {
            truncated = (void **)malloc(truncated_count * sizeof(void *));
            if (!truncated)
                return NULL;
            size_t at = 0;
            for (size_t i = len; i < old_len; i++) {
                if (arr[i])
                    truncated[at++] = arr[i];
            }
        }
    }

    size_t new_cap = len;
    // compute total bytes with overflow checks similar to rt_arr_i32
    if (new_cap > 0 && new_cap > (SIZE_MAX - sizeof(rt_heap_hdr_t)) / sizeof(void *)) {
        free(truncated);
        return NULL;
    }

    void **payload = (void **)rt_heap_realloc(arr, sizeof(void *), len, new_cap);
    if (!payload) {
        free(truncated);
        return NULL;
    }

    if (truncated) {
        for (size_t i = 0; i < truncated_count; i++) {
            rt_arr_obj_release_element(truncated[i]);
        }
        free(truncated);
    }

    return payload;
}

/// @brief Release all elements and the array itself.
/// @details Each non-NULL element is released and freed when its reference count
///          drops to zero. The array payload is then released via the heap API.
/// @param arr Object array payload pointer (may be NULL).
void rt_arr_obj_release(void **arr) {
    if (!arr)
        return;
    rt_heap_hdr_t *hdr = rt_arr_obj_hdr(arr);
    if (!rt_arr_obj_header_valid(hdr))
        return;
    rt_arr_obj_assert_header(hdr);

    size_t n = hdr->len;
    size_t refs = rt_heap_release_deferred(arr);
    if (refs != 0)
        return;

    for (size_t i = 0; i < n; ++i) {
        void *p = arr[i];
        if (p) {
            rt_arr_obj_release_element(p);
            arr[i] = NULL;
        }
    }
    rt_heap_free_zero_ref(arr);
}
