//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/oop/rt_oop_dispatch.c
// Purpose: Implements virtual method dispatch (vtable lookup) for the Viper OOP
//          runtime. Objects carry a vptr to their class vtable; dispatch reads
//          the function pointer at the requested slot index and calls it.
//
// Key invariants:
//   - Slot 0 is Object.ToString, slot 1 is Object.Equals, slot 2 is GetHashCode;
//     class-specific overrides start at slot 3.
//   - NULL object, NULL vptr, or out-of-range slot returns NULL (not a trap).
//   - Vtable contents are set at class registration time and never modified.
//   - Vtable lookups are read-only and fully thread-safe after registration.
//
// Ownership/Lifetime:
//   - Vtables are global static data owned by the type registry.
//   - Callers do not own the returned function pointer; they call it immediately.
//
// Links: src/runtime/oop/rt_oop_dispatch.h (public API, via rt_oop.h),
//        src/runtime/oop/rt_type_registry.h (class and vtable registration),
//        src/runtime/oop/rt_object.h (object allocation and vptr layout)
//
//===----------------------------------------------------------------------===//

#include "rt_oop.h"

/// @brief Look up a virtual function pointer from an object's vtable.
///
/// Retrieves the function pointer at a specific slot in the object's vtable.
/// This is the core operation for virtual method dispatch. The slot index
/// corresponds to the virtual method's position in the class hierarchy.
///
/// **Dispatch sequence:**
/// ```
/// obj.Method()
///   ↓
/// rt_get_vfunc(obj, METHOD_SLOT)
///   ↓
/// obj->vptr[slot] → function pointer
///   ↓
/// Indirect call through function pointer
/// ```
///
/// **Safety behavior:**
/// - NULL object → returns NULL (no crash)
/// - NULL vptr → returns NULL (uninitialized object)
/// - Unknown class → returns NULL (no bounds check possible)
/// - Out of bounds slot → returns NULL (prevents buffer overread)
///
/// @param obj Object whose vtable is queried. May be NULL.
/// @param slot Zero-based vtable slot index to fetch.
///
/// @return Function pointer at the slot, or NULL on any error condition.
///
/// @note Callers must check for NULL before calling the returned pointer.
/// @note O(1) time complexity (array index lookup).
void *rt_get_vfunc(const rt_object *obj, uint32_t slot)
{
    if (!obj || !obj->vptr)
        return (void *)0;

    // Bounds check: retrieve class info and validate slot index
    const rt_class_info *ci = rt_get_class_info_from_vptr(obj->vptr);
    if (!ci)
        return (void *)0; // Unknown class, cannot validate

    if (slot >= ci->vtable_len)
        return (void *)0; // Out of bounds

    return obj->vptr[slot];
}
