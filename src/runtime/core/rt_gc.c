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
} g_gc;

/// GC lock — initialized statically to avoid init races (CONC-001 fix).
#ifdef _WIN32
static INIT_ONCE g_gc_lock_once = INIT_ONCE_STATIC_INIT;
static CRITICAL_SECTION g_gc_lock_cs;
#else
static pthread_mutex_t g_gc_lock_mtx = PTHREAD_MUTEX_INITIALIZER;
#endif

/// Set to 1 after rt_gc_shutdown(); prevents gc_lock() from re-initializing
/// a destroyed lock primitive (Windows: DeleteCriticalSection + InitOnce reset).
static volatile int g_gc_is_shutdown = 0;

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
static BOOL CALLBACK gc_lock_init_callback(PINIT_ONCE InitOnce, PVOID Parameter, PVOID *Context) {
    (void)InitOnce;
    (void)Parameter;
    (void)Context;
    InitializeCriticalSection(&g_gc_lock_cs);
    return TRUE;
}

static void gc_lock(void) {
    if (__atomic_load_n(&g_gc_is_shutdown, __ATOMIC_ACQUIRE))
        return; // Lock destroyed after shutdown — no-op to prevent UB
    InitOnceExecuteOnce(&g_gc_lock_once, gc_lock_init_callback, NULL, NULL);
    EnterCriticalSection(&g_gc_lock_cs);
}

static void gc_unlock(void) {
    if (__atomic_load_n(&g_gc_is_shutdown, __ATOMIC_ACQUIRE))
        return;
    LeaveCriticalSection(&g_gc_lock_cs);
}
#else
static void gc_lock(void) {
    if (__atomic_load_n(&g_gc_is_shutdown, __ATOMIC_ACQUIRE))
        return;
    pthread_mutex_lock(&g_gc_lock_mtx);
}

static void gc_unlock(void) {
    if (__atomic_load_n(&g_gc_is_shutdown, __ATOMIC_ACQUIRE))
        return;
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
static void gc_rehash(int64_t new_cap) {
    gc_entry *old = g_gc.entries;
    int64_t old_cap = g_gc.capacity;

    gc_entry *new_entries = (gc_entry *)calloc((size_t)new_cap, sizeof(gc_entry));
    if (!new_entries)
        rt_trap("rt_gc: failed to grow hash table (out of memory)");

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
}

/// @brief Register an object for cycle detection.
/// @details Inserts @p obj into the GC hash table. If already tracked, updates
///          the traverse function. The table grows when load exceeds 5/8.
void rt_gc_track(void *obj, rt_gc_traverse_fn traverse) {
    if (!obj || !traverse)
        return;

    gc_lock();

    /* Already tracked? Update traverse function. */
    int64_t idx = find_entry(obj);
    if (idx >= 0) {
        g_gc.entries[idx].traverse = traverse;
        gc_unlock();
        return;
    }

    /* Grow if needed: maintain < 5/8 load factor. */
    if (g_gc.capacity == 0 || g_gc.count * 8 >= g_gc.capacity * 5) {
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

static void ensure_weak_buckets(void) {
    if (g_gc.weak_buckets)
        return;
    g_gc.weak_bucket_count = WEAK_BUCKET_COUNT;
    g_gc.weak_buckets = (weak_chain *)calloc((size_t)g_gc.weak_bucket_count, sizeof(weak_chain));
    if (!g_gc.weak_buckets)
        rt_trap("gc: memory allocation failed");
}

static void register_weak_ref(void *target, rt_weakref *ref) {
    ensure_weak_buckets();
    uint64_t bucket = ptr_hash(target) % (uint64_t)g_gc.weak_bucket_count;

    /* Find existing chain for this target. */
    weak_chain *wc = g_gc.weak_buckets[bucket].next;
    while (wc) {
        if (wc->target == target) {
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

static void unregister_weak_ref(void *target, rt_weakref *ref) {
    ensure_weak_buckets();
    uint64_t bucket = ptr_hash(target) % (uint64_t)g_gc.weak_bucket_count;
    weak_chain *wc = g_gc.weak_buckets[bucket].next;

    while (wc) {
        if (wc->target == target) {
            rt_weakref **pp = &wc->head;
            while (*pp) {
                if (*pp == ref) {
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

/// @brief Create a zeroing weak reference. The target's refcount is NOT bumped.
/// @details When the target is freed, the reference automatically becomes NULL.
rt_weakref *rt_weakref_new(void *target) {
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

/// @brief Dereference a weak ref, returning the target or NULL if freed.
void *rt_weakref_get(rt_weakref *ref) {
    if (!ref)
        return NULL;

    gc_lock();
    void *t = ref->target;
    gc_unlock();
    return t;
}

/// @brief Check if a weak reference's target is still alive.
int8_t rt_weakref_alive(rt_weakref *ref) {
    if (!ref)
        return 0;

    gc_lock();
    int8_t alive = ref->target != NULL ? 1 : 0;
    gc_unlock();
    return alive;
}

/// @brief Destroy a weak reference handle. Does NOT affect the target.
void rt_weakref_free(rt_weakref *ref) {
    if (!ref)
        return;

    gc_lock();
    if (ref->target)
        unregister_weak_ref(ref->target, ref);
    gc_unlock();
}

/// @brief Zero all weak references pointing to a target being freed.
/// @details Called internally by rt_obj_free before deallocating. Walks the
///          per-target chain in the weak bucket and sets each ref's target to NULL.
void rt_gc_clear_weak_refs(void *target) {
    if (!target)
        return;

    gc_lock();
    ensure_weak_buckets();
    uint64_t bucket = ptr_hash(target) % (uint64_t)g_gc.weak_bucket_count;

    weak_chain **wc_pp = &g_gc.weak_buckets[bucket].next;
    while (*wc_pp) {
        weak_chain *wc = *wc_pp;
        if (wc->target == target) {
            /* Clear all weak refs in this chain. */
            rt_weakref *r = wc->head;
            while (r) {
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
/// Called while gc_lock is held for the entire phase.
static void trial_decrement(void *child, void *ctx) {
    (void)ctx;
    if (!child)
        return;

    int64_t idx = find_entry(child);
    if (idx >= 0)
        g_gc.entries[idx].trial_rc--;
}

/// Visitor that restores trial refcounts (marks reachable children).
/// Called while gc_lock is held for the entire phase.
static void trial_restore(void *child, void *ctx) {
    (void)ctx;
    if (!child)
        return;

    int64_t idx = find_entry(child);
    if (idx >= 0 && g_gc.entries[idx].color != 2) {
        g_gc.entries[idx].color = 2; /* black = reachable */
        /* Recursively restore children — lock is already held. */
        gc_entry e = g_gc.entries[idx];
        e.traverse(e.obj, trial_restore, NULL);
    }
}

/// Lightweight snapshot entry for traversal outside the lock.
typedef struct {
    void *obj;
    rt_gc_traverse_fn traverse;
} gc_snap_entry;

/// @brief Run one synchronous cycle-collection pass.
/// @details Implements a four-phase trial-deletion algorithm:
///   Phase 1: Initialize trial refcounts (assume 1 external ref per object).
///   Phase 2: Trial-decrement children — objects whose trial_rc drops to 0 are
///            referenced only by other tracked objects (potential cycle members).
///   Phase 3: Restore reachable objects (trial_rc > 0) by marking them black and
///            recursively marking all their children.
///   Phase 4: Collect white objects (unreachable cycle members). Clear their weak
///            refs and invoke finalizers outside the lock to avoid deadlock.
/// @return Number of objects freed.
int64_t rt_gc_collect(void) {
    int64_t freed = 0;

    gc_lock();

    if (g_gc.count == 0) {
        g_gc.pass_count++;
        gc_unlock();
        return 0;
    }

    /* Epoch tagging: every GC_FULL_SCAN_INTERVAL passes, scan ALL objects
       regardless of promotion status to catch new cycles involving
       long-lived objects. */
    int full_scan = (g_gc.pass_count % GC_FULL_SCAN_INTERVAL == 0);

    /* Phase 1: Initialize trial refcounts and build a snapshot of all live
       entries for safe traversal outside the lock.  Promoted objects are
       included in the snapshot but marked black (skipped) unless this is
       a full scan pass. */
    int64_t snap_count = 0;
    gc_snap_entry *snapshot = (gc_snap_entry *)malloc((size_t)g_gc.count * sizeof(gc_snap_entry));
    if (!snapshot)
        rt_trap("gc: memory allocation failed");

    for (int64_t i = 0; i < g_gc.capacity; i++) {
        if (!gc_slot_is_live(&g_gc.entries[i]))
            continue;

        g_gc.entries[i].trial_rc = 1; /* assume 1 external ref */

        /* Skip promoted objects in non-full-scan passes: mark them black
           so they are treated as reachable without trial-deletion. */
        if (!full_scan && g_gc.entries[i].survived >= GC_PROMOTION_THRESHOLD)
            g_gc.entries[i].color = 2; /* black = skip */
        else
            g_gc.entries[i].color = 0; /* white = candidate */

        snapshot[snap_count].obj = g_gc.entries[i].obj;
        snapshot[snap_count].traverse = g_gc.entries[i].traverse;
        snap_count++;
    }

    /* Lock remains held from Phase 1 into Phase 2 to prevent another
       thread from untracking + freeing an object whose pointer is in
       the snapshot (TOCTOU race). */

    /* Phase 2: Trial decrement — for each tracked object, visit its
       children.  If a child is also tracked, decrement its trial_rc.
       After this phase, objects whose trial_rc <= 0 are only referenced
       by other tracked objects (potential cycle members). */
    for (int64_t i = 0; i < snap_count; i++) {
        snapshot[i].traverse(snapshot[i].obj, trial_decrement, NULL);
    }

    /* Phase 3: Scan — objects with trial_rc > 0 have external references
       and are definitely reachable.  Mark them black and recursively
       mark everything reachable from them.  Lock remains held from Phase 2
       to avoid per-child acquire/release overhead. */
    for (int64_t i = 0; i < snap_count; i++) {
        int64_t idx = find_entry(snapshot[i].obj);
        if (idx >= 0 && g_gc.entries[idx].trial_rc > 0 && g_gc.entries[idx].color != 2) {
            g_gc.entries[idx].color = 2; /* black = definitely reachable */
            snapshot[i].traverse(snapshot[i].obj, trial_restore, NULL);
        }
    }

    /* Phase 3b: Epoch tagging — increment survived counter for objects
       that survived this pass (color == 2, reachable). */
    for (int64_t i = 0; i < g_gc.capacity; i++) {
        if (gc_slot_is_live(&g_gc.entries[i]) && g_gc.entries[i].color == 2) {
            if (g_gc.entries[i].survived < UINT16_MAX)
                g_gc.entries[i].survived++;
        }
    }

    gc_unlock();

    free(snapshot);

    /* Phase 4: Collect — white objects are unreachable cycle members.
       Gather them, remove from the hash table, clear weak refs, then free. */
    gc_lock();

    int64_t garbage_count = 0;
    for (int64_t i = 0; i < g_gc.capacity; i++) {
        if (gc_slot_is_live(&g_gc.entries[i]) && g_gc.entries[i].color == 0)
            garbage_count++;
    }

    void **garbage = NULL;
    if (garbage_count > 0) {
        garbage = (void **)malloc((size_t)garbage_count * sizeof(void *));
        if (!garbage)
            rt_trap("gc: memory allocation failed");
        int64_t gi = 0;

        for (int64_t i = 0; i < g_gc.capacity && gi < garbage_count; i++) {
            if (gc_slot_is_live(&g_gc.entries[i]) && g_gc.entries[i].color == 0) {
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
    if (garbage) {
        for (int64_t i = 0; i < garbage_count; i++) {
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

/// @brief Configure the auto-collection allocation threshold.
/// @details When n > 0, every n-th heap allocation triggers rt_gc_collect().
///          Set to 0 (default) to disable automatic collection.
void rt_gc_set_threshold(int64_t n) {
    __atomic_store_n(&g_gc_threshold, n > 0 ? n : 0, __ATOMIC_RELAXED);
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
        /* CONC-003 fix: use CAS to atomically claim the counter reset.
           Only the thread that successfully resets the counter triggers
           collection, preventing redundant double-collects. */
        int64_t expected = count;
        if (gc_atomic_cas_i64(&g_gc_alloc_counter, &expected, 0)) {
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

void rt_gc_run_all_finalizers(void) {
    gc_lock();

    if (g_gc.count == 0) {
        gc_unlock();
        return;
    }

    /* Snapshot all live entries so we can release the lock before running
       finalizers (same pattern as rt_gc_collect phase 4). */
    int64_t snap_count = 0;
    void **snapshot = (void **)malloc((size_t)g_gc.count * sizeof(void *));
    if (!snapshot) {
        /* Best-effort: if malloc fails during shutdown, skip finalizer sweep.
           The OS will reclaim file descriptors and sockets on process exit. */
        gc_unlock();
        return;
    }

    for (int64_t i = 0; i < g_gc.capacity; i++) {
        if (gc_slot_is_live(&g_gc.entries[i]))
            snapshot[snap_count++] = g_gc.entries[i].obj;
    }

    gc_unlock();

    /* Run finalizers outside the lock.  We skip the refcount check that
       rt_obj_free performs because at shutdown ALL tracked objects must
       release external resources regardless of outstanding references
       (cycle members typically have refcnt > 0). */
    for (int64_t i = 0; i < snap_count; i++) {
        rt_heap_hdr_t *hdr = rt_heap_hdr(snapshot[i]);
        if (hdr && (rt_heap_kind_t)hdr->kind == RT_HEAP_OBJECT && hdr->finalizer) {
            rt_heap_finalizer_t fin = hdr->finalizer;
            hdr->finalizer = NULL; /* prevent double-finalization */
            fin(snapshot[i]);
        }
    }

    free(snapshot);
}

void rt_gc_shutdown(void) {
    gc_lock();

    /* Free tracked-object hash table. */
    free(g_gc.entries);
    g_gc.entries = NULL;
    g_gc.count = 0;
    g_gc.capacity = 0;

    /* Free weak reference bucket chains. */
    if (g_gc.weak_buckets) {
        for (int64_t i = 0; i < g_gc.weak_bucket_count; i++) {
            weak_chain *wc = g_gc.weak_buckets[i].next;
            while (wc) {
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

    /* Mark as shut down BEFORE destroying the lock to prevent gc_lock()
       from re-initializing a destroyed primitive (e.g., from a late finalizer). */
    __atomic_store_n(&g_gc_is_shutdown, 1, __ATOMIC_RELEASE);

    /* Destroy and reinitialize lock primitive so it can be reused. */
#ifdef _WIN32
    DeleteCriticalSection(&g_gc_lock_cs);
    g_gc_lock_once = (INIT_ONCE)INIT_ONCE_STATIC_INIT;
#else
    pthread_mutex_destroy(&g_gc_lock_mtx);
    g_gc_lock_mtx = (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;
#endif
}
