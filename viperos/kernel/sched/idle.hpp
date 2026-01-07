//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: kernel/sched/idle.hpp
// Purpose: CPU idle state management.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "../include/types.hpp"

namespace idle
{

/**
 * @brief Idle state statistics per CPU.
 */
struct IdleStats
{
    u64 wfi_count;      ///< Number of times WFI was executed
    u64 wakeup_count;   ///< Number of times CPU woke from idle
};

/**
 * @brief Initialize idle state tracking.
 */
void init();

/**
 * @brief Record that CPU is entering idle (WFI).
 *
 * @param cpu_id CPU entering idle.
 */
void enter(u32 cpu_id);

/**
 * @brief Record that CPU is exiting idle.
 *
 * @param cpu_id CPU exiting idle.
 */
void exit(u32 cpu_id);

/**
 * @brief Get idle statistics for a CPU.
 *
 * @param cpu_id CPU to query.
 * @param stats Output statistics.
 */
void get_stats(u32 cpu_id, IdleStats *stats);

} // namespace idle
