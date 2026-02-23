//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file rt_screenfx.c
/// @brief Implementation of screen effects manager.
///
//===----------------------------------------------------------------------===//

#include "rt_screenfx.h"
#include "rt_object.h"

#include <stdlib.h>
#include <string.h>

/// Per-instance LCG state for shake offset — avoids global state thread hazard.
static int64_t screenfx_rand(int64_t *state)
{
    *state = (*state) * 1103515245 + 12345;
    return ((*state) >> 16) & 0x7FFF;
}

/// Internal effect structure.
struct screenfx_effect
{
    rt_screenfx_type type; ///< Effect type.
    int64_t color;         ///< Color (RGBA).
    int64_t intensity;     ///< Intensity (for shake).
    int64_t duration;      ///< Total duration (ms).
    int64_t elapsed;       ///< Elapsed time (ms).
    int64_t decay;         ///< Decay rate (for shake).
};

/// Internal manager structure.
struct rt_screenfx_impl
{
    struct screenfx_effect effects[RT_SCREENFX_MAX_EFFECTS];
    int64_t shake_x;        ///< Current shake offset X.
    int64_t shake_y;        ///< Current shake offset Y.
    int64_t overlay_color;  ///< Current overlay color (RGB).
    int64_t overlay_alpha;  ///< Current overlay alpha (0-255).
    int64_t rand_state;     ///< Per-instance LCG state for shake RNG (thread-safe).
};

rt_screenfx rt_screenfx_new(void)
{
    struct rt_screenfx_impl *fx =
        (struct rt_screenfx_impl *)rt_obj_new_i64(0, (int64_t)sizeof(struct rt_screenfx_impl));
    if (!fx)
        return NULL;

    memset(fx, 0, sizeof(struct rt_screenfx_impl));
    fx->rand_state = (int64_t)(uintptr_t)fx ^ 0xDEADBEEF; // per-instance seed
    return fx;
}

void rt_screenfx_destroy(rt_screenfx fx)
{
    if (fx)
        rt_obj_free(fx);
}

/// Finds a free effect slot.
static int find_free_slot(rt_screenfx fx)
{
    for (int i = 0; i < RT_SCREENFX_MAX_EFFECTS; i++)
    {
        if (fx->effects[i].type == RT_SCREENFX_NONE)
            return i;
    }
    return -1;
}

/// Finds an existing effect of given type (to replace).
static int find_effect_of_type(rt_screenfx fx, rt_screenfx_type type)
{
    for (int i = 0; i < RT_SCREENFX_MAX_EFFECTS; i++)
    {
        if (fx->effects[i].type == type)
            return i;
    }
    return -1;
}

void rt_screenfx_update(rt_screenfx fx, int64_t dt)
{
    if (!fx)
        return;

    // Reset accumulators
    fx->shake_x = 0;
    fx->shake_y = 0;
    fx->overlay_alpha = 0;
    fx->overlay_color = 0;

    int64_t max_shake_intensity = 0;
    int64_t max_overlay_alpha = 0;

    for (int i = 0; i < RT_SCREENFX_MAX_EFFECTS; i++)
    {
        struct screenfx_effect *e = &fx->effects[i];
        if (e->type == RT_SCREENFX_NONE)
            continue;

        e->elapsed += dt;

        // Check if effect has finished
        if (e->elapsed >= e->duration)
        {
            e->type = RT_SCREENFX_NONE;
            continue;
        }

        // Calculate progress (0-1000)
        int64_t progress = (e->elapsed * 1000) / e->duration;

        switch (e->type)
        {
            case RT_SCREENFX_SHAKE:
            {
                // Exponential decay: intensity falls as (1 - progress/1000)^decay_exp
                // where decay_exp is controlled by e->decay (higher = faster decay).
                // When decay == 0 → no decay (constant intensity).
                // When decay == 1000 → approximately linear decay.
                // When decay == 2000 → quadratic (trauma model: natural feel).
                int64_t current_intensity;
                if (e->decay <= 0)
                {
                    current_intensity = e->intensity;
                }
                else
                {
                    // Use integer approximation of (1 - t)^2 for decay==2000 default
                    // General form: factor = (1000 - progress)^(decay/1000) / 1000
                    int64_t remaining = 1000 - progress; // 0..1000
                    if (remaining < 0) remaining = 0;
                    // decay stored as 1000×exponent; apply once for linear, twice for quadratic
                    int64_t decay_factor = remaining; // always at least one factor
                    if (e->decay >= 1500)             // >= 1.5 exponent → apply twice
                        decay_factor = (remaining * remaining) / 1000;
                    current_intensity = (e->intensity * decay_factor) / 1000;
                }

                if (current_intensity > max_shake_intensity)
                    max_shake_intensity = current_intensity;

                // Random offset based on intensity (per-instance state)
                int64_t rx = (screenfx_rand(&fx->rand_state) % 2001) - 1000;
                int64_t ry = (screenfx_rand(&fx->rand_state) % 2001) - 1000;
                fx->shake_x += (current_intensity * rx) / 1000;
                fx->shake_y += (current_intensity * ry) / 1000;
                break;
            }

            case RT_SCREENFX_FLASH:
            {
                // Flash starts bright and fades
                int64_t alpha = ((e->color & 0xFF) * (1000 - progress)) / 1000;
                if (alpha > max_overlay_alpha)
                {
                    max_overlay_alpha = alpha;
                    fx->overlay_color = e->color & 0xFFFFFF00;
                    fx->overlay_alpha = alpha;
                }
                break;
            }

            case RT_SCREENFX_FADE_IN:
            {
                // Fade from color to clear
                int64_t base_alpha = e->color & 0xFF;
                int64_t alpha = (base_alpha * (1000 - progress)) / 1000;
                if (alpha > max_overlay_alpha)
                {
                    max_overlay_alpha = alpha;
                    fx->overlay_color = e->color & 0xFFFFFF00;
                    fx->overlay_alpha = alpha;
                }
                break;
            }

            case RT_SCREENFX_FADE_OUT:
            {
                // Fade from clear to color
                int64_t base_alpha = e->color & 0xFF;
                int64_t alpha = (base_alpha * progress) / 1000;
                if (alpha > max_overlay_alpha)
                {
                    max_overlay_alpha = alpha;
                    fx->overlay_color = e->color & 0xFFFFFF00;
                    fx->overlay_alpha = alpha;
                }
                break;
            }

            default:
                break;
        }
    }
}

void rt_screenfx_shake(rt_screenfx fx, int64_t intensity, int64_t duration, int64_t decay)
{
    if (!fx || duration <= 0)
        return;

    // Find existing shake or free slot
    int slot = find_effect_of_type(fx, RT_SCREENFX_SHAKE);
    if (slot < 0)
        slot = find_free_slot(fx);
    if (slot < 0)
        return;

    struct screenfx_effect *e = &fx->effects[slot];
    e->type = RT_SCREENFX_SHAKE;
    e->intensity = intensity;
    e->duration = duration;
    e->elapsed = 0;
    e->decay = decay;
    e->color = 0;
}

void rt_screenfx_flash(rt_screenfx fx, int64_t color, int64_t duration)
{
    if (!fx || duration <= 0)
        return;

    int slot = find_free_slot(fx);
    if (slot < 0)
        return;

    struct screenfx_effect *e = &fx->effects[slot];
    e->type = RT_SCREENFX_FLASH;
    e->color = color;
    e->duration = duration;
    e->elapsed = 0;
    e->intensity = 0;
    e->decay = 0;
}

void rt_screenfx_fade_in(rt_screenfx fx, int64_t color, int64_t duration)
{
    if (!fx || duration <= 0)
        return;

    // Cancel existing fades
    rt_screenfx_cancel_type(fx, RT_SCREENFX_FADE_IN);
    rt_screenfx_cancel_type(fx, RT_SCREENFX_FADE_OUT);

    int slot = find_free_slot(fx);
    if (slot < 0)
        return;

    struct screenfx_effect *e = &fx->effects[slot];
    e->type = RT_SCREENFX_FADE_IN;
    e->color = color;
    e->duration = duration;
    e->elapsed = 0;
    e->intensity = 0;
    e->decay = 0;
}

void rt_screenfx_fade_out(rt_screenfx fx, int64_t color, int64_t duration)
{
    if (!fx || duration <= 0)
        return;

    // Cancel existing fades
    rt_screenfx_cancel_type(fx, RT_SCREENFX_FADE_IN);
    rt_screenfx_cancel_type(fx, RT_SCREENFX_FADE_OUT);

    int slot = find_free_slot(fx);
    if (slot < 0)
        return;

    struct screenfx_effect *e = &fx->effects[slot];
    e->type = RT_SCREENFX_FADE_OUT;
    e->color = color;
    e->duration = duration;
    e->elapsed = 0;
    e->intensity = 0;
    e->decay = 0;
}

void rt_screenfx_cancel_all(rt_screenfx fx)
{
    if (!fx)
        return;

    for (int i = 0; i < RT_SCREENFX_MAX_EFFECTS; i++)
        fx->effects[i].type = RT_SCREENFX_NONE;

    fx->shake_x = 0;
    fx->shake_y = 0;
    fx->overlay_color = 0;
    fx->overlay_alpha = 0;
}

void rt_screenfx_cancel_type(rt_screenfx fx, int64_t type)
{
    if (!fx)
        return;

    for (int i = 0; i < RT_SCREENFX_MAX_EFFECTS; i++)
    {
        if (fx->effects[i].type == (rt_screenfx_type)type)
            fx->effects[i].type = RT_SCREENFX_NONE;
    }
}

int8_t rt_screenfx_is_active(rt_screenfx fx)
{
    if (!fx)
        return 0;

    for (int i = 0; i < RT_SCREENFX_MAX_EFFECTS; i++)
    {
        if (fx->effects[i].type != RT_SCREENFX_NONE)
            return 1;
    }
    return 0;
}

int8_t rt_screenfx_is_type_active(rt_screenfx fx, int64_t type)
{
    if (!fx)
        return 0;

    for (int i = 0; i < RT_SCREENFX_MAX_EFFECTS; i++)
    {
        if (fx->effects[i].type == (rt_screenfx_type)type)
            return 1;
    }
    return 0;
}

int64_t rt_screenfx_get_shake_x(rt_screenfx fx)
{
    return fx ? fx->shake_x : 0;
}

int64_t rt_screenfx_get_shake_y(rt_screenfx fx)
{
    return fx ? fx->shake_y : 0;
}

int64_t rt_screenfx_get_overlay_color(rt_screenfx fx)
{
    return fx ? fx->overlay_color : 0;
}

int64_t rt_screenfx_get_overlay_alpha(rt_screenfx fx)
{
    return fx ? fx->overlay_alpha : 0;
}
