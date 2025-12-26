#include "scheduler.hpp"
#include "../console/serial.hpp"
#include "task.hpp"

/**
 * @file scheduler.cpp
 * @brief Simple FIFO scheduler implementation.
 *
 * @details
 * This scheduler maintains a FIFO ready queue of runnable tasks and performs
 * context switches using the assembly `context_switch` routine.
 *
 * Time slicing:
 * - Each task is given a fixed number of timer ticks (`TIME_SLICE_DEFAULT`).
 * - The timer interrupt decrements the counter and `preempt()` triggers a
 *   reschedule when it reaches zero.
 *
 * This design is intentionally minimal and appropriate for bring-up. It does
 * not yet implement priorities, fairness across CPUs, or sophisticated
 * blocking/wakeup mechanisms beyond cooperative yielding.
 */
namespace scheduler
{

namespace
{
// Ready queue (simple FIFO linked list)
task::Task *ready_head = nullptr;
task::Task *ready_tail = nullptr;

// Statistics
u64 context_switch_count = 0;

// Scheduler running flag
bool running = false;
} // namespace

/** @copydoc scheduler::init */
void init()
{
    serial::puts("[sched] Initializing scheduler\n");

    ready_head = nullptr;
    ready_tail = nullptr;
    context_switch_count = 0;
    running = false;

    serial::puts("[sched] Scheduler initialized\n");
}

/** @copydoc scheduler::enqueue */
void enqueue(task::Task *t)
{
    if (!t)
        return;

    t->next = nullptr;
    t->prev = ready_tail;

    if (ready_tail)
    {
        ready_tail->next = t;
    }
    else
    {
        ready_head = t;
    }
    ready_tail = t;

    t->state = task::TaskState::Ready;
}

/** @copydoc scheduler::dequeue */
task::Task *dequeue()
{
    if (!ready_head)
        return nullptr;

    task::Task *t = ready_head;
    ready_head = t->next;

    if (ready_head)
    {
        ready_head->prev = nullptr;
    }
    else
    {
        ready_tail = nullptr;
    }

    t->next = nullptr;
    t->prev = nullptr;

    return t;
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
        current->state = task::TaskState::Ready;
        enqueue(current);
    }

    // Switch to next task
    next->state = task::TaskState::Running;
    next->time_slice = task::TIME_SLICE_DEFAULT;

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
        if (ready_head)
        {
            schedule();
        }
        return;
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
