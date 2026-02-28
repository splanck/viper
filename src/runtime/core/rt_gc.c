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
//   - Weak references to collected objects are zeroed before the finalizer runs,
//     ensuring no dangling weak-ref reads after collection.
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

#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <pthread.h>
#endif

extern void rt_trap(const char *msg);

//=============================================================================
// Internal Data Structures
//=============================================================================

/// Sentinel values for the open-addressing hash table.
/// GC_EMPTY marks a slot that has never been used (terminates probe chains).
/// GC_TOMBSTONE marks a deleted slot (skipped during probing, reusable on insert).
#define GC_EMPTY NULL
#define GC_TOMBSTONE ((void *)(uintptr_t)1)

/// Entry in the tracked-object hash table.
typedef struct gc_entry
{
    void *obj; ///< Object pointer (NULL=empty, 1=tombstone, else live).
    rt_gc_traverse_fn traverse;
    int64_t trial_rc; ///< Temporary refcount for cycle detection.
    int8_t color;     ///< 0=white(unchecked), 1=gray(candidate), 2=black(reachable)
} gc_entry;

/// Weak reference record.
struct rt_weakref
{
    void *target;
    struct rt_weakref *next_for_target; ///< Chain of weak refs to same target.
};

/// Weak ref registry entry (per-target chain).
typedef struct weak_chain
{
    void *target;
    rt_weakref *head;
    struct weak_chain *next;
} weak_chain;

/// Global GC state.
static struct
{
    gc_entry *entries; ///< Open-addressing hash table (power-of-two capacity).
    int64_t count;     ///< Number of live entries (excludes tombstones).
    int64_t capacity;  ///< Table size (always a power of two, or 0).

    weak_chain *weak_buckets;
    int64_t weak_bucket_count;

    int64_t total_collected;
    int64_t pass_count;

#ifdef _WIN32
    CRITICAL_SECTION lock;
    int lock_init;
#else
    pthread_mutex_t lock;
    int lock_init;
#endif
} g_gc;

/// Auto-trigger: allocation counter and threshold.
/// When g_gc_threshold > 0, every rt_gc_notify_alloc() call increments
/// g_gc_alloc_counter; when the counter reaches the threshold, a collection
/// pass is triggered automatically.
static int64_t g_gc_threshold = 0;
static int64_t g_gc_alloc_counter = 0;

//=============================================================================
// Lock helpers
//=============================================================================

static void gc_lock_init(void)
{
    if (g_gc.lock_init)
        return;
#ifdef _WIN32
    InitializeCriticalSection(&g_gc.lock);
#else
    pthread_mutex_init(&g_gc.lock, NULL);
#endif
    g_gc.lock_init = 1;
}

static void gc_lock(void)
{
    gc_lock_init();
#ifdef _WIN32
    EnterCriticalSection(&g_gc.lock);
#else
    pthread_mutex_lock(&g_gc.lock);
#endif
}

static void gc_unlock(void)
{
#ifdef _WIN32
    LeaveCriticalSection(&g_gc.lock);
#else
    pthread_mutex_unlock(&g_gc.lock);
#endif
}

//=============================================================================
// Hash Utility
//=============================================================================

/// @brief Splitmix64-style pointer hash for hash table slot computation.
static uint64_t ptr_hash(void *p)
{
    uint64_t v = (uint64_t)(uintptr_t)p;
    v = (v ^ (v >> 30)) * 0xbf58476d1ce4e5b9ULL;
    v = (v ^ (v >> 27)) * 0x94d049bb133111ebULL;
    return v ^ (v >> 31);
}

/// @brief Check if a hash table slot contains a live (tracked) entry.
static int gc_slot_is_live(const gc_entry *e)
{
    return e->obj != GC_EMPTY && e->obj != GC_TOMBSTONE;
}

//=============================================================================
// Tracked Objects Hash Table
//=============================================================================

/// @brief Find the slot index for @p obj in the hash table.
/// @details Uses linear probing. Tombstones are skipped (do not terminate
///          the probe chain); empty slots terminate it.
/// @return Slot index if found, -1 otherwise. Caller must hold gc_lock.
static int64_t find_entry(void *obj)
{
    if (!g_gc.entries || g_gc.capacity == 0)
        return -1;
    uint64_t mask = (uint64_t)(g_gc.capacity - 1);
    uint64_t idx = ptr_hash(obj) & mask;
    for (int64_t probe = 0; probe < g_gc.capacity; probe++)
    {
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
static void gc_rehash(int64_t new_cap)
{
    gc_entry *old = g_gc.entries;
    int64_t old_cap = g_gc.capacity;

    gc_entry *new_entries = (gc_entry *)calloc((size_t)new_cap, sizeof(gc_entry));
    if (!new_entries)
        rt_trap("rt_gc: failed to grow hash table (out of memory)");

    uint64_t mask = (uint64_t)(new_cap - 1);
    int64_t live = 0;

    for (int64_t i = 0; i < old_cap; i++)
    {
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
}

void rt_gc_track(void *obj, rt_gc_traverse_fn traverse)
{
    if (!obj || !traverse)
        return;

    gc_lock();

    /* Already tracked? Update traverse function. */
    int64_t idx = find_entry(obj);
    if (idx >= 0)
    {
        g_gc.entries[idx].traverse = traverse;
        gc_unlock();
        return;
    }

    /* Grow if needed: maintain < 5/8 load factor. */
    if (g_gc.capacity == 0 || g_gc.count * 8 >= g_gc.capacity * 5)
    {
        int64_t new_cap = g_gc.capacity == 0 ? 64 : g_gc.capacity * 2;
        gc_rehash(new_cap);
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
    g_gc.count++;

    gc_unlock();
}

void rt_gc_untrack(void *obj)
{
    if (!obj)
        return;

    gc_lock();

    int64_t idx = find_entry(obj);
    if (idx >= 0)
    {
        /* Mark slot as tombstone so probe chains are preserved. */
        g_gc.entries[idx].obj = GC_TOMBSTONE;
        g_gc.entries[idx].traverse = NULL;
        g_gc.count--;
    }

    gc_unlock();
}

int8_t rt_gc_is_tracked(void *obj)
{
    if (!obj)
        return 0;

    gc_lock();
    int8_t found = find_entry(obj) >= 0 ? 1 : 0;
    gc_unlock();
    return found;
}

int64_t rt_gc_tracked_count(void)
{
    gc_lock();
    int64_t n = g_gc.count;
    gc_unlock();
    return n;
}

//=============================================================================
// Weak Reference Registry
//=============================================================================

#define WEAK_BUCKET_COUNT 64

static void ensure_weak_buckets(void)
{
    if (g_gc.weak_buckets)
        return;
    g_gc.weak_bucket_count = WEAK_BUCKET_COUNT;
    g_gc.weak_buckets = (weak_chain *)calloc((size_t)g_gc.weak_bucket_count, sizeof(weak_chain));
    if (!g_gc.weak_buckets)
        rt_trap("gc: memory allocation failed");
}

static void register_weak_ref(void *target, rt_weakref *ref)
{
    ensure_weak_buckets();
    uint64_t bucket = ptr_hash(target) % (uint64_t)g_gc.weak_bucket_count;

    /* Find existing chain for this target. */
    weak_chain *wc = g_gc.weak_buckets[bucket].next;
    while (wc)
    {
        if (wc->target == target)
        {
            ref->next_for_target = wc->head;
            wc->head = ref;
            return;
        }
        wc = wc->next;
    }

    /* Create new chain. */
    weak_chain *new_wc = (weak_chain *)malloc(sizeof(weak_chain));
    if (!new_wc)
        return;
    new_wc->target = target;
    new_wc->head = ref;
    new_wc->next = g_gc.weak_buckets[bucket].next;
    g_gc.weak_buckets[bucket].next = new_wc;
}

static void unregister_weak_ref(void *target, rt_weakref *ref)
{
    ensure_weak_buckets();
    uint64_t bucket = ptr_hash(target) % (uint64_t)g_gc.weak_bucket_count;
    weak_chain *wc = g_gc.weak_buckets[bucket].next;

    while (wc)
    {
        if (wc->target == target)
        {
            rt_weakref **pp = &wc->head;
            while (*pp)
            {
                if (*pp == ref)
                {
                    *pp = ref->next_for_target;
                    ref->next_for_target = NULL;
                    return;
                }
                pp = &(*pp)->next_for_target;
            }
            return;
        }
        wc = wc->next;
    }
}

//=============================================================================
// Zeroing Weak References (Public API)
//=============================================================================

rt_weakref *rt_weakref_new(void *target)
{
    rt_weakref *ref = (rt_weakref *)rt_obj_new_i64(0, (int64_t)sizeof(rt_weakref));
    memset(ref, 0, sizeof(rt_weakref));

    gc_lock();
    ref->target = target;
    ref->next_for_target = NULL;
    if (target)
        register_weak_ref(target, ref);
    gc_unlock();

    return ref;
}

void *rt_weakref_get(rt_weakref *ref)
{
    if (!ref)
        return NULL;

    gc_lock();
    void *t = ref->target;
    gc_unlock();
    return t;
}

int8_t rt_weakref_alive(rt_weakref *ref)
{
    if (!ref)
        return 0;

    gc_lock();
    int8_t alive = ref->target != NULL ? 1 : 0;
    gc_unlock();
    return alive;
}

void rt_weakref_free(rt_weakref *ref)
{
    if (!ref)
        return;

    gc_lock();
    if (ref->target)
        unregister_weak_ref(ref->target, ref);
    gc_unlock();
}

void rt_gc_clear_weak_refs(void *target)
{
    if (!target)
        return;

    gc_lock();
    ensure_weak_buckets();
    uint64_t bucket = ptr_hash(target) % (uint64_t)g_gc.weak_bucket_count;

    weak_chain **wc_pp = &g_gc.weak_buckets[bucket].next;
    while (*wc_pp)
    {
        weak_chain *wc = *wc_pp;
        if (wc->target == target)
        {
            /* Clear all weak refs in this chain. */
            rt_weakref *r = wc->head;
            while (r)
            {
                r->target = NULL;
                r = r->next_for_target;
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

/// Visitor that trial-decrements child refcounts.
static void trial_decrement(void *child, void *ctx)
{
    (void)ctx;
    if (!child)
        return;

    gc_lock();
    int64_t idx = find_entry(child);
    if (idx >= 0)
        g_gc.entries[idx].trial_rc--;
    gc_unlock();
}

/// Visitor that restores trial refcounts (marks reachable children).
static void trial_restore(void *child, void *ctx)
{
    (void)ctx;
    if (!child)
        return;

    gc_lock();
    int64_t idx = find_entry(child);
    if (idx >= 0 && g_gc.entries[idx].color != 2)
    {
        g_gc.entries[idx].color = 2; /* black = reachable */
        /* Recursively restore children. */
        gc_entry e = g_gc.entries[idx];
        gc_unlock();
        e.traverse(e.obj, trial_restore, NULL);
        return;
    }
    gc_unlock();
}

/// Lightweight snapshot entry for traversal outside the lock.
typedef struct
{
    void *obj;
    rt_gc_traverse_fn traverse;
} gc_snap_entry;

int64_t rt_gc_collect(void)
{
    int64_t freed = 0;

    gc_lock();

    if (g_gc.count == 0)
    {
        g_gc.pass_count++;
        gc_unlock();
        return 0;
    }

    /* Phase 1: Initialize trial refcounts and build a snapshot of all live
       entries for safe traversal outside the lock. */
    int64_t snap_count = 0;
    gc_snap_entry *snapshot = (gc_snap_entry *)malloc((size_t)g_gc.count * sizeof(gc_snap_entry));
    if (!snapshot)
        rt_trap("gc: memory allocation failed");

    for (int64_t i = 0; i < g_gc.capacity; i++)
    {
        if (!gc_slot_is_live(&g_gc.entries[i]))
            continue;
        g_gc.entries[i].trial_rc = 1; /* assume 1 external ref */
        g_gc.entries[i].color = 0;    /* white */
        snapshot[snap_count].obj = g_gc.entries[i].obj;
        snapshot[snap_count].traverse = g_gc.entries[i].traverse;
        snap_count++;
    }

    gc_unlock();

    /* Phase 2: Trial decrement — for each tracked object, visit its
       children.  If a child is also tracked, decrement its trial_rc.
       After this phase, objects whose trial_rc <= 0 are only referenced
       by other tracked objects (potential cycle members). */
    for (int64_t i = 0; i < snap_count; i++)
    {
        snapshot[i].traverse(snapshot[i].obj, trial_decrement, NULL);
    }

    /* Phase 3: Scan — objects with trial_rc > 0 have external references
       and are definitely reachable.  Mark them black and recursively
       mark everything reachable from them.  We look up each snapshot
       entry in the (possibly rehashed) live table via find_entry. */
    for (int64_t i = 0; i < snap_count; i++)
    {
        gc_lock();
        int64_t idx = find_entry(snapshot[i].obj);
        if (idx >= 0 && g_gc.entries[idx].trial_rc > 0 && g_gc.entries[idx].color != 2)
        {
            g_gc.entries[idx].color = 2; /* black = definitely reachable */
            gc_unlock();
            snapshot[i].traverse(snapshot[i].obj, trial_restore, NULL);
        }
        else
        {
            gc_unlock();
        }
    }

    free(snapshot);

    /* Phase 4: Collect — white objects are unreachable cycle members.
       Gather them, remove from the hash table, clear weak refs, then free. */
    gc_lock();

    int64_t garbage_count = 0;
    for (int64_t i = 0; i < g_gc.capacity; i++)
    {
        if (gc_slot_is_live(&g_gc.entries[i]) && g_gc.entries[i].color == 0)
            garbage_count++;
    }

    void **garbage = NULL;
    if (garbage_count > 0)
    {
        garbage = (void **)malloc((size_t)garbage_count * sizeof(void *));
        if (!garbage)
            rt_trap("gc: memory allocation failed");
        int64_t gi = 0;

        for (int64_t i = 0; i < g_gc.capacity && gi < garbage_count; i++)
        {
            if (gc_slot_is_live(&g_gc.entries[i]) && g_gc.entries[i].color == 0)
            {
                garbage[gi++] = g_gc.entries[i].obj;
                /* Tombstone the slot. */
                g_gc.entries[i].obj = GC_TOMBSTONE;
                g_gc.entries[i].traverse = NULL;
                g_gc.count--;
            }
        }
    }

    g_gc.total_collected += garbage_count;
    g_gc.pass_count++;
    freed = garbage_count;

    gc_unlock();

    /* Free garbage objects (outside the lock). */
    if (garbage)
    {
        for (int64_t i = 0; i < garbage_count; i++)
        {
            rt_gc_clear_weak_refs(garbage[i]);
            rt_obj_free(garbage[i]);
        }
        free(garbage);
    }

    return freed;
}

//=============================================================================
// Auto-Trigger
//=============================================================================

void rt_gc_set_threshold(int64_t n)
{
    __atomic_store_n(&g_gc_threshold, n > 0 ? n : 0, __ATOMIC_RELAXED);
}

int64_t rt_gc_get_threshold(void)
{
    return __atomic_load_n(&g_gc_threshold, __ATOMIC_RELAXED);
}

void rt_gc_notify_alloc(void)
{
    int64_t threshold = __atomic_load_n(&g_gc_threshold, __ATOMIC_RELAXED);
    if (threshold <= 0)
        return;
    int64_t count = __atomic_fetch_add(&g_gc_alloc_counter, 1, __ATOMIC_RELAXED) + 1;
    if (count >= threshold)
    {
        __atomic_store_n(&g_gc_alloc_counter, 0, __ATOMIC_RELAXED);
        rt_gc_collect();
    }
}

//=============================================================================
// Statistics
//=============================================================================

int64_t rt_gc_total_collected(void)
{
    gc_lock();
    int64_t n = g_gc.total_collected;
    gc_unlock();
    return n;
}

int64_t rt_gc_pass_count(void)
{
    gc_lock();
    int64_t n = g_gc.pass_count;
    gc_unlock();
    return n;
}

//=============================================================================
// Shutdown
//=============================================================================

void rt_gc_run_all_finalizers(void)
{
    gc_lock();

    if (g_gc.count == 0)
    {
        gc_unlock();
        return;
    }

    /* Snapshot all live entries so we can release the lock before running
       finalizers (same pattern as rt_gc_collect phase 4). */
    int64_t snap_count = 0;
    void **snapshot = (void **)malloc((size_t)g_gc.count * sizeof(void *));
    if (!snapshot)
    {
        /* Best-effort: if malloc fails during shutdown, skip finalizer sweep.
           The OS will reclaim file descriptors and sockets on process exit. */
        gc_unlock();
        return;
    }

    for (int64_t i = 0; i < g_gc.capacity; i++)
    {
        if (gc_slot_is_live(&g_gc.entries[i]))
            snapshot[snap_count++] = g_gc.entries[i].obj;
    }

    gc_unlock();

    /* Run finalizers outside the lock.  We skip the refcount check that
       rt_obj_free performs because at shutdown ALL tracked objects must
       release external resources regardless of outstanding references
       (cycle members typically have refcnt > 0). */
    for (int64_t i = 0; i < snap_count; i++)
    {
        rt_heap_hdr_t *hdr = rt_heap_hdr(snapshot[i]);
        if (hdr && (rt_heap_kind_t)hdr->kind == RT_HEAP_OBJECT && hdr->finalizer)
        {
            rt_heap_finalizer_t fin = hdr->finalizer;
            hdr->finalizer = NULL; /* prevent double-finalization */
            fin(snapshot[i]);
        }
    }

    free(snapshot);
}

void rt_gc_shutdown(void)
{
    gc_lock();

    /* Free tracked-object hash table. */
    free(g_gc.entries);
    g_gc.entries = NULL;
    g_gc.count = 0;
    g_gc.capacity = 0;

    /* Free weak reference bucket chains. */
    if (g_gc.weak_buckets)
    {
        for (int64_t i = 0; i < g_gc.weak_bucket_count; i++)
        {
            weak_chain *wc = g_gc.weak_buckets[i].next;
            while (wc)
            {
                weak_chain *next = wc->next;
                free(wc);
                wc = next;
            }
        }
        free(g_gc.weak_buckets);
        g_gc.weak_buckets = NULL;
        g_gc.weak_bucket_count = 0;
    }

    /* Reset auto-trigger state. */
    __atomic_store_n(&g_gc_threshold, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&g_gc_alloc_counter, 0, __ATOMIC_RELAXED);

    gc_unlock();
}
