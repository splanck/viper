//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/threads/rt_parallel_internal.h
// Purpose: Shared task-context types and pool/sync helpers for the data-parallel
//          combinators, split between rt_parallel.c (pool + sync + callbacks)
//          and rt_parallel_ops.c (ForEach/Map/Invoke/Reduce/For).
//
// Key invariants:
//   - Engine-internal; included only by the threads/ parallel translation units.
//   - Platform sync uses Win32 events/Interlocked on Windows, pthread mutex/cond
//     otherwise; the per-task contexts carry the matching field set.
//   - The first worker to trap captures its message into the per-batch error
//     slot; helpers here marshal that slot and the completion counters.
//
// Ownership/Lifetime:
//   - Task contexts are stack/heap owned by the submitting combinator; helpers
//     borrow them. parallel_sync blocks are heap-allocated (POSIX path).
//
// Links: src/runtime/threads/rt_parallel.c, src/runtime/threads/rt_parallel_ops.c
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "rt_platform.h"

#if RT_PLATFORM_WINDOWS
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <pthread.h>
#endif

//=============================================================================
// Per-task context types (shared by the combinators and their callbacks)
//=============================================================================

typedef struct {
    void **items;
    int64_t start;
    int64_t end;
    void (*func)(void *);
#if RT_PLATFORM_WINDOWS
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
#if RT_PLATFORM_WINDOWS
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
#if RT_PLATFORM_WINDOWS
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
#if RT_PLATFORM_WINDOWS
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
#if RT_PLATFORM_WINDOWS
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

#if !RT_PLATFORM_WINDOWS
/* Heap-allocated synchronisation state shared across all tasks in one batch. */
typedef struct {
    int remaining;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} parallel_sync;
#endif

//=============================================================================
// Shared pool / sync / error helpers (defined in rt_parallel.c)
//=============================================================================

#if RT_PLATFORM_WINDOWS
LONG *parallel_win_remaining_new(int64_t count);
void parallel_win_complete_one(LONG *remaining, HANDLE event);
void parallel_win_wait_for_completion(HANDLE event);
#endif

/// @brief Convert a function pointer to void* without pedantic warnings.
static inline void *fnptr_to_voidptr(void (*fn)(void *)) {
    void *p;
    _Static_assert(sizeof(p) == sizeof(fn),
                   "Parallel task callback bridge requires equal function/data pointer sizes");
    memcpy(&p, &fn, sizeof(p));
    return p;
}

#if !RT_PLATFORM_WINDOWS
parallel_sync *parallel_sync_new(int initial);
void parallel_sync_wait_and_free(parallel_sync *s);
void parallel_sync_destroy(parallel_sync *s);
void parallel_sync_complete(parallel_sync *s);
#endif

void parallel_release_default_pool(void *requested_pool, void *actual_pool);
int64_t parallel_pool_size(void *pool);
int64_t parallel_choose_task_count(void *pool, int64_t count);
void parallel_split_range(
    int64_t count, int64_t task_count, int64_t task_index, int64_t *start_out, int64_t *end_out);
void parallel_copy_error(char *dst, size_t dst_size, const char *fallback);
void parallel_trap_error(const char *fallback, const char *captured);
int parallel_count_fits_array(int64_t count, size_t elem_size);
int parallel_count_fits_wait_counter(int64_t count);
int parallel_is_current_pool(void *pool);
void parallel_release_map_results(void **results, void **items, int64_t count);
