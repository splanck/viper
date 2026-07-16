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

/// @brief Clamp a widget dimension to the supported non-negative range.
int64_t ui_clamp_dim(int64_t value);

/// @brief Saturating 64-bit addition (no UB on overflow).
int64_t ui_add_sat_i64(int64_t a, int64_t b);

/// @brief Saturating 64-bit multiplication (no UB on overflow).
int64_t ui_mul_sat_i64(int64_t a, int64_t b);

/// @brief Convert a long double to i64, saturating at the type bounds.
int64_t ui_ld_to_i64_sat(long double value);

/// @brief True when @p point lies within [start, start+extent).
int8_t ui_coord_inside(int64_t start, int64_t extent, int64_t point);

/// @brief Offset of @p point from @p start, clamped into [0, extent).
int64_t ui_coord_offset_clamped(int64_t start, int64_t extent, int64_t point);

/// @brief Validate a 2D canvas handle; traps with @p api context on failure.
int8_t ui_validate_canvas(void *canvas, const char *api);
/// @brief Resolve a Draw-call canvas (2D Canvas or Canvas3D) into a draw-ops table;
///        traps @p api on unknown handles. @return 1 on success.
int8_t ui_resolve_draw_ops(void *canvas, const char *api, rt_gameui_draw_ops_t *ops);
/// @brief Copy @p text into a fixed buffer with truncation and NUL termination.
void ui_copy_text(char *dst, size_t cap, rt_string text);

/// @brief Release one reference on a runtime object (NULL-safe).
void ui_release_obj(void *obj);

/// @brief Pixel width of the first @p bytes of @p text in @p font at @p scale.
int64_t ui_text_prefix_width(const char *text, int64_t bytes, void *font, int64_t scale);

/// @brief Draw text through the resolved draw-ops table (default or bitmap font).
void ui_draw_text_basic(const rt_gameui_draw_ops_t *ops,
                        int64_t x,
                        int64_t y,
                        const char *text,
                        void *font,
                        int64_t scale,
                        int64_t color);
/// @brief True when (@p px, @p py) lies inside the rectangle at (x, y, w, h).
int8_t ui_point_inside(int64_t x, int64_t y, int64_t w, int64_t h, int64_t px, int64_t py);

// Text/UTF-8 helpers shared with UITextInput (defined in rt_gameui.c).

/// @brief Validate a BitmapFont handle; traps with @p api context on failure.
int8_t ui_validate_bitmapfont(void *font, const char *api);

/// @brief Length of @p s up to the first NUL, capped at @p max_len bytes.
size_t ui_visible_len(const char *s, size_t max_len);

/// @brief Byte length of the UTF-8 code point starting at @p pos (1 for invalid leads).
size_t ui_utf8_cp_len(const char *s, size_t len, size_t pos);

/// @brief Longest prefix of @p s within @p max_bytes that ends on a code-point boundary.
size_t ui_utf8_trunc_len(const char *s, size_t len, size_t max_bytes);

/// @brief Byte length of the first @p max_codepoints code points of @p s.
size_t ui_utf8_trunc_codepoints(const char *s, size_t len, size_t max_codepoints);

/// @brief Retain @p value, release the previous occupant, and store it in @p slot.
void ui_replace_ref(void **slot, void *value);

/// @brief Number of code points in the first @p bytes of @p text.
int64_t ui_codepoint_count_bytes(const char *text, int64_t bytes);

/// @brief Byte offset where the @p cp_index-th code point starts.
int64_t ui_byte_for_codepoint(const char *text, int64_t bytes, int64_t cp_index);

/// @brief Code-point index containing the byte at @p byte_index.
int64_t ui_codepoint_for_byte(const char *text, int64_t bytes, int64_t byte_index);

/// @brief Byte offset of the code point preceding @p byte_index (0 at start).
int64_t ui_prev_codepoint_byte(const char *text, int64_t bytes, int64_t byte_index);

/// @brief Byte offset of the code point following @p byte_index (len at end).
int64_t ui_next_codepoint_byte(const char *text, int64_t bytes, int64_t byte_index);
