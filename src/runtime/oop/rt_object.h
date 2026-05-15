//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/oop/rt_object.h
// Purpose: Reference-counted object allocation, retain/release, and System.Object surface providing
// the foundational object model for all Viper heap objects.
//
// Key invariants:
//   - Refcounts never underflow; retain and release calls must be balanced.
//   - Objects start at refcount 1; freed when the count reaches zero.
//   - Finalizer callbacks are invoked from rt_obj_free before releasing heap storage.
//   - rt_obj_resurrect re-arms a finalizer for pool-managed objects (e.g., Vec2/Vec3 pools).
//
// Ownership/Lifetime:
//   - Objects start with refcount 1; caller owns the initial reference.
//   - rt_obj_retain increments; rt_obj_release decrements and frees at zero.
//
// Links: src/runtime/oop/rt_object.c (implementation), src/runtime/core/rt_heap.h
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Finalizer callback invoked from @ref rt_obj_free before releasing heap storage.
typedef void (*rt_obj_finalizer_t)(void *obj);

struct rt_string_impl; // fwd decl is provided in rt_string.h; include where needed

/// @brief Allocate a new runtime-managed object with the given class identifier and size.
/// @param class_id Runtime class identifier tag for the object to create.
/// @param byte_size Total size in bytes to allocate for the object payload.
/// @return Pointer to the allocated object or NULL on allocation failure.
void *rt_obj_new_i64(int64_t class_id, int64_t byte_size);

/// @brief Get the class ID of a runtime-managed object.
/// @param p Pointer to a runtime-managed object (may be NULL).
/// @return The class ID, or 0 if p is NULL or not a valid object.
int64_t rt_obj_class_id(void *p);

/// @brief Check that @p p is a live object instance with the requested class and payload size.
/// @details This is for runtime-owned opaque handle validation before casting a payload to an
///          implementation struct. It rejects NULL, non-heap pointers, non-object heap payloads,
///          wrong class IDs, and allocations smaller than @p min_payload_bytes.
/// @param p Candidate object payload pointer.
/// @param class_id Expected runtime class identifier.
/// @param min_payload_bytes Minimum payload size required by the target implementation struct.
/// @return 1 when the object matches, otherwise 0.
int8_t rt_obj_is_instance(void *p, int64_t class_id, size_t min_payload_bytes);

/// @brief Increment the reference count for a runtime-managed object if the pointer is
/// non-null.
/// @param p Pointer to a runtime-managed object; NULL pointers are ignored.
void rt_obj_retain_maybe(void *p);

/// @brief Increment the reference count for a compiler-proven runtime heap object.
/// @param p Pointer to a runtime-managed object; NULL pointers are ignored.
/// @warning This skips string/raw-pointer discrimination and is only valid for
///          values proven to originate from object allocation helpers.
void rt_obj_retain_known(void *p);

/// @brief Public Viper.Memory retain wrapper.
/// @details Validates that @p p is a live runtime heap/string handle before
///          retaining. Invalid non-null pointers trap instead of relying on
///          debug-only heap assertions.
void rt_memory_retain(void *p);

/// @brief String-typed Viper.Memory retain wrapper.
void rt_memory_retain_str(struct rt_string_impl *s);

/// @brief Decrement the reference count and report whether the object should be destroyed.
/// @param p Pointer to a runtime-managed object.
/// @return 1 when the reference count reaches zero, otherwise 0.
int32_t rt_obj_release_check0(void *p);

/// @brief Release a compiler-proven runtime heap object and report last-user state.
/// @param p Pointer to a runtime-managed object.
/// @return 1 when the reference count reaches zero, otherwise 0.
/// @warning This skips string/raw-pointer discrimination and is only valid for
///          values proven to originate from object allocation helpers.
int32_t rt_obj_release_known_check0(void *p);

/// @brief Public Viper.Memory release wrapper.
/// @details Releases strings, arrays, and objects through their managed
///          lifetime paths. Object finalizers run when this drops the last
///          reference.
/// @return Remaining reference count, or 0 when the value was destroyed.
int64_t rt_memory_release(void *p);

/// @brief String-typed Viper.Memory release wrapper.
int64_t rt_memory_release_str(struct rt_string_impl *s);

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

/// @brief Value equality check between @p self and @p other.
/// @details Compares identity by default; types may override for value semantics.
int64_t rt_obj_equals(void *self, void *other);

/// @brief Compute a hash code for @p self.
/// @details Uses identity or type-specific hashing where available.
int64_t rt_obj_get_hash_code(void *self);

/// @brief Convert @p self to a runtime string.
/// @details Uses type-specific ToString or a default qualified-name fallback.
struct rt_string_impl *rt_obj_to_string(void *self);

/// @brief Identity equality check for two object references.
/// @details Returns 1 when pointers are identical, 0 otherwise.
int64_t rt_obj_reference_equals(void *a, void *b);

// --- Object Introspection ---

/// @brief Get the fully-qualified type name of an object.
struct rt_string_impl *rt_obj_type_name(void *self);

/// @brief Get the numeric type ID of an object.
int64_t rt_obj_type_id(void *self);

/// @brief Check if an object reference is null.
/// @return 1 when @p self is NULL, 0 otherwise.
int64_t rt_obj_is_null(void *self);

// --- Weak Reference Support ---
/// @brief Store a weak reference without incrementing reference count.
/// @param addr Address of the field to store to.
/// @param value Managed target pointer to store (may be NULL).
/// @details Runtime-managed objects, arrays, and strings are wrapped in a
///          zeroing weak handle; non-runtime raw pointers are stored as-is for
///          compatibility.
void rt_weak_store(void **addr, void *value);

/// @brief Load a weak reference and retain the live target.
/// @param addr Address of the field to load from.
/// @return The retained live target pointer, or NULL after the target has been freed.
///         Runtime-managed weak handles return owned references; legacy raw
///         pointer slots are returned borrowed.
void *rt_weak_load(void **addr);

#ifdef __cplusplus
}
#endif
