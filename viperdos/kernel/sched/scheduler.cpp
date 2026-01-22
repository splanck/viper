#include "scheduler.hpp"
#include "../arch/aarch64/cpu.hpp"
#include "../console/serial.hpp"
#include "../lib/spinlock.hpp"
#include "../viper/address_space.hpp"
#include "../viper/viper.hpp"
#include "cfs.hpp"
#include "deadline.hpp"
#include "idle.hpp"
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
 *
 * Lock Ordering (to prevent deadlocks):
 * - Always acquire sched_lock before per-CPU locks
 * - Ordering: sched_lock -> per_cpu_sched[N].lock
 * - Release in reverse order
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

/**
 * @brief Per-CPU scheduler state.
 *
 * Each CPU has its own set of priority queues and statistics for
 * reduced contention in SMP systems.
 */
struct PerCpuScheduler
{
    PriorityQueue queues[task::NUM_PRIORITY_QUEUES];
    Spinlock lock;
    u64 context_switches;
    u32 total_tasks;
    u32 steals;
    u32 migrations;
    bool initialized;
};

// Per-CPU scheduler state
PerCpuScheduler per_cpu_sched[cpu::MAX_CPUS];

// Global scheduler lock - protects global operations and fallback
// The spinlock automatically disables interrupts to prevent timer races
Spinlock sched_lock;

// 8 priority queues (0=highest, 7=lowest) - used for initial boot and global operations
PriorityQueue priority_queues[task::NUM_PRIORITY_QUEUES];

// Statistics (use atomics for SMP-safe access)
// Note: Use volatile to prevent compiler optimization and __atomic for SMP safety
volatile u64 context_switch_count = 0;

// Scheduler running flag
bool running = false;

// Load balancing interval (ticks)
constexpr u32 LOAD_BALANCE_INTERVAL = 100;
volatile u32 load_balance_counter = 0;

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
    u32 cpu_id = cpu::current_id();
    u32 cpu_mask = (1u << cpu_id);

    // First pass: find earliest deadline task (SCHED_DEADLINE has highest priority)
    task::Task *dl_best = nullptr;
    for (u8 i = 0; i < task::NUM_PRIORITY_QUEUES; i++)
    {
        task::Task *t = priority_queues[i].head;
        while (t)
        {
            if ((t->cpu_affinity & cpu_mask) &&
                t->policy == task::SchedPolicy::SCHED_DEADLINE)
            {
                if (!dl_best || deadline::earlier_deadline(t, dl_best))
                {
                    dl_best = t;
                }
            }
            t = t->next;
        }
    }

    // If we found a deadline task, return it
    if (dl_best)
    {
        PriorityQueue &queue = priority_queues[priority_to_queue(dl_best->priority)];

        if (dl_best->prev)
            dl_best->prev->next = dl_best->next;
        else
            queue.head = dl_best->next;

        if (dl_best->next)
            dl_best->next->prev = dl_best->prev;
        else
            queue.tail = dl_best->prev;

        dl_best->next = nullptr;
        dl_best->prev = nullptr;
        return dl_best;
    }

    // Check queues from highest priority (0) to lowest (7) for RT and SCHED_OTHER
    for (u8 i = 0; i < task::NUM_PRIORITY_QUEUES; i++)
    {
        PriorityQueue &queue = priority_queues[i];
        if (!queue.head)
            continue;

        // For CFS: find the task with lowest vruntime that can run on this CPU
        // RT tasks (SCHED_FIFO/RR) still use FIFO ordering
        task::Task *best = nullptr;
        task::Task *t = queue.head;

        while (t)
        {
            // Check CPU affinity - can this task run on current CPU?
            if (t->cpu_affinity & cpu_mask)
            {
                // Skip deadline tasks (handled above)
                if (t->policy == task::SchedPolicy::SCHED_DEADLINE)
                {
                    t = t->next;
                    continue;
                }

                // RT tasks: take first one (FIFO within priority)
                if (t->policy == task::SchedPolicy::SCHED_FIFO ||
                    t->policy == task::SchedPolicy::SCHED_RR)
                {
                    best = t;
                    break;
                }

                // SCHED_OTHER: select by lowest vruntime (CFS)
                if (!best || t->vruntime < best->vruntime)
                {
                    best = t;
                }
            }
            t = t->next;
        }

        if (best)
        {
            // Remove best from queue
            if (best->prev)
            {
                best->prev->next = best->next;
            }
            else
            {
                queue.head = best->next;
            }

            if (best->next)
            {
                best->next->prev = best->prev;
            }
            else
            {
                queue.tail = best->prev;
            }

            best->next = nullptr;
            best->prev = nullptr;

            return best;
        }
    }

    return nullptr;
}

/**
 * @brief Enqueue a task on a specific CPU's queue.
 * @note Caller must hold the per-CPU lock.
 */
void enqueue_percpu_locked(task::Task *t, u32 cpu_id)
{
    if (!t || cpu_id >= cpu::MAX_CPUS)
        return;

    if (!per_cpu_sched[cpu_id].initialized)
    {
        // Fall back to global queue if per-CPU not initialized
        enqueue_locked(t);
        return;
    }

    // State validation
    if (t->state != task::TaskState::Ready && t->state != task::TaskState::Running)
    {
        return;
    }

    u8 queue_idx = priority_to_queue(t->priority);
    PriorityQueue &queue = per_cpu_sched[cpu_id].queues[queue_idx];

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
    per_cpu_sched[cpu_id].total_tasks++;
}

/**
 * @brief Dequeue the highest priority task from a specific CPU's queue.
 * @note Caller must hold the per-CPU lock.
 */
task::Task *dequeue_percpu_locked(u32 cpu_id)
{
    if (cpu_id >= cpu::MAX_CPUS || !per_cpu_sched[cpu_id].initialized)
    {
        return dequeue_locked(); // Fall back to global
    }

    u32 cpu_mask = (1u << cpu_id);
    PerCpuScheduler &sched = per_cpu_sched[cpu_id];

    // First pass: find earliest deadline task (SCHED_DEADLINE has highest priority)
    task::Task *dl_best = nullptr;
    for (u8 i = 0; i < task::NUM_PRIORITY_QUEUES; i++)
    {
        task::Task *t = sched.queues[i].head;
        while (t)
        {
            if ((t->cpu_affinity & cpu_mask) &&
                t->policy == task::SchedPolicy::SCHED_DEADLINE)
            {
                if (!dl_best || deadline::earlier_deadline(t, dl_best))
                {
                    dl_best = t;
                }
            }
            t = t->next;
        }
    }

    // If we found a deadline task, return it
    if (dl_best)
    {
        PriorityQueue &queue = sched.queues[priority_to_queue(dl_best->priority)];

        if (dl_best->prev)
            dl_best->prev->next = dl_best->next;
        else
            queue.head = dl_best->next;

        if (dl_best->next)
            dl_best->next->prev = dl_best->prev;
        else
            queue.tail = dl_best->prev;

        dl_best->next = nullptr;
        dl_best->prev = nullptr;
        sched.total_tasks--;
        return dl_best;
    }

    // Check queues for RT and SCHED_OTHER tasks
    for (u8 i = 0; i < task::NUM_PRIORITY_QUEUES; i++)
    {
        PriorityQueue &queue = sched.queues[i];
        if (!queue.head)
            continue;

        // For CFS: find the task with lowest vruntime that can run on this CPU
        // RT tasks (SCHED_FIFO/RR) still use FIFO ordering
        task::Task *best = nullptr;
        task::Task *t = queue.head;

        while (t)
        {
            // Check CPU affinity
            if (t->cpu_affinity & cpu_mask)
            {
                // Skip deadline tasks (handled above)
                if (t->policy == task::SchedPolicy::SCHED_DEADLINE)
                {
                    t = t->next;
                    continue;
                }

                // RT tasks: take first one (FIFO within priority)
                if (t->policy == task::SchedPolicy::SCHED_FIFO ||
                    t->policy == task::SchedPolicy::SCHED_RR)
                {
                    best = t;
                    break;
                }

                // SCHED_OTHER: select by lowest vruntime (CFS)
                if (!best || t->vruntime < best->vruntime)
                {
                    best = t;
                }
            }
            t = t->next;
        }

        if (best)
        {
            // Remove best from queue
            if (best->prev)
            {
                best->prev->next = best->next;
            }
            else
            {
                queue.head = best->next;
            }

            if (best->next)
            {
                best->next->prev = best->prev;
            }
            else
            {
                queue.tail = best->prev;
            }

            best->next = nullptr;
            best->prev = nullptr;
            sched.total_tasks--;

            return best;
        }
    }

    return nullptr;
}

/**
 * @brief Try to steal a task from another CPU's queue.
 * @return Stolen task, or nullptr if none available.
 */
task::Task *steal_task(u32 current_cpu)
{
    u32 cpu_mask = (1u << current_cpu);

    // Try each other CPU
    for (u32 i = 0; i < cpu::MAX_CPUS; i++)
    {
        if (i == current_cpu)
            continue;

        PerCpuScheduler &victim = per_cpu_sched[i];
        if (!victim.initialized || victim.total_tasks < 2)
            continue;

        // Try to acquire victim's lock without blocking
        u64 saved_daif;
        if (!victim.lock.try_acquire(saved_daif))
            continue;

        // Steal from the lowest priority queue that has tasks
        // (don't steal high-priority or RT tasks)
        for (i8 q = task::NUM_PRIORITY_QUEUES - 1; q >= 4; q--)
        {
            PriorityQueue &queue = victim.queues[q];

            // Find a stealable task that can run on current CPU
            task::Task *t = queue.tail;
            while (t && t != queue.head)
            {
                // Check CPU affinity before stealing
                if (t->cpu_affinity & cpu_mask)
                {
                    // Remove from queue
                    if (t->prev)
                    {
                        t->prev->next = t->next;
                    }
                    else
                    {
                        queue.head = t->next;
                    }

                    if (t->next)
                    {
                        t->next->prev = t->prev;
                    }
                    else
                    {
                        queue.tail = t->prev;
                    }

                    t->next = nullptr;
                    t->prev = nullptr;
                    victim.total_tasks--;
                    victim.migrations++;

                    victim.lock.release(saved_daif);

                    // Update steal counter atomically to avoid race with other CPUs
                    // Since this is our own CPU's counter, we just need atomicity
                    __atomic_fetch_add(&per_cpu_sched[current_cpu].steals, 1, __ATOMIC_RELAXED);
                    return t;
                }
                t = t->prev;
            }
        }

        victim.lock.release(saved_daif);
    }

    return nullptr;
}

/**
 * @brief Check if any tasks are ready on the current CPU.
 * @note Caller must hold sched_lock. This function acquires per-CPU lock internally.
 */
bool any_ready_percpu(u32 cpu_id)
{
    if (cpu_id >= cpu::MAX_CPUS || !per_cpu_sched[cpu_id].initialized)
    {
        return any_ready_locked();
    }

    PerCpuScheduler &sched = per_cpu_sched[cpu_id];

    // Lock ordering: sched_lock (caller holds) -> per-CPU lock
    u64 saved_daif = sched.lock.acquire();
    bool has_ready = false;
    for (u8 i = 0; i < task::NUM_PRIORITY_QUEUES; i++)
    {
        if (sched.queues[i].head)
        {
            has_ready = true;
            break;
        }
    }
    sched.lock.release(saved_daif);

    return has_ready;
}

} // namespace

/** @copydoc scheduler::init */
void init()
{
    serial::puts("[sched] Initializing priority scheduler\n");

    // Initialize all global priority queues
    for (u8 i = 0; i < task::NUM_PRIORITY_QUEUES; i++)
    {
        priority_queues[i].head = nullptr;
        priority_queues[i].tail = nullptr;
    }

    // Initialize per-CPU scheduler state
    for (u32 c = 0; c < cpu::MAX_CPUS; c++)
    {
        for (u8 i = 0; i < task::NUM_PRIORITY_QUEUES; i++)
        {
            per_cpu_sched[c].queues[i].head = nullptr;
            per_cpu_sched[c].queues[i].tail = nullptr;
        }
        per_cpu_sched[c].context_switches = 0;
        per_cpu_sched[c].total_tasks = 0;
        per_cpu_sched[c].steals = 0;
        per_cpu_sched[c].migrations = 0;
        per_cpu_sched[c].initialized = false;
    }

    // Initialize boot CPU (CPU 0)
    per_cpu_sched[0].initialized = true;

    __atomic_store_n(&context_switch_count, 0, __ATOMIC_RELAXED);
    running = false;

    // Initialize idle state tracking
    idle::init();

    serial::puts("[sched] Priority scheduler initialized (8 queues, per-CPU support)\n");
}

/** @copydoc scheduler::init_cpu */
void init_cpu(u32 cpu_id)
{
    if (cpu_id >= cpu::MAX_CPUS)
        return;

    PerCpuScheduler &sched = per_cpu_sched[cpu_id];

    for (u8 i = 0; i < task::NUM_PRIORITY_QUEUES; i++)
    {
        sched.queues[i].head = nullptr;
        sched.queues[i].tail = nullptr;
    }
    sched.context_switches = 0;
    sched.total_tasks = 0;
    sched.steals = 0;
    sched.migrations = 0;
    sched.initialized = true;

    serial::puts("[sched] CPU ");
    serial::put_dec(cpu_id);
    serial::puts(" scheduler initialized\n");
}

/** @copydoc scheduler::enqueue */
void enqueue(task::Task *t)
{
    if (!t)
        return;

    u32 cpu_id = cpu::current_id();

    // Use per-CPU queue if available
    if (cpu_id < cpu::MAX_CPUS && per_cpu_sched[cpu_id].initialized)
    {
        SpinlockGuard guard(per_cpu_sched[cpu_id].lock);
        enqueue_percpu_locked(t, cpu_id);
    }
    else
    {
        SpinlockGuard guard(sched_lock);
        enqueue_locked(t);
    }
}

/** @copydoc scheduler::dequeue */
task::Task *dequeue()
{
    u32 cpu_id = cpu::current_id();

    // Try per-CPU queue first
    if (cpu_id < cpu::MAX_CPUS && per_cpu_sched[cpu_id].initialized)
    {
        SpinlockGuard guard(per_cpu_sched[cpu_id].lock);
        task::Task *t = dequeue_percpu_locked(cpu_id);
        if (t)
            return t;

        // Try work stealing from other CPUs
        t = steal_task(cpu_id);
        if (t)
            return t;
    }

    // Fall back to global queue
    SpinlockGuard guard(sched_lock);
    return dequeue_locked();
}

/** @copydoc scheduler::schedule */
void schedule()
{
    task::Task *current = task::current();
    task::Task *next = nullptr;
    task::Task *old = nullptr;
    u32 cpu_id = cpu::current_id();

    // Try per-CPU queue first (with its own lock)
    if (cpu_id < cpu::MAX_CPUS && per_cpu_sched[cpu_id].initialized)
    {
        SpinlockGuard guard(per_cpu_sched[cpu_id].lock);
        next = dequeue_percpu_locked(cpu_id);
    }

    // Critical section: queue manipulation and state transitions
    u64 sched_saved_daif = sched_lock.acquire();

    // Fall back to global queue if no per-CPU task found
    if (!next)
    {
        next = dequeue_locked();
    }

    // If no task ready, use idle task (task 0)
    if (!next)
    {
        next = task::get_by_id(0); // Idle task
        if (!next || next == current)
        {
            // Already running idle or no idle task
            sched_lock.release(sched_saved_daif);
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
        sched_lock.release(sched_saved_daif);
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
            if (__atomic_load_n(&context_switch_count, __ATOMIC_RELAXED) <= 10)
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
        sched_lock.release(sched_saved_daif);
        return;
    }

    // Switch to next task
    next->state = task::TaskState::Running;

    // Set time slice based on scheduling policy
    if (next->policy == task::SchedPolicy::SCHED_DEADLINE)
    {
        // SCHED_DEADLINE: Time slice from runtime budget (ns -> ticks, 1 tick = 1ms)
        next->time_slice = static_cast<u32>(next->dl_runtime / 1000000);
        if (next->time_slice == 0)
            next->time_slice = 1; // Minimum 1 tick
    }
    else if (next->policy == task::SchedPolicy::SCHED_FIFO)
    {
        // SCHED_FIFO: Infinite time slice (run until yield/block)
        next->time_slice = 0xFFFFFFFF;
    }
    else if (next->policy == task::SchedPolicy::SCHED_RR)
    {
        // SCHED_RR: Fixed RT time slice
        next->time_slice = task::RT_TIME_SLICE_DEFAULT;
    }
    else
    {
        // SCHED_OTHER: Priority-based time slice
        next->time_slice = task::time_slice_for_priority(next->priority);
    }

    next->switch_count++;

    u64 switch_num = __atomic_fetch_add(&context_switch_count, 1, __ATOMIC_RELAXED) + 1;

    // Debug output (first 5 switches only)
    if (switch_num <= 5)
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
    sched_lock.release(sched_saved_daif);

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
    u32 cpu_id = cpu::current_id();

    // Quick check for preemption (with lock for queue access)
    {
        SpinlockGuard guard(sched_lock);

        // Don't preempt idle task if something else is ready
        if (current->flags & task::TASK_FLAG_IDLE)
        {
            if (any_ready_percpu(cpu_id) || any_ready_locked())
            {
                need_schedule = true;
            }
        }
        else
        {
            // Check if a higher-priority task became ready
            u8 current_queue = priority_to_queue(current->priority);
            bool current_is_rt = (current->policy == task::SchedPolicy::SCHED_FIFO ||
                                  current->policy == task::SchedPolicy::SCHED_RR);

            for (u8 i = 0; i < current_queue; i++)
            {
                // Check both per-CPU and global queues for higher priority tasks
                task::Task *ready = nullptr;

                // Check per-CPU queue - must acquire per-CPU lock
                // Lock ordering: sched_lock (already held) -> per-CPU lock
                if (per_cpu_sched[cpu_id].initialized)
                {
                    u64 percpu_saved_daif = per_cpu_sched[cpu_id].lock.acquire();
                    ready = per_cpu_sched[cpu_id].queues[i].head;
                    per_cpu_sched[cpu_id].lock.release(percpu_saved_daif);
                }
                if (!ready)
                {
                    ready = priority_queues[i].head;
                }

                if (ready)
                {
                    // Check if the ready task is RT - RT always preempts non-RT
                    bool ready_is_rt = (ready->policy == task::SchedPolicy::SCHED_FIFO ||
                                        ready->policy == task::SchedPolicy::SCHED_RR);

                    if (ready_is_rt && !current_is_rt)
                    {
                        // RT task preempts non-RT task
                        need_schedule = true;
                        break;
                    }
                    else if (i < current_queue)
                    {
                        // Higher priority task is ready - preempt
                        need_schedule = true;
                        break;
                    }
                }
            }

            // Handle time slice based on scheduling policy
            if (!need_schedule)
            {
                if (current->policy == task::SchedPolicy::SCHED_FIFO)
                {
                    // SCHED_FIFO: Never preempt on time slice (run until yield/block)
                    // Don't decrement time_slice
                }
                else if (current->policy == task::SchedPolicy::SCHED_RR)
                {
                    // SCHED_RR: Round-robin with fixed RT time slice
                    if (current->time_slice > 0)
                    {
                        current->time_slice--;
                    }
                }
                else
                {
                    // SCHED_OTHER: Normal time-sharing with CFS vruntime
                    if (current->time_slice > 0)
                    {
                        current->time_slice--;

                        // Update vruntime: 1 tick = 1ms = 1,000,000ns
                        // vruntime is scaled by weight (higher nice = faster vruntime growth)
                        u64 delta_ns = 1000000; // 1ms per tick
                        current->vruntime += cfs::calc_vruntime_delta(delta_ns, current->nice);
                    }
                }
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

    // SCHED_FIFO tasks are never preempted by time slice expiry
    // They run until they voluntarily yield or block
    if (current->policy == task::SchedPolicy::SCHED_FIFO)
    {
        return;
    }

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

    // Set time slice based on scheduling policy
    if (first->policy == task::SchedPolicy::SCHED_DEADLINE)
    {
        first->time_slice = static_cast<u32>(first->dl_runtime / 1000000);
        if (first->time_slice == 0)
            first->time_slice = 1;
    }
    else if (first->policy == task::SchedPolicy::SCHED_FIFO)
    {
        first->time_slice = 0xFFFFFFFF;
    }
    else if (first->policy == task::SchedPolicy::SCHED_RR)
    {
        first->time_slice = task::RT_TIME_SLICE_DEFAULT;
    }
    else
    {
        first->time_slice = task::time_slice_for_priority(first->priority);
    }

    task::set_current(first);

    __atomic_fetch_add(&context_switch_count, 1, __ATOMIC_RELAXED);

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
    return __atomic_load_n(&context_switch_count, __ATOMIC_RELAXED);
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

    stats->context_switches = __atomic_load_n(&context_switch_count, __ATOMIC_RELAXED);
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

/** @copydoc scheduler::enqueue_on_cpu */
void enqueue_on_cpu(task::Task *t, u32 cpu_id)
{
    if (!t || cpu_id >= cpu::MAX_CPUS)
        return;

    u32 current_cpu = cpu::current_id();

    // Use per-CPU lock if the target CPU's scheduler is initialized
    if (per_cpu_sched[cpu_id].initialized)
    {
        SpinlockGuard guard(per_cpu_sched[cpu_id].lock);
        enqueue_percpu_locked(t, cpu_id);
    }
    else
    {
        // Fall back to global queue
        SpinlockGuard guard(sched_lock);
        enqueue_locked(t);
    }

    // If enqueuing to a different CPU, send an IPI to trigger reschedule
    if (cpu_id != current_cpu)
    {
        cpu::send_ipi(cpu_id, cpu::ipi::RESCHEDULE);
    }
}

/** @copydoc scheduler::get_percpu_stats */
void get_percpu_stats(u32 cpu_id, PerCpuStats *stats)
{
    if (!stats || cpu_id >= cpu::MAX_CPUS)
        return;

    if (!per_cpu_sched[cpu_id].initialized)
    {
        stats->context_switches = 0;
        stats->queue_length = 0;
        stats->steals = 0;
        stats->migrations = 0;
        return;
    }

    SpinlockGuard guard(per_cpu_sched[cpu_id].lock);
    stats->context_switches = per_cpu_sched[cpu_id].context_switches;
    stats->queue_length = per_cpu_sched[cpu_id].total_tasks;
    stats->steals = per_cpu_sched[cpu_id].steals;
    stats->migrations = per_cpu_sched[cpu_id].migrations;
}

/** @copydoc scheduler::balance_load */
void balance_load()
{
    u32 current_cpu = cpu::current_id();

    // Only run load balancing periodically (atomic increment for SMP safety)
    u32 counter = __atomic_fetch_add(&load_balance_counter, 1, __ATOMIC_RELAXED) + 1;
    if (counter < LOAD_BALANCE_INTERVAL)
        return;
    __atomic_store_n(&load_balance_counter, 0, __ATOMIC_RELAXED);

    // Find the most and least loaded CPUs
    u32 max_load = 0, min_load = 0xFFFFFFFF;
    u32 max_cpu = current_cpu, min_cpu = current_cpu;

    for (u32 i = 0; i < cpu::MAX_CPUS; i++)
    {
        if (!per_cpu_sched[i].initialized)
            continue;

        u32 load = per_cpu_sched[i].total_tasks;
        if (load > max_load)
        {
            max_load = load;
            max_cpu = i;
        }
        if (load < min_load)
        {
            min_load = load;
            min_cpu = i;
        }
    }

    // Migrate tasks if imbalance is significant (>2 tasks difference)
    if (max_load > min_load + 2 && max_cpu != min_cpu)
    {
        // Try to steal a task from the overloaded CPU
        task::Task *stolen = steal_task(min_cpu);
        if (stolen)
        {
            enqueue_on_cpu(stolen, min_cpu);
        }
    }
}

} // namespace scheduler
