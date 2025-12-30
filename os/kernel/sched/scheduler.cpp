#include "scheduler.hpp"
#include "../console/serial.hpp"
#include "../lib/spinlock.hpp"
#include "../viper/address_space.hpp"
#include "../viper/viper.hpp"
#include "task.hpp"

/**
 * @file scheduler.cpp
 * @brief Priority-based scheduler implementation.
 *
 * @details
 * This scheduler maintains 8 priority queues (0=highest, 7=lowest) and performs
 * context switches using the assembly `context_switch` routine.
 *
 * Priority mapping:
 * - Task priority 0-31   -> Queue 0 (highest)
 * - Task priority 32-63  -> Queue 1
 * - Task priority 64-95  -> Queue 2
 * - Task priority 96-127 -> Queue 3
 * - Task priority 128-159 -> Queue 4 (default tasks)
 * - Task priority 160-191 -> Queue 5
 * - Task priority 192-223 -> Queue 6
 * - Task priority 224-255 -> Queue 7 (idle task)
 *
 * Time slicing:
 * - Each task is given a fixed number of timer ticks (`TIME_SLICE_DEFAULT`).
 * - The timer interrupt decrements the counter and `preempt()` triggers a
 *   reschedule when it reaches zero.
 * - Tasks are preempted only by higher-priority tasks or when their slice expires.
 */
namespace scheduler
{

namespace
{
/**
 * @brief Per-priority ready queue.
 */
struct PriorityQueue
{
    task::Task *head;
    task::Task *tail;
};

// Scheduler lock - protects all queue operations and state transitions
// The spinlock automatically disables interrupts to prevent timer races
Spinlock sched_lock;

// 8 priority queues (0=highest, 7=lowest)
PriorityQueue priority_queues[task::NUM_PRIORITY_QUEUES];

// Statistics
u64 context_switch_count = 0;

// Scheduler running flag
bool running = false;

/**
 * @brief Map a task priority (0-255) to a queue index (0-7).
 *
 * @param priority Task priority value.
 * @return Queue index (0=highest priority, 7=lowest).
 */
inline u8 priority_to_queue(u8 priority)
{
    return priority / task::PRIORITIES_PER_QUEUE;
}

/**
 * @brief Check if any tasks are ready in any queue.
 * @note Caller must hold sched_lock.
 * @return true if at least one task is ready.
 */
bool any_ready_locked()
{
    for (u8 i = 0; i < task::NUM_PRIORITY_QUEUES; i++)
    {
        if (priority_queues[i].head)
            return true;
    }
    return false;
}

/**
 * @brief Internal enqueue without lock (caller must hold sched_lock).
 */
void enqueue_locked(task::Task *t)
{
    if (!t)
        return;

    // State validation: only Ready or Running tasks should be enqueued
    // (Running tasks become Ready when preempted)
    if (t->state != task::TaskState::Ready && t->state != task::TaskState::Running)
    {
        serial::puts("[sched] WARNING: enqueue task '");
        serial::puts(t->name);
        serial::puts("' in state ");
        serial::put_dec(static_cast<u32>(t->state));
        serial::puts(" (expected Ready/Running)\n");
        return; // Don't enqueue invalid state tasks
    }

    // Determine which priority queue this task belongs to
    u8 queue_idx = priority_to_queue(t->priority);
    PriorityQueue &queue = priority_queues[queue_idx];

    // Add to tail of the appropriate queue (FIFO within priority level)
    t->next = nullptr;
    t->prev = queue.tail;

    if (queue.tail)
    {
        queue.tail->next = t;
    }
    else
    {
        queue.head = t;
    }
    queue.tail = t;

    t->state = task::TaskState::Ready;
}

/**
 * @brief Internal dequeue without lock (caller must hold sched_lock).
 */
task::Task *dequeue_locked()
{
    // Check queues from highest priority (0) to lowest (7)
    for (u8 i = 0; i < task::NUM_PRIORITY_QUEUES; i++)
    {
        PriorityQueue &queue = priority_queues[i];

        if (queue.head)
        {
            task::Task *t = queue.head;
            queue.head = t->next;

            if (queue.head)
            {
                queue.head->prev = nullptr;
            }
            else
            {
                queue.tail = nullptr;
            }

            t->next = nullptr;
            t->prev = nullptr;

            return t;
        }
    }

    return nullptr;
}

} // namespace

/** @copydoc scheduler::init */
void init()
{
    serial::puts("[sched] Initializing priority scheduler\n");

    // Initialize all priority queues
    for (u8 i = 0; i < task::NUM_PRIORITY_QUEUES; i++)
    {
        priority_queues[i].head = nullptr;
        priority_queues[i].tail = nullptr;
    }

    context_switch_count = 0;
    running = false;

    serial::puts("[sched] Priority scheduler initialized (8 queues)\n");
}

/** @copydoc scheduler::enqueue */
void enqueue(task::Task *t)
{
    if (!t)
        return;

    SpinlockGuard guard(sched_lock);
    enqueue_locked(t);
}

/** @copydoc scheduler::dequeue */
task::Task *dequeue()
{
    SpinlockGuard guard(sched_lock);
    return dequeue_locked();
}

/** @copydoc scheduler::schedule */
void schedule()
{
    task::Task *current = task::current();
    task::Task *next = nullptr;
    task::Task *old = nullptr;

    // Critical section: queue manipulation and state transitions
    sched_lock.acquire();

    next = dequeue_locked();

    // If no task ready, use idle task (task 0)
    if (!next)
    {
        next = task::get_by_id(0); // Idle task
        if (!next || next == current)
        {
            // Already running idle or no idle task
            sched_lock.release();
            return;
        }
    }

    // If same task, nothing to do
    if (next == current)
    {
        // Re-enqueue if it was dequeued
        if (current->state == task::TaskState::Ready)
        {
            enqueue_locked(current);
        }
        sched_lock.release();
        return;
    }

    // Put current task back in ready queue if it's still runnable
    if (current)
    {
        if (current->state == task::TaskState::Running)
        {
            // Account for CPU time used (consumed time slice)
            u32 original_slice = task::time_slice_for_priority(current->priority);
            u64 ticks_used = original_slice - current->time_slice;
            current->cpu_ticks += ticks_used;

            current->state = task::TaskState::Ready;
            enqueue_locked(current);
        }
        else if (current->state == task::TaskState::Exited)
        {
            // Task exited - don't re-enqueue
            // Serial output for debugging
            if (context_switch_count <= 10)
            {
                serial::puts("[sched] Task '");
                serial::puts(current->name);
                serial::puts("' exited\n");
            }
        }
        // Blocked tasks are on wait queues, not re-enqueued here
    }

    // Validate next task state before switching
    if (next->state != task::TaskState::Ready && next != task::get_by_id(0))
    {
        serial::puts("[sched] ERROR: next task '");
        serial::puts(next->name);
        serial::puts("' not Ready (state=");
        serial::put_dec(static_cast<u32>(next->state));
        serial::puts(")\n");
        sched_lock.release();
        return;
    }

    // Switch to next task
    next->state = task::TaskState::Running;
    next->time_slice = task::time_slice_for_priority(next->priority);
    next->switch_count++;

    context_switch_count++;

    // Debug output (first 5 switches only)
    if (context_switch_count <= 5)
    {
        serial::puts("[sched] ");
        if (current)
        {
            serial::puts(current->name);
        }
        else
        {
            serial::puts("(none)");
        }
        serial::puts(" -> ");
        serial::puts(next->name);
        serial::puts("\n");
    }

    // Update current task pointer
    old = current;
    task::set_current(next);

    // Switch address space if the next task is a user task with a different viper
    if (next->viper)
    {
        viper::Viper *v = reinterpret_cast<viper::Viper *>(next->viper);
        viper::switch_address_space(v->ttbr0, v->asid);
        viper::set_current(v);
    }

    // Release lock before context switch - the new task will run with interrupts enabled
    sched_lock.release();

    // Perform context switch (with interrupts enabled)
    if (old)
    {
        context_switch(&old->context, &next->context);
    }
    else
    {
        // First switch - just load new context
        // This is handled by start()
        context_switch(&next->context, &next->context);
    }
}

/** @copydoc scheduler::tick */
void tick()
{
    // Don't do anything until scheduler has started
    if (!running)
        return;

    task::Task *current = task::current();
    if (!current)
        return;

    bool need_schedule = false;

    // Quick check for preemption (with lock for queue access)
    {
        SpinlockGuard guard(sched_lock);

        // Don't preempt idle task if something else is ready
        if (current->flags & task::TASK_FLAG_IDLE)
        {
            if (any_ready_locked())
            {
                need_schedule = true;
            }
        }
        else
        {
            // Check if a higher-priority task became ready
            u8 current_queue = priority_to_queue(current->priority);
            for (u8 i = 0; i < current_queue; i++)
            {
                if (priority_queues[i].head)
                {
                    // Higher priority task is ready - preempt immediately
                    need_schedule = true;
                    break;
                }
            }

            // Decrement time slice
            if (!need_schedule && current->time_slice > 0)
            {
                current->time_slice--;
            }
        }
    }

    // Schedule outside the lock (schedule() acquires its own lock)
    if (need_schedule)
    {
        schedule();
    }
}

/** @copydoc scheduler::preempt */
void preempt()
{
    // Don't do anything until scheduler has started
    if (!running)
        return;

    task::Task *current = task::current();
    if (!current)
        return;

    // Check if time slice expired (read atomically)
    // No lock needed - time_slice is only modified by the owning task or tick()
    if (current->time_slice == 0)
    {
        schedule();
    }
}

/** @copydoc scheduler::start */
[[noreturn]] void start()
{
    serial::puts("[sched] Starting scheduler\n");

    // Disable interrupts while setting up - prevents timer from
    // calling schedule() before we've switched to the first task
    asm volatile("msr daifset, #2"); // Mask IRQ

    running = true;

    // Get first task from ready queue
    task::Task *first = dequeue();
    if (!first)
    {
        // No tasks, run idle
        first = task::get_by_id(0);
    }

    if (!first)
    {
        serial::puts("[sched] PANIC: No tasks to run!\n");
        for (;;)
            asm volatile("wfi");
    }

    serial::puts("[sched] First task: ");
    serial::puts(first->name);
    serial::puts("\n");

    // Set as current and running
    first->state = task::TaskState::Running;
    first->time_slice = task::time_slice_for_priority(first->priority);
    task::set_current(first);

    context_switch_count++;

    // Load the first task's context and jump to it
    // We create a dummy "old" context on the stack that we don't care about
    task::TaskContext dummy;

    // Re-enable interrupts just before switch
    // The new task will start with interrupts enabled
    asm volatile("msr daifclr, #2"); // Unmask IRQ

    context_switch(&dummy, &first->context);

    // Should never return
    serial::puts("[sched] PANIC: start() returned!\n");
    for (;;)
        asm volatile("wfi");
}

/** @copydoc scheduler::get_context_switches */
u64 get_context_switches()
{
    return context_switch_count;
}

/** @copydoc scheduler::get_queue_length */
u32 get_queue_length(u8 queue_idx)
{
    if (queue_idx >= task::NUM_PRIORITY_QUEUES)
        return 0;

    SpinlockGuard guard(sched_lock);

    u32 count = 0;
    task::Task *t = priority_queues[queue_idx].head;
    while (t)
    {
        count++;
        t = t->next;
    }
    return count;
}

/** @copydoc scheduler::get_stats */
void get_stats(Stats *stats)
{
    if (!stats)
        return;

    SpinlockGuard guard(sched_lock);

    stats->context_switches = context_switch_count;
    stats->total_ready = 0;
    stats->blocked_tasks = 0;
    stats->exited_tasks = 0;

    // Count tasks in each priority queue
    for (u8 i = 0; i < task::NUM_PRIORITY_QUEUES; i++)
    {
        u32 count = 0;
        task::Task *t = priority_queues[i].head;
        while (t)
        {
            count++;
            t = t->next;
        }
        stats->queue_lengths[i] = count;
        stats->total_ready += count;
    }

    // Count blocked and exited tasks by scanning task table
    for (u32 i = 0; i < task::MAX_TASKS; i++)
    {
        task::Task *t = task::get_by_id(i);
        if (t)
        {
            if (t->state == task::TaskState::Blocked)
                stats->blocked_tasks++;
            else if (t->state == task::TaskState::Exited)
                stats->exited_tasks++;
        }
    }
}

/** @copydoc scheduler::dump_stats */
void dump_stats()
{
    Stats stats;
    get_stats(&stats);

    serial::puts("\n=== Scheduler Statistics ===\n");
    serial::puts("Context switches: ");
    serial::put_dec(stats.context_switches);
    serial::puts("\n");

    serial::puts("Ready queues:\n");
    for (u8 i = 0; i < task::NUM_PRIORITY_QUEUES; i++)
    {
        serial::puts("  Queue ");
        serial::put_dec(i);
        serial::puts(" (pri ");
        serial::put_dec(i * task::PRIORITIES_PER_QUEUE);
        serial::puts("-");
        serial::put_dec((i + 1) * task::PRIORITIES_PER_QUEUE - 1);
        serial::puts("): ");
        serial::put_dec(stats.queue_lengths[i]);
        serial::puts(" tasks, slice=");
        serial::put_dec(task::TIME_SLICE_BY_QUEUE[i]);
        serial::puts("ms\n");
    }

    serial::puts("Total ready: ");
    serial::put_dec(stats.total_ready);
    serial::puts(", Blocked: ");
    serial::put_dec(stats.blocked_tasks);
    serial::puts(", Exited: ");
    serial::put_dec(stats.exited_tasks);
    serial::puts("\n===========================\n");
}

} // namespace scheduler
