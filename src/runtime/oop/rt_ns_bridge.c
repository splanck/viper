//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/oop/rt_ns_bridge.c
// Purpose: Provides minimal bridge helpers that wrap Viper.* namespaced types
//          (such as StringBuilder) as heap-allocated runtime objects compatible
//          with the OOP object model. Enables Viper code to use these types
//          through the standard retain/release interface.
//
// Key invariants:
//   - All wrapped objects are allocated through the runtime heap (rt_heap_alloc).
//   - Returned handles follow the same retain/release ownership model as
//     other rt_object instances.
//   - Each wrapped type embeds its data at a fixed offset after the vptr field.
//   - The vptr field at offset 0 is set to a static vtable for the type.
//
// Ownership/Lifetime:
//   - Callers receive a fresh reference (refcount=1) from bridge constructors.
//   - The runtime GC handles deallocation when the refcount reaches zero.
//
// Links: src/runtime/oop/rt_ns_bridge.h (public API),
//        src/runtime/rt_object.h (rt_object allocation and lifecycle),
//        src/runtime/rt_string_builder.h (StringBuilder embedded in bridge object)
//
//===----------------------------------------------------------------------===//

#include "rt_object.h"
#include "rt_string_builder.h"
#include <stddef.h>

// The StringBuilder object layout:
// [0..7]   : vptr (for vtable)
// [8..]    : embedded rt_string_builder struct
typedef struct
{
    void *vptr;                // vtable pointer (8 bytes)
    rt_string_builder builder; // embedded builder state
} StringBuilder;

static void rt_ns_stringbuilder_finalize(void *obj)
{
    if (!obj)
        return;
    StringBuilder *sb = (StringBuilder *)obj;
    rt_sb_free(&sb->builder);
}

/// @brief Allocate a new instance of the namespaced StringBuilder class.
///
/// @details This bridges the high-level Viper.Strings.Builder class to a
///          runtime-managed object by allocating a header (vptr) followed by an
///          embedded @c rt_string_builder payload. The embedded builder is
///          initialized in-place so callers receive a ready-to-use object.
///
/// @return Opaque pointer to the created object or NULL on allocation failure.
void *rt_ns_stringbuilder_new(void)
{
    // Allocate enough space for vptr + embedded builder
    const int64_t kClassId = 0;
    const int64_t kObjectSize = sizeof(StringBuilder);

    StringBuilder *sb = (StringBuilder *)rt_obj_new_i64(kClassId, kObjectSize);
    if (sb)
    {
        // Initialize the embedded builder
        rt_sb_init(&sb->builder);
        rt_obj_set_finalizer(sb, rt_ns_stringbuilder_finalize);
    }
    return sb;
}
