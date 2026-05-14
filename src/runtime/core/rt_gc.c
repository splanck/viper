//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/core/rt_gc.c
// Purpose: Implements the cycle-detecting garbage collector for the Viper
//          runtime. Uses a trial-deletion (synchronous mark-sweep) algorithm:
//          trial-decrement child refcounts, identify zero-trial-refcount
//          candidates as potential cycle members, restore reachable objects,
//          then free confirmed cycles.
//
//          The tracked-object set is stored in an open-addressing hash table
//          (power-of-two capacity, linear probing, tombstone deletion) for O(1)
//          lookup during the trial-decrement and restore phases.
//
// Key invariants:
//   - Objects must be registered via rt_gc_track before cycles can be detected;
//     untracked objects rely solely on reference counting for collection.
//   - Trial refcounts are temporary; they are computed per-pass and do not
//     modify the actual reference counts stored in heap headers.
//   - Weak references to collected objects are zeroed after finalizers decline
//     resurrection, preserving weak handles when a finalizer keeps the target alive.
//   - The GC table lock (pthread_mutex / CRITICAL_SECTION) protects the tracked-
//     object table and weak reference registry; finalizers run outside the lock.
//   - Pass statistics (total_collected, pass_count) are updated atomically.
//
// Ownership/Lifetime:
//   - The global GC state (entries table, weak ref registry) is heap-allocated
//     lazily and lives for the process lifetime.
//   - Tracked object pointers are borrowed; the GC does not retain a reference
//     — it relies on the object's own refcount to stay alive until collection.
//
// Links: src/runtime/core/rt_gc.h (public API),
//        src/runtime/core/rt_heap.c (heap header layout, refcount fields),
//        src/runtime/core/rt_object.c (object allocation and finalizer registry)
//
//===----------------------------------------------------------------------===//

#include "rt_gc.h"

#include "rt_atomic_compat.h"
#include "rt_heap.h"
#include "rt_internal.h"
#include "rt_object.h"
#include "rt_string.h"
#include "rt_threads.h"

#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <pthread.h>
#endif

#include "rt_trap.h"

void rt_trap_set_recovery(jmp_buf *buf);
void rt_trap_clear_recovery(void);
const char *rt_trap_get_error(void);

//=============================================================================
// Internal Data Structures
//=============================================================================

/// Sentinel values for the open-addressing hash table.
/// GC_EMPTY marks a slot that has never been used (terminates probe chains).
/// GC_TOMBSTONE marks a deleted slot (skipped during probing, reusable on insert).
#define GC_EMPTY NULL
#define GC_TOMBSTONE ((void *)(uintptr_t)1)

/// Epoch tagging: objects that survive GC_PROMOTION_THRESHOLD consecutive
/// collection passes are "promoted" and skipped in future trial-deletion
/// phases.  Every GC_FULL_SCAN_INTERVAL passes, ALL objects are scanned
/// regardless of promotion (catches new cycles involving promoted objects).
#define GC_PROMOTION_THRESHOLD 8
#define GC_FULL_SCAN_INTERVAL 16

/// Entry in the tracked-object hash table.
typedef struct gc_entry {
    void *obj; ///< Object pointer (NULL=empty, 1=tombstone, else live).
    rt_gc_traverse_fn traverse;
    int64_t trial_rc;  ///< Temporary refcount for cycle detection.
    int8_t color;      ///< 0=white(unchecked), 1=gray(candidate), 2=black(reachable)
    uint16_t survived; ///< Number of collection passes this object has survived.
} gc_entry;

/// Weak reference record.
struct rt_weakref {
    void *target;
    struct rt_weakref *next_for_target; ///< Chain of weak refs to same target.
};

/// Weak ref registry entry (per-target chain).
typedef struct weak_chain {
    void *target;
    rt_weakref *head;
    struct weak_chain *next;
} weak_chain;

/// Global GC state.
static struct {
    gc_entry *entries; ///< Open-addressing hash table (power-of-two capacity).
    int64_t count;     ///< Number of live entries (excludes tombstones).
    int64_t capacity;  ///< Table size (always a power of two, or 0).

    weak_chain *weak_buckets;
    int64_t weak_bucket_count;

    int64_t total_collected;
    int64_t pass_count;
    int collecting;
} g_gc;

/// GC lock — initialized once and kept alive for the process lifetime.
/// `rt_gc_shutdown()` releases GC-owned tables but intentionally does not
/// destroy this primitive, so embedders and tests can shut down and reuse the
/// GC without racing a late lock user.
#ifdef _WIN32
static INIT_ONCE g_gc_lock_once = INIT_ONCE_STATIC_INIT;
static CRITICAL_SECTION g_gc_lock_cs;
#else
static pthread_mutex_t g_gc_lock_mtx = PTHREAD_MUTEX_INITIALIZER;
#endif

/// Auto-trigger: allocation counter and threshold.
/// When g_gc_threshold > 0, every rt_gc_notify_alloc() call increments
/// g_gc_alloc_counter; when the counter reaches the threshold, a collection
/// pass is triggered automatically.
static int64_t g_gc_threshold = 0;
static int64_t g_gc_alloc_counter = 0;

/// @brief Atomic CAS for int64_t (portable across GCC/Clang and MSVC).
static int gc_atomic_cas_i64(int64_t *ptr, int64_t *expected, int64_t desired) {
#if defined(_MSC_VER) && !defined(__clang__)
    long long old = _InterlockedCompareExchange64(
        (volatile long long *)ptr, (long long)desired, (long long)*expected);
    if (old == (long long)*expected)
        return 1;
    *expected = (int64_t)old;
    return 0;
#else
    return __atomic_compare_exchange_n(
        ptr, expected, desired, /*weak=*/1, __ATOMIC_RELAXED, __ATOMIC_RELAXED);
#endif
}

//=============================================================================
// Lock helpers (CONC-001 fix: race-free initialization)
//=============================================================================

#ifdef _WIN32
/// @brief InitOnce callback that initialises the CRITICAL_SECTION on first use.
static BOOL CALLBACK gc_lock_init_callback(PINIT_ONCE InitOnce, PVOID Parameter, PVOID *Context) {
    (void)InitOnce;
    (void)Parameter;
    (void)Context;
    InitializeCriticalSection(&g_gc_lock_cs);
    return TRUE;
}

/// @brief Acquire the GC lock, initialising the CRITICAL_SECTION on first call.
static void gc_lock(void) {
    InitOnceExecuteOnce(&g_gc_lock_once, gc_lock_init_callback, NULL, NULL);
    EnterCriticalSection(&g_gc_lock_cs);
}

/// @brief Release the GC lock.
static void gc_unlock(void) {
    LeaveCriticalSection(&g_gc_lock_cs);
}
#else
/// @brief Acquire the GC mutex (POSIX static initializer — always safe to call).
static void gc_lock(void) {
    pthread_mutex_lock(&g_gc_lock_mtx);
}

/// @brief Release the GC mutex.
static void gc_unlock(void) {
    pthread_mutex_unlock(&g_gc_lock_mtx);
}
#endif

//=============================================================================
// Hash Utility
//=============================================================================

/// @brief Splitmix64-style pointer hash for hash table slot computation.
static uint64_t ptr_hash(void *p) {
    uint64_t v = (uint64_t)(uintptr_t)p;
    v = (v ^ (v >> 30)) * 0xbf58476d1ce4e5b9ULL;
    v = (v ^ (v >> 27)) * 0x94d049bb133111ebULL;
    return v ^ (v >> 31);
}

/// @brief Check if a hash table slot contains a live (tracked) entry.
static int gc_slot_is_live(const gc_entry *e) {
    return e->obj != GC_EMPTY && e->obj != GC_TOMBSTONE;
}

/// @brief Clear the in-progress-collection sentinel under the GC lock.
/// @details Called from the reclaim-phase trap-recovery branch in `rt_gc_collect` so a
///          finalizer that traps doesn't leave the active-collection flag stuck on,
///          which would cause every later `rt_gc_collect` call to short-circuit and
///          return zero forever.
static void gc_clear_collecting_flag(void) {
    gc_lock();
    g_gc.collecting = 0;
    gc_unlock();
}

//=============================================================================
// Tracked Objects Hash Table
//=============================================================================

/// @brief Find the slot index for @p obj in the hash table.
/// @details Uses linear probing. Tombstones are skipped (do not terminate
///          the probe chain); empty slots terminate it.
/// @return Slot index if found, -1 otherwise. Caller must hold gc_lock.
static int64_t find_entry(void *obj) {
    if (!g_gc.entries || g_gc.capacity == 0)
        return -1;
    uint64_t mask = (uint64_t)(g_gc.capacity - 1);
    uint64_t idx = ptr_hash(obj) & mask;
    for (int64_t probe = 0; probe < g_gc.capacity; probe++) {
        gc_entry *e = &g_gc.entries[idx];
        if (e->obj == obj)
            return (int64_t)idx;
        if (e->obj == GC_EMPTY)
            return -1;
        /* Tombstone — keep probing. */
        idx = (idx + 1) & mask;
    }
    return -1;
}

/// @brief Rehash all live entries into a new table of @p new_cap slots.
/// @details Allocates a fresh zero-initialised table, re-inserts every live
///          entry, and frees the old table. Tombstones are discarded. The
///          caller must hold gc_lock.
/// @param new_cap New table capacity (must be a power of two).
static int gc_rehash(int64_t new_cap) {
    if (new_cap <= 0 || (uint64_t)new_cap > (uint64_t)SIZE_MAX / sizeof(gc_entry))
        return 0;
    gc_entry *old = g_gc.entries;
    int64_t old_cap = g_gc.capacity;

    gc_entry *new_entries = (gc_entry *)calloc((size_t)new_cap, sizeof(gc_entry));
    if (!new_entries)
        return 0;

    uint64_t mask = (uint64_t)(new_cap - 1);
    int64_t live = 0;

    for (int64_t i = 0; i < old_cap; i++) {
        if (!gc_slot_is_live(&old[i]))
            continue;
        uint64_t slot = ptr_hash(old[i].obj) & mask;
        while (new_entries[slot].obj != GC_EMPTY)
            slot = (slot + 1) & mask;
        new_entries[slot] = old[i];
        live++;
    }

    g_gc.entries = new_entries;
    g_gc.capacity = new_cap;
    g_gc.count = live;
    free(old);
    return 1;
}

/// @brief Register an object for cycle detection.
/// @details Inserts @p obj into the GC hash table. If already tracked, updates
///          the traverse function. The table grows when load exceeds 5/8.
void rt_gc_track(void *obj, rt_gc_traverse_fn traverse) {
    if (!obj || !traverse)
        return;
    rt_heap_hdr_t *hdr = NULL;
    if (!rt_heap_try_get_header(obj, &hdr) || !hdr) {
        rt_trap("rt_gc_track: object is not a live heap payload");
        return;
    }
    if ((rt_heap_kind_t)hdr->kind != RT_HEAP_OBJECT) {
        rt_trap("rt_gc_track: payload is not a heap object");
        return;
    }

    gc_lock();

    /* Already tracked? Update traverse function. */
    int64_t idx = find_entry(obj);
    if (idx >= 0) {
        g_gc.entries[idx].traverse = traverse;
        gc_unlock();
        return;
    }

    /* Grow if needed: maintain < 5/8 load factor. */
    if (g_gc.capacity == 0 || g_gc.count >= (g_gc.capacity / 8) * 5) {
        if (g_gc.capacity > 0 && g_gc.capacity > INT64_MAX / 2) {
            gc_unlock();
            rt_trap("rt_gc: hash table capacity overflow");
            return;
        }
        int64_t new_cap = g_gc.capacity == 0 ? 64 : g_gc.capacity * 2;
        if (!gc_rehash(new_cap)) {
            gc_unlock();
            rt_trap("rt_gc: failed to grow hash table (out of memory)");
            return;
        }
    }

    /* Insert at first empty or tombstone slot. */
    uint64_t mask = (uint64_t)(g_gc.capacity - 1);
    uint64_t slot = ptr_hash(obj) & mask;
    while (gc_slot_is_live(&g_gc.entries[slot]))
        slot = (slot + 1) & mask;

    gc_entry *e = &g_gc.entries[slot];
    e->obj = obj;
    e->traverse = traverse;
    e->trial_rc = 0;
    e->color = 0;
    e->survived = 0;
    g_gc.count++;

    gc_unlock();
}

/// @brief Remove an object from cycle tracking.
/// @details Tombstones the hash table slot so probe chains remain intact.
void rt_gc_untrack(void *obj) {
    if (!obj)
        return;

    gc_lock();

    int64_t idx = find_entry(obj);
    if (idx >= 0) {
        /* Mark slot as tombstone so probe chains are preserved. */
        g_gc.entries[idx].obj = GC_TOMBSTONE;
        g_gc.entries[idx].traverse = NULL;
        g_gc.count--;
    }

    gc_unlock();
}

/// @brief Check if an object is in the tracking table.
int8_t rt_gc_is_tracked(void *obj) {
    if (!obj)
        return 0;

    gc_lock();
    int8_t found = find_entry(obj) >= 0 ? 1 : 0;
    gc_unlock();
    return found;
}

/// @brief Return the number of objects currently in the tracking table.
int64_t rt_gc_tracked_count(void) {
    gc_lock();
    int64_t n = g_gc.count;
    gc_unlock();
    return n;
}

//=============================================================================
// Weak Reference Registry
//=============================================================================

#define WEAK_BUCKET_COUNT 64

/// @brief Lazily allocate the weak-reference bucket table on first use.
static void ensure_weak_buckets(void) {
    if (g_gc.weak_buckets)
        return;
    g_gc.weak_bucket_count = WEAK_BUCKET_COUNT;
    g_gc.weak_buckets = (weak_chain *)calloc((size_t)g_gc.weak_bucket_count, sizeof(weak_chain));
    if (!g_gc.weak_buckets)
        rt_trap("gc: memory allocation failed");
}

/// @brief Add @p ref to the per-target weak-reference chain for @p target.
/// @return 1 on success, 0 on allocation failure.
static int register_weak_ref(void *target, rt_weakref *ref) {
    ensure_weak_buckets();
    uint64_t bucket = ptr_hash(target) % (uint64_t)g_gc.weak_bucket_count;

    /* Find existing chain for this target. */
    weak_chain *wc = g_gc.weak_buckets[bucket].next;
    while (wc) {
        if (wc->target == target) {
            ref->next_for_target = wc->head;
            wc->head = ref;
            return 1;
        }
        wc = wc->next;
    }

    /* Create new chain. */
    weak_chain *new_wc = (weak_chain *)malloc(sizeof(weak_chain));
    if (!new_wc)
        return 0;
    new_wc->target = target;
    new_wc->head = ref;
    new_wc->next = g_gc.weak_buckets[bucket].next;
    g_gc.weak_buckets[bucket].next = new_wc;
    return 1;
}

/// @brief Remove @p ref from the per-target weak-reference chain for @p target.
static void unregister_weak_ref(void *target, rt_weakref *ref) {
    if (!g_gc.weak_buckets || g_gc.weak_bucket_count <= 0)
        return;
    uint64_t bucket = ptr_hash(target) % (uint64_t)g_gc.weak_bucket_count;
    weak_chain **wc_pp = &g_gc.weak_buckets[bucket].next;

    while (*wc_pp) {
        weak_chain *wc = *wc_pp;
        if (wc->target == target) {
            rt_weakref **pp = &wc->head;
            while (*pp) {
                if (*pp == ref) {
                    *pp = ref->next_for_target;
                    ref->next_for_target = NULL;
                    if (!wc->head) {
                        *wc_pp = wc->next;
                        free(wc);
                    }
                    return;
                }
                pp = &(*pp)->next_for_target;
            }
            return;
        }
        wc_pp = &(*wc_pp)->next;
    }
}

/// @brief Return 1 if @p candidate is a live weak-reference heap object (lock must be held).
static int gc_is_weakref_handle_unlocked(void *candidate) {
    rt_heap_hdr_t *hdr = NULL;
    if (!candidate || !rt_heap_try_get_header(candidate, &hdr) || !hdr)
        return 0;
    return (rt_heap_kind_t)hdr->kind == RT_HEAP_OBJECT && hdr->class_id == RT_WEAKREF_CLASS_ID &&
                   hdr->cap >= sizeof(rt_weakref)
               ? 1
               : 0;
}

/// @brief Return 1 if @p target can be registered as a zeroing weak target.
static int gc_weak_target_is_valid(void *target) {
    if (!target)
        return 1;
    if (rt_string_is_handle(target))
        return 1;
    rt_heap_hdr_t *hdr = NULL;
    if (!rt_heap_try_get_header(target, &hdr) || !hdr)
        return 0;
    return (rt_heap_kind_t)hdr->kind != RT_HEAP_STRING ? 1 : 0;
}

/// @brief Detach a weak-reference object from the registry during generic object release.
static void weakref_finalizer(void *obj) {
    rt_weakref *ref = (rt_weakref *)obj;
    if (!ref)
        return;
    gc_lock();
    if (ref->target)
        unregister_weak_ref(ref->target, ref);
    ref->target = NULL;
    ref->next_for_target = NULL;
    gc_unlock();
}

//=============================================================================
// Zeroing Weak References (Public API)
//=============================================================================

/// @brief Create a zeroing weak reference. The target's refcount is NOT bumped.
/// @details When the target is freed, the reference automatically becomes NULL.
rt_weakref *rt_weakref_new(void *target) {
    if (!gc_weak_target_is_valid(target)) {
        rt_trap("gc: weak reference target is not a live runtime handle");
        return NULL;
    }
    rt_weakref *ref = (rt_weakref *)rt_obj_new_i64(RT_WEAKREF_CLASS_ID, (int64_t)sizeof(rt_weakref));
    if (!ref) {
        rt_trap("gc: weak reference allocation failed");
        return NULL;
    }
    memset(ref, 0, sizeof(rt_weakref));
    rt_obj_set_finalizer(ref, weakref_finalizer);

    gc_lock();
    ref->target = target;
    ref->next_for_target = NULL;
    if (target && !register_weak_ref(target, ref)) {
        ref->target = NULL;
        gc_unlock();
        if (rt_obj_release_check0(ref))
            rt_obj_free(ref);
        rt_trap("gc: weak reference allocation failed");
        return NULL;
    }
    gc_unlock();

    return ref;
}

/// @brief Dereference a weak ref, retaining and returning the target or NULL if freed.
void *rt_weakref_get(rt_weakref *ref) {
    if (!ref)
        return NULL;

    gc_lock();
    if (!gc_is_weakref_handle_unlocked(ref)) {
        gc_unlock();
        return NULL;
    }
    void *t = ref->target;
    if (t) {
        if (rt_string_is_handle(t)) {
            rt_string s = (rt_string)t;
            rt_heap_hdr_t *hdr = s->heap && s->heap != RT_SSO_SENTINEL ? s->heap : NULL;
            size_t *refs = hdr ? &hdr->refcnt : &s->literal_refs;
            size_t old = __atomic_load_n(refs, __ATOMIC_RELAXED);
            for (;;) {
                if (old == 0) {
                    t = NULL;
                    break;
                }
                if (old >= RT_HEAP_IMMORTAL_REFCNT)
                    break;
                if (old >= RT_HEAP_MAX_MORTAL_REFCNT) {
                    gc_unlock();
                    rt_trap("gc: weak reference promotion refcount overflow");
                    return NULL;
                }
                size_t next = old + 1;
                if (__atomic_compare_exchange_n(refs,
                                                &old,
                                                next,
                                                /*weak=*/0,
                                                __ATOMIC_RELAXED,
                                                __ATOMIC_RELAXED)) {
                    break;
                }
            }
        } else {
            rt_heap_hdr_t *hdr = NULL;
            if (!rt_heap_try_get_header(t, &hdr) || !hdr) {
                t = NULL;
            } else {
                size_t old = __atomic_load_n(&hdr->refcnt, __ATOMIC_RELAXED);
                for (;;) {
                    if (old == 0) {
                        t = NULL;
                        break;
                    }
                    if (old >= RT_HEAP_IMMORTAL_REFCNT)
                        break;
                    if (old >= RT_HEAP_MAX_MORTAL_REFCNT) {
                        gc_unlock();
                        rt_trap("gc: weak reference promotion refcount overflow");
                        return NULL;
                    }
                    size_t next = old + 1;
                    if (__atomic_compare_exchange_n(&hdr->refcnt,
                                                    &old,
                                                    next,
                                                    /*weak=*/0,
                                                    __ATOMIC_RELAXED,
                                                    __ATOMIC_RELAXED)) {
                        break;
                    }
                }
            }
        }
    }
    gc_unlock();
    return t;
}

/// @brief Check if a weak reference's target is still alive.
int8_t rt_weakref_alive(rt_weakref *ref) {
    if (!ref)
        return 0;

    gc_lock();
    if (!gc_is_weakref_handle_unlocked(ref)) {
        gc_unlock();
        return 0;
    }
    int8_t alive = 0;
    void *target = ref->target;
    if (target) {
        if (rt_string_is_handle(target)) {
            rt_string s = (rt_string)target;
            rt_heap_hdr_t *hdr = s->heap && s->heap != RT_SSO_SENTINEL ? s->heap : NULL;
            size_t refs =
                __atomic_load_n(hdr ? &hdr->refcnt : &s->literal_refs, __ATOMIC_RELAXED);
            alive = refs > 0 ? 1 : 0;
        } else {
            rt_heap_hdr_t *hdr = NULL;
            if (rt_heap_try_get_header(target, &hdr) && hdr) {
                size_t refs = __atomic_load_n(&hdr->refcnt, __ATOMIC_RELAXED);
                alive = refs > 0 ? 1 : 0;
            }
        }
    }
    gc_unlock();
    return alive;
}

/// @brief Destroy a weak reference handle. Does NOT affect the target.
void rt_weakref_free(rt_weakref *ref) {
    if (!ref)
        return;

    gc_lock();
    if (!gc_is_weakref_handle_unlocked(ref)) {
        gc_unlock();
        rt_trap("gc: invalid or freed weak reference");
        return;
    }
    if (ref->target)
        unregister_weak_ref(ref->target, ref);
    ref->target = NULL;
    ref->next_for_target = NULL;
    gc_unlock();

    if (rt_obj_release_check0(ref))
        rt_obj_free(ref);
}

/// @brief Returns 1 if `candidate` is a registered weak-reference handle. Used by polymorphic
/// dispatch sites to distinguish weak-ref objects from regular GC objects.
int8_t rt_weakref_is_handle(void *candidate) {
    int8_t is_handle = 0;
    gc_lock();
    is_handle = gc_is_weakref_handle_unlocked(candidate);
    gc_unlock();
    return is_handle;
}

/// @brief Re-point a weak reference at a different target (or to NULL to clear). Unregisters
/// from the old target's weak-list and registers with the new one.
void rt_weakref_reset(rt_weakref *ref, void *target) {
    if (!ref)
        return;
    if (!gc_weak_target_is_valid(target)) {
        rt_trap("gc: weak reference target is not a live runtime handle");
        return;
    }

    gc_lock();
    if (!gc_is_weakref_handle_unlocked(ref)) {
        gc_unlock();
        return;
    }
    if (ref->target)
        unregister_weak_ref(ref->target, ref);
    ref->target = target;
    ref->next_for_target = NULL;
    if (target && !register_weak_ref(target, ref)) {
        ref->target = NULL;
        gc_unlock();
        rt_trap("gc: weak reference allocation failed");
        return;
    }
    gc_unlock();
}

/// @brief Zero all weak references pointing to a target being freed.
/// @details Called internally by rt_obj_free before deallocating. Walks the
///          per-target chain in the weak bucket and sets each ref's target to NULL.
void rt_gc_clear_weak_refs(void *target) {
    if (!target)
        return;

    gc_lock();
    if (!g_gc.weak_buckets || g_gc.weak_bucket_count <= 0) {
        gc_unlock();
        return;
    }
    uint64_t bucket = ptr_hash(target) % (uint64_t)g_gc.weak_bucket_count;

    weak_chain **wc_pp = &g_gc.weak_buckets[bucket].next;
    while (*wc_pp) {
        weak_chain *wc = *wc_pp;
        if (wc->target == target) {
            /* Clear all weak refs in this chain. */
            rt_weakref *r = wc->head;
            while (r) {
                rt_weakref *next = r->next_for_target;
                r->target = NULL;
                r->next_for_target = NULL;
                r = next;
            }

            /* Remove chain from bucket. */
            *wc_pp = wc->next;
            free(wc);
            gc_unlock();
            return;
        }
        wc_pp = &(*wc_pp)->next;
    }

    gc_unlock();
}

//=============================================================================
// Cycle Detection — Trial Deletion Algorithm
//=============================================================================

/// @brief Visitor that trial-decrements child refcounts (called under gc_lock).
static void trial_decrement(void *child, void *ctx) {
    (void)ctx;
    if (!child)
        return;

    int64_t idx = find_entry(child);
    if (idx >= 0)
        g_gc.entries[idx].trial_rc--;
}

/// Lightweight snapshot entry for traversal outside the lock.
typedef struct {
    void *obj;
    rt_gc_traverse_fn traverse;
    int8_t retained;
} gc_snap_entry;

typedef struct {
    gc_snap_entry entry;
    size_t saved_refs;
    rt_heap_finalizer_t saved_finalizer;
    int snapshot_released;
    int finalized;
    int finalizer_cleared;
    int resurrected;
    int reclaimed;
} gc_garbage_state;

typedef struct {
    void **items;
    int64_t count;
    int64_t cap;
    int oom;
} gc_edge_list;

typedef struct {
    gc_snap_entry *items;
    int64_t count;
    int64_t cap;
    int oom;
} gc_worklist;

/// @brief Append @p child to a heap-resident edge list, growing the buffer on demand.
/// @details Used by the trial-deletion phase to record outgoing references collected by
///          a traversal callback. The buffer doubles each time it fills and the OOM flag
///          sticks once set so subsequent pushes are no-ops — callers detect overflow by
///          inspecting `list->oom` after the traversal completes.
/// @return 1 on successful append, 0 on OOM, NULL inputs, or NULL @p child.
static int gc_edge_list_push(gc_edge_list *list, void *child) {
    if (!list || list->oom || !child)
        return 0;
    if (list->count == list->cap) {
        if (list->cap > INT64_MAX / 2) {
            list->oom = 1;
            return 0;
        }
        int64_t new_cap = list->cap ? list->cap * 2 : 16;
        if ((uint64_t)new_cap > (uint64_t)SIZE_MAX / sizeof(void *)) {
            list->oom = 1;
            return 0;
        }
        void **items = (void **)realloc(list->items, (size_t)new_cap * sizeof(void *));
        if (!items) {
            list->oom = 1;
            return 0;
        }
        list->items = items;
        list->cap = new_cap;
    }
    list->items[list->count++] = child;
    return 1;
}

/// @brief `rt_gc_visitor_t` adapter that funnels traversed children into a `gc_edge_list`.
/// @details Bridges the per-object traverse callback (which expects a `(child, ctx)`
///          visitor) to the edge-list collector. Discards push failures since the caller
///          checks `list->oom` after the walk completes.
static void gc_edge_collector(void *child, void *ctx) {
    (void)gc_edge_list_push((gc_edge_list *)ctx, child);
}

/// @brief Push a `(obj, traverse)` pair onto the restore-phase BFS worklist.
/// @details Used by phase 3 (the "restore" pass) to enqueue objects that need to be
///          re-marked as live after the trial-deletion phase. Doubling growth and
///          sticky OOM flag mirror `gc_edge_list_push`.
/// @return 1 on success, 0 on OOM, NULL inputs, or missing traverse function.
static int gc_worklist_push(gc_worklist *work, void *obj, rt_gc_traverse_fn traverse) {
    if (!work || work->oom || !obj || !traverse)
        return 0;
    if (work->count == work->cap) {
        if (work->cap > INT64_MAX / 2) {
            work->oom = 1;
            return 0;
        }
        int64_t new_cap = work->cap ? work->cap * 2 : 16;
        if ((uint64_t)new_cap > (uint64_t)SIZE_MAX / sizeof(gc_snap_entry)) {
            work->oom = 1;
            return 0;
        }
        gc_snap_entry *items =
            (gc_snap_entry *)realloc(work->items, (size_t)new_cap * sizeof(gc_snap_entry));
        if (!items) {
            work->oom = 1;
            return 0;
        }
        work->items = items;
        work->cap = new_cap;
    }
    work->items[work->count].obj = obj;
    work->items[work->count].traverse = traverse;
    work->count++;
    return 1;
}

static int gc_garbage_contains(const gc_garbage_state *items, int64_t count, void *obj) {
    for (int64_t i = 0; i < count; ++i) {
        if (items[i].entry.obj == obj)
            return 1;
    }
    return 0;
}

static void gc_release_snapshot_entry(gc_snap_entry *entry) {
    if (!entry || !entry->retained || !entry->obj || !rt_heap_is_payload(entry->obj))
        return;
    void *obj = entry->obj;
    entry->retained = 0;
    if (rt_obj_release_check0(obj))
        rt_obj_free(obj);
}

typedef struct {
    const gc_garbage_state *garbage;
    int64_t garbage_count;
} gc_release_edges_ctx;

/// @brief Reclaim-phase visitor: drop one outgoing reference from a garbage object.
/// @details Called for each child reachable from an object being freed. Skips children
///          that are also in the garbage set — those will be freed by their own
///          reclaim-phase walk and re-releasing them here would underflow the refcount.
///          External (non-garbage) children get a normal release-and-free-on-zero.
static void gc_release_outgoing_ref(void *child, void *ctx) {
    if (!child)
        return;
    gc_release_edges_ctx *release_ctx = (gc_release_edges_ctx *)ctx;
    if (release_ctx && gc_garbage_contains(release_ctx->garbage, release_ctx->garbage_count, child))
        return;
    if (rt_obj_release_check0(child))
        rt_obj_free(child);
}

/// @brief Prepare a confirmed-unreachable cycle member for finalization.
/// @details Drops the GC snapshot retain, remembers the object's real refcount,
///          zeros the count for finalizer semantics, and runs the finalizer once.
///          The caller either restores every garbage member when any object
///          resurrects, or frees every member when no resurrection occurs.
static void gc_finalize_unreachable(gc_garbage_state *state) {
    if (!state || !state->entry.obj)
        return;
    void *obj = state->entry.obj;
    rt_heap_hdr_t *hdr = NULL;
    if (!rt_heap_try_get_header(obj, &hdr) || !hdr)
        return;

    size_t current_refs = rt_heap_release_deferred(obj);
    state->snapshot_released = 1;
    state->saved_refs = current_refs;
    if (current_refs >= RT_HEAP_IMMORTAL_REFCNT) {
        rt_trap("gc: cannot collect immortal heap object");
        return;
    }

    __atomic_store_n(&hdr->refcnt, 0, __ATOMIC_RELEASE);

    if ((rt_heap_kind_t)hdr->kind == RT_HEAP_OBJECT) {
        if (hdr->finalizer) {
            rt_heap_finalizer_t fin = hdr->finalizer;
            state->saved_finalizer = fin;
            state->finalizer_cleared = 1;
            hdr->finalizer = NULL;
            fin(obj);
        }
        state->finalized = 1;
        if (__atomic_load_n(&hdr->refcnt, __ATOMIC_ACQUIRE) != 0) {
            state->resurrected = 1;
            return;
        }
    }
}

/// @brief Restore garbage members after finalizer resurrection or trap recovery.
static void gc_restore_garbage_state(gc_garbage_state *garbage,
                                     int64_t garbage_count,
                                     int restore_finalizers) {
    if (!garbage)
        return;
    for (int64_t i = 0; i < garbage_count; ++i) {
        gc_garbage_state *state = &garbage[i];
        void *obj = state->entry.obj;
        if (!obj || state->reclaimed || !rt_heap_is_payload(obj))
            continue;
        rt_heap_hdr_t *hdr = NULL;
        if (!rt_heap_try_get_header(obj, &hdr) || !hdr)
            continue;
        if (state->snapshot_released) {
            size_t current = __atomic_load_n(&hdr->refcnt, __ATOMIC_ACQUIRE);
            size_t restored = state->saved_refs;
            if (current > 0) {
                if (restored > SIZE_MAX - current)
                    restored = SIZE_MAX;
                else
                    restored += current;
            }
            __atomic_store_n(&hdr->refcnt, restored, __ATOMIC_RELEASE);
        }
        if (restore_finalizers && state->finalizer_cleared && state->finalized &&
            !state->resurrected &&
            (rt_heap_kind_t)hdr->kind == RT_HEAP_OBJECT && !hdr->finalizer)
            hdr->finalizer = state->saved_finalizer;
        if (state->entry.traverse)
            rt_gc_track(obj, state->entry.traverse);
    }
}

/// @brief Free a finalized unreachable object after the no-resurrection decision.
static int gc_free_finalized_unreachable(gc_garbage_state *state, void *release_ctx) {
    if (!state || !state->entry.obj || state->reclaimed)
        return 0;
    void *obj = state->entry.obj;
    rt_heap_hdr_t *hdr = NULL;
    if (!rt_heap_try_get_header(obj, &hdr) || !hdr)
        return 0;

    if ((rt_heap_kind_t)hdr->kind == RT_HEAP_OBJECT) {
        if (state->entry.traverse)
            state->entry.traverse(obj, gc_release_outgoing_ref, release_ctx);
        rt_gc_clear_weak_refs(obj);
        rt_monitor_forget(obj);
    }

    rt_heap_free_zero_ref(obj);
    state->reclaimed = 1;
    return 1;
}

/// @brief Run one synchronous cycle-collection pass.
/// @details Implements a four-phase trial-deletion algorithm:
///   Phase 1: Initialize trial refcounts from the real heap-header refcount.
///   Phase 2: Trial-decrement children — objects whose trial_rc drops to 0 are
///            referenced only by other tracked objects (potential cycle members).
///   Phase 3: Restore reachable objects (trial_rc > 0) by marking them black and
///            recursively marking all their children.
///   Phase 4: Collect white objects (unreachable cycle members). Invoke finalizers
///            outside the lock and clear weak refs only for objects not resurrected.
/// @return Number of objects freed.
int64_t rt_gc_collect(void) {
    int64_t freed = 0;
    int64_t snap_count = 0;
    gc_snap_entry *snapshot = NULL;
    gc_edge_list trial_edges = {0};
    gc_worklist work = {0};
    int64_t garbage_count = 0;
    gc_garbage_state *garbage = NULL;
    jmp_buf collection_recovery;

    gc_lock();

    if (g_gc.collecting) {
        gc_unlock();
        return 0;
    }

    if (g_gc.count == 0) {
        g_gc.pass_count++;
        gc_unlock();
        return 0;
    }

    g_gc.collecting = 1;

    /* Epoch tagging: every GC_FULL_SCAN_INTERVAL passes, scan ALL objects
       regardless of promotion status to catch new cycles involving
       long-lived objects. */
    int full_scan = (g_gc.pass_count % GC_FULL_SCAN_INTERVAL == 0);

    /* Phase 1: Initialize trial refcounts and build a snapshot of all live
       entries for safe traversal outside the lock.  Promoted objects are
       included in the snapshot but marked black (skipped) unless this is
       a full scan pass. */
    if (g_gc.count < 0 || (uint64_t)g_gc.count > (uint64_t)SIZE_MAX / sizeof(gc_snap_entry)) {
        g_gc.collecting = 0;
        gc_unlock();
        rt_trap("gc: snapshot size overflow");
        return 0;
    }

    snapshot = (gc_snap_entry *)malloc((size_t)g_gc.count * sizeof(gc_snap_entry));
    if (!snapshot) {
        g_gc.collecting = 0;
        gc_unlock();
        rt_trap("gc: memory allocation failed");
        return 0;
    }

    for (int64_t i = 0; i < g_gc.capacity; i++) {
        if (!gc_slot_is_live(&g_gc.entries[i]))
            continue;

        rt_heap_hdr_t *hdr = NULL;
        if (!rt_heap_try_get_header(g_gc.entries[i].obj, &hdr) || !hdr) {
            g_gc.collecting = 0;
            gc_unlock();
            for (int64_t j = 0; j < snap_count; j++)
                gc_release_snapshot_entry(&snapshot[j]);
            free(snapshot);
            rt_trap("gc: tracked object is not a live heap payload");
            return 0;
        }
        size_t refcnt = __atomic_load_n(&hdr->refcnt, __ATOMIC_ACQUIRE);
        g_gc.entries[i].trial_rc =
            refcnt > (size_t)INT64_MAX ? INT64_MAX : (int64_t)refcnt;

        /* Skip promoted objects in non-full-scan passes: mark them black
           so they are treated as reachable without trial-deletion. */
        if (refcnt >= RT_HEAP_IMMORTAL_REFCNT)
            g_gc.entries[i].color = 2; /* black = immortal/static */
        else if (!full_scan && g_gc.entries[i].survived >= GC_PROMOTION_THRESHOLD)
            g_gc.entries[i].color = 2; /* black = skip */
        else
            g_gc.entries[i].color = 0; /* white = candidate */

        void *snapshot_obj = g_gc.entries[i].obj;
        int32_t retained = rt_heap_try_retain_live(snapshot_obj);
        if (retained <= 0) {
            g_gc.collecting = 0;
            gc_unlock();
            for (int64_t j = 0; j < snap_count; j++)
                gc_release_snapshot_entry(&snapshot[j]);
            free(snapshot);
            rt_trap(retained < 0 ? "gc: snapshot retain refcount overflow"
                                  : "gc: tracked object is not live");
            return 0;
        }

        snapshot[snap_count].obj = snapshot_obj;
        snapshot[snap_count].traverse = g_gc.entries[i].traverse;
        snapshot[snap_count].retained = retained == 1;
        snap_count++;
    }

    gc_unlock();

    rt_trap_set_recovery(&collection_recovery);
    if (setjmp(collection_recovery) != 0) {
        char saved_error[512];
        const char *err = rt_trap_get_error();
        snprintf(saved_error,
                 sizeof(saved_error),
                 "%s",
                 err && err[0] ? err : "gc: trap during collection");
        rt_trap_clear_recovery();
        free(trial_edges.items);
        free(work.items);
        gc_restore_garbage_state(garbage, garbage_count, 0);
        for (int64_t i = 0; i < snap_count; i++) {
            if (garbage && gc_garbage_contains(garbage, garbage_count, snapshot[i].obj)) {
                int snapshot_released = 0;
                for (int64_t gi = 0; gi < garbage_count; ++gi) {
                    if (garbage[gi].entry.obj == snapshot[i].obj) {
                        snapshot_released = garbage[gi].snapshot_released;
                        break;
                    }
                }
                if (snapshot_released)
                    continue;
            }
            if (garbage && gc_garbage_contains(garbage, garbage_count, snapshot[i].obj) &&
                rt_heap_is_payload(snapshot[i].obj)) {
                gc_release_snapshot_entry(&snapshot[i]);
                continue;
            }
            gc_release_snapshot_entry(&snapshot[i]);
        }
        free(garbage);
        free(snapshot);
        gc_clear_collecting_flag();
        rt_trap(saved_error);
        return 0;
    }

    /* Phase 2: Trial decrement — for each tracked object, visit its
       children.  If a child is also tracked, decrement its trial_rc.
       After this phase, objects whose trial_rc <= 0 are only referenced
       by other tracked objects (potential cycle members). */
    for (int64_t i = 0; i < snap_count; i++) {
        snapshot[i].traverse(snapshot[i].obj, gc_edge_collector, &trial_edges);
    }
    if (trial_edges.oom) {
        rt_trap("gc: memory allocation failed");
        return 0;
    }

    gc_lock();
    for (int64_t i = 0; i < trial_edges.count; ++i) {
        trial_decrement(trial_edges.items[i], NULL);
    }
    gc_unlock();
    free(trial_edges.items);
    trial_edges.items = NULL;
    trial_edges.count = 0;
    trial_edges.cap = 0;

    /* Phase 3: Scan — objects with trial_rc > 0 have external references
       and are definitely reachable.  Mark them black and recursively
       mark everything reachable from them. Traversal callbacks run outside
       gc_lock so callbacks can safely use weak/GC APIs. */
    gc_lock();
    for (int64_t i = 0; i < snap_count; i++) {
        int64_t idx = find_entry(snapshot[i].obj);
        if (idx >= 0 && g_gc.entries[idx].color == 2)
            gc_worklist_push(&work, snapshot[i].obj, snapshot[i].traverse);
    }
    for (int64_t i = 0; i < snap_count; i++) {
        int64_t idx = find_entry(snapshot[i].obj);
        if (idx >= 0 && g_gc.entries[idx].trial_rc > 0 && g_gc.entries[idx].color != 2) {
            g_gc.entries[idx].color = 2; /* black = definitely reachable */
            gc_worklist_push(&work, snapshot[i].obj, snapshot[i].traverse);
        }
    }
    gc_unlock();

    for (int64_t wi = 0; wi < work.count; ++wi) {
        gc_edge_list restore_edges = {0};
        work.items[wi].traverse(work.items[wi].obj, gc_edge_collector, &restore_edges);
        if (restore_edges.oom) {
            work.oom = 1;
            free(restore_edges.items);
            break;
        }

        gc_lock();
        for (int64_t ei = 0; ei < restore_edges.count; ++ei) {
            int64_t idx = find_entry(restore_edges.items[ei]);
            if (idx >= 0 && g_gc.entries[idx].color != 2) {
                g_gc.entries[idx].color = 2;
                gc_worklist_push(&work, g_gc.entries[idx].obj, g_gc.entries[idx].traverse);
            }
        }
        gc_unlock();
        free(restore_edges.items);
        if (work.oom)
            break;
    }

    if (work.oom) {
        rt_trap("gc: memory allocation failed");
        return 0;
    }
    free(work.items);
    work.items = NULL;
    work.count = 0;
    work.cap = 0;

    /* Phase 3b: Epoch tagging — increment survived counter for objects
       that survived this pass (color == 2, reachable). */
    gc_lock();
    for (int64_t i = 0; i < g_gc.capacity; i++) {
        if (gc_slot_is_live(&g_gc.entries[i]) && g_gc.entries[i].color == 2) {
            if (g_gc.entries[i].survived < UINT16_MAX)
                g_gc.entries[i].survived++;
        }
    }

    /* Phase 4: Collect — white objects are unreachable cycle members.
       Gather them, remove from the hash table, then finalize and free. */
    for (int64_t i = 0; i < g_gc.capacity; i++) {
        if (gc_slot_is_live(&g_gc.entries[i]) && g_gc.entries[i].color == 0)
            garbage_count++;
    }

    if (garbage_count > 0) {
        garbage = (gc_garbage_state *)calloc((size_t)garbage_count, sizeof(gc_garbage_state));
        if (!garbage) {
            gc_unlock();
            rt_trap("gc: memory allocation failed");
            return 0;
        }
        int64_t gi = 0;

        for (int64_t i = 0; i < g_gc.capacity && gi < garbage_count; i++) {
            if (gc_slot_is_live(&g_gc.entries[i]) && g_gc.entries[i].color == 0) {
                garbage[gi].entry.obj = g_gc.entries[i].obj;
                garbage[gi].entry.traverse = g_gc.entries[i].traverse;
                gi++;
            }
        }
    }

    g_gc.pass_count++;

    gc_unlock();

    /* Free garbage objects (outside the lock). */
    if (garbage) {
        gc_release_edges_ctx release_ctx = {garbage, garbage_count};
        int resurrected = 0;
        for (int64_t i = 0; i < garbage_count; i++) {
            rt_gc_untrack(garbage[i].entry.obj);
            if (rt_heap_is_payload(garbage[i].entry.obj))
                gc_finalize_unreachable(&garbage[i]);
            if (garbage[i].resurrected)
                resurrected = 1;
        }
        if (resurrected) {
            gc_restore_garbage_state(garbage, garbage_count, 1);
        } else {
            for (int64_t i = 0; i < garbage_count; i++) {
                if (gc_free_finalized_unreachable(&garbage[i], &release_ctx))
                    freed++;
            }
        }
    }

    for (int64_t i = 0; i < snap_count; i++) {
        if (garbage && gc_garbage_contains(garbage, garbage_count, snapshot[i].obj))
            continue;
        gc_release_snapshot_entry(&snapshot[i]);
    }

    free(garbage);
    garbage = NULL;
    garbage_count = 0;
    free(snapshot);
    snapshot = NULL;
    snap_count = 0;

    rt_trap_clear_recovery();

    gc_lock();
    if (freed > 0) {
        if (g_gc.total_collected > INT64_MAX - freed)
            g_gc.total_collected = INT64_MAX;
        else
            g_gc.total_collected += freed;
    }
    g_gc.collecting = 0;
    gc_unlock();

    return freed;
}

//=============================================================================
// Auto-Trigger
//=============================================================================

/// @brief Configure the auto-collection allocation threshold.
/// @details When n > 0, every n-th heap allocation triggers rt_gc_collect().
///          Set to 0 (default) to disable automatic collection.
void rt_gc_set_threshold(int64_t n) {
    __atomic_store_n(&g_gc_threshold, n > 0 ? n : 0, __ATOMIC_RELAXED);
    __atomic_store_n(&g_gc_alloc_counter, 0, __ATOMIC_RELAXED);
}

/// @brief Read the current auto-collection threshold (0 = disabled).
int64_t rt_gc_get_threshold(void) {
    return __atomic_load_n(&g_gc_threshold, __ATOMIC_RELAXED);
}

/// @brief Called by rt_heap_alloc on every allocation.
/// @details Increments an internal counter and triggers collection when the
///          counter reaches the configured threshold. Uses CAS to ensure exactly
///          one thread claims the reset (CONC-003 fix — prevents double-collect).
void rt_gc_notify_alloc(void) {
    int64_t threshold = __atomic_load_n(&g_gc_threshold, __ATOMIC_RELAXED);
    if (threshold <= 0)
        return;
    int64_t count = __atomic_fetch_add(&g_gc_alloc_counter, 1, __ATOMIC_RELAXED) + 1;
    if (count >= threshold) {
        int64_t observed = count;
        while (observed >= threshold) {
            if (gc_atomic_cas_i64(&g_gc_alloc_counter, &observed, 0)) {
                rt_gc_collect();
                return;
            }
        }
        if (observed < 0 && gc_atomic_cas_i64(&g_gc_alloc_counter, &observed, 0)) {
            rt_gc_collect();
        }
    }
}

//=============================================================================
// Statistics
//=============================================================================

/// @brief Return cumulative count of objects freed by cycle collection.
int64_t rt_gc_total_collected(void) {
    gc_lock();
    int64_t n = g_gc.total_collected;
    gc_unlock();
    return n;
}

/// @brief Return the number of collection passes run since startup.
int64_t rt_gc_pass_count(void) {
    gc_lock();
    int64_t n = g_gc.pass_count;
    gc_unlock();
    return n;
}

//=============================================================================
// Shutdown
//=============================================================================

/// @brief Iterate every tracked object, invoking its finalizer (if any) and freeing it.
/// Called at process shutdown to clean up resources that depend on side-effects (file handles,
/// network sockets) before the heap arena is released.
void rt_gc_run_all_finalizers(void) {
    typedef struct gc_shutdown_snapshot_entry {
        void *obj;
        int8_t retained;
    } gc_shutdown_snapshot_entry;

    gc_lock();

    if (g_gc.count == 0) {
        gc_unlock();
        return;
    }

    /* Snapshot all live entries so we can release the lock before running
       finalizers (same pattern as rt_gc_collect phase 4). */
    int64_t snap_count = 0;
    gc_shutdown_snapshot_entry *snapshot =
        (gc_shutdown_snapshot_entry *)malloc((size_t)g_gc.count * sizeof(*snapshot));
    if (!snapshot) {
        /* Best-effort: if malloc fails during shutdown, skip finalizer sweep.
           The OS will reclaim file descriptors and sockets on process exit. */
        gc_unlock();
        return;
    }

    for (int64_t i = 0; i < g_gc.capacity; i++) {
        if (gc_slot_is_live(&g_gc.entries[i])) {
            void *obj = g_gc.entries[i].obj;
            rt_heap_hdr_t *hdr = NULL;
            if (!rt_heap_try_get_header(obj, &hdr) || !hdr)
                continue;

            snapshot[snap_count].obj = obj;
            snapshot[snap_count].retained = 0;

            size_t refcnt = __atomic_load_n(&hdr->refcnt, __ATOMIC_ACQUIRE);
            if (refcnt > 0 && refcnt < RT_HEAP_IMMORTAL_REFCNT) {
                rt_obj_retain_maybe(obj);
                snapshot[snap_count].retained = 1;
            }
            snap_count++;
        }
    }

    gc_unlock();

    /* Run finalizers outside the lock.  We skip the refcount check that
       rt_obj_free performs because at shutdown ALL tracked objects must
       release external resources regardless of outstanding references
       (cycle members typically have refcnt > 0). */
    volatile int64_t cleanup_from = 0;
    jmp_buf finalizer_recovery;
    rt_trap_set_recovery(&finalizer_recovery);
    if (setjmp(finalizer_recovery) != 0) {
        char saved_error[512];
        const char *err = rt_trap_get_error();
        snprintf(saved_error,
                 sizeof(saved_error),
                 "%s",
                 err && err[0] ? err : "gc: trap during finalizer sweep");
        rt_trap_clear_recovery();
        for (int64_t i = (int64_t)cleanup_from; i < snap_count; ++i) {
            void *obj = snapshot[i].obj;
            if (snapshot[i].retained && obj && rt_heap_is_payload(obj)) {
                if (rt_obj_release_check0(obj))
                    rt_obj_free(obj);
            }
        }
        free(snapshot);
        rt_trap(saved_error);
        return;
    }

    for (int64_t i = 0; i < snap_count; i++) {
        cleanup_from = i;
        void *obj = snapshot[i].obj;
        rt_heap_hdr_t *hdr = NULL;
        if (!rt_heap_try_get_header(obj, &hdr) || !hdr) {
            cleanup_from = i + 1;
            continue;
        }

        if (__atomic_load_n(&hdr->refcnt, __ATOMIC_ACQUIRE) == 0) {
            rt_obj_free(obj);
            cleanup_from = i + 1;
            continue;
        }

        if (hdr && (rt_heap_kind_t)hdr->kind == RT_HEAP_OBJECT && hdr->finalizer) {
            rt_heap_finalizer_t fin = hdr->finalizer;
            hdr->finalizer = NULL; /* prevent double-finalization */
            fin(obj);
        }
        if (snapshot[i].retained && obj && rt_heap_is_payload(obj)) {
            if (rt_obj_release_check0(obj))
                rt_obj_free(obj);
            snapshot[i].retained = 0;
        }
        cleanup_from = i + 1;
    }

    rt_trap_clear_recovery();
    free(snapshot);
}

/// @brief Free all weak_chain nodes in the bucket array and the array itself.
static void free_weak_buckets(weak_chain *weak_buckets, int64_t weak_bucket_count) {
    if (!weak_buckets)
        return;
    for (int64_t i = 0; i < weak_bucket_count; i++) {
        weak_chain *wc = weak_buckets[i].next;
        while (wc) {
            weak_chain *next = wc->next;
            free(wc);
            wc = next;
        }
    }
    free(weak_buckets);
}

/// @brief Tear down GC state at process exit or embedder reset.
/// @details Detaches the tracking and weak-ref tables while holding the GC lock,
/// then frees the detached storage after unlocking. This keeps shutdown
/// idempotent and allows a later allocation/GC pass to lazily recreate the
/// tables without touching a destroyed mutex.
void rt_gc_shutdown(void) {
    gc_lock();

    gc_entry *entries = g_gc.entries;
    weak_chain *weak_buckets = g_gc.weak_buckets;
    int64_t weak_bucket_count = g_gc.weak_bucket_count;

    g_gc.entries = NULL;
    g_gc.count = 0;
    g_gc.capacity = 0;
    g_gc.weak_buckets = NULL;
    g_gc.weak_bucket_count = 0;
    g_gc.total_collected = 0;
    g_gc.pass_count = 0;
    g_gc.collecting = 0;

    /* Reset auto-trigger state. */
    __atomic_store_n(&g_gc_threshold, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&g_gc_alloc_counter, 0, __ATOMIC_RELAXED);

    gc_unlock();

    free(entries);
    free_weak_buckets(weak_buckets, weak_bucket_count);
}
