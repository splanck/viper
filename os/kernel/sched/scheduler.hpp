#pragma once

#include "task.hpp"

/**
 * @file scheduler.hpp
 * @brief Cooperative scheduler interface.
 *
 * @details
 * The scheduler is responsible for selecting which runnable task should
 * execute next and for performing context switches between tasks.
 *
 * The current scheduler implementation is a simple FIFO ready queue with a
 * time-slice counter used for preemption checks. It is intended for early
 * kernel bring-up and demonstration rather than production-grade scheduling.
 */
namespace scheduler
{

/**
 * @brief Initialize the scheduler.
 *
 * @details
 * Resets the ready queue, clears statistics, and marks the scheduler as not
 * running. Must be called before enqueuing tasks or starting scheduling.
 */
void init();

/**
 * @brief Add a task to the ready queue.
 *
 * @details
 * Inserts the task at the tail of the FIFO queue and marks it Ready. The task
 * must not already be present in the queue.
 *
 * @param t Task to enqueue.
 */
void enqueue(task::Task *t);

/**
 * @brief Remove and return the next task from the ready queue.
 *
 * @details
 * Dequeues from the head of the FIFO queue.
 *
 * @return Next task, or `nullptr` if no task is ready.
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
 * @brief Return the number of context switches performed.
 *
 * @return Context switch count.
 */
u64 get_context_switches();

} // namespace scheduler
