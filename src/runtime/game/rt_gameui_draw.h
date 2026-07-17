//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/game/rt_gameui_draw.h
// Purpose: Canvas-polymorphic draw-operation table for the Game.UI widgets so
//   one widget implementation renders on both the 2D Canvas and Canvas3D
//   (ADR 0065). Widgets call through this table instead of rt_canvas_*.
// Key invariants:
//   - The 2D binding is behavior-identical to the direct rt_canvas_* calls it
//     replaced (function pointers straight to the 2D primitives).
//   - The 3D binding registers itself at Canvas3D creation via
//     rt_gameui_register_canvas3d_ops, keeping runtime/game free of
//     graphics/3d includes (layering: game -> graphics/3d is forbidden).
//   - ops.canvas is the handle every op receives as its first argument.
// Ownership/Lifetime:
//   - The ops table is a stack value filled per Draw call; it owns nothing.
//   - Registered 3D hooks are process-global function pointers set once.
// Links: misc/plans/3d_overhaul/08-ui-toolkit-3d.md, rt_gameui.h
//
//===----------------------------------------------------------------------===//
#pragma once

#include "rt_string.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Canvas-polymorphic draw operations consumed by the Game.UI widgets.
/// @invariant Every function pointer is non-NULL after a successful resolve.
/// @ownership Stack value; borrows the canvas handle for the duration of a Draw.
typedef struct rt_gameui_draw_ops {
    void *canvas;
    void (*box)(void *canvas, int64_t x, int64_t y, int64_t w, int64_t h, int64_t color);
    void (*box_alpha)(
        void *canvas, int64_t x, int64_t y, int64_t w, int64_t h, int64_t color, int64_t alpha);
    void (*frame)(void *canvas, int64_t x, int64_t y, int64_t w, int64_t h, int64_t color);
    void (*line)(void *canvas, int64_t x1, int64_t y1, int64_t x2, int64_t y2, int64_t color);
    void (*round_box)(
        void *canvas, int64_t x, int64_t y, int64_t w, int64_t h, int64_t radius, int64_t color);
    void (*round_frame)(
        void *canvas, int64_t x, int64_t y, int64_t w, int64_t h, int64_t radius, int64_t color);
    void (*text)(void *canvas, int64_t x, int64_t y, rt_string text, int64_t color);
    void (*text_scaled)(
        void *canvas, int64_t x, int64_t y, rt_string text, int64_t scale, int64_t color);
    void (*text_font)(
        void *canvas, int64_t x, int64_t y, rt_string text, void *font, int64_t color);
    void (*text_font_scaled)(void *canvas,
                             int64_t x,
                             int64_t y,
                             rt_string text,
                             void *font,
                             int64_t scale,
                             int64_t color);
    void (*blit_region)(void *canvas,
                        int64_t dx,
                        int64_t dy,
                        void *pixels,
                        int64_t sx,
                        int64_t sy,
                        int64_t w,
                        int64_t h);
    int64_t (*width)(void *canvas);
    int64_t (*height)(void *canvas);
} rt_gameui_draw_ops_t;

/// @brief Fill @p ops for @p canvas (2D Canvas or a registered Canvas3D).
/// @return 1 when the handle resolved to a known canvas kind, 0 otherwise.
int rt_gameui_resolve_draw_ops(void *canvas, rt_gameui_draw_ops_t *ops);

/// @brief Register the Canvas3D probe + ops-fill hooks (called by Canvas3D at init).
/// @details Keeps runtime/game free of graphics/3d includes: the 3D layer pushes its
///   binding down rather than the widgets reaching up. Idempotent; later calls replace
///   the hooks.
void rt_gameui_register_canvas3d_ops(int8_t (*probe)(void *canvas),
                                     void (*fill)(void *canvas, rt_gameui_draw_ops_t *ops));

#ifdef __cplusplus
}
#endif
