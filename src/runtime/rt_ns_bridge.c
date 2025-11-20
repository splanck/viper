// File: src/runtime/rt_ns_bridge.c
// Purpose: Minimal helpers to bridge Viper.* namespaced types to runtime objects.

#include "rt_object.h"

void *rt_ns_stringbuilder_new(void)
{
    // Allocate a minimal object payload (at least vptr slot width).
    // Class id 0 is a generic placeholder; no destructor dispatch yet.
    const int64_t kClassId = 0;
    const int64_t kMinBytes = 8; // vptr space
    return rt_obj_new_i64(kClassId, kMinBytes);
}
