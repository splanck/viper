// File: src/runtime/rt_oop_dispatch.c
// Purpose: Provide minimal virtual dispatch helpers for runtime-managed objects.
// Key invariants: vptr points to an array of function pointers; slot indices are bounds-checked at runtime.
// Ownership/Lifetime: Does not own object memory; returns raw function pointers for indirect calls.
// Links: src/runtime/rt_oop.h

#include "rt_oop.h"

void *rt_get_vfunc(const rt_object *obj, uint32_t slot)
{
    if (!obj || !obj->vptr)
        return (void *)0;

    // Bounds check: retrieve class info and validate slot index
    const rt_class_info *ci = rt_get_class_info_from_vptr(obj->vptr);
    if (!ci)
        return (void *)0;  // Unknown class, cannot validate

    if (slot >= ci->vtable_len)
        return (void *)0;  // Out of bounds

    return obj->vptr[slot];
}

