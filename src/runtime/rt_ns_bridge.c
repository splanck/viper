// File: src/runtime/rt_ns_bridge.c
// Purpose: Minimal helpers to bridge Viper.* namespaced types to runtime objects.

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
