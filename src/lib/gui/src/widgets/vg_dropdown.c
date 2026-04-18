//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/lib/gui/src/widgets/vg_dropdown.c
//
//===----------------------------------------------------------------------===//
// vg_dropdown.c - Dropdown/ComboBox widget implementation
#include "../../../graphics/include/vgfx.h"
#include "../../include/vg_event.h"
#include "../../include/vg_theme.h"
#include "../../include/vg_widgets.h"
#include <stdlib.h>
#include <string.h>

//=============================================================================
// Forward Declarations
//=============================================================================

static void dropdown_destroy(vg_widget_t *widget);
static void dropdown_measure(vg_widget_t *widget, float avail_w, float avail_h);
static void dropdown_paint(vg_widget_t *widget, void *canvas);
static void dropdown_paint_overlay(vg_widget_t *widget, void *canvas);
static bool dropdown_handle_event(vg_widget_t *widget, vg_event_t *event);
static bool dropdown_can_focus(vg_widget_t *widget);

static void dropdown_clamp_scroll(vg_dropdown_t *dd, float panel_h);
static void dropdown_scroll_to_index(vg_dropdown_t *dd, int index);
static void dropdown_open(vg_widget_t *widget, vg_dropdown_t *dd, int preferred_hover);
static void dropdown_close(vg_widget_t *widget, vg_dropdown_t *dd);

static float dropdown_scrollbar_thumb_size(float track_size, float content_size, float viewport_size);
static float dropdown_scrollbar_thumb_offset(float scroll_pos,
                                             float scroll_range,
                                             float track_size,
                                             float thumb_size);

//=============================================================================
// Dropdown VTable
//=============================================================================

static vg_widget_vtable_t g_dropdown_vtable = {.destroy = dropdown_destroy,
                                               .measure = dropdown_measure,
                                               .arrange = NULL,
                                               .paint = dropdown_paint,
                                               .paint_overlay = dropdown_paint_overlay,
                                               .handle_event = dropdown_handle_event,
                                               .can_focus = dropdown_can_focus,
                                               .on_focus = NULL};

static void dropdown_destroy(vg_widget_t *widget) {
    vg_dropdown_t *dd = (vg_dropdown_t *)widget;
    if (vg_widget_get_input_capture() == widget)
        vg_widget_release_input_capture();
    for (int i = 0; i < dd->item_count; i++)
        free(dd->items[i]);
    free(dd->items);
    free(dd->placeholder);
    dd->items = NULL;
    dd->item_count = 0;
    dd->item_capacity = 0;
}

static void dropdown_measure(vg_widget_t *widget, float avail_w, float avail_h) {
    vg_theme_t *theme = vg_theme_get_current();
    (void)avail_w;
    (void)avail_h;

    widget->measured_width = 140.0f;
    widget->measured_height = theme ? theme->input.height : 28.0f;
}

// Height of one item row in the dropdown panel
static float dropdown_item_height(vg_dropdown_t *dd) {
    return dd->font_size > 0 ? (dd->font_size * 1.6f) : 24.0f;
}

static void dropdown_get_viewport_bounds(vg_dropdown_t *dd,
                                         float *out_x,
                                         float *out_y,
                                         float *out_w,
                                         float *out_h) {
    vg_widget_t *root = &dd->base;
    while (root->parent)
        root = root->parent;
    vg_widget_get_screen_bounds(root, out_x, out_y, out_w, out_h);
}

// Shared geometry helper: resolves panel top (absolute) plus flip-above decision
// when the panel would clip the bottom of the window. Used by both hit-testing
// and paint so clicks always match the rendered position.
static void dropdown_resolve_panel_rect(vg_dropdown_t *dd,
                                        float abs_widget_x,
                                        float abs_widget_y,
                                        float *out_panel_top,
                                        float *out_panel_h) {
    float ih = dropdown_item_height(dd);
    float panel_h = ih * dd->item_count;
    if (panel_h > dd->dropdown_height)
        panel_h = dd->dropdown_height;

    float below_top = abs_widget_y + dd->base.height;
    float panel_top = below_top;

    float viewport_y = 0.0f;
    float viewport_h = 0.0f;
    dropdown_get_viewport_bounds(dd, NULL, &viewport_y, NULL, &viewport_h);
    if (viewport_h > 0.0f) {
        float viewport_bottom = viewport_y + viewport_h;
        float space_below = viewport_bottom - below_top;
        float space_above = abs_widget_y - viewport_y;
        if (space_below < panel_h && space_above > space_below) {
            panel_top = abs_widget_y - panel_h;
            if (panel_top < viewport_y)
                panel_top = viewport_y;
        }
    }

    (void)abs_widget_x;
    if (out_panel_top)
        *out_panel_top = panel_top;
    if (out_panel_h)
        *out_panel_h = panel_h;
}

static bool dropdown_panel_hit(vg_dropdown_t *dd, float screen_x, float screen_y, int *index) {
    float sx = 0.0f;
    float sy = 0.0f;
    vg_widget_get_screen_bounds(&dd->base, &sx, &sy, NULL, NULL);

    float ih = dropdown_item_height(dd);
    float panel_top = 0.0f;
    float panel_h = 0.0f;
    dropdown_resolve_panel_rect(dd, sx, sy, &panel_top, &panel_h);

    float panel_left = sx;
    float panel_right = panel_left + dd->base.width;
    float panel_bottom = panel_top + panel_h;
    if (screen_x < panel_left || screen_x >= panel_right || screen_y < panel_top ||
        screen_y >= panel_bottom) {
        if (index)
            *index = -1;
        return false;
    }

    float rel_y = screen_y - panel_top + dd->scroll_y;
    int hit = rel_y >= 0.0f ? (int)(rel_y / ih) : -1;
    if (hit < 0 || hit >= dd->item_count)
        hit = -1;
    if (index)
        *index = hit;
    return true;
}

static void dropdown_clamp_scroll(vg_dropdown_t *dd, float panel_h) {
    if (!dd)
        return;

    if (dd->scroll_y < 0.0f)
        dd->scroll_y = 0.0f;

    float max_scroll = dd->item_count * dropdown_item_height(dd) - panel_h;
    if (max_scroll < 0.0f)
        max_scroll = 0.0f;
    if (dd->scroll_y > max_scroll)
        dd->scroll_y = max_scroll;
}

static float dropdown_scrollbar_thumb_size(float track_size, float content_size, float viewport_size) {
    if (track_size <= 0.0f || content_size <= 0.0f || viewport_size <= 0.0f)
        return track_size;

    float thumb_size = track_size * (viewport_size / content_size);
    if (thumb_size < 18.0f)
        thumb_size = 18.0f;
    if (thumb_size > track_size)
        thumb_size = track_size;
    return thumb_size;
}

static float dropdown_scrollbar_thumb_offset(float scroll_pos,
                                             float scroll_range,
                                             float track_size,
                                             float thumb_size) {
    float thumb_travel = track_size - thumb_size;
    if (scroll_range <= 0.0f || thumb_travel <= 0.0f)
        return 0.0f;
    return (scroll_pos / scroll_range) * thumb_travel;
}

static void dropdown_scroll_to_index(vg_dropdown_t *dd, int index) {
    if (!dd || index < 0 || index >= dd->item_count)
        return;

    float sx = 0.0f;
    float sy = 0.0f;
    float panel_top = 0.0f;
    float panel_h = 0.0f;
    vg_widget_get_screen_bounds(&dd->base, &sx, &sy, NULL, NULL);
    dropdown_resolve_panel_rect(dd, sx, sy, &panel_top, &panel_h);
    dropdown_clamp_scroll(dd, panel_h);

    float ih = dropdown_item_height(dd);
    float item_top = index * ih;
    float item_bottom = item_top + ih;
    if (item_top < dd->scroll_y)
        dd->scroll_y = item_top;
    else if (item_bottom > dd->scroll_y + panel_h)
        dd->scroll_y = item_bottom - panel_h;
    dropdown_clamp_scroll(dd, panel_h);
}

static void dropdown_open(vg_widget_t *widget, vg_dropdown_t *dd, int preferred_hover) {
    if (!dd || dd->open)
        return;

    dd->open = true;
    if (preferred_hover >= 0 && preferred_hover < dd->item_count)
        dd->hovered_index = preferred_hover;
    else if (dd->selected_index >= 0 && dd->selected_index < dd->item_count)
        dd->hovered_index = dd->selected_index;
    else if (dd->item_count > 0)
        dd->hovered_index = 0;
    else
        dd->hovered_index = -1;
    if (dd->hovered_index >= 0)
        dropdown_scroll_to_index(dd, dd->hovered_index);
    vg_widget_set_input_capture(widget);
    widget->needs_paint = true;
}

static void dropdown_close(vg_widget_t *widget, vg_dropdown_t *dd) {
    if (!dd || !dd->open)
        return;
    dd->open = false;
    dd->hovered_index = -1;
    if (vg_widget_get_input_capture() == widget)
        vg_widget_release_input_capture();
    widget->needs_paint = true;
}

static void dropdown_paint(vg_widget_t *widget, void *canvas) {
    vg_dropdown_t *dd = (vg_dropdown_t *)widget;
    vg_theme_t *theme = vg_theme_get_current();
    vgfx_window_t win = (vgfx_window_t)canvas;

    int32_t x = (int32_t)widget->x;
    int32_t y = (int32_t)widget->y;
    int32_t w = (int32_t)widget->width;
    int32_t h = (int32_t)widget->height;
    int32_t gutter_w = (int32_t)(dd->arrow_size + 14.0f);

    uint32_t bg = dd->bg_color;
    uint32_t border = dd->border_color;
    uint32_t text = dd->text_color;

    if (!widget->enabled) {
        bg = theme->colors.bg_disabled;
        border = theme->colors.border_secondary;
        text = theme->colors.fg_disabled;
    } else {
        if (widget->state & VG_STATE_HOVERED)
            bg = vg_color_blend(bg, theme->colors.bg_hover, 0.3f);
        if (dd->open)
            bg = vg_color_blend(bg, theme->colors.bg_secondary, 0.25f);
        if (widget->state & VG_STATE_FOCUSED)
            border = theme->colors.border_focus;
        else if (widget->state & VG_STATE_HOVERED || dd->open)
            border = theme->colors.border_secondary;
    }
    uint32_t gutter_bg = widget->enabled ? vg_color_blend(bg, theme->colors.bg_tertiary, 0.55f)
                                         : theme->colors.bg_disabled;

    // Header box
    vgfx_fill_rect(win, x, y, w, h, bg);
    if (w > gutter_w)
        vgfx_fill_rect(win, x + w - gutter_w, y + 1, gutter_w - 1, h - 2, gutter_bg);
    if (w > gutter_w)
        vgfx_fill_rect(win, x + w - gutter_w, y + 2, 1, h - 4, theme->colors.border_secondary);
    if (w > 2)
        vgfx_fill_rect(win, x + 1, y + 1, w - 2, 1, vg_color_lighten(bg, 0.05f));
    vgfx_rect(win, x, y, w, h, border);
    if (w > 2)
        vgfx_fill_rect(win,
                       x + 1,
                       y + h - 2,
                       w - 2,
                       2,
                       dd->open ? theme->colors.accent_primary
                                : vg_color_darken(border, 0.15f));

    // Selected text or placeholder
    const char *label = (dd->selected_index >= 0 && dd->selected_index < dd->item_count)
                            ? dd->items[dd->selected_index]
                            : dd->placeholder;
    if (label && dd->font) {
        float ty = widget->y + widget->height * 0.5f + dd->font_size * 0.35f;
        vg_font_draw_text(canvas,
                          dd->font,
                          dd->font_size,
                          widget->x + 6.0f,
                          ty,
                          label,
                          label == dd->placeholder ? theme->colors.fg_secondary : text);
    }

    // Arrow glyph
    float ax = widget->x + widget->width - dd->arrow_size - 8.0f;
    float ay = widget->y + widget->height / 2.0f;
    float as2 = dd->arrow_size / 2.0f;
    vgfx_line(win,
              (int32_t)(ax),
              (int32_t)(dd->open ? ay + as2 / 2 : ay - as2 / 2),
              (int32_t)(ax + as2),
              (int32_t)(dd->open ? ay - as2 / 2 : ay + as2 / 2),
              text);
    vgfx_line(win,
              (int32_t)(ax + as2),
              (int32_t)(dd->open ? ay - as2 / 2 : ay + as2 / 2),
              (int32_t)(ax + dd->arrow_size),
              (int32_t)(dd->open ? ay + as2 / 2 : ay - as2 / 2),
              text);
}

static void dropdown_paint_overlay(vg_widget_t *widget, void *canvas) {
    vg_dropdown_t *dd = (vg_dropdown_t *)widget;
    vg_theme_t *theme = vg_theme_get_current();
    vgfx_window_t win = (vgfx_window_t)canvas;

    if (!dd->open || dd->item_count <= 0)
        return;

    float ih = dropdown_item_height(dd);
    float panel_top_abs = 0.0f;
    float panel_h = 0.0f;
    dropdown_resolve_panel_rect(dd, widget->x, widget->y, &panel_top_abs, &panel_h);

    int32_t px = (int32_t)widget->x;
    // paint works in widget-coordinates via canvas; translate the absolute
    // panel_top back into the same coordinate space the rest of paint uses.
    int32_t py = (int32_t)panel_top_abs;
    int32_t pw = (int32_t)widget->width;
    int32_t ph = (int32_t)panel_h;
    int32_t scroll_track_w = 6;
    bool show_scrollbar = dd->item_count * ih > panel_h;

    dropdown_clamp_scroll(dd, panel_h);

    vgfx_fill_rect(win, px, py, pw, ph, dd->dropdown_bg);
    vgfx_rect(win, px, py, pw, ph, dd->border_color);
    if (pw > 2)
        vgfx_fill_rect(win, px + 1, py + 1, pw - 2, 1, theme->colors.accent_primary);

    int visible_count = (int)(panel_h / ih);
    int start_item = (int)(dd->scroll_y / ih);
    if (start_item < 0)
        start_item = 0;

    for (int i = start_item; i < dd->item_count && i < start_item + visible_count + 1; i++) {
        float iy = (float)py + (i - start_item) * ih;
        int32_t iy32 = (int32_t)iy;
        uint32_t row_bg =
            (i == dd->hovered_index)
                ? dd->hover_bg
                : ((i == dd->selected_index)
                       ? dd->selected_bg
                       : (((i & 1) == 0) ? dd->dropdown_bg
                                         : vg_color_blend(dd->dropdown_bg, theme->colors.bg_secondary, 0.35f)));

        vgfx_fill_rect(win, px + 1, iy32, pw - 2, (int32_t)ih, row_bg);
        if (i == dd->selected_index)
            vgfx_fill_rect(win, px + 1, iy32, 3, (int32_t)ih, theme->colors.accent_primary);
        vgfx_fill_rect(win, px + 1, iy32 + (int32_t)ih - 1, pw - 2, 1, theme->colors.border_secondary);

        if (dd->items[i] && dd->font) {
            float ty = iy + ih * 0.7f;
            vg_font_draw_text(
                canvas,
                dd->font,
                dd->font_size,
                (float)(px + 8),
                ty,
                dd->items[i],
                dd->text_color);
        }
    }

    if (show_scrollbar) {
        float track_x = (float)(px + pw - scroll_track_w - 3);
        float track_y = (float)(py + 3);
        float track_h = panel_h - 6.0f;
        float thumb_h = dropdown_scrollbar_thumb_size(track_h, dd->item_count * ih, panel_h);
        float scroll_range = dd->item_count * ih - panel_h;
        float thumb_y = track_y +
                        dropdown_scrollbar_thumb_offset(dd->scroll_y, scroll_range, track_h, thumb_h);
        vgfx_fill_rect(win,
                       (int32_t)track_x,
                       (int32_t)track_y,
                       scroll_track_w,
                       (int32_t)track_h,
                       theme->colors.bg_secondary);
        vgfx_fill_rect(win,
                       (int32_t)track_x + 1,
                       (int32_t)thumb_y,
                       scroll_track_w - 2,
                       (int32_t)thumb_h,
                       theme->colors.bg_hover);
    }
}

static bool dropdown_handle_event(vg_widget_t *widget, vg_event_t *event) {
    vg_dropdown_t *dd = (vg_dropdown_t *)widget;

    if (!widget->enabled)
        return false;

    switch (event->type) {
        case VG_EVENT_CLICK: {
            if (!dd->open) {
                dropdown_open(widget, dd, dd->selected_index);
            } else {
                int idx = -1;
                if (dropdown_panel_hit(dd, event->mouse.screen_x, event->mouse.screen_y, &idx) &&
                    idx >= 0) {
                    vg_dropdown_set_selected(dd, idx);
                }
                dropdown_close(widget, dd);
            }
            event->handled = true;
            return true;
        }

        case VG_EVENT_MOUSE_MOVE:
            if (dd->open) {
                int old_hover = dd->hovered_index;
                if (!dropdown_panel_hit(
                        dd, event->mouse.screen_x, event->mouse.screen_y, &dd->hovered_index)) {
                    dd->hovered_index = -1;
                }
                if (old_hover != dd->hovered_index)
                    widget->needs_paint = true;
                return true;
            }
            return false;

        case VG_EVENT_MOUSE_WHEEL:
            if (!dd->open)
                return false;
            dd->scroll_y -= event->wheel.delta_y * dropdown_item_height(dd);
            {
                float sx = 0.0f;
                float sy = 0.0f;
                float panel_top = 0.0f;
                float panel_h = 0.0f;
                vg_widget_get_screen_bounds(&dd->base, &sx, &sy, NULL, NULL);
                dropdown_resolve_panel_rect(dd, sx, sy, &panel_top, &panel_h);
                dropdown_clamp_scroll(dd, panel_h);
            }
            widget->needs_paint = true;
            event->handled = true;
            return true;

        case VG_EVENT_KEY_DOWN:
            if (!dd->open) {
                if (event->key.key == VG_KEY_ENTER || event->key.key == VG_KEY_SPACE ||
                    event->key.key == VG_KEY_DOWN || event->key.key == VG_KEY_UP) {
                    int preferred = dd->selected_index;
                    if (event->key.key == VG_KEY_UP && dd->item_count > 0 && preferred < 0)
                        preferred = dd->item_count - 1;
                    dropdown_open(widget, dd, preferred);
                    event->handled = true;
                    return true;
                }
                return false;
            }
            if (event->key.key == VG_KEY_ESCAPE) {
                dropdown_close(widget, dd);
                event->handled = true;
                return true;
            }
            if (event->key.key == VG_KEY_DOWN) {
                if (dd->hovered_index < dd->item_count - 1)
                    dd->hovered_index++;
                dropdown_scroll_to_index(dd, dd->hovered_index);
                widget->needs_paint = true;
                return true;
            }
            if (event->key.key == VG_KEY_UP) {
                if (dd->hovered_index > 0)
                    dd->hovered_index--;
                dropdown_scroll_to_index(dd, dd->hovered_index);
                widget->needs_paint = true;
                return true;
            }
            if (event->key.key == VG_KEY_HOME) {
                if (dd->item_count > 0)
                    dd->hovered_index = 0;
                dropdown_scroll_to_index(dd, dd->hovered_index);
                widget->needs_paint = true;
                return true;
            }
            if (event->key.key == VG_KEY_END) {
                if (dd->item_count > 0)
                    dd->hovered_index = dd->item_count - 1;
                dropdown_scroll_to_index(dd, dd->hovered_index);
                widget->needs_paint = true;
                return true;
            }
            if (event->key.key == VG_KEY_ENTER) {
                if (dd->hovered_index >= 0 && dd->hovered_index < dd->item_count)
                    vg_dropdown_set_selected(dd, dd->hovered_index);
                dropdown_close(widget, dd);
                event->handled = true;
                return true;
            }
            return false;

        default:
            return false;
    }
}

static bool dropdown_can_focus(vg_widget_t *widget) {
    return widget->enabled && widget->visible;
}

vg_dropdown_t *vg_dropdown_create(vg_widget_t *parent) {
    vg_dropdown_t *dropdown = calloc(1, sizeof(vg_dropdown_t));
    if (!dropdown)
        return NULL;
    vg_theme_t *theme = vg_theme_get_current();

    vg_widget_init(&dropdown->base, VG_WIDGET_DROPDOWN, &g_dropdown_vtable);
    dropdown->selected_index = -1;
    dropdown->hovered_index = -1;
    dropdown->item_capacity = 8;
    dropdown->items = calloc(dropdown->item_capacity, sizeof(char *));

    // Default appearance
    dropdown->font_size = 14;
    dropdown->dropdown_height = 200;
    dropdown->arrow_size = 12;
    dropdown->bg_color = theme->colors.bg_primary;
    dropdown->text_color = theme->colors.fg_primary;
    dropdown->border_color = theme->colors.border_primary;
    dropdown->dropdown_bg = theme->colors.bg_secondary;
    dropdown->hover_bg = theme->colors.bg_hover;
    dropdown->selected_bg = theme->colors.bg_selected;

    if (parent) {
        vg_widget_add_child(parent, &dropdown->base);
    }

    return dropdown;
}

/// @brief Dropdown add item.
int vg_dropdown_add_item(vg_dropdown_t *dropdown, const char *text) {
    if (!dropdown || !text)
        return -1;

    // Grow array if needed
    if (dropdown->item_count >= dropdown->item_capacity) {
        int new_cap = dropdown->item_capacity * 2;
        char **new_items = realloc(dropdown->items, new_cap * sizeof(char *));
        if (!new_items)
            return -1;
        dropdown->items = new_items;
        dropdown->item_capacity = new_cap;
    }

    dropdown->items[dropdown->item_count] = strdup(text);
    dropdown->base.needs_layout = true;
    dropdown->base.needs_paint = true;
    return dropdown->item_count++;
}

/// @brief Dropdown remove item.
void vg_dropdown_remove_item(vg_dropdown_t *dropdown, int index) {
    if (!dropdown || index < 0 || index >= dropdown->item_count)
        return;

    free(dropdown->items[index]);

    // Shift remaining items
    for (int i = index; i < dropdown->item_count - 1; i++) {
        dropdown->items[i] = dropdown->items[i + 1];
    }
    dropdown->item_count--;

    // Adjust selected index
    if (dropdown->selected_index == index) {
        dropdown->selected_index = -1;
    } else if (dropdown->selected_index > index) {
        dropdown->selected_index--;
    }
    if (dropdown->hovered_index == index) {
        dropdown->hovered_index = -1;
    } else if (dropdown->hovered_index > index) {
        dropdown->hovered_index--;
    }
    dropdown->base.needs_layout = true;
    dropdown->base.needs_paint = true;
}

/// @brief Dropdown clear.
void vg_dropdown_clear(vg_dropdown_t *dropdown) {
    if (!dropdown)
        return;

    for (int i = 0; i < dropdown->item_count; i++) {
        free(dropdown->items[i]);
    }
    dropdown->item_count = 0;
    dropdown->selected_index = -1;
    dropdown->hovered_index = -1;
    dropdown->scroll_y = 0.0f;
    dropdown->base.needs_layout = true;
    dropdown->base.needs_paint = true;
}

/// @brief Dropdown set selected.
void vg_dropdown_set_selected(vg_dropdown_t *dropdown, int index) {
    if (!dropdown)
        return;

    int old_index = dropdown->selected_index;
    if (index < -1 || index >= dropdown->item_count) {
        dropdown->selected_index = -1;
    } else {
        dropdown->selected_index = index;
    }

    if (old_index != dropdown->selected_index && dropdown->on_change) {
        dropdown->on_change(&dropdown->base,
                            dropdown->selected_index,
                            vg_dropdown_get_selected_text(dropdown),
                            dropdown->on_change_data);
    }
    dropdown->base.needs_paint = true;
}

/// @brief Dropdown get selected.
int vg_dropdown_get_selected(vg_dropdown_t *dropdown) {
    return dropdown ? dropdown->selected_index : -1;
}

const char *vg_dropdown_get_selected_text(vg_dropdown_t *dropdown) {
    if (!dropdown || dropdown->selected_index < 0 ||
        dropdown->selected_index >= dropdown->item_count) {
        return NULL;
    }
    return dropdown->items[dropdown->selected_index];
}

/// @brief Dropdown set placeholder.
void vg_dropdown_set_placeholder(vg_dropdown_t *dropdown, const char *text) {
    if (!dropdown)
        return;
    free(dropdown->placeholder);
    dropdown->placeholder = text ? strdup(text) : NULL;
    dropdown->base.needs_paint = true;
}

/// @brief Dropdown set font.
void vg_dropdown_set_font(vg_dropdown_t *dropdown, vg_font_t *font, float size) {
    if (!dropdown)
        return;
    dropdown->font = font;
    dropdown->font_size = size;
    dropdown->base.needs_layout = true;
    dropdown->base.needs_paint = true;
}

/// @brief Dropdown set on change.
void vg_dropdown_set_on_change(vg_dropdown_t *dropdown,
                               vg_dropdown_callback_t callback,
                               void *user_data) {
    if (!dropdown)
        return;
    dropdown->on_change = callback;
    dropdown->on_change_data = user_data;
}
