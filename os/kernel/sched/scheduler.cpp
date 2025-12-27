#include "scheduler.hpp"
#include "../console/serial.hpp"
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
 *
 * @return true if at least one task is ready.
 */
bool any_ready()
{
    for (u8 i = 0; i < task::NUM_PRIORITY_QUEUES; i++)
    {
        if (priority_queues[i].head)
            return true;
    }
    return false;
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

/** @copydoc scheduler::dequeue */
task::Task *dequeue()
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

/** @copydoc scheduler::schedule */
void schedule()
{
    task::Task *current = task::current();
    task::Task *next = dequeue();

    // If no task ready, use idle task (task 0)
    if (!next)
    {
        next = task::get_by_id(0); // Idle task
        if (!next || next == current)
        {
            // Already running idle or no idle task
            return;
        }
    }

    // If same task, nothing to do
    if (next == current)
    {
        // Re-enqueue if it was dequeued
        if (current->state == task::TaskState::Ready)
        {
            enqueue(current);
        }
        return;
    }

    // Put current task back in ready queue if it's still runnable
    if (current && current->state == task::TaskState::Running)
    {
        // Account for CPU time used (consumed time slice)
        u64 ticks_used = task::TIME_SLICE_DEFAULT - current->time_slice;
        current->cpu_ticks += ticks_used;

        current->state = task::TaskState::Ready;
        enqueue(current);
    }

    // Switch to next task
    next->state = task::TaskState::Running;
    next->time_slice = task::TIME_SLICE_DEFAULT;
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
    task::Task *old = current;
    task::set_current(next);

    // Switch address space if the next task is a user task with a different viper
    if (next->viper)
    {
        viper::Viper *v = reinterpret_cast<viper::Viper *>(next->viper);
        viper::switch_address_space(v->ttbr0, v->asid);
        viper::set_current(v);
    }

    // Perform context switch
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

    // Don't preempt idle task if something else is ready
    if (current->flags & task::TASK_FLAG_IDLE)
    {
        if (any_ready())
        {
            schedule();
        }
        return;
    }

    // Check if a higher-priority task became ready
    u8 current_queue = priority_to_queue(current->priority);
    for (u8 i = 0; i < current_queue; i++)
    {
        if (priority_queues[i].head)
        {
            // Higher priority task is ready - preempt immediately
            schedule();
            return;
        }
    }

    // Decrement time slice
    if (current->time_slice > 0)
    {
        current->time_slice--;
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

    // Check if time slice expired
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
    first->time_slice = task::TIME_SLICE_DEFAULT;
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

} // namespace scheduler
