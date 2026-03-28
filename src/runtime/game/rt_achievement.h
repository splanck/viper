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
extern "C"
{
#endif

    typedef struct rt_achievement_impl *rt_achievement;

    rt_achievement rt_achievement_new(int64_t max_achievements);
    void rt_achievement_destroy(rt_achievement ach);

    // Define achievements
    void rt_achievement_add(rt_achievement ach, int64_t id,
                            const char *name, const char *description);

    // Unlock / query
    int8_t rt_achievement_unlock(rt_achievement ach, int64_t id);
    int8_t rt_achievement_is_unlocked(rt_achievement ach, int64_t id);
    int64_t rt_achievement_get_mask(rt_achievement ach);
    void rt_achievement_set_mask(rt_achievement ach, int64_t mask);
    int64_t rt_achievement_unlocked_count(rt_achievement ach);
    int64_t rt_achievement_total_count(rt_achievement ach);

    // Stat tracking
    void rt_achievement_inc_stat(rt_achievement ach, int64_t stat_id, int64_t amount);
    int64_t rt_achievement_get_stat(rt_achievement ach, int64_t stat_id);
    void rt_achievement_set_stat(rt_achievement ach, int64_t stat_id, int64_t value);

    // Notification
    void rt_achievement_update(rt_achievement ach, int64_t dt);
    void rt_achievement_draw(rt_achievement ach, void *canvas);
    void rt_achievement_set_notify_duration(rt_achievement ach, int64_t ms);
    int8_t rt_achievement_has_notification(rt_achievement ach);

#ifdef __cplusplus
}
#endif
