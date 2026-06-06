//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/game/rt_gameui_internal.h
// Purpose: Shared helpers and key constants for the immediate-mode GameUI
//          widgets, split across rt_gameui.c (core widgets) and
//          rt_gameui_widgets.c (table/slider/dropdown/tooltip/modal).
//
// Key invariants:
//   - Engine-internal; included only by the game/ GameUI translation units.
//   - Geometry/text/clamp helpers are pure and saturate rather than overflow.
//   - UI_KEY_* values mirror the canvas key codes the poll path receives.
//
// Ownership/Lifetime:
//   - Helpers borrow caller buffers/handles; ui_release_obj drops a GC ref.
//
// Links: src/runtime/game/rt_gameui.c, src/runtime/game/rt_gameui_widgets.c
//
//===----------------------------------------------------------------------===//
#pragma once

#include "rt_string.h"

#include <stddef.h>
#include <stdint.h>

// Key codes delivered by the canvas poll path.
#define UI_KEY_ESCAPE 256
#define UI_KEY_ENTER 257
#define UI_KEY_TAB 258
#define UI_KEY_BACKSPACE 259
#define UI_KEY_DELETE 261
#define UI_KEY_RIGHT 262
#define UI_KEY_LEFT 263
#define UI_KEY_DOWN 264
#define UI_KEY_UP 265
#define UI_KEY_PAGE_UP 266
#define UI_KEY_PAGE_DOWN 267
#define UI_KEY_HOME 268
#define UI_KEY_END 269

// Shared geometry / text / lifetime helpers (defined in rt_gameui.c).
int64_t ui_clamp_dim(int64_t value);
int64_t ui_add_sat_i64(int64_t a, int64_t b);
int64_t ui_ld_to_i64_sat(long double value);
int8_t ui_coord_inside(int64_t start, int64_t extent, int64_t point);
int64_t ui_coord_offset_clamped(int64_t start, int64_t extent, int64_t point);
int8_t ui_validate_canvas(void *canvas, const char *api);
void ui_copy_text(char *dst, size_t cap, rt_string text);
void ui_release_obj(void *obj);
int64_t ui_text_prefix_width(const char *text, int64_t bytes, void *font, int64_t scale);
void ui_draw_text_basic(
    void *canvas, int64_t x, int64_t y, const char *text, void *font, int64_t scale, int64_t color);
int8_t ui_point_inside(int64_t x, int64_t y, int64_t w, int64_t h, int64_t px, int64_t py);
