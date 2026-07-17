//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/oop/rt_type_registry.c
// Purpose: Implements the runtime type registry that stores class and interface
//          metadata for Viper's OOP features. Supports class registration with
//          vtables, interface registration with slot counts, interface binding
//          (itable association), and type-ID-based inheritance checks.
//
// Key invariants:
//   - Classes are registered before their derived classes; base lookups succeed
//     only when the base has already been registered.
//   - Virtual method slots 0-2 are reserved for Object.ToString/Equals/GetHashCode.
//   - Type IDs are unique non-negative integers assigned at registration time.
//   - Interface binding associates an itable (function pointer array) with a
//     class-interface pair; dispatch uses this for interface method calls.
//   - The registry is per-VM-context; each context has its own isolated copy.
//   - A reader-writer lock protects concurrent access. After initialization,
//     the registry is sealed (immutable), and reads bypass the lock entirely
//     via an atomic-checked fast path for zero overhead.
//
// Ownership/Lifetime:
//   - Registered metadata lives inside the active runtime context and is
//     released by rt_type_registry_cleanup().
//   - Vtable and itable arrays are caller-owned static data; the registry
//     stores pointers without copying.
//
// Links: src/runtime/oop/rt_type_registry.h (public API, via rt_oop.h),
//        src/runtime/oop/rt_oop_dispatch.h (vtable lookup using registry data),
//        src/runtime/oop/rt_object.h (object type-tag layout)
//
//===----------------------------------------------------------------------===//

#include "rt_context.h"
#include "rt_heap.h"
#include "rt_internal.h"
#include "rt_oop.h"

#include "rt_atomic_compat.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <pthread.h>
#endif

// ============================================================================
// Reader-Writer Lock Helpers
// ============================================================================

/// @brief Initialize the per-registry reader-writer lock.
static void tr_rwlock_init(RtTypeRegistryState *st) {
    st->sealed = 0;
#ifdef _WIN32
    SRWLOCK *lock = (SRWLOCK *)malloc(sizeof(SRWLOCK));
    if (lock)
        InitializeSRWLock(lock);
    st->rw_lock = lock;
#else
    pthread_rwlock_t *lock = (pthread_rwlock_t *)malloc(sizeof(pthread_rwlock_t));
    if (lock)
        pthread_rwlock_init(lock, NULL);
    st->rw_lock = lock;
#endif
}

/// @brief Destroy and free the per-registry reader-writer lock.
static void tr_rwlock_destroy(RtTypeRegistryState *st) {
    if (!st->rw_lock)
        return;
#if !defined(_WIN32)
    pthread_rwlock_destroy((pthread_rwlock_t *)st->rw_lock);
#endif
    free(st->rw_lock);
    st->rw_lock = NULL;
}

/// @brief Acquire shared (read) lock. Returns non-zero only when a lock was acquired.
static int tr_rdlock(RtTypeRegistryState *st) {
    if (__atomic_load_n(&st->sealed, __ATOMIC_ACQUIRE))
        return 0;
    if (!st->rw_lock)
        return 0;
#ifdef _WIN32
    AcquireSRWLockShared((SRWLOCK *)st->rw_lock);
#else
    pthread_rwlock_rdlock((pthread_rwlock_t *)st->rw_lock);
#endif
    return 1;
}

/// @brief Release a shared read lock that was actually acquired by tr_rdlock().
static void tr_rdunlock(RtTypeRegistryState *st, int locked) {
    if (!locked)
        return;
    if (!st->rw_lock)
        return;
#ifdef _WIN32
    ReleaseSRWLockShared((SRWLOCK *)st->rw_lock);
#else
    pthread_rwlock_unlock((pthread_rwlock_t *)st->rw_lock);
#endif
}

/// @brief Acquire exclusive (write) lock. No-op if no lock.
static void tr_wrlock(RtTypeRegistryState *st) {
    if (!st->rw_lock)
        return;
#ifdef _WIN32
    AcquireSRWLockExclusive((SRWLOCK *)st->rw_lock);
#else
    pthread_rwlock_wrlock((pthread_rwlock_t *)st->rw_lock);
#endif
}

/// @brief Release exclusive (write) lock. No-op if no lock.
static void tr_wrunlock(RtTypeRegistryState *st) {
    if (!st->rw_lock)
        return;
#ifdef _WIN32
    ReleaseSRWLockExclusive((SRWLOCK *)st->rw_lock);
#else
    pthread_rwlock_unlock((pthread_rwlock_t *)st->rw_lock);
#endif
}

/// @brief Check whether the registry is sealed and unlock + trap if so (called under write lock).
/// @return 1 if sealed (lock released, trap fired), 0 otherwise.
static int tr_reject_if_sealed_locked(RtTypeRegistryState *st) {
    if (st && __atomic_load_n(&st->sealed, __ATOMIC_ACQUIRE)) {
        tr_wrunlock(st);
        rt_trap("rt_type_registry: cannot register after registry is sealed");
        return 1;
    }
    return 0;
}

/// @brief Free a registry-owned C string that was strdup'd during rs-variant registration.
static void tr_free_owned_cstr(const char *text) {
    free((void *)(uintptr_t)text);
}

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
typedef struct {
    int type_id;             ///< Unique class identifier.
    const rt_class_info *ci; ///< Class metadata (vtable, name, base).
    int base_type_id;        ///< Base class ID, or -1 for root classes.
    int owned_ci;            ///< Non-zero if ci should be freed on cleanup.
    int owned_qname;         ///< Non-zero if ci->qname should be freed on cleanup.
} class_entry;

/// @brief Entry in the interface registry tracking one registered interface.
///
/// Stores the interface's unique ID and its registration metadata including
/// the qualified name and slot count.
typedef struct {
    int iface_id;     ///< Unique interface identifier.
    rt_iface_reg reg; ///< Interface registration info (name, slot count).
    int owned_qname;  ///< Non-zero if reg.qname should be freed on cleanup.
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
typedef struct {
    int type_id;   ///< Class implementing the interface.
    int iface_id;  ///< Interface being implemented.
    void **itable; ///< Array of function pointers for interface methods.
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
static inline RtTypeRegistryState *rt_tr_state(void) {
    RtContext *ctx = rt_get_current_context();
    if (!ctx)
        ctx = rt_legacy_context();
    return &ctx->type_registry;
}

/// @brief Access the class-entry array and its len/cap counters from the current context.
static inline class_entry *get_classes(size_t **plen, size_t **pcap) {
    RtTypeRegistryState *st = rt_tr_state();
    if (!st)
        return NULL;
    if (plen)
        *plen = &st->classes_len;
    if (pcap)
        *pcap = &st->classes_cap;
    return (class_entry *)st->classes;
}

/// @brief Store a (potentially reallocated) class-entry array back into the current context.
static inline void set_classes(class_entry *p) {
    RtTypeRegistryState *st = rt_tr_state();
    if (st)
        st->classes = p;
}

/// @brief Access the interface-entry array and its len/cap counters from the current context.
static inline iface_entry *get_ifaces(size_t **plen, size_t **pcap) {
    RtTypeRegistryState *st = rt_tr_state();
    if (!st)
        return NULL;
    if (plen)
        *plen = &st->ifaces_len;
    if (pcap)
        *pcap = &st->ifaces_cap;
    return (iface_entry *)st->ifaces;
}

/// @brief Store a (potentially reallocated) interface-entry array back into the current context.
static inline void set_ifaces(iface_entry *p) {
    RtTypeRegistryState *st = rt_tr_state();
    if (st)
        st->ifaces = p;
}

/// @brief Access the binding-entry array and its len/cap counters from the current context.
static inline binding_entry *get_bindings(size_t **plen, size_t **pcap) {
    RtTypeRegistryState *st = rt_tr_state();
    if (!st)
        return NULL;
    if (plen)
        *plen = &st->bindings_len;
    if (pcap)
        *pcap = &st->bindings_cap;
    return (binding_entry *)st->bindings;
}

/// @brief Store a (potentially reallocated) binding-entry array back into the current context.
static inline void set_bindings(binding_entry *p) {
    RtTypeRegistryState *st = rt_tr_state();
    if (st)
        st->bindings = p;
}

/// @brief Grow a dynamic registry array by 2× when capacity is exhausted.
/// @details Initial capacity is 16; subsequent growth doubles with overflow guards.
/// @return NULL on success, or a static diagnostic string on failure.
static const char *ensure_cap(void **buf, size_t *cap, size_t elem_size) {
    if (*cap == 0) {
        size_t new_cap = 16;
        if (elem_size != 0 && new_cap > (SIZE_MAX / elem_size))
            return "rt_type_registry: size overflow";
        void *tmp = malloc(new_cap * elem_size);
        if (!tmp)
            return "rt_type_registry: alloc failed";
        *buf = tmp;
        *cap = new_cap;
        return NULL;
    }

    // Exponential growth with overflow guards
    if (*cap > (SIZE_MAX / 2))
        return "rt_type_registry: capacity overflow";
    size_t new_cap = (*cap) * 2;
    if (elem_size != 0 && new_cap > (SIZE_MAX / elem_size))
        return "rt_type_registry: size overflow";
    void *new_buf = realloc(*buf, new_cap * elem_size);
    if (!new_buf)
        return "rt_type_registry: realloc failed";
    *buf = new_buf;
    *cap = new_cap;
    return NULL;
}

/// @brief Linear search the class array for a class with the given type id.
static const class_entry *find_class_by_type(int type_id) {
    size_t *plen = NULL;
    class_entry *arr = get_classes(&plen, NULL);
    size_t len = arr && plen ? *plen : 0;
    for (size_t i = 0; i < len; ++i)
        if (arr[i].type_id == type_id)
            return &arr[i];
    return NULL;
}

/// @brief Find the class_entry whose vtable pointer matches @p vptr.
static const class_entry *find_class_by_vptr(void **vptr) {
    // Heuristic: vtable pointer equals ci->vtable
    size_t *plen = NULL;
    class_entry *arr = get_classes(&plen, NULL);
    size_t len = arr && plen ? *plen : 0;
    for (size_t i = 0; i < len; ++i)
        if (arr[i].ci && arr[i].ci->vtable == vptr)
            return &arr[i];
    return NULL;
}

/// @brief Linear search the interface array for an interface with the given id.
static const iface_entry *find_iface(int iface_id) {
    size_t *plen = NULL;
    iface_entry *arr = get_ifaces(&plen, NULL);
    size_t len = arr && plen ? *plen : 0;
    for (size_t i = 0; i < len; ++i)
        if (arr[i].iface_id == iface_id)
            return &arr[i];
    return NULL;
}

/// @brief Find the itable array for a (type_id, iface_id) binding pair, or NULL if unbound.
static void **find_binding(int type_id, int iface_id) {
    size_t *plen = NULL;
    binding_entry *arr = get_bindings(&plen, NULL);
    size_t len = arr && plen ? *plen : 0;
    for (size_t i = 0; i < len; ++i)
        if (arr[i].type_id == type_id && arr[i].iface_id == iface_id)
            return arr[i].itable;
    return NULL;
}

/// @brief NULL-safe `strcmp`-equality test for two C strings.
/// @details Treats `(NULL, NULL)` and pointer-equal cases as equal up front so registry
///          lookups that hand in optional names don't crash on a NULL probe.
static int tr_cstr_eq(const char *a, const char *b) {
    if (a == b)
        return 1;
    if (!a || !b)
        return 0;
    return strcmp(a, b) == 0;
}

/// @brief Return an object's vtable only after validating a real runtime heap object.
/// @details Cast/type/interface helpers are public C ABI entry points and may receive raw,
///          stale, or stack pointers. The generated runtime only passes heap objects, so reject
///          anything else before reading the first payload word.
static void **tr_object_vptr_or_null(void *obj) {
    if (!obj)
        return NULL;
    rt_heap_hdr_t *hdr = NULL;
    if (!rt_heap_try_get_header(obj, &hdr) || !hdr)
        return NULL;
    if ((rt_heap_kind_t)hdr->kind != RT_HEAP_OBJECT || hdr->cap < sizeof(rt_object))
        return NULL;
    rt_object *o = (rt_object *)obj;
    return o->vptr;
}

/// @brief Selectively free the parts of a class-info record that the registry owns.
/// @details The registry sometimes accepts caller-owned `rt_class_info` records and
///          sometimes allocates its own. The two ownership flags let cleanup skip the
///          parts that belong to someone else: only @p owned_qname-flagged qnames are
///          freed, and only @p owned_ci-flagged outer records are freed.
static void tr_free_owned_class_info(const rt_class_info *ci, int owned_ci, int owned_qname) {
    if (owned_qname && ci && ci->qname)
        tr_free_owned_cstr(ci->qname);
    if (owned_ci && ci)
        free((void *)(uintptr_t)ci);
}

/// @brief Test whether an existing registry entry already represents the same class.
/// @details Compares `(type_id, vtable, vtable_len, base_type_id, qname)`. Used by
///          re-registration paths to recognise idempotent calls (same class registered
///          twice from different translation units) versus genuine collisions.
static int tr_class_entry_matches(const class_entry *entry, const rt_class_info *ci) {
    if (!entry || !entry->ci || !ci)
        return 0;
    int base_type_id = ci->base ? ci->base->type_id : -1;
    return entry->type_id == ci->type_id && entry->ci->vtable == ci->vtable &&
           entry->ci->vtable_len == ci->vtable_len && entry->base_type_id == base_type_id &&
           tr_cstr_eq(entry->ci->qname, ci->qname);
}

/// @brief Append a class descriptor to the registry's class array (caller holds write lock).
/// @param owned_ci    Non-zero if the registry should free @p ci on cleanup.
/// @param owned_qname Non-zero if the registry should free ci->qname on cleanup.
static const char *rt_register_class_entry(const rt_class_info *ci, int owned_ci, int owned_qname) {
    if (!ci)
        return NULL;
    if (ci->type_id < 0) {
        tr_free_owned_class_info(ci, owned_ci, owned_qname);
        return "rt_type_registry: class type id must be non-negative";
    }
    const class_entry *existing_type = find_class_by_type(ci->type_id);
    if (existing_type) {
        int same = tr_class_entry_matches(existing_type, ci);
        tr_free_owned_class_info(ci, owned_ci, owned_qname);
        return same ? NULL : "rt_type_registry: duplicate class type id";
    }
    if (ci->vtable) {
        const class_entry *existing_vptr = find_class_by_vptr(ci->vtable);
        if (existing_vptr) {
            tr_free_owned_class_info(ci, owned_ci, owned_qname);
            return "rt_type_registry: duplicate class vtable";
        }
    }
    size_t *plen = NULL, *pcap = NULL;
    class_entry *arr = get_classes(&plen, &pcap);
    if (!plen || !pcap)
        return NULL;
    if (*plen == *pcap) {
        const char *err = ensure_cap((void **)&arr, pcap, sizeof(class_entry));
        set_classes(arr);
        if (err) {
            tr_free_owned_class_info(ci, owned_ci, owned_qname);
            return err;
        }
    }
    int base_type_id = -1;
    if (ci->base)
        base_type_id = ci->base->type_id;
    arr[(*plen)++] = (class_entry){ci->type_id, ci, base_type_id, owned_ci, owned_qname};
    return NULL;
}

/// @brief Register a class metadata descriptor with the active VM registry.
///
/// @details Appends @p ci to the per-VM class table, growing the table as
///          needed. The descriptor's @c base pointer is not modified here; use
///          @ref rt_register_class_with_base to wire base classes by id.
/// @param ci Pointer to a constant @ref rt_class_info describing the class.
void rt_register_class(const rt_class_info *ci) {
    RtTypeRegistryState *st = rt_tr_state();
    if (st && __atomic_load_n(&st->sealed, __ATOMIC_ACQUIRE)) {
        rt_trap("rt_type_registry: cannot register after registry is sealed");
        return;
    }
    if (st)
        tr_wrlock(st);
    if (tr_reject_if_sealed_locked(st))
        return;
    const char *err = rt_register_class_entry(ci, 0, 0);
    if (st)
        tr_wrunlock(st);
    if (err)
        rt_trap(err);
}

/// @brief Register an interface descriptor with the active VM registry.
/// @param iface Interface registration record (id, name, slot count).
static void rt_register_interface_entry(const rt_iface_reg *iface, int owned_qname) {
    if (!iface)
        return;
    if (iface->iface_id < 0 || iface->slot_count < 0) {
        if (owned_qname && iface->qname)
            tr_free_owned_cstr(iface->qname);
        rt_trap("rt_type_registry: invalid interface metadata");
        return;
    }
    RtTypeRegistryState *st = rt_tr_state();
    if (st && __atomic_load_n(&st->sealed, __ATOMIC_ACQUIRE)) {
        if (owned_qname && iface->qname)
            tr_free_owned_cstr(iface->qname);
        rt_trap("rt_type_registry: cannot register after registry is sealed");
        return;
    }
    if (st)
        tr_wrlock(st);
    if (tr_reject_if_sealed_locked(st)) {
        if (owned_qname && iface->qname)
            tr_free_owned_cstr(iface->qname);
        return;
    }
    size_t *plen = NULL, *pcap = NULL;
    iface_entry *arr = get_ifaces(&plen, &pcap);
    if (!arr && (!plen || !pcap)) {
        if (st)
            tr_wrunlock(st);
        if (owned_qname && iface->qname)
            tr_free_owned_cstr(iface->qname);
        return;
    }
    const iface_entry *existing = find_iface(iface->iface_id);
    if (existing) {
        int same = existing->reg.slot_count == iface->slot_count &&
                   tr_cstr_eq(existing->reg.qname, iface->qname);
        if (st)
            tr_wrunlock(st);
        if (owned_qname && iface->qname)
            tr_free_owned_cstr(iface->qname);
        if (!same)
            rt_trap("rt_type_registry: duplicate interface id");
        return;
    }
    if (*plen == *pcap) {
        const char *err = ensure_cap((void **)&arr, pcap, sizeof(iface_entry));
        set_ifaces(arr);
        if (err) {
            if (st)
                tr_wrunlock(st);
            if (owned_qname && iface->qname)
                tr_free_owned_cstr(iface->qname);
            rt_trap(err);
            return;
        }
    }
    arr[(*plen)++] = (iface_entry){iface->iface_id, *iface, owned_qname};
    if (st)
        tr_wrunlock(st);
}

/// @brief Register an interface descriptor in the global registry. Caller-owned constant
/// `rt_iface_reg` (typically static in generated code). Required before any class can claim
/// to implement the interface via `_bind_interface`.
void rt_register_interface(const rt_iface_reg *iface) {
    rt_register_interface_entry(iface, 0);
}

/// @brief Bind an interface method table to a class type id.
///
/// @details Records the association so virtual dispatch via iface calls can
///          locate the correct itable for instances of @p type_id.
/// @param type_id     Concrete class type id.
/// @param iface_id    Interface id to bind.
/// @param itable_slots Pointer to array of function pointers (length = slot_count).
void rt_bind_interface(int type_id, int iface_id, void **itable_slots) {
    if (!itable_slots)
        return;
    RtTypeRegistryState *st = rt_tr_state();
    if (st && __atomic_load_n(&st->sealed, __ATOMIC_ACQUIRE)) {
        rt_trap("rt_type_registry: cannot register after registry is sealed");
        return;
    }
    if (st)
        tr_wrlock(st);
    if (tr_reject_if_sealed_locked(st))
        return;
    if (!find_class_by_type(type_id)) {
        if (st)
            tr_wrunlock(st);
        rt_trap("rt_type_registry: cannot bind unknown class");
        return;
    }
    if (!find_iface(iface_id)) {
        if (st)
            tr_wrunlock(st);
        rt_trap("rt_type_registry: cannot bind unknown interface");
        return;
    }
    void **existing = find_binding(type_id, iface_id);
    if (existing) {
        if (st)
            tr_wrunlock(st);
        if (existing != itable_slots)
            rt_trap("rt_type_registry: duplicate interface binding");
        return;
    }
    size_t *plen = NULL, *pcap = NULL;
    binding_entry *arr = get_bindings(&plen, &pcap);
    if (!arr && (!plen || !pcap)) {
        if (st)
            tr_wrunlock(st);
        return;
    }
    if (*plen == *pcap) {
        const char *err = ensure_cap((void **)&arr, pcap, sizeof(binding_entry));
        set_bindings(arr);
        if (err) {
            if (st)
                tr_wrunlock(st);
            rt_trap(err);
            return;
        }
    }
    arr[(*plen)++] = (binding_entry){type_id, iface_id, itable_slots};
    if (st)
        tr_wrunlock(st);
}

/// @brief Return the runtime type id for an object instance.
/// @param obj Object pointer (may be NULL).
/// @return Type id when known, 0 for NULL, -1 for unknown objects.
int rt_typeid_of(void *obj) {
    if (!obj)
        return 0;
    void **vptr = tr_object_vptr_or_null(obj);
    if (!vptr)
        return -1;
    RtTypeRegistryState *st = rt_tr_state();
    int locked = 0;
    if (st)
        locked = tr_rdlock(st);
    const class_entry *ce = find_class_by_vptr(vptr);
    int result = ce ? ce->type_id : -1;
    if (st)
        tr_rdunlock(st, locked);
    return result;
}

/// @brief Check class inheritance (is-a) by type id.
/// @return 1 when @p type_id equals or derives from @p test_type_id; 0 otherwise.
int8_t rt_type_is_a(int type_id, int test_type_id) {
    if (type_id < 0 || test_type_id < 0)
        return 0;
    RtTypeRegistryState *st = rt_tr_state();
    int locked = 0;
    if (st)
        locked = tr_rdlock(st);
    const class_entry *ce = find_class_by_type(type_id);
    int8_t result = ce && type_id == test_type_id ? 1 : 0;
    while (ce && ce->base_type_id >= 0) {
        if (ce->base_type_id == test_type_id) {
            result = 1;
            break;
        }
        ce = find_class_by_type(ce->base_type_id);
    }
    if (st)
        tr_rdunlock(st, locked);
    return result;
}

/// @brief Check whether a class implements an interface by id.
/// @return 1 if implemented by the class or any ancestor; 0 otherwise.
int8_t rt_type_implements(int type_id, int iface_id) {
    if (type_id < 0 || iface_id < 0)
        return 0;
    RtTypeRegistryState *st = rt_tr_state();
    int locked = 0;
    if (st)
        locked = tr_rdlock(st);

    int8_t result = 0;

    // Check the exact type first.
    if (find_binding(type_id, iface_id) != NULL) {
        result = 1;
    } else {
        // Walk the base class chain to see if any ancestor implements the interface.
        const class_entry *ce = find_class_by_type(type_id);
        while (ce && ce->base_type_id >= 0) {
            if (find_binding(ce->base_type_id, iface_id) != NULL) {
                result = 1;
                break;
            }
            ce = find_class_by_type(ce->base_type_id);
        }
    }

    if (st)
        tr_rdunlock(st, locked);
    return result;
}

/// @brief Safe-cast an object to an interface by id.
/// @return @p obj when compatible; NULL otherwise.
void *rt_cast_as_iface(void *obj, int iface_id) {
    if (!obj || iface_id < 0)
        return NULL;
    void **vptr = tr_object_vptr_or_null(obj);
    if (!vptr)
        return NULL;

    RtTypeRegistryState *st = rt_tr_state();
    int locked = 0;
    if (st)
        locked = tr_rdlock(st);

    void *result = NULL;
    const class_entry *ce = find_class_by_vptr(vptr);
    if (ce) {
        int tid = ce->type_id;
        if (find_binding(tid, iface_id) != NULL) {
            result = obj;
        } else {
            const class_entry *base = find_class_by_type(tid);
            while (base && base->base_type_id >= 0) {
                if (find_binding(base->base_type_id, iface_id) != NULL) {
                    result = obj;
                    break;
                }
                base = find_class_by_type(base->base_type_id);
            }
        }
    }

    if (st)
        tr_rdunlock(st, locked);
    return result;
}

/// @brief Safe-cast an object to a target class by id.
/// @return @p obj when compatible; NULL otherwise.
void *rt_cast_as(void *obj, int target_type_id) {
    if (!obj || target_type_id < 0)
        return NULL;
    void **vptr = tr_object_vptr_or_null(obj);
    if (!vptr)
        return NULL;

    RtTypeRegistryState *st = rt_tr_state();
    int locked = 0;
    if (st)
        locked = tr_rdlock(st);

    void *result = NULL;
    const class_entry *ce = find_class_by_vptr(vptr);
    if (ce) {
        int tid = ce->type_id;
        if (tid == target_type_id) {
            result = obj;
        } else {
            const class_entry *cur = find_class_by_type(tid);
            while (cur && cur->base_type_id >= 0) {
                if (cur->base_type_id == target_type_id) {
                    result = obj;
                    break;
                }
                cur = find_class_by_type(cur->base_type_id);
            }
        }
    }

    if (st)
        tr_rdunlock(st, locked);
    return result;
}

/// @brief Lookup the active interface method table for an object instance.
/// @param obj      Object to query.
/// @param iface_id Interface id to search.
/// @return Pointer to the itable when found; NULL otherwise.
void **rt_itable_lookup(void *obj, int iface_id) {
    if (!obj || iface_id < 0)
        return NULL;
    void **vptr = tr_object_vptr_or_null(obj);
    if (!vptr)
        return NULL;

    RtTypeRegistryState *st = rt_tr_state();
    int locked = 0;
    if (st)
        locked = tr_rdlock(st);

    void **result = NULL;
    const class_entry *ce = find_class_by_vptr(vptr);
    if (ce) {
        int tid = ce->type_id;
        result = find_binding(tid, iface_id);
        if (!result) {
            const class_entry *cur = find_class_by_type(tid);
            while (cur && cur->base_type_id >= 0) {
                result = find_binding(cur->base_type_id, iface_id);
                if (result)
                    break;
                cur = find_class_by_type(cur->base_type_id);
            }
        }
    }

    if (st)
        tr_rdunlock(st, locked);
    return result;
}

/// @brief Convenience wrapper to register an interface using C strings.
void rt_register_interface_direct(int iface_id, const char *qname, int slot_count) {
    (void)qname;
    (void)slot_count; // stored in reg; currently unused by runtime
    rt_iface_reg r = {iface_id, qname, slot_count};
    rt_register_interface_entry(&r, 0);
}

/// @brief Runtime-string bridge for @ref rt_register_interface_direct.
void rt_register_interface_direct_rs(int64_t iface_id, rt_string qname, int64_t slot_count) {
    if (iface_id < 0 || iface_id > INT_MAX || slot_count < 0 || slot_count > INT_MAX) {
        rt_trap("rt_type_registry: interface metadata out of range");
        return;
    }
    if (qname && !rt_string_is_handle((const void *)qname)) {
        rt_trap("rt_type_registry: invalid interface name string");
        return;
    }
    const char *name = qname ? strdup(rt_string_cstr(qname)) : NULL;
    if (qname && !name) {
        rt_trap("rt_type_registry: interface name alloc failed");
        return;
    }
    rt_iface_reg r = {(int)iface_id, name, (int)slot_count};
    rt_register_interface_entry(&r, 1);
}

/// @brief Resolve a class descriptor from a vtable pointer.
/// @return Class info when registered; NULL otherwise.
const rt_class_info *rt_get_class_info_from_vptr(void **vptr) {
    if (!vptr)
        return NULL;
    RtTypeRegistryState *st = rt_tr_state();
    int locked = 0;
    if (st)
        locked = tr_rdlock(st);
    const class_entry *ce = find_class_by_vptr(vptr);
    const rt_class_info *result = ce ? ce->ci : NULL;
    if (st)
        tr_rdunlock(st, locked);
    return result;
}

/// @brief Register a class descriptor built from parts, with base by id.
/// @param type_id      Assigned class id.
/// @param vtable       Vtable pointer array.
/// @param qname        Qualified class name (borrowed).
/// @param vslot_count  Number of entries in the vtable.
/// @param base_type_id Base class id or -1 when none.
static void rt_register_class_with_base_impl(int type_id,
                                             void **vtable,
                                             const char *qname,
                                             int vslot_count,
                                             int base_type_id,
                                             int owned_qname) {
    if (!vtable) {
        if (owned_qname && qname)
            tr_free_owned_cstr(qname);
        return;
    }
    if (type_id < 0 || vslot_count < 0 || base_type_id < -1) {
        if (owned_qname && qname)
            tr_free_owned_cstr(qname);
        rt_trap("rt_type_registry: invalid class metadata");
        return;
    }
    RtTypeRegistryState *st = rt_tr_state();
    if (st && __atomic_load_n(&st->sealed, __ATOMIC_ACQUIRE)) {
        if (owned_qname && qname)
            tr_free_owned_cstr(qname);
        rt_trap("rt_type_registry: cannot register after registry is sealed");
        return;
    }

    if (st)
        tr_wrlock(st);
    if (tr_reject_if_sealed_locked(st)) {
        if (owned_qname && qname)
            tr_free_owned_cstr(qname);
        return;
    }

    const class_entry *base_entry = NULL;
    if (base_type_id >= 0) {
        base_entry = find_class_by_type(base_type_id);
        if (!base_entry || !base_entry->ci) {
            if (st)
                tr_wrunlock(st);
            if (owned_qname && qname)
                tr_free_owned_cstr(qname);
            rt_trap("rt_type_registry: base class is not registered");
            return;
        }
    }

    rt_class_info *ci = (rt_class_info *)malloc(sizeof(rt_class_info));
    if (!ci) {
        if (st)
            tr_wrunlock(st);
        if (owned_qname && qname)
            tr_free_owned_cstr(qname);
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
    if (base_entry)
        ci->base = base_entry->ci;

    const char *err = rt_register_class_entry(ci, 1, owned_qname);

    if (st)
        tr_wrunlock(st);
    if (err)
        rt_trap(err);
}

/// @brief Register a class with explicit base-class wiring. `base_type_id == -1` for root
/// classes. The qname string is borrowed (caller-owned static string); use `_rs` variants for
/// rt_string-based registration that copies internally.
void rt_register_class_with_base(
    int type_id, void **vtable, const char *qname, int vslot_count, int base_type_id) {
    rt_register_class_with_base_impl(type_id, vtable, qname, vslot_count, base_type_id, 0);
}

/// @brief Convenience wrapper to register a root class (no base).
void rt_register_class_direct(int type_id, void **vtable, const char *qname, int vslot_count) {
    // Delegate to the base-aware version with no base class.
    rt_register_class_with_base_impl(type_id, vtable, qname, vslot_count, -1, 0);
}

/// @brief Fetch the vtable pointer array for a registered class id.
/// @return Vtable pointer array or NULL when unknown.
void **rt_get_class_vtable(int type_id) {
    if (type_id < 0)
        return NULL;
    RtTypeRegistryState *st = rt_tr_state();
    int locked = 0;
    if (st)
        locked = tr_rdlock(st);
    const class_entry *ce = find_class_by_type(type_id);
    void **result = (ce && ce->ci) ? ce->ci->vtable : NULL;
    if (st)
        tr_rdunlock(st, locked);
    return result;
}

// Runtime bridge wrapper: accept runtime string for qname
/// @brief Runtime-string bridge for @ref rt_register_class_direct.
void rt_register_class_direct_rs(int type_id, void **vtable, rt_string qname, int64_t vslot_count) {
    // strdup the name to avoid dangling pointer if the rt_string is freed
    if (vslot_count < 0 || vslot_count > INT_MAX) {
        rt_trap("rt_type_registry: class metadata out of range");
        return;
    }
    if (qname && !rt_string_is_handle((const void *)qname)) {
        rt_trap("rt_type_registry: invalid class name string");
        return;
    }
    const char *name = qname ? strdup(rt_string_cstr(qname)) : NULL;
    if (qname && !name) {
        rt_trap("rt_type_registry: class name alloc failed");
        return;
    }
    rt_register_class_with_base_impl(type_id, vtable, name, (int)vslot_count, -1, 1);
}

// Runtime bridge wrapper: accept runtime string for qname with base class
/// @brief Runtime-string bridge for @ref rt_register_class_with_base.
void rt_register_class_with_base_rs(
    int type_id, void **vtable, rt_string qname, int64_t vslot_count, int64_t base_type_id) {
    // strdup the name to avoid dangling pointer if the rt_string is freed
    if (vslot_count < 0 || vslot_count > INT_MAX || base_type_id < -1 || base_type_id > INT_MAX) {
        rt_trap("rt_type_registry: class metadata out of range");
        return;
    }
    if (qname && !rt_string_is_handle((const void *)qname)) {
        rt_trap("rt_type_registry: invalid class name string");
        return;
    }
    const char *name = qname ? strdup(rt_string_cstr(qname)) : NULL;
    if (qname && !name) {
        rt_trap("rt_type_registry: class name alloc failed");
        return;
    }
    rt_register_class_with_base_impl(type_id, vtable, name, (int)vslot_count, (int)base_type_id, 1);
}

/// @brief Register an interface implementation for a class (IL-friendly wrapper).
/// @param type_id   Class type id.
/// @param iface_id  Interface id.
/// @param itable    Interface method table.
void rt_register_interface_impl(int64_t type_id, int64_t iface_id, void **itable) {
    if (type_id < 0 || type_id > INT_MAX || iface_id < 0 || iface_id > INT_MAX) {
        rt_trap("rt_type_registry: interface binding metadata out of range");
        return;
    }
    rt_bind_interface((int)type_id, (int)iface_id, itable);
}

/// @brief Lookup interface implementation table by type id and interface id.
/// @param type_id  Class type id.
/// @param iface_id Interface id.
/// @return Interface method table or NULL.
void **rt_get_interface_impl(int64_t type_id, int64_t iface_id) {
    if (type_id < 0 || type_id > INT_MAX || iface_id < 0 || iface_id > INT_MAX)
        return NULL;
    int tid = (int)type_id;
    int iid = (int)iface_id;

    RtTypeRegistryState *st = rt_tr_state();
    int locked = 0;
    if (st)
        locked = tr_rdlock(st);

    void **result = find_binding(tid, iid);
    if (!result) {
        const class_entry *ce = find_class_by_type(tid);
        while (ce && ce->base_type_id >= 0) {
            result = find_binding(ce->base_type_id, iid);
            if (result)
                break;
            ce = find_class_by_type(ce->base_type_id);
        }
    }

    if (st)
        tr_rdunlock(st, locked);
    return result;
}

// ============================================================================
// Registry Lifecycle
// ============================================================================

/// @brief Initialize the type registry's reader-writer lock for a context.
///
/// Called during context creation to set up the rwlock that protects
/// concurrent access to the registry arrays. Must be called before any
/// registration or lookup operations.
///
/// @param ctx Context whose type registry should be initialized.
void rt_type_registry_init(RtContext *ctx) {
    if (!ctx)
        return;
    tr_rwlock_init(&ctx->type_registry);
}

/// @brief Seal the type registry, enabling lock-free reads.
///
/// After all type registration is complete (typically at the end of module
/// initialization), call this to mark the registry as immutable. Once sealed:
/// - Read operations bypass the rwlock entirely (zero overhead).
/// - Write operations (registration) will trap with an error.
///
/// @thread-safety Safe to call from any thread; sealing is performed while the
///                registry write lock is held so no writer can race the
///                lock-free read transition.
void rt_type_registry_seal(void) {
    RtTypeRegistryState *st = rt_tr_state();
    if (!st)
        return;
    tr_wrlock(st);
    __atomic_store_n(&st->sealed, 1, __ATOMIC_RELEASE);
    tr_wrunlock(st);
}

/// @brief Clean up type registry resources for a context.
///
/// Frees all memory associated with the type registry including:
/// - Class entries and their owned rt_class_info structures
/// - Interface entries
/// - Interface binding entries
/// - The reader-writer lock
///
/// After cleanup, the registry is empty and ready for reinitialization
/// if needed.
///
/// @param ctx Context whose type registry should be cleaned up.
///
/// @note Safe to call with NULL or on an already-cleaned context.
/// @note Owned class info structures are freed; static ones are left alone.
void rt_type_registry_cleanup(RtContext *ctx) {
    if (!ctx)
        return;

    class_entry *classes = (class_entry *)ctx->type_registry.classes;
    size_t len = ctx->type_registry.classes_len;
    if (classes) {
        for (size_t i = 0; i < len; ++i) {
            if (classes[i].owned_qname && classes[i].ci && classes[i].ci->qname)
                tr_free_owned_cstr(classes[i].ci->qname);
            if (classes[i].owned_ci && classes[i].ci)
                free((void *)(uintptr_t)classes[i].ci);
            classes[i].ci = NULL;
            classes[i].owned_ci = 0;
            classes[i].owned_qname = 0;
            classes[i].type_id = 0;
            classes[i].base_type_id = -1;
        }
    }

    free(ctx->type_registry.classes);
    iface_entry *ifaces = (iface_entry *)ctx->type_registry.ifaces;
    size_t ifaces_len = ctx->type_registry.ifaces_len;
    if (ifaces) {
        for (size_t i = 0; i < ifaces_len; ++i) {
            if (ifaces[i].owned_qname && ifaces[i].reg.qname)
                tr_free_owned_cstr(ifaces[i].reg.qname);
            ifaces[i].reg.qname = NULL;
            ifaces[i].owned_qname = 0;
        }
    }
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
    tr_rwlock_destroy(&ctx->type_registry);
    ctx->type_registry.rw_lock = NULL;
    ctx->type_registry.sealed = 0;
}
