//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/game/rt_gameui.h
// Purpose: Lightweight in-game UI widgets (Label, Bar, Panel, NineSlice,
//   MenuList, GameButton) that draw directly to a Canvas. Purpose-built for
//   game HUDs, menus, and overlays — separate from the desktop ViperGUI widget system.
//
// Key invariants:
//   - All widgets are GC-managed via rt_obj_new_i64.
//   - Draw methods are no-ops when canvas or widget is NULL.
//   - Widgets use immediate-mode rendering (no retained widget tree).
//   - UIBar values are clamped to [0, max].
//   - UIMenuList selection wraps around on MoveUp/MoveDown.
//   - Fixed-size widget text buffers truncate on UTF-8 codepoint boundaries.
//
// Ownership/Lifetime:
//   - Widget objects are GC-managed; no manual free needed.
//   - UILabel/GameButton/MenuList copy text into widget-owned buffers.
//   - UILabel/MenuList retain assigned BitmapFont handles.
//   - UINineSlice retains a reference to its Pixels source (not copied).
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

#define RT_UILABEL_CLASS_ID INT64_C(-0x510101)
#define RT_UIBAR_CLASS_ID INT64_C(-0x510102)
#define RT_UIPANEL_CLASS_ID INT64_C(-0x510103)
#define RT_UININESLICE_CLASS_ID INT64_C(-0x510104)
#define RT_UIMENULIST_CLASS_ID INT64_C(-0x510105)
#define RT_GAMEBUTTON_CLASS_ID INT64_C(-0x510106)
#define RT_UITEXTINPUT_CLASS_ID INT64_C(-0x510107)
#define RT_UITABLE_CLASS_ID INT64_C(-0x510108)
#define RT_UIMODAL_CLASS_ID INT64_C(-0x510109)
#define RT_UISLIDER_CLASS_ID INT64_C(-0x51010A)
#define RT_UIDROPDOWN_CLASS_ID INT64_C(-0x51010B)
#define RT_UITOOLTIP_CLASS_ID INT64_C(-0x51010C)

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
/// @brief Set the border color (use 0 to disable the border).
void rt_uibar_set_border(void *bar, int64_t color);
/// @brief Set the fill direction (0 = left-to-right, 1 = right-to-left, 2 = bottom-up, 3 =
/// top-down).
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
/// @brief Set border color and thickness in pixels (color 0 disables the border).
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
/// Corners stay fixed-size; edges tile along their axis; center tiles to fill.
void *rt_uinineslice_new(void *pixels, int64_t left, int64_t top, int64_t right, int64_t bottom);
/// @brief Render the 9-slice at (x, y) tiled to fill (w × h).
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
/// @brief Get the button's width.
int64_t rt_gamebutton_get_width(void *btn);
/// @brief Get the button's height.
int64_t rt_gamebutton_get_height(void *btn);
/// @brief Set the button's x-coordinate.
void rt_gamebutton_set_x(void *btn, int64_t x);
/// @brief Set the button's y-coordinate.
void rt_gamebutton_set_y(void *btn, int64_t y);
/// @brief Set the button's width and height.
void rt_gamebutton_set_size(void *btn, int64_t w, int64_t h);
/// @brief Toggle the button's visibility.
void rt_gamebutton_set_visible(void *btn, int8_t visible);
/// @brief Return whether the button is visible.
int8_t rt_gamebutton_get_visible(void *btn);
/// @brief Set the button text scale.
void rt_gamebutton_set_text_scale(void *btn, int64_t scale);
/// @brief Get the button text scale.
int64_t rt_gamebutton_get_text_scale(void *btn);

//=========================================================================
// TextInput
//=========================================================================

void *rt_uitextinput_new(int64_t x, int64_t y, int64_t w, int64_t h);
void rt_uitextinput_set_text(void *ti, rt_string text);
rt_string rt_uitextinput_get_text(void *ti);
int64_t rt_uitextinput_text_length(void *ti);
int64_t rt_uitextinput_get_cursor(void *ti);
void rt_uitextinput_set_cursor(void *ti, int64_t pos);
void rt_uitextinput_select_all(void *ti);
void rt_uitextinput_clear_selection(void *ti);
int8_t rt_uitextinput_has_selection(void *ti);
rt_string rt_uitextinput_get_selected_text(void *ti);
void rt_uitextinput_delete_selection(void *ti);
int64_t rt_uitextinput_handle_key(void *ti, int64_t key_code, int8_t shift_held);
int64_t rt_uitextinput_handle_text(void *ti, rt_string typed_text);
void rt_uitextinput_handle_mouse_click(void *ti, int64_t mx, int64_t my, int8_t shift_held);
void rt_uitextinput_handle_mouse_drag(void *ti, int64_t mx, int64_t my);
void rt_uitextinput_update(void *ti, int64_t delta_ms);
void rt_uitextinput_draw(void *ti, void *canvas);
void rt_uitextinput_set_text_color(void *ti, int64_t color);
int64_t rt_uitextinput_get_text_color(void *ti);
void rt_uitextinput_set_bg_color(void *ti, int64_t color);
int64_t rt_uitextinput_get_bg_color(void *ti);
void rt_uitextinput_set_cursor_color(void *ti, int64_t color);
void rt_uitextinput_set_selection_color(void *ti, int64_t color);
void rt_uitextinput_set_border_color(void *ti, int64_t color);
void rt_uitextinput_set_border_color_focused(void *ti, int64_t color);
void rt_uitextinput_set_font(void *ti, void *font);
void rt_uitextinput_set_visible(void *ti, int8_t visible);
int8_t rt_uitextinput_get_visible(void *ti);
void rt_uitextinput_set_enabled(void *ti, int8_t enabled);
int8_t rt_uitextinput_get_enabled(void *ti);
void rt_uitextinput_set_focused(void *ti, int8_t focused);
int8_t rt_uitextinput_get_focused(void *ti);
void rt_uitextinput_set_password_mode(void *ti, int8_t password);
void rt_uitextinput_set_placeholder(void *ti, rt_string placeholder);
void rt_uitextinput_set_max_codepoints(void *ti, int64_t max_cps);

//=========================================================================
// Table
//=========================================================================

void *rt_uitable_new(int64_t x, int64_t y, int64_t w, int64_t h);
int64_t rt_uitable_add_column(void *table, rt_string title, int64_t width, int64_t align);
void rt_uitable_set_column_sortable(void *table, int64_t col, int8_t sortable, int8_t numeric);
int64_t rt_uitable_column_count(void *table);
int64_t rt_uitable_add_row(void *table);
void rt_uitable_set_cell(void *table, int64_t row, int64_t col, rt_string text);
rt_string rt_uitable_get_cell(void *table, int64_t row, int64_t col);
void rt_uitable_remove_row(void *table, int64_t row);
void rt_uitable_clear_rows(void *table);
int64_t rt_uitable_row_count(void *table);
void rt_uitable_sort_by(void *table, int64_t col, int8_t descending);
int64_t rt_uitable_get_sort_column(void *table);
int8_t rt_uitable_get_sort_descending(void *table);
void rt_uitable_set_scroll(void *table, int64_t row);
int64_t rt_uitable_get_scroll(void *table);
void rt_uitable_set_selected_row(void *table, int64_t row);
int64_t rt_uitable_get_selected_row(void *table);
int64_t rt_uitable_handle_click(void *table, int64_t mx, int64_t my);
int64_t rt_uitable_last_header_click(void *table);
void rt_uitable_handle_scroll(void *table, int64_t delta);
void rt_uitable_handle_key(void *table, int64_t key_code);
void rt_uitable_draw(void *table, void *canvas);

//=========================================================================
// Modal
//=========================================================================

void *rt_uimodal_new(int64_t w, int64_t h);
void *rt_uimodal_new_at(int64_t x, int64_t y, int64_t w, int64_t h);
void rt_uimodal_set_title(void *modal, rt_string title);
void rt_uimodal_set_content(void *modal, rt_string text);
int64_t rt_uimodal_add_button(void *modal, rt_string text, int64_t return_value);
void rt_uimodal_set_default_button(void *modal, int64_t index);
void rt_uimodal_set_cancel_button(void *modal, int64_t index);
void rt_uimodal_add_child(void *modal, void *child_widget);
void rt_uimodal_open(void *modal);
void rt_uimodal_close(void *modal);
int8_t rt_uimodal_is_open(void *modal);
int64_t rt_uimodal_get_result(void *modal);
int64_t rt_uimodal_handle_key(void *modal, int64_t key_code, int8_t shift_held);
int64_t rt_uimodal_handle_click(void *modal, int64_t mx, int64_t my);
void rt_uimodal_draw(void *modal, void *canvas);

//=========================================================================
// Slider
//=========================================================================

void *rt_uislider_new(int64_t x, int64_t y, int64_t w, int64_t h, int64_t min_v, int64_t max_v);
void rt_uislider_set_value(void *s, int64_t v);
int64_t rt_uislider_get_value(void *s);
void rt_uislider_set_step(void *s, int64_t step);
void rt_uislider_set_label(void *s, rt_string label);
int8_t rt_uislider_handle_key(void *s, int64_t key_code);
int8_t rt_uislider_handle_mouse_down(void *s, int64_t mx, int64_t my);
int8_t rt_uislider_handle_mouse_drag(void *s, int64_t mx);
int8_t rt_uislider_handle_mouse_up(void *s);
void rt_uislider_draw(void *s, void *canvas);

//=========================================================================
// Dropdown
//=========================================================================

void *rt_uidropdown_new(int64_t x, int64_t y, int64_t w, int64_t h);
void rt_uidropdown_add_option(void *dd, rt_string text);
void rt_uidropdown_clear_options(void *dd);
int64_t rt_uidropdown_get_selected(void *dd);
void rt_uidropdown_set_selected(void *dd, int64_t index);
rt_string rt_uidropdown_get_selected_text(void *dd);
int8_t rt_uidropdown_is_open(void *dd);
void rt_uidropdown_open(void *dd);
void rt_uidropdown_close(void *dd);
int8_t rt_uidropdown_handle_click(void *dd, int64_t mx, int64_t my);
int8_t rt_uidropdown_handle_key(void *dd, int64_t key_code);
void rt_uidropdown_draw(void *dd, void *canvas);

//=========================================================================
// Tooltip
//=========================================================================

void *rt_uitooltip_new(void);
void rt_uitooltip_set_text(void *t, rt_string text);
void rt_uitooltip_set_hover_delay_ms(void *t, int64_t ms);
void rt_uitooltip_update(void *t, int64_t mx, int64_t my, int8_t hovered_target, int64_t delta_ms);
void rt_uitooltip_draw(void *t, void *canvas);

#ifdef __cplusplus
}
#endif
