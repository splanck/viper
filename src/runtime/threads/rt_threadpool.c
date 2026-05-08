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
#include "rt_internal.h"
#include "rt_object.h"
#include "rt_threads.h"

#include <limits.h>
#include <setjmp.h>
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
    int64_t error_count;   ///< Number of worker task traps not yet observed.
    char last_error[512];  ///< Last worker task trap message.
    int8_t shutdown;       ///< Shutdown flag.
    int8_t shutdown_now;   ///< Immediate shutdown flag.
    int8_t cleanup_scheduled; ///< Deferred cleanup was scheduled from a worker finalizer.
} pool_impl;

//=============================================================================
// Forward Declarations
//=============================================================================

static void pool_finalizer(void *obj);
static void worker_entry(void *arg);
static void pool_deferred_cleanup_entry(void *arg);

#include "rt_trap.h"

void rt_trap_set_recovery(jmp_buf *buf);
void rt_trap_clear_recovery(void);
const char *rt_trap_get_error(void);

#if defined(_MSC_VER)
static __declspec(thread) pool_impl *g_current_worker_pool = NULL;
#else
static __thread pool_impl *g_current_worker_pool = NULL;
#endif

/// @brief Release a worker-thread handle held in @p *slot and NULL the slot.
/// @details Standard ownership-discipline helper used during pool shutdown
///          and finalize. Releases the runtime ref; if the refcount drops
///          to zero the object is freed. NULLing the slot prevents any
///          subsequent re-release from double-freeing.
static void pool_release_thread_handle(void **slot) {
    if (!slot || !*slot)
        return;
    if (rt_obj_release_check0(*slot))
        rt_obj_free(*slot);
    *slot = NULL;
}

/// @brief Release a retained pool reference; free the impl when the count reaches zero.
/// @details Used by deferred-cleanup paths that hold a pool ref across
///          background work. Safe to call with NULL — no-op in that case.
static void pool_release_object(pool_impl *pool) {
    if (pool && rt_obj_release_check0(pool))
        rt_obj_free(pool);
}

/// @brief Validate-and-cast an opaque pool pointer to its impl.
/// @details Every public Pool entry point dispatches through this guard.
///          NULL @p pool_obj triggers a trap iff @p trap_on_null is set
///          (otherwise returns NULL); wrong-class id always traps. Returns
///          the downcast pointer on success.
static pool_impl *pool_require(void *pool_obj, const char *what, int8_t trap_on_null) {
    if (!pool_obj) {
        if (trap_on_null)
            rt_trap(what ? what : "Pool: null object");
        return NULL;
    }
    if (rt_obj_class_id(pool_obj) != RT_THREADPOOL_CLASS_ID) {
        rt_trap("Pool: invalid object");
        return NULL;
    }
    return (pool_impl *)pool_obj;
}

/// @brief If the pool has captured a worker-task trap message, copy it to @p out and report 1.
/// @details Caller must hold pool->monitor. Used by Wait / Shutdown to
///          surface the first task-trap message seen by any worker. The
///          buffer is null-terminated (truncating long messages) so the
///          caller can pass it directly to rt_trap. Returns 0 if no
///          worker has trapped since the last error-take.
static int8_t pool_take_error_locked(pool_impl *pool, char *out, size_t out_size) {
    if (!pool || pool->error_count <= 0)
        return 0;
    const char *msg = pool->last_error[0] ? pool->last_error : "Pool.Wait: task trapped";
    if (out && out_size > 0) {
        strncpy(out, msg, out_size - 1);
        out[out_size - 1] = '\0';
    }
    return 1;
}

/// @brief Re-raise the first worker-task trap message under the pool's monitor.
/// @details Drains the captured error via pool_take_error_locked and, if
///          one is present, traps the calling thread with that message.
///          Used by Pool.Wait so a Submit-then-Wait sequence surfaces task
///          traps to the caller rather than swallowing them in worker
///          threads.
static void pool_trap_if_error(pool_impl *pool) {
    char error[512];
    error[0] = '\0';

    rt_monitor_enter(pool->monitor);
    int8_t has_error = pool_take_error_locked(pool, error, sizeof(error));
    rt_monitor_exit(pool->monitor);

    if (has_error)
        rt_trap(error[0] ? error : "Pool.Wait: task trapped");
}

/// @brief Move every worker thread handle out of the pool's `workers` array into @p handles.
/// @details Caller must hold pool->monitor. Steals the thread handles
///          (NULLing the pool's slots) so the caller can join them
///          without holding the lock. Used by Shutdown's two-phase
///          sequence: take handles under the lock, drop the lock, then
///          join the handles to avoid blocking submit/wait callers.
static void pool_take_worker_handles_locked(pool_impl *pool, void **handles) {
    if (!pool || !handles || !pool->workers)
        return;
    for (int64_t i = 0; i < pool->worker_count; i++) {
        handles[i] = pool->workers[i].thread;
        pool->workers[i].thread = NULL;
    }
}

/// @brief Join every non-NULL handle in @p handles and release each retained ref.
/// @details Companion to pool_take_worker_handles_locked. Walks the handle
///          array, calls rt_thread_join on each, and releases the runtime
///          ref. NULL slots are skipped (the take helper may have left
///          some empty if the pool was constructed but never started).
static void pool_join_worker_handles(void **handles, int64_t count) {
    if (!handles)
        return;
    for (int64_t i = 0; i < count; i++) {
        if (handles[i]) {
            rt_thread_join(handles[i]);
            pool_release_thread_handle(&handles[i]);
        }
    }
}

static void **pool_detach_worker_handles(pool_impl *pool) {
    if (!pool || pool->worker_count <= 0)
        return NULL;
    void **handles = (void **)calloc((size_t)pool->worker_count, sizeof(void *));
    if (!handles)
        return NULL;
    if (pool->monitor)
        rt_monitor_enter(pool->monitor);
    pool_take_worker_handles_locked(pool, handles);
    if (pool->monitor)
        rt_monitor_exit(pool->monitor);
    return handles;
}

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

/// @brief GC finalizer for a `ThreadPool` — drain, join, and tear down.
/// @details Two paths, gated on whether the current thread is one of the pool's
///          own workers:
///
///          **Self-finalize guard.** If a worker thread is the last owner (a user
///          callback returned the pool as its only live reference), joining and
///          freeing the worker array + monitor from that same worker would free
///          state the thread is still actively executing. The finalizer detects
///          this via `g_current_worker_pool` and instead:
///            1. Resurrects the pool object so GC doesn't reclaim it.
///            2. Re-installs itself as the finalizer.
///            3. Signals shutdown so workers exit their loop.
///          A later non-worker release will reclaim the pool safely.
///
///          **Normal path.** Untracks from GC, signals shutdown, joins every
///          worker, destroys the monitor, and frees the pool struct. Handles
///          the at-exit case where a program terminates without an explicit
///          `ThreadPool.Shutdown` call.
static void pool_finalizer(void *obj) {
    pool_impl *pool = (pool_impl *)obj;
    if (!pool)
        return;

    if (g_current_worker_pool == pool) {
        /*
         * A worker may be the last owner of a pool through a user callback.
         * Joining/freeing the worker array and monitor from that same worker
         * would free state while it is still executing. Keep the object alive
         * and ask workers to stop; a later non-worker release/finalizer can
         * reclaim the pool safely.
         */
        int8_t start_cleanup = 0;
        rt_obj_resurrect(pool);
        rt_obj_set_finalizer(pool, pool_finalizer);
        if (pool->monitor) {
            rt_monitor_enter(pool->monitor);
            pool->shutdown = 1;
            pool->shutdown_now = 1;
            if (!pool->cleanup_scheduled) {
                pool->cleanup_scheduled = 1;
                start_cleanup = 1;
            }
            rt_monitor_pause_all(pool->monitor);
            rt_monitor_exit(pool->monitor);
        }
        if (start_cleanup) {
            void *cleanup_thread = rt_thread_start_owned((void *)pool_deferred_cleanup_entry, pool);
            pool_release_thread_handle(&cleanup_thread);
        }
        return;
    }

    rt_gc_untrack(pool);

    /* If not already shut down, signal workers and join them.
       This handles the case where a program exits without calling
       rt_threadpool_shutdown() — the atexit finalizer sweep invokes
       this finalizer to prevent abandoned worker threads. */
    if (pool->monitor) {
        rt_monitor_enter(pool->monitor);
        if (!pool->shutdown) {
            pool->shutdown = 1;
            pool->shutdown_now = 1;
        }
        rt_monitor_pause_all(pool->monitor);
        rt_monitor_exit(pool->monitor);
    }

    void **handles = pool_detach_worker_handles(pool);
    if (handles) {
        pool_join_worker_handles(handles, pool->worker_count);
        free(handles);
    } else {
        for (int64_t i = 0; i < pool->worker_count; i++) {
            if (pool->workers && pool->workers[i].thread) {
                rt_thread_join(pool->workers[i].thread);
                pool_release_thread_handle(&pool->workers[i].thread);
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

    pool_impl *pool =
        (pool_impl *)rt_obj_new_i64(RT_THREADPOOL_CLASS_ID, sizeof(pool_impl));
    if (!pool)
        return NULL;

    rt_obj_set_finalizer(pool, pool_finalizer);
    rt_gc_track(pool, pool_traverse);

    pool->monitor = rt_obj_new_i64(0, 1); // Create a monitor object
    if (!pool->monitor) {
        pool_release_object(pool);
        return NULL;
    }

    pool->queue_head = NULL;
    pool->queue_tail = NULL;
    pool->pending_count = 0;
    pool->active_count = 0;
    pool->error_count = 0;
    pool->last_error[0] = '\0';
    pool->shutdown = 0;
    pool->shutdown_now = 0;
    pool->cleanup_scheduled = 0;
    pool->worker_count = size;

    // Allocate workers array
    pool->workers = (pool_worker *)calloc((size_t)size, sizeof(pool_worker));
    if (!pool->workers) {
        pool_release_object(pool);
        return NULL;
    }

    // Start worker threads
    for (int64_t i = 0; i < size; i++) {
        pool->workers[i].pool = pool;
        pool->workers[i].thread = rt_thread_start((void *)worker_entry, &pool->workers[i]);
        if (!pool->workers[i].thread) {
            // Shutdown already started workers
            rt_monitor_enter(pool->monitor);
            pool->shutdown = 1;
            pool->shutdown_now = 1;
            rt_monitor_pause_all(pool->monitor);
            rt_monitor_exit(pool->monitor);

            for (int64_t j = 0; j < i; j++) {
                if (pool->workers[j].thread) {
                    rt_thread_join(pool->workers[j].thread);
                    pool_release_thread_handle(&pool->workers[j].thread);
                }
            }
            pool_release_object(pool);
            return NULL;
        }
    }

    return pool;
}

//=============================================================================
// Worker Thread
//=============================================================================

/// @brief Main worker-thread loop: dequeue tasks, execute under trap recovery, signal
///        waiters when the pool drains.
/// @details Each iteration acquires the pool monitor, waits on the condition until
///          the queue has work OR shutdown was requested, dequeues one task (if any),
///          releases the monitor, and executes the task with a `setjmp` recovery
///          frame so a trap in user code doesn't unwind through the worker thread.
///          After the task returns (or the trap is caught), the monitor is re-acquired
///          to decrement `active_count`, and if both `pending_count` and
///          `active_count` have reached zero the waiter-set (e.g. `Pool.Wait()`) is
///          woken via `rt_monitor_pause_all`.
///
///          Two shutdown modes:
///            - `shutdown_now`: break out immediately regardless of remaining work.
///            - `shutdown` (graceful): drain the queue first, break when empty.
///
///          `g_current_worker_pool` is set so that user task code can detect
///          whether it's running inside a worker (used to reject re-entrant
///          `Pool.Wait()` calls that would deadlock).
///
///          Trap recovery invariants:
///            - `rt_trap_set_recovery` must pair with `rt_trap_clear_recovery` on
///              every path (happy + trap). Missing the clear would leave a stale
///              `longjmp` target in TLS that a later unrelated trap could jump to.
///            - Task memory is `free`'d only after the recovery clear so a trap
///              inside the task callback doesn't double-free when the next iteration
///              re-enters.
static void worker_entry(void *arg) {
    pool_worker *worker = (pool_worker *)arg;
    pool_impl *pool = worker->pool;
    g_current_worker_pool = pool;

    for (;;) {
        rt_monitor_enter(pool->monitor);

        // Wait for a task or shutdown
        while (pool->queue_head == NULL && !pool->shutdown) {
            rt_monitor_wait(pool->monitor);
        }

        // Check for immediate shutdown
        if (pool->shutdown_now) {
            rt_monitor_exit(pool->monitor);
            break;
        }

        // Check for graceful shutdown with empty queue
        if (pool->shutdown && pool->queue_head == NULL) {
            rt_monitor_exit(pool->monitor);
            break;
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
        int8_t trapped = 0;
        char task_error[512];
        task_error[0] = '\0';
        if (task && task->callback) {
            jmp_buf recovery;
            rt_trap_set_recovery(&recovery);
            if (setjmp(recovery) == 0) {
                task->callback(task->arg);
            } else {
                const char *msg = rt_trap_get_error();
                if (!msg || !msg[0])
                    msg = "Pool.Wait: task trapped";
                strncpy(task_error, msg, sizeof(task_error) - 1);
                task_error[sizeof(task_error) - 1] = '\0';
                trapped = 1;
            }
            rt_trap_clear_recovery();
            free(task);
        }

        // Mark task complete
        rt_monitor_enter(pool->monitor);
        if (trapped) {
            pool->error_count++;
            strncpy(pool->last_error, task_error[0] ? task_error : "Pool.Wait: task trapped", sizeof(pool->last_error) - 1);
            pool->last_error[sizeof(pool->last_error) - 1] = '\0';
        }
        pool->active_count--;
        // Signal waiters (for Wait() calls)
        if (pool->pending_count == 0 && pool->active_count == 0) {
            rt_monitor_pause_all(pool->monitor);
        }
        rt_monitor_exit(pool->monitor);
    }

    g_current_worker_pool = NULL;
}

static void pool_deferred_cleanup_entry(void *arg) {
    pool_impl *pool = (pool_impl *)arg;
    pool_release_object(pool);
}

//=============================================================================
// Public API - Task Submission
//=============================================================================

/// @brief Submit a task (callback + arg) for execution on the next available worker thread.
int8_t rt_threadpool_submit(void *pool_obj, void *callback, void *arg) {
    if (!pool_obj || !callback)
        return 0;

    pool_impl *pool = pool_require(pool_obj, "Pool.Submit: null object", 0);
    if (!pool)
        return 0;

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

    pool_impl *pool = pool_require(pool_obj, "Pool.Wait: null object", 0);
    if (!pool)
        return;
    if (g_current_worker_pool == pool) {
        rt_trap("Pool.Wait: cannot wait from pool worker");
        return;
    }

    rt_monitor_enter(pool->monitor);

    while (pool->pending_count > 0 || pool->active_count > 0) {
        rt_monitor_wait(pool->monitor);
    }

    rt_monitor_exit(pool->monitor);
    pool_trap_if_error(pool);
}

/// @brief Wait for all tasks with a timeout. Returns 1 if all completed, 0 on timeout.
int8_t rt_threadpool_wait_for(void *pool_obj, int64_t ms) {
    if (!pool_obj)
        return 1;

    pool_impl *pool = pool_require(pool_obj, "Pool.WaitFor: null object", 0);
    if (!pool)
        return 0;
    if (g_current_worker_pool == pool) {
        rt_trap("Pool.WaitFor: cannot wait from pool worker");
        return 0;
    }

    if (ms <= 0) {
        // Immediate check
        rt_monitor_enter(pool->monitor);
        int8_t done = (pool->pending_count == 0 && pool->active_count == 0) ? 1 : 0;
        rt_monitor_exit(pool->monitor);
        if (done)
            pool_trap_if_error(pool);
        return done;
    }

    rt_monitor_enter(pool->monitor);

#if defined(_WIN32)
    ULONGLONG now0 = GetTickCount64();
    ULONGLONG add = (ULONGLONG)ms;
    ULONGLONG deadline = (ULLONG_MAX - now0 < add) ? ULLONG_MAX : now0 + add;
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
        int64_t elapsed_ms = sec > INT64_MAX / 1000 ? INT64_MAX : sec * 1000 + ns / 1000000L;
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
    pool_trap_if_error(pool);
    return 1;
}

//=============================================================================
// Public API - Shutdown
//=============================================================================

/// @brief Gracefully shut down the pool — finish pending tasks, then stop workers.
void rt_threadpool_shutdown(void *pool_obj) {
    if (!pool_obj)
        return;

    pool_impl *pool = pool_require(pool_obj, "Pool.Shutdown: null object", 0);
    if (!pool)
        return;
    if (g_current_worker_pool == pool) {
        rt_trap("Pool.Shutdown: cannot shutdown from pool worker");
        return;
    }

    void **handles = (void **)calloc((size_t)pool->worker_count, sizeof(void *));
    if (!handles) {
        rt_trap("Pool.Shutdown: memory allocation failed");
        return;
    }

    rt_monitor_enter(pool->monitor);
    pool->shutdown = 1;
    pool_take_worker_handles_locked(pool, handles);
    rt_monitor_pause_all(pool->monitor);
    rt_monitor_exit(pool->monitor);

    pool_join_worker_handles(handles, pool->worker_count);
    free(handles);
    pool_trap_if_error(pool);
}

/// @brief Immediately shut down the pool — discard pending tasks and stop workers.
void rt_threadpool_shutdown_now(void *pool_obj) {
    if (!pool_obj)
        return;

    pool_impl *pool = pool_require(pool_obj, "Pool.ShutdownNow: null object", 0);
    if (!pool)
        return;
    if (g_current_worker_pool == pool) {
        rt_trap("Pool.ShutdownNow: cannot shutdown from pool worker");
        return;
    }

    void **handles = (void **)calloc((size_t)pool->worker_count, sizeof(void *));
    if (!handles) {
        rt_trap("Pool.ShutdownNow: memory allocation failed");
        return;
    }

    rt_monitor_enter(pool->monitor);
    pool->shutdown = 1;
    pool->shutdown_now = 1;
    pool_take_worker_handles_locked(pool, handles);

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

    pool_join_worker_handles(handles, pool->worker_count);
    free(handles);
    pool_trap_if_error(pool);
}

//=============================================================================
// Public API - Properties
//=============================================================================

/// @brief Get the number of worker threads in the pool.
int64_t rt_threadpool_get_size(void *pool_obj) {
    if (!pool_obj)
        return 0;

    pool_impl *pool = pool_require(pool_obj, "Pool.get_Size: null object", 0);
    if (!pool)
        return 0;
    return pool->worker_count;
}

/// @brief Get the number of tasks waiting in the queue (not yet started).
int64_t rt_threadpool_get_pending(void *pool_obj) {
    if (!pool_obj)
        return 0;

    pool_impl *pool = pool_require(pool_obj, "Pool.get_Pending: null object", 0);
    if (!pool)
        return 0;

    rt_monitor_enter(pool->monitor);
    int64_t count = pool->pending_count;
    rt_monitor_exit(pool->monitor);

    return count;
}

/// @brief Get the number of tasks currently being executed by worker threads.
int64_t rt_threadpool_get_active(void *pool_obj) {
    if (!pool_obj)
        return 0;

    pool_impl *pool = pool_require(pool_obj, "Pool.get_Active: null object", 0);
    if (!pool)
        return 0;

    rt_monitor_enter(pool->monitor);
    int64_t count = pool->active_count;
    rt_monitor_exit(pool->monitor);

    return count;
}

/// @brief Check whether the pool has been shut down.
int8_t rt_threadpool_get_is_shutdown(void *pool_obj) {
    if (!pool_obj)
        return 1;

    pool_impl *pool = pool_require(pool_obj, "Pool.get_IsShutdown: null object", 0);
    if (!pool)
        return 1;
    rt_monitor_enter(pool->monitor);
    int8_t shutdown = pool->shutdown;
    rt_monitor_exit(pool->monitor);
    return shutdown;
}

void *rt_threadpool_current_worker_pool(void) {
    return g_current_worker_pool;
}
