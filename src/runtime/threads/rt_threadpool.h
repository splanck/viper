//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/rt_threadpool.h
// Purpose: Thread pool for async task execution (Viper.Threads.Pool).
// Key invariants: Tasks are executed FIFO, workers are recycled.
// Ownership/Lifetime: Pool objects are runtime-managed and ref-counted.
//
//===----------------------------------------------------------------------===//

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    //=========================================================================
    // Viper.Threads.Pool
    //=========================================================================

    /// @brief Create a new thread pool with the specified number of workers.
    /// @details Workers start immediately and wait for tasks. The pool size
    ///          must be at least 1.
    /// @param size Number of worker threads (clamped to 1..1024).
    /// @return Opaque Pool object pointer, or NULL on failure.
    void *rt_threadpool_new(int64_t size);

    /// @brief Submit a task to the pool for async execution.
    /// @details The task is queued and executed by the next available worker.
    ///          The callback receives @p arg as its parameter.
    /// @param pool Pool object pointer.
    /// @param callback Task function to execute (signature: void(*)(void*)).
    /// @param arg Argument passed to the callback.
    /// @return 1 if submitted, 0 if pool is shut down.
    int8_t rt_threadpool_submit(void *pool, void *callback, void *arg);

    /// @brief Wait for all pending tasks to complete.
    /// @details Blocks until the task queue is empty and all workers are idle.
    ///          Does not prevent new tasks from being submitted.
    /// @param pool Pool object pointer.
    void rt_threadpool_wait(void *pool);

    /// @brief Wait for all pending tasks with a timeout.
    /// @details Blocks up to @p ms milliseconds for tasks to complete.
    /// @param pool Pool object pointer.
    /// @param ms Timeout in milliseconds.
    /// @return 1 if all tasks completed, 0 if timed out.
    int8_t rt_threadpool_wait_for(void *pool, int64_t ms);

    /// @brief Shut down the pool gracefully.
    /// @details Stops accepting new tasks and waits for pending tasks to
    ///          complete before terminating workers. Calling Submit after
    ///          shutdown returns 0.
    /// @param pool Pool object pointer.
    void rt_threadpool_shutdown(void *pool);

    /// @brief Shut down the pool immediately.
    /// @details Stops accepting new tasks and terminates workers without
    ///          waiting for pending tasks. Tasks in the queue are discarded.
    /// @param pool Pool object pointer.
    void rt_threadpool_shutdown_now(void *pool);

    /// @brief Get the number of worker threads.
    /// @param pool Pool object pointer.
    /// @return Number of workers in the pool.
    int64_t rt_threadpool_get_size(void *pool);

    /// @brief Get the number of pending tasks.
    /// @details Returns the number of tasks waiting in the queue (not
    ///          including tasks currently being executed).
    /// @param pool Pool object pointer.
    /// @return Number of pending tasks.
    int64_t rt_threadpool_get_pending(void *pool);

    /// @brief Get the number of tasks currently running.
    /// @param pool Pool object pointer.
    /// @return Number of active tasks.
    int64_t rt_threadpool_get_active(void *pool);

    /// @brief Check if the pool is shut down.
    /// @param pool Pool object pointer.
    /// @return 1 if shut down, 0 otherwise.
    int8_t rt_threadpool_get_is_shutdown(void *pool);

#ifdef __cplusplus
}
#endif
