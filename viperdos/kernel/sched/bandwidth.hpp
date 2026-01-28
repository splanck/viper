//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: kernel/sched/bandwidth.hpp
// Purpose: CPU bandwidth control (rate limiting) for tasks.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "../include/types.hpp"
#include "task.hpp"

/**
 * @file bandwidth.hpp
 * @brief CPU bandwidth control for rate-limiting task CPU usage.
 *
 * @details
 * Bandwidth control allows limiting how much CPU time a task can consume
 * over a given period. For example, setting runtime=50ms and period=100ms
 * limits a task to 50% CPU utilization.
 *
 * When a task exceeds its budget, it is "throttled" and removed from the
 * run queue until the next period begins.
 */
namespace bandwidth {

/**
 * @brief Default bandwidth period (100ms in nanoseconds).
 */
constexpr u64 DEFAULT_PERIOD_NS = 100000000ULL;

/**
 * @brief Minimum bandwidth period (1ms in nanoseconds).
 */
constexpr u64 MIN_PERIOD_NS = 1000000ULL;

/**
 * @brief Maximum bandwidth period (1 second in nanoseconds).
 */
constexpr u64 MAX_PERIOD_NS = 1000000000ULL;

/**
 * @brief Bandwidth configuration parameters.
 */
struct BandwidthParams {
    u64 runtime; ///< Maximum runtime per period (nanoseconds, 0=unlimited)
    u64 period;  ///< Period length (nanoseconds)
};

/**
 * @brief Set bandwidth limits for a task.
 *
 * @details
 * Configures the maximum CPU time a task can use per period.
 * Setting runtime=0 disables bandwidth control (unlimited).
 *
 * @param t Task to configure.
 * @param params Bandwidth parameters.
 * @return 0 on success, -1 on invalid parameters.
 */
i32 set_bandwidth(task::Task *t, const BandwidthParams *params);

/**
 * @brief Clear bandwidth limits for a task.
 *
 * @param t Task to clear bandwidth limits from.
 */
void clear_bandwidth(task::Task *t);

/**
 * @brief Account for CPU time used by a task.
 *
 * @details
 * Called from the scheduler to track runtime consumption.
 * If the task exceeds its budget, it will be throttled.
 *
 * @param t Task to account.
 * @param ticks_used Number of timer ticks consumed (1 tick = 1ms).
 * @return true if task should continue running, false if throttled.
 */
bool account_runtime(task::Task *t, u32 ticks_used);

/**
 * @brief Check if a task is currently throttled.
 *
 * @param t Task to check.
 * @return true if throttled, false if can run.
 */
inline bool is_throttled(const task::Task *t) {
    return t && t->bw_throttled;
}

/**
 * @brief Replenish bandwidth budget for a new period.
 *
 * @details
 * Called at the start of a new period to reset the consumed runtime
 * and unthrottle the task if it was throttled.
 *
 * @param t Task to replenish.
 * @param current_tick Current system tick.
 */
void replenish_budget(task::Task *t, u64 current_tick);

/**
 * @brief Check and replenish all throttled tasks.
 *
 * @details
 * Called from the timer interrupt to check if any throttled tasks
 * have reached their next period and should be unthrottled.
 *
 * @param current_tick Current system tick.
 * @return Number of tasks unthrottled.
 */
u32 check_replenish(u64 current_tick);

/**
 * @brief Get remaining budget for a task.
 *
 * @param t Task to query.
 * @return Remaining runtime in nanoseconds, or U64_MAX if unlimited.
 */
inline u64 get_remaining(const task::Task *t) {
    if (!t || t->bw_runtime == 0)
        return static_cast<u64>(-1); // Unlimited
    if (t->bw_consumed >= t->bw_runtime)
        return 0;
    return t->bw_runtime - t->bw_consumed;
}

} // namespace bandwidth
