//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
#pragma once

/**
 * @file pressure.hpp
 * @brief Memory pressure monitoring and reclaim callbacks.
 *
 * @details
 * The memory pressure subsystem monitors available memory and triggers
 * reclaim callbacks when memory runs low. This allows subsystems like
 * the slab allocator, page cache, and buffer cache to release unused
 * memory before the system runs out.
 *
 * Pressure levels:
 * - NONE: Plenty of free memory (>50% free)
 * - LOW: Memory getting low (25-50% free)
 * - MEDIUM: Memory pressure (10-25% free)
 * - HIGH: Critical (5-10% free)
 * - CRITICAL: OOM imminent (<5% free)
 */

#include "../include/types.hpp"

namespace mm::pressure {

/**
 * @brief Memory pressure levels.
 */
enum class Level : u8 {
    NONE = 0,     ///< No pressure, plenty of free memory
    LOW = 1,      ///< Low pressure, starting to reclaim
    MEDIUM = 2,   ///< Medium pressure, aggressive reclaim
    HIGH = 3,     ///< High pressure, emergency reclaim
    CRITICAL = 4, ///< Critical, OOM imminent
};

/**
 * @brief Callback function type for pressure notifications.
 *
 * @param level Current pressure level.
 * @return Number of pages reclaimed by this callback.
 */
using PressureCallback = u64 (*)(Level level);

/**
 * @brief Maximum number of registered callbacks.
 */
constexpr usize MAX_CALLBACKS = 8;

/**
 * @brief Initialize the memory pressure subsystem.
 */
void init();

/**
 * @brief Register a callback to be notified on memory pressure.
 *
 * @param name Human-readable name for debugging.
 * @param callback Function to call when pressure changes.
 * @return true on success, false if callback table is full.
 */
bool register_callback(const char *name, PressureCallback callback);

/**
 * @brief Check current memory pressure level.
 *
 * @return Current pressure level based on free memory percentage.
 */
Level check_level();

/**
 * @brief Trigger memory reclaim if under pressure.
 *
 * @details
 * Calls all registered callbacks if memory pressure is above NONE.
 * Should be called periodically or before large allocations.
 *
 * @return Total pages reclaimed.
 */
u64 reclaim_if_needed();

/**
 * @brief Force a reclaim pass regardless of pressure level.
 *
 * @return Total pages reclaimed.
 */
u64 force_reclaim();

/**
 * @brief Get the percentage of free memory.
 *
 * @return Free memory as a percentage (0-100).
 */
u32 get_free_percent();

/**
 * @brief Get pressure statistics.
 *
 * @param out_level Current pressure level.
 * @param out_free_pages Number of free pages.
 * @param out_total_pages Total managed pages.
 * @param out_reclaim_calls Number of times reclaim was triggered.
 * @param out_pages_reclaimed Total pages reclaimed since init.
 */
void get_stats(Level *out_level,
               u64 *out_free_pages,
               u64 *out_total_pages,
               u64 *out_reclaim_calls,
               u64 *out_pages_reclaimed);

/**
 * @brief Convert pressure level to string.
 */
const char *level_name(Level level);

} // namespace mm::pressure
