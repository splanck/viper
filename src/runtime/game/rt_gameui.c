//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/game/rt_gameui.c
// Purpose: Lightweight in-game UI widgets that draw directly to a Canvas.
//   Provides Label, Bar, Panel, NineSlice, MenuList, and GameButton — the
//   building blocks for game HUDs, menus, and overlays without the overhead of
//   the desktop ViperGUI widget system.
//
// Key invariants:
//   - All widgets are GC-managed via rt_obj_new_i64 (no manual free).
//   - Draw functions are no-ops when canvas or widget is NULL.
//   - UIBar values are clamped: value in [0, max], max >= 1.
//   - UIMenuList selection wraps: MoveUp at 0 → last item, MoveDown at last → 0.
//   - UILabel falls back to built-in 8x8 font when font is NULL.
//   - Fixed-size text buffers are truncated on UTF-8 codepoint boundaries.
//
// Ownership/Lifetime:
//   - Widgets are GC-managed.
//   - UILabel and GameButton copy text into widget-owned buffers.
//   - UIMenuList stores item text in fixed widget-owned buffers.
//   - UILabel/UIMenuList retain assigned BitmapFont handles.
//   - UINineSlice retains its Pixels source.
//
// Links: rt_gameui.h (public API), rt_graphics.h (Canvas drawing),
//        rt_bitmapfont.h (custom font support), rt_pixels.h (NineSlice source)
//
//===----------------------------------------------------------------------===//

#include "rt_gameui.h"
#include "rt_bitmapfont.h"
#include "rt_graphics.h"
#include "rt_internal.h"
#include "rt_object.h"
#include "rt_pixels.h"

#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define UI_MAX_DIM INT64_C(16384)

static int64_t ui_clamp_dim(int64_t value) {
    if (value <= 0)
        return 1;
    return value > UI_MAX_DIM ? UI_MAX_DIM : value;
}

static int64_t ui_clamp_scale(int64_t scale) {
    if (scale < 1)
        return 1;
    if (scale > 16)
        return 16;
    return scale;
}

static int64_t ui_add_sat_i64(int64_t a, int64_t b) {
    if (b > 0 && a > INT64_MAX - b)
        return INT64_MAX;
    if (b < 0 && a < INT64_MIN - b)
        return INT64_MIN;
    return a + b;
}

static int64_t ui_ld_to_i64_sat(long double value) {
    if (!isfinite(value))
        return 0;
#if LDBL_MANT_DIG < 64
    if (value >= (long double)(INT64_MAX - INT64_C(4096)))
        return INT64_MAX;
    if (value <= (long double)(INT64_MIN + INT64_C(4096)))
        return INT64_MIN;
#else
    if (value > (long double)INT64_MAX)
        return INT64_MAX;
    if (value < (long double)INT64_MIN)
        return INT64_MIN;
#endif
    return (int64_t)value;
}

static int8_t ui_coord_inside(int64_t start, int64_t extent, int64_t point) {
    if (extent <= 0 || point < start)
        return 0;
    uint64_t offset = (uint64_t)point - (uint64_t)start;
    return offset < (uint64_t)extent;
}

static int64_t ui_coord_offset_clamped(int64_t start, int64_t extent, int64_t point) {
    if (extent <= 0 || point <= start)
        return 0;
    uint64_t offset = (uint64_t)point - (uint64_t)start;
    if (offset >= (uint64_t)extent)
        return extent;
    return (int64_t)offset;
}

static int8_t ui_is_bitmapfont(void *obj) {
    return obj && rt_obj_class_id(obj) == RT_BITMAPFONT_CLASS_ID;
}

static int8_t ui_is_pixels(void *obj) {
    return obj && rt_obj_class_id(obj) == RT_PIXELS_CLASS_ID;
}

static int8_t ui_validate_bitmapfont(void *font, const char *api) {
    if (!font)
        return 1;
    if (!ui_is_bitmapfont(font)) {
        rt_trap(api);
        return 0;
    }
    return 1;
}

static int8_t ui_validate_pixels(void *pixels, const char *api) {
    if (!pixels)
        return 0;
    if (!ui_is_pixels(pixels)) {
        rt_trap(api);
        return 0;
    }
    return 1;
}

static int8_t ui_validate_canvas(void *canvas, const char *api) {
    if (!canvas)
        return 0;
    if (!rt_canvas_is_handle(canvas)) {
        rt_trap(api);
        return 0;
    }
    return 1;
}

static size_t ui_visible_len(const char *s, size_t max_len) {
    size_t len = 0;
    if (!s)
        return 0;
    while (len < max_len && s[len] != '\0')
        len++;
    return len;
}

static int ui_is_continuation(unsigned char c) {
    return (c & 0xC0u) == 0x80u;
}

static size_t ui_utf8_cp_len(const char *s, size_t len, size_t pos) {
    unsigned char c = (unsigned char)s[pos];
    if (c < 0x80u)
        return 1;
    if (c >= 0xC2u && c <= 0xDFu) {
        if (pos + 1 < len && ui_is_continuation((unsigned char)s[pos + 1]))
            return 2;
        return 1;
    }
    if ((c & 0xF0u) == 0xE0u) {
        unsigned char c1 = pos + 1 < len ? (unsigned char)s[pos + 1] : 0;
        unsigned char c2 = pos + 2 < len ? (unsigned char)s[pos + 2] : 0;
        if (pos + 2 < len && ui_is_continuation(c1) && ui_is_continuation(c2) &&
            !(c == 0xE0u && c1 < 0xA0u) && !(c == 0xEDu && c1 >= 0xA0u))
            return 3;
        return 1;
    }
    if ((c & 0xF8u) == 0xF0u) {
        unsigned char c1 = pos + 1 < len ? (unsigned char)s[pos + 1] : 0;
        unsigned char c2 = pos + 2 < len ? (unsigned char)s[pos + 2] : 0;
        unsigned char c3 = pos + 3 < len ? (unsigned char)s[pos + 3] : 0;
        if (c >= 0xF0u && c <= 0xF4u && pos + 3 < len && ui_is_continuation(c1) &&
            ui_is_continuation(c2) && ui_is_continuation(c3) &&
            !(c == 0xF0u && c1 < 0x90u) && !(c == 0xF4u && c1 > 0x8Fu))
            return 4;
        return 1;
    }
    return 1;
}

static size_t ui_utf8_trunc_len(const char *s, size_t len, size_t max_bytes) {
    size_t pos = 0;
    size_t last = 0;
    while (pos < len && pos < max_bytes) {
        size_t cp_len = ui_utf8_cp_len(s, len, pos);
        if (pos + cp_len > len || pos + cp_len > max_bytes)
            break;
        pos += cp_len;
        last = pos;
    }
    return last;
}

static size_t ui_utf8_trunc_codepoints(const char *s, size_t len, size_t max_codepoints) {
    size_t pos = 0;
    size_t count = 0;
    if (!s || max_codepoints == 0)
        return 0;
    while (pos < len && count < max_codepoints) {
        size_t cp_len = ui_utf8_cp_len(s, len, pos);
        if (pos + cp_len > len)
            break;
        pos += cp_len;
        count++;
    }
    return pos;
}

static void ui_copy_text(char *dst, size_t cap, rt_string text) {
    if (!dst || cap == 0)
        return;
    dst[0] = '\0';
    if (!text)
        return;

    const char *s = rt_string_cstr(text);
    if (!s)
        return;
    size_t len = ui_visible_len(s, (size_t)rt_str_len(text));
    size_t copy_len = ui_utf8_trunc_len(s, len, cap - 1);
    memcpy(dst, s, copy_len);
    dst[copy_len] = '\0';
}

static void ui_release_obj(void *obj) {
    if (obj && rt_obj_release_check0(obj))
        rt_obj_free(obj);
}

static void ui_replace_ref(void **slot, void *value) {
    if (!slot || *slot == value)
        return;
    if (value)
        rt_obj_retain_maybe(value);
    ui_release_obj(*slot);
    *slot = value;
}

static void ui_round_box_alpha(void *canvas,
                               int64_t x,
                               int64_t y,
                               int64_t w,
                               int64_t h,
                               int64_t radius,
                               int64_t color,
                               int64_t alpha) {
    if (!canvas || w <= 0 || h <= 0 || alpha <= 0)
        return;
    if (alpha >= 255) {
        rt_canvas_round_box(canvas, x, y, w, h, radius, color);
        return;
    }

    int64_t max_radius = (w < h ? w : h) / 2;
    if (radius < 0)
        radius = 0;
    if (radius > max_radius)
        radius = max_radius;
    if (radius <= 0) {
        rt_canvas_box_alpha(canvas, x, y, w, h, color, alpha);
        return;
    }

    long double r = (long double)radius;
    long double rr = r * r;
    for (int64_t py = 0; py < h; py++) {
        int64_t inset = 0;
        if (py < radius || py >= h - radius) {
            long double dy = py < radius ? (r - 1.0L - (long double)py)
                                         : ((long double)py - (long double)(h - radius));
            long double inside = rr - dy * dy;
            if (inside < 0.0L)
                inside = 0.0L;
            long double extent = sqrtl(inside);
            long double raw_inset = r - extent - 1.0L;
            if (raw_inset > 0.0L) {
                if (raw_inset > (long double)radius)
                    inset = radius;
                else
                    inset = (int64_t)raw_inset;
            }
        }
        int64_t row_w = w - inset * 2;
        if (row_w > 0)
            rt_canvas_box_alpha(canvas, x + inset, y + py, row_w, 1, color, alpha);
    }
}

//=============================================================================
// UILabel
//=============================================================================

/// Maximum label text length (bytes).
#define LABEL_MAX_TEXT 512

typedef struct {
    void *vptr;
    int64_t x, y;
    char text[LABEL_MAX_TEXT];
    int64_t color;
    void *font;    ///< BitmapFont or NULL for default
    int64_t scale; ///< Integer scale (1 = normal)
    int8_t visible;
} rt_uilabel_impl;

static rt_uilabel_impl *checked_label(void *ptr, const char *api) {
    if (!ptr)
        return NULL;
    if (rt_obj_class_id(ptr) != RT_UILABEL_CLASS_ID) {
        rt_trap(api);
        return NULL;
    }
    return (rt_uilabel_impl *)ptr;
}

static void uilabel_finalizer(void *obj) {
    rt_uilabel_impl *label = (rt_uilabel_impl *)obj;
    if (!label)
        return;
    ui_release_obj(label->font);
    label->font = NULL;
}

void *rt_uilabel_new(int64_t x, int64_t y, rt_string text, int64_t color) {
    rt_uilabel_impl *label =
        (rt_uilabel_impl *)rt_obj_new_i64(RT_UILABEL_CLASS_ID, (int64_t)sizeof(rt_uilabel_impl));
    if (!label)
        return NULL;

    memset(label, 0, sizeof(rt_uilabel_impl));
    label->x = x;
    label->y = y;
    label->color = color;
    label->scale = 1;
    label->visible = 1;
    ui_copy_text(label->text, sizeof(label->text), text);
    rt_obj_set_finalizer(label, uilabel_finalizer);

    return label;
}

/// @brief Replace the label's displayed text, truncating to 511 bytes if needed.
void rt_uilabel_set_text(void *ptr, rt_string text) {
    rt_uilabel_impl *label = checked_label(ptr, "UILabel.SetText: expected Viper.Game.UI.Label");
    if (!label)
        return;
    ui_copy_text(label->text, sizeof(label->text), text);
}

/// @brief Reposition the label to screen coordinates (x, y).
void rt_uilabel_set_pos(void *ptr, int64_t x, int64_t y) {
    rt_uilabel_impl *label = checked_label(ptr, "UILabel.SetPos: expected Viper.Game.UI.Label");
    if (!label)
        return;
    label->x = x;
    label->y = y;
}

/// @brief Set the label's text color (RGBA packed integer).
void rt_uilabel_set_color(void *ptr, int64_t color) {
    rt_uilabel_impl *label = checked_label(ptr, "UILabel.SetColor: expected Viper.Game.UI.Label");
    if (label)
        label->color = color;
}

/// @brief Assign a BitmapFont for rendering; NULL uses the built-in 8x8 font.
void rt_uilabel_set_font(void *ptr, void *font) {
    rt_uilabel_impl *label = checked_label(ptr, "UILabel.SetFont: expected Viper.Game.UI.Label");
    if (!label)
        return;
    if (!ui_validate_bitmapfont(font, "UILabel.SetFont: expected BitmapFont"))
        return;
    ui_replace_ref(&label->font, font);
}

/// @brief Set the integer pixel scale for text rendering (minimum 1).
void rt_uilabel_set_scale(void *ptr, int64_t scale) {
    rt_uilabel_impl *label = checked_label(ptr, "UILabel.SetScale: expected Viper.Game.UI.Label");
    if (label)
        label->scale = ui_clamp_scale(scale);
}

/// @brief Show or hide the label; hidden labels are skipped during draw.
void rt_uilabel_set_visible(void *ptr, int8_t visible) {
    rt_uilabel_impl *label =
        checked_label(ptr, "UILabel.SetVisible: expected Viper.Game.UI.Label");
    if (label)
        label->visible = visible ? 1 : 0;
}

/// @brief Return the label's current X position in screen coordinates.
int64_t rt_uilabel_get_x(void *ptr) {
    rt_uilabel_impl *label = checked_label(ptr, "UILabel.X: expected Viper.Game.UI.Label");
    return label ? label->x : 0;
}

/// @brief Return the label's current Y position in screen coordinates.
int64_t rt_uilabel_get_y(void *ptr) {
    rt_uilabel_impl *label = checked_label(ptr, "UILabel.Y: expected Viper.Game.UI.Label");
    return label ? label->y : 0;
}

/// @brief Render the label onto the canvas using its font, scale, and color.
/// @details Skips rendering if the label is hidden or has empty text. Uses
///          the assigned BitmapFont when set, otherwise falls back to the
///          built-in 8x8 pixel font.
void rt_uilabel_draw(void *ptr, void *canvas) {
    if (!ptr || !canvas)
        return;
    rt_uilabel_impl *label = checked_label(ptr, "UILabel.Draw: expected Viper.Game.UI.Label");
    if (!label || !ui_validate_canvas(canvas, "UILabel.Draw: expected Canvas"))
        return;
    if (!label->visible || label->text[0] == '\0')
        return;

    rt_string rtext = rt_const_cstr(label->text);

    if (label->font) {
        if (label->scale > 1)
            rt_canvas_text_font_scaled(
                canvas, label->x, label->y, rtext, label->font, label->scale, label->color);
        else
            rt_canvas_text_font(canvas, label->x, label->y, rtext, label->font, label->color);
    } else {
        if (label->scale > 1)
            rt_canvas_text_scaled(canvas, label->x, label->y, rtext, label->scale, label->color);
        else
            rt_canvas_text(canvas, label->x, label->y, rtext, label->color);
    }
}

//=============================================================================
// UIBar
//=============================================================================

/// Bar fill direction constants.
#define BAR_DIR_LEFT_TO_RIGHT 0
#define BAR_DIR_RIGHT_TO_LEFT 1
#define BAR_DIR_BOTTOM_TO_TOP 2
#define BAR_DIR_TOP_TO_BOTTOM 3

typedef struct {
    void *vptr;
    int64_t x, y, w, h;
    int64_t value, max_value;
    int64_t fg_color, bg_color;
    int64_t border_color; ///< 0 = no border
    int64_t direction;
    int8_t visible;
} rt_uibar_impl;

static rt_uibar_impl *checked_bar(void *ptr, const char *api) {
    if (!ptr)
        return NULL;
    if (rt_obj_class_id(ptr) != RT_UIBAR_CLASS_ID) {
        rt_trap(api);
        return NULL;
    }
    return (rt_uibar_impl *)ptr;
}

void *rt_uibar_new(int64_t x, int64_t y, int64_t w, int64_t h, int64_t fg_color, int64_t bg_color) {
    rt_uibar_impl *bar =
        (rt_uibar_impl *)rt_obj_new_i64(RT_UIBAR_CLASS_ID, (int64_t)sizeof(rt_uibar_impl));
    if (!bar)
        return NULL;

    bar->vptr = NULL;
    bar->x = x;
    bar->y = y;
    bar->w = ui_clamp_dim(w);
    bar->h = ui_clamp_dim(h);
    bar->value = 0;
    bar->max_value = 100;
    bar->fg_color = fg_color;
    bar->bg_color = bg_color;
    bar->border_color = 0;
    bar->direction = BAR_DIR_LEFT_TO_RIGHT;
    bar->visible = 1;

    return bar;
}

/// @brief Set the bar's current and maximum values, clamping to [0, max].
/// @details max_value is forced to at least 1 to prevent division by zero
///          during fill-ratio computation in the draw function.
void rt_uibar_set_value(void *ptr, int64_t value, int64_t max_value) {
    rt_uibar_impl *bar = checked_bar(ptr, "UIBar.SetValue: expected Viper.Game.UI.Bar");
    if (!bar)
        return;
    if (max_value < 1)
        max_value = 1;
    if (value < 0)
        value = 0;
    if (value > max_value)
        value = max_value;
    bar->value = value;
    bar->max_value = max_value;
}

/// @brief Reposition the bar to screen coordinates (x, y).
void rt_uibar_set_pos(void *ptr, int64_t x, int64_t y) {
    rt_uibar_impl *bar = checked_bar(ptr, "UIBar.SetPos: expected Viper.Game.UI.Bar");
    if (!bar)
        return;
    bar->x = x;
    bar->y = y;
}

/// @brief Set the bar's width and height in pixels (minimum 1 each).
void rt_uibar_set_size(void *ptr, int64_t w, int64_t h) {
    rt_uibar_impl *bar = checked_bar(ptr, "UIBar.SetSize: expected Viper.Game.UI.Bar");
    if (!bar)
        return;
    bar->w = ui_clamp_dim(w);
    bar->h = ui_clamp_dim(h);
}

/// @brief Set the foreground (filled portion) and background colors of the bar.
void rt_uibar_set_colors(void *ptr, int64_t fg, int64_t bg) {
    rt_uibar_impl *bar = checked_bar(ptr, "UIBar.SetColors: expected Viper.Game.UI.Bar");
    if (!bar)
        return;
    bar->fg_color = fg;
    bar->bg_color = bg;
}

/// @brief Set the bar's border color; 0 disables the border outline.
void rt_uibar_set_border(void *ptr, int64_t color) {
    rt_uibar_impl *bar = checked_bar(ptr, "UIBar.SetBorder: expected Viper.Game.UI.Bar");
    if (bar)
        bar->border_color = color;
}

/// @brief Set the bar's fill direction (0=L→R, 1=R→L, 2=B→T, 3=T→B).
void rt_uibar_set_direction(void *ptr, int64_t dir) {
    rt_uibar_impl *bar = checked_bar(ptr, "UIBar.SetDirection: expected Viper.Game.UI.Bar");
    if (!bar)
        return;
    if (dir < 0 || dir > 3)
        dir = BAR_DIR_LEFT_TO_RIGHT;
    bar->direction = dir;
}

/// @brief Show or hide the bar; hidden bars are skipped during draw.
void rt_uibar_set_visible(void *ptr, int8_t visible) {
    rt_uibar_impl *bar = checked_bar(ptr, "UIBar.SetVisible: expected Viper.Game.UI.Bar");
    if (bar)
        bar->visible = visible ? 1 : 0;
}

/// @brief Return the bar's current fill value.
int64_t rt_uibar_get_value(void *ptr) {
    rt_uibar_impl *bar = checked_bar(ptr, "UIBar.Value: expected Viper.Game.UI.Bar");
    return bar ? bar->value : 0;
}

/// @brief Return the bar's maximum value (the denominator for fill ratio).
int64_t rt_uibar_get_max(void *ptr) {
    rt_uibar_impl *bar = checked_bar(ptr, "UIBar.Max: expected Viper.Game.UI.Bar");
    return bar ? bar->max_value : 0;
}

static int64_t ui_scaled_fill(int64_t value, int64_t extent, int64_t max_value) {
    if (value <= 0 || extent <= 0 || max_value <= 0)
        return 0;
    long double fill = ((long double)value * (long double)extent) / (long double)max_value;
    if (fill <= 0.0L)
        return 0;
    if (fill >= (long double)extent)
        return extent;
    int64_t pixels = (int64_t)ceill(fill);
    if (pixels < 1)
        pixels = 1;
    return pixels > extent ? extent : pixels;
}

/// @brief Draw the uibar.
void rt_uibar_draw(void *ptr, void *canvas) {
    if (!ptr || !canvas)
        return;
    rt_uibar_impl *bar = checked_bar(ptr, "UIBar.Draw: expected Viper.Game.UI.Bar");
    if (!bar || !ui_validate_canvas(canvas, "UIBar.Draw: expected Canvas"))
        return;
    if (!bar->visible)
        return;

    // Background
    rt_canvas_box(canvas, bar->x, bar->y, bar->w, bar->h, bar->bg_color);

    // Filled portion
    if (bar->value > 0 && bar->max_value > 0) {
        int64_t fill;
        switch (bar->direction) {
            case BAR_DIR_RIGHT_TO_LEFT:
                fill = ui_scaled_fill(bar->value, bar->w, bar->max_value);
                rt_canvas_box(canvas, bar->x + bar->w - fill, bar->y, fill, bar->h, bar->fg_color);
                break;
            case BAR_DIR_TOP_TO_BOTTOM:
                fill = ui_scaled_fill(bar->value, bar->h, bar->max_value);
                rt_canvas_box(canvas, bar->x, bar->y, bar->w, fill, bar->fg_color);
                break;
            case BAR_DIR_BOTTOM_TO_TOP:
                fill = ui_scaled_fill(bar->value, bar->h, bar->max_value);
                rt_canvas_box(canvas, bar->x, bar->y + bar->h - fill, bar->w, fill, bar->fg_color);
                break;
            default: // LEFT_TO_RIGHT
                fill = ui_scaled_fill(bar->value, bar->w, bar->max_value);
                rt_canvas_box(canvas, bar->x, bar->y, fill, bar->h, bar->fg_color);
                break;
        }
    }

    // Border
    if (bar->border_color != 0)
        rt_canvas_frame(canvas, bar->x, bar->y, bar->w, bar->h, bar->border_color);
}

//=============================================================================
// UIPanel
//=============================================================================

typedef struct {
    void *vptr;
    int64_t x, y, w, h;
    int64_t bg_color;
    int64_t alpha;        ///< 0-255
    int64_t border_color; ///< 0 = no border
    int64_t border_thickness;
    int64_t corner_radius; ///< 0 = sharp corners
    int8_t visible;
} rt_uipanel_impl;

static rt_uipanel_impl *checked_panel(void *ptr, const char *api) {
    if (!ptr)
        return NULL;
    if (rt_obj_class_id(ptr) != RT_UIPANEL_CLASS_ID) {
        rt_trap(api);
        return NULL;
    }
    return (rt_uipanel_impl *)ptr;
}

void *rt_uipanel_new(int64_t x, int64_t y, int64_t w, int64_t h, int64_t bg_color, int64_t alpha) {
    rt_uipanel_impl *panel =
        (rt_uipanel_impl *)rt_obj_new_i64(RT_UIPANEL_CLASS_ID, (int64_t)sizeof(rt_uipanel_impl));
    if (!panel)
        return NULL;

    panel->vptr = NULL;
    panel->x = x;
    panel->y = y;
    panel->w = ui_clamp_dim(w);
    panel->h = ui_clamp_dim(h);
    panel->bg_color = bg_color;
    panel->alpha = alpha < 0 ? 0 : (alpha > 255 ? 255 : alpha);
    panel->border_color = 0;
    panel->border_thickness = 1;
    panel->corner_radius = 0;
    panel->visible = 1;

    return panel;
}

/// @brief Reposition the panel to screen coordinates (x, y).
void rt_uipanel_set_pos(void *ptr, int64_t x, int64_t y) {
    rt_uipanel_impl *panel = checked_panel(ptr, "UIPanel.SetPos: expected Viper.Game.UI.Panel");
    if (!panel)
        return;
    panel->x = x;
    panel->y = y;
}

/// @brief Set the panel's width and height in pixels (minimum 1 each).
void rt_uipanel_set_size(void *ptr, int64_t w, int64_t h) {
    rt_uipanel_impl *panel = checked_panel(ptr, "UIPanel.SetSize: expected Viper.Game.UI.Panel");
    if (!panel)
        return;
    panel->w = ui_clamp_dim(w);
    panel->h = ui_clamp_dim(h);
}

/// @brief Set the panel's background color and alpha (opacity 0-255).
void rt_uipanel_set_color(void *ptr, int64_t bg_color, int64_t alpha) {
    rt_uipanel_impl *panel = checked_panel(ptr, "UIPanel.SetColor: expected Viper.Game.UI.Panel");
    if (!panel)
        return;
    panel->bg_color = bg_color;
    panel->alpha = alpha < 0 ? 0 : (alpha > 255 ? 255 : alpha);
}

/// @brief Set the panel's border color and thickness. Color 0 or thickness <= 0 disables the border.
void rt_uipanel_set_border(void *ptr, int64_t color, int64_t thickness) {
    rt_uipanel_impl *panel = checked_panel(ptr, "UIPanel.SetBorder: expected Viper.Game.UI.Panel");
    if (!panel)
        return;
    if (color == 0 || thickness <= 0) {
        panel->border_color = 0;
        panel->border_thickness = 0;
        return;
    }
    panel->border_color = color;
    panel->border_thickness = thickness > UI_MAX_DIM ? UI_MAX_DIM : thickness;
}

/// @brief Set the corner radius for rounded-rectangle drawing (0 = sharp).
void rt_uipanel_set_corner_radius(void *ptr, int64_t radius) {
    rt_uipanel_impl *panel =
        checked_panel(ptr, "UIPanel.SetCornerRadius: expected Viper.Game.UI.Panel");
    if (panel)
        panel->corner_radius = radius < 0 ? 0 : radius;
}

/// @brief Show or hide the panel; hidden panels are skipped during draw.
void rt_uipanel_set_visible(void *ptr, int8_t visible) {
    rt_uipanel_impl *panel =
        checked_panel(ptr, "UIPanel.SetVisible: expected Viper.Game.UI.Panel");
    if (panel)
        panel->visible = visible ? 1 : 0;
}

/// @brief Render the panel onto the canvas as a filled rectangle.
/// @details Draws a rounded or sharp rectangle depending on corner_radius,
///          then overlays the border if a border color is set.
void rt_uipanel_draw(void *ptr, void *canvas) {
    if (!ptr || !canvas)
        return;
    rt_uipanel_impl *panel = checked_panel(ptr, "UIPanel.Draw: expected Viper.Game.UI.Panel");
    if (!panel || !ui_validate_canvas(canvas, "UIPanel.Draw: expected Canvas"))
        return;
    if (!panel->visible)
        return;

    // Background with alpha
    if (panel->alpha >= 255) {
        if (panel->corner_radius > 0)
            rt_canvas_round_box(canvas,
                                panel->x,
                                panel->y,
                                panel->w,
                                panel->h,
                                panel->corner_radius,
                                panel->bg_color);
        else
            rt_canvas_box(canvas, panel->x, panel->y, panel->w, panel->h, panel->bg_color);
    } else if (panel->alpha > 0) {
        if (panel->corner_radius > 0)
            ui_round_box_alpha(canvas,
                               panel->x,
                               panel->y,
                               panel->w,
                               panel->h,
                               panel->corner_radius,
                               panel->bg_color,
                               panel->alpha);
        else
            rt_canvas_box_alpha(
                canvas, panel->x, panel->y, panel->w, panel->h, panel->bg_color, panel->alpha);
    }

    // Border
    if (panel->border_color != 0 && panel->border_thickness > 0) {
        int64_t max_t = (panel->w < panel->h ? panel->w : panel->h) / 2;
        if (max_t < 1)
            max_t = 1;
        int64_t thickness = panel->border_thickness > max_t ? max_t : panel->border_thickness;
        for (int64_t t = 0; t < thickness; t++) {
            int64_t iw = panel->w - t * 2;
            int64_t ih = panel->h - t * 2;
            if (iw <= 0 || ih <= 0)
                break;
            int64_t radius = panel->corner_radius - t;
            if (radius < 0)
                radius = 0;
            int64_t max_radius = (iw < ih ? iw : ih) / 2;
            if (radius > max_radius)
                radius = max_radius;
            if (radius > 0)
                rt_canvas_round_frame(canvas,
                                      panel->x + t,
                                      panel->y + t,
                                      iw,
                                      ih,
                                      radius,
                                      panel->border_color);
            else
                rt_canvas_frame(canvas,
                                panel->x + t,
                                panel->y + t,
                                iw,
                                ih,
                                panel->border_color);
        }
    }
}

//=============================================================================
// UINineSlice
//=============================================================================

typedef struct {
    void *vptr;
    void *pixels;        ///< Source Pixels handle (retained)
    void *tinted_pixels; ///< Cached tinted source, if tint != 0
    uint64_t tinted_source_generation;
    int64_t tinted_color;
    int64_t left;   ///< Left margin (corner width)
    int64_t top;    ///< Top margin (corner height)
    int64_t right;  ///< Right margin
    int64_t bottom; ///< Bottom margin
    int64_t tint;   ///< Tint color (0 = no tint)
} rt_uinineslice_impl;

static rt_uinineslice_impl *checked_nineslice(void *ptr, const char *api) {
    if (!ptr)
        return NULL;
    if (rt_obj_class_id(ptr) != RT_UININESLICE_CLASS_ID) {
        rt_trap(api);
        return NULL;
    }
    return (rt_uinineslice_impl *)ptr;
}

static void uinineslice_finalizer(void *obj) {
    rt_uinineslice_impl *ns = (rt_uinineslice_impl *)obj;
    if (!ns)
        return;
    ui_release_obj(ns->pixels);
    ui_release_obj(ns->tinted_pixels);
    ns->pixels = NULL;
    ns->tinted_pixels = NULL;
}

void *rt_uinineslice_new(void *pixels, int64_t left, int64_t top, int64_t right, int64_t bottom) {
    if (!ui_validate_pixels(pixels, "UINineSlice: expected Pixels"))
        return NULL;

    rt_uinineslice_impl *ns = (rt_uinineslice_impl *)rt_obj_new_i64(
        RT_UININESLICE_CLASS_ID, (int64_t)sizeof(rt_uinineslice_impl));
    if (!ns)
        return NULL;

    ns->vptr = NULL;
    int64_t pw = rt_pixels_width(pixels);
    int64_t ph = rt_pixels_height(pixels);

    // Clamp margins to source dimensions
    if (left < 0)
        left = 0;
    if (top < 0)
        top = 0;
    if (right < 0)
        right = 0;
    if (bottom < 0)
        bottom = 0;
    if (left > pw || right > pw || left > pw - right) {
        left = pw / 2;
        right = pw - left;
    }
    if (top > ph || bottom > ph || top > ph - bottom) {
        top = ph / 2;
        bottom = ph - top;
    }

    rt_obj_retain_maybe(pixels);
    ns->pixels = pixels;
    ns->tinted_pixels = NULL;
    ns->tinted_source_generation = 0;
    ns->tinted_color = 0;
    ns->left = left;
    ns->top = top;
    ns->right = right;
    ns->bottom = bottom;
    ns->tint = 0;
    rt_obj_set_finalizer(ns, uinineslice_finalizer);

    return ns;
}

/// @brief Set a color tint applied over the nine-slice texture when drawn.
void rt_uinineslice_set_tint(void *ptr, int64_t color) {
    rt_uinineslice_impl *ns =
        checked_nineslice(ptr, "UINineSlice.SetTint: expected Viper.Game.UI.NineSlice");
    if (!ns)
        return;
    if (ns->tint == color)
        return;
    ns->tint = color;
    ui_release_obj(ns->tinted_pixels);
    ns->tinted_pixels = NULL;
    ns->tinted_source_generation = 0;
    ns->tinted_color = 0;
}

static void *uinineslice_draw_source(rt_uinineslice_impl *ns) {
    if (!ns || !ns->pixels || ns->tint == 0)
        return ns ? ns->pixels : NULL;

    uint64_t generation = rt_pixels_generation(ns->pixels);
    if (!ns->tinted_pixels || ns->tinted_source_generation != generation ||
        ns->tinted_color != ns->tint) {
        ui_release_obj(ns->tinted_pixels);
        ns->tinted_pixels = rt_pixels_tint(ns->pixels, ns->tint);
        ns->tinted_source_generation = generation;
        ns->tinted_color = ns->tint;
    }
    return ns->tinted_pixels ? ns->tinted_pixels : ns->pixels;
}

/// @brief Render the nine-slice texture onto the canvas at the given rect.
/// @details Splits the source texture into 9 patches using the configured insets
///          (left/top/right/bottom) and tiles the center/edge patches to fill
///          the destination rectangle while keeping corners at their natural size.
void rt_uinineslice_draw(void *ptr, void *canvas, int64_t x, int64_t y, int64_t w, int64_t h) {
    if (!ptr || !canvas)
        return;
    rt_uinineslice_impl *ns =
        checked_nineslice(ptr, "UINineSlice.Draw: expected Viper.Game.UI.NineSlice");
    if (!ns || !ui_validate_canvas(canvas, "UINineSlice.Draw: expected Canvas"))
        return;
    if (!ns->pixels || w <= 0 || h <= 0)
        return;

    w = ui_clamp_dim(w);
    h = ui_clamp_dim(h);
    void *source_pixels = uinineslice_draw_source(ns);
    if (!source_pixels)
        return;

    int64_t pw = rt_pixels_width(source_pixels);
    int64_t ph = rt_pixels_height(source_pixels);
    int64_t src_l = ns->left, src_t = ns->top, src_r = ns->right, src_b = ns->bottom;
    int64_t L = src_l < w ? src_l : w;
    int64_t R = src_r < w - L ? src_r : w - L;
    int64_t T = src_t < h ? src_t : h;
    int64_t B = src_b < h - T ? src_b : h - T;

    // Source center region
    int64_t src_cx = src_l;
    int64_t src_cy = src_t;
    int64_t src_cw = pw - src_l - src_r;
    int64_t src_ch = ph - src_t - src_b;

    // Destination center region
    int64_t dst_cw = w - L - R;
    int64_t dst_ch = h - T - B;

    // Draw 4 corners (unscaled — blit directly)
    if (T > 0 && L > 0) // top-left
        rt_canvas_blit_region(canvas, x, y, source_pixels, 0, 0, L, T);
    if (T > 0 && R > 0) // top-right
        rt_canvas_blit_region(canvas, x + w - R, y, source_pixels, pw - R, 0, R, T);
    if (B > 0 && L > 0) // bottom-left
        rt_canvas_blit_region(canvas, x, y + h - B, source_pixels, 0, ph - B, L, B);
    if (B > 0 && R > 0) // bottom-right
        rt_canvas_blit_region(canvas, x + w - R, y + h - B, source_pixels, pw - R, ph - B, R, B);

    // Draw 4 edges (stretched by tiling the 1-pixel-wide/tall source strip)
    // Top edge
    if (T > 0 && dst_cw > 0 && src_cw > 0) {
        for (int64_t dx = 0; dx < dst_cw; dx += src_cw) {
            int64_t bw = src_cw;
            if (dx + bw > dst_cw)
                bw = dst_cw - dx;
            rt_canvas_blit_region(canvas, x + L + dx, y, source_pixels, src_cx, 0, bw, T);
        }
    }
    // Bottom edge
    if (B > 0 && dst_cw > 0 && src_cw > 0) {
        for (int64_t dx = 0; dx < dst_cw; dx += src_cw) {
            int64_t bw = src_cw;
            if (dx + bw > dst_cw)
                bw = dst_cw - dx;
            rt_canvas_blit_region(
                canvas, x + L + dx, y + h - B, source_pixels, src_cx, ph - B, bw, B);
        }
    }
    // Left edge
    if (L > 0 && dst_ch > 0 && src_ch > 0) {
        for (int64_t dy = 0; dy < dst_ch; dy += src_ch) {
            int64_t bh = src_ch;
            if (dy + bh > dst_ch)
                bh = dst_ch - dy;
            rt_canvas_blit_region(canvas, x, y + T + dy, source_pixels, 0, src_cy, L, bh);
        }
    }
    // Right edge
    if (R > 0 && dst_ch > 0 && src_ch > 0) {
        for (int64_t dy = 0; dy < dst_ch; dy += src_ch) {
            int64_t bh = src_ch;
            if (dy + bh > dst_ch)
                bh = dst_ch - dy;
            rt_canvas_blit_region(
                canvas, x + w - R, y + T + dy, source_pixels, pw - R, src_cy, R, bh);
        }
    }

    // Center (tiled)
    if (dst_cw > 0 && dst_ch > 0 && src_cw > 0 && src_ch > 0) {
        for (int64_t dy = 0; dy < dst_ch; dy += src_ch) {
            int64_t bh = src_ch;
            if (dy + bh > dst_ch)
                bh = dst_ch - dy;
            for (int64_t dx = 0; dx < dst_cw; dx += src_cw) {
                int64_t bw = src_cw;
                if (dx + bw > dst_cw)
                    bw = dst_cw - dx;
                rt_canvas_blit_region(
                    canvas, x + L + dx, y + T + dy, source_pixels, src_cx, src_cy, bw, bh);
            }
        }
    }
}

//=============================================================================
// UIMenuList
//=============================================================================

/// Maximum length for a single menu item label.
#define MENULIST_MAX_TEXT 128

typedef struct {
    void *vptr;
    int64_t x, y;
    int64_t item_height;
    char items[RT_UIMENULIST_MAX_ITEMS][MENULIST_MAX_TEXT];
    int64_t count;
    int64_t selected;
    int64_t text_color;
    int64_t selected_color;
    int64_t highlight_bg;
    void *font; ///< BitmapFont or NULL
    int8_t visible;
} rt_uimenulist_impl;

static rt_uimenulist_impl *checked_menulist(void *ptr, const char *api) {
    if (!ptr)
        return NULL;
    if (rt_obj_class_id(ptr) != RT_UIMENULIST_CLASS_ID) {
        rt_trap(api);
        return NULL;
    }
    return (rt_uimenulist_impl *)ptr;
}

static void uimenulist_finalizer(void *obj) {
    rt_uimenulist_impl *menu = (rt_uimenulist_impl *)obj;
    if (!menu)
        return;
    ui_release_obj(menu->font);
    menu->font = NULL;
}

void *rt_uimenulist_new(int64_t x, int64_t y, int64_t item_height) {
    rt_uimenulist_impl *menu = (rt_uimenulist_impl *)rt_obj_new_i64(
        RT_UIMENULIST_CLASS_ID, (int64_t)sizeof(rt_uimenulist_impl));
    if (!menu)
        return NULL;

    memset(menu, 0, sizeof(rt_uimenulist_impl));
    menu->x = x;
    menu->y = y;
    menu->item_height = item_height > 0 ? ui_clamp_dim(item_height) : 16;
    menu->selected = 0;
    menu->text_color = 0xFFFFFF;     // White
    menu->selected_color = 0xFFFF00; // Yellow
    menu->highlight_bg = 0x333333;   // Dark gray
    menu->visible = 1;
    rt_obj_set_finalizer(menu, uimenulist_finalizer);

    return menu;
}

/// @brief Append a text item to the menu list (max 64 items, 127 bytes each).
void rt_uimenulist_add_item(void *ptr, rt_string text) {
    if (!ptr || !text)
        return;
    rt_uimenulist_impl *menu =
        checked_menulist(ptr, "UIMenuList.AddItem: expected Viper.Game.UI.MenuList");
    if (!menu)
        return;
    if (menu->count >= RT_UIMENULIST_MAX_ITEMS) {
        rt_trap("UIMenuList.AddItem: item limit exceeded (max 64)");
        return;
    }

    ui_copy_text(menu->items[menu->count], sizeof(menu->items[menu->count]), text);
    menu->count++;
}

/// @brief Remove all entries from the uimenulist.
void rt_uimenulist_clear(void *ptr) {
    rt_uimenulist_impl *menu =
        checked_menulist(ptr, "UIMenuList.Clear: expected Viper.Game.UI.MenuList");
    if (!menu)
        return;
    menu->count = 0;
    menu->selected = 0;
}

/// @brief Set the selected item index, clamped to [0, count-1].
void rt_uimenulist_set_selected(void *ptr, int64_t index) {
    rt_uimenulist_impl *menu =
        checked_menulist(ptr, "UIMenuList.SetSelected: expected Viper.Game.UI.MenuList");
    if (!menu)
        return;
    if (menu->count == 0) {
        menu->selected = 0;
        return;
    }
    if (index < 0)
        index = 0;
    if (index >= menu->count)
        index = menu->count - 1;
    menu->selected = index;
}

/// @brief Return the zero-based index of the currently highlighted menu item.
int64_t rt_uimenulist_get_selected(void *ptr) {
    rt_uimenulist_impl *menu =
        checked_menulist(ptr, "UIMenuList.Selected: expected Viper.Game.UI.MenuList");
    return menu ? menu->selected : 0;
}

/// @brief Move the selection cursor up by one; wraps from first to last item.
void rt_uimenulist_move_up(void *ptr) {
    rt_uimenulist_impl *menu =
        checked_menulist(ptr, "UIMenuList.MoveUp: expected Viper.Game.UI.MenuList");
    if (!menu)
        return;
    if (menu->count == 0)
        return;
    if (menu->selected <= 0)
        menu->selected = menu->count - 1; // Wrap to bottom
    else
        menu->selected--;
}

/// @brief Move the selection cursor down by one; wraps from last to first item.
void rt_uimenulist_move_down(void *ptr) {
    rt_uimenulist_impl *menu =
        checked_menulist(ptr, "UIMenuList.MoveDown: expected Viper.Game.UI.MenuList");
    if (!menu)
        return;
    if (menu->count == 0)
        return;
    if (menu->selected >= menu->count - 1)
        menu->selected = 0; // Wrap to top
    else
        menu->selected++;
}

/// @brief Set text/selection colors. `text_color` is the unselected entry color, `selected_color`
/// is the highlighted entry, `highlight_bg` is the row background drawn behind the selection.
void rt_uimenulist_set_colors(void *ptr,
                              int64_t text_color,
                              int64_t selected_color,
                              int64_t highlight_bg) {
    rt_uimenulist_impl *menu =
        checked_menulist(ptr, "UIMenuList.SetColors: expected Viper.Game.UI.MenuList");
    if (!menu)
        return;
    menu->text_color = text_color;
    menu->selected_color = selected_color;
    menu->highlight_bg = highlight_bg;
}

/// @brief Assign a BitmapFont for menu item text; NULL uses the default font.
void rt_uimenulist_set_font(void *ptr, void *font) {
    rt_uimenulist_impl *menu =
        checked_menulist(ptr, "UIMenuList.SetFont: expected Viper.Game.UI.MenuList");
    if (!menu)
        return;
    if (!ui_validate_bitmapfont(font, "UIMenuList.SetFont: expected BitmapFont"))
        return;
    ui_replace_ref(&menu->font, font);
}

/// @brief Show or hide the menu list; hidden menus are skipped during draw.
void rt_uimenulist_set_visible(void *ptr, int8_t visible) {
    rt_uimenulist_impl *menu =
        checked_menulist(ptr, "UIMenuList.SetVisible: expected Viper.Game.UI.MenuList");
    if (menu)
        menu->visible = visible ? 1 : 0;
}

/// @brief Return the count of elements in the uimenulist.
int64_t rt_uimenulist_get_count(void *ptr) {
    rt_uimenulist_impl *menu =
        checked_menulist(ptr, "UIMenuList.Count: expected Viper.Game.UI.MenuList");
    return menu ? menu->count : 0;
}

/// @brief Draw the uimenulist.
void rt_uimenulist_draw(void *ptr, void *canvas) {
    if (!ptr || !canvas)
        return;
    rt_uimenulist_impl *menu =
        checked_menulist(ptr, "UIMenuList.Draw: expected Viper.Game.UI.MenuList");
    if (!menu || !ui_validate_canvas(canvas, "UIMenuList.Draw: expected Canvas"))
        return;
    if (!menu->visible || menu->count == 0)
        return;

    for (int64_t i = 0; i < menu->count; i++) {
        int64_t iy = menu->y + i * menu->item_height;
        int8_t is_selected = (i == menu->selected);

        // Highlight background for selected item
        if (is_selected) {
            // Use the full canvas width? No — use a reasonable highlight width.
            // Measure text to get width, or use a fixed highlight width.
            int64_t hw = 200; // Default highlight width
            rt_string rtext = rt_const_cstr(menu->items[i]);
            if (menu->font)
                hw = rt_bitmapfont_text_width(menu->font, rtext) + 16;
            else
                hw = rt_canvas_text_width(rtext) + 16;
            rt_canvas_box(canvas, menu->x - 4, iy, hw, menu->item_height, menu->highlight_bg);
        }

        int64_t color = is_selected ? menu->selected_color : menu->text_color;
        rt_string rtext = rt_const_cstr(menu->items[i]);

        if (menu->font)
            rt_canvas_text_font(canvas, menu->x, iy + 2, rtext, menu->font, color);
        else
            rt_canvas_text(canvas, menu->x, iy + 2, rtext, color);
    }
}

/// @brief Handle input for menu navigation. Returns selected index on confirm, -1 otherwise.
int64_t rt_uimenulist_handle_input(void *ptr, int8_t up, int8_t down, int8_t confirm) {
    rt_uimenulist_impl *menu =
        checked_menulist(ptr, "UIMenuList.HandleInput: expected Viper.Game.UI.MenuList");
    if (!menu)
        return -1;
    if (!menu->visible || menu->count == 0)
        return -1;

    if (up && !down) {
        menu->selected--;
        if (menu->selected < 0)
            menu->selected = menu->count - 1; // wrap
    }
    if (down && !up) {
        menu->selected++;
        if (menu->selected >= menu->count)
            menu->selected = 0; // wrap
    }
    if (confirm)
        return menu->selected;
    return -1;
}

//=============================================================================
// GameButton — standalone styled button for custom layouts
//=============================================================================

typedef struct {
    void *vptr;
    int64_t x, y, width, height;
    char text[64];
    int64_t text_scale;
    int64_t color_normal;
    int64_t color_selected;
    int64_t text_color;
    int64_t text_color_selected;
    int64_t border_color;
    int64_t border_width;
    int8_t visible;
} rt_gamebutton_impl;

static rt_gamebutton_impl *checked_gamebutton(void *ptr, const char *api) {
    if (!ptr)
        return NULL;
    if (rt_obj_class_id(ptr) != RT_GAMEBUTTON_CLASS_ID) {
        rt_trap(api);
        return NULL;
    }
    return (rt_gamebutton_impl *)ptr;
}

/// @brief Construct a styled game button at (x, y) with size (w, h) and label text. Defaults:
/// dark gray normal, blue selected, light gray text, 1 px border, scale 1, visible. Text is
/// truncated to 63 bytes.
void *rt_gamebutton_new(int64_t x, int64_t y, int64_t w, int64_t h, void *text) {
    rt_gamebutton_impl *btn = (rt_gamebutton_impl *)rt_obj_new_i64(
        RT_GAMEBUTTON_CLASS_ID, (int64_t)sizeof(rt_gamebutton_impl));
    if (!btn)
        return NULL;
    memset(btn, 0, sizeof(rt_gamebutton_impl));
    btn->x = x;
    btn->y = y;
    btn->width = ui_clamp_dim(w);
    btn->height = ui_clamp_dim(h);
    btn->text_scale = 1;
    btn->color_normal = 0x333333;
    btn->color_selected = 0x4444AA;
    btn->text_color = 0xCCCCCC;
    btn->text_color_selected = 0xFFFFFF;
    btn->border_color = 0x666666;
    btn->border_width = 1;
    btn->visible = 1;
    ui_copy_text(btn->text, sizeof(btn->text), (rt_string)text);
    return btn;
}

/// @brief Replace the button's label text (truncated to 63 bytes).
void rt_gamebutton_set_text(void *ptr, void *text) {
    rt_gamebutton_impl *btn =
        checked_gamebutton(ptr, "GameButton.SetText: expected Viper.Game.UI.GameButton");
    if (!btn)
        return;
    ui_copy_text(btn->text, sizeof(btn->text), (rt_string)text);
}

/// @brief Set the box-fill colors for unselected (`normal`) and selected (highlighted) states.
void rt_gamebutton_set_colors(void *ptr, int64_t normal, int64_t selected) {
    rt_gamebutton_impl *btn =
        checked_gamebutton(ptr, "GameButton.SetColors: expected Viper.Game.UI.GameButton");
    if (!btn)
        return;
    btn->color_normal = normal;
    btn->color_selected = selected;
}

/// @brief Set the text colors for unselected and selected states.
void rt_gamebutton_set_text_colors(void *ptr, int64_t normal, int64_t selected) {
    rt_gamebutton_impl *btn =
        checked_gamebutton(ptr, "GameButton.SetTextColors: expected Viper.Game.UI.GameButton");
    if (!btn)
        return;
    btn->text_color = normal;
    btn->text_color_selected = selected;
}

/// @brief Set border outline width (px) and color. Width 0 disables the border.
void rt_gamebutton_set_border(void *ptr, int64_t width, int64_t color) {
    rt_gamebutton_impl *btn =
        checked_gamebutton(ptr, "GameButton.SetBorder: expected Viper.Game.UI.GameButton");
    if (!btn)
        return;
    btn->border_width = width > 0 ? width : 0;
    btn->border_color = color;
}

/// @brief Read the button's screen X position.
int64_t rt_gamebutton_get_x(void *ptr) {
    rt_gamebutton_impl *btn =
        checked_gamebutton(ptr, "GameButton.X: expected Viper.Game.UI.GameButton");
    return btn ? btn->x : 0;
}

/// @brief Read the button's screen Y position.
int64_t rt_gamebutton_get_y(void *ptr) {
    rt_gamebutton_impl *btn =
        checked_gamebutton(ptr, "GameButton.Y: expected Viper.Game.UI.GameButton");
    return btn ? btn->y : 0;
}

/// @brief Read the button width in pixels.
int64_t rt_gamebutton_get_width(void *ptr) {
    rt_gamebutton_impl *btn =
        checked_gamebutton(ptr, "GameButton.Width: expected Viper.Game.UI.GameButton");
    return btn ? btn->width : 0;
}

/// @brief Read the button height in pixels.
int64_t rt_gamebutton_get_height(void *ptr) {
    rt_gamebutton_impl *btn =
        checked_gamebutton(ptr, "GameButton.Height: expected Viper.Game.UI.GameButton");
    return btn ? btn->height : 0;
}

/// @brief Set the button's screen X position.
void rt_gamebutton_set_x(void *ptr, int64_t v) {
    rt_gamebutton_impl *btn =
        checked_gamebutton(ptr, "GameButton.SetX: expected Viper.Game.UI.GameButton");
    if (btn)
        btn->x = v;
}

/// @brief Set the button's screen Y position.
void rt_gamebutton_set_y(void *ptr, int64_t v) {
    rt_gamebutton_impl *btn =
        checked_gamebutton(ptr, "GameButton.SetY: expected Viper.Game.UI.GameButton");
    if (btn)
        btn->y = v;
}

/// @brief Resize the button, clamping dimensions to the UI maximum.
void rt_gamebutton_set_size(void *ptr, int64_t w, int64_t h) {
    rt_gamebutton_impl *btn =
        checked_gamebutton(ptr, "GameButton.SetSize: expected Viper.Game.UI.GameButton");
    if (!btn)
        return;
    btn->width = ui_clamp_dim(w);
    btn->height = ui_clamp_dim(h);
}

/// @brief Toggle visibility.
void rt_gamebutton_set_visible(void *ptr, int8_t visible) {
    rt_gamebutton_impl *btn =
        checked_gamebutton(ptr, "GameButton.SetVisible: expected Viper.Game.UI.GameButton");
    if (btn)
        btn->visible = visible ? 1 : 0;
}

/// @brief Return 1 if the button is visible.
int8_t rt_gamebutton_get_visible(void *ptr) {
    rt_gamebutton_impl *btn =
        checked_gamebutton(ptr, "GameButton.Visible: expected Viper.Game.UI.GameButton");
    return btn ? btn->visible : 0;
}

/// @brief Set integer text scale, clamped to [1, 16].
void rt_gamebutton_set_text_scale(void *ptr, int64_t scale) {
    rt_gamebutton_impl *btn =
        checked_gamebutton(ptr, "GameButton.SetTextScale: expected Viper.Game.UI.GameButton");
    if (btn)
        btn->text_scale = ui_clamp_scale(scale);
}

/// @brief Return the current integer text scale.
int64_t rt_gamebutton_get_text_scale(void *ptr) {
    rt_gamebutton_impl *btn =
        checked_gamebutton(ptr, "GameButton.TextScale: expected Viper.Game.UI.GameButton");
    return btn ? btn->text_scale : 1;
}

/// @brief Render the button to `canvas`. Picks colors based on `is_selected`, draws box +
/// optional border + centered label. No-op if button is hidden.
void rt_gamebutton_draw(void *ptr, void *canvas, int8_t is_selected) {
    if (!ptr || !canvas)
        return;
    rt_gamebutton_impl *btn =
        checked_gamebutton(ptr, "GameButton.Draw: expected Viper.Game.UI.GameButton");
    if (!btn || !ui_validate_canvas(canvas, "GameButton.Draw: expected Canvas"))
        return;
    if (!btn->visible)
        return;

    int64_t bg = is_selected ? btn->color_selected : btn->color_normal;
    int64_t tc = is_selected ? btn->text_color_selected : btn->text_color;

    // Background
    rt_canvas_box(canvas, btn->x, btn->y, btn->width, btn->height, bg);

    // Border
    if (btn->border_width > 0 && btn->border_color != 0) {
        int64_t max_t = (btn->width < btn->height ? btn->width : btn->height) / 2;
        if (max_t < 1)
            max_t = 1;
        int64_t border = btn->border_width > max_t ? max_t : btn->border_width;
        for (int64_t t = 0; t < border; t++) {
            int64_t bw = btn->width - t * 2;
            int64_t bh = btn->height - t * 2;
            if (bw <= 0 || bh <= 0)
                break;
            rt_canvas_frame(canvas, btn->x + t, btn->y + t, bw, bh, btn->border_color);
        }
    }

    // Centered text
    if (btn->text[0] != '\0') {
        int64_t scale = ui_clamp_scale(btn->text_scale);
        int64_t cell_w = 8 * scale;
        int64_t max_codepoints = cell_w > 0 ? btn->width / cell_w : 0;
        if (max_codepoints <= 0)
            return;
        char clipped[sizeof(btn->text)];
        size_t text_len = ui_visible_len(btn->text, sizeof(btn->text));
        size_t clipped_len = ui_utf8_trunc_codepoints(btn->text, text_len, (size_t)max_codepoints);
        memcpy(clipped, btn->text, clipped_len);
        clipped[clipped_len] = '\0';
        rt_string rtext = rt_const_cstr(clipped);
        int64_t text_w = rt_canvas_text_scaled_width(rtext, scale);
        int64_t text_h = rt_canvas_text_height() * scale;
        int64_t text_x = btn->x + (btn->width - text_w) / 2;
        int64_t text_y = btn->y + (btn->height - text_h) / 2;
        if (scale > 1)
            rt_canvas_text_scaled(canvas, text_x, text_y, rtext, scale, tc);
        else
            rt_canvas_text(canvas, text_x, text_y, rtext, tc);
    }
}

//=============================================================================
// Runtime-additions shared constants
//=============================================================================

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

static int64_t ui_codepoint_count_bytes(const char *text, int64_t bytes) {
    if (!text || bytes <= 0)
        return 0;
    size_t pos = 0;
    int64_t count = 0;
    while (pos < (size_t)bytes) {
        size_t len = ui_utf8_cp_len(text, (size_t)bytes, pos);
        if (pos + len > (size_t)bytes)
            break;
        pos += len;
        count++;
    }
    return count;
}

static int64_t ui_byte_for_codepoint(const char *text, int64_t bytes, int64_t cp_index) {
    if (!text || bytes <= 0 || cp_index <= 0)
        return 0;
    size_t pos = 0;
    int64_t count = 0;
    while (pos < (size_t)bytes && count < cp_index) {
        size_t len = ui_utf8_cp_len(text, (size_t)bytes, pos);
        if (pos + len > (size_t)bytes)
            break;
        pos += len;
        count++;
    }
    return (int64_t)pos;
}

static int64_t ui_codepoint_for_byte(const char *text, int64_t bytes, int64_t byte_index) {
    if (!text || bytes <= 0 || byte_index <= 0)
        return 0;
    if (byte_index > bytes)
        byte_index = bytes;
    size_t pos = 0;
    int64_t count = 0;
    while (pos < (size_t)byte_index) {
        size_t len = ui_utf8_cp_len(text, (size_t)bytes, pos);
        if (pos + len > (size_t)bytes || pos + len > (size_t)byte_index)
            break;
        pos += len;
        count++;
    }
    return count;
}

static int64_t ui_prev_codepoint_byte(const char *text, int64_t bytes, int64_t byte_index) {
    if (!text || bytes <= 0 || byte_index <= 0)
        return 0;
    if (byte_index > bytes)
        byte_index = bytes;
    int64_t prev = 0;
    int64_t pos = 0;
    while (pos < byte_index) {
        prev = pos;
        size_t len = ui_utf8_cp_len(text, (size_t)bytes, (size_t)pos);
        if (pos + (int64_t)len >= byte_index)
            break;
        pos += (int64_t)len;
    }
    return prev;
}

static int64_t ui_next_codepoint_byte(const char *text, int64_t bytes, int64_t byte_index) {
    if (!text || bytes <= 0 || byte_index >= bytes)
        return bytes > 0 ? bytes : 0;
    if (byte_index < 0)
        byte_index = 0;
    size_t len = ui_utf8_cp_len(text, (size_t)bytes, (size_t)byte_index);
    int64_t next = byte_index + (int64_t)len;
    return next > bytes ? bytes : next;
}

static int64_t ui_text_prefix_width(const char *text, int64_t bytes, void *font, int64_t scale) {
    if (!text || bytes <= 0)
        return 0;
    if (bytes > 511)
        bytes = 511;
    char tmp[512];
    memcpy(tmp, text, (size_t)bytes);
    tmp[bytes] = '\0';
    rt_string s = rt_const_cstr(tmp);
    int64_t width = font ? rt_bitmapfont_text_width(font, s) : rt_canvas_text_width(s);
    return width * ui_clamp_scale(scale);
}

static void ui_draw_text_basic(
    void *canvas, int64_t x, int64_t y, const char *text, void *font, int64_t scale, int64_t color) {
    if (!canvas || !text || text[0] == '\0')
        return;
    rt_string s = rt_const_cstr(text);
    scale = ui_clamp_scale(scale);
    if (font) {
        if (scale > 1)
            rt_canvas_text_font_scaled(canvas, x, y, s, font, scale, color);
        else
            rt_canvas_text_font(canvas, x, y, s, font, color);
    } else if (scale > 1) {
        rt_canvas_text_scaled(canvas, x, y, s, scale, color);
    } else {
        rt_canvas_text(canvas, x, y, s, color);
    }
}

static int8_t ui_point_inside(int64_t x,
                              int64_t y,
                              int64_t w,
                              int64_t h,
                              int64_t px,
                              int64_t py) {
    return ui_coord_inside(x, w, px) && ui_coord_inside(y, h, py);
}

//=============================================================================
// UITextInput
//=============================================================================

#define RT_UITEXTINPUT_MAX_BYTES 512
#define RT_UITEXTINPUT_DEFAULT_CURSOR_BLINK_MS 530

typedef struct {
    void *vptr;
    int64_t x, y, w, h;
    char text[RT_UITEXTINPUT_MAX_BYTES];
    int64_t text_bytes;
    int64_t cursor_byte;
    int64_t selection_anchor;
    int64_t scroll_byte;
    int64_t text_color;
    int64_t bg_color;
    int64_t cursor_color;
    int64_t selection_color;
    int64_t border_color;
    int64_t border_color_focused;
    int64_t cursor_blink_ms;
    int64_t cursor_blink_elapsed;
    void *font;
    int8_t visible;
    int8_t enabled;
    int8_t focused;
    int8_t password_mode;
    int8_t multiline;
    int64_t max_codepoints;
    char placeholder[RT_UITEXTINPUT_MAX_BYTES];
} rt_uitextinput_impl;

static rt_uitextinput_impl *checked_textinput(void *ptr, const char *api) {
    if (!ptr)
        return NULL;
    if (rt_obj_class_id(ptr) != RT_UITEXTINPUT_CLASS_ID) {
        rt_trap(api);
        return NULL;
    }
    return (rt_uitextinput_impl *)ptr;
}

static void uitextinput_finalizer(void *obj) {
    rt_uitextinput_impl *ti = (rt_uitextinput_impl *)obj;
    if (!ti)
        return;
    ui_release_obj(ti->font);
    ti->font = NULL;
}

static void textinput_set_bytes(rt_uitextinput_impl *ti, const char *text, size_t len) {
    if (!ti)
        return;
    if (!text)
        len = 0;
    len = ui_utf8_trunc_len(text, len, RT_UITEXTINPUT_MAX_BYTES - 1);
    if (ti->max_codepoints > 0)
        len = ui_utf8_trunc_codepoints(text, len, (size_t)ti->max_codepoints);
    if (len > 0)
        memcpy(ti->text, text, len);
    ti->text[len] = '\0';
    ti->text_bytes = (int64_t)len;
    ti->cursor_byte = ti->text_bytes;
    ti->selection_anchor = -1;
    ti->scroll_byte = 0;
}

static int8_t textinput_selection_range(rt_uitextinput_impl *ti, int64_t *start, int64_t *end) {
    if (!ti || ti->selection_anchor < 0 || ti->selection_anchor == ti->cursor_byte)
        return 0;
    int64_t a = ti->selection_anchor;
    int64_t b = ti->cursor_byte;
    if (a > b) {
        int64_t tmp = a;
        a = b;
        b = tmp;
    }
    if (a < 0)
        a = 0;
    if (b > ti->text_bytes)
        b = ti->text_bytes;
    if (a >= b)
        return 0;
    if (start)
        *start = a;
    if (end)
        *end = b;
    return 1;
}

static int8_t textinput_delete_range(rt_uitextinput_impl *ti, int64_t start, int64_t end) {
    if (!ti || start < 0 || end <= start || start >= ti->text_bytes)
        return 0;
    if (end > ti->text_bytes)
        end = ti->text_bytes;
    memmove(ti->text + start, ti->text + end, (size_t)(ti->text_bytes - end + 1));
    ti->text_bytes -= end - start;
    ti->cursor_byte = start;
    ti->selection_anchor = -1;
    if (ti->scroll_byte > ti->cursor_byte)
        ti->scroll_byte = ti->cursor_byte;
    return 1;
}

static int8_t textinput_delete_selection(rt_uitextinput_impl *ti) {
    int64_t start = 0;
    int64_t end = 0;
    return textinput_selection_range(ti, &start, &end) ? textinput_delete_range(ti, start, end) : 0;
}

static int8_t textinput_insert_bytes(rt_uitextinput_impl *ti, const char *src, size_t src_len) {
    if (!ti || !src || src_len == 0)
        return 0;
    src_len = ui_utf8_trunc_len(src, src_len, src_len);
    if (src_len == 0)
        return 0;
    int8_t changed = textinput_delete_selection(ti);
    int64_t current_cps = ui_codepoint_count_bytes(ti->text, ti->text_bytes);
    int64_t remaining_cps =
        ti->max_codepoints > 0 ? (ti->max_codepoints - current_cps) : INT64_MAX;
    if (remaining_cps <= 0)
        return changed;
    size_t accepted = 0;
    int64_t cps = 0;
    while (accepted < src_len && cps < remaining_cps) {
        size_t cp_len = ui_utf8_cp_len(src, src_len, accepted);
        if (accepted + cp_len > src_len)
            break;
        if ((int64_t)cp_len > (RT_UITEXTINPUT_MAX_BYTES - 1) - ti->text_bytes)
            break;
        if (!ti->multiline && (src[accepted] == '\n' || src[accepted] == '\r')) {
            accepted += cp_len;
            continue;
        }
        memmove(ti->text + ti->cursor_byte + (int64_t)cp_len,
                ti->text + ti->cursor_byte,
                (size_t)(ti->text_bytes - ti->cursor_byte + 1));
        memcpy(ti->text + ti->cursor_byte, src + accepted, cp_len);
        ti->cursor_byte += (int64_t)cp_len;
        ti->text_bytes += (int64_t)cp_len;
        accepted += cp_len;
        cps++;
        changed = 1;
    }
    ti->selection_anchor = -1;
    return changed;
}

static void textinput_move_cursor(rt_uitextinput_impl *ti, int64_t byte_pos, int8_t shift_held) {
    if (!ti)
        return;
    if (byte_pos < 0)
        byte_pos = 0;
    if (byte_pos > ti->text_bytes)
        byte_pos = ti->text_bytes;
    if (shift_held) {
        if (ti->selection_anchor < 0)
            ti->selection_anchor = ti->cursor_byte;
    } else {
        ti->selection_anchor = -1;
    }
    ti->cursor_byte = byte_pos;
    ti->cursor_blink_elapsed = 0;
}

static int64_t textinput_byte_from_mouse(rt_uitextinput_impl *ti, int64_t mx) {
    if (!ti || ti->text_bytes <= 0)
        return 0;
    int64_t local = mx - ti->x - 4;
    if (local <= 0)
        return 0;
    int64_t pos = 0;
    int64_t best = ti->text_bytes;
    while (pos < ti->text_bytes) {
        int64_t next = ui_next_codepoint_byte(ti->text, ti->text_bytes, pos);
        int64_t mid = ui_text_prefix_width(ti->text, pos, ti->font, 1) +
                      (ui_text_prefix_width(ti->text + pos, next - pos, ti->font, 1) / 2);
        if (local < mid) {
            best = pos;
            break;
        }
        pos = next;
    }
    return best;
}

void *rt_uitextinput_new(int64_t x, int64_t y, int64_t w, int64_t h) {
    rt_uitextinput_impl *ti = (rt_uitextinput_impl *)rt_obj_new_i64(
        RT_UITEXTINPUT_CLASS_ID, (int64_t)sizeof(rt_uitextinput_impl));
    if (!ti)
        return NULL;
    memset(ti, 0, sizeof(*ti));
    ti->x = x;
    ti->y = y;
    ti->w = ui_clamp_dim(w);
    ti->h = ui_clamp_dim(h);
    ti->selection_anchor = -1;
    ti->text_color = 0xFFFFFF;
    ti->bg_color = 0x202020;
    ti->cursor_color = 0xFFFFFF;
    ti->selection_color = 0x3355AA;
    ti->border_color = 0x606060;
    ti->border_color_focused = 0x88AAFF;
    ti->cursor_blink_ms = RT_UITEXTINPUT_DEFAULT_CURSOR_BLINK_MS;
    ti->visible = 1;
    ti->enabled = 1;
    rt_obj_set_finalizer(ti, uitextinput_finalizer);
    return ti;
}

void rt_uitextinput_set_text(void *ptr, rt_string text) {
    rt_uitextinput_impl *ti =
        checked_textinput(ptr, "UITextInput.SetText: expected Viper.Game.UI.TextInput");
    if (!ti)
        return;
    const char *s = text ? rt_string_cstr(text) : "";
    size_t len = text ? (size_t)rt_str_len(text) : 0;
    textinput_set_bytes(ti, s, len);
}

rt_string rt_uitextinput_get_text(void *ptr) {
    rt_uitextinput_impl *ti =
        checked_textinput(ptr, "UITextInput.GetText: expected Viper.Game.UI.TextInput");
    return ti ? rt_const_cstr(ti->text) : rt_str_empty();
}

int64_t rt_uitextinput_text_length(void *ptr) {
    rt_uitextinput_impl *ti =
        checked_textinput(ptr, "UITextInput.TextLength: expected Viper.Game.UI.TextInput");
    return ti ? ui_codepoint_count_bytes(ti->text, ti->text_bytes) : 0;
}

int64_t rt_uitextinput_get_cursor(void *ptr) {
    rt_uitextinput_impl *ti =
        checked_textinput(ptr, "UITextInput.GetCursor: expected Viper.Game.UI.TextInput");
    return ti ? ui_codepoint_for_byte(ti->text, ti->text_bytes, ti->cursor_byte) : 0;
}

void rt_uitextinput_set_cursor(void *ptr, int64_t pos) {
    rt_uitextinput_impl *ti =
        checked_textinput(ptr, "UITextInput.SetCursor: expected Viper.Game.UI.TextInput");
    if (ti)
        textinput_move_cursor(ti, ui_byte_for_codepoint(ti->text, ti->text_bytes, pos), 0);
}

void rt_uitextinput_select_all(void *ptr) {
    rt_uitextinput_impl *ti =
        checked_textinput(ptr, "UITextInput.SelectAll: expected Viper.Game.UI.TextInput");
    if (!ti)
        return;
    ti->selection_anchor = 0;
    ti->cursor_byte = ti->text_bytes;
}

void rt_uitextinput_clear_selection(void *ptr) {
    rt_uitextinput_impl *ti =
        checked_textinput(ptr, "UITextInput.ClearSelection: expected Viper.Game.UI.TextInput");
    if (ti)
        ti->selection_anchor = -1;
}

int8_t rt_uitextinput_has_selection(void *ptr) {
    rt_uitextinput_impl *ti =
        checked_textinput(ptr, "UITextInput.HasSelection: expected Viper.Game.UI.TextInput");
    return textinput_selection_range(ti, NULL, NULL);
}

rt_string rt_uitextinput_get_selected_text(void *ptr) {
    rt_uitextinput_impl *ti =
        checked_textinput(ptr, "UITextInput.GetSelectedText: expected Viper.Game.UI.TextInput");
    int64_t start = 0;
    int64_t end = 0;
    if (!textinput_selection_range(ti, &start, &end))
        return rt_str_empty();
    return rt_string_from_bytes(ti->text + start, (size_t)(end - start));
}

void rt_uitextinput_delete_selection(void *ptr) {
    rt_uitextinput_impl *ti =
        checked_textinput(ptr, "UITextInput.DeleteSelection: expected Viper.Game.UI.TextInput");
    textinput_delete_selection(ti);
}

int64_t rt_uitextinput_handle_key(void *ptr, int64_t key_code, int8_t shift_held) {
    rt_uitextinput_impl *ti =
        checked_textinput(ptr, "UITextInput.HandleKey: expected Viper.Game.UI.TextInput");
    if (!ti || !ti->enabled || !ti->focused)
        return 0;
    if (key_code == UI_KEY_BACKSPACE) {
        if (textinput_delete_selection(ti))
            return 1;
        if (ti->cursor_byte <= 0)
            return 0;
        return textinput_delete_range(ti,
                                      ui_prev_codepoint_byte(ti->text, ti->text_bytes, ti->cursor_byte),
                                      ti->cursor_byte);
    }
    if (key_code == UI_KEY_DELETE) {
        if (textinput_delete_selection(ti))
            return 1;
        if (ti->cursor_byte >= ti->text_bytes)
            return 0;
        return textinput_delete_range(ti,
                                      ti->cursor_byte,
                                      ui_next_codepoint_byte(ti->text, ti->text_bytes, ti->cursor_byte));
    }
    if (key_code == UI_KEY_LEFT) {
        if (!shift_held && rt_uitextinput_has_selection(ptr)) {
            int64_t start = 0;
            int64_t end = 0;
            textinput_selection_range(ti, &start, &end);
            (void)end;
            textinput_move_cursor(ti, start, 0);
        } else {
            textinput_move_cursor(
                ti, ui_prev_codepoint_byte(ti->text, ti->text_bytes, ti->cursor_byte), shift_held);
        }
        return 0;
    }
    if (key_code == UI_KEY_RIGHT) {
        if (!shift_held && rt_uitextinput_has_selection(ptr)) {
            int64_t start = 0;
            int64_t end = 0;
            (void)start;
            textinput_selection_range(ti, &start, &end);
            textinput_move_cursor(ti, end, 0);
        } else {
            textinput_move_cursor(
                ti, ui_next_codepoint_byte(ti->text, ti->text_bytes, ti->cursor_byte), shift_held);
        }
        return 0;
    }
    if (key_code == UI_KEY_HOME) {
        textinput_move_cursor(ti, 0, shift_held);
        return 0;
    }
    if (key_code == UI_KEY_END) {
        textinput_move_cursor(ti, ti->text_bytes, shift_held);
        return 0;
    }
    if (key_code == 1) {
        rt_uitextinput_select_all(ptr);
        return 0;
    }
    return 0;
}

int64_t rt_uitextinput_handle_text(void *ptr, rt_string typed_text) {
    rt_uitextinput_impl *ti =
        checked_textinput(ptr, "UITextInput.HandleText: expected Viper.Game.UI.TextInput");
    if (!ti || !typed_text || !ti->enabled || !ti->focused)
        return 0;
    const char *s = rt_string_cstr(typed_text);
    return textinput_insert_bytes(ti, s, (size_t)rt_str_len(typed_text));
}

void rt_uitextinput_handle_mouse_click(void *ptr, int64_t mx, int64_t my, int8_t shift_held) {
    rt_uitextinput_impl *ti = checked_textinput(
        ptr, "UITextInput.HandleMouseClick: expected Viper.Game.UI.TextInput");
    if (!ti || !ti->enabled || !ti->visible)
        return;
    ti->focused = ui_point_inside(ti->x, ti->y, ti->w, ti->h, mx, my);
    if (ti->focused)
        textinput_move_cursor(ti, textinput_byte_from_mouse(ti, mx), shift_held);
}

void rt_uitextinput_handle_mouse_drag(void *ptr, int64_t mx, int64_t my) {
    (void)my;
    rt_uitextinput_impl *ti =
        checked_textinput(ptr, "UITextInput.HandleMouseDrag: expected Viper.Game.UI.TextInput");
    if (!ti || !ti->enabled || !ti->focused)
        return;
    if (ti->selection_anchor < 0)
        ti->selection_anchor = ti->cursor_byte;
    ti->cursor_byte = textinput_byte_from_mouse(ti, mx);
}

void rt_uitextinput_update(void *ptr, int64_t delta_ms) {
    rt_uitextinput_impl *ti =
        checked_textinput(ptr, "UITextInput.Update: expected Viper.Game.UI.TextInput");
    if (!ti || delta_ms <= 0)
        return;
    if (delta_ms > INT64_MAX - ti->cursor_blink_elapsed)
        ti->cursor_blink_elapsed = 0;
    else
        ti->cursor_blink_elapsed += delta_ms;
}

void rt_uitextinput_draw(void *ptr, void *canvas) {
    rt_uitextinput_impl *ti =
        checked_textinput(ptr, "UITextInput.Draw: expected Viper.Game.UI.TextInput");
    if (!ti || !canvas || !ui_validate_canvas(canvas, "UITextInput.Draw: expected Canvas"))
        return;
    if (!ti->visible)
        return;
    rt_canvas_box(canvas, ti->x, ti->y, ti->w, ti->h, ti->bg_color);
    rt_canvas_frame(
        canvas, ti->x, ti->y, ti->w, ti->h, ti->focused ? ti->border_color_focused : ti->border_color);

    const char *draw_text = ti->text;
    char password[RT_UITEXTINPUT_MAX_BYTES];
    if (ti->password_mode && ti->text_bytes > 0) {
        int64_t cps = ui_codepoint_count_bytes(ti->text, ti->text_bytes);
        if (cps >= RT_UITEXTINPUT_MAX_BYTES)
            cps = RT_UITEXTINPUT_MAX_BYTES - 1;
        memset(password, '*', (size_t)cps);
        password[cps] = '\0';
        draw_text = password;
    } else if (ti->text_bytes == 0 && !ti->focused && ti->placeholder[0] != '\0') {
        draw_text = ti->placeholder;
    }

    int64_t start = 0;
    int64_t end = 0;
    if (textinput_selection_range(ti, &start, &end)) {
        int64_t x0 = ti->x + 4 + ui_text_prefix_width(ti->text, start, ti->font, 1);
        int64_t sel_w = ui_text_prefix_width(ti->text + start, end - start, ti->font, 1);
        rt_canvas_box_alpha(canvas, x0, ti->y + 2, sel_w, ti->h - 4, ti->selection_color, 160);
    }
    ui_draw_text_basic(canvas, ti->x + 4, ti->y + (ti->h - 8) / 2, draw_text, ti->font, 1,
                       ti->text_bytes == 0 && !ti->focused ? 0x909090 : ti->text_color);
    if (ti->focused && ti->enabled && ti->cursor_blink_ms > 0 &&
        ((ti->cursor_blink_elapsed / ti->cursor_blink_ms) % 2) == 0) {
        int64_t cx = ti->x + 4 + ui_text_prefix_width(ti->text, ti->cursor_byte, ti->font, 1);
        rt_canvas_line(canvas, cx, ti->y + 3, cx, ti->y + ti->h - 4, ti->cursor_color);
    }
}

void rt_uitextinput_set_text_color(void *ptr, int64_t color) {
    rt_uitextinput_impl *ti =
        checked_textinput(ptr, "UITextInput.SetTextColor: expected Viper.Game.UI.TextInput");
    if (ti)
        ti->text_color = color;
}

int64_t rt_uitextinput_get_text_color(void *ptr) {
    rt_uitextinput_impl *ti =
        checked_textinput(ptr, "UITextInput.GetTextColor: expected Viper.Game.UI.TextInput");
    return ti ? ti->text_color : 0;
}

void rt_uitextinput_set_bg_color(void *ptr, int64_t color) {
    rt_uitextinput_impl *ti =
        checked_textinput(ptr, "UITextInput.SetBgColor: expected Viper.Game.UI.TextInput");
    if (ti)
        ti->bg_color = color;
}

int64_t rt_uitextinput_get_bg_color(void *ptr) {
    rt_uitextinput_impl *ti =
        checked_textinput(ptr, "UITextInput.GetBgColor: expected Viper.Game.UI.TextInput");
    return ti ? ti->bg_color : 0;
}

void rt_uitextinput_set_cursor_color(void *ptr, int64_t color) {
    rt_uitextinput_impl *ti =
        checked_textinput(ptr, "UITextInput.SetCursorColor: expected Viper.Game.UI.TextInput");
    if (ti)
        ti->cursor_color = color;
}

void rt_uitextinput_set_selection_color(void *ptr, int64_t color) {
    rt_uitextinput_impl *ti =
        checked_textinput(ptr, "UITextInput.SetSelectionColor: expected Viper.Game.UI.TextInput");
    if (ti)
        ti->selection_color = color;
}

void rt_uitextinput_set_border_color(void *ptr, int64_t color) {
    rt_uitextinput_impl *ti =
        checked_textinput(ptr, "UITextInput.SetBorderColor: expected Viper.Game.UI.TextInput");
    if (ti)
        ti->border_color = color;
}

void rt_uitextinput_set_border_color_focused(void *ptr, int64_t color) {
    rt_uitextinput_impl *ti = checked_textinput(
        ptr, "UITextInput.SetBorderColorFocused: expected Viper.Game.UI.TextInput");
    if (ti)
        ti->border_color_focused = color;
}

void rt_uitextinput_set_font(void *ptr, void *font) {
    rt_uitextinput_impl *ti =
        checked_textinput(ptr, "UITextInput.SetFont: expected Viper.Game.UI.TextInput");
    if (!ti || !ui_validate_bitmapfont(font, "UITextInput.SetFont: expected BitmapFont"))
        return;
    ui_replace_ref(&ti->font, font);
}

void rt_uitextinput_set_visible(void *ptr, int8_t visible) {
    rt_uitextinput_impl *ti =
        checked_textinput(ptr, "UITextInput.SetVisible: expected Viper.Game.UI.TextInput");
    if (ti)
        ti->visible = visible ? 1 : 0;
}

int8_t rt_uitextinput_get_visible(void *ptr) {
    rt_uitextinput_impl *ti =
        checked_textinput(ptr, "UITextInput.GetVisible: expected Viper.Game.UI.TextInput");
    return ti ? ti->visible : 0;
}

void rt_uitextinput_set_enabled(void *ptr, int8_t enabled) {
    rt_uitextinput_impl *ti =
        checked_textinput(ptr, "UITextInput.SetEnabled: expected Viper.Game.UI.TextInput");
    if (ti)
        ti->enabled = enabled ? 1 : 0;
}

int8_t rt_uitextinput_get_enabled(void *ptr) {
    rt_uitextinput_impl *ti =
        checked_textinput(ptr, "UITextInput.GetEnabled: expected Viper.Game.UI.TextInput");
    return ti ? ti->enabled : 0;
}

void rt_uitextinput_set_focused(void *ptr, int8_t focused) {
    rt_uitextinput_impl *ti =
        checked_textinput(ptr, "UITextInput.SetFocused: expected Viper.Game.UI.TextInput");
    if (!ti)
        return;
    ti->focused = focused ? 1 : 0;
    ti->cursor_blink_elapsed = 0;
    if (!ti->focused)
        ti->selection_anchor = -1;
}

int8_t rt_uitextinput_get_focused(void *ptr) {
    rt_uitextinput_impl *ti =
        checked_textinput(ptr, "UITextInput.GetFocused: expected Viper.Game.UI.TextInput");
    return ti ? ti->focused : 0;
}

void rt_uitextinput_set_password_mode(void *ptr, int8_t password) {
    rt_uitextinput_impl *ti =
        checked_textinput(ptr, "UITextInput.SetPasswordMode: expected Viper.Game.UI.TextInput");
    if (ti)
        ti->password_mode = password ? 1 : 0;
}

void rt_uitextinput_set_placeholder(void *ptr, rt_string placeholder) {
    rt_uitextinput_impl *ti =
        checked_textinput(ptr, "UITextInput.SetPlaceholder: expected Viper.Game.UI.TextInput");
    if (ti)
        ui_copy_text(ti->placeholder, sizeof(ti->placeholder), placeholder);
}

void rt_uitextinput_set_max_codepoints(void *ptr, int64_t max_cps) {
    rt_uitextinput_impl *ti =
        checked_textinput(ptr, "UITextInput.SetMaxCodepoints: expected Viper.Game.UI.TextInput");
    if (!ti)
        return;
    ti->max_codepoints = max_cps > 0 ? max_cps : 0;
    if (ti->max_codepoints > 0 &&
        ui_codepoint_count_bytes(ti->text, ti->text_bytes) > ti->max_codepoints)
        textinput_set_bytes(ti, ti->text, (size_t)ti->text_bytes);
}

//=============================================================================
// UITable
//=============================================================================

#define RT_UITABLE_MAX_COLUMNS 16
#define RT_UITABLE_MAX_ROWS 512
#define RT_UITABLE_MAX_CELL_BYTES 64

typedef struct {
    char title[64];
    int64_t width;
    int8_t align;
    int8_t sortable;
    int8_t sort_numeric;
} rt_uitable_column_t;

typedef struct {
    char cells[RT_UITABLE_MAX_COLUMNS][RT_UITABLE_MAX_CELL_BYTES];
} rt_uitable_row_t;

typedef struct {
    void *vptr;
    int64_t x, y, w, h;
    rt_uitable_column_t columns[RT_UITABLE_MAX_COLUMNS];
    int64_t column_count;
    rt_uitable_row_t rows[RT_UITABLE_MAX_ROWS];
    int64_t row_count;
    int64_t header_height;
    int64_t row_height;
    int64_t sort_column;
    int8_t sort_descending;
    int64_t scroll_offset;
    int64_t selected_row;
    int64_t last_header_click;
    int64_t text_color;
    int64_t header_text_color;
    int64_t header_bg_color;
    int64_t row_bg_color;
    int64_t row_alt_bg_color;
    int64_t selected_bg_color;
    int64_t border_color;
    void *font;
    int8_t visible;
    int8_t striped;
    int8_t show_header;
    int8_t show_borders;
} rt_uitable_impl;

static rt_uitable_impl *checked_table(void *ptr, const char *api) {
    if (!ptr)
        return NULL;
    if (rt_obj_class_id(ptr) != RT_UITABLE_CLASS_ID) {
        rt_trap(api);
        return NULL;
    }
    return (rt_uitable_impl *)ptr;
}

static void uitable_finalizer(void *obj) {
    rt_uitable_impl *table = (rt_uitable_impl *)obj;
    if (!table)
        return;
    ui_release_obj(table->font);
    table->font = NULL;
}

static int64_t table_visible_rows(rt_uitable_impl *table) {
    if (!table || table->row_height <= 0)
        return 0;
    int64_t usable = table->h - (table->show_header ? table->header_height : 0);
    if (usable <= 0)
        return 0;
    return usable / table->row_height;
}

static void table_clamp_scroll(rt_uitable_impl *table) {
    if (!table)
        return;
    int64_t visible_rows = table_visible_rows(table);
    int64_t max_scroll = table->row_count > visible_rows ? table->row_count - visible_rows : 0;
    if (table->scroll_offset < 0)
        table->scroll_offset = 0;
    if (table->scroll_offset > max_scroll)
        table->scroll_offset = max_scroll;
    if (table->selected_row >= table->row_count)
        table->selected_row = table->row_count - 1;
    if (table->row_count <= 0)
        table->selected_row = -1;
}

static int64_t table_column_next_x(int64_t x, int64_t width) {
    if (width <= 0)
        return x;
    return ui_add_sat_i64(x, width);
}

void *rt_uitable_new(int64_t x, int64_t y, int64_t w, int64_t h) {
    rt_uitable_impl *table =
        (rt_uitable_impl *)rt_obj_new_i64(RT_UITABLE_CLASS_ID, (int64_t)sizeof(*table));
    if (!table)
        return NULL;
    memset(table, 0, sizeof(*table));
    table->x = x;
    table->y = y;
    table->w = ui_clamp_dim(w);
    table->h = ui_clamp_dim(h);
    table->header_height = 22;
    table->row_height = 20;
    table->sort_column = -1;
    table->selected_row = -1;
    table->last_header_click = -1;
    table->text_color = 0xFFFFFF;
    table->header_text_color = 0xFFFFFF;
    table->header_bg_color = 0x303030;
    table->row_bg_color = 0x181818;
    table->row_alt_bg_color = 0x202020;
    table->selected_bg_color = 0x304A70;
    table->border_color = 0x606060;
    table->visible = 1;
    table->striped = 1;
    table->show_header = 1;
    table->show_borders = 1;
    rt_obj_set_finalizer(table, uitable_finalizer);
    return table;
}

int64_t rt_uitable_add_column(void *ptr, rt_string title, int64_t width, int64_t align) {
    rt_uitable_impl *table =
        checked_table(ptr, "UITable.AddColumn: expected Viper.Game.UI.Table");
    if (!table)
        return -1;
    if (table->column_count >= RT_UITABLE_MAX_COLUMNS) {
        rt_trap("UITable.AddColumn: column limit exceeded");
        return -1;
    }
    int64_t idx = table->column_count++;
    ui_copy_text(table->columns[idx].title, sizeof(table->columns[idx].title), title);
    table->columns[idx].width = width > 0 ? width : 80;
    table->columns[idx].align = (int8_t)(align < 0 || align > 2 ? 0 : align);
    table->columns[idx].sortable = 0;
    table->columns[idx].sort_numeric = 0;
    return idx;
}

void rt_uitable_set_column_sortable(void *ptr, int64_t col, int8_t sortable, int8_t numeric) {
    rt_uitable_impl *table =
        checked_table(ptr, "UITable.SetColumnSortable: expected Viper.Game.UI.Table");
    if (!table || col < 0 || col >= table->column_count)
        return;
    table->columns[col].sortable = sortable ? 1 : 0;
    table->columns[col].sort_numeric = numeric ? 1 : 0;
}

int64_t rt_uitable_column_count(void *ptr) {
    rt_uitable_impl *table =
        checked_table(ptr, "UITable.ColumnCount: expected Viper.Game.UI.Table");
    return table ? table->column_count : 0;
}

int64_t rt_uitable_add_row(void *ptr) {
    rt_uitable_impl *table = checked_table(ptr, "UITable.AddRow: expected Viper.Game.UI.Table");
    if (!table)
        return -1;
    if (table->row_count >= RT_UITABLE_MAX_ROWS) {
        rt_trap("UITable.AddRow: row limit exceeded");
        return -1;
    }
    int64_t idx = table->row_count++;
    memset(&table->rows[idx], 0, sizeof(table->rows[idx]));
    if (table->selected_row < 0)
        table->selected_row = 0;
    table_clamp_scroll(table);
    return idx;
}

void rt_uitable_set_cell(void *ptr, int64_t row, int64_t col, rt_string text) {
    rt_uitable_impl *table = checked_table(ptr, "UITable.SetCell: expected Viper.Game.UI.Table");
    if (!table || row < 0 || row >= table->row_count || col < 0 || col >= table->column_count)
        return;
    ui_copy_text(table->rows[row].cells[col], sizeof(table->rows[row].cells[col]), text);
}

rt_string rt_uitable_get_cell(void *ptr, int64_t row, int64_t col) {
    rt_uitable_impl *table = checked_table(ptr, "UITable.GetCell: expected Viper.Game.UI.Table");
    if (!table || row < 0 || row >= table->row_count || col < 0 || col >= table->column_count)
        return rt_str_empty();
    return rt_const_cstr(table->rows[row].cells[col]);
}

void rt_uitable_remove_row(void *ptr, int64_t row) {
    rt_uitable_impl *table =
        checked_table(ptr, "UITable.RemoveRow: expected Viper.Game.UI.Table");
    if (!table || row < 0 || row >= table->row_count)
        return;
    for (int64_t i = row; i < table->row_count - 1; i++)
        table->rows[i] = table->rows[i + 1];
    table->row_count--;
    memset(&table->rows[table->row_count], 0, sizeof(table->rows[table->row_count]));
    table_clamp_scroll(table);
}

void rt_uitable_clear_rows(void *ptr) {
    rt_uitable_impl *table =
        checked_table(ptr, "UITable.ClearRows: expected Viper.Game.UI.Table");
    if (!table)
        return;
    memset(table->rows, 0, sizeof(table->rows));
    table->row_count = 0;
    table->scroll_offset = 0;
    table->selected_row = -1;
}

int64_t rt_uitable_row_count(void *ptr) {
    rt_uitable_impl *table = checked_table(ptr, "UITable.RowCount: expected Viper.Game.UI.Table");
    return table ? table->row_count : 0;
}

static int table_compare_rows(rt_uitable_impl *table, const rt_uitable_row_t *a, const rt_uitable_row_t *b) {
    int64_t col = table->sort_column;
    if (col < 0 || col >= table->column_count)
        return 0;
    const char *sa = a->cells[col];
    const char *sb = b->cells[col];
    int cmp = 0;
    if (table->columns[col].sort_numeric) {
        double da = strtod(sa, NULL);
        double db = strtod(sb, NULL);
        cmp = da < db ? -1 : (da > db ? 1 : 0);
    } else {
        cmp = strcmp(sa, sb);
    }
    return table->sort_descending ? -cmp : cmp;
}

void rt_uitable_sort_by(void *ptr, int64_t col, int8_t descending) {
    rt_uitable_impl *table = checked_table(ptr, "UITable.SortBy: expected Viper.Game.UI.Table");
    if (!table || col < 0 || col >= table->column_count)
        return;
    if (!table->columns[col].sortable)
        table->columns[col].sortable = 1;
    table->sort_column = col;
    table->sort_descending = descending ? 1 : 0;
    for (int64_t i = 1; i < table->row_count; i++) {
        rt_uitable_row_t key = table->rows[i];
        int64_t j = i - 1;
        while (j >= 0 && table_compare_rows(table, &table->rows[j], &key) > 0) {
            table->rows[j + 1] = table->rows[j];
            j--;
        }
        table->rows[j + 1] = key;
    }
}

int64_t rt_uitable_get_sort_column(void *ptr) {
    rt_uitable_impl *table =
        checked_table(ptr, "UITable.SortColumn: expected Viper.Game.UI.Table");
    return table ? table->sort_column : -1;
}

int8_t rt_uitable_get_sort_descending(void *ptr) {
    rt_uitable_impl *table =
        checked_table(ptr, "UITable.SortDescending: expected Viper.Game.UI.Table");
    return table ? table->sort_descending : 0;
}

void rt_uitable_set_scroll(void *ptr, int64_t row) {
    rt_uitable_impl *table = checked_table(ptr, "UITable.SetScroll: expected Viper.Game.UI.Table");
    if (!table)
        return;
    table->scroll_offset = row;
    table_clamp_scroll(table);
}

int64_t rt_uitable_get_scroll(void *ptr) {
    rt_uitable_impl *table = checked_table(ptr, "UITable.Scroll: expected Viper.Game.UI.Table");
    return table ? table->scroll_offset : 0;
}

void rt_uitable_set_selected_row(void *ptr, int64_t row) {
    rt_uitable_impl *table =
        checked_table(ptr, "UITable.SetSelectedRow: expected Viper.Game.UI.Table");
    if (!table)
        return;
    table->selected_row = row < 0 || row >= table->row_count ? -1 : row;
}

int64_t rt_uitable_get_selected_row(void *ptr) {
    rt_uitable_impl *table =
        checked_table(ptr, "UITable.SelectedRow: expected Viper.Game.UI.Table");
    return table ? table->selected_row : -1;
}

int64_t rt_uitable_handle_click(void *ptr, int64_t mx, int64_t my) {
    rt_uitable_impl *table =
        checked_table(ptr, "UITable.HandleClick: expected Viper.Game.UI.Table");
    if (!table || !table->visible || !ui_point_inside(table->x, table->y, table->w, table->h, mx, my))
        return -1;
    table->last_header_click = -1;
    int64_t local_y = my - table->y;
    if (table->show_header && local_y < table->header_height) {
        int64_t cx = table->x;
        for (int64_t c = 0; c < table->column_count; c++) {
            int64_t cw = table->columns[c].width;
            if (ui_coord_inside(cx, cw, mx)) {
                table->last_header_click = c;
                if (table->columns[c].sortable)
                    rt_uitable_sort_by(ptr, c,
                                       table->sort_column == c ? !table->sort_descending : 0);
                return -2;
            }
            cx = table_column_next_x(cx, cw);
        }
        return -1;
    }
    int64_t row_y = local_y - (table->show_header ? table->header_height : 0);
    int64_t row = table->scroll_offset + row_y / table->row_height;
    if (row < 0 || row >= table->row_count)
        return -1;
    table->selected_row = row;
    return row;
}

int64_t rt_uitable_last_header_click(void *ptr) {
    rt_uitable_impl *table =
        checked_table(ptr, "UITable.LastHeaderClick: expected Viper.Game.UI.Table");
    return table ? table->last_header_click : -1;
}

void rt_uitable_handle_scroll(void *ptr, int64_t delta) {
    rt_uitable_impl *table =
        checked_table(ptr, "UITable.HandleScroll: expected Viper.Game.UI.Table");
    if (!table)
        return;
    table->scroll_offset = ui_add_sat_i64(table->scroll_offset, delta);
    table_clamp_scroll(table);
}

void rt_uitable_handle_key(void *ptr, int64_t key_code) {
    rt_uitable_impl *table =
        checked_table(ptr, "UITable.HandleKey: expected Viper.Game.UI.Table");
    if (!table || table->row_count <= 0)
        return;
    if (table->selected_row < 0)
        table->selected_row = 0;
    int64_t page = table_visible_rows(table);
    if (page < 1)
        page = 1;
    if (key_code == UI_KEY_UP)
        table->selected_row--;
    else if (key_code == UI_KEY_DOWN)
        table->selected_row++;
    else if (key_code == UI_KEY_HOME)
        table->selected_row = 0;
    else if (key_code == UI_KEY_END)
        table->selected_row = table->row_count - 1;
    else if (key_code == UI_KEY_PAGE_UP)
        table->selected_row -= page;
    else if (key_code == UI_KEY_PAGE_DOWN)
        table->selected_row += page;
    if (table->selected_row < 0)
        table->selected_row = 0;
    if (table->selected_row >= table->row_count)
        table->selected_row = table->row_count - 1;
    if (table->selected_row < table->scroll_offset)
        table->scroll_offset = table->selected_row;
    if (table->selected_row >= table->scroll_offset + page)
        table->scroll_offset = table->selected_row - page + 1;
    table_clamp_scroll(table);
}

void rt_uitable_draw(void *ptr, void *canvas) {
    rt_uitable_impl *table = checked_table(ptr, "UITable.Draw: expected Viper.Game.UI.Table");
    if (!table || !canvas || !ui_validate_canvas(canvas, "UITable.Draw: expected Canvas"))
        return;
    if (!table->visible)
        return;
    int64_t y = table->y;
    if (table->show_header) {
        rt_canvas_box(canvas, table->x, y, table->w, table->header_height, table->header_bg_color);
        int64_t x = table->x;
        for (int64_t c = 0; c < table->column_count; c++) {
            ui_draw_text_basic(canvas, x + 4, y + 4, table->columns[c].title, table->font, 1,
                               table->header_text_color);
            x += table->columns[c].width;
        }
        y += table->header_height;
    }
    int64_t visible_rows = table_visible_rows(table);
    for (int64_t vr = 0; vr < visible_rows; vr++) {
        int64_t row = table->scroll_offset + vr;
        if (row >= table->row_count)
            break;
        int64_t ry = y + vr * table->row_height;
        int64_t bg = row == table->selected_row
                         ? table->selected_bg_color
                         : (table->striped && (row & 1) ? table->row_alt_bg_color : table->row_bg_color);
        rt_canvas_box(canvas, table->x, ry, table->w, table->row_height, bg);
        int64_t x = table->x;
        for (int64_t c = 0; c < table->column_count; c++) {
            ui_draw_text_basic(canvas, x + 4, ry + 4, table->rows[row].cells[c], table->font, 1,
                               table->text_color);
            x += table->columns[c].width;
        }
    }
    if (table->show_borders)
        rt_canvas_frame(canvas, table->x, table->y, table->w, table->h, table->border_color);
}

//=============================================================================
// UISlider
//=============================================================================

typedef struct {
    void *vptr;
    int64_t x, y, w, h;
    int64_t min_value, max_value, current_value, step;
    char label[64];
    int8_t show_value;
    int8_t show_label;
    int64_t track_color, fill_color, thumb_color, text_color;
    int8_t visible, enabled, dragging;
} rt_uislider_impl;

static rt_uislider_impl *checked_slider(void *ptr, const char *api) {
    if (!ptr)
        return NULL;
    if (rt_obj_class_id(ptr) != RT_UISLIDER_CLASS_ID) {
        rt_trap(api);
        return NULL;
    }
    return (rt_uislider_impl *)ptr;
}

static int64_t slider_clamp_value(rt_uislider_impl *s, int64_t value) {
    if (!s)
        return 0;
    if (value < s->min_value)
        value = s->min_value;
    if (value > s->max_value)
        value = s->max_value;
    if (s->step > 1) {
        long double offset = (long double)value - (long double)s->min_value;
        if (offset < 0.0L)
            offset = 0.0L;
        long double steps =
            floorl((offset + (long double)s->step / 2.0L) / (long double)s->step);
        value = ui_ld_to_i64_sat((long double)s->min_value + steps * (long double)s->step);
        if (value > s->max_value)
            value = s->max_value;
        if (value < s->min_value)
            value = s->min_value;
    }
    return value;
}

static int8_t slider_set_from_mouse(rt_uislider_impl *s, int64_t mx) {
    if (!s || s->w <= 1)
        return 0;
    int64_t offset = ui_coord_offset_clamped(s->x, s->w, mx);
    long double t = (long double)offset / (long double)s->w;
    long double range = (long double)s->max_value - (long double)s->min_value;
    int64_t value = ui_ld_to_i64_sat((long double)s->min_value + range * t + 0.5L);
    value = slider_clamp_value(s, value);
    if (value == s->current_value)
        return 0;
    s->current_value = value;
    return 1;
}

void *rt_uislider_new(int64_t x, int64_t y, int64_t w, int64_t h, int64_t min_v, int64_t max_v) {
    rt_uislider_impl *s =
        (rt_uislider_impl *)rt_obj_new_i64(RT_UISLIDER_CLASS_ID, (int64_t)sizeof(*s));
    if (!s)
        return NULL;
    memset(s, 0, sizeof(*s));
    s->x = x;
    s->y = y;
    s->w = ui_clamp_dim(w);
    s->h = ui_clamp_dim(h);
    if (max_v < min_v) {
        int64_t tmp = min_v;
        min_v = max_v;
        max_v = tmp;
    }
    s->min_value = min_v;
    s->max_value = max_v;
    s->current_value = min_v;
    s->step = 1;
    s->show_value = 1;
    s->show_label = 0;
    s->track_color = 0x505050;
    s->fill_color = 0x4A90E2;
    s->thumb_color = 0xFFFFFF;
    s->text_color = 0xFFFFFF;
    s->visible = 1;
    s->enabled = 1;
    return s;
}

void rt_uislider_set_value(void *ptr, int64_t v) {
    rt_uislider_impl *s = checked_slider(ptr, "UISlider.SetValue: expected Viper.Game.UI.Slider");
    if (s)
        s->current_value = slider_clamp_value(s, v);
}

int64_t rt_uislider_get_value(void *ptr) {
    rt_uislider_impl *s = checked_slider(ptr, "UISlider.Value: expected Viper.Game.UI.Slider");
    return s ? s->current_value : 0;
}

void rt_uislider_set_step(void *ptr, int64_t step) {
    rt_uislider_impl *s = checked_slider(ptr, "UISlider.SetStep: expected Viper.Game.UI.Slider");
    if (!s)
        return;
    s->step = step > 0 ? step : 1;
    s->current_value = slider_clamp_value(s, s->current_value);
}

void rt_uislider_set_label(void *ptr, rt_string label) {
    rt_uislider_impl *s = checked_slider(ptr, "UISlider.SetLabel: expected Viper.Game.UI.Slider");
    if (!s)
        return;
    ui_copy_text(s->label, sizeof(s->label), label);
    s->show_label = s->label[0] != '\0';
}

int8_t rt_uislider_handle_key(void *ptr, int64_t key_code) {
    rt_uislider_impl *s = checked_slider(ptr, "UISlider.HandleKey: expected Viper.Game.UI.Slider");
    if (!s || !s->visible || !s->enabled)
        return 0;
    int64_t before = s->current_value;
    if (key_code == UI_KEY_LEFT || key_code == UI_KEY_DOWN)
        s->current_value = slider_clamp_value(s, ui_add_sat_i64(s->current_value, -s->step));
    else if (key_code == UI_KEY_RIGHT || key_code == UI_KEY_UP)
        s->current_value = slider_clamp_value(s, ui_add_sat_i64(s->current_value, s->step));
    else if (key_code == UI_KEY_HOME)
        s->current_value = s->min_value;
    else if (key_code == UI_KEY_END)
        s->current_value = s->max_value;
    return before != s->current_value;
}

int8_t rt_uislider_handle_mouse_down(void *ptr, int64_t mx, int64_t my) {
    rt_uislider_impl *s =
        checked_slider(ptr, "UISlider.HandleMouseDown: expected Viper.Game.UI.Slider");
    if (!s || !s->visible || !s->enabled || !ui_point_inside(s->x, s->y, s->w, s->h, mx, my))
        return 0;
    s->dragging = 1;
    return slider_set_from_mouse(s, mx);
}

int8_t rt_uislider_handle_mouse_drag(void *ptr, int64_t mx) {
    rt_uislider_impl *s =
        checked_slider(ptr, "UISlider.HandleMouseDrag: expected Viper.Game.UI.Slider");
    if (!s || !s->dragging || !s->enabled)
        return 0;
    return slider_set_from_mouse(s, mx);
}

int8_t rt_uislider_handle_mouse_up(void *ptr) {
    rt_uislider_impl *s =
        checked_slider(ptr, "UISlider.HandleMouseUp: expected Viper.Game.UI.Slider");
    if (!s)
        return 0;
    int8_t was = s->dragging;
    s->dragging = 0;
    return was;
}

void rt_uislider_draw(void *ptr, void *canvas) {
    rt_uislider_impl *s = checked_slider(ptr, "UISlider.Draw: expected Viper.Game.UI.Slider");
    if (!s || !canvas || !ui_validate_canvas(canvas, "UISlider.Draw: expected Canvas"))
        return;
    if (!s->visible)
        return;
    int64_t cy = s->y + s->h / 2;
    rt_canvas_box(canvas, s->x, cy - 2, s->w, 4, s->track_color);
    int64_t fill = 0;
    if (s->max_value > s->min_value)
        fill = (int64_t)(((long double)(s->current_value - s->min_value) * (long double)s->w) /
                         (long double)(s->max_value - s->min_value));
    rt_canvas_box(canvas, s->x, cy - 2, fill, 4, s->fill_color);
    rt_canvas_box(canvas, s->x + fill - 4, s->y, 8, s->h, s->thumb_color);
    if (s->show_label)
        ui_draw_text_basic(canvas, s->x, s->y - 12, s->label, NULL, 1, s->text_color);
}

//=============================================================================
// UIDropdown
//=============================================================================

#define RT_UIDROPDOWN_MAX_OPTIONS 32
#define RT_UIDROPDOWN_MAX_TEXT 64

typedef struct {
    void *vptr;
    int64_t x, y, w, h;
    char options[RT_UIDROPDOWN_MAX_OPTIONS][RT_UIDROPDOWN_MAX_TEXT];
    int64_t option_count;
    int64_t selected;
    int8_t open;
    int64_t text_color, bg_color, caret_color, border_color, selected_bg_color;
    void *font;
    int8_t visible, enabled;
} rt_uidropdown_impl;

static rt_uidropdown_impl *checked_dropdown(void *ptr, const char *api) {
    if (!ptr)
        return NULL;
    if (rt_obj_class_id(ptr) != RT_UIDROPDOWN_CLASS_ID) {
        rt_trap(api);
        return NULL;
    }
    return (rt_uidropdown_impl *)ptr;
}

static void uidropdown_finalizer(void *obj) {
    rt_uidropdown_impl *dd = (rt_uidropdown_impl *)obj;
    if (!dd)
        return;
    ui_release_obj(dd->font);
    dd->font = NULL;
}

void *rt_uidropdown_new(int64_t x, int64_t y, int64_t w, int64_t h) {
    rt_uidropdown_impl *dd =
        (rt_uidropdown_impl *)rt_obj_new_i64(RT_UIDROPDOWN_CLASS_ID, (int64_t)sizeof(*dd));
    if (!dd)
        return NULL;
    memset(dd, 0, sizeof(*dd));
    dd->x = x;
    dd->y = y;
    dd->w = ui_clamp_dim(w);
    dd->h = ui_clamp_dim(h);
    dd->selected = -1;
    dd->text_color = 0xFFFFFF;
    dd->bg_color = 0x202020;
    dd->caret_color = 0xFFFFFF;
    dd->border_color = 0x606060;
    dd->selected_bg_color = 0x304A70;
    dd->visible = 1;
    dd->enabled = 1;
    rt_obj_set_finalizer(dd, uidropdown_finalizer);
    return dd;
}

void rt_uidropdown_add_option(void *ptr, rt_string text) {
    rt_uidropdown_impl *dd =
        checked_dropdown(ptr, "UIDropdown.AddOption: expected Viper.Game.UI.Dropdown");
    if (!dd)
        return;
    if (dd->option_count >= RT_UIDROPDOWN_MAX_OPTIONS) {
        rt_trap("UIDropdown.AddOption: option limit exceeded");
        return;
    }
    ui_copy_text(dd->options[dd->option_count], sizeof(dd->options[dd->option_count]), text);
    if (dd->selected < 0)
        dd->selected = 0;
    dd->option_count++;
}

void rt_uidropdown_clear_options(void *ptr) {
    rt_uidropdown_impl *dd =
        checked_dropdown(ptr, "UIDropdown.ClearOptions: expected Viper.Game.UI.Dropdown");
    if (!dd)
        return;
    memset(dd->options, 0, sizeof(dd->options));
    dd->option_count = 0;
    dd->selected = -1;
    dd->open = 0;
}

int64_t rt_uidropdown_get_selected(void *ptr) {
    rt_uidropdown_impl *dd =
        checked_dropdown(ptr, "UIDropdown.Selected: expected Viper.Game.UI.Dropdown");
    return dd ? dd->selected : -1;
}

void rt_uidropdown_set_selected(void *ptr, int64_t index) {
    rt_uidropdown_impl *dd =
        checked_dropdown(ptr, "UIDropdown.SetSelected: expected Viper.Game.UI.Dropdown");
    if (!dd)
        return;
    dd->selected = index < 0 || index >= dd->option_count ? -1 : index;
}

rt_string rt_uidropdown_get_selected_text(void *ptr) {
    rt_uidropdown_impl *dd =
        checked_dropdown(ptr, "UIDropdown.SelectedText: expected Viper.Game.UI.Dropdown");
    if (!dd || dd->selected < 0 || dd->selected >= dd->option_count)
        return rt_str_empty();
    return rt_const_cstr(dd->options[dd->selected]);
}

int8_t rt_uidropdown_is_open(void *ptr) {
    rt_uidropdown_impl *dd =
        checked_dropdown(ptr, "UIDropdown.IsOpen: expected Viper.Game.UI.Dropdown");
    return dd ? dd->open : 0;
}

void rt_uidropdown_open(void *ptr) {
    rt_uidropdown_impl *dd =
        checked_dropdown(ptr, "UIDropdown.Open: expected Viper.Game.UI.Dropdown");
    if (dd && dd->enabled && dd->visible)
        dd->open = 1;
}

void rt_uidropdown_close(void *ptr) {
    rt_uidropdown_impl *dd =
        checked_dropdown(ptr, "UIDropdown.Close: expected Viper.Game.UI.Dropdown");
    if (dd)
        dd->open = 0;
}

int8_t rt_uidropdown_handle_click(void *ptr, int64_t mx, int64_t my) {
    rt_uidropdown_impl *dd =
        checked_dropdown(ptr, "UIDropdown.HandleClick: expected Viper.Game.UI.Dropdown");
    if (!dd || !dd->enabled || !dd->visible)
        return 0;
    if (ui_point_inside(dd->x, dd->y, dd->w, dd->h, mx, my)) {
        dd->open = !dd->open;
        return 1;
    }
    if (dd->open && ui_point_inside(dd->x, dd->y + dd->h, dd->w, dd->h * dd->option_count, mx, my)) {
        int64_t idx = (my - (dd->y + dd->h)) / dd->h;
        if (idx >= 0 && idx < dd->option_count)
            dd->selected = idx;
        dd->open = 0;
        return 1;
    }
    dd->open = 0;
    return 0;
}

int8_t rt_uidropdown_handle_key(void *ptr, int64_t key_code) {
    rt_uidropdown_impl *dd =
        checked_dropdown(ptr, "UIDropdown.HandleKey: expected Viper.Game.UI.Dropdown");
    if (!dd || !dd->enabled || !dd->visible || dd->option_count <= 0)
        return 0;
    if (key_code == UI_KEY_ESCAPE) {
        dd->open = 0;
        return 1;
    }
    if (key_code == UI_KEY_ENTER) {
        dd->open = !dd->open;
        return 1;
    }
    if (key_code == UI_KEY_UP) {
        dd->selected = dd->selected <= 0 ? dd->option_count - 1 : dd->selected - 1;
        return 1;
    }
    if (key_code == UI_KEY_DOWN) {
        dd->selected = dd->selected >= dd->option_count - 1 ? 0 : dd->selected + 1;
        return 1;
    }
    return 0;
}

void rt_uidropdown_draw(void *ptr, void *canvas) {
    rt_uidropdown_impl *dd = checked_dropdown(ptr, "UIDropdown.Draw: expected Viper.Game.UI.Dropdown");
    if (!dd || !canvas || !ui_validate_canvas(canvas, "UIDropdown.Draw: expected Canvas"))
        return;
    if (!dd->visible)
        return;
    rt_canvas_box(canvas, dd->x, dd->y, dd->w, dd->h, dd->bg_color);
    rt_canvas_frame(canvas, dd->x, dd->y, dd->w, dd->h, dd->border_color);
    if (dd->selected >= 0 && dd->selected < dd->option_count)
        ui_draw_text_basic(canvas, dd->x + 4, dd->y + 4, dd->options[dd->selected], dd->font, 1,
                           dd->text_color);
    rt_canvas_line(canvas, dd->x + dd->w - 14, dd->y + dd->h / 2 - 2, dd->x + dd->w - 8,
                   dd->y + dd->h / 2 + 4, dd->caret_color);
    rt_canvas_line(canvas, dd->x + dd->w - 8, dd->y + dd->h / 2 + 4, dd->x + dd->w - 2,
                   dd->y + dd->h / 2 - 2, dd->caret_color);
    if (dd->open) {
        for (int64_t i = 0; i < dd->option_count; i++) {
            int64_t y = dd->y + dd->h * (i + 1);
            rt_canvas_box(canvas, dd->x, y, dd->w, dd->h,
                          i == dd->selected ? dd->selected_bg_color : dd->bg_color);
            rt_canvas_frame(canvas, dd->x, y, dd->w, dd->h, dd->border_color);
            ui_draw_text_basic(canvas, dd->x + 4, y + 4, dd->options[i], dd->font, 1,
                               dd->text_color);
        }
    }
}

//=============================================================================
// UITooltip
//=============================================================================

typedef struct {
    void *vptr;
    int64_t x, y;
    char text[256];
    int64_t bg_color, text_color, border_color;
    int64_t padding;
    int8_t visible;
    void *font;
    int64_t hover_delay_ms, hover_elapsed_ms;
    int64_t target_x, target_y;
    int8_t hovered;
} rt_uitooltip_impl;

static rt_uitooltip_impl *checked_tooltip(void *ptr, const char *api) {
    if (!ptr)
        return NULL;
    if (rt_obj_class_id(ptr) != RT_UITOOLTIP_CLASS_ID) {
        rt_trap(api);
        return NULL;
    }
    return (rt_uitooltip_impl *)ptr;
}

static void uitooltip_finalizer(void *obj) {
    rt_uitooltip_impl *t = (rt_uitooltip_impl *)obj;
    if (!t)
        return;
    ui_release_obj(t->font);
    t->font = NULL;
}

void *rt_uitooltip_new(void) {
    rt_uitooltip_impl *t =
        (rt_uitooltip_impl *)rt_obj_new_i64(RT_UITOOLTIP_CLASS_ID, (int64_t)sizeof(*t));
    if (!t)
        return NULL;
    memset(t, 0, sizeof(*t));
    t->bg_color = 0x202020;
    t->text_color = 0xFFFFFF;
    t->border_color = 0x606060;
    t->padding = 6;
    t->hover_delay_ms = 500;
    rt_obj_set_finalizer(t, uitooltip_finalizer);
    return t;
}

void rt_uitooltip_set_text(void *ptr, rt_string text) {
    rt_uitooltip_impl *t =
        checked_tooltip(ptr, "UITooltip.SetText: expected Viper.Game.UI.Tooltip");
    if (t)
        ui_copy_text(t->text, sizeof(t->text), text);
}

void rt_uitooltip_set_hover_delay_ms(void *ptr, int64_t ms) {
    rt_uitooltip_impl *t =
        checked_tooltip(ptr, "UITooltip.SetHoverDelayMs: expected Viper.Game.UI.Tooltip");
    if (t)
        t->hover_delay_ms = ms < 0 ? 0 : ms;
}

void rt_uitooltip_update(void *ptr, int64_t mx, int64_t my, int8_t hovered_target, int64_t delta_ms) {
    rt_uitooltip_impl *t =
        checked_tooltip(ptr, "UITooltip.Update: expected Viper.Game.UI.Tooltip");
    if (!t)
        return;
    t->target_x = mx;
    t->target_y = my;
    t->x = mx + 14;
    t->y = my + 18;
    if (!hovered_target) {
        t->hovered = 0;
        t->hover_elapsed_ms = 0;
        t->visible = 0;
        return;
    }
    t->hovered = 1;
    if (delta_ms > 0)
        t->hover_elapsed_ms = ui_add_sat_i64(t->hover_elapsed_ms, delta_ms);
    t->visible = t->hover_elapsed_ms >= t->hover_delay_ms;
}

void rt_uitooltip_draw(void *ptr, void *canvas) {
    rt_uitooltip_impl *t = checked_tooltip(ptr, "UITooltip.Draw: expected Viper.Game.UI.Tooltip");
    if (!t || !canvas || !ui_validate_canvas(canvas, "UITooltip.Draw: expected Canvas"))
        return;
    if (!t->visible || t->text[0] == '\0')
        return;
    int64_t w = ui_text_prefix_width(t->text, (int64_t)strlen(t->text), t->font, 1) + t->padding * 2;
    int64_t h = 8 + t->padding * 2;
    int64_t cw = rt_canvas_width(canvas);
    int64_t ch = rt_canvas_height(canvas);
    int64_t x = t->x;
    int64_t y = t->y;
    if (x + w > cw)
        x = cw - w;
    if (y + h > ch)
        y = ch - h;
    if (x < 0)
        x = 0;
    if (y < 0)
        y = 0;
    rt_canvas_box(canvas, x, y, w, h, t->bg_color);
    rt_canvas_frame(canvas, x, y, w, h, t->border_color);
    ui_draw_text_basic(canvas, x + t->padding, y + t->padding, t->text, t->font, 1, t->text_color);
}

//=============================================================================
// UIModal
//=============================================================================

#define RT_UIMODAL_MAX_CHILDREN 16
#define RT_UIMODAL_MAX_BUTTONS 4

typedef struct {
    char text[64];
    int64_t return_value;
    int8_t is_default;
    int8_t is_cancel;
} rt_uimodal_button_t;

typedef struct {
    void *vptr;
    int64_t x, y, w, h;
    char title[128];
    char content_text[512];
    void *children[RT_UIMODAL_MAX_CHILDREN];
    int64_t child_count;
    rt_uimodal_button_t buttons[RT_UIMODAL_MAX_BUTTONS];
    int64_t button_count;
    int64_t selected_button;
    int64_t result;
    int8_t open;
    int8_t visible;
    int8_t modal;
    int64_t bg_color;
    int64_t title_bar_color;
    int64_t title_text_color;
    int64_t content_text_color;
    int64_t overlay_color;
    int64_t overlay_alpha;
    void *font;
} rt_uimodal_impl;

static rt_uimodal_impl *checked_modal(void *ptr, const char *api) {
    if (!ptr)
        return NULL;
    if (rt_obj_class_id(ptr) != RT_UIMODAL_CLASS_ID) {
        rt_trap(api);
        return NULL;
    }
    return (rt_uimodal_impl *)ptr;
}

static void uimodal_finalizer(void *obj) {
    rt_uimodal_impl *m = (rt_uimodal_impl *)obj;
    if (!m)
        return;
    for (int64_t i = 0; i < m->child_count; i++)
        ui_release_obj(m->children[i]);
    ui_release_obj(m->font);
    m->font = NULL;
    m->child_count = 0;
}

static void modal_init(rt_uimodal_impl *m, int64_t x, int64_t y, int64_t w, int64_t h) {
    memset(m, 0, sizeof(*m));
    m->x = x;
    m->y = y;
    m->w = ui_clamp_dim(w);
    m->h = ui_clamp_dim(h);
    m->selected_button = -1;
    m->result = -1;
    m->visible = 1;
    m->modal = 1;
    m->bg_color = 0x202020;
    m->title_bar_color = 0x303030;
    m->title_text_color = 0xFFFFFF;
    m->content_text_color = 0xDDDDDD;
    m->overlay_color = 0x000000;
    m->overlay_alpha = 128;
}

void *rt_uimodal_new(int64_t w, int64_t h) {
    return rt_uimodal_new_at(0, 0, w, h);
}

void *rt_uimodal_new_at(int64_t x, int64_t y, int64_t w, int64_t h) {
    rt_uimodal_impl *m =
        (rt_uimodal_impl *)rt_obj_new_i64(RT_UIMODAL_CLASS_ID, (int64_t)sizeof(*m));
    if (!m)
        return NULL;
    modal_init(m, x, y, w, h);
    rt_obj_set_finalizer(m, uimodal_finalizer);
    return m;
}

void rt_uimodal_set_title(void *ptr, rt_string title) {
    rt_uimodal_impl *m = checked_modal(ptr, "UIModal.SetTitle: expected Viper.Game.UI.Modal");
    if (m)
        ui_copy_text(m->title, sizeof(m->title), title);
}

void rt_uimodal_set_content(void *ptr, rt_string text) {
    rt_uimodal_impl *m = checked_modal(ptr, "UIModal.SetContent: expected Viper.Game.UI.Modal");
    if (m)
        ui_copy_text(m->content_text, sizeof(m->content_text), text);
}

int64_t rt_uimodal_add_button(void *ptr, rt_string text, int64_t return_value) {
    rt_uimodal_impl *m = checked_modal(ptr, "UIModal.AddButton: expected Viper.Game.UI.Modal");
    if (!m)
        return -1;
    if (m->button_count >= RT_UIMODAL_MAX_BUTTONS) {
        rt_trap("UIModal.AddButton: button limit exceeded");
        return -1;
    }
    int64_t idx = m->button_count++;
    ui_copy_text(m->buttons[idx].text, sizeof(m->buttons[idx].text), text);
    m->buttons[idx].return_value = return_value;
    if (m->selected_button < 0)
        m->selected_button = idx;
    return idx;
}

void rt_uimodal_set_default_button(void *ptr, int64_t index) {
    rt_uimodal_impl *m =
        checked_modal(ptr, "UIModal.SetDefaultButton: expected Viper.Game.UI.Modal");
    if (!m || index < 0 || index >= m->button_count)
        return;
    for (int64_t i = 0; i < m->button_count; i++)
        m->buttons[i].is_default = 0;
    m->buttons[index].is_default = 1;
    m->selected_button = index;
}

void rt_uimodal_set_cancel_button(void *ptr, int64_t index) {
    rt_uimodal_impl *m =
        checked_modal(ptr, "UIModal.SetCancelButton: expected Viper.Game.UI.Modal");
    if (!m || index < 0 || index >= m->button_count)
        return;
    for (int64_t i = 0; i < m->button_count; i++)
        m->buttons[i].is_cancel = 0;
    m->buttons[index].is_cancel = 1;
}

void rt_uimodal_add_child(void *ptr, void *child_widget) {
    rt_uimodal_impl *m = checked_modal(ptr, "UIModal.AddChild: expected Viper.Game.UI.Modal");
    if (!m || !child_widget)
        return;
    if (m->child_count >= RT_UIMODAL_MAX_CHILDREN) {
        rt_trap("UIModal.AddChild: child limit exceeded");
        return;
    }
    rt_obj_retain_maybe(child_widget);
    m->children[m->child_count++] = child_widget;
}

void rt_uimodal_open(void *ptr) {
    rt_uimodal_impl *m = checked_modal(ptr, "UIModal.Open: expected Viper.Game.UI.Modal");
    if (!m)
        return;
    m->open = 1;
    m->visible = 1;
    m->result = -1;
}

void rt_uimodal_close(void *ptr) {
    rt_uimodal_impl *m = checked_modal(ptr, "UIModal.Close: expected Viper.Game.UI.Modal");
    if (m)
        m->open = 0;
}

int8_t rt_uimodal_is_open(void *ptr) {
    rt_uimodal_impl *m = checked_modal(ptr, "UIModal.IsOpen: expected Viper.Game.UI.Modal");
    return m ? m->open : 0;
}

int64_t rt_uimodal_get_result(void *ptr) {
    rt_uimodal_impl *m = checked_modal(ptr, "UIModal.Result: expected Viper.Game.UI.Modal");
    return m ? m->result : -1;
}

static int64_t modal_trigger_button(rt_uimodal_impl *m, int64_t index) {
    if (!m || index < 0 || index >= m->button_count)
        return -1;
    m->result = m->buttons[index].return_value;
    m->open = 0;
    m->visible = 0;
    return m->result;
}

int64_t rt_uimodal_handle_key(void *ptr, int64_t key_code, int8_t shift_held) {
    rt_uimodal_impl *m = checked_modal(ptr, "UIModal.HandleKey: expected Viper.Game.UI.Modal");
    if (!m || !m->open)
        return -1;
    if (key_code == UI_KEY_TAB && m->button_count > 0) {
        if (m->selected_button < 0)
            m->selected_button = 0;
        else if (shift_held)
            m->selected_button = m->selected_button <= 0 ? m->button_count - 1 : m->selected_button - 1;
        else
            m->selected_button = (m->selected_button + 1) % m->button_count;
        return -1;
    }
    if (key_code == UI_KEY_ENTER) {
        int64_t target = m->selected_button;
        if (target < 0) {
            for (int64_t i = 0; i < m->button_count; i++) {
                if (m->buttons[i].is_default) {
                    target = i;
                    break;
                }
            }
        }
        return modal_trigger_button(m, target);
    }
    if (key_code == UI_KEY_ESCAPE) {
        for (int64_t i = 0; i < m->button_count; i++) {
            if (m->buttons[i].is_cancel)
                return modal_trigger_button(m, i);
        }
    }
    for (int64_t i = 0; i < m->child_count; i++) {
        if (m->children[i] && rt_obj_class_id(m->children[i]) == RT_UITEXTINPUT_CLASS_ID)
            rt_uitextinput_handle_key(m->children[i], key_code, shift_held);
    }
    return -1;
}

int64_t rt_uimodal_handle_click(void *ptr, int64_t mx, int64_t my) {
    rt_uimodal_impl *m = checked_modal(ptr, "UIModal.HandleClick: expected Viper.Game.UI.Modal");
    if (!m || !m->open)
        return -1;
    int64_t button_w = 88;
    int64_t button_h = 24;
    int64_t gap = 8;
    int64_t total_w = m->button_count * button_w + (m->button_count > 0 ? (m->button_count - 1) * gap : 0);
    int64_t bx = m->x + m->w - total_w - 12;
    int64_t by = m->y + m->h - button_h - 12;
    for (int64_t i = 0; i < m->button_count; i++) {
        int64_t x = bx + i * (button_w + gap);
        if (ui_point_inside(x, by, button_w, button_h, mx, my))
            return modal_trigger_button(m, i);
    }
    for (int64_t i = 0; i < m->child_count; i++) {
        if (m->children[i] && rt_obj_class_id(m->children[i]) == RT_UITEXTINPUT_CLASS_ID)
            rt_uitextinput_handle_mouse_click(m->children[i], mx, my, 0);
    }
    return -1;
}

void rt_uimodal_draw(void *ptr, void *canvas) {
    rt_uimodal_impl *m = checked_modal(ptr, "UIModal.Draw: expected Viper.Game.UI.Modal");
    if (!m || !canvas || !ui_validate_canvas(canvas, "UIModal.Draw: expected Canvas"))
        return;
    if (!m->open || !m->visible)
        return;
    rt_canvas_box_alpha(canvas, 0, 0, rt_canvas_width(canvas), rt_canvas_height(canvas), m->overlay_color,
                        m->overlay_alpha);
    rt_canvas_box(canvas, m->x, m->y, m->w, m->h, m->bg_color);
    rt_canvas_frame(canvas, m->x, m->y, m->w, m->h, 0x707070);
    rt_canvas_box(canvas, m->x, m->y, m->w, 28, m->title_bar_color);
    ui_draw_text_basic(canvas, m->x + 8, m->y + 8, m->title, m->font, 1, m->title_text_color);
    ui_draw_text_basic(canvas, m->x + 12, m->y + 40, m->content_text, m->font, 1,
                       m->content_text_color);
    for (int64_t i = 0; i < m->child_count; i++) {
        void *child = m->children[i];
        if (!child)
            continue;
        int64_t cid = rt_obj_class_id(child);
        if (cid == RT_UITEXTINPUT_CLASS_ID)
            rt_uitextinput_draw(child, canvas);
        else if (cid == RT_UISLIDER_CLASS_ID)
            rt_uislider_draw(child, canvas);
        else if (cid == RT_UIDROPDOWN_CLASS_ID)
            rt_uidropdown_draw(child, canvas);
        else if (cid == RT_UITABLE_CLASS_ID)
            rt_uitable_draw(child, canvas);
        else if (cid == RT_UIMENULIST_CLASS_ID)
            rt_uimenulist_draw(child, canvas);
    }
    int64_t button_w = 88;
    int64_t button_h = 24;
    int64_t gap = 8;
    int64_t total_w = m->button_count * button_w + (m->button_count > 0 ? (m->button_count - 1) * gap : 0);
    int64_t bx = m->x + m->w - total_w - 12;
    int64_t by = m->y + m->h - button_h - 12;
    for (int64_t i = 0; i < m->button_count; i++) {
        int64_t x = bx + i * (button_w + gap);
        rt_canvas_box(canvas, x, by, button_w, button_h,
                      i == m->selected_button ? 0x405A86 : 0x303030);
        rt_canvas_frame(canvas, x, by, button_w, button_h, 0x808080);
        ui_draw_text_basic(canvas, x + 8, by + 8, m->buttons[i].text, m->font, 1, 0xFFFFFF);
    }
}
