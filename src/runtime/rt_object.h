// File: src/runtime/rt_object.h
// Purpose: Declares allocation and reference counting helpers for runtime-managed objects.
// Key invariants: Reference counts never underflow; allocation size matches runtime metadata.
// Ownership/Lifetime: Callers receive owned pointers and must balance retain/release calls.
// Links: docs/codemap.md

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Allocate a new runtime-managed object with the given class identifier and size.
    /// @param class_id Runtime class identifier tag for the object to create.
    /// @param byte_size Total size in bytes to allocate for the object payload.
    /// @return Pointer to the allocated object or NULL on allocation failure.
    void *rt_obj_new_i64(int64_t class_id, int64_t byte_size);

    /// @brief Increment the reference count for a runtime-managed object if the pointer is
    /// non-null.
    /// @param p Pointer to a runtime-managed object; NULL pointers are ignored.
    void rt_obj_retain_maybe(void *p);

    /// @brief Decrement the reference count and report whether the object should be destroyed.
    /// @param p Pointer to a runtime-managed object.
    /// @return 1 when the reference count reaches zero, otherwise 0.
    int32_t rt_obj_release_check0(void *p);

    /// @brief Release storage for a runtime-managed object without modifying its reference count.
    /// @param p Pointer to a runtime-managed object to free; must not be NULL.
    void rt_obj_free(void *p);

#ifdef __cplusplus
}
#endif
