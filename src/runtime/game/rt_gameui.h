//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/game/rt_gameui.h
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
// Links: src/runtime/game/rt_gameui.c (implementation),
//        src/runtime/graphics/rt_graphics.h (Canvas drawing API),
//        src/runtime/graphics/rt_bitmapfont.h (custom font support)
//
//===----------------------------------------------------------------------===//
#pragma once

#include "rt_string.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

//=========================================================================
// UILabel — Positioned text with optional BitmapFont
//=========================================================================

/// @brief Create a positioned text label with the given color (canvas color format).
void *rt_uilabel_new(int64_t x, int64_t y, rt_string text, int64_t color);
/// @brief Replace the label's text content.
void rt_uilabel_set_text(void *label, rt_string text);
/// @brief Move the label to a new (x, y) position.
void rt_uilabel_set_pos(void *label, int64_t x, int64_t y);
/// @brief Change the label's text color.
void rt_uilabel_set_color(void *label, int64_t color);
/// @brief Bind a custom BitmapFont (NULL falls back to the built-in 8×8 font).
void rt_uilabel_set_font(void *label, void *font);
/// @brief Set integer scale (1 = native, 2 = double-size, etc.).
void rt_uilabel_set_scale(void *label, int64_t scale);
/// @brief Toggle the label's visibility (Draw becomes a no-op when hidden).
void rt_uilabel_set_visible(void *label, int8_t visible);
/// @brief Render the label onto @p canvas.
void rt_uilabel_draw(void *label, void *canvas);
/// @brief Get the label's x-coordinate.
int64_t rt_uilabel_get_x(void *label);
/// @brief Get the label's y-coordinate.
int64_t rt_uilabel_get_y(void *label);

//=========================================================================
// UIBar — Progress/health/XP bar
//=========================================================================

/// @brief Create a progress bar widget at (x, y) of given size with foreground/background colors.
void *rt_uibar_new(int64_t x, int64_t y, int64_t w, int64_t h, int64_t fg_color, int64_t bg_color);
/// @brief Update the bar's current value (clamped to [0, max_value]).
void rt_uibar_set_value(void *bar, int64_t value, int64_t max_value);
/// @brief Move the bar to a new (x, y) position.
void rt_uibar_set_pos(void *bar, int64_t x, int64_t y);
/// @brief Resize the bar.
void rt_uibar_set_size(void *bar, int64_t w, int64_t h);
/// @brief Change the foreground (filled) and background (empty) colors.
void rt_uibar_set_colors(void *bar, int64_t fg, int64_t bg);
/// @brief Set the border color (use -1 to disable the border).
void rt_uibar_set_border(void *bar, int64_t color);
/// @brief Set the fill direction (0 = left-to-right, 1 = right-to-left, 2 = bottom-up, 3 = top-down).
void rt_uibar_set_direction(void *bar, int64_t dir);
/// @brief Toggle visibility.
void rt_uibar_set_visible(void *bar, int8_t visible);
/// @brief Render the bar onto @p canvas.
void rt_uibar_draw(void *bar, void *canvas);
/// @brief Get the current value.
int64_t rt_uibar_get_value(void *bar);
/// @brief Get the maximum value.
int64_t rt_uibar_get_max(void *bar);

//=========================================================================
// UIPanel — Semi-transparent background panel
//=========================================================================

/// @brief Create a semi-transparent rectangular panel with optional border and corner radius.
void *rt_uipanel_new(int64_t x, int64_t y, int64_t w, int64_t h, int64_t bg_color, int64_t alpha);
/// @brief Move the panel to (x, y).
void rt_uipanel_set_pos(void *panel, int64_t x, int64_t y);
/// @brief Resize the panel.
void rt_uipanel_set_size(void *panel, int64_t w, int64_t h);
/// @brief Change the panel's background color and per-pixel alpha (0–255).
void rt_uipanel_set_color(void *panel, int64_t bg_color, int64_t alpha);
/// @brief Set border color and thickness in pixels (thickness 0 disables the border).
void rt_uipanel_set_border(void *panel, int64_t color, int64_t thickness);
/// @brief Set rounded-corner radius in pixels (0 = sharp corners).
void rt_uipanel_set_corner_radius(void *panel, int64_t radius);
/// @brief Toggle visibility.
void rt_uipanel_set_visible(void *panel, int8_t visible);
/// @brief Render the panel onto @p canvas.
void rt_uipanel_draw(void *panel, void *canvas);

//=========================================================================
// UINineSlice — Scalable bordered UI element
//=========================================================================

/// @brief Create a 9-slice scalable bordered widget from a Pixels source with corner insets.
/// Corners stay fixed-size; edges stretch along their axis; center stretches both axes.
void *rt_uinineslice_new(void *pixels, int64_t left, int64_t top, int64_t right, int64_t bottom);
/// @brief Render the 9-slice at (x, y) stretched to (w × h).
void rt_uinineslice_draw(void *ns, void *canvas, int64_t x, int64_t y, int64_t w, int64_t h);
/// @brief Apply a per-channel multiplicative tint to subsequent draws.
void rt_uinineslice_set_tint(void *ns, int64_t color);

//=========================================================================
// UIMenuList — Vertical menu with selection highlight
//=========================================================================

/// Maximum items in a MenuList.
#define RT_UIMENULIST_MAX_ITEMS 64

/// @brief Create a vertical menu list at (x, y) with the given per-row pixel height.
void *rt_uimenulist_new(int64_t x, int64_t y, int64_t item_height);
/// @brief Append a menu item (string is copied; max RT_UIMENULIST_MAX_ITEMS).
void rt_uimenulist_add_item(void *menu, rt_string text);
/// @brief Remove all items and free their string copies.
void rt_uimenulist_clear(void *menu);
/// @brief Set which item is highlighted (clamped to valid range).
void rt_uimenulist_set_selected(void *menu, int64_t index);
/// @brief Get the index of the currently selected item.
int64_t rt_uimenulist_get_selected(void *menu);
/// @brief Move selection up by one item, wrapping to the last item past the top.
void rt_uimenulist_move_up(void *menu);
/// @brief Move selection down by one item, wrapping to the first item past the bottom.
void rt_uimenulist_move_down(void *menu);
/// @brief Set normal text color, selected-item text color, and highlight background color.
void rt_uimenulist_set_colors(void *menu,
                              int64_t text_color,
                              int64_t selected_color,
                              int64_t highlight_bg);
/// @brief Bind a custom BitmapFont (NULL falls back to built-in 8×8).
void rt_uimenulist_set_font(void *menu, void *font);
/// @brief Toggle visibility.
void rt_uimenulist_set_visible(void *menu, int8_t visible);
/// @brief Get the number of items currently in the menu.
int64_t rt_uimenulist_get_count(void *menu);
/// @brief Render the menu onto @p canvas.
void rt_uimenulist_draw(void *menu, void *canvas);
/// @brief Process input flags and return the selected index when @p confirm is set, else -1.
int64_t rt_uimenulist_handle_input(void *menu, int8_t up, int8_t down, int8_t confirm);

//=========================================================================
// GameButton
//=========================================================================

/// @brief Create a clickable in-game button at (x, y) of size (w × h) with label text.
void *rt_gamebutton_new(int64_t x, int64_t y, int64_t w, int64_t h, void *text);
/// @brief Replace the button's label.
void rt_gamebutton_set_text(void *btn, void *text);
/// @brief Set background colors for normal vs selected (focused) state.
void rt_gamebutton_set_colors(void *btn, int64_t normal, int64_t selected);
/// @brief Set text colors for normal vs selected state.
void rt_gamebutton_set_text_colors(void *btn, int64_t normal, int64_t selected);
/// @brief Set border thickness in pixels and border color (width 0 disables border).
void rt_gamebutton_set_border(void *btn, int64_t width, int64_t color);
/// @brief Render the button; @p is_selected picks the selected colorway when nonzero.
void rt_gamebutton_draw(void *btn, void *canvas, int8_t is_selected);
/// @brief Get the button's x-coordinate.
int64_t rt_gamebutton_get_x(void *btn);
/// @brief Get the button's y-coordinate.
int64_t rt_gamebutton_get_y(void *btn);
/// @brief Set the button's x-coordinate.
void rt_gamebutton_set_x(void *btn, int64_t x);
/// @brief Set the button's y-coordinate.
void rt_gamebutton_set_y(void *btn, int64_t y);

#ifdef __cplusplus
}
#endif
