//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file rt_type_registry.c
/// @brief Runtime type system for Viper's object-oriented features.
///
/// This file implements the type registry that enables Viper's OOP features
/// at runtime. The registry maintains metadata about classes and interfaces,
/// supporting operations like type casting, inheritance checks, interface
/// dispatch, and Object.ToString().
///
/// **What is the Type Registry?**
/// The type registry is a per-VM database of class and interface metadata
/// that enables:
/// - Runtime type identification (typeof, is-a checks)
/// - Virtual method dispatch via vtables
/// - Interface method dispatch via itables
/// - Object.ToString() default implementation
/// - Safe type casting (TryCast, DirectCast)
///
/// **Type System Architecture:**
/// ```
/// ┌─────────────────────────────────────────────────────────────────────────┐
/// │                         Type Registry                                   │
/// │                                                                         │
/// │  ┌────────────────┐  ┌────────────────┐  ┌────────────────────────┐     │
/// │  │ Classes Array  │  │ Interfaces     │  │ Bindings               │     │
/// │  │                │  │ Array          │  │ (Class→Interface)      │     │
/// │  │ ┌────────────┐ │  │ ┌────────────┐ │  │ ┌──────────────────┐   │     │
/// │  │ │ type_id    │ │  │ │ iface_id   │ │  │ │ type_id          │   │     │
/// │  │ │ ci (meta)  │ │  │ │ name       │ │  │ │ iface_id         │   │     │
/// │  │ │ base_type  │ │  │ │ slot_count │ │  │ │ itable (methods) │   │     │
/// │  │ └────────────┘ │  │ └────────────┘ │  │ └──────────────────┘   │     │
/// │  └────────────────┘  └────────────────┘  └────────────────────────┘     │
/// └─────────────────────────────────────────────────────────────────────────┘
/// ```
///
/// **Class Info Structure (rt_class_info):**
/// ```
/// ┌─────────────────────────────────────────────────────┐
/// │ rt_class_info                                       │
/// │   type_id: 42        ← Unique identifier            │
/// │   qname: "MyClass"   ← Qualified name for ToString  │
/// │   vtable: [...]      ← Virtual method pointers      │
/// │   vtable_len: 5      ← Number of virtual methods    │
/// │   base: *ParentInfo  ← Pointer to base class info   │
/// └─────────────────────────────────────────────────────┘
/// ```
///
/// **Registration Order:**
/// Classes must be registered before their derived classes so that base
/// class pointers can be resolved:
/// ```
/// 1. rt_register_class_with_base(Animal, vtable, "Animal", 2, -1)
/// 2. rt_register_class_with_base(Dog, vtable, "Dog", 3, Animal_id)
///    ↑ Dog's base is resolved by looking up Animal in registry
/// ```
///
/// **Type Operations:**
/// | Operation             | Description                                    |
/// |-----------------------|------------------------------------------------|
/// | rt_register_class     | Add class metadata to registry                 |
/// | rt_register_interface | Add interface metadata to registry             |
/// | rt_bind_interface     | Associate itable with class for interface      |
/// | rt_typeid_of          | Get type ID from object instance               |
/// | rt_type_is_a          | Check inheritance relationship                 |
/// | rt_type_implements    | Check if class implements interface            |
/// | rt_cast_as            | Safe downcast to class type                    |
/// | rt_cast_as_iface      | Safe cast to interface type                    |
/// | rt_itable_lookup      | Get interface method table for object          |
///
/// **Inheritance Walk:**
/// Type checks walk the inheritance chain by following base_type_id links:
/// ```
/// rt_type_is_a(Dog, Animal):
///   Dog.base_type_id → Animal.type_id → match! return true
///
/// rt_type_is_a(Dog, Vehicle):
///   Dog.base_type_id → Animal.type_id → no match
///   Animal.base_type_id → -1 → end of chain, return false
/// ```
///
/// **Interface Dispatch:**
/// When calling an interface method, the runtime:
/// 1. Gets the object's type_id from its vptr
/// 2. Looks up the binding (type_id, iface_id) → itable
/// 3. Calls the method at the appropriate itable slot
///
/// **Per-VM Isolation:**
/// Each VM context has its own type registry, enabling multiple independent
/// Viper programs to run in the same process without type ID conflicts.
///
/// **Thread Safety:**
/// - Registration functions should be called during VM initialization
/// - Query functions (typeid_of, is_a, etc.) are thread-safe for reads
/// - Concurrent registration is not supported
///
/// @see rt_oop.h For OOP type definitions
/// @see rt_oop_dispatch.c For virtual method dispatch
/// @see rt_context.c For per-VM isolation
///
//===----------------------------------------------------------------------===//

#include "rt_context.h"
#include "rt_internal.h"
#include "rt_oop.h"
#include <stdlib.h>
#include <string.h>

// ============================================================================
// Internal Data Structures
// ============================================================================

/// @brief Entry in the class registry tracking one registered class.
///
/// Each entry associates a type ID with its class metadata (rt_class_info).
/// The base_type_id enables inheritance chain traversal for is-a checks.
///
/// @note owned_ci indicates whether ci was allocated by the registry and
///       must be freed during cleanup (vs. static metadata from codegen).
typedef struct
{
    int type_id;              ///< Unique class identifier.
    const rt_class_info *ci;  ///< Class metadata (vtable, name, base).
    int base_type_id;         ///< Base class ID, or -1 for root classes.
    int owned_ci;             ///< Non-zero if ci should be freed on cleanup.
} class_entry;

/// @brief Entry in the interface registry tracking one registered interface.
///
/// Stores the interface's unique ID and its registration metadata including
/// the qualified name and slot count.
typedef struct
{
    int iface_id;    ///< Unique interface identifier.
    rt_iface_reg reg; ///< Interface registration info (name, slot count).
} iface_entry;

/// @brief Entry in the bindings table associating a class with an interface.
///
/// When a class implements an interface, a binding is created that links
/// the class type_id and iface_id to the interface method table (itable).
/// The itable is an array of function pointers for the interface's methods.
///
/// **Example:**
/// ```
/// Class Dog implements IComparable
///   → binding_entry { type_id=Dog, iface_id=IComparable, itable=... }
/// ```
typedef struct
{
    int type_id;     ///< Class implementing the interface.
    int iface_id;    ///< Interface being implemented.
    void **itable;   ///< Array of function pointers for interface methods.
} binding_entry;

// ============================================================================
// State Access Helpers
// ============================================================================

/// @brief Get the type registry state for the current context.
///
/// Returns the type registry from either the thread's bound VM context or
/// the legacy fallback context. This enables per-VM type isolation.
///
/// @return Pointer to the current context's type registry state.
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

static void rt_register_class_entry(const rt_class_info *ci, int owned_ci)
{
    if (!ci)
        return;
    size_t *plen = NULL, *pcap = NULL;
    class_entry *arr = get_classes(&plen, &pcap);
    if (!plen || !pcap)
        return;
    if (*plen == *pcap)
    {
        ensure_cap((void **)&arr, pcap, sizeof(class_entry));
        set_classes(arr);
    }
    int base_type_id = -1;
    if (ci->base)
        base_type_id = ci->base->type_id;
    arr[(*plen)++] = (class_entry){ci->type_id, ci, base_type_id, owned_ci};
}

/// @brief Register a class metadata descriptor with the active VM registry.
///
/// @details Appends @p ci to the per-VM class table, growing the table as
///          needed. The descriptor's @c base pointer is not modified here; use
///          @ref rt_register_class_with_base to wire base classes by id.
/// @param ci Pointer to a constant @ref rt_class_info describing the class.
void rt_register_class(const rt_class_info *ci)
{
    rt_register_class_entry(ci, 0);
}

/// @brief Register an interface descriptor with the active VM registry.
/// @param iface Interface registration record (id, name, slot count).
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

/// @brief Bind an interface method table to a class type id.
///
/// @details Records the association so virtual dispatch via iface calls can
///          locate the correct itable for instances of @p type_id.
/// @param type_id     Concrete class type id.
/// @param iface_id    Interface id to bind.
/// @param itable_slots Pointer to array of function pointers (length = slot_count).
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

/// @brief Return the runtime type id for an object instance.
/// @param obj Object pointer (may be NULL).
/// @return Type id when known, -1 otherwise.
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

/// @brief Check class inheritance (is-a) by type id.
/// @return 1 when @p type_id equals or derives from @p test_type_id; 0 otherwise.
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

/// @brief Check whether a class implements an interface by id.
/// @return 1 if implemented by the class or any ancestor; 0 otherwise.
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

/// @brief Safe-cast an object to an interface by id.
/// @return @p obj when compatible; NULL otherwise.
void *rt_cast_as_iface(void *obj, int iface_id)
{
    if (!obj)
        return NULL;
    int tid = rt_typeid_of(obj);
    if (tid < 0)
        return NULL;
    return rt_type_implements(tid, iface_id) ? obj : NULL;
}

/// @brief Safe-cast an object to a target class by id.
/// @return @p obj when compatible; NULL otherwise.
void *rt_cast_as(void *obj, int target_type_id)
{
    if (!obj)
        return NULL;
    int tid = rt_typeid_of(obj);
    if (tid < 0)
        return NULL;
    return rt_type_is_a(tid, target_type_id) ? obj : NULL;
}

/// @brief Lookup the active interface method table for an object instance.
/// @param obj      Object to query.
/// @param iface_id Interface id to search.
/// @return Pointer to the itable when found; NULL otherwise.
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

/// @brief Convenience wrapper to register an interface using C strings.
void rt_register_interface_direct(int iface_id, const char *qname, int slot_count)
{
    (void)qname;
    (void)slot_count; // stored in reg; currently unused by runtime
    rt_iface_reg r = {iface_id, qname, slot_count};
    rt_register_interface(&r);
}

/// @brief Runtime-string bridge for @ref rt_register_interface_direct.
void rt_register_interface_direct_rs(int64_t iface_id, rt_string qname, int64_t slot_count)
{
    const char *name = qname ? rt_string_cstr(qname) : NULL;
    rt_register_interface_direct((int)iface_id, name, (int)slot_count);
}

/// @brief Resolve a class descriptor from a vtable pointer.
/// @return Class info when registered; NULL otherwise.
const rt_class_info *rt_get_class_info_from_vptr(void **vptr)
{
    if (!vptr)
        return NULL;
    const class_entry *ce = find_class_by_vptr(vptr);
    return ce ? ce->ci : NULL;
}

/// @brief Register a class descriptor built from parts, with base by id.
/// @param type_id      Assigned class id.
/// @param vtable       Vtable pointer array.
/// @param qname        Qualified class name (borrowed).
/// @param vslot_count  Number of entries in the vtable.
/// @param base_type_id Base class id or -1 when none.
void rt_register_class_with_base(
    int type_id, void **vtable, const char *qname, int vslot_count, int base_type_id)
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

    rt_register_class_entry(ci, 1);
}

/// @brief Convenience wrapper to register a root class (no base).
void rt_register_class_direct(int type_id, void **vtable, const char *qname, int vslot_count)
{
    // Delegate to the base-aware version with no base class.
    rt_register_class_with_base(type_id, vtable, qname, vslot_count, -1);
}

/// @brief Fetch the vtable pointer array for a registered class id.
/// @return Vtable pointer array or NULL when unknown.
void **rt_get_class_vtable(int type_id)
{
    const class_entry *ce = find_class_by_type(type_id);
    if (!ce || !ce->ci)
        return NULL;
    return ce->ci->vtable;
}

// Runtime bridge wrapper: accept runtime string for qname
/// @brief Runtime-string bridge for @ref rt_register_class_direct.
void rt_register_class_direct_rs(int type_id, void **vtable, rt_string qname, int64_t vslot_count)
{
    const char *name = qname ? rt_string_cstr(qname) : NULL;
    rt_register_class_direct(type_id, vtable, name, (int)vslot_count);
}

// Runtime bridge wrapper: accept runtime string for qname with base class
/// @brief Runtime-string bridge for @ref rt_register_class_with_base.
void rt_register_class_with_base_rs(
    int type_id, void **vtable, rt_string qname, int64_t vslot_count, int64_t base_type_id)
{
    const char *name = qname ? rt_string_cstr(qname) : NULL;
    rt_register_class_with_base(type_id, vtable, name, (int)vslot_count, (int)base_type_id);
}

/// @brief Register an interface implementation for a class (IL-friendly wrapper).
/// @param type_id   Class type id.
/// @param iface_id  Interface id.
/// @param itable    Interface method table.
void rt_register_interface_impl(int64_t type_id, int64_t iface_id, void **itable)
{
    rt_bind_interface((int)type_id, (int)iface_id, itable);
}

/// @brief Lookup interface implementation table by type id and interface id.
/// @param type_id  Class type id.
/// @param iface_id Interface id.
/// @return Interface method table or NULL.
void **rt_get_interface_impl(int64_t type_id, int64_t iface_id)
{
    int tid = (int)type_id;
    int iid = (int)iface_id;

    // Check the exact type first.
    void **itable = find_binding(tid, iid);
    if (itable)
        return itable;

    // Walk the base class chain to find the interface binding.
    const class_entry *ce = find_class_by_type(tid);
    while (ce && ce->base_type_id >= 0)
    {
        itable = find_binding(ce->base_type_id, iid);
        if (itable)
            return itable;
        ce = find_class_by_type(ce->base_type_id);
    }
    return NULL;
}

/// @brief Clean up type registry resources for a context.
///
/// Frees all memory associated with the type registry including:
/// - Class entries and their owned rt_class_info structures
/// - Interface entries
/// - Interface binding entries
///
/// After cleanup, the registry is empty and ready for reinitialization
/// if needed.
///
/// @param ctx Context whose type registry should be cleaned up.
///
/// @note Safe to call with NULL or on an already-cleaned context.
/// @note Owned class info structures are freed; static ones are left alone.
void rt_type_registry_cleanup(RtContext *ctx)
{
    if (!ctx)
        return;

    class_entry *classes = (class_entry *)ctx->type_registry.classes;
    size_t len = ctx->type_registry.classes_len;
    if (classes)
    {
        for (size_t i = 0; i < len; ++i)
        {
            if (classes[i].owned_ci && classes[i].ci)
                free((void *)classes[i].ci);
            classes[i].ci = NULL;
            classes[i].owned_ci = 0;
            classes[i].type_id = 0;
            classes[i].base_type_id = -1;
        }
    }

    free(ctx->type_registry.classes);
    free(ctx->type_registry.ifaces);
    free(ctx->type_registry.bindings);

    ctx->type_registry.classes = NULL;
    ctx->type_registry.classes_len = 0;
    ctx->type_registry.classes_cap = 0;
    ctx->type_registry.ifaces = NULL;
    ctx->type_registry.ifaces_len = 0;
    ctx->type_registry.ifaces_cap = 0;
    ctx->type_registry.bindings = NULL;
    ctx->type_registry.bindings_len = 0;
    ctx->type_registry.bindings_cap = 0;
}
