//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/game/rt_screenfx.c
// Purpose: Screen effects manager for Viper games. Provides camera shake,
//   color flash, fade-in, and fade-out effects that are composited each frame.
//   Effects are stored in a small fixed-size slot array and updated with a
//   delta-time value (milliseconds). Multiple effects of different types can
//   run simultaneously; shake offsets are accumulated and the brightest
//   overlay alpha wins (max-alpha compositing).
//
// Key invariants:
//   - Up to RT_SCREENFX_MAX_EFFECTS simultaneous effects are supported. New
//     effects that find no free slot are silently dropped (not an error).
//   - Shake: uses per-instance LCG RNG (seeded from the object pointer) so
//     concurrent ScreenFX instances on different threads produce independent
//     random sequences without global state.
//   - Shake decay model: decay parameter controls the exponent applied to the
//     remaining-time factor (1 - progress):
//       decay == 0      → no decay (constant amplitude throughout)
//       decay == 1000   → linear decay: amplitude ∝ (1 - t)
//       decay >= 1500   → quadratic decay: amplitude ∝ (1 - t)²
//     The quadratic "trauma model" feels more natural for game camera shake.
//   - Color format for all effects is 0xRRGGBBAA (32-bit, alpha in low byte).
//     This differs from the Canvas drawing API which uses 0x00RRGGBB. Pass
//     colors through the ScreenFX API using this format, not Canvas colors.
//   - Flash alpha fades from base_alpha → 0 over the duration (starts bright).
//   - FadeIn alpha fades from base_alpha → 0 (fades FROM the color TO clear).
//   - FadeOut alpha fades from 0 → base_alpha (fades FROM clear TO the color).
//   - Starting a new FadeIn or FadeOut cancels any currently running fade.
//
// Ownership/Lifetime:
//   - ScreenFX objects are GC-managed via rt_obj_new_i64. rt_screenfx_destroy()
//     calls rt_obj_free() for callers that manage lifetimes explicitly, but GC
//     finalisation also reclaims the allocation automatically.
//
// Links: src/runtime/game/rt_screenfx.h (public API),
//        docs/viperlib/game.md (ScreenFX section, color format table)
//
//===----------------------------------------------------------------------===//

#include "rt_screenfx.h"
#include "rt_object.h"

#include <stdlib.h>
#include <string.h>

/// @brief Per-instance Linear Congruential Generator returning a 15-bit pseudo-random number.
/// Uses the classic `glibc` parameters (1103515245, 12345) and discards the low 16 bits to
/// improve distribution. State is owned by each ScreenFX instance, so concurrent instances on
/// different threads produce independent sequences without locks.
static int64_t screenfx_rand(int64_t *state) {
    *state = (*state) * 1103515245 + 12345;
    return ((*state) >> 16) & 0x7FFF;
}

/// Internal effect structure.
struct screenfx_effect {
    rt_screenfx_type_t type; ///< Effect type.
    int64_t color;           ///< Color (RGBA for old effects, RGB for transitions).
    int64_t intensity;       ///< Intensity (shake) / direction (wipe) / max_block (pixelate).
    int64_t duration;        ///< Total duration (ms).
    int64_t elapsed;         ///< Elapsed time (ms).
    int64_t decay;           ///< Decay rate (shake) / center_x (circle).
    int64_t extra;           ///< center_y (circle) / unused for others.
};

/// Internal manager structure.
struct rt_screenfx_impl {
    struct screenfx_effect effects[RT_SCREENFX_MAX_EFFECTS];
    int64_t shake_x;       ///< Current shake offset X.
    int64_t shake_y;       ///< Current shake offset Y.
    int64_t overlay_color; ///< Current overlay color (RGB).
    int64_t overlay_alpha; ///< Current overlay alpha (0-255).
    int64_t rand_state;    ///< Per-instance LCG state for shake RNG (thread-safe).
};

/// @brief Construct an empty ScreenFX manager. RNG state is seeded from the object pointer
/// XOR 0xDEADBEEF so each instance gets a unique deterministic sequence. Returns a GC-managed
/// handle; NULL on allocation failure.
rt_screenfx rt_screenfx_new(void) {
    struct rt_screenfx_impl *fx =
        (struct rt_screenfx_impl *)rt_obj_new_i64(0, (int64_t)sizeof(struct rt_screenfx_impl));
    if (!fx)
        return NULL;

    memset(fx, 0, sizeof(struct rt_screenfx_impl));
    fx->rand_state = (int64_t)(uintptr_t)fx ^ 0xDEADBEEF; // per-instance seed
    return fx;
}

/// @brief Release the ScreenFX manager; frees the inline struct when refcount hits zero.
/// Provided for API symmetry — GC finalization also reclaims the allocation automatically.
void rt_screenfx_destroy(rt_screenfx fx) {
    if (fx && rt_obj_release_check0(fx))
        rt_obj_free(fx);
}

/// @brief Linear scan for the first slot whose `type == RT_SCREENFX_NONE`. Returns -1 when full.
static int find_free_slot(rt_screenfx fx) {
    for (int i = 0; i < RT_SCREENFX_MAX_EFFECTS; i++) {
        if (fx->effects[i].type == RT_SCREENFX_NONE)
            return i;
    }
    return -1;
}

/// @brief Locate an existing effect of the given type so it can be reused/restarted (e.g. shake
/// only ever lives in one slot — re-triggering replaces in place rather than allocating).
static int find_effect_of_type(rt_screenfx fx, rt_screenfx_type_t type) {
    for (int i = 0; i < RT_SCREENFX_MAX_EFFECTS; i++) {
        if (fx->effects[i].type == type)
            return i;
    }
    return -1;
}

/// @brief Per-frame tick: advance every active effect by `dt` ms and recompute the composited
/// shake offset + overlay color/alpha. Behavior per type:
///   - **SHAKE:** decay-modulated random offset accumulated into shake_x/shake_y. Decay model:
///     `decay <= 0` constant amplitude; `decay >= 1500` quadratic (trauma); else linear.
///   - **FLASH / FADE_IN:** alpha fades from `base_alpha → 0` over the duration.
///   - **FADE_OUT:** alpha fades from `0 → base_alpha`.
///   - **WIPE / CIRCLE_* / DISSOLVE / PIXELATE:** time-only advance; rendering happens in `draw()`.
/// Multiple overlays use **max-alpha compositing** — the brightest active overlay wins.
/// Effects whose elapsed time exceeds duration are reclaimed (slot type → NONE).
void rt_screenfx_update(rt_screenfx fx, int64_t dt) {
    if (!fx)
        return;

    // Reset accumulators
    fx->shake_x = 0;
    fx->shake_y = 0;
    fx->overlay_alpha = 0;
    fx->overlay_color = 0;

    int64_t max_shake_intensity = 0;
    int64_t max_overlay_alpha = 0;

    for (int i = 0; i < RT_SCREENFX_MAX_EFFECTS; i++) {
        struct screenfx_effect *e = &fx->effects[i];
        if (e->type == RT_SCREENFX_NONE)
            continue;

        e->elapsed += dt;

        // Check if effect has finished
        if (e->elapsed >= e->duration) {
            e->type = RT_SCREENFX_NONE;
            continue;
        }

        // Calculate progress (0-1000)
        int64_t progress = (e->elapsed * 1000) / e->duration;

        switch (e->type) {
            case RT_SCREENFX_SHAKE: {
                // Exponential decay: intensity falls as (1 - progress/1000)^decay_exp
                // where decay_exp is controlled by e->decay (higher = faster decay).
                // When decay == 0 → no decay (constant intensity).
                // When decay == 1000 → approximately linear decay.
                // When decay == 2000 → quadratic (trauma model: natural feel).
                int64_t current_intensity;
                if (e->decay <= 0) {
                    current_intensity = e->intensity;
                } else {
                    // Use integer approximation of (1 - t)^2 for decay==2000 default
                    // General form: factor = (1000 - progress)^(decay/1000) / 1000
                    int64_t remaining = 1000 - progress; // 0..1000
                    if (remaining < 0)
                        remaining = 0;
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

            case RT_SCREENFX_FLASH: {
                // Flash starts bright and fades
                // Color format: 0xRRGGBBAA — alpha in low byte
                int64_t alpha = ((e->color & 0xFF) * (1000 - progress)) / 1000;
                if (alpha > max_overlay_alpha) {
                    max_overlay_alpha = alpha;
                    fx->overlay_color = e->color & 0xFFFFFF00;
                    fx->overlay_alpha = alpha;
                }
                break;
            }

            case RT_SCREENFX_FADE_IN: {
                // Fade from color to clear
                int64_t base_alpha = e->color & 0xFF;
                int64_t alpha = (base_alpha * (1000 - progress)) / 1000;
                if (alpha > max_overlay_alpha) {
                    max_overlay_alpha = alpha;
                    fx->overlay_color = e->color & 0xFFFFFF00;
                    fx->overlay_alpha = alpha;
                }
                break;
            }

            case RT_SCREENFX_FADE_OUT: {
                // Fade from clear to color
                int64_t base_alpha = e->color & 0xFF;
                int64_t alpha = (base_alpha * progress) / 1000;
                if (alpha > max_overlay_alpha) {
                    max_overlay_alpha = alpha;
                    fx->overlay_color = e->color & 0xFFFFFF00;
                    fx->overlay_alpha = alpha;
                }
                break;
            }

            case RT_SCREENFX_WIPE:
            case RT_SCREENFX_CIRCLE_IN:
            case RT_SCREENFX_CIRCLE_OUT:
            case RT_SCREENFX_DISSOLVE:
            case RT_SCREENFX_PIXELATE:
                // Transitions are rendered via Draw(); update only advances time.
                break;

            default:
                break;
        }
    }
}

/// @brief Trigger a camera shake: random per-frame offset of magnitude `intensity` (pixels),
/// damped over `duration` ms by the `decay` model (0 = constant, 1000 = linear, ≥1500 = quadratic).
/// Replaces any existing shake in place — only one shake runs at a time.
void rt_screenfx_shake(rt_screenfx fx, int64_t intensity, int64_t duration, int64_t decay) {
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

/// @brief Trigger a one-shot color flash. `color` is 0xRRGGBBAA (alpha encodes peak intensity);
/// alpha fades from peak to 0 over `duration` ms. Useful for hit-frames, lightning, screen blasts.
/// Multiple simultaneous flashes pick the one with the highest current alpha.
void rt_screenfx_flash(rt_screenfx fx, int64_t color, int64_t duration) {
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

/// @brief Fade FROM the color TO clear (alpha goes peak→0). Used at scene-entry — start covered,
/// reveal underlying gameplay. Cancels any in-flight FADE_IN/FADE_OUT first so fades don't stack.
void rt_screenfx_fade_in(rt_screenfx fx, int64_t color, int64_t duration) {
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

/// @brief Fade FROM clear TO the color (alpha goes 0→peak). Used at scene-exit — start clear,
/// end covered. Cancels any in-flight FADE_IN/FADE_OUT first.
void rt_screenfx_fade_out(rt_screenfx fx, int64_t color, int64_t duration) {
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

/// @brief Stop every active effect immediately and zero the composited shake/overlay state.
/// Use between scene transitions to guarantee a clean slate.
void rt_screenfx_cancel_all(rt_screenfx fx) {
    if (!fx)
        return;

    for (int i = 0; i < RT_SCREENFX_MAX_EFFECTS; i++)
        fx->effects[i].type = RT_SCREENFX_NONE;

    fx->shake_x = 0;
    fx->shake_y = 0;
    fx->overlay_color = 0;
    fx->overlay_alpha = 0;
}

/// @brief Stop every effect of a single type (e.g. cancel all FADE_IN slots before issuing a new one).
/// Composited state (shake/overlay) is NOT zeroed — they decay naturally on the next `update()`.
void rt_screenfx_cancel_type(rt_screenfx fx, int64_t type) {
    if (!fx)
        return;

    for (int i = 0; i < RT_SCREENFX_MAX_EFFECTS; i++) {
        if (fx->effects[i].type == (rt_screenfx_type_t)type)
            fx->effects[i].type = RT_SCREENFX_NONE;
    }
}

/// @brief Returns 1 if any effect slot is currently in use.
int8_t rt_screenfx_is_active(rt_screenfx fx) {
    if (!fx)
        return 0;

    for (int i = 0; i < RT_SCREENFX_MAX_EFFECTS; i++) {
        if (fx->effects[i].type != RT_SCREENFX_NONE)
            return 1;
    }
    return 0;
}

/// @brief Returns 1 if at least one slot of the given effect type is active. Useful for guarding
/// "don't restart this effect if it's still running" patterns.
int8_t rt_screenfx_is_type_active(rt_screenfx fx, int64_t type) {
    if (!fx)
        return 0;

    for (int i = 0; i < RT_SCREENFX_MAX_EFFECTS; i++) {
        if (fx->effects[i].type == (rt_screenfx_type_t)type)
            return 1;
    }
    return 0;
}

/// @brief Read the composited X-axis camera-shake offset for the current frame. Caller adds this
/// to the camera position before drawing the world.
int64_t rt_screenfx_get_shake_x(rt_screenfx fx) {
    return fx ? fx->shake_x : 0;
}

/// @brief Read the composited Y-axis camera-shake offset for the current frame.
int64_t rt_screenfx_get_shake_y(rt_screenfx fx) {
    return fx ? fx->shake_y : 0;
}

/// @brief Read the current overlay color (RGB packed in upper 24 bits, alpha in low byte = 0).
/// Use together with `get_overlay_alpha` to render a full-screen tinted box on top of gameplay.
int64_t rt_screenfx_get_overlay_color(rt_screenfx fx) {
    return fx ? fx->overlay_color : 0;
}

/// @brief Read the current overlay alpha (0–255). Effects use max-alpha compositing — the
/// brightest active overlay (FLASH, FADE_IN, FADE_OUT) wins.
int64_t rt_screenfx_get_overlay_alpha(rt_screenfx fx) {
    return fx ? fx->overlay_alpha : 0;
}

//=============================================================================
// Transition Effects
//=============================================================================

/// @brief Trigger a directional wipe transition. `direction` ∈ {LEFT, RIGHT, UP, DOWN}; out-of-range
/// values default to LEFT. The colored rectangle grows from one screen edge across the duration,
/// rendered in `draw()`.
void rt_screenfx_wipe(rt_screenfx fx, int64_t direction, int64_t color, int64_t duration) {
    if (!fx || duration <= 0)
        return;
    if (direction < 0 || direction > 3)
        direction = RT_DIR_LEFT;

    int slot = find_free_slot(fx);
    if (slot < 0)
        return;

    struct screenfx_effect *e = &fx->effects[slot];
    e->type = RT_SCREENFX_WIPE;
    e->color = color;
    e->intensity = direction;
    e->duration = duration;
    e->elapsed = 0;
    e->decay = 0;
    e->extra = 0;
}

/// @brief Iris-out / "closing aperture" transition centered at (cx, cy). A colored region grows
/// inward, leaving a shrinking unobscured circle. Implemented in `draw()` as four rectangles
/// surrounding the visible circle (cheap; no per-pixel mask required).
void rt_screenfx_circle_in(
    rt_screenfx fx, int64_t cx, int64_t cy, int64_t color, int64_t duration) {
    if (!fx || duration <= 0)
        return;

    int slot = find_free_slot(fx);
    if (slot < 0)
        return;

    struct screenfx_effect *e = &fx->effects[slot];
    e->type = RT_SCREENFX_CIRCLE_IN;
    e->color = color;
    e->intensity = 0;
    e->duration = duration;
    e->elapsed = 0;
    e->decay = cx;
    e->extra = cy;
}

/// @brief Iris-in / "opening aperture" transition centered at (cx, cy). The colored region recedes
/// from the screen as a growing visible circle reveals the underlying scene. The reverse companion
/// to `circle_in` — pair them across a scene boundary for a smooth iris transition.
void rt_screenfx_circle_out(
    rt_screenfx fx, int64_t cx, int64_t cy, int64_t color, int64_t duration) {
    if (!fx || duration <= 0)
        return;

    int slot = find_free_slot(fx);
    if (slot < 0)
        return;

    struct screenfx_effect *e = &fx->effects[slot];
    e->type = RT_SCREENFX_CIRCLE_OUT;
    e->color = color;
    e->intensity = 0;
    e->duration = duration;
    e->elapsed = 0;
    e->decay = cx;
    e->extra = cy;
}

/// @brief Trigger an ordered-dither dissolve transition. Uses a 4×4 Bayer matrix (Mac-style
/// dissolve): each pixel's threshold is its `bayer4x4[y%4][x%4]` value, and a global threshold
/// sweeps 0→255 over the duration. Visually a pleasing "scatter" effect; rendered pixel-by-pixel
/// in `draw()` (avoid for very high-resolution canvases).
void rt_screenfx_dissolve(rt_screenfx fx, int64_t color, int64_t duration) {
    if (!fx || duration <= 0)
        return;

    int slot = find_free_slot(fx);
    if (slot < 0)
        return;

    struct screenfx_effect *e = &fx->effects[slot];
    e->type = RT_SCREENFX_DISSOLVE;
    e->color = color;
    e->duration = duration;
    e->elapsed = 0;
    e->intensity = 0;
    e->decay = 0;
    e->extra = 0;
}

/// @brief Trigger a "pixelate" transition. Block size grows from 1→`max_block_size` (clamped
/// minimum 2) over the duration. The runtime cannot read pixels back, so this is approximated
/// by drawing a darkening grid overlay rather than true block-averaging — the visual hint of
/// pixelation without the readback cost.
void rt_screenfx_pixelate(rt_screenfx fx, int64_t max_block_size, int64_t duration) {
    if (!fx || duration <= 0)
        return;
    if (max_block_size < 2)
        max_block_size = 2;

    int slot = find_free_slot(fx);
    if (slot < 0)
        return;

    struct screenfx_effect *e = &fx->effects[slot];
    e->type = RT_SCREENFX_PIXELATE;
    e->color = 0;
    e->intensity = max_block_size;
    e->duration = duration;
    e->elapsed = 0;
    e->decay = 0;
    e->extra = 0;
}

/// @brief Returns 1 when no slots are active. Useful for chaining transitions ("when fade-out
/// finishes, load the next scene"). Treats a NULL handle as finished (returns 1).
int8_t rt_screenfx_is_finished(rt_screenfx fx) {
    if (!fx)
        return 1;

    for (int i = 0; i < RT_SCREENFX_MAX_EFFECTS; i++) {
        if (fx->effects[i].type != RT_SCREENFX_NONE)
            return 0;
    }
    return 1;
}

/// @brief Read the progress (0–1000 per mille) of the first active transition effect (WIPE,
/// CIRCLE_*, DISSOLVE, PIXELATE). Returns 0 when no transition is running, 1000 when complete.
/// Used to drive scene-load timing — kick off the next scene at progress=500 (mid-transition).
int64_t rt_screenfx_get_transition_progress(rt_screenfx fx) {
    if (!fx)
        return 0;

    // Find the first active transition effect and return its progress
    for (int i = 0; i < RT_SCREENFX_MAX_EFFECTS; i++) {
        struct screenfx_effect *e = &fx->effects[i];
        if (e->type >= RT_SCREENFX_WIPE && e->type <= RT_SCREENFX_PIXELATE) {
            if (e->duration <= 0)
                return 1000;
            int64_t p = (e->elapsed * 1000) / e->duration;
            return p > 1000 ? 1000 : p;
        }
    }
    return 0;
}

//=============================================================================
// Draw — Render transitions to Canvas
//=============================================================================

// Forward declarations for Canvas drawing functions used here.
// These are declared in rt_graphics.h which collections can include.
#include "rt_graphics.h"

/// @brief 4×4 Bayer dithering matrix (values 0-15, scaled to 0-255 range).
static const uint8_t bayer4x4[4][4] = {
    {0, 128, 32, 160},
    {192, 64, 224, 96},
    {48, 176, 16, 144},
    {240, 112, 208, 80},
};

/// @brief Render every active transition effect onto the canvas, plus the FLASH/FADE_*-driven
/// alpha overlay accumulated by `update()`. Per-effect drawing strategies:
///   - **WIPE:** one growing rectangle from the chosen edge.
///   - **CIRCLE_IN/OUT:** four edge rectangles around a shrinking/growing visible circle (uses
///     Manhattan max-radius for cheap full-corner coverage; no isqrt required).
///   - **DISSOLVE:** per-pixel ordered Bayer dither vs a global 0→255 threshold.
///   - **PIXELATE:** semi-transparent grid overlay simulating block-pixelation without read-back.
/// Caller provides the canvas dimensions explicitly so this function never queries the canvas.
void rt_screenfx_draw(rt_screenfx fx, void *canvas, int64_t screen_w, int64_t screen_h) {
    if (!fx || !canvas || screen_w <= 0 || screen_h <= 0)
        return;

    // Also draw overlay for existing fade/flash effects
    if (fx->overlay_alpha > 0) {
        rt_canvas_box_alpha(
            canvas, 0, 0, screen_w, screen_h, fx->overlay_color >> 8, fx->overlay_alpha);
    }

    for (int i = 0; i < RT_SCREENFX_MAX_EFFECTS; i++) {
        struct screenfx_effect *e = &fx->effects[i];
        if (e->type == RT_SCREENFX_NONE)
            continue;
        if (e->duration <= 0)
            continue;

        int64_t progress = (e->elapsed * 1000) / e->duration;
        if (progress > 1000)
            progress = 1000;

        switch (e->type) {
            case RT_SCREENFX_WIPE: {
                int64_t dir = e->intensity;
                int64_t color = e->color;

                switch (dir) {
                    case RT_DIR_RIGHT: {
                        int64_t w = screen_w * progress / 1000;
                        rt_canvas_box(canvas, screen_w - w, 0, w, screen_h, color);
                        break;
                    }
                    case RT_DIR_UP: {
                        int64_t h = screen_h * progress / 1000;
                        rt_canvas_box(canvas, 0, 0, screen_w, h, color);
                        break;
                    }
                    case RT_DIR_DOWN: {
                        int64_t h = screen_h * progress / 1000;
                        rt_canvas_box(canvas, 0, screen_h - h, screen_w, h, color);
                        break;
                    }
                    default: // LEFT
                    {
                        int64_t w = screen_w * progress / 1000;
                        rt_canvas_box(canvas, 0, 0, w, screen_h, color);
                        break;
                    }
                }
                break;
            }

            case RT_SCREENFX_CIRCLE_IN: {
                // Circle closes: starts at max radius, shrinks to 0.
                // We draw the color everywhere EXCEPT inside the circle.
                // Approximate with 4 rectangles forming a frame around the shrinking circle.
                int64_t cx = e->decay;
                int64_t cy = e->extra;
                int64_t color = e->color;

                // Max radius = distance from center to farthest corner
                int64_t dx1 = cx > screen_w - cx ? cx : screen_w - cx;
                int64_t dy1 = cy > screen_h - cy ? cy : screen_h - cy;
                int64_t max_r = dx1 + dy1; // Manhattan approximation (good enough)

                // Current radius shrinks from max_r to 0
                int64_t radius = max_r * (1000 - progress) / 1000;

                // Draw color boxes on all four sides of the circle region
                // Top
                if (cy - radius > 0)
                    rt_canvas_box(canvas, 0, 0, screen_w, cy - radius, color);
                // Bottom
                if (cy + radius < screen_h)
                    rt_canvas_box(
                        canvas, 0, cy + radius, screen_w, screen_h - (cy + radius), color);
                // Left
                int64_t ytop = cy - radius > 0 ? cy - radius : 0;
                int64_t ybot = cy + radius < screen_h ? cy + radius : screen_h;
                if (cx - radius > 0)
                    rt_canvas_box(canvas, 0, ytop, cx - radius, ybot - ytop, color);
                // Right
                if (cx + radius < screen_w)
                    rt_canvas_box(
                        canvas, cx + radius, ytop, screen_w - (cx + radius), ybot - ytop, color);
                break;
            }

            case RT_SCREENFX_CIRCLE_OUT: {
                // Circle opens: starts at 0 radius, expands to reveal.
                // We draw color everywhere EXCEPT inside the growing circle.
                int64_t cx = e->decay;
                int64_t cy = e->extra;
                int64_t color = e->color;

                int64_t dx1 = cx > screen_w - cx ? cx : screen_w - cx;
                int64_t dy1 = cy > screen_h - cy ? cy : screen_h - cy;
                int64_t max_r = dx1 + dy1;

                int64_t radius = max_r * progress / 1000;

                // Draw surrounding boxes (same as circle_in but with growing radius)
                if (cy - radius > 0)
                    rt_canvas_box(canvas, 0, 0, screen_w, cy - radius, color);
                if (cy + radius < screen_h)
                    rt_canvas_box(
                        canvas, 0, cy + radius, screen_w, screen_h - (cy + radius), color);
                int64_t ytop = cy - radius > 0 ? cy - radius : 0;
                int64_t ybot = cy + radius < screen_h ? cy + radius : screen_h;
                if (cx - radius > 0)
                    rt_canvas_box(canvas, 0, ytop, cx - radius, ybot - ytop, color);
                if (cx + radius < screen_w)
                    rt_canvas_box(
                        canvas, cx + radius, ytop, screen_w - (cx + radius), ybot - ytop, color);
                break;
            }

            case RT_SCREENFX_DISSOLVE: {
                // Bayer dithering: threshold increases with progress.
                // Pixels where bayer[x%4][y%4] < threshold get the overlay color.
                int64_t threshold = progress * 256 / 1000; // 0-255
                int64_t color = e->color;

                // Draw in 4×4 tile blocks for efficiency
                for (int64_t ty = 0; ty < screen_h; ty += 4) {
                    for (int64_t tx = 0; tx < screen_w; tx += 4) {
                        for (int by = 0; by < 4 && ty + by < screen_h; by++) {
                            for (int bx = 0; bx < 4 && tx + bx < screen_w; bx++) {
                                if (bayer4x4[by][bx] < (uint8_t)threshold) {
                                    rt_canvas_plot(canvas, tx + bx, ty + by, color);
                                }
                            }
                        }
                    }
                }
                break;
            }

            case RT_SCREENFX_PIXELATE: {
                // Block size grows from 1 to max_block_size over duration.
                // We render colored blocks at increasing sizes.
                // Note: true pixelation requires reading back pixels — we approximate
                // by drawing a grid of solid blocks with the average color.
                // Since we can't read pixels without a canvas read-back, we draw
                // a semi-transparent overlay grid to simulate the effect.
                int64_t max_block = e->intensity;
                int64_t block = 1 + (max_block - 1) * progress / 1000;
                if (block < 2)
                    break; // No visible effect at block=1

                // Draw grid lines to create pixelation appearance
                int64_t alpha = progress * 128 / 1000; // Gradually increasing
                if (alpha > 128)
                    alpha = 128;

                for (int64_t gy = 0; gy < screen_h; gy += block) {
                    rt_canvas_box_alpha(canvas, 0, gy, screen_w, 1, 0x000000, alpha);
                }
                for (int64_t gx = 0; gx < screen_w; gx += block) {
                    rt_canvas_box_alpha(canvas, gx, 0, 1, screen_h, 0x000000, alpha);
                }
                break;
            }

            default:
                break;
        }
    }
}
