//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/collections/rt_gameui.h
// Purpose: Lightweight in-game UI widgets (Label, Bar, Panel, NineSlice,
//   MenuList) that draw directly to a Canvas. Purpose-built for game HUDs,
//   menus, and overlays — separate from the desktop ViperGUI widget system.
//
// Key invariants:
//   - All widgets are GC-managed via rt_obj_new_i64.
//   - Draw methods are no-ops when canvas or widget is NULL.
//   - Widgets use immediate-mode rendering (no retained widget tree).
//   - UIBar values are clamped to [0, max].
//   - UIMenuList selection wraps around on MoveUp/MoveDown.
//
// Ownership/Lifetime:
//   - Widget objects are GC-managed; no manual free needed.
//   - UIMenuList item strings are copied (strdup); freed on Clear/destroy.
//   - UINineSlice holds a reference to its Pixels source (not copied).
//
// Links: src/runtime/collections/rt_gameui.c (implementation),
//        src/runtime/graphics/rt_graphics.h (Canvas drawing API),
//        src/runtime/graphics/rt_bitmapfont.h (custom font support)
//
//===----------------------------------------------------------------------===//
#pragma once

#include "rt_string.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    //=========================================================================
    // UILabel — Positioned text with optional BitmapFont
    //=========================================================================

    void   *rt_uilabel_new(int64_t x, int64_t y, rt_string text, int64_t color);
    void    rt_uilabel_set_text(void *label, rt_string text);
    void    rt_uilabel_set_pos(void *label, int64_t x, int64_t y);
    void    rt_uilabel_set_color(void *label, int64_t color);
    void    rt_uilabel_set_font(void *label, void *font);
    void    rt_uilabel_set_scale(void *label, int64_t scale);
    void    rt_uilabel_set_visible(void *label, int8_t visible);
    void    rt_uilabel_draw(void *label, void *canvas);
    int64_t rt_uilabel_get_x(void *label);
    int64_t rt_uilabel_get_y(void *label);

    //=========================================================================
    // UIBar — Progress/health/XP bar
    //=========================================================================

    void   *rt_uibar_new(int64_t x, int64_t y, int64_t w, int64_t h,
                          int64_t fg_color, int64_t bg_color);
    void    rt_uibar_set_value(void *bar, int64_t value, int64_t max_value);
    void    rt_uibar_set_pos(void *bar, int64_t x, int64_t y);
    void    rt_uibar_set_size(void *bar, int64_t w, int64_t h);
    void    rt_uibar_set_colors(void *bar, int64_t fg, int64_t bg);
    void    rt_uibar_set_border(void *bar, int64_t color);
    void    rt_uibar_set_direction(void *bar, int64_t dir);
    void    rt_uibar_set_visible(void *bar, int8_t visible);
    void    rt_uibar_draw(void *bar, void *canvas);
    int64_t rt_uibar_get_value(void *bar);
    int64_t rt_uibar_get_max(void *bar);

    //=========================================================================
    // UIPanel — Semi-transparent background panel
    //=========================================================================

    void   *rt_uipanel_new(int64_t x, int64_t y, int64_t w, int64_t h,
                            int64_t bg_color, int64_t alpha);
    void    rt_uipanel_set_pos(void *panel, int64_t x, int64_t y);
    void    rt_uipanel_set_size(void *panel, int64_t w, int64_t h);
    void    rt_uipanel_set_color(void *panel, int64_t bg_color, int64_t alpha);
    void    rt_uipanel_set_border(void *panel, int64_t color, int64_t thickness);
    void    rt_uipanel_set_corner_radius(void *panel, int64_t radius);
    void    rt_uipanel_set_visible(void *panel, int8_t visible);
    void    rt_uipanel_draw(void *panel, void *canvas);

    //=========================================================================
    // UINineSlice — Scalable bordered UI element
    //=========================================================================

    void   *rt_uinineslice_new(void *pixels, int64_t left, int64_t top,
                                int64_t right, int64_t bottom);
    void    rt_uinineslice_draw(void *ns, void *canvas, int64_t x, int64_t y,
                                 int64_t w, int64_t h);
    void    rt_uinineslice_set_tint(void *ns, int64_t color);

    //=========================================================================
    // UIMenuList — Vertical menu with selection highlight
    //=========================================================================

    /// Maximum items in a MenuList.
    #define RT_UIMENULIST_MAX_ITEMS 64

    void   *rt_uimenulist_new(int64_t x, int64_t y, int64_t item_height);
    void    rt_uimenulist_add_item(void *menu, rt_string text);
    void    rt_uimenulist_clear(void *menu);
    void    rt_uimenulist_set_selected(void *menu, int64_t index);
    int64_t rt_uimenulist_get_selected(void *menu);
    void    rt_uimenulist_move_up(void *menu);
    void    rt_uimenulist_move_down(void *menu);
    void    rt_uimenulist_set_colors(void *menu, int64_t text_color,
                                      int64_t selected_color, int64_t highlight_bg);
    void    rt_uimenulist_set_font(void *menu, void *font);
    void    rt_uimenulist_set_visible(void *menu, int8_t visible);
    int64_t rt_uimenulist_get_count(void *menu);
    void    rt_uimenulist_draw(void *menu, void *canvas);

#ifdef __cplusplus
}
#endif
