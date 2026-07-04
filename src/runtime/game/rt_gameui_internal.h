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

#include "rt_gameui_draw.h"
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
int64_t ui_mul_sat_i64(int64_t a, int64_t b);
int64_t ui_ld_to_i64_sat(long double value);
int8_t ui_coord_inside(int64_t start, int64_t extent, int64_t point);
int64_t ui_coord_offset_clamped(int64_t start, int64_t extent, int64_t point);
int8_t ui_validate_canvas(void *canvas, const char *api);
/// @brief Resolve a Draw-call canvas (2D Canvas or Canvas3D) into a draw-ops table;
///        traps @p api on unknown handles. @return 1 on success.
int8_t ui_resolve_draw_ops(void *canvas, const char *api, rt_gameui_draw_ops_t *ops);
void ui_copy_text(char *dst, size_t cap, rt_string text);
void ui_release_obj(void *obj);
int64_t ui_text_prefix_width(const char *text, int64_t bytes, void *font, int64_t scale);
void ui_draw_text_basic(const rt_gameui_draw_ops_t *ops,
                        int64_t x,
                        int64_t y,
                        const char *text,
                        void *font,
                        int64_t scale,
                        int64_t color);
int8_t ui_point_inside(int64_t x, int64_t y, int64_t w, int64_t h, int64_t px, int64_t py);

// Text/UTF-8 helpers shared with UITextInput (defined in rt_gameui.c).
int8_t ui_validate_bitmapfont(void *font, const char *api);
size_t ui_visible_len(const char *s, size_t max_len);
size_t ui_utf8_cp_len(const char *s, size_t len, size_t pos);
size_t ui_utf8_trunc_len(const char *s, size_t len, size_t max_bytes);
size_t ui_utf8_trunc_codepoints(const char *s, size_t len, size_t max_codepoints);
void ui_replace_ref(void **slot, void *value);
int64_t ui_codepoint_count_bytes(const char *text, int64_t bytes);
int64_t ui_byte_for_codepoint(const char *text, int64_t bytes, int64_t cp_index);
int64_t ui_codepoint_for_byte(const char *text, int64_t bytes, int64_t byte_index);
int64_t ui_prev_codepoint_byte(const char *text, int64_t bytes, int64_t byte_index);
int64_t ui_next_codepoint_byte(const char *text, int64_t bytes, int64_t byte_index);
