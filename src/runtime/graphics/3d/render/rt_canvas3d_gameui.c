//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/render/rt_canvas3d_gameui.c
// Purpose: Canvas3D binding for the canvas-polymorphic Game.UI widget draw-ops
//   table (ADR 0065): maps each widget primitive onto the Canvas3D screen-space
//   overlay queue so the same widget objects render over 3D scenes.
// Key invariants:
//   - Registered via rt_gameui_register_canvas3d_ops from Canvas3D creation so
//     runtime/game never includes graphics/3d headers (layering).
//   - Text ops draw the built-in Canvas3D 5x7 font advance-matched to the 2D
//     8px-per-character metrics (scale 8/12) so widget layout computed from
//     rt_canvas_text_width stays visually correct; custom Font objects fall
//     back to the built-in font on Canvas3D (v1 limitation, documented).
//   - box_alpha's 0..255 alpha converts to the overlay queue's 0..1 range.
// Ownership/Lifetime:
//   - Stateless: the ops table borrows the canvas handle per Draw call.
// Links: src/runtime/game/rt_gameui_draw.h,
//   misc/plans/3d_overhaul/08-ui-toolkit-3d.md
//
//===----------------------------------------------------------------------===//
#ifdef ZANNA_ENABLE_GRAPHICS

#include "rt_canvas3d.h"
#include "rt_canvas3d_internal.h"
#include "rt_gameui_draw.h"

/* Advance-match the Canvas3D built-in font (12px/char at scale 1) to the 2D
 * canvas metrics (8px/char) that widget layout is computed against. */
#define CANVAS3D_GAMEUI_TEXT_SCALE (8.0 / 12.0)

/// @brief Probe: true when @p canvas is a Canvas3D handle.
static int8_t canvas3d_gameui_probe(void *canvas) {
    return rt_canvas3d_checked_or_stack(canvas) != NULL ? 1 : 0;
}

static void canvas3d_gameui_box(
    void *canvas, int64_t x, int64_t y, int64_t w, int64_t h, int64_t color) {
    rt_canvas3d_draw_rect2d(canvas, x, y, w, h, color);
}

static void canvas3d_gameui_box_alpha(
    void *canvas, int64_t x, int64_t y, int64_t w, int64_t h, int64_t color, int64_t alpha) {
    if (alpha < 0)
        alpha = 0;
    if (alpha > 255)
        alpha = 255;
    rt_canvas3d_draw_rect2d_alpha(canvas, x, y, w, h, color, (double)alpha / 255.0);
}

static void canvas3d_gameui_frame(
    void *canvas, int64_t x, int64_t y, int64_t w, int64_t h, int64_t color) {
    rt_canvas3d_draw_frame2d(canvas, x, y, w, h, color, 1.0);
}

static void canvas3d_gameui_line(
    void *canvas, int64_t x1, int64_t y1, int64_t x2, int64_t y2, int64_t color) {
    rt_canvas3d_draw_line2d(canvas, x1, y1, x2, y2, color, 1.0);
}

static void canvas3d_gameui_round_box(
    void *canvas, int64_t x, int64_t y, int64_t w, int64_t h, int64_t radius, int64_t color) {
    rt_canvas3d_draw_round_rect2d(canvas, x, y, w, h, radius, color, 1.0);
}

static void canvas3d_gameui_round_frame(
    void *canvas, int64_t x, int64_t y, int64_t w, int64_t h, int64_t radius, int64_t color) {
    rt_canvas3d_draw_round_frame2d(canvas, x, y, w, h, radius, color, 1.0);
}

static void canvas3d_gameui_text(
    void *canvas, int64_t x, int64_t y, rt_string text, int64_t color) {
    rt_canvas3d_draw_text2d_scaled(canvas, x, y, text, color, CANVAS3D_GAMEUI_TEXT_SCALE);
}

static void canvas3d_gameui_text_scaled(
    void *canvas, int64_t x, int64_t y, rt_string text, int64_t scale, int64_t color) {
    if (scale < 1)
        scale = 1;
    rt_canvas3d_draw_text2d_scaled(
        canvas, x, y, text, color, (double)scale * CANVAS3D_GAMEUI_TEXT_SCALE);
}

static void canvas3d_gameui_text_font(
    void *canvas, int64_t x, int64_t y, rt_string text, void *font, int64_t color) {
    (void)font; /* v1: custom Font objects render with the built-in font on Canvas3D */
    canvas3d_gameui_text(canvas, x, y, text, color);
}

static void canvas3d_gameui_text_font_scaled(
    void *canvas, int64_t x, int64_t y, rt_string text, void *font, int64_t scale, int64_t color) {
    (void)font; /* v1: custom Font objects render with the built-in font on Canvas3D */
    canvas3d_gameui_text_scaled(canvas, x, y, text, scale, color);
}

static void canvas3d_gameui_blit_region(void *canvas,
                                        int64_t dx,
                                        int64_t dy,
                                        void *pixels,
                                        int64_t sx,
                                        int64_t sy,
                                        int64_t w,
                                        int64_t h) {
    rt_canvas3d_draw_image2d_region(canvas, dx, dy, w, h, pixels, sx, sy, w, h);
}

/// @brief Fill the widget draw-ops table for a Canvas3D handle.
static void canvas3d_gameui_fill(void *canvas, rt_gameui_draw_ops_t *ops) {
    if (!ops)
        return;
    ops->canvas = canvas;
    ops->box = canvas3d_gameui_box;
    ops->box_alpha = canvas3d_gameui_box_alpha;
    ops->frame = canvas3d_gameui_frame;
    ops->line = canvas3d_gameui_line;
    ops->round_box = canvas3d_gameui_round_box;
    ops->round_frame = canvas3d_gameui_round_frame;
    ops->text = canvas3d_gameui_text;
    ops->text_scaled = canvas3d_gameui_text_scaled;
    ops->text_font = canvas3d_gameui_text_font;
    ops->text_font_scaled = canvas3d_gameui_text_font_scaled;
    ops->blit_region = canvas3d_gameui_blit_region;
    ops->width = rt_canvas3d_get_width;
    ops->height = rt_canvas3d_get_height;
}

/// @brief Register the Canvas3D widget draw-ops binding (idempotent; called at
///        Canvas3D creation).
void canvas3d_register_gameui_ops(void) {
    rt_gameui_register_canvas3d_ops(canvas3d_gameui_probe, canvas3d_gameui_fill);
}

#else
typedef int rt_graphics_disabled_tu_guard_canvas3d_gameui;
#endif /* ZANNA_ENABLE_GRAPHICS */
