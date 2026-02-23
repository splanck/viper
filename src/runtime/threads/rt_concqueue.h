//===----------------------------------------------------------------------===//
//
// File: src/runtime/threads/rt_concqueue.h
// Purpose: Thread-safe concurrent queue with blocking and non-blocking dequeue operations, using a mutex and condition variable for producer/consumer synchronization.
//
// Key invariants:
//   - FIFO ordering; mutex-protected for thread safety.
//   - rt_concqueue_dequeue blocks on a condition variable when the queue is empty.
//   - rt_concqueue_try_dequeue returns NULL immediately if the queue is empty.
//   - rt_concqueue_close wakes all blocked dequeue callers with NULL.
//
// Ownership/Lifetime:
//   - ConcQueue objects are heap-allocated; caller is responsible for lifetime management.
//   - Enqueued values are retained; dequeued values are returned with retained reference.
//
// Links: src/runtime/threads/rt_concqueue.c (implementation)
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Create a new empty concurrent queue.
    /// @return Pointer to queue object.
    void *rt_concqueue_new(void);

    /// @brief Get approximate number of elements.
    /// @param obj ConcurrentQueue pointer.
    /// @return Element count (approximate under concurrency).
    int64_t rt_concqueue_len(void *obj);

    /// @brief Check if queue is approximately empty.
    /// @param obj ConcurrentQueue pointer.
    /// @return 1 if likely empty, 0 otherwise.
    int8_t rt_concqueue_is_empty(void *obj);

    /// @brief Add item to back of queue (thread-safe).
    /// @param obj ConcurrentQueue pointer.
    /// @param item Value to enqueue (retained).
    void rt_concqueue_enqueue(void *obj, void *item);

    /// @brief Remove item from front of queue (non-blocking).
    /// @param obj ConcurrentQueue pointer.
    /// @return Item or NULL if empty.
    void *rt_concqueue_try_dequeue(void *obj);

    /// @brief Remove item from front of queue (blocking).
    /// @param obj ConcurrentQueue pointer.
    /// @return Item (waits until available).
    void *rt_concqueue_dequeue(void *obj);

    /// @brief Remove item with timeout.
    /// @param obj ConcurrentQueue pointer.
    /// @param timeout_ms Maximum time to wait in milliseconds.
    /// @return Item or NULL if timeout expired.
    void *rt_concqueue_dequeue_timeout(void *obj, int64_t timeout_ms);

    /// @brief Peek at front item without removing (non-blocking).
    /// @param obj ConcurrentQueue pointer.
    /// @return Front item or NULL if empty.
    void *rt_concqueue_peek(void *obj);

    /// @brief Remove all items from queue (thread-safe).
    /// @param obj ConcurrentQueue pointer.
    void rt_concqueue_clear(void *obj);

#ifdef __cplusplus
}
#endif
