//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: rt_scheduler.h
// Purpose: Simple task scheduler for named delayed tasks.
// Key invariants: Poll-based, not thread-based. Uses monotonic clock.
//                 Tasks identified by name; duplicate names replace previous.
// Ownership/Lifetime: Scheduler objects are heap-allocated; caller responsible
//                     for lifetime management.
// Links: docs/viperlib.md
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
