//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/game/rt_gameui.c
// Purpose: Lightweight in-game UI widgets that draw directly to a Canvas.
//   Provides Label, Bar, Panel, NineSlice, and MenuList — the building blocks
//   for game HUDs, menus, and overlays without the overhead of the desktop
//   ViperGUI widget system.
//
// Key invariants:
//   - All widgets are GC-managed via rt_obj_new_i64 (no manual free).
//   - Draw functions are no-ops when canvas or widget is NULL.
//   - UIBar values are clamped: value in [0, max], max >= 1.
//   - UIMenuList selection wraps: MoveUp at 0 → last item, MoveDown at last → 0.
//   - UILabel falls back to built-in 8x8 font when font is NULL.
//
// Ownership/Lifetime:
//   - Widgets are GC-managed.
//   - UILabel text is stored as rt_string (GC-managed).
//   - UIMenuList item strings are strdup'd copies; freed in clear/destroy.
//   - UINineSlice keeps a raw pointer to its Pixels source.
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

#include <stdlib.h>
#include <string.h>

//=============================================================================
// UILabel
//=============================================================================

/// Maximum label text length (bytes).
#define LABEL_MAX_TEXT 512

typedef struct {
    int64_t x, y;
    char text[LABEL_MAX_TEXT];
    int64_t color;
    void *font;    ///< BitmapFont or NULL for default
    int64_t scale; ///< Integer scale (1 = normal)
    int8_t visible;
} rt_uilabel_impl;

void *rt_uilabel_new(int64_t x, int64_t y, rt_string text, int64_t color) {
    rt_uilabel_impl *label = (rt_uilabel_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_uilabel_impl));
    if (!label)
        return NULL;

    memset(label, 0, sizeof(rt_uilabel_impl));
    label->x = x;
    label->y = y;
    label->color = color;
    label->scale = 1;
    label->visible = 1;

    if (text) {
        const char *s = rt_string_cstr(text);
        if (s) {
            size_t len = strlen(s);
            if (len >= LABEL_MAX_TEXT)
                len = LABEL_MAX_TEXT - 1;
            memcpy(label->text, s, len);
            label->text[len] = '\0';
        }
    }

    return label;
}

/// @brief Perform uilabel set text operation.
/// @param ptr
/// @param text
void rt_uilabel_set_text(void *ptr, rt_string text) {
    if (!ptr)
        return;
    rt_uilabel_impl *label = (rt_uilabel_impl *)ptr;
    label->text[0] = '\0';
    if (text) {
        const char *s = rt_string_cstr(text);
        if (s) {
            size_t len = strlen(s);
            if (len >= LABEL_MAX_TEXT)
                len = LABEL_MAX_TEXT - 1;
            memcpy(label->text, s, len);
            label->text[len] = '\0';
        }
    }
}

/// @brief Perform uilabel set pos operation.
/// @param ptr
/// @param x
/// @param y
void rt_uilabel_set_pos(void *ptr, int64_t x, int64_t y) {
    if (!ptr)
        return;
    rt_uilabel_impl *label = (rt_uilabel_impl *)ptr;
    label->x = x;
    label->y = y;
}

/// @brief Perform uilabel set color operation.
/// @param ptr
/// @param color
void rt_uilabel_set_color(void *ptr, int64_t color) {
    if (!ptr)
        return;
    ((rt_uilabel_impl *)ptr)->color = color;
}

/// @brief Perform uilabel set font operation.
/// @param ptr
/// @param font
void rt_uilabel_set_font(void *ptr, void *font) {
    if (!ptr)
        return;
    ((rt_uilabel_impl *)ptr)->font = font;
}

/// @brief Perform uilabel set scale operation.
/// @param ptr
/// @param scale
void rt_uilabel_set_scale(void *ptr, int64_t scale) {
    if (!ptr)
        return;
    if (scale < 1)
        scale = 1;
    ((rt_uilabel_impl *)ptr)->scale = scale;
}

/// @brief Perform uilabel set visible operation.
/// @param ptr
/// @param visible
void rt_uilabel_set_visible(void *ptr, int8_t visible) {
    if (!ptr)
        return;
    ((rt_uilabel_impl *)ptr)->visible = visible ? 1 : 0;
}

/// @brief Perform uilabel get x operation.
/// @param ptr
/// @return Result value.
int64_t rt_uilabel_get_x(void *ptr) {
    if (!ptr)
        return 0;
    return ((rt_uilabel_impl *)ptr)->x;
}

/// @brief Perform uilabel get y operation.
/// @param ptr
/// @return Result value.
int64_t rt_uilabel_get_y(void *ptr) {
    if (!ptr)
        return 0;
    return ((rt_uilabel_impl *)ptr)->y;
}

/// @brief Perform uilabel draw operation.
/// @param ptr
/// @param canvas
void rt_uilabel_draw(void *ptr, void *canvas) {
    if (!ptr || !canvas)
        return;
    rt_uilabel_impl *label = (rt_uilabel_impl *)ptr;
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
    int64_t x, y, w, h;
    int64_t value, max_value;
    int64_t fg_color, bg_color;
    int64_t border_color; ///< 0 = no border
    int64_t direction;
    int8_t visible;
} rt_uibar_impl;

void *rt_uibar_new(int64_t x, int64_t y, int64_t w, int64_t h, int64_t fg_color, int64_t bg_color) {
    rt_uibar_impl *bar = (rt_uibar_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_uibar_impl));
    if (!bar)
        return NULL;

    bar->x = x;
    bar->y = y;
    bar->w = w > 0 ? w : 1;
    bar->h = h > 0 ? h : 1;
    bar->value = 0;
    bar->max_value = 100;
    bar->fg_color = fg_color;
    bar->bg_color = bg_color;
    bar->border_color = 0;
    bar->direction = BAR_DIR_LEFT_TO_RIGHT;
    bar->visible = 1;

    return bar;
}

/// @brief Perform uibar set value operation.
/// @param ptr
/// @param value
/// @param max_value
void rt_uibar_set_value(void *ptr, int64_t value, int64_t max_value) {
    if (!ptr)
        return;
    rt_uibar_impl *bar = (rt_uibar_impl *)ptr;
    if (max_value < 1)
        max_value = 1;
    if (value < 0)
        value = 0;
    if (value > max_value)
        value = max_value;
    bar->value = value;
    bar->max_value = max_value;
}

/// @brief Perform uibar set pos operation.
/// @param ptr
/// @param x
/// @param y
void rt_uibar_set_pos(void *ptr, int64_t x, int64_t y) {
    if (!ptr)
        return;
    rt_uibar_impl *bar = (rt_uibar_impl *)ptr;
    bar->x = x;
    bar->y = y;
}

/// @brief Perform uibar set size operation.
/// @param ptr
/// @param w
/// @param h
void rt_uibar_set_size(void *ptr, int64_t w, int64_t h) {
    if (!ptr)
        return;
    rt_uibar_impl *bar = (rt_uibar_impl *)ptr;
    bar->w = w > 0 ? w : 1;
    bar->h = h > 0 ? h : 1;
}

/// @brief Perform uibar set colors operation.
/// @param ptr
/// @param fg
/// @param bg
void rt_uibar_set_colors(void *ptr, int64_t fg, int64_t bg) {
    if (!ptr)
        return;
    rt_uibar_impl *bar = (rt_uibar_impl *)ptr;
    bar->fg_color = fg;
    bar->bg_color = bg;
}

/// @brief Perform uibar set border operation.
/// @param ptr
/// @param color
void rt_uibar_set_border(void *ptr, int64_t color) {
    if (!ptr)
        return;
    ((rt_uibar_impl *)ptr)->border_color = color;
}

/// @brief Perform uibar set direction operation.
/// @param ptr
/// @param dir
void rt_uibar_set_direction(void *ptr, int64_t dir) {
    if (!ptr)
        return;
    if (dir < 0 || dir > 3)
        dir = BAR_DIR_LEFT_TO_RIGHT;
    ((rt_uibar_impl *)ptr)->direction = dir;
}

/// @brief Perform uibar set visible operation.
/// @param ptr
/// @param visible
void rt_uibar_set_visible(void *ptr, int8_t visible) {
    if (!ptr)
        return;
    ((rt_uibar_impl *)ptr)->visible = visible ? 1 : 0;
}

/// @brief Perform uibar get value operation.
/// @param ptr
/// @return Result value.
int64_t rt_uibar_get_value(void *ptr) {
    if (!ptr)
        return 0;
    return ((rt_uibar_impl *)ptr)->value;
}

/// @brief Perform uibar get max operation.
/// @param ptr
/// @return Result value.
int64_t rt_uibar_get_max(void *ptr) {
    if (!ptr)
        return 0;
    return ((rt_uibar_impl *)ptr)->max_value;
}

/// @brief Perform uibar draw operation.
/// @param ptr
/// @param canvas
void rt_uibar_draw(void *ptr, void *canvas) {
    if (!ptr || !canvas)
        return;
    rt_uibar_impl *bar = (rt_uibar_impl *)ptr;
    if (!bar->visible)
        return;

    // Background
    rt_canvas_box(canvas, bar->x, bar->y, bar->w, bar->h, bar->bg_color);

    // Filled portion
    if (bar->value > 0 && bar->max_value > 0) {
        int64_t fill;
        switch (bar->direction) {
            case BAR_DIR_RIGHT_TO_LEFT:
                fill = bar->value * bar->w / bar->max_value;
                rt_canvas_box(canvas, bar->x + bar->w - fill, bar->y, fill, bar->h, bar->fg_color);
                break;
            case BAR_DIR_TOP_TO_BOTTOM:
                fill = bar->value * bar->h / bar->max_value;
                rt_canvas_box(canvas, bar->x, bar->y, bar->w, fill, bar->fg_color);
                break;
            case BAR_DIR_BOTTOM_TO_TOP:
                fill = bar->value * bar->h / bar->max_value;
                rt_canvas_box(canvas, bar->x, bar->y + bar->h - fill, bar->w, fill, bar->fg_color);
                break;
            default: // LEFT_TO_RIGHT
                fill = bar->value * bar->w / bar->max_value;
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
    int64_t x, y, w, h;
    int64_t bg_color;
    int64_t alpha;        ///< 0-255
    int64_t border_color; ///< 0 = no border
    int64_t border_thickness;
    int64_t corner_radius; ///< 0 = sharp corners
    int8_t visible;
} rt_uipanel_impl;

void *rt_uipanel_new(int64_t x, int64_t y, int64_t w, int64_t h, int64_t bg_color, int64_t alpha) {
    rt_uipanel_impl *panel = (rt_uipanel_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_uipanel_impl));
    if (!panel)
        return NULL;

    panel->x = x;
    panel->y = y;
    panel->w = w > 0 ? w : 1;
    panel->h = h > 0 ? h : 1;
    panel->bg_color = bg_color;
    panel->alpha = alpha < 0 ? 0 : (alpha > 255 ? 255 : alpha);
    panel->border_color = 0;
    panel->border_thickness = 1;
    panel->corner_radius = 0;
    panel->visible = 1;

    return panel;
}

/// @brief Perform uipanel set pos operation.
/// @param ptr
/// @param x
/// @param y
void rt_uipanel_set_pos(void *ptr, int64_t x, int64_t y) {
    if (!ptr)
        return;
    rt_uipanel_impl *panel = (rt_uipanel_impl *)ptr;
    panel->x = x;
    panel->y = y;
}

/// @brief Perform uipanel set size operation.
/// @param ptr
/// @param w
/// @param h
void rt_uipanel_set_size(void *ptr, int64_t w, int64_t h) {
    if (!ptr)
        return;
    rt_uipanel_impl *panel = (rt_uipanel_impl *)ptr;
    panel->w = w > 0 ? w : 1;
    panel->h = h > 0 ? h : 1;
}

/// @brief Perform uipanel set color operation.
/// @param ptr
/// @param bg_color
/// @param alpha
void rt_uipanel_set_color(void *ptr, int64_t bg_color, int64_t alpha) {
    if (!ptr)
        return;
    rt_uipanel_impl *panel = (rt_uipanel_impl *)ptr;
    panel->bg_color = bg_color;
    panel->alpha = alpha < 0 ? 0 : (alpha > 255 ? 255 : alpha);
}

/// @brief Perform uipanel set border operation.
/// @param ptr
/// @param color
/// @param thickness
void rt_uipanel_set_border(void *ptr, int64_t color, int64_t thickness) {
    if (!ptr)
        return;
    rt_uipanel_impl *panel = (rt_uipanel_impl *)ptr;
    panel->border_color = color;
    panel->border_thickness = thickness > 0 ? thickness : 1;
}

/// @brief Perform uipanel set corner radius operation.
/// @param ptr
/// @param radius
void rt_uipanel_set_corner_radius(void *ptr, int64_t radius) {
    if (!ptr)
        return;
    ((rt_uipanel_impl *)ptr)->corner_radius = radius < 0 ? 0 : radius;
}

/// @brief Perform uipanel set visible operation.
/// @param ptr
/// @param visible
void rt_uipanel_set_visible(void *ptr, int8_t visible) {
    if (!ptr)
        return;
    ((rt_uipanel_impl *)ptr)->visible = visible ? 1 : 0;
}

/// @brief Perform uipanel draw operation.
/// @param ptr
/// @param canvas
void rt_uipanel_draw(void *ptr, void *canvas) {
    if (!ptr || !canvas)
        return;
    rt_uipanel_impl *panel = (rt_uipanel_impl *)ptr;
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
        rt_canvas_box_alpha(
            canvas, panel->x, panel->y, panel->w, panel->h, panel->bg_color, panel->alpha);
    }

    // Border
    if (panel->border_color != 0) {
        for (int64_t t = 0; t < panel->border_thickness; t++) {
            if (panel->corner_radius > 0)
                rt_canvas_round_frame(canvas,
                                      panel->x + t,
                                      panel->y + t,
                                      panel->w - t * 2,
                                      panel->h - t * 2,
                                      panel->corner_radius,
                                      panel->border_color);
            else
                rt_canvas_frame(canvas,
                                panel->x + t,
                                panel->y + t,
                                panel->w - t * 2,
                                panel->h - t * 2,
                                panel->border_color);
        }
    }
}

//=============================================================================
// UINineSlice
//=============================================================================

typedef struct {
    void *pixels;   ///< Source Pixels handle (not owned — caller keeps alive)
    int64_t left;   ///< Left margin (corner width)
    int64_t top;    ///< Top margin (corner height)
    int64_t right;  ///< Right margin
    int64_t bottom; ///< Bottom margin
    int64_t tint;   ///< Tint color (0 = no tint)
} rt_uinineslice_impl;

void *rt_uinineslice_new(void *pixels, int64_t left, int64_t top, int64_t right, int64_t bottom) {
    if (!pixels)
        return NULL;

    rt_uinineslice_impl *ns =
        (rt_uinineslice_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_uinineslice_impl));
    if (!ns)
        return NULL;

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
    if (left + right > pw) {
        left = pw / 2;
        right = pw - left;
    }
    if (top + bottom > ph) {
        top = ph / 2;
        bottom = ph - top;
    }

    ns->pixels = pixels;
    ns->left = left;
    ns->top = top;
    ns->right = right;
    ns->bottom = bottom;
    ns->tint = 0;

    return ns;
}

/// @brief Perform uinineslice set tint operation.
/// @param ptr
/// @param color
void rt_uinineslice_set_tint(void *ptr, int64_t color) {
    if (!ptr)
        return;
    ((rt_uinineslice_impl *)ptr)->tint = color;
}

/// @brief Perform uinineslice draw operation.
/// @param ptr
/// @param canvas
/// @param x
/// @param y
/// @param w
/// @param h
void rt_uinineslice_draw(void *ptr, void *canvas, int64_t x, int64_t y, int64_t w, int64_t h) {
    if (!ptr || !canvas)
        return;
    rt_uinineslice_impl *ns = (rt_uinineslice_impl *)ptr;
    if (!ns->pixels)
        return;

    int64_t pw = rt_pixels_width(ns->pixels);
    int64_t ph = rt_pixels_height(ns->pixels);
    int64_t L = ns->left, T = ns->top, R = ns->right, B = ns->bottom;

    // Source center region
    int64_t src_cx = L;
    int64_t src_cy = T;
    int64_t src_cw = pw - L - R;
    int64_t src_ch = ph - T - B;

    // Destination center region
    int64_t dst_cw = w - L - R;
    int64_t dst_ch = h - T - B;
    if (dst_cw < 0)
        dst_cw = 0;
    if (dst_ch < 0)
        dst_ch = 0;

    // Draw 4 corners (unscaled — blit directly)
    if (T > 0 && L > 0) // top-left
        rt_canvas_blit_region(canvas, x, y, ns->pixels, 0, 0, L, T);
    if (T > 0 && R > 0) // top-right
        rt_canvas_blit_region(canvas, x + w - R, y, ns->pixels, pw - R, 0, R, T);
    if (B > 0 && L > 0) // bottom-left
        rt_canvas_blit_region(canvas, x, y + h - B, ns->pixels, 0, ph - B, L, B);
    if (B > 0 && R > 0) // bottom-right
        rt_canvas_blit_region(canvas, x + w - R, y + h - B, ns->pixels, pw - R, ph - B, R, B);

    // Draw 4 edges (stretched by tiling the 1-pixel-wide/tall source strip)
    // Top edge
    if (T > 0 && dst_cw > 0 && src_cw > 0) {
        for (int64_t dx = 0; dx < dst_cw; dx += src_cw) {
            int64_t bw = src_cw;
            if (dx + bw > dst_cw)
                bw = dst_cw - dx;
            rt_canvas_blit_region(canvas, x + L + dx, y, ns->pixels, src_cx, 0, bw, T);
        }
    }
    // Bottom edge
    if (B > 0 && dst_cw > 0 && src_cw > 0) {
        for (int64_t dx = 0; dx < dst_cw; dx += src_cw) {
            int64_t bw = src_cw;
            if (dx + bw > dst_cw)
                bw = dst_cw - dx;
            rt_canvas_blit_region(canvas, x + L + dx, y + h - B, ns->pixels, src_cx, ph - B, bw, B);
        }
    }
    // Left edge
    if (L > 0 && dst_ch > 0 && src_ch > 0) {
        for (int64_t dy = 0; dy < dst_ch; dy += src_ch) {
            int64_t bh = src_ch;
            if (dy + bh > dst_ch)
                bh = dst_ch - dy;
            rt_canvas_blit_region(canvas, x, y + T + dy, ns->pixels, 0, src_cy, L, bh);
        }
    }
    // Right edge
    if (R > 0 && dst_ch > 0 && src_ch > 0) {
        for (int64_t dy = 0; dy < dst_ch; dy += src_ch) {
            int64_t bh = src_ch;
            if (dy + bh > dst_ch)
                bh = dst_ch - dy;
            rt_canvas_blit_region(canvas, x + w - R, y + T + dy, ns->pixels, pw - R, src_cy, R, bh);
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
                    canvas, x + L + dx, y + T + dy, ns->pixels, src_cx, src_cy, bw, bh);
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

void *rt_uimenulist_new(int64_t x, int64_t y, int64_t item_height) {
    rt_uimenulist_impl *menu =
        (rt_uimenulist_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_uimenulist_impl));
    if (!menu)
        return NULL;

    memset(menu, 0, sizeof(rt_uimenulist_impl));
    menu->x = x;
    menu->y = y;
    menu->item_height = item_height > 0 ? item_height : 16;
    menu->selected = 0;
    menu->text_color = 0xFFFFFF;     // White
    menu->selected_color = 0xFFFF00; // Yellow
    menu->highlight_bg = 0x333333;   // Dark gray
    menu->visible = 1;

    return menu;
}

/// @brief Perform uimenulist add item operation.
/// @param ptr
/// @param text
void rt_uimenulist_add_item(void *ptr, rt_string text) {
    if (!ptr || !text)
        return;
    rt_uimenulist_impl *menu = (rt_uimenulist_impl *)ptr;
    if (menu->count >= RT_UIMENULIST_MAX_ITEMS)
        return;

    const char *s = rt_string_cstr(text);
    if (!s)
        return;

    size_t len = strlen(s);
    if (len >= MENULIST_MAX_TEXT)
        len = MENULIST_MAX_TEXT - 1;
    memcpy(menu->items[menu->count], s, len);
    menu->items[menu->count][len] = '\0';
    menu->count++;
}

/// @brief Perform uimenulist clear operation.
/// @param ptr
void rt_uimenulist_clear(void *ptr) {
    if (!ptr)
        return;
    rt_uimenulist_impl *menu = (rt_uimenulist_impl *)ptr;
    menu->count = 0;
    menu->selected = 0;
}

/// @brief Perform uimenulist set selected operation.
/// @param ptr
/// @param index
void rt_uimenulist_set_selected(void *ptr, int64_t index) {
    if (!ptr)
        return;
    rt_uimenulist_impl *menu = (rt_uimenulist_impl *)ptr;
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

/// @brief Perform uimenulist get selected operation.
/// @param ptr
/// @return Result value.
int64_t rt_uimenulist_get_selected(void *ptr) {
    if (!ptr)
        return 0;
    return ((rt_uimenulist_impl *)ptr)->selected;
}

/// @brief Perform uimenulist move up operation.
/// @param ptr
void rt_uimenulist_move_up(void *ptr) {
    if (!ptr)
        return;
    rt_uimenulist_impl *menu = (rt_uimenulist_impl *)ptr;
    if (menu->count == 0)
        return;
    if (menu->selected <= 0)
        menu->selected = menu->count - 1; // Wrap to bottom
    else
        menu->selected--;
}

/// @brief Perform uimenulist move down operation.
/// @param ptr
void rt_uimenulist_move_down(void *ptr) {
    if (!ptr)
        return;
    rt_uimenulist_impl *menu = (rt_uimenulist_impl *)ptr;
    if (menu->count == 0)
        return;
    if (menu->selected >= menu->count - 1)
        menu->selected = 0; // Wrap to top
    else
        menu->selected++;
}

void rt_uimenulist_set_colors(void *ptr,
                              int64_t text_color,
                              int64_t selected_color,
                              int64_t highlight_bg) {
    if (!ptr)
        return;
    rt_uimenulist_impl *menu = (rt_uimenulist_impl *)ptr;
    menu->text_color = text_color;
    menu->selected_color = selected_color;
    menu->highlight_bg = highlight_bg;
}

/// @brief Perform uimenulist set font operation.
/// @param ptr
/// @param font
void rt_uimenulist_set_font(void *ptr, void *font) {
    if (!ptr)
        return;
    ((rt_uimenulist_impl *)ptr)->font = font;
}

/// @brief Perform uimenulist set visible operation.
/// @param ptr
/// @param visible
void rt_uimenulist_set_visible(void *ptr, int8_t visible) {
    if (!ptr)
        return;
    ((rt_uimenulist_impl *)ptr)->visible = visible ? 1 : 0;
}

/// @brief Perform uimenulist get count operation.
/// @param ptr
/// @return Result value.
int64_t rt_uimenulist_get_count(void *ptr) {
    if (!ptr)
        return 0;
    return ((rt_uimenulist_impl *)ptr)->count;
}

/// @brief Perform uimenulist draw operation.
/// @param ptr
/// @param canvas
void rt_uimenulist_draw(void *ptr, void *canvas) {
    if (!ptr || !canvas)
        return;
    rt_uimenulist_impl *menu = (rt_uimenulist_impl *)ptr;
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
                hw = rt_str_len(rtext) * 8 + 16;
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
