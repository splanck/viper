//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/threads/rt_parallel.c
// Purpose: Implements the data-parallel combinators on `Viper.Threads.Parallel`:
//            - ForEach / ForEachPool        — apply a callback to each element.
//            - Map / MapPool                — transform each element into a result Seq.
//            - Reduce / ReducePool          — fold elements into a single accumulator.
//            - For / ForPool                — call a callback for each integer in a range.
//            - Invoke / InvokePool          — fan out a fixed list of nullary callbacks.
//          The non-`Pool` variants run on the shared default pool; the `Pool` variants
//          accept an explicit `Threadpool` handle so callers can isolate workloads.
//
// Key invariants:
//   - All combinators distribute work across N tasks chosen by `parallel_choose_task_count`
//     and wait for every task to finish before returning to the caller.
//   - Element order of processing is non-deterministic; Map / Reduce however preserve
//     positional order in their result Seq / final accumulator.
//   - The first worker callback to trap wins: its message is captured into a per-batch
//     error slot and rethrown on the submitting thread after the batch drains, instead
//     of letting the caller hang or crash mid-batch.
//   - Calls into a `*Pool` variant from a worker already running in the target pool
//     execute serially on the calling worker (`parallel_is_current_pool`) to avoid
//     self-deadlock under exhausted worker pools.
//   - Worker count defaults to the hardware thread count or the supplied pool's size.
//
// Ownership/Lifetime:
//   - Callback function pointers and primitive arguments are borrowed; callers must
//     keep them alive across the call.
//   - `Map` callback results are retained before entering the returned Seq so
//     borrowed input or shared runtime objects remain valid after the caller
//     releases its original references.
//   - `Reduce` callbacks own intermediate accumulator allocation and freeing; the
//     runtime forwards their pointer through the final combine step unchanged.
//   - Submitted callbacks run on a thread pool; no detached per-element threads
//     are created here.
//
// Links: src/runtime/threads/rt_parallel.h (public API),
//        src/runtime/threads/rt_threads.h (OS thread creation and joining),
//        src/runtime/threads/rt_threadpool.h (persistent pool alternative)
//
//===----------------------------------------------------------------------===//

#include "rt_parallel.h"
#include "rt_internal.h"
#include "rt_object.h"
#include "rt_seq.h"
#include "rt_threadpool.h"

#include <limits.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
static LONG *parallel_win_remaining_new(int64_t count) {
    LONG *remaining = (LONG *)malloc(sizeof(LONG));
    if (!remaining)
        return NULL;
    *remaining = (LONG)count;
    return remaining;
}

static void parallel_win_complete_one(LONG *remaining, HANDLE event) {
    if (remaining && event && InterlockedDecrement(remaining) == 0)
        SetEvent(event);
}
#else
#include <pthread.h>
#ifdef __APPLE__
#include <sys/types.h>
// Forward declare sysctlbyname to avoid header issues
int sysctlbyname(const char *, void *, size_t *, void *, size_t);
#else
#include <unistd.h>
#endif
#endif

/// @brief Convert a function pointer to void* without pedantic warnings.
/// POSIX guarantees function/data pointer round-trip, but ISO C forbids the cast.
static inline void *fnptr_to_voidptr(void (*fn)(void *)) {
    void *p;
    _Static_assert(sizeof(p) == sizeof(fn),
                   "Parallel task callback bridge requires equal function/data pointer sizes");
    memcpy(&p, &fn, sizeof(p));
    return p;
}

//=============================================================================
// Internal Types
//=============================================================================

// Task context for foreach
typedef struct {
    void **items;
    int64_t start;
    int64_t end;
    void (*func)(void *);
#ifdef _WIN32
    LONG *remaining;
    LONG *failed;
    HANDLE event;
    CRITICAL_SECTION *error_lock;
#else
    int *remaining;
    int *failed;
    pthread_mutex_t *mutex;
    pthread_cond_t *cond;
#endif
    char *error;
    size_t error_size;
} foreach_task;

// Task context for map
typedef struct {
    void **items;
    void **results;
    void *(*func)(void *);
    int64_t start;
    int64_t end;
#ifdef _WIN32
    LONG *remaining;
    LONG *failed;
    HANDLE event;
    CRITICAL_SECTION *error_lock;
#else
    int *remaining;
    int *failed;
    pthread_mutex_t *mutex;
    pthread_cond_t *cond;
#endif
    char *error;
    size_t error_size;
} map_task;

// Task context for invoke
typedef struct {
    void (*func)(void);
#ifdef _WIN32
    LONG *remaining;
    LONG *failed;
    HANDLE event;
    CRITICAL_SECTION *error_lock;
#else
    int *remaining;
    int *failed;
    pthread_mutex_t *mutex;
    pthread_cond_t *cond;
#endif
    char *error;
    size_t error_size;
} invoke_task;

// Task context for reduce
typedef struct {
    void **items;
    int64_t start;
    int64_t end;
    void *(*func)(void *, void *);
    void *identity;
    void *result;
#ifdef _WIN32
    LONG *remaining;
    LONG *failed;
    HANDLE event;
    CRITICAL_SECTION *error_lock;
#else
    int *remaining;
    int *failed;
    pthread_mutex_t *mutex;
    pthread_cond_t *cond;
#endif
    char *error;
    size_t error_size;
} reduce_task;

// Task context for parallel for
typedef struct {
    int64_t start;
    int64_t end;
    void (*func)(int64_t);
#ifdef _WIN32
    LONG *remaining;
    LONG *failed;
    HANDLE event;
    CRITICAL_SECTION *error_lock;
#else
    int *remaining;
    int *failed;
    pthread_mutex_t *mutex;
    pthread_cond_t *cond;
#endif
    char *error;
    size_t error_size;
} for_task;

#ifndef _WIN32
/* Heap-allocated synchronisation state shared across all tasks in one batch.
   Using heap allocation (rather than stack) eliminates any risk of
   use-after-stack-free if a future code path ever returns early.
   CONC-006 fix: volatile removed — mutex provides ordering guarantees. */
typedef struct {
    int remaining;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} parallel_sync;

/// @brief Allocate a heap-resident batch synchronization block (POSIX path).
/// @details Used to coordinate completion across N parallel tasks. The block lives on the heap
///          (not on the calling thread's stack) so that an early-return error path on the
///          submitter cannot pull the rug out from workers still racing to decrement
///          `remaining`. Returns NULL on allocation failure; the caller falls back to running
///          the work serially.
static parallel_sync *parallel_sync_new(int initial) {
    parallel_sync *s = (parallel_sync *)malloc(sizeof(parallel_sync));
    if (!s)
        return NULL;
    s->remaining = initial;
    pthread_mutex_init(&s->mutex, NULL);
    pthread_cond_init(&s->cond, NULL);
    return s;
}

/// @brief Block until every task in the batch has called `parallel_sync_complete`, then free.
/// @details Spurious wake-ups are tolerated by the `while (remaining > 0)` loop. After the last
///          task signals, the mutex/cond are destroyed and the heap block freed in one step —
///          callers must not touch the pointer after this returns.
static void parallel_sync_wait_and_free(parallel_sync *s) {
    pthread_mutex_lock(&s->mutex);
    while (s->remaining > 0)
        pthread_cond_wait(&s->cond, &s->mutex);
    pthread_mutex_unlock(&s->mutex);
    pthread_mutex_destroy(&s->mutex);
    pthread_cond_destroy(&s->cond);
    free(s);
}

/// @brief Tear down a sync block on a submission-failure path where no tasks were ever queued.
/// @details Distinct from `_wait_and_free` because there's nothing to wait for — the workers
///          that would have decremented `remaining` never started. Safe on NULL.
static void parallel_sync_destroy(parallel_sync *s) {
    if (!s)
        return;
    pthread_mutex_destroy(&s->mutex);
    pthread_cond_destroy(&s->cond);
    free(s);
}

/// @brief Mark one task complete and wake the waiter once `remaining` hits zero.
/// @details Called once per submitted task after its work runs (success or trap). The
///          decrement and the conditional signal are both performed under the mutex, so the
///          waiter can never miss the final wake-up.
static void parallel_sync_complete(parallel_sync *s) {
    pthread_mutex_lock(&s->mutex);
    s->remaining--;
    if (s->remaining == 0)
        pthread_cond_signal(&s->cond);
    pthread_mutex_unlock(&s->mutex);
}
#endif

//=============================================================================
// Default Pool (singleton)
//=============================================================================

static void *g_default_pool = NULL;
#ifdef _WIN32
static INIT_ONCE g_pool_lock_once = INIT_ONCE_STATIC_INIT;
static CRITICAL_SECTION g_pool_lock;

/// @brief One-shot initializer for the Windows default-pool critical section.
/// @details Invoked at most once per process by `InitOnceExecuteOnce`. Required because Win32
///          `CRITICAL_SECTION` has no static initializer (unlike `pthread_mutex_t`'s
///          `PTHREAD_MUTEX_INITIALIZER`), so we can't construct `g_pool_lock` at load time.
///          Returning TRUE unconditionally — `InitializeCriticalSection` raises a structured
///          exception on failure rather than returning an error code.
static BOOL CALLBACK pool_lock_init_callback(PINIT_ONCE InitOnce, PVOID Param, PVOID *Ctx) {
    (void)InitOnce;
    (void)Param;
    (void)Ctx;
    InitializeCriticalSection(&g_pool_lock);
    return TRUE;
}
#else
static pthread_mutex_t g_pool_lock = PTHREAD_MUTEX_INITIALIZER;
#endif

/// @brief Get the default number of worker threads (= number of CPU cores).
int64_t rt_parallel_default_workers(void) {
#ifdef _WIN32
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    int64_t count = si.dwNumberOfProcessors;
#elif defined(__APPLE__)
    int ncpu = 0;
    size_t len = sizeof(ncpu);
    sysctlbyname("hw.ncpu", &ncpu, &len, NULL, 0);
    int64_t count = ncpu;
#else
    int64_t count = sysconf(_SC_NPROCESSORS_ONLN);
#endif
    return count > 0 ? count : 4;
}

/// @brief Get (or lazily create) the shared default thread pool for parallel operations.
void *rt_parallel_default_pool(void) {
#ifdef _WIN32
    InitOnceExecuteOnce(&g_pool_lock_once, pool_lock_init_callback, NULL, NULL);
    EnterCriticalSection(&g_pool_lock);
#else
    pthread_mutex_lock(&g_pool_lock);
#endif

    if (!g_default_pool) {
        g_default_pool = rt_threadpool_new(rt_parallel_default_workers());
        if (g_default_pool)
            rt_obj_retain_maybe(g_default_pool); // Shared singleton reference.
    } else if (rt_threadpool_get_is_shutdown(g_default_pool)) {
        void *old_pool = g_default_pool;
        g_default_pool = rt_threadpool_new(rt_parallel_default_workers());
        if (g_default_pool)
            rt_obj_retain_maybe(g_default_pool);
        if (rt_obj_release_check0(old_pool))
            rt_obj_free(old_pool);
    }

    void *result = g_default_pool;
    if (result)
        rt_obj_retain_maybe(result);

#ifdef _WIN32
    LeaveCriticalSection(&g_pool_lock);
#else
    pthread_mutex_unlock(&g_pool_lock);
#endif

    return result;
}

/// @brief Drop the per-call retain on the default pool when the caller did not supply one.
/// @details `rt_parallel_default_pool()` returns a retained handle so the singleton can survive
///          the call. If the caller passed their own pool (`requested_pool != NULL`), we never
///          touched the singleton and there's nothing to release. If the caller relied on the
///          default (we did the lookup on their behalf), release the temporary retain — and
///          free the singleton if this happened to be the last reference (e.g. shutdown path).
static void parallel_release_default_pool(void *requested_pool, void *actual_pool) {
    if (requested_pool || !actual_pool)
        return;
    if (rt_obj_release_check0(actual_pool))
        rt_obj_free(actual_pool);
}

/// @brief Resolve the effective worker count for a parallel-for invocation.
/// @details Three-step fallback chain so we always return a usable positive
///          integer regardless of caller inputs:
///            1. If a `pool` is provided, ask it for its configured size.
///            2. If that returned zero or negative (no pool, or a mis-configured
///               pool), fall back to `rt_parallel_default_workers()` which
///               derives a platform-appropriate default (typically CPU count).
///            3. If even that fails (returning zero on a platform where we
///               can't query CPU count), return `1` so work still runs
///               serially instead of being silently skipped.
///          Used by `parallel_choose_task_count` to size the per-iteration
///          batch split.
static int64_t parallel_pool_size(void *pool) {
    int64_t size = pool ? rt_threadpool_get_size(pool) : 0;
    if (size <= 0)
        size = rt_parallel_default_workers();
    return size > 0 ? size : 1;
}

/// @brief Multiply two non-negative int64s, saturating at INT64_MAX on
///        overflow; returns 0 if either operand is <= 0. Used to size work
///        partitions without UB on extreme range/chunk counts.
static int64_t parallel_saturating_mul_i64(int64_t lhs, int64_t rhs) {
    if (lhs <= 0 || rhs <= 0)
        return 0;
    if (lhs > INT64_MAX / rhs)
        return INT64_MAX;
    return lhs * rhs;
}

/// @brief Decide how many tasks to split a `count`-element workload into.
/// @details Heuristic with three regimes:
///            - **Tiny / small (`count <= 8 * workers`):** one task per element so we don't
///              under-utilise the pool when there's little to do.
///            - **Medium / large:** target `4 * workers` tasks for good load balancing without
///              excessive scheduling overhead, then floor the chunk size at `min_chunk = 16`
///              elements so we don't pay queue cost per single iteration on huge ranges.
///            - **Degenerate (`count <= 1`):** return `count` directly so callers can skip the
///              parallel machinery entirely.
///          The `workers * 4` target gives ~4× over-subscription, which empirically smooths
///          tail latency when individual tasks have variable cost.
static int64_t parallel_choose_task_count(void *pool, int64_t count) {
    if (count <= 1)
        return count;

    int64_t workers = parallel_pool_size(pool);
    if (count <= parallel_saturating_mul_i64(workers, 8))
        return count;

    const int64_t min_chunk = 16;
    int64_t task_count = parallel_saturating_mul_i64(workers, 4);
    if (task_count < 1)
        task_count = 1;
    if (task_count > count)
        task_count = count;

    int64_t capped_by_chunk = count / min_chunk + (count % min_chunk != 0 ? 1 : 0);
    if (capped_by_chunk < 1)
        capped_by_chunk = 1;
    if (task_count > capped_by_chunk)
        task_count = capped_by_chunk;
    return task_count > 0 ? task_count : 1;
}

/// @brief Compute the [start, end) sub-range owned by a given task in an even partition.
/// @details Spreads a `remainder` of `count % task_count` extra elements across the first
///          `remainder` tasks (one extra each), so total task sizes differ by at most one.
///          Used by ForEach/Map/Reduce/For to slice the work without overlap or gaps.
///          `start_out`/`end_out` are written only when non-NULL.
static void parallel_split_range(
    int64_t count, int64_t task_count, int64_t task_index, int64_t *start_out, int64_t *end_out) {
    int64_t base = count / task_count;
    int64_t remainder = count % task_count;
    int64_t start = task_index * base + (task_index < remainder ? task_index : remainder);
    int64_t size = base + (task_index < remainder ? 1 : 0);
    if (start_out)
        *start_out = start;
    if (end_out)
        *end_out = start + size;
}

/// @brief Capture the current trap message into a per-batch error buffer if it's still empty.
/// @details Implements the "first trap wins" semantics shared by every parallel callback. The
///          `dst[0] != '\0'` guard means later traps are silently dropped — only the earliest
///          captured failure surfaces to the caller. Falls back to `fallback` (or a generic
///          "Parallel: task trapped") when `rt_trap_get_error` returned nothing useful.
static void parallel_copy_error(char *dst, size_t dst_size, const char *fallback) {
    if (!dst || dst_size == 0 || dst[0] != '\0')
        return;
    const char *msg = rt_trap_get_error();
    if (!msg || !msg[0])
        msg = fallback ? fallback : "Parallel: task trapped";
    strncpy(dst, msg, dst_size - 1);
    dst[dst_size - 1] = '\0';
}

/// @brief Re-raise the captured failure on the submitting thread after the batch drains.
/// @details Workers cannot directly trap into the caller's setjmp recovery, so they store the
///          message into a shared buffer and the submitter calls this once all tasks have
///          completed. Picks the captured message if present, otherwise the generic fallback.
static void parallel_trap_error(const char *fallback, const char *captured) {
    rt_trap((captured && captured[0]) ? captured : fallback);
}

/// @brief True if a @p count-element array of @p elem_size bytes can be
///        allocated without size_t overflow (rejects negative counts).
static int parallel_count_fits_array(int64_t count, size_t elem_size) {
    return count >= 0 && elem_size > 0 && (uint64_t)count <= (uint64_t)SIZE_MAX / elem_size;
}

/// @brief True if @p count fits a non-negative int wait-counter (the
///        cross-thread completion counter is an int; reject overflow).
static int parallel_count_fits_wait_counter(int64_t count) {
    return count >= 0 && count <= INT_MAX;
}

/// @brief Check whether a Map-callback result is one of the input pointers.
/// @details Map allows a callback to return its own input as the result (identity-style
///          pass-through). When tearing down the result array, we must not release such
///          values — they're owned by the caller's input array, not the result array.
///          Returns 1 if `value` matches any `items[i]`, else 0. Linear search is fine here:
///          this is only called on cleanup paths and Map result arrays are typically small.
/// @brief Detect whether the calling thread is already a worker of `pool`.
/// @details Used to short-circuit nested parallel calls — recursing into the same pool from
///          inside one of its worker tasks would deadlock if the pool is saturated, so the
///          caller falls back to running the work serially on the current thread.
static int parallel_is_current_pool(void *pool) {
    return pool && rt_threadpool_current_worker_pool() == pool;
}

/// @brief Drop retained values stored in a Map result array on the failure path.
/// @details Map treats callback returns as borrowed and retains every runtime
///          object before storing it in the result array, so cleanup can release
///          every populated slot uniformly.
static void parallel_release_map_results(void **results, void **items, int64_t count) {
    (void)items;
    if (!results)
        return;
    for (int64_t i = 0; i < count; i++) {
        void *value = results[i];
        results[i] = NULL;
        if (value) {
            if (rt_obj_release_check0(value))
                rt_obj_free(value);
        }
    }
}

//=============================================================================
// Task Callbacks
//=============================================================================

/// @brief Per-task entry point for `Parallel.ForEach`.
/// @details Runs `task->func(task->items[i])` over its assigned [start, end) slice with a
///          local setjmp recovery frame so a Zia-side trap doesn't crash the worker. On trap,
///          captures the message into `task->error` (first-trap-wins under the batch lock)
///          and sets `*task->failed`. Always decrements `*task->remaining` exactly once and
///          signals the wait event/condvar when it hits zero so the submitter can return.
static void foreach_callback(void *arg) {
    foreach_task *task = (foreach_task *)arg;
    int task_failed = 0;
    char local_error[512];
    local_error[0] = '\0';

    jmp_buf recovery;
    rt_trap_set_recovery(&recovery);
    if (setjmp(recovery) == 0) {
        for (int64_t i = task->start; i < task->end; i++)
            task->func(task->items[i]);
    } else {
        parallel_copy_error(local_error, sizeof(local_error), "Parallel.ForEach: task trapped");
        task_failed = 1;
    }
    rt_trap_clear_recovery();

#ifdef _WIN32
    if (task_failed) {
        EnterCriticalSection(task->error_lock);
        if (task->error && task->error_size > 0 && task->error[0] == '\0') {
            strncpy(task->error, local_error, task->error_size - 1);
            task->error[task->error_size - 1] = '\0';
        }
        LeaveCriticalSection(task->error_lock);
        InterlockedExchange(task->failed, 1);
    }
    if (InterlockedDecrement(task->remaining) == 0) {
        SetEvent(task->event);
    }
#else
    pthread_mutex_lock(task->mutex);
    if (task_failed) {
        if (task->error && task->error_size > 0 && task->error[0] == '\0') {
            strncpy(task->error, local_error, task->error_size - 1);
            task->error[task->error_size - 1] = '\0';
        }
        *task->failed = 1;
    }
    (*task->remaining)--;
    if (*task->remaining == 0) {
        pthread_cond_signal(task->cond);
    }
    pthread_mutex_unlock(task->mutex);
#endif
}

/// @brief Per-task entry point for `Parallel.Map`.
/// @details Same skeleton as `foreach_callback`, but each iteration writes
///          `task->func(items[i])` into `results[i]`. Callback returns are
///          retained before storage so borrowed results remain valid until the
///          returned sequence owns them.
///          Cleanup of partially populated retained results on a trap path is the caller's
///          responsibility (see `parallel_release_map_results`). Trap capture and
///          batch-completion signaling are identical to ForEach.
static void map_callback(void *arg) {
    map_task *task = (map_task *)arg;
    int task_failed = 0;
    char local_error[512];
    local_error[0] = '\0';

    jmp_buf recovery;
    rt_trap_set_recovery(&recovery);
    if (setjmp(recovery) == 0) {
        for (int64_t i = task->start; i < task->end; i++) {
            void *mapped = task->func(task->items[i]);
            rt_obj_retain_maybe(mapped);
            task->results[i] = mapped;
        }
    } else {
        parallel_copy_error(local_error, sizeof(local_error), "Parallel.Map: task trapped");
        task_failed = 1;
    }
    rt_trap_clear_recovery();

#ifdef _WIN32
    if (task_failed) {
        EnterCriticalSection(task->error_lock);
        if (task->error && task->error_size > 0 && task->error[0] == '\0') {
            strncpy(task->error, local_error, task->error_size - 1);
            task->error[task->error_size - 1] = '\0';
        }
        LeaveCriticalSection(task->error_lock);
        InterlockedExchange(task->failed, 1);
    }
    if (InterlockedDecrement(task->remaining) == 0) {
        SetEvent(task->event);
    }
#else
    pthread_mutex_lock(task->mutex);
    if (task_failed) {
        if (task->error && task->error_size > 0 && task->error[0] == '\0') {
            strncpy(task->error, local_error, task->error_size - 1);
            task->error[task->error_size - 1] = '\0';
        }
        *task->failed = 1;
    }
    (*task->remaining)--;
    if (*task->remaining == 0) {
        pthread_cond_signal(task->cond);
    }
    pthread_mutex_unlock(task->mutex);
#endif
}

/// @brief Per-task entry point for `Parallel.Invoke`.
/// @details Runs a single nullary `task->func()` under a setjmp recovery frame. Used to spawn
///          a fixed list of independent callbacks concurrently. Trap capture and the
///          remaining/failed/event/condvar accounting match ForEach exactly — only the inner
///          work is different.
static void invoke_callback(void *arg) {
    invoke_task *task = (invoke_task *)arg;
    int task_failed = 0;
    char local_error[512];
    local_error[0] = '\0';

    jmp_buf recovery;
    rt_trap_set_recovery(&recovery);
    if (setjmp(recovery) == 0)
        task->func();
    else {
        parallel_copy_error(local_error, sizeof(local_error), "Parallel.Invoke: task trapped");
        task_failed = 1;
    }
    rt_trap_clear_recovery();

#ifdef _WIN32
    if (task_failed) {
        EnterCriticalSection(task->error_lock);
        if (task->error && task->error_size > 0 && task->error[0] == '\0') {
            strncpy(task->error, local_error, task->error_size - 1);
            task->error[task->error_size - 1] = '\0';
        }
        LeaveCriticalSection(task->error_lock);
        InterlockedExchange(task->failed, 1);
    }
    if (InterlockedDecrement(task->remaining) == 0) {
        SetEvent(task->event);
    }
#else
    pthread_mutex_lock(task->mutex);
    if (task_failed) {
        if (task->error && task->error_size > 0 && task->error[0] == '\0') {
            strncpy(task->error, local_error, task->error_size - 1);
            task->error[task->error_size - 1] = '\0';
        }
        *task->failed = 1;
    }
    (*task->remaining)--;
    if (*task->remaining == 0) {
        pthread_cond_signal(task->cond);
    }
    pthread_mutex_unlock(task->mutex);
#endif
}

/// @brief Per-task entry point for `Parallel.Reduce` (per-task partial fold).
/// @details Folds the [start, end) slice with `task->func(accum, items[i])`. An empty slice
///          stores the user-supplied identity into `task->result` so the caller's final
///          combine step has a neutral value to absorb. Trap capture and batch signaling
///          mirror ForEach. The cross-slice combine that turns these partials into the
///          single overall result happens on the submitting thread, not here. Accumulator
///          ownership is the reducer's contract: the runtime forwards partial/final pointers
///          exactly as produced and does not retain or release intermediate accumulators.
static void reduce_callback(void *arg) {
    reduce_task *task = (reduce_task *)arg;
    int task_failed = 0;
    char local_error[512];
    local_error[0] = '\0';

    jmp_buf recovery;
    rt_trap_set_recovery(&recovery);
    if (setjmp(recovery) == 0) {
        if (task->start >= task->end) {
            task->result = task->identity;
        } else {
            void *accum = task->items[task->start];
            for (int64_t i = task->start + 1; i < task->end; i++) {
                accum = task->func(accum, task->items[i]);
            }
            task->result = accum;
        }
    } else {
        parallel_copy_error(local_error, sizeof(local_error), "Parallel.Reduce: task trapped");
        task_failed = 1;
    }
    rt_trap_clear_recovery();

#ifdef _WIN32
    if (task_failed) {
        EnterCriticalSection(task->error_lock);
        if (task->error && task->error_size > 0 && task->error[0] == '\0') {
            strncpy(task->error, local_error, task->error_size - 1);
            task->error[task->error_size - 1] = '\0';
        }
        LeaveCriticalSection(task->error_lock);
        InterlockedExchange(task->failed, 1);
    }
    if (InterlockedDecrement(task->remaining) == 0) {
        SetEvent(task->event);
    }
#else
    pthread_mutex_lock(task->mutex);
    if (task_failed) {
        if (task->error && task->error_size > 0 && task->error[0] == '\0') {
            strncpy(task->error, local_error, task->error_size - 1);
            task->error[task->error_size - 1] = '\0';
        }
        *task->failed = 1;
    }
    (*task->remaining)--;
    if (*task->remaining == 0) {
        pthread_cond_signal(task->cond);
    }
    pthread_mutex_unlock(task->mutex);
#endif
}

/// @brief Per-task entry point for `Parallel.For` (numeric range iteration).
/// @details Calls `task->func(i)` for each integer `i` in [start, end). Differs from
///          ForEach only in that the callback receives an `int64_t` index rather than an
///          element pointer — there is no input array to dereference. Trap capture and
///          batch-completion bookkeeping are identical to the other callbacks.
static void for_callback(void *arg) {
    for_task *task = (for_task *)arg;
    int task_failed = 0;
    char local_error[512];
    local_error[0] = '\0';

    jmp_buf recovery;
    rt_trap_set_recovery(&recovery);
    if (setjmp(recovery) == 0) {
        for (int64_t i = task->start; i < task->end; i++)
            task->func(i);
    } else {
        parallel_copy_error(local_error, sizeof(local_error), "Parallel.For: task trapped");
        task_failed = 1;
    }
    rt_trap_clear_recovery();

#ifdef _WIN32
    if (task_failed) {
        EnterCriticalSection(task->error_lock);
        if (task->error && task->error_size > 0 && task->error[0] == '\0') {
            strncpy(task->error, local_error, task->error_size - 1);
            task->error[task->error_size - 1] = '\0';
        }
        LeaveCriticalSection(task->error_lock);
        InterlockedExchange(task->failed, 1);
    }
    if (InterlockedDecrement(task->remaining) == 0) {
        SetEvent(task->event);
    }
#else
    pthread_mutex_lock(task->mutex);
    if (task_failed) {
        if (task->error && task->error_size > 0 && task->error[0] == '\0') {
            strncpy(task->error, local_error, task->error_size - 1);
            task->error[task->error_size - 1] = '\0';
        }
        *task->failed = 1;
    }
    (*task->remaining)--;
    if (*task->remaining == 0) {
        pthread_cond_signal(task->cond);
    }
    pthread_mutex_unlock(task->mutex);
#endif
}

//=============================================================================
// Parallel ForEach
//=============================================================================

/// @brief Apply a function to each element of a sequence in parallel using the given pool.
void rt_parallel_foreach_pool(void *seq, void *func, void *pool) {
    if (!seq || !func)
        return;

    int64_t count = rt_seq_len(seq);
    if (count < 0) {
        rt_trap("Parallel.ForEach: negative sequence length");
        return;
    }
    if (count == 0)
        return;

    void *actual_pool = pool ? pool : rt_parallel_default_pool();
    int64_t task_count = parallel_choose_task_count(actual_pool, count);
    if (parallel_is_current_pool(actual_pool)) {
        char task_error[512];
        task_error[0] = '\0';
        jmp_buf recovery;
        rt_trap_set_recovery(&recovery);
        if (setjmp(recovery) == 0) {
            void (*worker)(void *) = (void (*)(void *))func;
            for (int64_t i = 0; i < count; i++)
                worker(rt_seq_get(seq, i));
        } else {
            parallel_copy_error(task_error, sizeof(task_error), "Parallel.ForEach: task trapped");
        }
        rt_trap_clear_recovery();
        parallel_release_default_pool(pool, actual_pool);
        if (task_error[0])
            parallel_trap_error("Parallel.ForEach: task trapped", task_error);
        return;
    }

    if (!parallel_count_fits_array(count, sizeof(void *))) {
        parallel_release_default_pool(pool, actual_pool);
        rt_trap("Parallel.ForEach: allocation size overflow");
        return;
    }
    void **items = (void **)malloc((size_t)count * sizeof(void *));
    if (!items) {
        parallel_release_default_pool(pool, actual_pool);
        rt_trap("Parallel.ForEach: memory allocation failed");
        return;
    }
    for (int64_t i = 0; i < count; i++)
        items[i] = rt_seq_get(seq, i);

    if (!parallel_count_fits_wait_counter(task_count)) {
        free(items);
        parallel_release_default_pool(pool, actual_pool);
        rt_trap("Parallel.ForEach: allocation size overflow");
        return;
    }

#ifdef _WIN32
    LONG *remaining = parallel_win_remaining_new(task_count);
    if (!remaining) {
        free(items);
        parallel_release_default_pool(pool, actual_pool);
        rt_trap("Parallel.ForEach: memory allocation failed");
        return;
    }
    LONG task_failed = 0;
    HANDLE event = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!event) {
        free(remaining);
        free(items);
        parallel_release_default_pool(pool, actual_pool);
        rt_trap("Parallel.ForEach: event creation failed");
        return;
    }
    CRITICAL_SECTION error_lock;
    InitializeCriticalSection(&error_lock);
#else
    int task_failed = 0;
    parallel_sync *sync = parallel_sync_new((int)task_count);
    if (!sync) {
        free(items);
        parallel_release_default_pool(pool, actual_pool);
        rt_trap("Parallel.ForEach: memory allocation failed");
        return;
    }
#endif
    char task_error[512];
    task_error[0] = '\0';

    // Allocate task array
    if (!parallel_count_fits_array(task_count, sizeof(foreach_task))) {
#ifdef _WIN32
        CloseHandle(event);
        DeleteCriticalSection(&error_lock);
        free(remaining);
#else
        parallel_sync_destroy(sync);
#endif
        free(items);
        parallel_release_default_pool(pool, actual_pool);
        rt_trap("Parallel.ForEach: allocation size overflow");
        return;
    }
    foreach_task *tasks = (foreach_task *)malloc((size_t)task_count * sizeof(foreach_task));
    if (!tasks) {
#ifdef _WIN32
        CloseHandle(event);
        DeleteCriticalSection(&error_lock);
        free(remaining);
#else
        parallel_sync_destroy(sync);
#endif
        free(items);
        parallel_release_default_pool(pool, actual_pool);
        rt_trap("Parallel.ForEach: memory allocation failed");
        return;
    }

    int submit_failed = 0;

    // Submit all tasks
    for (int64_t i = 0; i < task_count; i++) {
        tasks[i].items = items;
        parallel_split_range(count, task_count, i, &tasks[i].start, &tasks[i].end);
        tasks[i].func = (void (*)(void *))func;
#ifdef _WIN32
        tasks[i].remaining = remaining;
        tasks[i].failed = &task_failed;
        tasks[i].event = event;
        tasks[i].error_lock = &error_lock;
#else
        tasks[i].remaining = &sync->remaining;
        tasks[i].failed = &task_failed;
        tasks[i].mutex = &sync->mutex;
        tasks[i].cond = &sync->cond;
#endif
        tasks[i].error = task_error;
        tasks[i].error_size = sizeof(task_error);
        if (!rt_threadpool_submit(actual_pool, fnptr_to_voidptr(foreach_callback), &tasks[i])) {
            submit_failed = 1;
#ifdef _WIN32
            parallel_win_complete_one(remaining, event);
#else
            parallel_sync_complete(sync);
#endif
        }
    }

    // Wait for completion
#ifdef _WIN32
    WaitForSingleObject(event, INFINITE);
    CloseHandle(event);
    DeleteCriticalSection(&error_lock);
    free(remaining);
#else
    parallel_sync_wait_and_free(sync);
#endif

    free(tasks);
    free(items);

    parallel_release_default_pool(pool, actual_pool);
    if (task_failed)
        parallel_trap_error("Parallel.ForEach: task trapped", task_error);
    if (submit_failed)
        rt_trap("Parallel.ForEach: failed to submit work");
}

/// @brief Apply a function to each element of a sequence in parallel (uses default pool).
void rt_parallel_foreach(void *seq, void *func) {
    rt_parallel_foreach_pool(seq, func, NULL);
}

//=============================================================================
// Parallel Map
//=============================================================================

/// @brief Transform each element in parallel and return a new sequence of results.
void *rt_parallel_map_pool(void *seq, void *func, void *pool) {
    if (!seq || !func)
        return rt_seq_new();

    int64_t count = rt_seq_len(seq);
    if (count < 0) {
        rt_trap("Parallel.Map: negative sequence length");
        return rt_seq_new();
    }
    if (count == 0)
        return rt_seq_new();

    void *actual_pool = pool ? pool : rt_parallel_default_pool();
    int64_t task_count = parallel_choose_task_count(actual_pool, count);
    if (parallel_is_current_pool(actual_pool)) {
        void *result = rt_seq_new();
        if (!result) {
            parallel_release_default_pool(pool, actual_pool);
            return NULL;
        }
        rt_seq_set_owns_elements(result, 1);
        char task_error[512];
        task_error[0] = '\0';
        jmp_buf recovery;
        rt_trap_set_recovery(&recovery);
        if (setjmp(recovery) == 0) {
            void *(*mapper)(void *) = (void *(*)(void *))func;
            for (int64_t i = 0; i < count; i++) {
                void *mapped = mapper(rt_seq_get(seq, i));
                rt_seq_push(result, mapped);
            }
        } else {
            parallel_copy_error(task_error, sizeof(task_error), "Parallel.Map: task trapped");
        }
        rt_trap_clear_recovery();
        parallel_release_default_pool(pool, actual_pool);
        if (task_error[0]) {
            if (rt_obj_release_check0(result))
                rt_obj_free(result);
            parallel_trap_error("Parallel.Map: task trapped", task_error);
            return NULL;
        }
        return result;
    }

    if (!parallel_count_fits_array(count, sizeof(void *))) {
        parallel_release_default_pool(pool, actual_pool);
        rt_trap("Parallel.Map: allocation size overflow");
        return NULL;
    }
    void **items = (void **)malloc((size_t)count * sizeof(void *));
    void **results = (void **)calloc((size_t)count, sizeof(void *));
    if (!items || !results) {
        free(items);
        free(results);
        parallel_release_default_pool(pool, actual_pool);
        rt_trap("Parallel.Map: memory allocation failed");
        return NULL;
    }
    for (int64_t i = 0; i < count; i++)
        items[i] = rt_seq_get(seq, i);

    if (!parallel_count_fits_wait_counter(task_count)) {
        free(items);
        free(results);
        parallel_release_default_pool(pool, actual_pool);
        rt_trap("Parallel.Map: allocation size overflow");
        return NULL;
    }

#ifdef _WIN32
    LONG *remaining = parallel_win_remaining_new(task_count);
    if (!remaining) {
        free(items);
        free(results);
        parallel_release_default_pool(pool, actual_pool);
        rt_trap("Parallel.Map: memory allocation failed");
        return NULL;
    }
    LONG task_failed = 0;
    HANDLE event = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!event) {
        free(remaining);
        free(items);
        free(results);
        parallel_release_default_pool(pool, actual_pool);
        rt_trap("Parallel.Map: event creation failed");
        return NULL;
    }
    CRITICAL_SECTION error_lock;
    InitializeCriticalSection(&error_lock);
#else
    int task_failed = 0;
    parallel_sync *sync = parallel_sync_new((int)task_count);
    if (!sync) {
        free(items);
        free(results);
        parallel_release_default_pool(pool, actual_pool);
        rt_trap("Parallel.Map: memory allocation failed");
        return NULL;
    }
#endif
    char task_error[512];
    task_error[0] = '\0';

    // Allocate task array
    if (!parallel_count_fits_array(task_count, sizeof(map_task))) {
#ifdef _WIN32
        CloseHandle(event);
        DeleteCriticalSection(&error_lock);
        free(remaining);
#else
        parallel_sync_destroy(sync);
#endif
        free(items);
        free(results);
        parallel_release_default_pool(pool, actual_pool);
        rt_trap("Parallel.Map: allocation size overflow");
        return NULL;
    }
    map_task *tasks = (map_task *)malloc((size_t)task_count * sizeof(map_task));
    if (!tasks) {
#ifdef _WIN32
        CloseHandle(event);
        DeleteCriticalSection(&error_lock);
        free(remaining);
#else
        parallel_sync_destroy(sync);
#endif
        free(items);
        free(results);
        parallel_release_default_pool(pool, actual_pool);
        rt_trap("Parallel.Map: memory allocation failed");
        return NULL;
    }

    int submit_failed = 0;

    // Submit all tasks
    for (int64_t i = 0; i < task_count; i++) {
        tasks[i].items = items;
        tasks[i].results = results;
        tasks[i].func = (void *(*)(void *))func;
        parallel_split_range(count, task_count, i, &tasks[i].start, &tasks[i].end);
#ifdef _WIN32
        tasks[i].remaining = remaining;
        tasks[i].failed = &task_failed;
        tasks[i].event = event;
        tasks[i].error_lock = &error_lock;
#else
        tasks[i].remaining = &sync->remaining;
        tasks[i].failed = &task_failed;
        tasks[i].mutex = &sync->mutex;
        tasks[i].cond = &sync->cond;
#endif
        tasks[i].error = task_error;
        tasks[i].error_size = sizeof(task_error);
        if (!rt_threadpool_submit(actual_pool, fnptr_to_voidptr(map_callback), &tasks[i])) {
            submit_failed = 1;
#ifdef _WIN32
            parallel_win_complete_one(remaining, event);
#else
            parallel_sync_complete(sync);
#endif
        }
    }

    // Wait for completion
#ifdef _WIN32
    WaitForSingleObject(event, INFINITE);
    CloseHandle(event);
    DeleteCriticalSection(&error_lock);
    free(remaining);
#else
    parallel_sync_wait_and_free(sync);
#endif

    if (task_failed || submit_failed) {
        parallel_release_map_results(results, items, count);
        free(tasks);
        free(items);
        free(results);
        parallel_release_default_pool(pool, actual_pool);
        if (task_failed)
            parallel_trap_error("Parallel.Map: task trapped", task_error);
        rt_trap("Parallel.Map: failed to submit work");
        return NULL;
    }

    // Collect results in order
    void *result = rt_seq_new();
    if (!result) {
        parallel_release_map_results(results, items, count);
        free(tasks);
        free(items);
        free(results);
        parallel_release_default_pool(pool, actual_pool);
        return NULL;
    }
    rt_seq_set_owns_elements(result, 1);
    for (int64_t i = 0; i < count; i++) {
        rt_seq_push_raw(result, results[i]);
        results[i] = NULL;
    }

    free(tasks);
    free(items);
    free(results);
    parallel_release_default_pool(pool, actual_pool);
    return result;
}

/// @brief Transform each element in parallel (uses default pool).
void *rt_parallel_map(void *seq, void *func) {
    return rt_parallel_map_pool(seq, func, NULL);
}

//=============================================================================
// Parallel Invoke
//=============================================================================

/// @brief Execute a list of functions in parallel and wait for all to complete.
void rt_parallel_invoke_pool(void *funcs, void *pool) {
    if (!funcs)
        return;

    int64_t count = rt_seq_len(funcs);
    if (count < 0) {
        rt_trap("Parallel.Invoke: negative sequence length");
        return;
    }
    if (count == 0)
        return;

    void *actual_pool = pool ? pool : rt_parallel_default_pool();
    if (parallel_is_current_pool(actual_pool)) {
        char task_error[512];
        task_error[0] = '\0';
        jmp_buf recovery;
        rt_trap_set_recovery(&recovery);
        if (setjmp(recovery) == 0) {
            for (int64_t i = 0; i < count; i++) {
                void (*worker)(void) = (void (*)(void))rt_seq_get(funcs, i);
                worker();
            }
        } else {
            parallel_copy_error(task_error, sizeof(task_error), "Parallel.Invoke: task trapped");
        }
        rt_trap_clear_recovery();
        parallel_release_default_pool(pool, actual_pool);
        if (task_error[0])
            parallel_trap_error("Parallel.Invoke: task trapped", task_error);
        return;
    }

    if (!parallel_count_fits_wait_counter(count)) {
        parallel_release_default_pool(pool, actual_pool);
        rt_trap("Parallel.Invoke: allocation size overflow");
        return;
    }

#ifdef _WIN32
    LONG *remaining = parallel_win_remaining_new(count);
    if (!remaining) {
        parallel_release_default_pool(pool, actual_pool);
        rt_trap("Parallel.Invoke: memory allocation failed");
        return;
    }
    LONG task_failed = 0;
    HANDLE event = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!event) {
        free(remaining);
        parallel_release_default_pool(pool, actual_pool);
        rt_trap("Parallel.Invoke: event creation failed");
        return;
    }
    CRITICAL_SECTION error_lock;
    InitializeCriticalSection(&error_lock);
#else
    int task_failed = 0;
    parallel_sync *sync = parallel_sync_new((int)count);
    if (!sync) {
        parallel_release_default_pool(pool, actual_pool);
        rt_trap("Parallel.Invoke: memory allocation failed");
        return;
    }
#endif
    char task_error[512];
    task_error[0] = '\0';

    // Allocate task array
    if (!parallel_count_fits_array(count, sizeof(invoke_task))) {
#ifdef _WIN32
        CloseHandle(event);
        DeleteCriticalSection(&error_lock);
        free(remaining);
#else
        parallel_sync_destroy(sync);
#endif
        parallel_release_default_pool(pool, actual_pool);
        rt_trap("Parallel.Invoke: allocation size overflow");
        return;
    }
    invoke_task *tasks = (invoke_task *)malloc((size_t)count * sizeof(invoke_task));
    if (!tasks) {
#ifdef _WIN32
        CloseHandle(event);
        DeleteCriticalSection(&error_lock);
        free(remaining);
#else
        parallel_sync_destroy(sync);
#endif
        parallel_release_default_pool(pool, actual_pool);
        rt_trap("Parallel.Invoke: memory allocation failed");
        return;
    }

    int submit_failed = 0;

    // Submit all tasks
    for (int64_t i = 0; i < count; i++) {
        tasks[i].func = (void (*)(void))rt_seq_get(funcs, i);
#ifdef _WIN32
        tasks[i].remaining = remaining;
        tasks[i].failed = &task_failed;
        tasks[i].event = event;
        tasks[i].error_lock = &error_lock;
#else
        tasks[i].remaining = &sync->remaining;
        tasks[i].failed = &task_failed;
        tasks[i].mutex = &sync->mutex;
        tasks[i].cond = &sync->cond;
#endif
        tasks[i].error = task_error;
        tasks[i].error_size = sizeof(task_error);
        if (!rt_threadpool_submit(actual_pool, fnptr_to_voidptr(invoke_callback), &tasks[i])) {
            submit_failed = 1;
#ifdef _WIN32
            parallel_win_complete_one(remaining, event);
#else
            parallel_sync_complete(sync);
#endif
        }
    }

    // Wait for completion
#ifdef _WIN32
    WaitForSingleObject(event, INFINITE);
    CloseHandle(event);
    DeleteCriticalSection(&error_lock);
    free(remaining);
#else
    parallel_sync_wait_and_free(sync);
#endif

    free(tasks);

    parallel_release_default_pool(pool, actual_pool);
    if (task_failed)
        parallel_trap_error("Parallel.Invoke: task trapped", task_error);
    if (submit_failed)
        rt_trap("Parallel.Invoke: failed to submit work");
}

/// @brief Execute a list of functions in parallel (uses default pool).
void rt_parallel_invoke(void *funcs) {
    rt_parallel_invoke_pool(funcs, NULL);
}

//=============================================================================
// Parallel Reduce
//=============================================================================

/// @brief Reduce a sequence in parallel using an associative binary function and identity value.
void *rt_parallel_reduce_pool(void *seq, void *func, void *identity, void *pool) {
    if (!seq || !func)
        return identity;

    int64_t count = rt_seq_len(seq);
    if (count < 0) {
        rt_trap("Parallel.Reduce: negative sequence length");
        return identity;
    }
    if (count == 0)
        return identity;

    /* For small sequences, reduce serially. */
    void *(*combine)(void *, void *) = (void *(*)(void *, void *))func;
    if (count <= 4) {
        void *accum = identity;
        for (int64_t i = 0; i < count; i++) {
            accum = combine(accum, rt_seq_get(seq, i));
        }
        return accum;
    }

    void *actual_pool = pool ? pool : rt_parallel_default_pool();
    if (parallel_is_current_pool(actual_pool)) {
        void *result = identity;
        char task_error[512];
        task_error[0] = '\0';
        jmp_buf recovery;
        rt_trap_set_recovery(&recovery);
        if (setjmp(recovery) == 0) {
            for (int64_t i = 0; i < count; i++)
                result = combine(result, rt_seq_get(seq, i));
        } else {
            parallel_copy_error(task_error, sizeof(task_error), "Parallel.Reduce: reducer trapped");
        }
        rt_trap_clear_recovery();
        parallel_release_default_pool(pool, actual_pool);
        if (task_error[0])
            parallel_trap_error("Parallel.Reduce: reducer trapped", task_error);
        if (task_error[0])
            return identity;
        return result;
    }

    int64_t nworkers = parallel_pool_size(actual_pool);
    if (nworkers > count)
        nworkers = count;
    if (!parallel_count_fits_wait_counter(nworkers)) {
        parallel_release_default_pool(pool, actual_pool);
        rt_trap("Parallel.Reduce: allocation size overflow");
        return identity;
    }

    /* Extract items array for chunk access. */
    if (!parallel_count_fits_array(count, sizeof(void *))) {
        parallel_release_default_pool(pool, actual_pool);
        rt_trap("Parallel.Reduce: allocation size overflow");
        return identity;
    }
    void **items = (void **)malloc((size_t)count * sizeof(void *));
    if (!items) {
        parallel_release_default_pool(pool, actual_pool);
        rt_trap("Parallel.Reduce: memory allocation failed");
        return identity;
    }
    for (int64_t i = 0; i < count; i++) {
        items[i] = rt_seq_get(seq, i);
    }

#ifdef _WIN32
    LONG *remaining = parallel_win_remaining_new(nworkers);
    if (!remaining) {
        free(items);
        parallel_release_default_pool(pool, actual_pool);
        rt_trap("Parallel.Reduce: memory allocation failed");
        return identity;
    }
    LONG task_failed = 0;
    HANDLE event = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!event) {
        free(remaining);
        free(items);
        parallel_release_default_pool(pool, actual_pool);
        rt_trap("Parallel.Reduce: event creation failed");
        return identity;
    }
    CRITICAL_SECTION error_lock;
    InitializeCriticalSection(&error_lock);
#else
    int task_failed = 0;
    parallel_sync *sync = parallel_sync_new((int)nworkers);
    if (!sync) {
        free(items);
        parallel_release_default_pool(pool, actual_pool);
        rt_trap("Parallel.Reduce: memory allocation failed");
        return identity;
    }
#endif
    char task_error[512];
    task_error[0] = '\0';

    if (!parallel_count_fits_array(nworkers, sizeof(reduce_task))) {
#ifdef _WIN32
        CloseHandle(event);
        DeleteCriticalSection(&error_lock);
        free(remaining);
#else
        parallel_sync_destroy(sync);
#endif
        free(items);
        parallel_release_default_pool(pool, actual_pool);
        rt_trap("Parallel.Reduce: allocation size overflow");
        return identity;
    }
    reduce_task *tasks = (reduce_task *)malloc((size_t)nworkers * sizeof(reduce_task));
    if (!tasks) {
#ifdef _WIN32
        CloseHandle(event);
        DeleteCriticalSection(&error_lock);
        free(remaining);
#else
        parallel_sync_destroy(sync);
#endif
        free(items);
        parallel_release_default_pool(pool, actual_pool);
        rt_trap("Parallel.Reduce: memory allocation failed");
        return identity;
    }

    int64_t chunk = count / nworkers;
    int64_t remainder = count % nworkers;
    int64_t offset = 0;
    int submit_failed = 0;

    for (int64_t i = 0; i < nworkers; i++) {
        int64_t chunk_size = chunk + (i < remainder ? 1 : 0);
        tasks[i].items = items;
        tasks[i].start = offset;
        tasks[i].end = offset + chunk_size;
        tasks[i].func = combine;
        tasks[i].identity = identity;
        tasks[i].result = identity;
#ifdef _WIN32
        tasks[i].remaining = remaining;
        tasks[i].failed = &task_failed;
        tasks[i].event = event;
        tasks[i].error_lock = &error_lock;
#else
        tasks[i].remaining = &sync->remaining;
        tasks[i].failed = &task_failed;
        tasks[i].mutex = &sync->mutex;
        tasks[i].cond = &sync->cond;
#endif
        tasks[i].error = task_error;
        tasks[i].error_size = sizeof(task_error);
        if (!rt_threadpool_submit(actual_pool, fnptr_to_voidptr(reduce_callback), &tasks[i])) {
            submit_failed = 1;
#ifdef _WIN32
            parallel_win_complete_one(remaining, event);
#else
            parallel_sync_complete(sync);
#endif
        }
        offset += chunk_size;
    }

    /* Wait for completion. */
#ifdef _WIN32
    WaitForSingleObject(event, INFINITE);
    CloseHandle(event);
    DeleteCriticalSection(&error_lock);
    free(remaining);
#else
    parallel_sync_wait_and_free(sync);
#endif

    /* Apply the identity exactly once on the caller thread. */
    void *result = identity;
    int combine_failed = 0;
    char combine_error[512];
    combine_error[0] = '\0';
    if (!task_failed && !submit_failed) {
        jmp_buf recovery;
        rt_trap_set_recovery(&recovery);
        if (setjmp(recovery) == 0) {
            for (int64_t i = 0; i < nworkers; i++) {
                result = combine(result, tasks[i].result);
            }
        } else {
            parallel_copy_error(
                combine_error, sizeof(combine_error), "Parallel.Reduce: reducer trapped");
            combine_failed = 1;
        }
        rt_trap_clear_recovery();
    }

    free(tasks);
    free(items);
    parallel_release_default_pool(pool, actual_pool);
    if (combine_failed) {
        parallel_trap_error("Parallel.Reduce: reducer trapped", combine_error);
        return identity;
    }
    if (task_failed) {
        parallel_trap_error("Parallel.Reduce: task trapped", task_error);
        return identity;
    }
    if (submit_failed) {
        rt_trap("Parallel.Reduce: failed to submit work");
        return identity;
    }
    return result;
}

/// @brief Reduce a sequence in parallel (uses default pool).
void *rt_parallel_reduce(void *seq, void *func, void *identity) {
    return rt_parallel_reduce_pool(seq, func, identity, NULL);
}

//=============================================================================
// Parallel For
//=============================================================================

/// @brief Execute func(i) for each i in [start, end) in parallel, splitting work across threads.
void rt_parallel_for_pool(int64_t start, int64_t end, void *func, void *pool) {
    if (!func || start >= end)
        return;

    uint64_t count_u = (uint64_t)end - (uint64_t)start;
    if (count_u > (uint64_t)INT64_MAX) {
        rt_trap("Parallel.For: range too large");
        return;
    }
    int64_t count = (int64_t)count_u;
    void *actual_pool = pool ? pool : rt_parallel_default_pool();
    int64_t task_count = parallel_choose_task_count(actual_pool, count);
    if (parallel_is_current_pool(actual_pool)) {
        char task_error[512];
        task_error[0] = '\0';
        jmp_buf recovery;
        rt_trap_set_recovery(&recovery);
        if (setjmp(recovery) == 0) {
            void (*worker)(int64_t) = (void (*)(int64_t))func;
            for (int64_t i = start; i < end; i++)
                worker(i);
        } else {
            parallel_copy_error(task_error, sizeof(task_error), "Parallel.For: task trapped");
        }
        rt_trap_clear_recovery();
        parallel_release_default_pool(pool, actual_pool);
        if (task_error[0])
            parallel_trap_error("Parallel.For: task trapped", task_error);
        return;
    }

    if (!parallel_count_fits_wait_counter(task_count)) {
        parallel_release_default_pool(pool, actual_pool);
        rt_trap("Parallel.For: allocation size overflow");
        return;
    }

#ifdef _WIN32
    LONG *remaining = parallel_win_remaining_new(task_count);
    if (!remaining) {
        parallel_release_default_pool(pool, actual_pool);
        rt_trap("Parallel.For: memory allocation failed");
        return;
    }
    LONG task_failed = 0;
    HANDLE event = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!event) {
        free(remaining);
        parallel_release_default_pool(pool, actual_pool);
        rt_trap("Parallel.For: event creation failed");
        return;
    }
    CRITICAL_SECTION error_lock;
    InitializeCriticalSection(&error_lock);
#else
    int task_failed = 0;
    parallel_sync *sync = parallel_sync_new((int)task_count);
    if (!sync) {
        parallel_release_default_pool(pool, actual_pool);
        rt_trap("Parallel.For: memory allocation failed");
        return;
    }
#endif
    char task_error[512];
    task_error[0] = '\0';

    // Allocate task array
    if (!parallel_count_fits_array(task_count, sizeof(for_task))) {
#ifdef _WIN32
        CloseHandle(event);
        DeleteCriticalSection(&error_lock);
        free(remaining);
#else
        parallel_sync_destroy(sync);
#endif
        parallel_release_default_pool(pool, actual_pool);
        rt_trap("Parallel.For: allocation size overflow");
        return;
    }
    for_task *tasks = (for_task *)malloc((size_t)task_count * sizeof(for_task));
    if (!tasks) {
#ifdef _WIN32
        CloseHandle(event);
        DeleteCriticalSection(&error_lock);
        free(remaining);
#else
        parallel_sync_destroy(sync);
#endif
        parallel_release_default_pool(pool, actual_pool);
        rt_trap("Parallel.For: memory allocation failed");
        return;
    }

    int submit_failed = 0;

    // Submit all tasks
    for (int64_t i = 0; i < task_count; i++) {
        int64_t local_start = 0;
        int64_t local_end = 0;
        parallel_split_range(count, task_count, i, &local_start, &local_end);
        tasks[i].start = start + local_start;
        tasks[i].end = start + local_end;
        tasks[i].func = (void (*)(int64_t))func;
#ifdef _WIN32
        tasks[i].remaining = remaining;
        tasks[i].failed = &task_failed;
        tasks[i].event = event;
        tasks[i].error_lock = &error_lock;
#else
        tasks[i].remaining = &sync->remaining;
        tasks[i].failed = &task_failed;
        tasks[i].mutex = &sync->mutex;
        tasks[i].cond = &sync->cond;
#endif
        tasks[i].error = task_error;
        tasks[i].error_size = sizeof(task_error);
        if (!rt_threadpool_submit(actual_pool, fnptr_to_voidptr(for_callback), &tasks[i])) {
            submit_failed = 1;
#ifdef _WIN32
            parallel_win_complete_one(remaining, event);
#else
            parallel_sync_complete(sync);
#endif
        }
    }

    // Wait for completion
#ifdef _WIN32
    WaitForSingleObject(event, INFINITE);
    CloseHandle(event);
    DeleteCriticalSection(&error_lock);
    free(remaining);
#else
    parallel_sync_wait_and_free(sync);
#endif

    free(tasks);

    parallel_release_default_pool(pool, actual_pool);
    if (task_failed)
        parallel_trap_error("Parallel.For: task trapped", task_error);
    if (submit_failed)
        rt_trap("Parallel.For: failed to submit work");
}

/// @brief Execute func(i) for each i in [start, end) in parallel (uses default pool).
void rt_parallel_for(int64_t start, int64_t end, void *func) {
    rt_parallel_for_pool(start, end, func, NULL);
}
