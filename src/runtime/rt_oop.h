// File: src/runtime/rt_oop.h
// Purpose: Define minimal runtime ABI for objects and class metadata (OOP).
// Key invariants: vptr is stored at offset 0 of every object; vtable entries are stable.
// Ownership/Lifetime: Runtime holds class metadata; objects embed a vptr to class vtable.
// Links: docs/codemap.md

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

    typedef struct rt_class_info {
        int type_id;                       // stable type id assigned by compiler
        const char *qname;                 // fully-qualified class name, e.g. "A.B.C"
        const struct rt_class_info *base;  // base class metadata, or NULL
        void **vtable;                     // array of function pointers (slots)
        uint32_t vtable_len;               // number of slots in vtable
    } rt_class_info;

    typedef struct rt_object {
        void **vptr; // points into class vtable (slot 0)
        // instance fields follow (layout defined by compiler)
    } rt_object;

    // Registration API (no-op placeholder if registry is not enabled)
    void rt_register_class(const rt_class_info *ci);

    // Virtual dispatch helper: fetch vfunc pointer by slot from object
    void *rt_get_vfunc(const rt_object *obj, uint32_t slot);

    // Internal helper: get class info from vptr (used for bounds checking)
    const rt_class_info *rt_get_class_info_from_vptr(void **vptr);

    // Interface registration --------------------------------------------------
    typedef struct {
        int iface_id;          // stable interface id assigned by compiler
        const char *qname;     // fully-qualified interface name
        int slot_count;        // number of methods in interface slot table
    } rt_iface_reg;

    // Register interface metadata
    void rt_register_interface(const rt_iface_reg *iface);

    // Bind class (type_id) to interface with concrete itable slots array
    // itable_slots must point to an array of slot_count function pointers.
    void rt_bind_interface(int type_id, int iface_id, void **itable_slots);

    // RTTI helpers ------------------------------------------------------------
    // Resolve a type id from an object instance
    int rt_typeid_of(void *obj);

    // True if type_id is-a test_type_id (same type or subclass)
    int rt_type_is_a(int type_id, int test_type_id);

    // True if type implements interface iface_id
    int rt_type_implements(int type_id, int iface_id);

    // Cast object to target type; returns obj when is-a holds, else NULL
    void *rt_cast_as(void *obj, int target_type_id);

    // Cast object to interface; returns obj when implements holds, else NULL
    void *rt_cast_as_iface(void *obj, int iface_id);

    // Lookup the itable pointer for (obj, iface_id); returns NULL when unbound.
    void **rt_itable_lookup(void *obj, int iface_id);

    // Convenience: direct registration without aggregate build in IL.
    void rt_register_interface_direct(int iface_id, const char *qname, int slot_count);

#ifdef __cplusplus
}
#endif
