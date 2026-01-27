//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
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
 * ## Locking Requirements
 *
 * @warning WaitQueue operations are NOT thread-safe on their own. Callers
 * MUST hold an appropriate lock (typically a Spinlock) when calling any
 * WaitQueue function that modifies the queue state. This includes:
 * - wait_enqueue() - adds task to queue
 * - wait_dequeue() - removes task from queue
 * - wait_wake_one() - removes and wakes first task
 * - wait_wake_all() - removes and wakes all tasks
 *
 * The read-only functions wait_empty() and wait_count() should also be
 * called under the same lock if the result affects synchronization decisions.
 *
 * Typical pattern with external locking:
 * @code
 * Spinlock lock;
 * WaitQueue wq;
 *
 * // To block (producer/consumer example):
 * u64 saved_daif = lock.acquire();
 * while (buffer_empty) {
 *     wait_enqueue(&wq, task::current());
 *     lock.release(saved_daif);
 *     task::yield();  // Switch away, wake will re-acquire
 *     saved_daif = lock.acquire();
 * }
 * // ... consume from buffer ...
 * lock.release(saved_daif);
 *
 * // To wake:
 * u64 saved_daif = lock.acquire();
 * // ... produce to buffer ...
 * wait_wake_one(&wq);
 * lock.release(saved_daif);
 * @endcode
 *
 * @note The lock must be released BEFORE calling task::yield() but the task
 * must be enqueued BEFORE releasing the lock to avoid lost wakeups.
 */
namespace sched {

/**
 * @brief A wait queue for blocking/waking tasks.
 *
 * @details
 * Uses the task's next/prev pointers for linking. This means a task can
 * only be on one wait queue OR the ready queue at a time (which is the
 * correct semantic - a blocked task shouldn't be on the ready queue).
 */
struct WaitQueue {
    task::Task *head; // First waiter (will be woken first)
    task::Task *tail; // Last waiter
    u32 count;        // Number of waiters
};

/**
 * @brief Initialize a wait queue.
 *
 * @param wq Wait queue to initialize.
 */
inline void wait_init(WaitQueue *wq) {
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
 * Tasks are inserted in priority order (lower priority value = higher priority).
 * This ensures high-priority tasks are woken before low-priority ones.
 *
 * @param wq Wait queue to add to.
 * @param t Task to add.
 */
inline void wait_enqueue(WaitQueue *wq, task::Task *t) {
    if (!wq || !t)
        return;

    // Set task state to blocked
    t->state = task::TaskState::Blocked;
    t->wait_channel = wq; // For debugging

    // Insert in priority order (lower value = higher priority = earlier in queue)
    if (!wq->head || t->priority < wq->head->priority) {
        // Insert at head (highest priority or empty queue)
        t->next = wq->head;
        t->prev = nullptr;
        if (wq->head) {
            wq->head->prev = t;
        } else {
            wq->tail = t;
        }
        wq->head = t;
    } else {
        // Find insertion point (after all tasks with higher or equal priority)
        task::Task *curr = wq->head;
        while (curr->next && curr->next->priority <= t->priority) {
            curr = curr->next;
        }
        // Insert after curr
        t->next = curr->next;
        t->prev = curr;
        if (curr->next) {
            curr->next->prev = t;
        } else {
            wq->tail = t;
        }
        curr->next = t;
    }
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
inline bool wait_dequeue(WaitQueue *wq, task::Task *t) {
    if (!wq || !t)
        return false;

    // Search for task in queue
    task::Task *curr = wq->head;
    while (curr) {
        if (curr == t) {
            // Found - remove from queue
            if (curr->prev) {
                curr->prev->next = curr->next;
            } else {
                wq->head = curr->next;
            }

            if (curr->next) {
                curr->next->prev = curr->prev;
            } else {
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
inline bool wait_empty(const WaitQueue *wq) {
    return wq ? (wq->head == nullptr) : true;
}

/**
 * @brief Get number of waiters in queue.
 *
 * @param wq Wait queue to query.
 * @return Number of waiting tasks.
 */
inline u32 wait_count(const WaitQueue *wq) {
    return wq ? wq->count : 0;
}

/**
 * @brief Add a task to the wait queue with a timeout.
 *
 * @details
 * Same as wait_enqueue but sets a timeout. If the timeout expires before
 * the task is woken, it will be woken with a timeout indication.
 *
 * @param wq Wait queue to add to.
 * @param t Task to add.
 * @param timeout_ticks Timeout in timer ticks from now. 0 means no timeout.
 */
void wait_enqueue_timeout(WaitQueue *wq, task::Task *t, u64 timeout_ticks);

/**
 * @brief Check for and wake timed-out waiters.
 *
 * @details
 * Called from the timer interrupt to check all blocked tasks for timeouts.
 * Any task whose timeout has expired is woken and its wait_timeout is set to 0.
 *
 * @param current_tick Current system tick count.
 * @return Number of tasks woken due to timeout.
 */
u32 check_wait_timeouts(u64 current_tick);

/**
 * @brief Check if a task was woken due to timeout.
 *
 * @details
 * Call after a task returns from waiting to check if it timed out.
 * A task that timed out will have wait_timeout == 0 but was not
 * explicitly woken by wait_wake_one/all.
 *
 * @param t Task to check.
 * @return true if the task timed out, false if it was explicitly woken.
 */
inline bool wait_timed_out(task::Task *t) {
    // A task that was woken by timeout will have wait_channel cleared
    // but was not on a wait queue when woken - check via a flag
    // For simplicity, we use wait_timeout == (u64)-1 to indicate timeout occurred
    return t && t->wait_timeout == static_cast<u64>(-1);
}

/**
 * @brief Clear the timeout flag after handling.
 *
 * @param t Task to clear.
 */
inline void wait_clear_timeout(task::Task *t) {
    if (t) {
        t->wait_timeout = 0;
    }
}

} // namespace sched
