//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file rt_threadpool.c
/// @brief Thread pool implementation for async task execution.
///
/// This file implements a fixed-size thread pool that executes tasks
/// asynchronously using a pool of worker threads.
///
/// **Architecture:**
///
/// | Component     | Description                           |
/// |---------------|---------------------------------------|
/// | Task Queue    | FIFO queue of pending tasks           |
/// | Worker        | Thread that dequeues and runs tasks   |
/// | Monitor       | Synchronization for queue access      |
///
/// **Usage Example:**
/// ```
/// Dim pool = Pool.New(4)  ' Create pool with 4 workers
/// pool.Submit(MyTask, arg1)
/// pool.Submit(MyTask, arg2)
/// pool.Wait()  ' Wait for all tasks
/// pool.Shutdown()
/// ```
///
/// **Thread Safety:** All operations are thread-safe.
///
//===----------------------------------------------------------------------===//

#include "rt_threadpool.h"

#include "rt_object.h"
#include "rt_threads.h"

#include <stdlib.h>
#include <string.h>

//=============================================================================
// Internal Structures
//=============================================================================

/// @brief Task entry in the queue.
typedef struct pool_task
{
    void (*callback)(void *); ///< Task function pointer.
    void *arg;                ///< Argument for the callback.
    struct pool_task *next;   ///< Next task in queue.
} pool_task;

/// @brief Worker thread state.
typedef struct pool_worker
{
    void *thread;           ///< Thread handle.
    struct pool_impl *pool; ///< Back-reference to pool.
} pool_worker;

/// @brief Thread pool implementation.
typedef struct pool_impl
{
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

extern void rt_trap(const char *msg);

//=============================================================================
// Pool Management
//=============================================================================

static void pool_finalizer(void *obj)
{
    pool_impl *pool = (pool_impl *)obj;
    if (!pool)
        return;

    // Note: Should already be shut down when finalizer runs
    // Clean up any remaining queue
    pool_task *task = pool->queue_head;
    while (task)
    {
        pool_task *next = task->next;
        free(task);
        task = next;
    }

    // Free workers array (threads should already be joined)
    if (pool->workers)
        free(pool->workers);

    // Release monitor
    if (pool->monitor)
    {
        if (rt_obj_release_check0(pool->monitor))
            rt_obj_free(pool->monitor);
    }
}

void *rt_threadpool_new(int64_t size)
{
    // Clamp size to valid range
    if (size < 1)
        size = 1;
    if (size > 1024)
        size = 1024;

    pool_impl *pool = (pool_impl *)rt_obj_new_i64(0, sizeof(pool_impl));
    if (!pool)
        return NULL;

    rt_obj_set_finalizer(pool, pool_finalizer);

    pool->monitor = rt_obj_new_i64(0, 1); // Create a monitor object
    if (!pool->monitor)
    {
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
    if (!pool->workers)
    {
        if (rt_obj_release_check0(pool->monitor))
            rt_obj_free(pool->monitor);
        rt_obj_free(pool);
        return NULL;
    }

    // Start worker threads
    for (int64_t i = 0; i < size; i++)
    {
        pool->workers[i].pool = pool;
        pool->workers[i].thread = rt_thread_start((void *)worker_entry, &pool->workers[i]);
        if (!pool->workers[i].thread)
        {
            // Shutdown already started workers
            pool->shutdown = 1;
            pool->shutdown_now = 1;
            rt_monitor_enter(pool->monitor);
            rt_monitor_pause_all(pool->monitor);
            rt_monitor_exit(pool->monitor);

            for (int64_t j = 0; j < i; j++)
            {
                if (pool->workers[j].thread)
                    rt_thread_join(pool->workers[j].thread);
            }
            free(pool->workers);
            if (rt_obj_release_check0(pool->monitor))
                rt_obj_free(pool->monitor);
            rt_obj_free(pool);
            return NULL;
        }
    }

    return pool;
}

//=============================================================================
// Worker Thread
//=============================================================================

static void worker_entry(void *arg)
{
    pool_worker *worker = (pool_worker *)arg;
    pool_impl *pool = worker->pool;

    for (;;)
    {
        rt_monitor_enter(pool->monitor);

        // Wait for a task or shutdown
        while (pool->queue_head == NULL && !pool->shutdown)
        {
            rt_monitor_wait(pool->monitor);
        }

        // Check for immediate shutdown
        if (pool->shutdown_now)
        {
            rt_monitor_exit(pool->monitor);
            return;
        }

        // Check for graceful shutdown with empty queue
        if (pool->shutdown && pool->queue_head == NULL)
        {
            rt_monitor_exit(pool->monitor);
            return;
        }

        // Dequeue a task
        pool_task *task = pool->queue_head;
        if (task)
        {
            pool->queue_head = task->next;
            if (pool->queue_head == NULL)
                pool->queue_tail = NULL;
            pool->pending_count--;
            pool->active_count++;
        }

        rt_monitor_exit(pool->monitor);

        // Execute the task
        if (task && task->callback)
        {
            task->callback(task->arg);
            free(task);
        }

        // Mark task complete
        rt_monitor_enter(pool->monitor);
        pool->active_count--;
        // Signal waiters (for Wait() calls)
        if (pool->pending_count == 0 && pool->active_count == 0)
        {
            rt_monitor_pause_all(pool->monitor);
        }
        rt_monitor_exit(pool->monitor);
    }
}

//=============================================================================
// Public API - Task Submission
//=============================================================================

int8_t rt_threadpool_submit(void *pool_obj, void *callback, void *arg)
{
    if (!pool_obj || !callback)
        return 0;

    pool_impl *pool = (pool_impl *)pool_obj;

    rt_monitor_enter(pool->monitor);

    if (pool->shutdown)
    {
        rt_monitor_exit(pool->monitor);
        return 0;
    }

    // Allocate task
    pool_task *task = (pool_task *)malloc(sizeof(pool_task));
    if (!task)
    {
        rt_monitor_exit(pool->monitor);
        return 0;
    }

    task->callback = (void (*)(void *))callback;
    task->arg = arg;
    task->next = NULL;

    // Enqueue task
    if (pool->queue_tail)
    {
        pool->queue_tail->next = task;
        pool->queue_tail = task;
    }
    else
    {
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

void rt_threadpool_wait(void *pool_obj)
{
    if (!pool_obj)
        return;

    pool_impl *pool = (pool_impl *)pool_obj;

    rt_monitor_enter(pool->monitor);

    while (pool->pending_count > 0 || pool->active_count > 0)
    {
        rt_monitor_wait(pool->monitor);
    }

    rt_monitor_exit(pool->monitor);
}

int8_t rt_threadpool_wait_for(void *pool_obj, int64_t ms)
{
    if (!pool_obj)
        return 1;

    if (ms <= 0)
    {
        // Immediate check
        pool_impl *pool = (pool_impl *)pool_obj;
        rt_monitor_enter(pool->monitor);
        int8_t done = (pool->pending_count == 0 && pool->active_count == 0) ? 1 : 0;
        rt_monitor_exit(pool->monitor);
        return done;
    }

    pool_impl *pool = (pool_impl *)pool_obj;

    rt_monitor_enter(pool->monitor);

    while (pool->pending_count > 0 || pool->active_count > 0)
    {
        if (!rt_monitor_wait_for(pool->monitor, ms))
        {
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

void rt_threadpool_shutdown(void *pool_obj)
{
    if (!pool_obj)
        return;

    pool_impl *pool = (pool_impl *)pool_obj;

    rt_monitor_enter(pool->monitor);
    pool->shutdown = 1;
    rt_monitor_pause_all(pool->monitor);
    rt_monitor_exit(pool->monitor);

    // Join all workers
    for (int64_t i = 0; i < pool->worker_count; i++)
    {
        if (pool->workers[i].thread)
        {
            rt_thread_join(pool->workers[i].thread);
            pool->workers[i].thread = NULL;
        }
    }
}

void rt_threadpool_shutdown_now(void *pool_obj)
{
    if (!pool_obj)
        return;

    pool_impl *pool = (pool_impl *)pool_obj;

    rt_monitor_enter(pool->monitor);
    pool->shutdown = 1;
    pool->shutdown_now = 1;

    // Clear the queue
    pool_task *task = pool->queue_head;
    while (task)
    {
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
    for (int64_t i = 0; i < pool->worker_count; i++)
    {
        if (pool->workers[i].thread)
        {
            rt_thread_join(pool->workers[i].thread);
            pool->workers[i].thread = NULL;
        }
    }
}

//=============================================================================
// Public API - Properties
//=============================================================================

int64_t rt_threadpool_get_size(void *pool_obj)
{
    if (!pool_obj)
        return 0;

    pool_impl *pool = (pool_impl *)pool_obj;
    return pool->worker_count;
}

int64_t rt_threadpool_get_pending(void *pool_obj)
{
    if (!pool_obj)
        return 0;

    pool_impl *pool = (pool_impl *)pool_obj;

    rt_monitor_enter(pool->monitor);
    int64_t count = pool->pending_count;
    rt_monitor_exit(pool->monitor);

    return count;
}

int64_t rt_threadpool_get_active(void *pool_obj)
{
    if (!pool_obj)
        return 0;

    pool_impl *pool = (pool_impl *)pool_obj;

    rt_monitor_enter(pool->monitor);
    int64_t count = pool->active_count;
    rt_monitor_exit(pool->monitor);

    return count;
}

int8_t rt_threadpool_get_is_shutdown(void *pool_obj)
{
    if (!pool_obj)
        return 1;

    pool_impl *pool = (pool_impl *)pool_obj;
    return pool->shutdown;
}
