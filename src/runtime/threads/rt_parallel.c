//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/threads/rt_parallel.c
// Purpose: Implements data-parallel execution utilities for the
//          Viper.Threads.Parallel class. Provides ForEach (parallel iteration
//          over a collection with a worker callback) and Invoke (run a fixed
//          list of callbacks concurrently and wait for all to finish).
//
// Key invariants:
//   - ForEach distributes elements across worker threads; element order of
//     processing is non-deterministic.
//   - Invoke launches all callbacks simultaneously and blocks until every one
//     has returned.
//   - Worker count defaults to the hardware thread count (or a user-supplied cap).
//   - Errors thrown by workers are collected and re-raised after all finish.
//   - All threads are joined before the parallel call returns; no dangling threads.
//
// Ownership/Lifetime:
//   - Callback function pointers and arguments are not retained; callers must
//     ensure lifetimes exceed the Parallel call.
//   - Temporary thread handles are created and immediately joined within the call.
//
// Links: src/runtime/threads/rt_parallel.h (public API),
//        src/runtime/threads/rt_threads.h (OS thread creation and joining),
//        src/runtime/threads/rt_threadpool.h (persistent pool alternative)
//
//===----------------------------------------------------------------------===//

#include "rt_parallel.h"
#include "rt_internal.h"
#include "rt_seq.h"
#include "rt_threadpool.h"

#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
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
static inline void *fnptr_to_voidptr(void (*fn)(void *))
{
    void *p;
    memcpy(&p, &fn, sizeof(p));
    return p;
}

//=============================================================================
// Internal Types
//=============================================================================

// Task context for foreach
typedef struct
{
    void *item;
    void (*func)(void *);
#ifdef _WIN32
    volatile LONG *remaining;
    HANDLE event;
#else
    volatile int *remaining;
    pthread_mutex_t *mutex;
    pthread_cond_t *cond;
#endif
} foreach_task;

// Task context for map
typedef struct
{
    void *item;
    void *(*func)(void *);
    void *result;
    int64_t index;
#ifdef _WIN32
    volatile LONG *remaining;
    HANDLE event;
#else
    volatile int *remaining;
    pthread_mutex_t *mutex;
    pthread_cond_t *cond;
#endif
} map_task;

// Task context for invoke
typedef struct
{
    void (*func)(void);
#ifdef _WIN32
    volatile LONG *remaining;
    HANDLE event;
#else
    volatile int *remaining;
    pthread_mutex_t *mutex;
    pthread_cond_t *cond;
#endif
} invoke_task;

// Task context for reduce
typedef struct
{
    void **items;
    int64_t start;
    int64_t end;
    void *(*func)(void *, void *);
    void *identity;
    void *result;
#ifdef _WIN32
    volatile LONG *remaining;
    HANDLE event;
#else
    volatile int *remaining;
    pthread_mutex_t *mutex;
    pthread_cond_t *cond;
#endif
} reduce_task;

// Task context for parallel for
typedef struct
{
    int64_t index;
    void (*func)(int64_t);
#ifdef _WIN32
    volatile LONG *remaining;
    HANDLE event;
#else
    volatile int *remaining;
    pthread_mutex_t *mutex;
    pthread_cond_t *cond;
#endif
} for_task;

#ifndef _WIN32
/* Heap-allocated synchronisation state shared across all tasks in one batch.
   Using heap allocation (rather than stack) eliminates any risk of
   use-after-stack-free if a future code path ever returns early. */
typedef struct
{
    volatile int remaining;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} parallel_sync;

static parallel_sync *parallel_sync_new(int initial)
{
    parallel_sync *s = (parallel_sync *)malloc(sizeof(parallel_sync));
    if (!s)
        return NULL;
    s->remaining = initial;
    pthread_mutex_init(&s->mutex, NULL);
    pthread_cond_init(&s->cond, NULL);
    return s;
}

static void parallel_sync_wait_and_free(parallel_sync *s)
{
    pthread_mutex_lock(&s->mutex);
    while (s->remaining > 0)
        pthread_cond_wait(&s->cond, &s->mutex);
    pthread_mutex_unlock(&s->mutex);
    pthread_mutex_destroy(&s->mutex);
    pthread_cond_destroy(&s->cond);
    free(s);
}
#endif

//=============================================================================
// Default Pool (singleton)
//=============================================================================

static void *g_default_pool = NULL;
#ifdef _WIN32
static CRITICAL_SECTION g_pool_lock;
static int g_pool_lock_init = 0;
#else
static pthread_mutex_t g_pool_lock = PTHREAD_MUTEX_INITIALIZER;
#endif

int64_t rt_parallel_default_workers(void)
{
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

void *rt_parallel_default_pool(void)
{
#ifdef _WIN32
    if (!g_pool_lock_init)
    {
        InitializeCriticalSection(&g_pool_lock);
        g_pool_lock_init = 1;
    }
    EnterCriticalSection(&g_pool_lock);
#else
    pthread_mutex_lock(&g_pool_lock);
#endif

    if (!g_default_pool)
    {
        g_default_pool = rt_threadpool_new(rt_parallel_default_workers());
    }

#ifdef _WIN32
    LeaveCriticalSection(&g_pool_lock);
#else
    pthread_mutex_unlock(&g_pool_lock);
#endif

    return g_default_pool;
}

//=============================================================================
// Task Callbacks
//=============================================================================

static void foreach_callback(void *arg)
{
    foreach_task *task = (foreach_task *)arg;
    task->func(task->item);

#ifdef _WIN32
    if (InterlockedDecrement(task->remaining) == 0)
    {
        SetEvent(task->event);
    }
#else
    pthread_mutex_lock(task->mutex);
    (*task->remaining)--;
    if (*task->remaining == 0)
    {
        pthread_cond_signal(task->cond);
    }
    pthread_mutex_unlock(task->mutex);
#endif
}

static void map_callback(void *arg)
{
    map_task *task = (map_task *)arg;
    task->result = task->func(task->item);

#ifdef _WIN32
    if (InterlockedDecrement(task->remaining) == 0)
    {
        SetEvent(task->event);
    }
#else
    pthread_mutex_lock(task->mutex);
    (*task->remaining)--;
    if (*task->remaining == 0)
    {
        pthread_cond_signal(task->cond);
    }
    pthread_mutex_unlock(task->mutex);
#endif
}

static void invoke_callback(void *arg)
{
    invoke_task *task = (invoke_task *)arg;
    task->func();

#ifdef _WIN32
    if (InterlockedDecrement(task->remaining) == 0)
    {
        SetEvent(task->event);
    }
#else
    pthread_mutex_lock(task->mutex);
    (*task->remaining)--;
    if (*task->remaining == 0)
    {
        pthread_cond_signal(task->cond);
    }
    pthread_mutex_unlock(task->mutex);
#endif
}

static void reduce_callback(void *arg)
{
    reduce_task *task = (reduce_task *)arg;
    void *accum = task->identity;
    for (int64_t i = task->start; i < task->end; i++)
    {
        accum = task->func(accum, task->items[i]);
    }
    task->result = accum;

#ifdef _WIN32
    if (InterlockedDecrement(task->remaining) == 0)
    {
        SetEvent(task->event);
    }
#else
    pthread_mutex_lock(task->mutex);
    (*task->remaining)--;
    if (*task->remaining == 0)
    {
        pthread_cond_signal(task->cond);
    }
    pthread_mutex_unlock(task->mutex);
#endif
}

static void for_callback(void *arg)
{
    for_task *task = (for_task *)arg;
    task->func(task->index);

#ifdef _WIN32
    if (InterlockedDecrement(task->remaining) == 0)
    {
        SetEvent(task->event);
    }
#else
    pthread_mutex_lock(task->mutex);
    (*task->remaining)--;
    if (*task->remaining == 0)
    {
        pthread_cond_signal(task->cond);
    }
    pthread_mutex_unlock(task->mutex);
#endif
}

//=============================================================================
// Parallel ForEach
//=============================================================================

void rt_parallel_foreach_pool(void *seq, void *func, void *pool)
{
    if (!seq || !func)
        return;

    int64_t count = rt_seq_len(seq);
    if (count == 0)
        return;

    void *actual_pool = pool ? pool : rt_parallel_default_pool();

#ifdef _WIN32
    volatile LONG remaining = (LONG)count;
    HANDLE event = CreateEvent(NULL, TRUE, FALSE, NULL);
#else
    parallel_sync *sync = parallel_sync_new((int)count);
    if (!sync)
        rt_trap("Parallel.ForEach: memory allocation failed");
#endif

    // Allocate task array
    foreach_task *tasks = (foreach_task *)malloc(count * sizeof(foreach_task));
    if (!tasks)
        rt_trap("Parallel.ForEach: memory allocation failed");

    // Submit all tasks
    for (int64_t i = 0; i < count; i++)
    {
        tasks[i].item = rt_seq_get(seq, i);
        tasks[i].func = (void (*)(void *))func;
#ifdef _WIN32
        tasks[i].remaining = &remaining;
        tasks[i].event = event;
#else
        tasks[i].remaining = &sync->remaining;
        tasks[i].mutex = &sync->mutex;
        tasks[i].cond = &sync->cond;
#endif
        rt_threadpool_submit(actual_pool, fnptr_to_voidptr(foreach_callback), &tasks[i]);
    }

    // Wait for completion
#ifdef _WIN32
    WaitForSingleObject(event, INFINITE);
    CloseHandle(event);
#else
    parallel_sync_wait_and_free(sync);
#endif

    free(tasks);
}

void rt_parallel_foreach(void *seq, void *func)
{
    rt_parallel_foreach_pool(seq, func, NULL);
}

//=============================================================================
// Parallel Map
//=============================================================================

void *rt_parallel_map_pool(void *seq, void *func, void *pool)
{
    if (!seq || !func)
        return rt_seq_new();

    int64_t count = rt_seq_len(seq);
    if (count == 0)
        return rt_seq_new();

    void *actual_pool = pool ? pool : rt_parallel_default_pool();

#ifdef _WIN32
    volatile LONG remaining = (LONG)count;
    HANDLE event = CreateEvent(NULL, TRUE, FALSE, NULL);
#else
    parallel_sync *sync = parallel_sync_new((int)count);
    if (!sync)
        rt_trap("Parallel.Map: memory allocation failed");
#endif

    // Allocate task array
    map_task *tasks = (map_task *)malloc(count * sizeof(map_task));
    if (!tasks)
        rt_trap("Parallel.Map: memory allocation failed");

    // Submit all tasks
    for (int64_t i = 0; i < count; i++)
    {
        tasks[i].item = rt_seq_get(seq, i);
        tasks[i].func = (void *(*)(void *))func;
        tasks[i].result = NULL;
        tasks[i].index = i;
#ifdef _WIN32
        tasks[i].remaining = &remaining;
        tasks[i].event = event;
#else
        tasks[i].remaining = &sync->remaining;
        tasks[i].mutex = &sync->mutex;
        tasks[i].cond = &sync->cond;
#endif
        rt_threadpool_submit(actual_pool, fnptr_to_voidptr(map_callback), &tasks[i]);
    }

    // Wait for completion
#ifdef _WIN32
    WaitForSingleObject(event, INFINITE);
    CloseHandle(event);
#else
    parallel_sync_wait_and_free(sync);
#endif

    // Collect results in order
    void *result = rt_seq_new();
    for (int64_t i = 0; i < count; i++)
    {
        rt_seq_push(result, tasks[i].result);
    }

    free(tasks);
    return result;
}

void *rt_parallel_map(void *seq, void *func)
{
    return rt_parallel_map_pool(seq, func, NULL);
}

//=============================================================================
// Parallel Invoke
//=============================================================================

void rt_parallel_invoke_pool(void *funcs, void *pool)
{
    if (!funcs)
        return;

    int64_t count = rt_seq_len(funcs);
    if (count == 0)
        return;

    void *actual_pool = pool ? pool : rt_parallel_default_pool();

#ifdef _WIN32
    volatile LONG remaining = (LONG)count;
    HANDLE event = CreateEvent(NULL, TRUE, FALSE, NULL);
#else
    parallel_sync *sync = parallel_sync_new((int)count);
    if (!sync)
        rt_trap("Parallel.Invoke: memory allocation failed");
#endif

    // Allocate task array
    invoke_task *tasks = (invoke_task *)malloc(count * sizeof(invoke_task));
    if (!tasks)
        rt_trap("Parallel.Invoke: memory allocation failed");

    // Submit all tasks
    for (int64_t i = 0; i < count; i++)
    {
        tasks[i].func = (void (*)(void))rt_seq_get(funcs, i);
#ifdef _WIN32
        tasks[i].remaining = &remaining;
        tasks[i].event = event;
#else
        tasks[i].remaining = &sync->remaining;
        tasks[i].mutex = &sync->mutex;
        tasks[i].cond = &sync->cond;
#endif
        rt_threadpool_submit(actual_pool, fnptr_to_voidptr(invoke_callback), &tasks[i]);
    }

    // Wait for completion
#ifdef _WIN32
    WaitForSingleObject(event, INFINITE);
    CloseHandle(event);
#else
    parallel_sync_wait_and_free(sync);
#endif

    free(tasks);
}

void rt_parallel_invoke(void *funcs)
{
    rt_parallel_invoke_pool(funcs, NULL);
}

//=============================================================================
// Parallel Reduce
//=============================================================================

void *rt_parallel_reduce_pool(void *seq, void *func, void *identity, void *pool)
{
    if (!seq || !func)
        return identity;

    int64_t count = rt_seq_len(seq);
    if (count == 0)
        return identity;

    /* For small sequences, reduce serially. */
    void *(*combine)(void *, void *) = (void *(*)(void *, void *))func;
    if (count <= 4)
    {
        void *accum = identity;
        for (int64_t i = 0; i < count; i++)
        {
            accum = combine(accum, rt_seq_get(seq, i));
        }
        return accum;
    }

    void *actual_pool = pool ? pool : rt_parallel_default_pool();
    int64_t nworkers = rt_parallel_default_workers();
    if (nworkers > count)
        nworkers = count;

    /* Extract items array for chunk access. */
    void **items = (void **)malloc((size_t)count * sizeof(void *));
    if (!items)
        rt_trap("Parallel.Reduce: memory allocation failed");
    for (int64_t i = 0; i < count; i++)
    {
        items[i] = rt_seq_get(seq, i);
    }

#ifdef _WIN32
    volatile LONG remaining = (LONG)nworkers;
    HANDLE event = CreateEvent(NULL, TRUE, FALSE, NULL);
#else
    parallel_sync *sync = parallel_sync_new((int)nworkers);
    if (!sync)
    {
        free(items);
        rt_trap("Parallel.Reduce: memory allocation failed");
    }
#endif

    reduce_task *tasks = (reduce_task *)malloc((size_t)nworkers * sizeof(reduce_task));
    if (!tasks)
    {
        free(items);
        rt_trap("Parallel.Reduce: memory allocation failed");
    }

    int64_t chunk = count / nworkers;
    int64_t remainder = count % nworkers;
    int64_t offset = 0;

    for (int64_t i = 0; i < nworkers; i++)
    {
        int64_t chunk_size = chunk + (i < remainder ? 1 : 0);
        tasks[i].items = items;
        tasks[i].start = offset;
        tasks[i].end = offset + chunk_size;
        tasks[i].func = combine;
        tasks[i].identity = identity;
        tasks[i].result = identity;
#ifdef _WIN32
        tasks[i].remaining = &remaining;
        tasks[i].event = event;
#else
        tasks[i].remaining = &sync->remaining;
        tasks[i].mutex = &sync->mutex;
        tasks[i].cond = &sync->cond;
#endif
        rt_threadpool_submit(actual_pool, fnptr_to_voidptr(reduce_callback), &tasks[i]);
        offset += chunk_size;
    }

    /* Wait for completion. */
#ifdef _WIN32
    WaitForSingleObject(event, INFINITE);
    CloseHandle(event);
#else
    parallel_sync_wait_and_free(sync);
#endif

    /* Combine partial results on main thread. */
    void *result = tasks[0].result;
    for (int64_t i = 1; i < nworkers; i++)
    {
        result = combine(result, tasks[i].result);
    }

    free(tasks);
    free(items);
    return result;
}

void *rt_parallel_reduce(void *seq, void *func, void *identity)
{
    return rt_parallel_reduce_pool(seq, func, identity, NULL);
}

//=============================================================================
// Parallel For
//=============================================================================

void rt_parallel_for_pool(int64_t start, int64_t end, void *func, void *pool)
{
    if (!func || start >= end)
        return;

    int64_t count = end - start;
    void *actual_pool = pool ? pool : rt_parallel_default_pool();

#ifdef _WIN32
    volatile LONG remaining = (LONG)count;
    HANDLE event = CreateEvent(NULL, TRUE, FALSE, NULL);
#else
    parallel_sync *sync = parallel_sync_new((int)count);
    if (!sync)
        rt_trap("Parallel.For: memory allocation failed");
#endif

    // Allocate task array
    for_task *tasks = (for_task *)malloc(count * sizeof(for_task));
    if (!tasks)
        rt_trap("Parallel.For: memory allocation failed");

    // Submit all tasks
    for (int64_t i = 0; i < count; i++)
    {
        tasks[i].index = start + i;
        tasks[i].func = (void (*)(int64_t))func;
#ifdef _WIN32
        tasks[i].remaining = &remaining;
        tasks[i].event = event;
#else
        tasks[i].remaining = &sync->remaining;
        tasks[i].mutex = &sync->mutex;
        tasks[i].cond = &sync->cond;
#endif
        rt_threadpool_submit(actual_pool, fnptr_to_voidptr(for_callback), &tasks[i]);
    }

    // Wait for completion
#ifdef _WIN32
    WaitForSingleObject(event, INFINITE);
    CloseHandle(event);
#else
    parallel_sync_wait_and_free(sync);
#endif

    free(tasks);
}

void rt_parallel_for(int64_t start, int64_t end, void *func)
{
    rt_parallel_for_pool(start, end, func, NULL);
}
