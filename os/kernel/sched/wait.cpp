#include "wait.hpp"
#include "scheduler.hpp"

/**
 * @file wait.cpp
 * @brief Wait queue implementation.
 */
namespace sched
{

task::Task *wait_wake_one(WaitQueue *wq)
{
    if (!wq || !wq->head)
        return nullptr;

    // Remove first task from queue
    task::Task *t = wq->head;
    wq->head = t->next;

    if (wq->head)
    {
        wq->head->prev = nullptr;
    }
    else
    {
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

u32 wait_wake_all(WaitQueue *wq)
{
    if (!wq)
        return 0;

    u32 count = 0;

    while (wq->head)
    {
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

} // namespace sched
