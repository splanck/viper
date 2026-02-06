//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
#include "wait.hpp"
#include "../arch/aarch64/timer.hpp"
#include "../console/serial.hpp"
#include "scheduler.hpp"

/**
 * @file wait.cpp
 * @brief Wait queue implementation.
 *
 * ## Performance Notes
 *
 * The check_wait_timeouts() function currently uses O(n) scanning through
 * all tasks to find expired timeouts. This is adequate for the current
 * MAX_TASKS (256) but could be improved with a min-heap for timeouts.
 *
 * Optimization: We track the earliest timeout expiry to skip the scan entirely
 * when no timeouts are pending or none are due yet.
 *
 * @todo Consider adding a dedicated timeout heap using the existing TaskHeap
 *       infrastructure (would require a second heap_index in Task struct).
 */
namespace sched {

/// @brief Number of tasks currently waiting with a timeout.
static u32 timeout_count = 0;

/// @brief Earliest timeout expiry tick (minimum of all wait_timeout values).
///        0 means no timeouts are pending.
static u64 earliest_timeout = 0;

task::Task *wait_wake_one(WaitQueue *wq) {
    if (!wq || !wq->head)
        return nullptr;

    // Remove first task from queue
    task::Task *t = wq->head;
    wq->head = t->next;

    if (wq->head) {
        wq->head->prev = nullptr;
    } else {
        wq->tail = nullptr;
    }

    t->next = nullptr;
    t->prev = nullptr;
    t->wait_channel = nullptr;
    wq->count--;

    // Diagnostic: check if task was marked as having been in heap when blocked
    // (the magic marker from wait_enqueue defensive check)
    if (t->wait_timeout == 0xDEADBEEF) {
        serial::puts("[wait] WARNING: task '");
        serial::puts(t->name);
        serial::puts("' was in heap when blocked! heap_index=");
        serial::put_dec(t->heap_index);
        serial::puts("\n");
        t->wait_timeout = 0; // Clear the marker
    }

    // Verify heap_index is -1 before enqueueing
    if (t->heap_index != static_cast<u32>(-1)) {
        serial::puts("[wait] ERROR: task '");
        serial::puts(t->name);
        serial::puts("' heap_index=");
        serial::put_dec(t->heap_index);
        serial::puts(" at wake (should be -1)!\n");
        // Don't enqueue - task is already in a heap somehow
        return t;
    }

    // Set to ready and enqueue
    t->state = task::TaskState::Ready;
    scheduler::enqueue(t);

    return t;
}

u32 wait_wake_all(WaitQueue *wq) {
    if (!wq)
        return 0;

    u32 count = 0;

    while (wq->head) {
        task::Task *t = wq->head;
        wq->head = t->next;

        t->next = nullptr;
        t->prev = nullptr;
        t->wait_channel = nullptr;

        // Clear diagnostic marker if set
        if (t->wait_timeout == 0xDEADBEEF) {
            serial::puts("[wait] WARNING: task '");
            serial::puts(t->name);
            serial::puts("' was in heap when blocked!\n");
            t->wait_timeout = 0;
        }

        // Only wake tasks that are actually blocked (avoid double-enqueue)
        if (t->state == task::TaskState::Blocked) {
            // Verify heap_index before enqueueing
            if (t->heap_index != static_cast<u32>(-1)) {
                serial::puts("[wait] ERROR: task '");
                serial::puts(t->name);
                serial::puts("' heap_index=");
                serial::put_dec(t->heap_index);
                serial::puts(" at wake_all (should be -1)!\n");
                continue; // Skip this task
            }
            t->state = task::TaskState::Ready;
            scheduler::enqueue(t);
            count++;
        }
    }

    wq->tail = nullptr;
    wq->count = 0;

    return count;
}

void wait_enqueue_timeout(WaitQueue *wq, task::Task *t, u64 timeout_ticks) {
    if (!wq || !t)
        return;

    // Calculate absolute timeout tick
    u64 current_tick = timer::get_ticks();
    u64 abs_timeout = (timeout_ticks > 0) ? (current_tick + timeout_ticks) : 0;
    t->wait_timeout = abs_timeout;

    // Track timeout statistics for optimization
    if (abs_timeout > 0) {
        timeout_count++;
        if (earliest_timeout == 0 || abs_timeout < earliest_timeout) {
            earliest_timeout = abs_timeout;
        }
    }

    // Use regular priority-ordered enqueue
    wait_enqueue(wq, t);
}

u32 check_wait_timeouts(u64 current_tick) {
    // Fast path: no timeouts pending or none due yet
    if (timeout_count == 0) {
        return 0;
    }
    if (earliest_timeout > 0 && current_tick < earliest_timeout) {
        return 0; // No timeout is due yet
    }

    u32 woken = 0;
    u64 new_earliest = 0; // Track new minimum for next call

    // Scan all tasks for expired timeouts
    // TODO: Replace with min-heap for O(log n) operations
    for (u32 i = 0; i < task::MAX_TASKS; i++) {
        task::Task *t = task::get_by_id(i);
        if (!t)
            continue;

        // Skip tasks without timeouts or already marked as timed out
        if (t->wait_timeout == 0 || t->wait_timeout == static_cast<u64>(-1))
            continue;

        if (t->state == task::TaskState::Blocked && current_tick >= t->wait_timeout) {
            // Timeout expired - remove from wait queue and wake
            if (t->wait_channel) {
                WaitQueue *wq = reinterpret_cast<WaitQueue *>(t->wait_channel);
                wait_dequeue(wq, t);
            }

            // Mark as timed out and decrement counter
            t->wait_timeout = static_cast<u64>(-1);
            t->wait_channel = nullptr;
            if (timeout_count > 0) {
                timeout_count--;
            }

            // Verify heap_index before enqueueing
            if (t->heap_index != static_cast<u32>(-1)) {
                serial::puts("[wait] ERROR: task '");
                serial::puts(t->name);
                serial::puts("' heap_index=");
                serial::put_dec(t->heap_index);
                serial::puts(" at timeout wake (should be -1)!\n");
                continue; // Skip this task
            }

            // Wake the task
            t->state = task::TaskState::Ready;
            scheduler::enqueue(t);
            woken++;
        } else if (t->state == task::TaskState::Blocked) {
            // Task still waiting - track for new earliest
            if (new_earliest == 0 || t->wait_timeout < new_earliest) {
                new_earliest = t->wait_timeout;
            }
        }
    }

    // Update earliest timeout for next call
    earliest_timeout = new_earliest;

    return woken;
}

} // namespace sched
