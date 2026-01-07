//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/rt_array_obj.h
// Purpose: Runtime support for arrays of object references (opaque pointers).
// Key invariants: Length is tracked; elements are reference-managed via retain
//                 on store and release on overwrite/teardown; indices are
//                 bounds-checked by callers.
// Ownership/Lifetime: The array object owns references to its elements and is
//                     responsible for releasing them when resized or freed.
// Links: docs/runtime-arrays.md
//
//===----------------------------------------------------------------------===//

#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C"
{
#endif

    // Arrays of object references (void*). Payload pointer is the first element.
    // Summary of semantics:
    // - new: allocate array of length len, initialized to NULLs
    // - len: return logical length
    // - get: returns retained reference (caller must later release)
    // - put: retain new value, release old value
    // - resize: adjust length, zero-initialize new tail; may move payload
    // - release: release all elements and the array itself

    /// What: Allocate a new object array with logical length @p len.
    /// Why:  Backing storage for OOP fields and dynamic collections.
    /// How:  Allocates header+payload; zeros payload; returns pointer to slot 0.
    void **rt_arr_obj_new(size_t len);

    /// What: Return the logical length of the array.
    /// Why:  Callers need size for bounds checks and iteration.
    /// How:  Reads the length stored in the array header.
    size_t rt_arr_obj_len(void **arr);

    /// What: Get element at @p idx as a retained reference.
    /// Why:  Ensure the caller can safely hold the element beyond mutations.
    /// How:  Increments refcount (retain) on the stored object before returning.
    void *rt_arr_obj_get(void **arr, size_t idx);

    /// What: Store @p obj at index @p idx.
    /// Why:  Replace element while keeping reference accounting correct.
    /// How:  Retains @p obj, releases the previously stored object.
    void rt_arr_obj_put(void **arr, size_t idx, void *obj);

    /// What: Resize the array to @p len.
    /// Why:  Support dynamic growth/shrink for collections and properties.
    /// How:  Reallocates backing storage when needed; zero-initializes any new tail.
    void **rt_arr_obj_resize(void **arr, size_t len);

    /// What: Release all elements and the array itself.
    /// Why:  Prevent leaks when an array is no longer needed.
    /// How:  Iterates elements releasing retained references, then frees storage.
    void rt_arr_obj_release(void **arr);

#ifdef __cplusplus
}
#endif
