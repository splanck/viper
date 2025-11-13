//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// This file declares the runtime's object allocation and reference counting
// infrastructure. These functions provide the foundation for heap-allocated objects
// with automatic memory management through reference counting, used by strings,
// arrays, and user-defined types in BASIC programs.
//
// Viper's runtime uses reference counting for deterministic, low-latency memory
// management without garbage collection pauses. Each allocated object has a header
// containing its reference count and class identifier. Reference count increments
// (retain) and decrements (release) maintain object lifetime, with deallocation
// occurring when the count reaches zero.
//
// Object Lifecycle:
// 1. Allocation: rt_obj_new_i64 allocates an object with initial reference count of 1
// 2. Sharing: rt_obj_retain_maybe increments count when object is copied or stored
// 3. Release: rt_obj_release_check0 decrements count and returns whether object should be freed
// 4. Deallocation: rt_obj_free releases object memory after final release
//
// The class identifier enables runtime type checking and dispatches to appropriate
// destructors for objects with owned resources. This design supports polymorphic
// memory management while maintaining C-compatible ABI for IL-generated code.
//
// Key Invariants:
// - Reference counts never underflow (debug builds include assertions)
// - Allocation size matches class metadata and includes header overhead
// - Release/retain calls are balanced for correct lifetime management
//
//===----------------------------------------------------------------------===//

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
