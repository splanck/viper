//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
// File: src/runtime/game/rt_achievement.h
// Purpose: Achievement tracking system with bitmask unlocks, stat counters,
//   and timed notification popups.
//
// Key invariants:
//   - Max 64 achievements (bitmask fits in int64_t).
//   - Max 32 stat counters.
//   - Notification display lasts configurable ms (default 3000).
//   - Unlock mask is get/settable for save/load integration.
//
// Ownership/Lifetime:
//   - GC-managed via rt_obj_new_i64.
//
// Links: src/runtime/game/rt_achievement.c
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct rt_achievement_impl *rt_achievement;

/// @brief Create an achievement tracker sized for at most @p max_achievements (max 64).
rt_achievement rt_achievement_new(int64_t max_achievements);
/// @brief Free the tracker (also reclaimed by GC).
void rt_achievement_destroy(rt_achievement ach);

// Define achievements
/// @brief Define an achievement with a unique @p id (0..max_achievements-1) and display strings.
void rt_achievement_add(rt_achievement ach, int64_t id, const char *name, const char *description);

// Unlock / query
/// @brief Unlock the achievement with @p id. Returns 1 if newly unlocked, 0 if already unlocked.
int8_t rt_achievement_unlock(rt_achievement ach, int64_t id);
/// @brief True if the achievement with @p id has been unlocked.
int8_t rt_achievement_is_unlocked(rt_achievement ach, int64_t id);
/// @brief Get the entire unlock state as a bitmask (suitable for save/load).
int64_t rt_achievement_get_mask(rt_achievement ach);
/// @brief Restore the unlock state from a bitmask (e.g., on game load).
void rt_achievement_set_mask(rt_achievement ach, int64_t mask);
/// @brief Count how many achievements are currently unlocked.
int64_t rt_achievement_unlocked_count(rt_achievement ach);
/// @brief Count of all defined achievements.
int64_t rt_achievement_total_count(rt_achievement ach);

// Stat tracking
/// @brief Increment a per-game stat counter (e.g., enemies_killed) by @p amount.
void rt_achievement_inc_stat(rt_achievement ach, int64_t stat_id, int64_t amount);
/// @brief Read the current value of a stat counter.
int64_t rt_achievement_get_stat(rt_achievement ach, int64_t stat_id);
/// @brief Set a stat counter directly (typically used to restore from save).
void rt_achievement_set_stat(rt_achievement ach, int64_t stat_id, int64_t value);

// Notification
/// @brief Tick the notification timer by @p dt milliseconds (call once per frame).
void rt_achievement_update(rt_achievement ach, int64_t dt);
/// @brief Render the active notification popup (no-op when none is showing).
void rt_achievement_draw(rt_achievement ach, void *canvas);
/// @brief Set how long each notification stays on screen in milliseconds (default 3000).
void rt_achievement_set_notify_duration(rt_achievement ach, int64_t ms);
/// @brief True if a notification popup is currently being displayed.
int8_t rt_achievement_has_notification(rt_achievement ach);

#ifdef __cplusplus
}
#endif
