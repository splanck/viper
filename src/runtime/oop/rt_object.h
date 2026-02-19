//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/rt_object.h
// Purpose: Reference-counted object allocation, retain/release, and System.Object surface.
// Key invariants: Refcounts never underflow; retain/release calls must be balanced.
// Ownership/Lifetime: Objects start at refcount 1; freed when count reaches zero.
// Links: docs/viperlib.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Finalizer callback invoked from @ref rt_obj_free before releasing heap storage.
    typedef void (*rt_obj_finalizer_t)(void *obj);

    /// @brief Allocate a new runtime-managed object with the given class identifier and size.
    /// @param class_id Runtime class identifier tag for the object to create.
    /// @param byte_size Total size in bytes to allocate for the object payload.
    /// @return Pointer to the allocated object or NULL on allocation failure.
    void *rt_obj_new_i64(int64_t class_id, int64_t byte_size);

    /// @brief Get the class ID of a runtime-managed object.
    /// @param p Pointer to a runtime-managed object (may be NULL).
    /// @return The class ID, or 0 if p is NULL or not a valid object.
    int64_t rt_obj_class_id(void *p);

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

    /// @brief Install a finalizer callback for a runtime-managed object.
    /// @details The finalizer runs exactly once from @ref rt_obj_free when the reference count has
    ///          already reached zero (typically after @ref rt_obj_release_check0 returns true).
    /// @param p Object payload pointer returned by @ref rt_obj_new_i64; ignored when NULL.
    /// @param fn Finalizer callback or NULL to clear.
    void rt_obj_set_finalizer(void *p, rt_obj_finalizer_t fn);

    /// @brief Resurrect an object inside its finalizer to recycle it into a pool.
    /// @details Sets the reference count from 0 to 1 atomically.  Must only be
    ///          called from within a finalizer installed via @ref rt_obj_set_finalizer.
    ///          After resurrection @ref rt_heap_free_zero_ref will observe a
    ///          non-zero count and skip deallocation, keeping the allocation alive.
    ///          The caller is responsible for re-installing the finalizer before
    ///          returning the object to callers so the next release cycle can
    ///          recycle the object again.
    /// @param p Object payload pointer whose refcount is currently zero.
    void rt_obj_resurrect(void *p);

    // --- System.Object runtime surface ---
    // Instance methods
    /// What: Value equality check between @p self and @p other.
    /// Why:  Implement System.Object.Equals semantics for runtime objects.
    /// How:  Compares identity by default; types may override for value semantics.
    int64_t rt_obj_equals(void *self, void *other);

    /// What: Compute a hash code for @p self.
    /// Why:  Support hashed collections and equality-based lookups.
    /// How:  Uses identity or type-specific hashing where available.
    int64_t rt_obj_get_hash_code(void *self);

    struct rt_string_impl; // fwd decl is provided in rt_string.h; include where needed

    /// What: Convert @p self to a runtime string.
    /// Why:  Provide a textual representation for diagnostics and printing.
    /// How:  Uses type-specific ToString or a default fallback.
    struct rt_string_impl *rt_obj_to_string(void *self);

    // Static method: ReferenceEquals(a,b)
    /// What: Identity equality check for two object references.
    /// Why:  Expose reference equality independent of Equals overrides.
    /// How:  Returns 1 when pointers are identical, 0 otherwise.
    int64_t rt_obj_reference_equals(void *a, void *b);

    // --- Object Introspection ---
    /// What: Get the qualified type name of an object.
    struct rt_string_impl *rt_obj_type_name(void *self);

    /// What: Get the numeric type ID of an object.
    int64_t rt_obj_type_id(void *self);

    /// What: Check if an object reference is null.
    int64_t rt_obj_is_null(void *self);

    // --- Weak Reference Support ---
    /// @brief Store a weak reference without incrementing reference count.
    /// @param addr Address of the field to store to.
    /// @param value Object pointer to store (may be NULL).
    void rt_weak_store(void **addr, void *value);

    /// @brief Load a weak reference.
    /// @param addr Address of the field to load from.
    /// @return The stored pointer value.
    void *rt_weak_load(void **addr);

#ifdef __cplusplus
}
#endif
