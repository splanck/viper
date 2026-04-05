//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/threads/rt_threadpool.c
// Purpose: Implements a fixed-size thread pool for the Viper.Threads.Pool class.
//          Worker threads dequeue tasks from a FIFO linked-list queue protected
//          by a monitor. Supports Submit, Wait (drain all pending tasks), and
//          Shutdown.
//
// Key invariants:
//   - Worker count is fixed at construction; it cannot change after creation.
//   - Submit enqueues a (callback, arg) pair; workers dequeue and execute in FIFO.
//   - Wait blocks until all submitted tasks have completed execution.
//   - Shutdown signals workers to exit after draining the queue, then joins them.
//   - Submitting to a shut-down pool traps immediately.
//   - All queue and state access is serialized through the monitor.
//
// Ownership/Lifetime:
//   - Task arguments (void*) are passed through without retain/release; callers
//     must ensure argument lifetimes exceed task execution.
//   - Worker thread handles are owned by the pool and joined on Shutdown.
//   - The pool itself is heap-allocated and managed by the runtime GC.
//
// Links: src/runtime/threads/rt_threadpool.h (public API),
//        src/runtime/threads/rt_threads.h (OS thread creation and joining),
//        src/runtime/threads/rt_monitor.h (synchronization for task queue)
//
//===----------------------------------------------------------------------===//

#include "rt_threadpool.h"

#include "rt_gc.h"
#include "rt_object.h"
#include "rt_threads.h"

#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <time.h>
#endif

//=============================================================================
// Internal Structures
//=============================================================================

/// @brief Task entry in the queue.
typedef struct pool_task {
    void (*callback)(void *); ///< Task function pointer.
    void *arg;                ///< Argument for the callback.
    struct pool_task *next;   ///< Next task in queue.
} pool_task;

/// @brief Worker thread state.
typedef struct pool_worker {
    void *thread;           ///< Thread handle.
    struct pool_impl *pool; ///< Back-reference to pool.
} pool_worker;

/// @brief Thread pool implementation.
typedef struct pool_impl {
    void *monitor;         ///< Monitor for synchronization.
    pool_task *queue_head; ///< Head of task queue.
    pool_task *queue_tail; ///< Tail of task queue.
    pool_worker *workers;  ///< Array of workers.
    int64_t worker_count;  ///< Number of workers.
    int64_t pending_count; ///< Number of tasks in queue.
    int64_t active_count;  ///< Number of tasks running.
    int8_t shutdown;       ///< Shutdown flag.
    int8_t shutdown_now;   ///< Immediate shutdown flag.
} pool_impl;

//=============================================================================
// Forward Declarations
//=============================================================================

static void pool_finalizer(void *obj);
static void worker_entry(void *arg);

#include "rt_trap.h"

//=============================================================================
// Pool Management
//=============================================================================

/// @brief GC traverse callback for thread pools.
/// @details Pools hold no strong references that participate in reference
///          cycles, but we register them for GC tracking so the shutdown
///          finalizer sweep (rt_gc_run_all_finalizers) can reach them.
static void pool_traverse(void *obj, rt_gc_visitor_t visitor, void *ctx) {
    (void)obj;
    (void)visitor;
    (void)ctx;
}

static void pool_finalizer(void *obj) {
    pool_impl *pool = (pool_impl *)obj;
    if (!pool)
        return;

    rt_gc_untrack(pool);

    /* If not already shut down, signal workers and join them.
       This handles the case where a program exits without calling
       rt_threadpool_shutdown() — the atexit finalizer sweep invokes
       this finalizer to prevent abandoned worker threads. */
    if (!pool->shutdown && pool->monitor) {
        pool->shutdown = 1;
        pool->shutdown_now = 1;
        rt_monitor_enter(pool->monitor);
        rt_monitor_pause_all(pool->monitor);
        rt_monitor_exit(pool->monitor);

        for (int64_t i = 0; i < pool->worker_count; i++) {
            if (pool->workers && pool->workers[i].thread) {
                rt_thread_join(pool->workers[i].thread);
                pool->workers[i].thread = NULL;
            }
        }
    }

    // Clean up any remaining queue
    pool_task *task = pool->queue_head;
    while (task) {
        pool_task *next = task->next;
        free(task);
        task = next;
    }

    // Free workers array
    if (pool->workers)
        free(pool->workers);

    // Release monitor
    if (pool->monitor) {
        if (rt_obj_release_check0(pool->monitor))
            rt_obj_free(pool->monitor);
    }
}

/// @brief Create a thread pool with the given number of worker threads (1–1024).
/// @details Workers block on a shared work queue until tasks are submitted via
///          submit(). Tasks execute in FIFO order. The pool must be shut down
///          explicitly with shutdown() or shutdown_now().
void *rt_threadpool_new(int64_t size) {
    // Clamp size to valid range
    if (size < 1)
        size = 1;
    if (size > 1024)
        size = 1024;

    pool_impl *pool = (pool_impl *)rt_obj_new_i64(0, sizeof(pool_impl));
    if (!pool)
        return NULL;

    rt_obj_set_finalizer(pool, pool_finalizer);
    rt_gc_track(pool, pool_traverse);

    pool->monitor = rt_obj_new_i64(0, 1); // Create a monitor object
    if (!pool->monitor) {
        rt_obj_free(pool);
        return NULL;
    }

    pool->queue_head = NULL;
    pool->queue_tail = NULL;
    pool->pending_count = 0;
    pool->active_count = 0;
    pool->shutdown = 0;
    pool->shutdown_now = 0;
    pool->worker_count = size;

    // Allocate workers array
    pool->workers = (pool_worker *)calloc((size_t)size, sizeof(pool_worker));
    if (!pool->workers) {
        if (rt_obj_release_check0(pool->monitor))
            rt_obj_free(pool->monitor);
        pool->monitor = NULL;
        rt_obj_free(pool);
        return NULL;
    }

    // Start worker threads
    for (int64_t i = 0; i < size; i++) {
        pool->workers[i].pool = pool;
        pool->workers[i].thread = rt_thread_start((void *)worker_entry, &pool->workers[i]);
        if (!pool->workers[i].thread) {
            // Shutdown already started workers
            pool->shutdown = 1;
            pool->shutdown_now = 1;
            rt_monitor_enter(pool->monitor);
            rt_monitor_pause_all(pool->monitor);
            rt_monitor_exit(pool->monitor);

            for (int64_t j = 0; j < i; j++) {
                if (pool->workers[j].thread)
                    rt_thread_join(pool->workers[j].thread);
            }
            free(pool->workers);
            pool->workers = NULL;
            if (rt_obj_release_check0(pool->monitor))
                rt_obj_free(pool->monitor);
            pool->monitor = NULL;
            rt_obj_free(pool);
            return NULL;
        }
    }

    return pool;
}

//=============================================================================
// Worker Thread
//=============================================================================

static void worker_entry(void *arg) {
    pool_worker *worker = (pool_worker *)arg;
    pool_impl *pool = worker->pool;

    for (;;) {
        rt_monitor_enter(pool->monitor);

        // Wait for a task or shutdown
        while (pool->queue_head == NULL && !pool->shutdown) {
            rt_monitor_wait(pool->monitor);
        }

        // Check for immediate shutdown
        if (pool->shutdown_now) {
            rt_monitor_exit(pool->monitor);
            return;
        }

        // Check for graceful shutdown with empty queue
        if (pool->shutdown && pool->queue_head == NULL) {
            rt_monitor_exit(pool->monitor);
            return;
        }

        // Dequeue a task
        pool_task *task = pool->queue_head;
        if (task) {
            pool->queue_head = task->next;
            if (pool->queue_head == NULL)
                pool->queue_tail = NULL;
            pool->pending_count--;
            pool->active_count++;
        }

        rt_monitor_exit(pool->monitor);

        // Execute the task
        if (task && task->callback) {
            task->callback(task->arg);
            free(task);
        }

        // Mark task complete
        rt_monitor_enter(pool->monitor);
        pool->active_count--;
        // Signal waiters (for Wait() calls)
        if (pool->pending_count == 0 && pool->active_count == 0) {
            rt_monitor_pause_all(pool->monitor);
        }
        rt_monitor_exit(pool->monitor);
    }
}

//=============================================================================
// Public API - Task Submission
//=============================================================================

/// @brief Submit a task (callback + arg) for execution on the next available worker thread.
int8_t rt_threadpool_submit(void *pool_obj, void *callback, void *arg) {
    if (!pool_obj || !callback)
        return 0;

    pool_impl *pool = (pool_impl *)pool_obj;

    rt_monitor_enter(pool->monitor);

    if (pool->shutdown) {
        rt_monitor_exit(pool->monitor);
        return 0;
    }

    // Allocate task
    pool_task *task = (pool_task *)malloc(sizeof(pool_task));
    if (!task) {
        rt_monitor_exit(pool->monitor);
        return 0;
    }

    task->callback = (void (*)(void *))callback;
    task->arg = arg;
    task->next = NULL;

    // Enqueue task
    if (pool->queue_tail) {
        pool->queue_tail->next = task;
        pool->queue_tail = task;
    } else {
        pool->queue_head = task;
        pool->queue_tail = task;
    }
    pool->pending_count++;

    // Wake one worker
    rt_monitor_pause(pool->monitor);
    rt_monitor_exit(pool->monitor);

    return 1;
}

//=============================================================================
// Public API - Waiting
//=============================================================================

/// @brief Block until all submitted tasks have completed (queue empty and no active workers).
void rt_threadpool_wait(void *pool_obj) {
    if (!pool_obj)
        return;

    pool_impl *pool = (pool_impl *)pool_obj;

    rt_monitor_enter(pool->monitor);

    while (pool->pending_count > 0 || pool->active_count > 0) {
        rt_monitor_wait(pool->monitor);
    }

    rt_monitor_exit(pool->monitor);
}

/// @brief Wait for all tasks with a timeout. Returns 1 if all completed, 0 on timeout.
int8_t rt_threadpool_wait_for(void *pool_obj, int64_t ms) {
    if (!pool_obj)
        return 1;

    if (ms <= 0) {
        // Immediate check
        pool_impl *pool = (pool_impl *)pool_obj;
        rt_monitor_enter(pool->monitor);
        int8_t done = (pool->pending_count == 0 && pool->active_count == 0) ? 1 : 0;
        rt_monitor_exit(pool->monitor);
        return done;
    }

    pool_impl *pool = (pool_impl *)pool_obj;

    rt_monitor_enter(pool->monitor);

#if defined(_WIN32)
    ULONGLONG deadline = GetTickCount64() + (ULONGLONG)ms;
#else
    struct timespec start;
    clock_gettime(CLOCK_MONOTONIC, &start);
#endif

    while (pool->pending_count > 0 || pool->active_count > 0) {
#if defined(_WIN32)
        ULONGLONG now = GetTickCount64();
        int64_t remaining = now >= deadline ? 0 : (int64_t)(deadline - now);
#else
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        int64_t sec = (int64_t)now.tv_sec - (int64_t)start.tv_sec;
        int64_t ns = (int64_t)now.tv_nsec - (int64_t)start.tv_nsec;
        if (ns < 0) {
            sec--;
            ns += 1000000000L;
        }
        int64_t elapsed_ms = sec * 1000 + ns / 1000000L;
        int64_t remaining = ms - elapsed_ms;
#endif
        if (remaining <= 0 || !rt_monitor_wait_for(pool->monitor, remaining)) {
            if (pool->pending_count == 0 && pool->active_count == 0)
                break;
            // Timed out
            rt_monitor_exit(pool->monitor);
            return 0;
        }
    }

    rt_monitor_exit(pool->monitor);
    return 1;
}

//=============================================================================
// Public API - Shutdown
//=============================================================================

/// @brief Gracefully shut down the pool — finish pending tasks, then stop workers.
void rt_threadpool_shutdown(void *pool_obj) {
    if (!pool_obj)
        return;

    pool_impl *pool = (pool_impl *)pool_obj;

    rt_monitor_enter(pool->monitor);
    pool->shutdown = 1;
    rt_monitor_pause_all(pool->monitor);
    rt_monitor_exit(pool->monitor);

    // Join all workers
    for (int64_t i = 0; i < pool->worker_count; i++) {
        if (pool->workers[i].thread) {
            rt_thread_join(pool->workers[i].thread);
            pool->workers[i].thread = NULL;
        }
    }
}

/// @brief Immediately shut down the pool — discard pending tasks and stop workers.
void rt_threadpool_shutdown_now(void *pool_obj) {
    if (!pool_obj)
        return;

    pool_impl *pool = (pool_impl *)pool_obj;

    rt_monitor_enter(pool->monitor);
    pool->shutdown = 1;
    pool->shutdown_now = 1;

    // Clear the queue
    pool_task *task = pool->queue_head;
    while (task) {
        pool_task *next = task->next;
        free(task);
        task = next;
    }
    pool->queue_head = NULL;
    pool->queue_tail = NULL;
    pool->pending_count = 0;

    rt_monitor_pause_all(pool->monitor);
    rt_monitor_exit(pool->monitor);

    // Join all workers
    for (int64_t i = 0; i < pool->worker_count; i++) {
        if (pool->workers[i].thread) {
            rt_thread_join(pool->workers[i].thread);
            pool->workers[i].thread = NULL;
        }
    }
}

//=============================================================================
// Public API - Properties
//=============================================================================

/// @brief Get the number of worker threads in the pool.
int64_t rt_threadpool_get_size(void *pool_obj) {
    if (!pool_obj)
        return 0;

    pool_impl *pool = (pool_impl *)pool_obj;
    return pool->worker_count;
}

/// @brief Get the number of tasks waiting in the queue (not yet started).
int64_t rt_threadpool_get_pending(void *pool_obj) {
    if (!pool_obj)
        return 0;

    pool_impl *pool = (pool_impl *)pool_obj;

    rt_monitor_enter(pool->monitor);
    int64_t count = pool->pending_count;
    rt_monitor_exit(pool->monitor);

    return count;
}

/// @brief Get the number of tasks currently being executed by worker threads.
int64_t rt_threadpool_get_active(void *pool_obj) {
    if (!pool_obj)
        return 0;

    pool_impl *pool = (pool_impl *)pool_obj;

    rt_monitor_enter(pool->monitor);
    int64_t count = pool->active_count;
    rt_monitor_exit(pool->monitor);

    return count;
}

/// @brief Check whether the pool has been shut down.
int8_t rt_threadpool_get_is_shutdown(void *pool_obj) {
    if (!pool_obj)
        return 1;

    pool_impl *pool = (pool_impl *)pool_obj;
    return pool->shutdown;
}
