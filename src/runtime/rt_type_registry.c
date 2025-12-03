// File: src/runtime/rt_type_registry.c
// Purpose: Lightweight class metadata registry used by the OOP runtime.
// Key roles: Supports Object.ToString (via qname lookup), runtime type queries
//            (type ids, is-a tests, interface bindings), and registration of
//            both built-in and user-defined classes (per-VM RtContext).
// Ownership/Lifetime: Stores pointers to rt_class_info descriptors; entries are
//            associated with the active RtContext so multiple VMs remain isolated.
// Links: src/runtime/rt_oop.h

#include "rt_context.h"
#include "rt_internal.h"
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

static inline RtTypeRegistryState *rt_tr_state(void)
{
    RtContext *ctx = rt_get_current_context();
    if (!ctx)
        ctx = rt_legacy_context();
    return &ctx->type_registry;
}

static inline class_entry *get_classes(size_t **plen, size_t **pcap)
{
    RtTypeRegistryState *st = rt_tr_state();
    if (!st)
        return NULL;
    if (plen)
        *plen = &st->classes_len;
    if (pcap)
        *pcap = &st->classes_cap;
    return (class_entry *)st->classes;
}

static inline void set_classes(class_entry *p)
{
    RtTypeRegistryState *st = rt_tr_state();
    if (st)
        st->classes = p;
}

static inline iface_entry *get_ifaces(size_t **plen, size_t **pcap)
{
    RtTypeRegistryState *st = rt_tr_state();
    if (!st)
        return NULL;
    if (plen)
        *plen = &st->ifaces_len;
    if (pcap)
        *pcap = &st->ifaces_cap;
    return (iface_entry *)st->ifaces;
}

static inline void set_ifaces(iface_entry *p)
{
    RtTypeRegistryState *st = rt_tr_state();
    if (st)
        st->ifaces = p;
}

static inline binding_entry *get_bindings(size_t **plen, size_t **pcap)
{
    RtTypeRegistryState *st = rt_tr_state();
    if (!st)
        return NULL;
    if (plen)
        *plen = &st->bindings_len;
    if (pcap)
        *pcap = &st->bindings_cap;
    return (binding_entry *)st->bindings;
}

static inline void set_bindings(binding_entry *p)
{
    RtTypeRegistryState *st = rt_tr_state();
    if (st)
        st->bindings = p;
}

static void ensure_cap(void **buf, size_t *cap, size_t elem_size)
{
    if (*cap == 0)
    {
        *cap = 16;
        void *tmp = malloc((*cap) * elem_size);
        if (!tmp)
        {
            rt_trap("rt_type_registry: alloc failed");
            return;
        }
        *buf = tmp;
        return;
    }

    // Exponential growth with overflow guards
    if (*cap > (SIZE_MAX / 2))
    {
        rt_trap("rt_type_registry: capacity overflow");
        return;
    }
    size_t new_cap = (*cap) * 2;
    if (elem_size != 0 && new_cap > (SIZE_MAX / elem_size))
    {
        rt_trap("rt_type_registry: size overflow");
        return;
    }
    void *new_buf = realloc(*buf, new_cap * elem_size);
    if (!new_buf)
    {
        // Preserve existing buffer to avoid pointer loss on realloc failure
        rt_trap("rt_type_registry: realloc failed");
        return;
    }
    *buf = new_buf;
    *cap = new_cap;
}

static const class_entry *find_class_by_type(int type_id)
{
    size_t *plen = NULL;
    class_entry *arr = get_classes(&plen, NULL);
    size_t len = arr && plen ? *plen : 0;
    for (size_t i = 0; i < len; ++i)
        if (arr[i].type_id == type_id)
            return &arr[i];
    return NULL;
}

static const class_entry *find_class_by_vptr(void **vptr)
{
    // Heuristic: vtable pointer equals ci->vtable
    size_t *plen = NULL;
    class_entry *arr = get_classes(&plen, NULL);
    size_t len = arr && plen ? *plen : 0;
    for (size_t i = 0; i < len; ++i)
        if (arr[i].ci && arr[i].ci->vtable == vptr)
            return &arr[i];
    return NULL;
}

static const iface_entry *find_iface(int iface_id)
{
    size_t *plen = NULL;
    iface_entry *arr = get_ifaces(&plen, NULL);
    size_t len = arr && plen ? *plen : 0;
    for (size_t i = 0; i < len; ++i)
        if (arr[i].iface_id == iface_id)
            return &arr[i];
    return NULL;
}

static void **find_binding(int type_id, int iface_id)
{
    size_t *plen = NULL;
    binding_entry *arr = get_bindings(&plen, NULL);
    size_t len = arr && plen ? *plen : 0;
    for (size_t i = 0; i < len; ++i)
        if (arr[i].type_id == type_id && arr[i].iface_id == iface_id)
            return arr[i].itable;
    return NULL;
}

void rt_register_class(const rt_class_info *ci)
{
    if (!ci)
        return;
    size_t *plen = NULL, *pcap = NULL;
    class_entry *arr = get_classes(&plen, &pcap);
    if (!arr && (!plen || !pcap))
        return;
    if (*plen == *pcap)
    {
        ensure_cap((void **)&arr, pcap, sizeof(class_entry));
        set_classes(arr);
    }
    int base_type_id = -1;
    if (ci->base)
        base_type_id = ci->base->type_id;
    arr[(*plen)++] = (class_entry){ci->type_id, ci, base_type_id};
}

void rt_register_interface(const rt_iface_reg *iface)
{
    if (!iface)
        return;
    size_t *plen = NULL, *pcap = NULL;
    iface_entry *arr = get_ifaces(&plen, &pcap);
    if (!arr && (!plen || !pcap))
        return;
    if (*plen == *pcap)
    {
        ensure_cap((void **)&arr, pcap, sizeof(iface_entry));
        set_ifaces(arr);
    }
    arr[(*plen)++] = (iface_entry){iface->iface_id, *iface};
}

void rt_bind_interface(int type_id, int iface_id, void **itable_slots)
{
    if (!itable_slots)
        return;
    (void)find_iface; // suppress unused if not queried
    size_t *plen = NULL, *pcap = NULL;
    binding_entry *arr = get_bindings(&plen, &pcap);
    if (!arr && (!plen || !pcap))
        return;
    if (*plen == *pcap)
    {
        ensure_cap((void **)&arr, pcap, sizeof(binding_entry));
        set_bindings(arr);
    }
    arr[(*plen)++] = (binding_entry){type_id, iface_id, itable_slots};
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
    // Check the exact type first.
    if (find_binding(type_id, iface_id) != NULL)
        return 1;

    // Walk the base class chain to see if any ancestor implements the interface.
    // This ensures that derived classes inherit interface bindings.
    const class_entry *ce = find_class_by_type(type_id);
    while (ce && ce->base_type_id >= 0)
    {
        if (find_binding(ce->base_type_id, iface_id) != NULL)
            return 1;
        ce = find_class_by_type(ce->base_type_id);
    }
    return 0;
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

    // Check the exact type first.
    void **itable = find_binding(tid, iface_id);
    if (itable)
        return itable;

    // Walk the base class chain to find the interface binding.
    const class_entry *ce = find_class_by_type(tid);
    while (ce && ce->base_type_id >= 0)
    {
        itable = find_binding(ce->base_type_id, iface_id);
        if (itable)
            return itable;
        ce = find_class_by_type(ce->base_type_id);
    }
    return NULL;
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

void rt_register_class_with_base(int type_id,
                                  void **vtable,
                                  const char *qname,
                                  int vslot_count,
                                  int base_type_id)
{
    if (!vtable)
        return;
    rt_class_info *ci = (rt_class_info *)malloc(sizeof(rt_class_info));
    if (!ci)
    {
        rt_trap("rt_type_registry: class meta alloc failed");
        return;
    }
    ci->type_id = type_id;
    ci->qname = qname;
    ci->vtable = vtable;
    ci->vtable_len = (uint32_t)(vslot_count < 0 ? 0 : vslot_count);

    // Wire base class pointer by looking up base_type_id in the registry.
    // The base class must be registered before derived classes for this to work.
    ci->base = NULL;
    if (base_type_id >= 0)
    {
        const class_entry *base_entry = find_class_by_type(base_type_id);
        if (base_entry && base_entry->ci)
            ci->base = base_entry->ci;
    }

    rt_register_class(ci);
}

void rt_register_class_direct(int type_id, void **vtable, const char *qname, int vslot_count)
{
    // Delegate to the base-aware version with no base class.
    rt_register_class_with_base(type_id, vtable, qname, vslot_count, -1);
}

void **rt_get_class_vtable(int type_id)
{
    const class_entry *ce = find_class_by_type(type_id);
    if (!ce || !ce->ci)
        return NULL;
    return ce->ci->vtable;
}

// Runtime bridge wrapper: accept runtime string for qname
void rt_register_class_direct_rs(int type_id, void **vtable, rt_string qname, int64_t vslot_count)
{
    const char *name = qname ? rt_string_cstr(qname) : NULL;
    rt_register_class_direct(type_id, vtable, name, (int)vslot_count);
}

// Runtime bridge wrapper: accept runtime string for qname with base class
void rt_register_class_with_base_rs(int type_id,
                                    void **vtable,
                                    rt_string qname,
                                    int64_t vslot_count,
                                    int64_t base_type_id)
{
    const char *name = qname ? rt_string_cstr(qname) : NULL;
    rt_register_class_with_base(type_id, vtable, name, (int)vslot_count, (int)base_type_id);
}
