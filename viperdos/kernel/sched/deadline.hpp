//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: kernel/sched/deadline.hpp
// Purpose: SCHED_DEADLINE support utilities (EDF scheduling).
//
//===----------------------------------------------------------------------===//

#pragma once

#include "../include/types.hpp"
#include "task.hpp"

namespace deadline {

/**
 * @brief Minimum bandwidth fraction (1/1000 = 0.1%).
 */
constexpr u64 MIN_BANDWIDTH_FRACTION = 1000;

/**
 * @brief Deadline task flag: throttle on miss (skip to next period).
 */
constexpr u32 DL_FLAG_THROTTLE_ON_MISS = 1 << 0;

/**
 * @brief Deadline task flag: demote to SCHED_OTHER on repeated misses.
 */
constexpr u32 DL_FLAG_DEMOTE_ON_MISS = 1 << 1;

/**
 * @brief Number of consecutive misses before demotion (if DL_FLAG_DEMOTE_ON_MISS set).
 */
constexpr u32 DL_MISS_THRESHOLD = 3;

/**
 * @brief Maximum total bandwidth (95% = 950/1000).
 *
 * @details
 * Reserve 5% for non-deadline tasks to prevent starvation.
 */
constexpr u64 MAX_TOTAL_BANDWIDTH = 950;

/**
 * @brief Current total bandwidth reserved by deadline tasks.
 */
extern u64 total_bandwidth;

/**
 * @brief Deadline task parameters for sched_setattr.
 */
struct DeadlineParams {
    u64 runtime;  ///< Maximum runtime per period (nanoseconds)
    u64 deadline; ///< Relative deadline (nanoseconds)
    u64 period;   ///< Period length (nanoseconds)
};

/**
 * @brief Check if deadline parameters are valid.
 *
 * @details
 * Validates:
 * - runtime > 0
 * - runtime <= deadline
 * - deadline <= period
 *
 * @param params Deadline parameters to validate.
 * @return true if valid, false otherwise.
 */
inline bool validate_params(const DeadlineParams *params) {
    if (!params)
        return false;
    if (params->runtime == 0)
        return false;
    if (params->runtime > params->deadline)
        return false;
    if (params->deadline > params->period)
        return false;
    return true;
}

/**
 * @brief Calculate bandwidth as fraction (runtime * 1000 / period).
 *
 * @param params Deadline parameters.
 * @return Bandwidth in parts per thousand.
 */
inline u64 calc_bandwidth(const DeadlineParams *params) {
    if (!params || params->period == 0)
        return 0;
    return (params->runtime * MIN_BANDWIDTH_FRACTION) / params->period;
}

/**
 * @brief Check if admission control allows a new deadline task.
 *
 * @details
 * Ensures total bandwidth doesn't exceed MAX_TOTAL_BANDWIDTH.
 *
 * @param new_bandwidth Bandwidth of new task.
 * @return true if task can be admitted, false otherwise.
 */
inline bool can_admit(u64 new_bandwidth) {
    return (total_bandwidth + new_bandwidth) <= MAX_TOTAL_BANDWIDTH;
}

/**
 * @brief Set deadline parameters for a task.
 *
 * @param t Task to configure.
 * @param params Deadline parameters.
 * @return 0 on success, -1 on error.
 */
i32 set_deadline(task::Task *t, const DeadlineParams *params);

/**
 * @brief Clear deadline parameters from a task.
 *
 * @param t Task to clear.
 */
void clear_deadline(task::Task *t);

/**
 * @brief Update absolute deadline for next period.
 *
 * @param t Task to update.
 * @param current_time Current system time in nanoseconds.
 */
void replenish(task::Task *t, u64 current_time);

/**
 * @brief Compare two tasks by deadline (for EDF ordering).
 *
 * @param a First task.
 * @param b Second task.
 * @return true if a has earlier deadline than b.
 */
inline bool earlier_deadline(const task::Task *a, const task::Task *b) {
    if (!a)
        return false;
    if (!b)
        return true;
    return a->dl_abs_deadline < b->dl_abs_deadline;
}

/**
 * @brief Check if a deadline task has missed its deadline.
 *
 * @param t Task to check.
 * @param current_time Current system time in timer ticks.
 * @return true if deadline was missed, false otherwise.
 */
bool check_deadline_miss(task::Task *t, u64 current_time);

/**
 * @brief Handle a deadline miss for a task.
 *
 * @details
 * Called when a deadline miss is detected. Depending on dl_flags:
 * - DL_FLAG_THROTTLE_ON_MISS: Skip to next period
 * - DL_FLAG_DEMOTE_ON_MISS: Demote to SCHED_OTHER after threshold misses
 * Always increments dl_missed counter and logs the miss.
 *
 * @param t Task that missed its deadline.
 * @param current_time Current system time in timer ticks.
 */
void handle_deadline_miss(task::Task *t, u64 current_time);

/**
 * @brief Get the number of deadline misses for a task.
 *
 * @param t Task to query.
 * @return Number of missed deadlines, 0 if task is null.
 */
inline u32 get_missed_deadlines(const task::Task *t) {
    return t ? t->dl_missed : 0;
}

/**
 * @brief Reset the deadline miss counter for a task.
 *
 * @param t Task to reset.
 */
inline void reset_missed_deadlines(task::Task *t) {
    if (t) {
        t->dl_missed = 0;
    }
}

} // namespace deadline
