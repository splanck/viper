//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/lib/gui/src/widgets/vg_breadcrumb.c
//
//===----------------------------------------------------------------------===//
// vg_breadcrumb.c - Breadcrumb widget implementation
#include "../../../graphics/include/vgfx.h"
#include "../../include/vg_event.h"
#include "../../include/vg_ide_widgets.h"
#include "../../include/vg_theme.h"
#include <stdlib.h>
#include <string.h>

//=============================================================================
// Forward Declarations
//=============================================================================

static void breadcrumb_destroy(vg_widget_t *widget);
static void breadcrumb_measure(vg_widget_t *widget, float available_width, float available_height);
static void breadcrumb_paint(vg_widget_t *widget, void *canvas);
static bool breadcrumb_handle_event(vg_widget_t *widget, vg_event_t *event);
static void breadcrumb_set_font_widget(vg_widget_t *widget, void *font, float size);
static void breadcrumb_fill_round_rect(vgfx_window_t win,
                                       int32_t x,
                                       int32_t y,
                                       int32_t w,
                                       int32_t h,
                                       int32_t radius,
                                       uint32_t color);
static void breadcrumb_stroke_round_rect(vgfx_window_t win,
                                         int32_t x,
                                         int32_t y,
                                         int32_t w,
                                         int32_t h,
                                         int32_t radius,
                                         uint32_t color);
static float breadcrumb_outer_padding(const vg_breadcrumb_t *bc);
static bool breadcrumb_item_rect(const vg_breadcrumb_t *bc,
                                 int index,
                                 float *out_x,
                                 float *out_y,
                                 float *out_w,
                                 float *out_h);
static bool breadcrumb_dropdown_rect(const vg_breadcrumb_t *bc,
                                     float *out_x,
                                     float *out_y,
                                     float *out_w,
                                     float *out_h);
static int breadcrumb_find_dropdown_item_at(const vg_breadcrumb_t *bc, float px, float py);

//=============================================================================
// Breadcrumb VTable
//=============================================================================

static vg_widget_vtable_t g_breadcrumb_vtable = {.destroy = breadcrumb_destroy,
                                                 .measure = breadcrumb_measure,
                                                 .arrange = NULL,
                                                 .paint = breadcrumb_paint,
                                                 .handle_event = breadcrumb_handle_event,
                                                 .can_focus = NULL,
                                                 .on_focus = NULL,
                                                 .set_font = breadcrumb_set_font_widget};

//=============================================================================
// Breadcrumb Item Management
//=============================================================================

static void free_breadcrumb_item(vg_breadcrumb_item_t *item) {
    if (!item)
        return;

    free(item->label);
    free(item->tooltip);
    if (item->owns_user_data)
        free(item->user_data);

    for (size_t i = 0; i < item->dropdown_count; i++) {
        free(item->dropdown_items[i].label);
    }
    free(item->dropdown_items);
}

static void breadcrumb_fill_round_rect(vgfx_window_t win,
                                       int32_t x,
                                       int32_t y,
                                       int32_t w,
                                       int32_t h,
                                       int32_t radius,
                                       uint32_t color) {
    if (w <= 0 || h <= 0)
        return;

    int32_t max_radius = w < h ? w / 2 : h / 2;
    if (radius <= 0 || max_radius <= 0) {
        vgfx_fill_rect(win, x, y, w, h, color);
        return;
    }
    if (radius > max_radius)
        radius = max_radius;
    if (w <= radius * 2 || h <= radius * 2) {
        vgfx_fill_rect(win, x, y, w, h, color);
        return;
    }

    vgfx_fill_rect(win, x + radius, y, w - radius * 2, h, color);
    vgfx_fill_rect(win, x, y + radius, radius, h - radius * 2, color);
    vgfx_fill_rect(win, x + w - radius, y + radius, radius, h - radius * 2, color);
    vgfx_fill_circle(win, x + radius, y + radius, radius, color);
    vgfx_fill_circle(win, x + w - radius - 1, y + radius, radius, color);
    vgfx_fill_circle(win, x + radius, y + h - radius - 1, radius, color);
    vgfx_fill_circle(win, x + w - radius - 1, y + h - radius - 1, radius, color);
}

static void breadcrumb_stroke_round_rect(vgfx_window_t win,
                                         int32_t x,
                                         int32_t y,
                                         int32_t w,
                                         int32_t h,
                                         int32_t radius,
                                         uint32_t color) {
    if (w <= 1 || h <= 1)
        return;

    int32_t max_radius = w < h ? w / 2 : h / 2;
    if (radius <= 0 || max_radius <= 0) {
        vgfx_rect(win, x, y, w, h, color);
        return;
    }
    if (radius > max_radius)
        radius = max_radius;
    if (w <= radius * 2 || h <= radius * 2) {
        vgfx_rect(win, x, y, w, h, color);
        return;
    }

    vgfx_line(win, x + radius, y, x + w - radius - 1, y, color);
    vgfx_line(win, x + radius, y + h - 1, x + w - radius - 1, y + h - 1, color);
    vgfx_line(win, x, y + radius, x, y + h - radius - 1, color);
    vgfx_line(win, x + w - 1, y + radius, x + w - 1, y + h - radius - 1, color);
    vgfx_circle(win, x + radius, y + radius, radius, color);
    vgfx_circle(win, x + w - radius - 1, y + radius, radius, color);
    vgfx_circle(win, x + radius, y + h - radius - 1, radius, color);
    vgfx_circle(win, x + w - radius - 1, y + h - radius - 1, radius, color);
}

static float breadcrumb_outer_padding(const vg_breadcrumb_t *bc) {
    if (!bc)
        return 4.0f;
    return (float)bc->separator_padding + 2.0f;
}

static bool breadcrumb_item_rect(const vg_breadcrumb_t *bc,
                                 int index,
                                 float *out_x,
                                 float *out_y,
                                 float *out_w,
                                 float *out_h) {
    float x = 0.0f;
    float item_y = 0.0f;
    float item_h = 0.0f;

    if (!bc || !bc->font || index < 0 || (size_t)index >= bc->item_count)
        return false;

    x = bc->base.x + breadcrumb_outer_padding(bc);
    item_y = bc->base.y + 4.0f;
    item_h = bc->base.height - 8.0f;
    if (item_h < 12.0f)
        item_h = bc->base.height;

    for (size_t i = 0; i < bc->item_count; i++) {
        vg_text_metrics_t metrics = {0};
        float item_width = 0.0f;
        vg_font_measure_text(bc->font, bc->font_size, bc->items[i].label, &metrics);
        item_width = metrics.width + bc->item_padding * 2;
        if ((int)i == index) {
            if (out_x)
                *out_x = x;
            if (out_y)
                *out_y = item_y;
            if (out_w)
                *out_w = item_width;
            if (out_h)
                *out_h = item_h;
            return true;
        }

        x += item_width;
        if (i < bc->item_count - 1 && bc->separator) {
            vg_font_measure_text(bc->font, bc->font_size, bc->separator, &metrics);
            x += metrics.width + bc->separator_padding * 2;
        }
    }

    return false;
}

static bool breadcrumb_dropdown_rect(const vg_breadcrumb_t *bc,
                                     float *out_x,
                                     float *out_y,
                                     float *out_w,
                                     float *out_h) {
    vg_breadcrumb_item_t *item = NULL;
    float crumb_x = 0.0f;
    float crumb_y = 0.0f;
    float crumb_w = 0.0f;
    float crumb_h = 0.0f;
    float dropdown_w = 0.0f;
    float dropdown_h = 0.0f;

    if (!bc || !bc->dropdown_open || bc->dropdown_index < 0 ||
        (size_t)bc->dropdown_index >= bc->item_count) {
        return false;
    }

    item = &bc->items[bc->dropdown_index];
    if (item->dropdown_count == 0 || !breadcrumb_item_rect(
                                         bc, bc->dropdown_index, &crumb_x, &crumb_y, &crumb_w, &crumb_h)) {
        return false;
    }

    dropdown_w = crumb_w > 160.0f ? crumb_w : 160.0f;
    dropdown_h = (float)item->dropdown_count * (crumb_h > 24.0f ? crumb_h : 24.0f);

    if (out_x)
        *out_x = crumb_x;
    if (out_y)
        *out_y = crumb_y + crumb_h + 4.0f;
    if (out_w)
        *out_w = dropdown_w;
    if (out_h)
        *out_h = dropdown_h;
    return true;
}

static int breadcrumb_find_dropdown_item_at(const vg_breadcrumb_t *bc, float px, float py) {
    vg_breadcrumb_item_t *item = NULL;
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
    float row_h = 0.0f;

    if (!bc || !breadcrumb_dropdown_rect(bc, &x, &y, &w, &h))
        return -1;

    item = &bc->items[bc->dropdown_index];
    row_h = item->dropdown_count > 0 ? h / (float)item->dropdown_count : 0.0f;
    if (row_h <= 0.0f || px < x || px >= x + w || py < y || py >= y + h)
        return -1;

    return (int)((py - y) / row_h);
}

//=============================================================================
// Breadcrumb Implementation
//=============================================================================

vg_breadcrumb_t *vg_breadcrumb_create(void) {
    vg_breadcrumb_t *bc = calloc(1, sizeof(vg_breadcrumb_t));
    if (!bc)
        return NULL;

    vg_widget_init(&bc->base, VG_WIDGET_CUSTOM, &g_breadcrumb_vtable);

    vg_theme_t *theme = vg_theme_get_current();

    // Defaults
    bc->separator = strdup(">");
    bc->item_padding = theme ? (uint32_t)(theme->spacing.sm + 4.0f) : 8;
    bc->separator_padding = theme ? (uint32_t)(theme->spacing.sm + 1.0f) : 4;
    bc->bg_color = 0;
    bc->text_color = 0;
    bc->hover_bg = 0;
    bc->separator_color = 0;

    bc->font_size = theme ? theme->typography.size_normal : 13.0f;
    bc->hovered_index = -1;
    bc->dropdown_index = -1;
    bc->dropdown_hovered = -1;

    return bc;
}

static void breadcrumb_destroy(vg_widget_t *widget) {
    vg_breadcrumb_t *bc = (vg_breadcrumb_t *)widget;

    for (size_t i = 0; i < bc->item_count; i++) {
        free_breadcrumb_item(&bc->items[i]);
    }
    free(bc->items);
    free(bc->separator);
}

/// @brief Breadcrumb destroy.
void vg_breadcrumb_destroy(vg_breadcrumb_t *bc) {
    if (!bc)
        return;
    vg_widget_destroy(&bc->base);
}

static void breadcrumb_measure(vg_widget_t *widget, float available_width, float available_height) {
    vg_breadcrumb_t *bc = (vg_breadcrumb_t *)widget;
    vg_theme_t *theme = vg_theme_get_current();
    (void)available_width;
    (void)available_height;

    float width = breadcrumb_outer_padding(bc) * 2.0f;
    float height = 0;

    if (!bc->font) {
        widget->measured_width = 0;
        widget->measured_height = theme ? theme->input.height : 28.0f;
        return;
    }

    for (size_t i = 0; i < bc->item_count; i++) {
        vg_breadcrumb_item_t *item = &bc->items[i];

        vg_text_metrics_t metrics;
        vg_font_measure_text(bc->font, bc->font_size, item->label, &metrics);

        width += metrics.width + bc->item_padding * 2;
        if (metrics.height > height)
            height = metrics.height;

        // Add separator width
        if (i < bc->item_count - 1 && bc->separator) {
            vg_font_measure_text(bc->font, bc->font_size, bc->separator, &metrics);
            width += metrics.width + bc->separator_padding * 2;
        }
    }

    if (theme && height + theme->spacing.sm * 2.0f < theme->input.height) {
        height = theme->input.height - theme->spacing.sm * 2.0f;
    }
    widget->measured_width = width;
    widget->measured_height = height + (theme ? theme->spacing.sm * 2.0f : 8.0f);
}

static void breadcrumb_paint(vg_widget_t *widget, void *canvas) {
    vg_breadcrumb_t *bc = (vg_breadcrumb_t *)widget;
    vg_theme_t *theme = vg_theme_get_current();
    vgfx_window_t win = (vgfx_window_t)canvas;

    if (!bc->font || widget->width <= 0.0f || widget->height <= 0.0f)
        return;

    float outer_padding = breadcrumb_outer_padding(bc);
    float x = widget->x + outer_padding;
    float item_y = widget->y + 4.0f;
    float item_h = widget->height - 8.0f;
    if (item_h < 12.0f)
        item_h = widget->height;

    vg_font_metrics_t font_metrics = {0};
    vg_font_get_metrics(bc->font, bc->font_size, &font_metrics);
    int radius = theme ? (int)theme->button.border_radius : 6;
    if (radius < 4)
        radius = 4;

    uint32_t strip_bg = theme ? vg_color_blend(theme->colors.bg_secondary, theme->colors.bg_primary, 0.45f)
                              : 0x252526u;
    uint32_t strip_border = theme ? theme->colors.border_secondary : 0x3C3C3Cu;
    uint32_t strip_highlight =
        theme ? vg_color_lighten(strip_bg, 0.06f) : vg_color_lighten(strip_bg, 0.06f);
    uint32_t accent = theme ? theme->colors.accent_primary : strip_border;
    uint32_t text_color = bc->text_color ? bc->text_color
                                         : (theme ? theme->colors.fg_secondary : 0xD0D0D0u);
    uint32_t current_text = theme ? theme->colors.fg_primary : 0xFFFFFFu;
    uint32_t hover_bg = bc->hover_bg ? bc->hover_bg
                                     : (theme ? vg_color_blend(theme->colors.bg_hover,
                                                               theme->colors.bg_selected,
                                                               0.25f)
                                              : 0x2F3440u);
    uint32_t active_bg = theme ? vg_color_blend(theme->colors.bg_selected,
                                                theme->colors.accent_primary,
                                                0.18f)
                               : 0x334C73u;
    uint32_t separator_color = bc->separator_color
                                   ? bc->separator_color
                                   : (theme ? theme->colors.fg_tertiary : 0x8A8A8Au);

    breadcrumb_fill_round_rect(win,
                               (int32_t)widget->x,
                               (int32_t)widget->y,
                               (int32_t)widget->width,
                               (int32_t)widget->height,
                               radius,
                               bc->bg_color ? bc->bg_color : strip_bg);
    breadcrumb_stroke_round_rect(
        win, (int32_t)widget->x, (int32_t)widget->y, (int32_t)widget->width, (int32_t)widget->height, radius, strip_border);
    if (widget->width > radius * 2.0f) {
        vgfx_fill_rect(win,
                       (int32_t)(widget->x + radius),
                       (int32_t)(widget->y + 1.0f),
                       (int32_t)(widget->width - radius * 2.0f),
                       1,
                       strip_highlight);
        vgfx_fill_rect(win,
                       (int32_t)(widget->x + radius),
                       (int32_t)(widget->y + 1.0f),
                       (int32_t)(widget->width - radius * 2.0f),
                       2,
                       accent);
    }

    vgfx_set_clip(
        win, (int32_t)widget->x, (int32_t)widget->y, (int32_t)widget->width, (int32_t)widget->height);

    for (size_t i = 0; i < bc->item_count; i++) {
        vg_breadcrumb_item_t *item = &bc->items[i];

        vg_text_metrics_t metrics;
        vg_font_measure_text(bc->font, bc->font_size, item->label, &metrics);
        float item_width = metrics.width + bc->item_padding * 2;
        float baseline_y = item_y + (item_h - metrics.height) * 0.5f + font_metrics.ascent;
        bool is_current = i == bc->item_count - 1;
        bool is_hovered = (int)i == bc->hovered_index;

        if (is_current || is_hovered) {
            uint32_t pill_bg = is_current ? active_bg : hover_bg;
            if (is_current && is_hovered)
                pill_bg = vg_color_lighten(pill_bg, 0.08f);
            breadcrumb_fill_round_rect(win,
                                       (int32_t)x,
                                       (int32_t)item_y,
                                       (int32_t)item_width,
                                       (int32_t)item_h,
                                       radius - 2,
                                       pill_bg);
        }

        uint32_t crumb_text = is_current ? current_text : text_color;
        vg_font_draw_text(
            canvas, bc->font, bc->font_size, x + bc->item_padding, baseline_y, item->label, crumb_text);

        x += item_width;

        if (i < bc->item_count - 1 && bc->separator) {
            x += bc->separator_padding;
            float sep_y = item_y + (item_h - metrics.height) * 0.5f + font_metrics.ascent;
            vg_font_draw_text(
                canvas, bc->font, bc->font_size, x, sep_y, bc->separator, separator_color);
            vg_font_measure_text(bc->font, bc->font_size, bc->separator, &metrics);
            x += metrics.width + bc->separator_padding;
        }

        if (x > widget->x + widget->width)
            break;
    }
    vgfx_clear_clip(win);

    // Draw dropdown if open
    if (bc->dropdown_open && bc->dropdown_index >= 0 && bc->dropdown_index < (int)bc->item_count) {
        vg_breadcrumb_item_t *item = &bc->items[bc->dropdown_index];
        float dd_x = 0.0f;
        float dd_y = 0.0f;
        float dd_w = 0.0f;
        float dd_h = 0.0f;
        if (breadcrumb_dropdown_rect(bc, &dd_x, &dd_y, &dd_w, &dd_h)) {
            float row_h = item->dropdown_count > 0 ? dd_h / (float)item->dropdown_count : 0.0f;
            uint32_t dd_bg =
                theme ? vg_color_blend(theme->colors.bg_primary, theme->colors.bg_secondary, 0.22f)
                      : 0x1F2633u;
            uint32_t dd_border = theme ? theme->colors.border_primary : 0x3C4658u;
            uint32_t dd_hover = theme ? theme->colors.bg_hover : 0x2F3440u;

            breadcrumb_fill_round_rect(
                win, (int32_t)dd_x, (int32_t)dd_y, (int32_t)dd_w, (int32_t)dd_h, radius, dd_bg);
            breadcrumb_stroke_round_rect(
                win, (int32_t)dd_x, (int32_t)dd_y, (int32_t)dd_w, (int32_t)dd_h, radius, dd_border);
            vgfx_set_clip(win, (int32_t)dd_x, (int32_t)dd_y, (int32_t)dd_w, (int32_t)dd_h);

            for (size_t i = 0; i < item->dropdown_count; i++) {
                float row_y = dd_y + row_h * (float)i;
                vg_font_metrics_t dd_metrics = {0};
                vg_font_get_metrics(bc->font, bc->font_size, &dd_metrics);
                if ((int)i == bc->dropdown_hovered) {
                    vgfx_fill_rect(
                        win, (int32_t)dd_x, (int32_t)row_y, (int32_t)dd_w, (int32_t)row_h, dd_hover);
                }
                vg_font_draw_text(canvas,
                                  bc->font,
                                  bc->font_size,
                                  dd_x + (float)bc->item_padding,
                                  row_y + (row_h - bc->font_size) * 0.5f + dd_metrics.ascent,
                                  item->dropdown_items[i].label,
                                  theme ? theme->colors.fg_primary : 0xFFFFFFu);
            }

            vgfx_clear_clip(win);
        }
    }
}

static int find_item_at(vg_breadcrumb_t *bc, float px, float py) {
    if (!bc->font)
        return -1;
    if (py < bc->base.y || py > bc->base.y + bc->base.height)
        return -1;

    float x = bc->base.x + breadcrumb_outer_padding(bc);

    for (size_t i = 0; i < bc->item_count; i++) {
        vg_breadcrumb_item_t *item = &bc->items[i];

        vg_text_metrics_t metrics;
        vg_font_measure_text(bc->font, bc->font_size, item->label, &metrics);

        float item_width = metrics.width + bc->item_padding * 2;

        if (px >= x && px < x + item_width) {
            return (int)i;
        }

        x += item_width;

        // Skip separator
        if (i < bc->item_count - 1 && bc->separator) {
            vg_font_measure_text(bc->font, bc->font_size, bc->separator, &metrics);
            x += metrics.width + bc->separator_padding * 2;
        }
    }

    return -1;
}

static bool breadcrumb_handle_event(vg_widget_t *widget, vg_event_t *event) {
    vg_breadcrumb_t *bc = (vg_breadcrumb_t *)widget;

    switch (event->type) {
        case VG_EVENT_MOUSE_MOVE: {
            if (bc->dropdown_open) {
                int dd_idx = breadcrumb_find_dropdown_item_at(bc, event->mouse.x, event->mouse.y);
                if (dd_idx != bc->dropdown_hovered) {
                    bc->dropdown_hovered = dd_idx;
                    bc->base.needs_paint = true;
                }
                if (dd_idx >= 0)
                    return true;
            }
            int idx = find_item_at(bc, event->mouse.x, event->mouse.y);
            if (idx != bc->hovered_index) {
                bc->hovered_index = idx;
                bc->base.needs_paint = true;
            }
            return true;
        }

        case VG_EVENT_MOUSE_LEAVE:
            bc->hovered_index = -1;
            bc->base.needs_paint = true;
            return true;

        case VG_EVENT_CLICK: {
            if (bc->dropdown_open) {
                int dd_idx = breadcrumb_find_dropdown_item_at(bc, event->mouse.x, event->mouse.y);
                if (dd_idx >= 0 && bc->dropdown_index >= 0 &&
                    (size_t)bc->dropdown_index < bc->item_count) {
                    if (bc->on_dropdown_select) {
                        bc->on_dropdown_select(bc, bc->dropdown_index, dd_idx, bc->user_data);
                    }
                    bc->dropdown_open = false;
                    bc->dropdown_hovered = -1;
                    if (vg_widget_get_input_capture() == widget)
                        vg_widget_release_input_capture();
                    bc->base.needs_paint = true;
                    return true;
                }
            }

            int idx = find_item_at(bc, event->mouse.x, event->mouse.y);
            if (idx >= 0) {
                vg_breadcrumb_item_t *item = &bc->items[idx];

                // Check if item has dropdown
                if (item->dropdown_count > 0) {
                    bc->dropdown_open = !(bc->dropdown_open && bc->dropdown_index == idx);
                    bc->dropdown_index = idx;
                    bc->dropdown_hovered = -1;
                    if (bc->dropdown_open)
                        vg_widget_set_input_capture(widget);
                    else if (vg_widget_get_input_capture() == widget)
                        vg_widget_release_input_capture();
                    bc->base.needs_paint = true;
                } else {
                    // Regular click
                    bc->dropdown_open = false;
                    if (vg_widget_get_input_capture() == widget)
                        vg_widget_release_input_capture();
                    if (bc->on_click) {
                        bc->on_click(bc, idx, bc->user_data);
                    }
                }
                return true;
            }

            if (bc->dropdown_open) {
                bc->dropdown_open = false;
                bc->dropdown_hovered = -1;
                if (vg_widget_get_input_capture() == widget)
                    vg_widget_release_input_capture();
                bc->base.needs_paint = true;
                return true;
            }
            break;
        }

        default:
            break;
    }

    return false;
}

static void breadcrumb_set_font_widget(vg_widget_t *widget, void *font, float size) {
    if (!widget || !font)
        return;
    vg_breadcrumb_set_font((vg_breadcrumb_t *)widget, (vg_font_t *)font, size);
}

/// @brief Breadcrumb push.
void vg_breadcrumb_push(vg_breadcrumb_t *bc, const char *label, void *data) {
    if (!bc || !label)
        return;

    // Expand capacity if needed
    if (bc->item_count >= bc->item_capacity) {
        size_t new_cap = bc->item_capacity * 2;
        if (new_cap < 8)
            new_cap = 8;
        vg_breadcrumb_item_t *new_items =
            realloc(bc->items, new_cap * sizeof(vg_breadcrumb_item_t));
        if (!new_items)
            return;
        bc->items = new_items;
        bc->item_capacity = new_cap;
    }

    /* Enforce max_items: remove oldest (index 0) when limit exceeded */
    if (bc->max_items > 0 && (int)bc->item_count >= bc->max_items) {
        free_breadcrumb_item(&bc->items[0]);
        memmove(&bc->items[0], &bc->items[1], (bc->item_count - 1) * sizeof(vg_breadcrumb_item_t));
        bc->item_count--;
    }

    vg_breadcrumb_item_t *item = &bc->items[bc->item_count++];
    memset(item, 0, sizeof(vg_breadcrumb_item_t));
    item->label = strdup(label);
    item->user_data = data;
    item->owns_user_data = false;

    bc->base.needs_layout = true;
    bc->base.needs_paint = true;
}

/// @brief Breadcrumb pop.
void vg_breadcrumb_pop(vg_breadcrumb_t *bc) {
    if (!bc || bc->item_count == 0)
        return;

    bc->item_count--;
    free_breadcrumb_item(&bc->items[bc->item_count]);

    bc->base.needs_layout = true;
    bc->base.needs_paint = true;
}

/// @brief Breadcrumb clear.
void vg_breadcrumb_clear(vg_breadcrumb_t *bc) {
    if (!bc)
        return;

    for (size_t i = 0; i < bc->item_count; i++) {
        free_breadcrumb_item(&bc->items[i]);
    }
    bc->item_count = 0;

    bc->dropdown_open = false;
    bc->hovered_index = -1;

    bc->base.needs_layout = true;
    bc->base.needs_paint = true;
}

/// @brief Breadcrumb item add dropdown.
void vg_breadcrumb_item_add_dropdown(vg_breadcrumb_item_t *item, const char *label, void *data) {
    if (!item || !label)
        return;

    // Expand capacity if needed
    if (item->dropdown_count >= item->dropdown_capacity) {
        size_t new_cap = item->dropdown_capacity * 2;
        if (new_cap < 4)
            new_cap = 4;
        vg_breadcrumb_dropdown_t *new_items =
            realloc(item->dropdown_items, new_cap * sizeof(vg_breadcrumb_dropdown_t));
        if (!new_items)
            return;
        item->dropdown_items = new_items;
        item->dropdown_capacity = new_cap;
    }

    vg_breadcrumb_dropdown_t *dd = &item->dropdown_items[item->dropdown_count++];
    dd->label = strdup(label);
    dd->data = data;
}

/// @brief Breadcrumb set separator.
void vg_breadcrumb_set_separator(vg_breadcrumb_t *bc, const char *sep) {
    if (!bc)
        return;

    free(bc->separator);
    bc->separator = sep ? strdup(sep) : NULL;
    bc->base.needs_layout = true;
    bc->base.needs_paint = true;
}

/// @brief Breadcrumb set on click.
void vg_breadcrumb_set_on_click(vg_breadcrumb_t *bc,
                                void (*callback)(vg_breadcrumb_t *, int, void *),
                                void *user_data) {
    if (!bc)
        return;
    bc->on_click = callback;
    bc->user_data = user_data;
}

/// @brief Breadcrumb set font.
void vg_breadcrumb_set_font(vg_breadcrumb_t *bc, vg_font_t *font, float size) {
    if (!bc)
        return;

    bc->font = font;
    bc->font_size = size;
    bc->base.needs_layout = true;
    bc->base.needs_paint = true;
}

/// @brief Breadcrumb set max items.
void vg_breadcrumb_set_max_items(vg_breadcrumb_t *bc, int max) {
    if (!bc)
        return;
    bc->max_items = max;
    /* Trim existing items if already over the limit */
    if (max > 0) {
        while ((int)bc->item_count > max && bc->item_count > 0) {
            free_breadcrumb_item(&bc->items[0]);
            memmove(
                &bc->items[0], &bc->items[1], (bc->item_count - 1) * sizeof(vg_breadcrumb_item_t));
            bc->item_count--;
        }
    }
    bc->base.needs_layout = true;
    bc->base.needs_paint = true;
}
