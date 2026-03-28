//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
// File: src/runtime/game/rt_lighting2d.c
// Purpose: 2D darkness overlay with pulsing player light and pooled dynamic
//   lights. Renders a full-screen tinted overlay at configurable alpha, then
//   punches "holes" via concentric DiscAlpha rings for the player light, tile
//   lights, and time-limited dynamic lights (explosions, bullets, pickups).
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
#include "rt_object.h"
#include <stdlib.h>
#include <string.h>

// Forward declarations for canvas draw functions
extern void rt_canvas_box_alpha(void *canvas, int64_t x, int64_t y,
                                int64_t w, int64_t h, int64_t color, int64_t alpha);
extern void rt_canvas_disc_alpha(void *canvas, int64_t cx, int64_t cy,
                                 int64_t radius, int64_t color, int64_t alpha);
extern int64_t rt_canvas_width(void *canvas);
extern int64_t rt_canvas_height(void *canvas);

#define MAX_DYN_LIGHTS_CAP 128

struct rt_dyn_light
{
    int64_t x, y;
    int64_t radius;
    int64_t color;
    int64_t life;
    int64_t max_life;
    int8_t active;
};

struct rt_lighting2d_impl
{
    int64_t darkness;         // 0-255 overlay alpha
    int64_t tint_color;       // Darkness tint (0xRRGGBB)
    int64_t player_radius;    // Player light radius
    int64_t player_color;     // Player light color
    int64_t player_pulse;     // Internal pulse timer (0-119)
    int64_t max_lights;       // Pool capacity
    int64_t light_count;      // Active light count
    struct rt_dyn_light lights[MAX_DYN_LIGHTS_CAP];
};

rt_lighting2d rt_lighting2d_new(int64_t max_lights)
{
    struct rt_lighting2d_impl *lit =
        (struct rt_lighting2d_impl *)rt_obj_new_i64(0, (int64_t)sizeof(struct rt_lighting2d_impl));
    if (!lit)
        return NULL;

    lit->darkness = 0;
    lit->tint_color = 0x00000A; // Default: near-black with blue tint
    lit->player_radius = 180;
    lit->player_color = 0x303240;
    lit->player_pulse = 0;
    lit->max_lights = (max_lights > MAX_DYN_LIGHTS_CAP) ? MAX_DYN_LIGHTS_CAP : max_lights;
    lit->light_count = 0;
    memset(lit->lights, 0, sizeof(lit->lights));

    return lit;
}

void rt_lighting2d_destroy(rt_lighting2d lit)
{
    if (lit && rt_obj_release_check0(lit))
        rt_obj_free(lit);
}

void rt_lighting2d_set_darkness(rt_lighting2d lit, int64_t alpha)
{
    if (!lit)
        return;
    if (alpha < 0)
        alpha = 0;
    if (alpha > 255)
        alpha = 255;
    lit->darkness = alpha;
}

int64_t rt_lighting2d_get_darkness(rt_lighting2d lit)
{
    return lit ? lit->darkness : 0;
}

void rt_lighting2d_set_tint_color(rt_lighting2d lit, int64_t color)
{
    if (lit)
        lit->tint_color = color;
}

int64_t rt_lighting2d_get_tint_color(rt_lighting2d lit)
{
    return lit ? lit->tint_color : 0;
}

void rt_lighting2d_set_player_light(rt_lighting2d lit, int64_t radius, int64_t color)
{
    if (!lit)
        return;
    lit->player_radius = radius;
    lit->player_color = color;
}

void rt_lighting2d_add_light(rt_lighting2d lit, int64_t x, int64_t y,
                             int64_t radius, int64_t color, int64_t lifetime)
{
    if (!lit || lifetime <= 0)
        return;

    // Find an inactive slot
    for (int64_t i = 0; i < lit->max_lights; i++)
    {
        if (!lit->lights[i].active)
        {
            lit->lights[i].x = x;
            lit->lights[i].y = y;
            lit->lights[i].radius = radius;
            lit->lights[i].color = color;
            lit->lights[i].life = lifetime;
            lit->lights[i].max_life = lifetime;
            lit->lights[i].active = 1;
            lit->light_count++;
            return;
        }
    }
    // Pool full — silently drop
}

void rt_lighting2d_add_tile_light(rt_lighting2d lit, int64_t screen_x,
                                  int64_t screen_y, int64_t radius, int64_t color)
{
    // Tile lights are immediate-draw, not pooled. Stored temporarily for the
    // current frame's draw call. For simplicity, we add them as short-lived
    // dynamic lights (1 frame lifetime).
    rt_lighting2d_add_light(lit, screen_x, screen_y, radius, color, 1);
}

void rt_lighting2d_clear_lights(rt_lighting2d lit)
{
    if (!lit)
        return;
    for (int64_t i = 0; i < lit->max_lights; i++)
        lit->lights[i].active = 0;
    lit->light_count = 0;
}

void rt_lighting2d_update(rt_lighting2d lit)
{
    if (!lit)
        return;

    // Pulse player light
    lit->player_pulse++;
    if (lit->player_pulse >= 120)
        lit->player_pulse = 0;

    // Tick dynamic lights
    for (int64_t i = 0; i < lit->max_lights; i++)
    {
        if (lit->lights[i].active)
        {
            lit->lights[i].life--;
            if (lit->lights[i].life <= 0)
            {
                lit->lights[i].active = 0;
                lit->light_count--;
            }
        }
    }
}

void rt_lighting2d_draw(rt_lighting2d lit, void *canvas,
                        int64_t cam_x, int64_t cam_y,
                        int64_t player_sx, int64_t player_sy)
{
    if (!lit || !canvas || lit->darkness <= 0)
        return;

    int64_t w = rt_canvas_width(canvas);
    int64_t h = rt_canvas_height(canvas);
    int64_t dark = lit->darkness;

    // Step 1: Full-screen darkness overlay
    rt_canvas_box_alpha(canvas, 0, 0, w, h, lit->tint_color, dark);

    // Step 2: Player light — concentric rings for soft falloff
    int64_t pulse = 0;
    if (lit->player_pulse < 60)
        pulse = lit->player_pulse / 6;
    else
        pulse = (120 - lit->player_pulse) / 6;
    int64_t radius = lit->player_radius + pulse;

    // Outer glow
    rt_canvas_disc_alpha(canvas, player_sx, player_sy, radius + 40,
                         lit->player_color, 30);
    // Main light rings
    for (int ring = 0; ring < 6; ring++)
    {
        int64_t r = radius - ring * (radius / 6);
        int64_t alpha = 40 + ring * 15;
        if (alpha > dark)
            alpha = dark;
        rt_canvas_disc_alpha(canvas, player_sx, player_sy, r,
                             lit->player_color, alpha);
    }
    // Inner bright core
    rt_canvas_disc_alpha(canvas, player_sx, player_sy, radius / 4,
                         lit->player_color, dark / 2);

    // Step 3: Dynamic lights
    for (int64_t i = 0; i < lit->max_lights; i++)
    {
        if (!lit->lights[i].active)
            continue;

        int64_t lx = lit->lights[i].x - cam_x;
        int64_t ly = lit->lights[i].y - cam_y;
        int64_t lr = lit->lights[i].radius;
        int64_t lc = lit->lights[i].color;

        // Fade based on remaining life
        int64_t fade_alpha = dark;
        if (lit->lights[i].max_life > 0)
            fade_alpha = dark * lit->lights[i].life / lit->lights[i].max_life;

        if (fade_alpha > 0)
        {
            rt_canvas_disc_alpha(canvas, lx, ly, lr, lc, fade_alpha / 2);
            rt_canvas_disc_alpha(canvas, lx, ly, lr / 2, lc, fade_alpha);
        }
    }
}

int64_t rt_lighting2d_get_light_count(rt_lighting2d lit)
{
    return lit ? lit->light_count : 0;
}
