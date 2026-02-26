//===----------------------------------------------------------------------===//
//
// File: src/runtime/arrays/rt_array_obj.h
// Purpose: Dynamic array of opaque object references with element retain/release lifecycle, backing
// collections that store boxed Viper values.
//
// Key invariants:
//   - Elements are reference-managed: stored elements are retained on write and released on
//   overwrite or teardown.
//   - Indices are bounds-checked by callers before passing to array primitives.
//   - Length is always tracked; the array owns references to all live elements.
//
// Ownership/Lifetime:
//   - Heap-allocated; caller manages the container via retain/release or explicit free.
//   - The array owns references to its elements and releases them on resize or free.
//
// Links: src/runtime/arrays/rt_array_obj.c (implementation), src/runtime/core/rt_heap.h
//
//===----------------------------------------------------------------------===//
#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Allocate a new object array with a given logical length.
    /// @details Backing storage for OOP fields and dynamic collections. Allocates
    ///          a header and payload, zeros the payload, and returns a pointer to
    ///          slot 0 (the first element).
    /// @param len Initial logical length (number of slots).
    /// @return Pointer to the first element slot; NULL on allocation failure.
    void **rt_arr_obj_new(size_t len);

    /// @brief Return the logical length of the array.
    /// @details Reads the length stored in the array header. Callers need the
    ///          size for bounds checks and iteration.
    /// @param arr Pointer to the first element slot (as returned by rt_arr_obj_new).
    /// @return Current logical length.
    size_t rt_arr_obj_len(void **arr);

    /// @brief Get the element at an index as a retained reference.
    /// @details Increments the refcount (retain) on the stored object before
    ///          returning, ensuring the caller can safely hold the element beyond
    ///          subsequent array mutations.
    /// @param arr Pointer to the first element slot.
    /// @param idx 0-based index into the array (must be < length).
    /// @return The element pointer (may be NULL); caller must release when done.
    void *rt_arr_obj_get(void **arr, size_t idx);

    /// @brief Store an object at a specific index.
    /// @details Retains the new object and releases the previously stored object
    ///          to keep reference accounting correct.
    /// @param arr Pointer to the first element slot.
    /// @param idx 0-based index into the array (must be < length).
    /// @param obj The object to store (may be NULL).
    void rt_arr_obj_put(void **arr, size_t idx, void *obj);

    /// @brief Resize the array to a new logical length.
    /// @details Supports dynamic growth and shrink for collections and properties.
    ///          Reallocates backing storage when needed and zero-initializes any
    ///          new tail elements. Elements beyond the new length are released.
    /// @param arr Pointer to the first element slot.
    /// @param len New logical length.
    /// @return Pointer to the (possibly reallocated) first element slot.
    void **rt_arr_obj_resize(void **arr, size_t len);

    /// @brief Release all elements and free the array.
    /// @details Iterates over all elements, releases each retained reference,
    ///          and then frees the array's backing storage. Prevents leaks when
    ///          an array is no longer needed.
    /// @param arr Pointer to the first element slot (may be NULL for a no-op).
    void rt_arr_obj_release(void **arr);

#ifdef __cplusplus
}
#endif
