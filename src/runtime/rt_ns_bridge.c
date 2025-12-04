//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/rt_ns_bridge.c
// Purpose: Minimal helpers to bridge Viper.* namespaced types to runtime objects.
// Key invariants: Object memory is managed by the runtime heap; returned
//                 handles follow the same ownership model as other rt_object
//                 instances (retain/release enforced by higher layers).
// Ownership/Lifetime: Allocated objects live on the runtime heap and are
//                     reclaimed via the object lifecycle (retain/release + GC).
// Links: docs/codemap.md, src/runtime/rt_oop.h, src/runtime/rt_string_builder.h

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
    }
    return sb;
}
