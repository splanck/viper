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
//   - GC-managed via rt_obj_new_i64. Names/descriptions are strdup'd.
//
// Links: src/runtime/game/rt_achievement.h
//
//===----------------------------------------------------------------------===//

#include "rt_achievement.h"
#include "rt_object.h"
#include <stdlib.h>
#include <string.h>

// Forward declarations for canvas draw functions
extern void rt_canvas_box_alpha(void *canvas, int64_t x, int64_t y,
                                int64_t w, int64_t h, int64_t color, int64_t alpha);
extern void rt_canvas_text_scaled(void *canvas, int64_t x, int64_t y,
                                  const char *text, int64_t scale, int64_t color);
extern int64_t rt_canvas_width(void *canvas);

#define MAX_ACH 64
#define MAX_STATS 32

struct rt_ach_entry
{
    char *name;
    char *description;
    int8_t defined;
};

struct rt_achievement_impl
{
    int64_t unlock_mask;
    int64_t total_defined;
    struct rt_ach_entry entries[MAX_ACH];
    int64_t stats[MAX_STATS];

    // Notification state
    int64_t notify_id;        // Achievement ID being shown (-1 = none)
    int64_t notify_timer;     // Ms remaining
    int64_t notify_duration;  // Configurable display time (default 3000ms)
    int64_t slide_offset;     // Slide-in animation offset
};

rt_achievement rt_achievement_new(int64_t max_achievements)
{
    (void)max_achievements; // Capacity is fixed at 64

    struct rt_achievement_impl *ach =
        (struct rt_achievement_impl *)rt_obj_new_i64(
            0, (int64_t)sizeof(struct rt_achievement_impl));
    if (!ach)
        return NULL;

    ach->unlock_mask = 0;
    ach->total_defined = 0;
    memset(ach->entries, 0, sizeof(ach->entries));
    memset(ach->stats, 0, sizeof(ach->stats));
    ach->notify_id = -1;
    ach->notify_timer = 0;
    ach->notify_duration = 3000;
    ach->slide_offset = 0;

    return ach;
}

void rt_achievement_destroy(rt_achievement ach)
{
    if (!ach)
        return;
    for (int i = 0; i < MAX_ACH; i++)
    {
        if (ach->entries[i].name)
            free(ach->entries[i].name);
        if (ach->entries[i].description)
            free(ach->entries[i].description);
    }
    if (rt_obj_release_check0(ach))
        rt_obj_free(ach);
}

void rt_achievement_add(rt_achievement ach, int64_t id,
                        const char *name, const char *description)
{
    if (!ach || id < 0 || id >= MAX_ACH)
        return;
    if (ach->entries[id].name)
        free(ach->entries[id].name);
    if (ach->entries[id].description)
        free(ach->entries[id].description);

    ach->entries[id].name = name ? strdup(name) : NULL;
    ach->entries[id].description = description ? strdup(description) : NULL;
    if (!ach->entries[id].defined)
    {
        ach->entries[id].defined = 1;
        ach->total_defined++;
    }
}

int8_t rt_achievement_unlock(rt_achievement ach, int64_t id)
{
    if (!ach || id < 0 || id >= MAX_ACH)
        return 0;

    int64_t bit = (int64_t)1 << id;
    if (ach->unlock_mask & bit)
        return 0; // Already unlocked

    ach->unlock_mask |= bit;

    // Trigger notification
    ach->notify_id = id;
    ach->notify_timer = ach->notify_duration;
    ach->slide_offset = 300; // Start off-screen (slides in)

    return 1; // Newly unlocked
}

int8_t rt_achievement_is_unlocked(rt_achievement ach, int64_t id)
{
    if (!ach || id < 0 || id >= MAX_ACH)
        return 0;
    return (ach->unlock_mask & ((int64_t)1 << id)) ? 1 : 0;
}

int64_t rt_achievement_get_mask(rt_achievement ach)
{
    return ach ? ach->unlock_mask : 0;
}

void rt_achievement_set_mask(rt_achievement ach, int64_t mask)
{
    if (ach)
        ach->unlock_mask = mask;
}

int64_t rt_achievement_unlocked_count(rt_achievement ach)
{
    if (!ach)
        return 0;
    // Popcount
    int64_t mask = ach->unlock_mask;
    int64_t count = 0;
    while (mask)
    {
        count += mask & 1;
        mask >>= 1;
    }
    return count;
}

int64_t rt_achievement_total_count(rt_achievement ach)
{
    return ach ? ach->total_defined : 0;
}

// Stat tracking
void rt_achievement_inc_stat(rt_achievement ach, int64_t stat_id, int64_t amount)
{
    if (!ach || stat_id < 0 || stat_id >= MAX_STATS)
        return;
    ach->stats[stat_id] += amount;
}

int64_t rt_achievement_get_stat(rt_achievement ach, int64_t stat_id)
{
    if (!ach || stat_id < 0 || stat_id >= MAX_STATS)
        return 0;
    return ach->stats[stat_id];
}

void rt_achievement_set_stat(rt_achievement ach, int64_t stat_id, int64_t value)
{
    if (!ach || stat_id < 0 || stat_id >= MAX_STATS)
        return;
    ach->stats[stat_id] = value;
}

// Notification update
void rt_achievement_update(rt_achievement ach, int64_t dt)
{
    if (!ach || ach->notify_id < 0)
        return;

    ach->notify_timer -= dt;

    // Slide-in animation (first 300ms)
    if (ach->slide_offset > 0)
    {
        ach->slide_offset -= dt * 300 / 300; // 300ms slide-in
        if (ach->slide_offset < 0)
            ach->slide_offset = 0;
    }

    if (ach->notify_timer <= 0)
    {
        ach->notify_id = -1;
        ach->notify_timer = 0;
    }
}

void rt_achievement_draw(rt_achievement ach, void *canvas)
{
    if (!ach || !canvas || ach->notify_id < 0)
        return;

    int64_t id = ach->notify_id;
    if (id >= MAX_ACH || !ach->entries[id].defined)
        return;

    int64_t cw = rt_canvas_width(canvas);
    int64_t box_w = 280;
    int64_t box_h = 50;
    int64_t box_x = cw - box_w - 10 + ach->slide_offset;
    int64_t box_y = 10;

    // Background panel
    rt_canvas_box_alpha(canvas, box_x, box_y, box_w, box_h, 0x111122, 220);

    // Title "ACHIEVEMENT UNLOCKED"
    rt_canvas_text_scaled(canvas, box_x + 8, box_y + 6,
                          "ACHIEVEMENT UNLOCKED", 1, 0xFFDD22);

    // Achievement name
    if (ach->entries[id].name)
    {
        rt_canvas_text_scaled(canvas, box_x + 8, box_y + 22,
                              ach->entries[id].name, 2, 0xFFFFFF);
    }
}

void rt_achievement_set_notify_duration(rt_achievement ach, int64_t ms)
{
    if (ach && ms > 0)
        ach->notify_duration = ms;
}

int8_t rt_achievement_has_notification(rt_achievement ach)
{
    return (ach && ach->notify_id >= 0) ? 1 : 0;
}
