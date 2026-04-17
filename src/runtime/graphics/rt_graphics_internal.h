//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_graphics_internal.h
// Purpose: Shared internal definitions for the rt_graphics subsystem. Provides
//   the rt_canvas struct, the rt_pixels_impl forward declaration, common
//   includes, and static helper functions used across rt_canvas.c,
//   rt_drawing.c, and rt_drawing_advanced.c.
//
// Key invariants:
//   - This header is internal to the graphics subsystem; never include from
//     outside src/runtime/graphics/.
//   - All helpers are declared static inline to avoid duplicate-symbol errors
//     when included from multiple translation units.
//   - The rt_canvas struct layout must remain identical across all files.
//
// Ownership/Lifetime:
//   - No allocations; struct definitions and pure helper functions only.
//
// Links: rt_graphics.h (public API), rt_canvas.c, rt_drawing.c,
//        rt_drawing_advanced.c
//
//===----------------------------------------------------------------------===//
#pragma once

#include "rt_graphics.h"
#include "rt_string.h"
#include "rt_trap.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//=============================================================================
// Static inline helpers — available to all translation units that include this
//=============================================================================

/// @brief Absolute value for int64_t.
static inline int64_t rtg_abs64(int64_t x) {
    return x < 0 ? -x : x;
}

/// @brief Minimum of two int64_t values.
static inline int64_t rtg_min64(int64_t a, int64_t b) {
    return a < b ? a : b;
}

/// @brief Maximum of two int64_t values.
static inline int64_t rtg_max64(int64_t a, int64_t b) {
    return a > b ? a : b;
}

/// @brief Simple sine approximation using a lookup table (degrees).
/// @return sin(deg) * 1024 for fixed-point precision.
static inline int64_t rtg_sin_deg_fp(int64_t deg) {
    // Normalize to 0-359
    deg = deg % 360;
    if (deg < 0)
        deg += 360;

    // sin values * 1024 for 0, 10, 20, ... 120 degrees
    static const int64_t sin_table[13] = {
        0, 176, 342, 500, 643, 766, 866, 940, 985, 1004, 992, 951, 883};

    int64_t sign = 1;
    if (deg >= 180) {
        deg -= 180;
        sign = -1;
    }
    if (deg > 90) {
        deg = 180 - deg;
    }

    int64_t val;
    if (deg <= 90) {
        int64_t i = deg / 10;
        if (i > 9)
            i = 9;
        val = sin_table[i];
    } else {
        int64_t i = (180 - deg) / 10;
        if (i > 9)
            i = 9;
        val = sin_table[i];
    }

    return sign * val;
}

/// @brief Simple cosine approximation using sin_deg_fp(deg + 90).
/// @return cos(deg) * 1024 for fixed-point precision.
static inline int64_t rtg_cos_deg_fp(int64_t deg) {
    return rtg_sin_deg_fp(deg + 90);
}

/// @brief Convert RGB (0-255 each) to HSL.
/// @param h Output hue (0-360).
/// @param s Output saturation (0-100).
/// @param l Output lightness (0-100).
static inline void rtg_rgb_to_hsl(
    int64_t r, int64_t g, int64_t b, int64_t *h, int64_t *s, int64_t *l) {
    int64_t max_c = (r > g) ? (r > b ? r : b) : (g > b ? g : b);
    int64_t min_c = (r < g) ? (r < b ? r : b) : (g < b ? g : b);
    int64_t delta = max_c - min_c;

    *l = (max_c + min_c) * 100 / 510;

    if (delta == 0) {
        *h = 0;
        *s = 0;
    } else {
        if (*l <= 50)
            *s = delta * 100 / (max_c + min_c);
        else
            *s = delta * 100 / (510 - max_c - min_c);

        if (max_c == r)
            *h = ((g - b) * 60 / delta + 360) % 360;
        else if (max_c == g)
            *h = (b - r) * 60 / delta + 120;
        else
            *h = (r - g) * 60 / delta + 240;

        if (*h < 0)
            *h += 360;
    }
}

/// @brief Hue to RGB conversion helper for HSL.
static inline int64_t rtg_hue_to_rgb_helper(int64_t p, int64_t q, int64_t t) {
    if (t < 0)
        t += 360;
    if (t > 360)
        t -= 360;
    if (t < 60)
        return p + (q - p) * t / 60;
    if (t < 180)
        return q;
    if (t < 240)
        return p + (q - p) * (240 - t) / 60;
    return p;
}

/// @brief Convert HSL to RGB.
/// @param h Hue (0-360).
/// @param s Saturation (0-100).
/// @param l Lightness (0-100).
/// @param r Output red (0-255).
/// @param g Output green (0-255).
/// @param b Output blue (0-255).
static inline void rtg_hsl_to_rgb(
    int64_t h, int64_t s, int64_t l, int64_t *r, int64_t *g, int64_t *b) {
    if (s == 0) {
        *r = *g = *b = l * 255 / 100;
        return;
    }

    int64_t q = (l < 50) ? (l * (100 + s) / 100) : (l + s - l * s / 100);
    int64_t p = 2 * l - q;

    *r = rtg_hue_to_rgb_helper(p, q, h + 120) * 255 / 100;
    *g = rtg_hue_to_rgb_helper(p, q, h) * 255 / 100;
    *b = rtg_hue_to_rgb_helper(p, q, h - 120) * 255 / 100;

    if (*r < 0)
        *r = 0;
    if (*r > 255)
        *r = 255;
    if (*g < 0)
        *g = 0;
    if (*g > 255)
        *g = 255;
    if (*b < 0)
        *b = 0;
    if (*b > 255)
        *b = 255;
}

#ifdef VIPER_ENABLE_GRAPHICS

#include "rt_action.h"
#include "rt_font.h"
#include "rt_input.h"
#include "rt_object.h"
#include "rt_pixels.h"

#include "vgfx.h"

/* Internal input teardown helpers used by canvas lifecycle code. */
void rt_keyboard_clear_canvas_if_matches(void *canvas);
void rt_mouse_clear_canvas_if_matches(void *canvas);

/// @brief Magic value used to reject accidental calls with non-Canvas objects.
#define RT_CANVAS_MAGIC 0x564950455243414eLL /* "VIPERCAN" */

/// @brief Internal canvas wrapper structure.
/// @details Contains the vptr (for future OOP support), a runtime magic guard,
///   and the vgfx window handle.
typedef struct {
    void *vptr;              ///< VTable pointer (reserved for future use)
    int64_t magic;           ///< RT_CANVAS_MAGIC while this object is a live Canvas
    vgfx_window_t gfx_win;   ///< ViperGFX window handle
    int64_t should_close;    ///< Close request flag
    vgfx_event_t last_event; ///< Last polled event for retrieval
    char *title;             ///< Cached window title (heap-allocated, freed in finalizer)
    int64_t last_flip_us;    ///< Monotonic time (microseconds) of last Flip()
    int64_t delta_time_ms;   ///< Milliseconds elapsed between the last two Flip() calls
    int64_t dt_max_ms;       ///< Maximum delta time clamp (0 = no clamping)
    int8_t clip_enabled;     ///< Runtime mirror of the logical clip rect state
    int64_t clip_x;          ///< Logical clip X
    int64_t clip_y;          ///< Logical clip Y
    int64_t clip_w;          ///< Logical clip width
    int64_t clip_h;          ///< Logical clip height
} rt_canvas;

static inline rt_canvas *rt_canvas_checked(void *canvas_ptr) {
    if (!canvas_ptr)
        return NULL;
    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    return canvas->magic == RT_CANVAS_MAGIC ? canvas : NULL;
}

/// @brief Forward declaration for pixels internal access.
typedef struct rt_pixels_impl {
    int64_t width;
    int64_t height;
    uint32_t *data;
} rt_pixels_impl;

#define RT_COLOR_EXPLICIT_ALPHA_FLAG ((int64_t)1 << 56)

static inline float rtg_sanitize_scale(float scale) {
    return scale >= 1.0f ? scale : 1.0f;
}

static inline int64_t rtg_round_scaled(double value) {
    return (int64_t)(value >= 0.0 ? value + 0.5 : value - 0.5);
}

static inline int64_t rtg_scale_up_i64(int64_t logical, float scale) {
    return rtg_round_scaled((double)logical * (double)rtg_sanitize_scale(scale));
}

static inline int64_t rtg_scale_down_i64(int64_t physical, float scale) {
    return rtg_round_scaled((double)physical / (double)rtg_sanitize_scale(scale));
}

static inline void rt_canvas_resync_window_state(rt_canvas *canvas) {
    if (!canvas || !canvas->gfx_win)
        return;

    float scale = rtg_sanitize_scale(vgfx_window_get_scale(canvas->gfx_win));
    vgfx_set_coord_scale(canvas->gfx_win, scale);
    if (canvas->clip_enabled) {
        vgfx_set_clip(canvas->gfx_win,
                      (int32_t)canvas->clip_x,
                      (int32_t)canvas->clip_y,
                      (int32_t)canvas->clip_w,
                      (int32_t)canvas->clip_h);
    } else {
        vgfx_clear_clip(canvas->gfx_win);
    }
}

static inline int8_t rt_canvas_get_logical_clip_bounds(
    rt_canvas *canvas, int64_t *x, int64_t *y, int64_t *w, int64_t *h) {
    if (!canvas || !canvas->gfx_win || !x || !y || !w || !h)
        return 0;

    rt_canvas_resync_window_state(canvas);

    int32_t canvas_w = 0;
    int32_t canvas_h = 0;
    vgfx_get_size(canvas->gfx_win, &canvas_w, &canvas_h);

    int64_t clip_x = 0;
    int64_t clip_y = 0;
    int64_t clip_w = (int64_t)canvas_w;
    int64_t clip_h = (int64_t)canvas_h;
    if (canvas->clip_enabled) {
        clip_x = canvas->clip_x;
        clip_y = canvas->clip_y;
        clip_w = canvas->clip_w;
        clip_h = canvas->clip_h;
    }

    int64_t x0 = rtg_max64(0, clip_x);
    int64_t y0 = rtg_max64(0, clip_y);
    int64_t x1 = rtg_min64((int64_t)canvas_w, clip_x + clip_w);
    int64_t y1 = rtg_min64((int64_t)canvas_h, clip_y + clip_h);
    if (clip_w <= 0 || clip_h <= 0 || x1 <= x0 || y1 <= y0) {
        *x = x0;
        *y = y0;
        *w = 0;
        *h = 0;
        return 0;
    }

    *x = x0;
    *y = y0;
    *w = x1 - x0;
    *h = y1 - y0;
    return 1;
}

static inline int8_t rt_canvas_clip_intersect_logical(
    rt_canvas *canvas, int64_t *x, int64_t *y, int64_t *w, int64_t *h) {
    if (!x || !y || !w || !h || *w <= 0 || *h <= 0)
        return 0;

    int64_t clip_x = 0;
    int64_t clip_y = 0;
    int64_t clip_w = 0;
    int64_t clip_h = 0;
    if (!rt_canvas_get_logical_clip_bounds(canvas, &clip_x, &clip_y, &clip_w, &clip_h)) {
        *w = 0;
        *h = 0;
        return 0;
    }

    int64_t x0 = rtg_max64(*x, clip_x);
    int64_t y0 = rtg_max64(*y, clip_y);
    int64_t x1 = rtg_min64(*x + *w, clip_x + clip_w);
    int64_t y1 = rtg_min64(*y + *h, clip_y + clip_h);
    if (x1 <= x0 || y1 <= y0) {
        *x = x0;
        *y = y0;
        *w = 0;
        *h = 0;
        return 0;
    }

    *x = x0;
    *y = y0;
    *w = x1 - x0;
    *h = y1 - y0;
    return 1;
}

#endif /* VIPER_ENABLE_GRAPHICS */
