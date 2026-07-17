//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
// File: src/runtime/game/rt_lighting2d.c
// Purpose: 2D darkness overlay with pulsing player light and pooled dynamic
//   lights. Renders a full-screen tinted overlay at configurable alpha, then
//   source-over blends concentric DiscAlpha glow rings for the player light,
//   tile lights, and time-limited dynamic lights (explosions, bullets,
//   pickups). These rings do not subtract the darkness overlay.
//
// Key invariants:
//   - Max dynamic lights set at construction; excess lights are silently dropped.
//   - Dynamic lights decay over their lifetime; alpha fades proportionally.
//   - Player light pulses via an internal 120-frame cycle timer.
//   - Draw order: darkness overlay → player light → dynamic lights → tile lights.
//
// Ownership/Lifetime:
//   - GC-managed via rt_obj_new_i64. Dynamic light arrays are inline (no heap).
//
// Links: src/runtime/game/rt_lighting2d.h
//
//===----------------------------------------------------------------------===//

#include "rt_lighting2d.h"
#include "rt_graphics.h"
#include "rt_object.h"
#include "rt_trap.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define MAX_DYN_LIGHTS_CAP 128

struct rt_dyn_light {
    int64_t x, y;
    int64_t radius;
    int64_t color;
    int64_t life;
    int64_t max_life;
    int8_t active;
};

struct rt_lighting2d_impl {
    int64_t darkness;      // 0-255 overlay alpha
    int64_t tint_color;    // Darkness tint (0xRRGGBB)
    int64_t player_radius; // Player light radius
    int64_t player_color;  // Player light color
    int64_t player_pulse;  // Internal pulse timer (0-119)
    int64_t max_lights;    // Pool capacity
    int64_t light_count;   // Active light count
    struct rt_dyn_light lights[MAX_DYN_LIGHTS_CAP];
    int64_t tile_count; // Per-frame screen-space light count
    struct rt_dyn_light tile_lights[MAX_DYN_LIGHTS_CAP];
};

/// @brief Safe-cast a handle to the Lighting2D impl, trapping @p api on a
///        class-id mismatch. @return The impl, or NULL if @p lit is NULL.
static struct rt_lighting2d_impl *checked_lighting2d(rt_lighting2d lit, const char *api) {
    if (!lit)
        return NULL;
    if (rt_obj_class_id(lit) != RT_LIGHTING2D_CLASS_ID) {
        rt_trap(api);
        return NULL;
    }
    return lit;
}

/// @brief Clamp @p value to the inclusive [lo, hi] range.
static int64_t clamp_i64(int64_t value, int64_t lo, int64_t hi) {
    if (value < lo)
        return lo;
    if (value > hi)
        return hi;
    return value;
}

/// @brief Saturating int64 addition (clamps to INT64_MIN/MAX on overflow).
static int64_t lighting_add_sat_i64(int64_t a, int64_t b) {
    if (b > 0 && a > INT64_MAX - b)
        return INT64_MAX;
    if (b < 0 && a < INT64_MIN - b)
        return INT64_MIN;
    return a + b;
}

/// @brief Saturating int64 subtraction (a - b), clamped to the int64 range.
static int64_t lighting_sub_sat_i64(int64_t a, int64_t b) {
    if (b == INT64_MIN)
        return INT64_MAX;
    return lighting_add_sat_i64(a, -b);
}

/// @brief Compute (a*b)/divisor in long double and saturate to the int64
///        range; returns 0 when @p divisor is 0.
static int64_t lighting_mul_div_sat_i64(int64_t a, int64_t b, int64_t divisor) {
    if (divisor == 0)
        return 0;
    long double value = ((long double)a * (long double)b) / (long double)divisor;
    if (value > (long double)INT64_MAX)
        return INT64_MAX;
    if (value < (long double)INT64_MIN)
        return INT64_MIN;
    return (int64_t)value;
}

/// @brief Construct a Lighting2D system with `max_lights` dynamic-light slots (capped at
/// MAX_DYN_LIGHTS_CAP=128). Defaults: darkness=0 (off), tint=near-black with blue cast, player
/// radius=180, player color=0x303240. Returns a GC-managed handle; NULL on allocation failure.
rt_lighting2d rt_lighting2d_new(int64_t max_lights) {
    struct rt_lighting2d_impl *lit = (struct rt_lighting2d_impl *)rt_obj_new_i64(
        RT_LIGHTING2D_CLASS_ID, (int64_t)sizeof(struct rt_lighting2d_impl));
    if (!lit)
        return NULL;

    lit->darkness = 0;
    lit->tint_color = 0x00000A; // Default: near-black with blue tint
    lit->player_radius = 180;
    lit->player_color = 0x303240;
    lit->player_pulse = 0;
    lit->max_lights = clamp_i64(max_lights, 0, MAX_DYN_LIGHTS_CAP);
    lit->light_count = 0;
    lit->tile_count = 0;
    memset(lit->lights, 0, sizeof(lit->lights));
    memset(lit->tile_lights, 0, sizeof(lit->tile_lights));

    return lit;
}

/// @brief Release a Lighting2D handle; frees the inline structure when the refcount drops to zero.
void rt_lighting2d_destroy(rt_lighting2d lit) {
    lit = checked_lighting2d(lit, "Lighting2D.Destroy: expected Zanna.Game.Lighting2D");
    if (lit && rt_obj_release_check0(lit))
        rt_obj_free(lit);
}

/// @brief Set the full-screen darkness overlay alpha (0=off, 255=opaque). Clamped to [0,255].
void rt_lighting2d_set_darkness(rt_lighting2d lit, int64_t alpha) {
    lit = checked_lighting2d(lit, "Lighting2D.SetDarkness: expected Zanna.Game.Lighting2D");
    if (!lit)
        return;
    lit->darkness = clamp_i64(alpha, 0, 255);
}

/// @brief Read the current darkness overlay alpha; returns 0 for null handles.
int64_t rt_lighting2d_get_darkness(rt_lighting2d lit) {
    lit = checked_lighting2d(lit, "Lighting2D.GetDarkness: expected Zanna.Game.Lighting2D");
    return lit ? lit->darkness : 0;
}

/// @brief Set the tint color (0xRRGGBB) used by the darkness overlay; alpha is supplied separately.
void rt_lighting2d_set_tint_color(rt_lighting2d lit, int64_t color) {
    lit = checked_lighting2d(lit, "Lighting2D.SetTintColor: expected Zanna.Game.Lighting2D");
    if (lit)
        lit->tint_color = color & 0xFFFFFF;
}

/// @brief Read the darkness overlay tint color (0xRRGGBB).
int64_t rt_lighting2d_get_tint_color(rt_lighting2d lit) {
    lit = checked_lighting2d(lit, "Lighting2D.GetTintColor: expected Zanna.Game.Lighting2D");
    return lit ? lit->tint_color : 0;
}

/// @brief Configure the always-on player light's base radius and color.
/// Drawn at the screen-space player position passed to `draw()`; modulated by an internal pulse.
void rt_lighting2d_set_player_light(rt_lighting2d lit, int64_t radius, int64_t color) {
    lit = checked_lighting2d(lit, "Lighting2D.SetPlayerLight: expected Zanna.Game.Lighting2D");
    if (!lit)
        return;
    lit->player_radius = radius > 0 ? radius : 0;
    lit->player_color = color & 0xFFFFFF;
}

/// @brief Spawn a dynamic light (explosion, bullet, pickup) at world coords (x, y).
/// `lifetime` is in update ticks; positive lifetimes fade linearly to zero, and 0 is permanent.
/// Silently dropped if the pool (sized at construction, <= MAX_DYN_LIGHTS_CAP) is full, the radius
/// is non-positive, or `lifetime` is negative.
void rt_lighting2d_add_light(
    rt_lighting2d lit, int64_t x, int64_t y, int64_t radius, int64_t color, int64_t lifetime) {
    lit = checked_lighting2d(lit, "Lighting2D.AddLight: expected Zanna.Game.Lighting2D");
    if (!lit || lifetime < 0)
        return;
    if (radius <= 0)
        return;

    // Find an inactive slot
    for (int64_t i = 0; i < lit->max_lights; i++) {
        if (!lit->lights[i].active) {
            lit->lights[i].x = x;
            lit->lights[i].y = y;
            lit->lights[i].radius = radius;
            lit->lights[i].color = color & 0xFFFFFF;
            lit->lights[i].life = lifetime;
            lit->lights[i].max_life = lifetime;
            lit->lights[i].active = 1;
            lit->light_count++;
            return;
        }
    }
    // Pool full — silently drop
}

/// @brief Add a per-frame "tile light" (lamp, torch) at screen-space coords.
/// @details Tile lights are drawn once and cleared after Draw, so update-before-render
/// loops do not expire them before they are visible.
void rt_lighting2d_add_tile_light(
    rt_lighting2d lit, int64_t screen_x, int64_t screen_y, int64_t radius, int64_t color) {
    lit = checked_lighting2d(lit, "Lighting2D.AddTileLight: expected Zanna.Game.Lighting2D");
    if (!lit || radius <= 0 || lit->tile_count >= MAX_DYN_LIGHTS_CAP)
        return;
    struct rt_dyn_light *light = &lit->tile_lights[lit->tile_count++];
    light->x = screen_x;
    light->y = screen_y;
    light->radius = radius;
    light->color = color & 0xFFFFFF;
    light->life = 1;
    light->max_life = 1;
    light->active = 1;
}

/// @brief Deactivate every dynamic light slot at once; useful between scenes/levels.
void rt_lighting2d_clear_lights(rt_lighting2d lit) {
    lit = checked_lighting2d(lit, "Lighting2D.ClearLights: expected Zanna.Game.Lighting2D");
    if (!lit)
        return;
    for (int64_t i = 0; i < lit->max_lights; i++)
        lit->lights[i].active = 0;
    lit->light_count = 0;
    lit->tile_count = 0;
    memset(lit->tile_lights, 0, sizeof(lit->tile_lights));
}

/// @brief Per-frame tick: advance the player-light pulse (mod 120) and decrement every active
/// dynamic light's lifetime. Lights with `life <= 0` are returned to the pool.
void rt_lighting2d_update(rt_lighting2d lit) {
    lit = checked_lighting2d(lit, "Lighting2D.Update: expected Zanna.Game.Lighting2D");
    if (!lit)
        return;

    // Pulse player light
    lit->player_pulse++;
    if (lit->player_pulse >= 120)
        lit->player_pulse = 0;

    // Tick dynamic lights
    for (int64_t i = 0; i < lit->max_lights; i++) {
        if (lit->lights[i].active) {
            if (lit->lights[i].max_life > 0)
                lit->lights[i].life--;
            if (lit->lights[i].max_life > 0 && lit->lights[i].life <= 0) {
                lit->lights[i].active = 0;
                lit->light_count--;
            }
        }
    }
}

/// @brief Composite the lighting onto a canvas in three passes:
///   1. Full-screen tinted darkness overlay at `darkness` alpha.
///   2. Player light at `(player_sx, player_sy)` — outer glow + 6 concentric rings + bright core,
///      with radius modulated by an integer triangle wave (+0..10 px over 120 frames).
///   3. Dynamic lights, world-space positions converted to screen via `(cam_x, cam_y)`, with
///      alpha proportional to remaining lifetime so they fade gracefully.
/// Early-outs when darkness is zero (lighting effectively disabled).
void rt_lighting2d_draw(rt_lighting2d lit,
                        void *canvas,
                        int64_t cam_x,
                        int64_t cam_y,
                        int64_t player_sx,
                        int64_t player_sy) {
    lit = checked_lighting2d(lit, "Lighting2D.Draw: expected Zanna.Game.Lighting2D");
    if (!lit)
        return;
    if (!canvas || lit->darkness <= 0) {
        lit->tile_count = 0;
        return;
    }

    int64_t w = rt_canvas_width(canvas);
    int64_t h = rt_canvas_height(canvas);
    int64_t dark = lit->darkness;

    // Step 1: Full-screen darkness overlay
    rt_canvas_box_alpha(canvas, 0, 0, w, h, lit->tint_color, dark);

    // Step 2: Player light — concentric rings for soft falloff. A base radius of
    // zero disables the player light entirely (including the outer glow), so the
    // sentinel radius set by SetPlayerLight(0) is a real off switch (VDOC-271).
    if (lit->player_radius > 0) {
        int64_t pulse = 0;
        if (lit->player_pulse < 60)
            pulse = lit->player_pulse / 6;
        else
            pulse = (120 - lit->player_pulse) / 6;
        int64_t radius = lighting_add_sat_i64(lit->player_radius, pulse);
        if (radius < 0)
            radius = 0;

        // Outer glow
        rt_canvas_disc_alpha(
            canvas, player_sx, player_sy, lighting_add_sat_i64(radius, 40), lit->player_color, 30);
        // Main light rings
        for (int ring = 0; ring < 6; ring++) {
            int64_t r = radius - ring * (radius / 6);
            int64_t alpha = 40 + ring * 15;
            if (alpha > dark)
                alpha = dark;
            rt_canvas_disc_alpha(canvas, player_sx, player_sy, r, lit->player_color, alpha);
        }
        // Inner bright core
        rt_canvas_disc_alpha(
            canvas, player_sx, player_sy, radius / 4, lit->player_color, dark / 2);
    }

    // Step 3: Dynamic lights
    for (int64_t i = 0; i < lit->max_lights; i++) {
        if (!lit->lights[i].active)
            continue;

        int64_t lx = lighting_sub_sat_i64(lit->lights[i].x, cam_x);
        int64_t ly = lighting_sub_sat_i64(lit->lights[i].y, cam_y);
        int64_t lr = lit->lights[i].radius;
        int64_t lc = lit->lights[i].color;
        if (lr <= 0)
            continue;

        // Fade based on remaining life
        int64_t fade_alpha = dark;
        if (lit->lights[i].max_life > 0)
            fade_alpha =
                lighting_mul_div_sat_i64(dark, lit->lights[i].life, lit->lights[i].max_life);

        if (fade_alpha > 0) {
            rt_canvas_disc_alpha(canvas, lx, ly, lr, lc, fade_alpha / 2);
            rt_canvas_disc_alpha(canvas, lx, ly, lr / 2, lc, fade_alpha);
        }
    }

    // Step 4: Per-frame tile lights are already screen-space and are consumed by this draw.
    for (int64_t i = 0; i < lit->tile_count; i++) {
        if (!lit->tile_lights[i].active)
            continue;
        int64_t lr = lit->tile_lights[i].radius;
        if (lr <= 0)
            continue;
        rt_canvas_disc_alpha(canvas,
                             lit->tile_lights[i].x,
                             lit->tile_lights[i].y,
                             lr,
                             lit->tile_lights[i].color,
                             dark / 2);
        rt_canvas_disc_alpha(canvas,
                             lit->tile_lights[i].x,
                             lit->tile_lights[i].y,
                             lr / 2,
                             lit->tile_lights[i].color,
                             dark);
    }
    lit->tile_count = 0;
}

/// @brief Read the player light's base radius. A value of 0 means the player light is
/// disabled — Draw renders no player glow at all (VDOC-271). Returns 0 for null handles.
int64_t rt_lighting2d_get_player_radius(rt_lighting2d lit) {
    lit = checked_lighting2d(lit, "Lighting2D.PlayerRadius: expected Zanna.Game.Lighting2D");
    return lit ? lit->player_radius : 0;
}

/// @brief Number of currently active dynamic lights (excludes the always-on player light).
int64_t rt_lighting2d_get_light_count(rt_lighting2d lit) {
    lit = checked_lighting2d(lit, "Lighting2D.LightCount: expected Zanna.Game.Lighting2D");
    return lit ? lit->light_count : 0;
}
