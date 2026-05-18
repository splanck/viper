//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
// File: src/runtime/game/rt_achievement.c
// Purpose: Achievement tracking with bitmask unlocks, stat counters, and
//   slide-in notification popups rendered via Canvas text.
//
// Key invariants:
//   - Achievement IDs are 0-63 (bitmask range).
//   - Stat IDs are 0-31.
//   - Notification queue holds one pending notification at a time.
//   - Draw() renders notification popup at top-right of canvas.
//
// Ownership/Lifetime:
//   - GC-managed via rt_obj_new_i64. Names/descriptions retain runtime strings.
//
// Links: src/runtime/game/rt_achievement.h
//
//===----------------------------------------------------------------------===//

#include "rt_achievement.h"
#include "rt_graphics.h"
#include "rt_object.h"
#include "rt_trap.h"
#include <limits.h>
#include <string.h>

#define MAX_ACH 64
#define MAX_STATS 32

struct rt_ach_entry {
    rt_string name;
    rt_string description;
    int8_t defined;
};

struct rt_achievement_impl {
    int64_t unlock_mask;
    int64_t total_defined;
    int64_t capacity;
    struct rt_ach_entry entries[MAX_ACH];
    int64_t stats[MAX_STATS];

    // Notification state
    int64_t notify_id;       // Achievement ID being shown (-1 = none)
    int64_t notify_timer;    // Ms remaining
    int64_t notify_duration; // Configurable display time (default 3000ms)
    int64_t slide_offset;    // Slide-in animation offset
};

/// @brief Clamp a requested achievement count to [1, MAX_ACH] (0/invalid ->
///        MAX_ACH).
static int64_t achievement_capacity(int64_t requested) {
    if (requested < 1 || requested > MAX_ACH)
        return MAX_ACH;
    return requested;
}

/// @brief Bitmask of valid achievement-index bits for the object's capacity
///        (all-ones once capacity reaches the 64-slot ceiling).
static uint64_t achievement_mask_for_capacity(const struct rt_achievement_impl *ach) {
    if (!ach || ach->capacity >= MAX_ACH)
        return UINT64_MAX;
    return ((uint64_t)1 << ach->capacity) - 1u;
}

/// @brief Safe-cast a handle to the Achievement impl, trapping @p api on a
///        class-id mismatch. @return The impl, or NULL if @p ach is NULL.
static struct rt_achievement_impl *checked_achievement(rt_achievement ach, const char *api) {
    if (!ach)
        return NULL;
    if (rt_obj_class_id(ach) != RT_ACHIEVEMENT_CLASS_ID) {
        rt_trap(api);
        return NULL;
    }
    return ach;
}

/// @brief Release all entry name/description strings and reset entry state.
static void achievement_release_entries(struct rt_achievement_impl *ach) {
    if (!ach)
        return;
    for (int i = 0; i < MAX_ACH; i++) {
        if (ach->entries[i].name)
            rt_string_unref(ach->entries[i].name);
        if (ach->entries[i].description)
            rt_string_unref(ach->entries[i].description);
        ach->entries[i].name = NULL;
        ach->entries[i].description = NULL;
        ach->entries[i].defined = 0;
    }
    ach->total_defined = 0;
}

/// @brief GC finalizer: release entry strings when the Achievement is freed.
static void achievement_finalizer(void *obj) {
    achievement_release_entries((struct rt_achievement_impl *)obj);
}

/// @brief Saturating int64 addition (clamps to INT64_MIN/MAX on overflow).
static int64_t achievement_saturating_add_i64(int64_t a, int64_t b) {
    if (b > 0 && a > INT64_MAX - b)
        return INT64_MAX;
    if (b < 0 && a < INT64_MIN - b)
        return INT64_MIN;
    return a + b;
}

/// @brief Create a new achievement tracker (supports up to 64 achievements via bitmask).
/// @details Tracks unlock state as a 64-bit mask, supports per-achievement stats,
///          and provides slide-in notification display when achievements unlock.
rt_achievement rt_achievement_new(int64_t max_achievements) {
    struct rt_achievement_impl *ach = (struct rt_achievement_impl *)rt_obj_new_i64(
        RT_ACHIEVEMENT_CLASS_ID, (int64_t)sizeof(struct rt_achievement_impl));
    if (!ach)
        return NULL;

    ach->unlock_mask = 0;
    ach->total_defined = 0;
    ach->capacity = achievement_capacity(max_achievements);
    memset(ach->entries, 0, sizeof(ach->entries));
    memset(ach->stats, 0, sizeof(ach->stats));
    ach->notify_id = -1;
    ach->notify_timer = 0;
    ach->notify_duration = 3000;
    ach->slide_offset = 0;
    rt_obj_set_finalizer(ach, achievement_finalizer);

    return ach;
}

/// @brief Destroy the achievement tracker and free all name/description strings.
void rt_achievement_destroy(rt_achievement ach) {
    ach = checked_achievement(ach, "AchievementTracker.Destroy: expected Viper.Game.AchievementTracker");
    if (ach && rt_obj_release_check0(ach))
        rt_obj_free(ach);
}

/// @brief Define an achievement by ID with a display name and description.
void rt_achievement_add(rt_achievement ach, int64_t id, rt_string name, rt_string description) {
    ach = checked_achievement(ach, "AchievementTracker.Add: expected Viper.Game.AchievementTracker");
    if (!ach || id < 0 || id >= ach->capacity)
        return;
    if (ach->entries[id].name)
        rt_string_unref(ach->entries[id].name);
    if (ach->entries[id].description)
        rt_string_unref(ach->entries[id].description);

    ach->entries[id].name = name ? rt_string_ref(name) : NULL;
    ach->entries[id].description = description ? rt_string_ref(description) : NULL;
    if (!ach->entries[id].defined) {
        ach->entries[id].defined = 1;
        ach->total_defined++;
    }
}

/// @brief Unlock an achievement and trigger the slide-in notification. Returns 1 if newly unlocked.
int8_t rt_achievement_unlock(rt_achievement ach, int64_t id) {
    ach = checked_achievement(ach, "AchievementTracker.Unlock: expected Viper.Game.AchievementTracker");
    if (!ach || id < 0 || id >= ach->capacity || !ach->entries[id].defined)
        return 0;

    uint64_t unlock_mask = (uint64_t)ach->unlock_mask;
    uint64_t bit = UINT64_C(1) << (uint64_t)id;
    if (unlock_mask & bit)
        return 0; // Already unlocked

    ach->unlock_mask = (int64_t)(unlock_mask | bit);

    // Trigger notification
    ach->notify_id = id;
    ach->notify_timer = ach->notify_duration;
    ach->slide_offset = 300; // Start off-screen (slides in)

    return 1; // Newly unlocked
}

/// @brief Check whether an achievement has been unlocked.
int8_t rt_achievement_is_unlocked(rt_achievement ach, int64_t id) {
    ach = checked_achievement(ach, "AchievementTracker.IsUnlocked: expected Viper.Game.AchievementTracker");
    if (!ach || id < 0 || id >= ach->capacity)
        return 0;
    return ((((uint64_t)ach->unlock_mask) & (UINT64_C(1) << (uint64_t)id)) != 0) ? 1 : 0;
}

/// @brief Get the raw 64-bit unlock bitmask (for serialization/save games).
int64_t rt_achievement_get_mask(rt_achievement ach) {
    ach = checked_achievement(ach, "AchievementTracker.Mask: expected Viper.Game.AchievementTracker");
    return ach ? ach->unlock_mask : 0;
}

/// @brief Restore the unlock bitmask from a saved value (for loading save games).
void rt_achievement_set_mask(rt_achievement ach, int64_t mask) {
    ach = checked_achievement(ach, "AchievementTracker.Mask.set: expected Viper.Game.AchievementTracker");
    if (!ach)
        return;
    ach->unlock_mask = (int64_t)(((uint64_t)mask) & achievement_mask_for_capacity(ach));
    if (ach->notify_id < 0 || ach->notify_id >= ach->capacity ||
        !rt_achievement_is_unlocked(ach, ach->notify_id)) {
        ach->notify_id = -1;
        ach->notify_timer = 0;
        ach->slide_offset = 0;
    }
}

/// @brief Count how many achievements have been unlocked (popcount of the bitmask).
int64_t rt_achievement_unlocked_count(rt_achievement ach) {
    ach = checked_achievement(
        ach, "AchievementTracker.UnlockedCount: expected Viper.Game.AchievementTracker");
    if (!ach)
        return 0;
    // Popcount
    uint64_t mask = ((uint64_t)ach->unlock_mask) & achievement_mask_for_capacity(ach);
    int64_t count = 0;
    while (mask) {
        count += mask & 1;
        mask >>= 1;
    }
    return count;
}

/// @brief Get the total number of defined achievements.
int64_t rt_achievement_total_count(rt_achievement ach) {
    ach = checked_achievement(ach, "AchievementTracker.TotalCount: expected Viper.Game.AchievementTracker");
    return ach ? ach->total_defined : 0;
}

/// @brief Increment a tracked stat counter by the given amount.
void rt_achievement_inc_stat(rt_achievement ach, int64_t stat_id, int64_t amount) {
    ach = checked_achievement(
        ach, "AchievementTracker.IncrementStat: expected Viper.Game.AchievementTracker");
    if (!ach || stat_id < 0 || stat_id >= MAX_STATS)
        return;
    ach->stats[stat_id] = achievement_saturating_add_i64(ach->stats[stat_id], amount);
}

/// @brief Get the current value of a tracked stat counter.
int64_t rt_achievement_get_stat(rt_achievement ach, int64_t stat_id) {
    ach = checked_achievement(ach, "AchievementTracker.GetStat: expected Viper.Game.AchievementTracker");
    if (!ach || stat_id < 0 || stat_id >= MAX_STATS)
        return 0;
    return ach->stats[stat_id];
}

/// @brief Set a tracked stat counter to an absolute value.
void rt_achievement_set_stat(rt_achievement ach, int64_t stat_id, int64_t value) {
    ach = checked_achievement(ach, "AchievementTracker.SetStat: expected Viper.Game.AchievementTracker");
    if (!ach || stat_id < 0 || stat_id >= MAX_STATS)
        return;
    ach->stats[stat_id] = value;
}

/// @brief Update the notification slide-in animation and auto-dismiss timer.
void rt_achievement_update(rt_achievement ach, int64_t dt) {
    ach = checked_achievement(ach, "AchievementTracker.Update: expected Viper.Game.AchievementTracker");
    if (!ach || ach->notify_id < 0 || dt <= 0)
        return;

    if (dt >= ach->notify_timer)
        ach->notify_timer = 0;
    else
        ach->notify_timer -= dt;

    // Slide-in animation (first 300ms)
    if (ach->slide_offset > 0) {
        if (dt >= ach->slide_offset)
            ach->slide_offset = 0;
        else
            ach->slide_offset -= dt;
    }

    if (ach->notify_timer <= 0) {
        ach->notify_id = -1;
        ach->notify_timer = 0;
    }
}

/// @brief Draw the achievement notification banner (slides in from the right edge).
void rt_achievement_draw(rt_achievement ach, void *canvas) {
    rt_string title;
    ach = checked_achievement(ach, "AchievementTracker.Draw: expected Viper.Game.AchievementTracker");
    if (!ach || !canvas || ach->notify_id < 0)
        return;

    int64_t id = ach->notify_id;
    if (id >= ach->capacity || !ach->entries[id].defined)
        return;

    int64_t cw = rt_canvas_width(canvas);
    int64_t box_w = 280;
    int64_t box_h = 50;
    int64_t box_x = cw - box_w - 10 + ach->slide_offset;
    int64_t box_y = 10;

    // Background panel
    rt_canvas_box_alpha(canvas, box_x, box_y, box_w, box_h, 0x111122, 220);

    // Title "ACHIEVEMENT UNLOCKED"
    title = rt_const_cstr("ACHIEVEMENT UNLOCKED");
    rt_canvas_text_scaled(canvas, box_x + 8, box_y + 6, title, 1, 0xFFDD22);
    rt_string_unref(title);

    // Achievement name
    if (ach->entries[id].name) {
        rt_canvas_text_scaled(canvas, box_x + 8, box_y + 22, ach->entries[id].name, 2, 0xFFFFFF);
    }
}

/// @brief Set the notification display duration in milliseconds (default 3000).
void rt_achievement_set_notify_duration(rt_achievement ach, int64_t ms) {
    ach = checked_achievement(
        ach, "AchievementTracker.NotifyDuration.set: expected Viper.Game.AchievementTracker");
    if (ach && ms > 0)
        ach->notify_duration = ms;
}

/// @brief Check whether an achievement notification is currently being displayed.
int8_t rt_achievement_has_notification(rt_achievement ach) {
    ach = checked_achievement(
        ach, "AchievementTracker.HasNotification: expected Viper.Game.AchievementTracker");
    return (ach && ach->notify_id >= 0) ? 1 : 0;
}
