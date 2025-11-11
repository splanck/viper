// File: src/runtime/rt_type_registry.c
// Purpose: Minimal class metadata registry (placeholder for diagnostics/instrumentation).
// Key invariants: Safe to call multiple times; no-op by default.
// Ownership/Lifetime: Runtime may store references to static rt_class_info instances.
// Links: src/runtime/rt_oop.h

#include "rt_oop.h"
#include <stdlib.h>
#include <string.h>

// Simple runtime registries for classes and interfaces.

typedef struct
{
    int type_id;
    const rt_class_info *ci;
    int base_type_id; // -1 if none
} class_entry;

typedef struct
{
    int iface_id;
    rt_iface_reg reg;
} iface_entry;

typedef struct
{
    int type_id;
    int iface_id;
    void **itable;
} binding_entry;

static class_entry *g_classes = NULL;
static size_t g_classes_len = 0, g_classes_cap = 0;
static iface_entry *g_ifaces = NULL;
static size_t g_ifaces_len = 0, g_ifaces_cap = 0;
static binding_entry *g_bindings = NULL;
static size_t g_bindings_len = 0, g_bindings_cap = 0;

static void ensure_cap(void **buf, size_t *cap, size_t elem_size)
{
    if (*cap == 0)
    {
        *cap = 16;
        *buf = malloc((*cap) * elem_size);
    }
    else if (*cap < (size_t)-1 / 2)
    {
        *cap *= 2;
        *buf = realloc(*buf, (*cap) * elem_size);
    }
}

static const class_entry *find_class_by_type(int type_id)
{
    for (size_t i = 0; i < g_classes_len; ++i)
        if (g_classes[i].type_id == type_id)
            return &g_classes[i];
    return NULL;
}

static const class_entry *find_class_by_vptr(void **vptr)
{
    // Heuristic: vtable pointer equals ci->vtable
    for (size_t i = 0; i < g_classes_len; ++i)
        if (g_classes[i].ci && g_classes[i].ci->vtable == vptr)
            return &g_classes[i];
    return NULL;
}

static const iface_entry *find_iface(int iface_id)
{
    for (size_t i = 0; i < g_ifaces_len; ++i)
        if (g_ifaces[i].iface_id == iface_id)
            return &g_ifaces[i];
    return NULL;
}

static void **find_binding(int type_id, int iface_id)
{
    for (size_t i = 0; i < g_bindings_len; ++i)
        if (g_bindings[i].type_id == type_id && g_bindings[i].iface_id == iface_id)
            return g_bindings[i].itable;
    return NULL;
}

void rt_register_class(const rt_class_info *ci)
{
    if (!ci)
        return;
    if (g_classes_len == g_classes_cap)
        ensure_cap((void **)&g_classes, &g_classes_cap, sizeof(class_entry));
    int base_type_id = -1;
    if (ci->base)
        base_type_id = ci->base->type_id;
    g_classes[g_classes_len++] = (class_entry){ci->type_id, ci, base_type_id};
}

void rt_register_interface(const rt_iface_reg *iface)
{
    if (!iface)
        return;
    if (g_ifaces_len == g_ifaces_cap)
        ensure_cap((void **)&g_ifaces, &g_ifaces_cap, sizeof(iface_entry));
    g_ifaces[g_ifaces_len++] = (iface_entry){iface->iface_id, *iface};
}

void rt_bind_interface(int type_id, int iface_id, void **itable_slots)
{
    if (!itable_slots)
        return;
    (void)find_iface; // suppress unused if not queried
    if (g_bindings_len == g_bindings_cap)
        ensure_cap((void **)&g_bindings, &g_bindings_cap, sizeof(binding_entry));
    g_bindings[g_bindings_len++] = (binding_entry){type_id, iface_id, itable_slots};
}

int rt_typeid_of(void *obj)
{
    if (!obj)
        return -1;
    rt_object *o = (rt_object *)obj;
    if (!o->vptr)
        return -1;
    const class_entry *ce = find_class_by_vptr(o->vptr);
    return ce ? ce->type_id : -1;
}

int rt_type_is_a(int type_id, int test_type_id)
{
    if (type_id == test_type_id)
        return 1;
    const class_entry *ce = find_class_by_type(type_id);
    while (ce && ce->base_type_id >= 0)
    {
        if (ce->base_type_id == test_type_id)
            return 1;
        ce = find_class_by_type(ce->base_type_id);
    }
    return 0;
}

int rt_type_implements(int type_id, int iface_id)
{
    return find_binding(type_id, iface_id) != NULL;
}

void *rt_cast_as_iface(void *obj, int iface_id)
{
    if (!obj)
        return NULL;
    int tid = rt_typeid_of(obj);
    if (tid < 0)
        return NULL;
    return rt_type_implements(tid, iface_id) ? obj : NULL;
}

void *rt_cast_as(void *obj, int target_type_id)
{
    if (!obj)
        return NULL;
    int tid = rt_typeid_of(obj);
    if (tid < 0)
        return NULL;
    return rt_type_is_a(tid, target_type_id) ? obj : NULL;
}

void **rt_itable_lookup(void *obj, int iface_id)
{
    if (!obj)
        return NULL;
    int tid = rt_typeid_of(obj);
    if (tid < 0)
        return NULL;
    return find_binding(tid, iface_id);
}

void rt_register_interface_direct(int iface_id, const char *qname, int slot_count)
{
    (void)qname;
    (void)slot_count; // stored in reg; currently unused by runtime
    rt_iface_reg r = {iface_id, qname, slot_count};
    rt_register_interface(&r);
}

const rt_class_info *rt_get_class_info_from_vptr(void **vptr)
{
    if (!vptr)
        return NULL;
    const class_entry *ce = find_class_by_vptr(vptr);
    return ce ? ce->ci : NULL;
}
