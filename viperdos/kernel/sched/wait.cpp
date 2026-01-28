//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
#include "wait.hpp"
#include "../arch/aarch64/timer.hpp"
#include "scheduler.hpp"

/**
 * @file wait.cpp
 * @brief Wait queue implementation.
 */
namespace sched {

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

        // Set to ready and enqueue
        t->state = task::TaskState::Ready;
        scheduler::enqueue(t);
        count++;
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
    t->wait_timeout = (timeout_ticks > 0) ? (current_tick + timeout_ticks) : 0;

    // Use regular priority-ordered enqueue
    wait_enqueue(wq, t);
}

u32 check_wait_timeouts(u64 current_tick) {
    u32 woken = 0;

    // Iterate through all tasks checking for timeouts
    // We need access to the task table - this is a bit invasive
    // For now, we'll use get_by_id to iterate, but ideally we'd
    // have a dedicated timeout list
    for (u32 i = 0; i < task::MAX_TASKS; i++) {
        task::Task *t = task::get_by_id(i);
        if (!t)
            continue;

        if (t->state == task::TaskState::Blocked && t->wait_timeout != 0 &&
            t->wait_timeout != static_cast<u64>(-1) && current_tick >= t->wait_timeout) {
            // Timeout expired - remove from wait queue and wake
            if (t->wait_channel) {
                WaitQueue *wq = reinterpret_cast<WaitQueue *>(t->wait_channel);
                wait_dequeue(wq, t);
            }

            // Mark as timed out
            t->wait_timeout = static_cast<u64>(-1);
            t->wait_channel = nullptr;

            // Wake the task
            t->state = task::TaskState::Ready;
            scheduler::enqueue(t);
            woken++;
        }
    }

    return woken;
}

} // namespace sched
