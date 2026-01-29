//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: kernel/sched/scheduler.hpp
// Purpose: Priority-based preemptive scheduler interface.
// Key invariants: 8 priority queues; higher priority always preempts.
// Ownership/Lifetime: Global singleton; started once, never returns.
// Links: kernel/sched/scheduler.cpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "task.hpp"

/**
 * @file scheduler.hpp
 * @brief Priority-based preemptive scheduler interface.
 *
 * @details
 * The scheduler is responsible for selecting which runnable task should
 * execute next and for performing context switches between tasks.
 *
 * The scheduler maintains 8 priority queues:
 * - Queue 0: Priorities 0-31 (highest priority)
 * - Queue 1: Priorities 32-63
 * - Queue 2: Priorities 64-95
 * - Queue 3: Priorities 96-127
 * - Queue 4: Priorities 128-159 (default tasks)
 * - Queue 5: Priorities 160-191
 * - Queue 6: Priorities 192-223
 * - Queue 7: Priorities 224-255 (idle task)
 *
 * Within each priority level, tasks are scheduled FIFO with time-slice
 * preemption. Higher-priority tasks always preempt lower-priority ones.
 */
namespace scheduler {

/**
 * @brief Initialize the scheduler.
 *
 * @details
 * Resets the ready queue, clears statistics, and marks the scheduler as not
 * running. Must be called before enqueuing tasks or starting scheduling.
 */
void init();

/**
 * @brief Add a task to the appropriate priority queue.
 *
 * @details
 * Inserts the task at the tail of its priority queue (based on task->priority)
 * and marks it Ready. Within each priority level, tasks are scheduled FIFO.
 *
 * @param t Task to enqueue.
 */
void enqueue(task::Task *t);

/**
 * @brief Remove and return the highest-priority ready task.
 *
 * @details
 * Checks priority queues from highest (0) to lowest (7) and returns the
 * first task found. Within a priority level, returns the oldest task (FIFO).
 *
 * @return Next task to run, or `nullptr` if all queues are empty.
 */
task::Task *dequeue();

/**
 * @brief Select the next task to run and perform a context switch.
 *
 * @details
 * Picks the next task from the ready queue. If no tasks are ready, the idle
 * task is selected. The current running task is re-enqueued if still runnable.
 *
 * This routine performs the actual context switch via `context_switch`.
 */
void schedule();

/**
 * @brief Per-tick accounting hook invoked from the timer interrupt.
 *
 * @details
 * Decrements the current task's time slice and may force a schedule if the
 * idle task is running while other tasks are ready.
 */
void tick();

/**
 * @brief Check whether the current task should be preempted and reschedule.
 *
 * @details
 * If the current task's time slice has reached zero and the scheduler is
 * running, invokes @ref schedule.
 */
void preempt();

/**
 * @brief Start scheduling by switching into the first runnable task.
 *
 * @details
 * Marks the scheduler running, selects the first task (or idle), and performs
 * an initial context switch. This function does not return.
 */
[[noreturn]] void start();

/**
 * @brief Check if the scheduler is running.
 *
 * @details
 * Returns true after start() has been called. Useful for code that needs
 * to behave differently during early boot (before the scheduler is active).
 *
 * @return true if scheduler is running, false otherwise.
 */
bool is_running();

/**
 * @brief Return the number of context switches performed.
 *
 * @return Context switch count.
 */
u64 get_context_switches();

/**
 * @brief Scheduler statistics structure.
 */
struct Stats {
    u64 context_switches; // Total context switches
    u32 queue_lengths[8]; // Current length of each priority queue
    u32 total_ready;      // Total tasks in all ready queues
    u32 blocked_tasks;    // Number of blocked tasks
    u32 exited_tasks;     // Number of exited (zombie) tasks
};

/**
 * @brief Per-CPU scheduler statistics.
 */
struct PerCpuStats {
    u64 context_switches; // Context switches on this CPU
    u32 queue_length;     // Tasks in this CPU's local queue
    u32 steals;           // Tasks stolen from other CPUs
    u32 migrations;       // Tasks migrated to other CPUs
};

/**
 * @brief Get current scheduler statistics.
 *
 * @param stats Output structure to receive statistics.
 */
void get_stats(Stats *stats);

/**
 * @brief Get the length of a specific priority queue.
 *
 * @param queue_idx Queue index (0-7).
 * @return Number of tasks in the queue.
 */
u32 get_queue_length(u8 queue_idx);

/**
 * @brief Dump scheduler statistics to serial console.
 *
 * @details
 * Prints queue lengths, context switch count, and task state summary.
 */
void dump_stats();

/**
 * @brief Enqueue a task on a specific CPU's run queue.
 *
 * @details
 * Used for task affinity or load balancing. If the target CPU is not
 * the current CPU, an IPI is sent to trigger rescheduling.
 *
 * @param t Task to enqueue.
 * @param cpu_id Target CPU ID.
 */
void enqueue_on_cpu(task::Task *t, u32 cpu_id);

/**
 * @brief Get per-CPU scheduler statistics.
 *
 * @param cpu_id CPU to query.
 * @param stats Output structure for statistics.
 */
void get_percpu_stats(u32 cpu_id, PerCpuStats *stats);

/**
 * @brief Perform load balancing across CPUs.
 *
 * @details
 * Called periodically to redistribute tasks from overloaded CPUs
 * to idle or underloaded CPUs. Uses work stealing algorithm.
 */
void balance_load();

/**
 * @brief Initialize per-CPU scheduler state for a secondary CPU.
 *
 * @param cpu_id The CPU ID being initialized.
 */
void init_cpu(u32 cpu_id);

} // namespace scheduler
