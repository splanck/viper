//===----------------------------------------------------------------------===//
//
// File: src/runtime/threads/rt_scheduler.h
// Purpose: Poll-based task scheduler for named delayed tasks using the monotonic clock, where duplicate task names replace the previous registration.
//
// Key invariants:
//   - Poll-based, not thread-based; rt_scheduler_poll must be called regularly.
//   - Uses the monotonic clock; immune to wall-clock adjustments.
//   - Task names are unique; registering a duplicate name replaces the previous task.
//   - Tasks fire at most once per registration; use rt_scheduler_schedule to re-queue.
//
// Ownership/Lifetime:
//   - Scheduler objects are heap-allocated; caller is responsible for lifetime management.
//   - Task callback function pointers must remain valid until the task fires.
//
// Links: src/runtime/threads/rt_scheduler.c (implementation), src/runtime/core/rt_string.h
//
//===----------------------------------------------------------------------===//
#pragma once

#include "rt_string.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Create a new empty task scheduler.
    /// @return Pointer to new scheduler object.
    void *rt_scheduler_new(void);

    /// @brief Schedule a named task with a delay in milliseconds.
    /// @param sched Scheduler pointer.
    /// @param name Task name (string).
    /// @param delay_ms Delay in milliseconds from now.
    void rt_scheduler_schedule(void *sched, rt_string name, int64_t delay_ms);

    /// @brief Cancel a scheduled task by name.
    /// @param sched Scheduler pointer.
    /// @param name Task name to cancel.
    /// @return 1 if a task was cancelled, 0 if not found.
    int8_t rt_scheduler_cancel(void *sched, rt_string name);

    /// @brief Check if a named task is due (its delay has elapsed).
    /// @param sched Scheduler pointer.
    /// @param name Task name to check.
    /// @return 1 if due, 0 if not due or not found.
    int8_t rt_scheduler_is_due(void *sched, rt_string name);

    /// @brief Poll for all due tasks and return their names as a Seq.
    /// @param sched Scheduler pointer.
    /// @return Seq of task name strings that are due. Due tasks are removed.
    void *rt_scheduler_poll(void *sched);

    /// @brief Get the number of pending tasks (due and not-yet-due).
    /// @param sched Scheduler pointer.
    /// @return Count of scheduled tasks.
    int64_t rt_scheduler_pending(void *sched);

    /// @brief Clear all scheduled tasks.
    /// @param sched Scheduler pointer.
    void rt_scheduler_clear(void *sched);

#ifdef __cplusplus
}
#endif
