#pragma once

#include "../include/types.hpp"
#include "../lib/spinlock.hpp"
#include "task.hpp"

/**
 * @file wait.hpp
 * @brief Wait queue implementation for blocking/waking tasks.
 *
 * @details
 * Wait queues provide a proper mechanism for tasks to block waiting for
 * events and to be woken up when those events occur. Unlike single-task
 * pointers, wait queues support multiple waiters and provide FIFO ordering.
 *
 * Usage:
 * @code
 * WaitQueue wq;
 * wait_init(&wq);
 *
 * // To block:
 * wait_prepare(&wq, task::current());
 * if (condition_not_met) {
 *     wait_sleep(&wq);  // Task is now blocked
 * } else {
 *     wait_abort(&wq, task::current());  // Condition met, don't sleep
 * }
 *
 * // To wake:
 * wait_wake_one(&wq);   // Wake first waiter
 * wait_wake_all(&wq);   // Wake all waiters
 * @endcode
 */
namespace sched
{

/**
 * @brief A wait queue for blocking/waking tasks.
 *
 * @details
 * Uses the task's next/prev pointers for linking. This means a task can
 * only be on one wait queue OR the ready queue at a time (which is the
 * correct semantic - a blocked task shouldn't be on the ready queue).
 */
struct WaitQueue
{
    task::Task *head;  // First waiter (will be woken first)
    task::Task *tail;  // Last waiter
    u32 count;         // Number of waiters
};

/**
 * @brief Initialize a wait queue.
 *
 * @param wq Wait queue to initialize.
 */
inline void wait_init(WaitQueue *wq)
{
    wq->head = nullptr;
    wq->tail = nullptr;
    wq->count = 0;
}

/**
 * @brief Add a task to the wait queue (prepare for sleep).
 *
 * @details
 * Call this BEFORE checking the condition and potentially sleeping.
 * If the condition is met after adding, call wait_abort() to remove.
 * The task's state is set to Blocked.
 *
 * @param wq Wait queue to add to.
 * @param t Task to add.
 */
inline void wait_enqueue(WaitQueue *wq, task::Task *t)
{
    if (!wq || !t)
        return;

    // Set task state to blocked
    t->state = task::TaskState::Blocked;
    t->wait_channel = wq;  // For debugging

    // Add to tail of wait queue (FIFO)
    t->next = nullptr;
    t->prev = wq->tail;

    if (wq->tail)
    {
        wq->tail->next = t;
    }
    else
    {
        wq->head = t;
    }
    wq->tail = t;
    wq->count++;
}

/**
 * @brief Remove a task from the wait queue without waking.
 *
 * @details
 * Used when a task decides not to sleep after being added to the queue
 * (e.g., the condition was met before yielding).
 *
 * @param wq Wait queue to remove from.
 * @param t Task to remove.
 * @return true if task was found and removed, false otherwise.
 */
inline bool wait_dequeue(WaitQueue *wq, task::Task *t)
{
    if (!wq || !t)
        return false;

    // Search for task in queue
    task::Task *curr = wq->head;
    while (curr)
    {
        if (curr == t)
        {
            // Found - remove from queue
            if (curr->prev)
            {
                curr->prev->next = curr->next;
            }
            else
            {
                wq->head = curr->next;
            }

            if (curr->next)
            {
                curr->next->prev = curr->prev;
            }
            else
            {
                wq->tail = curr->prev;
            }

            curr->next = nullptr;
            curr->prev = nullptr;
            curr->wait_channel = nullptr;
            wq->count--;
            return true;
        }
        curr = curr->next;
    }
    return false;
}

/**
 * @brief Wake the first waiter in the queue.
 *
 * @details
 * Removes the first task from the queue, sets it to Ready state,
 * and enqueues it on the scheduler's ready queue.
 *
 * @param wq Wait queue to wake from.
 * @return The task that was woken, or nullptr if queue was empty.
 */
task::Task *wait_wake_one(WaitQueue *wq);

/**
 * @brief Wake all waiters in the queue.
 *
 * @details
 * Removes all tasks from the queue and enqueues them on the ready queue.
 *
 * @param wq Wait queue to wake from.
 * @return Number of tasks woken.
 */
u32 wait_wake_all(WaitQueue *wq);

/**
 * @brief Check if wait queue is empty.
 *
 * @param wq Wait queue to check.
 * @return true if empty, false if there are waiters.
 */
inline bool wait_empty(const WaitQueue *wq)
{
    return wq ? (wq->head == nullptr) : true;
}

/**
 * @brief Get number of waiters in queue.
 *
 * @param wq Wait queue to query.
 * @return Number of waiting tasks.
 */
inline u32 wait_count(const WaitQueue *wq)
{
    return wq ? wq->count : 0;
}

} // namespace sched
