//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/lib/gui/src/widgets/vg_spinner.c
//
//===----------------------------------------------------------------------===//
// vg_spinner.c - Spinner/NumberInput widget implementation
#include "../../../graphics/include/vgfx.h"
#include "../../include/vg_event.h"
#include "../../include/vg_theme.h"
#include "../../include/vg_widgets.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void spinner_destroy(vg_widget_t *widget);
static void spinner_measure(vg_widget_t *widget, float available_width, float available_height);
static void spinner_paint(vg_widget_t *widget, void *canvas);
static bool spinner_handle_event(vg_widget_t *widget, vg_event_t *event);
static bool spinner_can_focus(vg_widget_t *widget);
static void update_text_buffer(vg_spinner_t *spinner);

static vg_widget_vtable_t g_spinner_vtable = {.destroy = spinner_destroy,
                                              .measure = spinner_measure,
                                              .arrange = NULL,
                                              .paint = spinner_paint,
                                              .handle_event = spinner_handle_event,
                                              .can_focus = spinner_can_focus,
                                              .on_focus = NULL};

static void spinner_adjust_value(vg_spinner_t *spinner, double delta) {
    if (!spinner)
        return;
    vg_spinner_set_value(spinner, spinner->value + delta);
}

static bool spinner_point_in_up_button(const vg_spinner_t *spinner, float x, float y) {
    return x >= spinner->base.width - spinner->button_width && x < spinner->base.width && y >= 0.0f &&
           y < spinner->base.height * 0.5f;
}

static bool spinner_point_in_down_button(const vg_spinner_t *spinner, float x, float y) {
    return x >= spinner->base.width - spinner->button_width && x < spinner->base.width &&
           y >= spinner->base.height * 0.5f && y < spinner->base.height;
}

vg_spinner_t *vg_spinner_create(vg_widget_t *parent) {
    vg_spinner_t *spinner = calloc(1, sizeof(vg_spinner_t));
    if (!spinner)
        return NULL;

    vg_widget_init(&spinner->base, VG_WIDGET_SPINNER, &g_spinner_vtable);

    spinner->min_value = 0;
    spinner->max_value = 100;
    spinner->value = 0;
    spinner->step = 1;
    spinner->decimal_places = 0;

    spinner->text_buffer = malloc(64);
    if (spinner->text_buffer) {
        snprintf(spinner->text_buffer, 64, "%.0f", spinner->value);
    }

    vg_theme_t *theme = vg_theme_get_current();
    spinner->font_size = theme->typography.size_normal;
    spinner->button_width = 24;
    spinner->bg_color = theme->colors.bg_primary;
    spinner->text_color = theme->colors.fg_primary;
    spinner->border_color = theme->colors.border_primary;
    spinner->button_color = theme->colors.bg_tertiary;

    spinner->base.constraints.min_width = 80.0f;
    spinner->base.constraints.min_height = theme->input.height;

    if (parent) {
        vg_widget_add_child(parent, &spinner->base);
    }

    return spinner;
}

static void spinner_destroy(vg_widget_t *widget) {
    vg_spinner_t *spinner = (vg_spinner_t *)widget;
    free(spinner->text_buffer);
    spinner->text_buffer = NULL;
    if (vg_widget_get_input_capture() == widget)
        vg_widget_release_input_capture();
}

static void spinner_measure(vg_widget_t *widget, float available_width, float available_height) {
    vg_spinner_t *spinner = (vg_spinner_t *)widget;
    (void)available_width;
    (void)available_height;

    float width = widget->constraints.min_width > 0.0f ? widget->constraints.min_width : 80.0f;
    float height =
        widget->constraints.min_height > 0.0f ? widget->constraints.min_height : 28.0f;

    if (spinner->font && spinner->text_buffer) {
        vg_text_metrics_t metrics = {0};
        vg_font_measure_text(spinner->font, spinner->font_size, spinner->text_buffer, &metrics);
        width = metrics.width + spinner->button_width + 18.0f;
        if (width < widget->constraints.min_width)
            width = widget->constraints.min_width;
        height = spinner->font_size + 12.0f;
        if (height < widget->constraints.min_height)
            height = widget->constraints.min_height;
    }

    widget->measured_width = width;
    widget->measured_height = height;
}

static void spinner_paint(vg_widget_t *widget, void *canvas) {
    vg_spinner_t *spinner = (vg_spinner_t *)widget;
    vg_theme_t *theme = vg_theme_get_current();

    const int32_t x = (int32_t)widget->x;
    const int32_t y = (int32_t)widget->y;
    const int32_t w = (int32_t)widget->width;
    const int32_t h = (int32_t)widget->height;
    const int32_t button_x = (int32_t)(widget->x + widget->width - spinner->button_width);
    const int32_t button_w = (int32_t)spinner->button_width;
    const int32_t half_h = h / 2;
    const int32_t up_mid_x = button_x + button_w / 2;
    const int32_t up_mid_y = y + half_h / 2;
    const int32_t down_mid_y = y + half_h + (h - half_h) / 2;

    const uint32_t border =
        (widget->state & VG_STATE_FOCUSED) ? theme->colors.border_focus : spinner->border_color;

    vgfx_fill_rect((vgfx_window_t)canvas, x, y, w, h, spinner->bg_color);
    vgfx_fill_rect((vgfx_window_t)canvas,
                   button_x,
                   y,
                   button_w,
                   half_h,
                   spinner->up_pressed   ? theme->colors.bg_active
                   : spinner->up_hovered ? theme->colors.bg_hover
                                         : spinner->button_color);
    vgfx_fill_rect((vgfx_window_t)canvas,
                   button_x,
                   y + half_h,
                   button_w,
                   h - half_h,
                   spinner->down_pressed   ? theme->colors.bg_active
                   : spinner->down_hovered ? theme->colors.bg_hover
                                           : spinner->button_color);

    vgfx_rect((vgfx_window_t)canvas, x, y, w, h, border);
    vgfx_line((vgfx_window_t)canvas, button_x, y, button_x, y + h, spinner->border_color);
    vgfx_line((vgfx_window_t)canvas,
              button_x,
              y + half_h,
              button_x + button_w,
              y + half_h,
              spinner->border_color);

    if (spinner->font && spinner->text_buffer) {
        vg_font_metrics_t metrics;
        vg_font_get_metrics(spinner->font, spinner->font_size, &metrics);

        vg_text_metrics_t text_metrics = {0};
        vg_font_measure_text(spinner->font, spinner->font_size, spinner->text_buffer, &text_metrics);

        const float text_area_w = widget->width - spinner->button_width;
        const float text_x = widget->x + (text_area_w - text_metrics.width) * 0.5f;
        const float text_y = widget->y +
                             (widget->height - (metrics.ascent - metrics.descent)) * 0.5f +
                             metrics.ascent;
        vg_font_draw_text(
            canvas, spinner->font, spinner->font_size, text_x, text_y, spinner->text_buffer, spinner->text_color);
    }

    const uint32_t glyph = theme->colors.fg_primary;
    vgfx_line((vgfx_window_t)canvas, up_mid_x - 4, up_mid_y + 2, up_mid_x, up_mid_y - 2, glyph);
    vgfx_line((vgfx_window_t)canvas, up_mid_x, up_mid_y - 2, up_mid_x + 4, up_mid_y + 2, glyph);
    vgfx_line((vgfx_window_t)canvas,
              up_mid_x - 4,
              down_mid_y - 2,
              up_mid_x,
              down_mid_y + 2,
              glyph);
    vgfx_line((vgfx_window_t)canvas,
              up_mid_x,
              down_mid_y + 2,
              up_mid_x + 4,
              down_mid_y - 2,
              glyph);
}

static bool spinner_handle_event(vg_widget_t *widget, vg_event_t *event) {
    vg_spinner_t *spinner = (vg_spinner_t *)widget;
    if (widget->state & VG_STATE_DISABLED)
        return false;

    switch (event->type) {
        case VG_EVENT_MOUSE_MOVE: {
            const bool old_up_hovered = spinner->up_hovered;
            const bool old_down_hovered = spinner->down_hovered;
            spinner->up_hovered = spinner_point_in_up_button(spinner, event->mouse.x, event->mouse.y);
            spinner->down_hovered =
                spinner_point_in_down_button(spinner, event->mouse.x, event->mouse.y);
            if (old_up_hovered != spinner->up_hovered ||
                old_down_hovered != spinner->down_hovered) {
                widget->needs_paint = true;
            }
            return spinner->up_pressed || spinner->down_pressed;
        }

        case VG_EVENT_MOUSE_LEAVE:
            if (!spinner->up_pressed && !spinner->down_pressed) {
                spinner->up_hovered = false;
                spinner->down_hovered = false;
                widget->needs_paint = true;
            }
            return false;

        case VG_EVENT_MOUSE_DOWN:
            if (spinner_point_in_up_button(spinner, event->mouse.x, event->mouse.y)) {
                spinner->up_pressed = true;
                spinner->down_pressed = false;
                vg_widget_set_input_capture(widget);
                spinner_adjust_value(spinner, spinner->step);
                widget->needs_paint = true;
                return true;
            }
            if (spinner_point_in_down_button(spinner, event->mouse.x, event->mouse.y)) {
                spinner->down_pressed = true;
                spinner->up_pressed = false;
                vg_widget_set_input_capture(widget);
                spinner_adjust_value(spinner, -spinner->step);
                widget->needs_paint = true;
                return true;
            }
            return true;

        case VG_EVENT_MOUSE_UP:
            if (spinner->up_pressed || spinner->down_pressed) {
                spinner->up_pressed = false;
                spinner->down_pressed = false;
                if (vg_widget_get_input_capture() == widget)
                    vg_widget_release_input_capture();
                widget->needs_paint = true;
                return true;
            }
            return false;

        case VG_EVENT_MOUSE_WHEEL:
            if (event->wheel.delta_y > 0.0f) {
                spinner_adjust_value(spinner, spinner->step);
                return true;
            }
            if (event->wheel.delta_y < 0.0f) {
                spinner_adjust_value(spinner, -spinner->step);
                return true;
            }
            return false;

        case VG_EVENT_KEY_DOWN:
            if (event->key.key == VG_KEY_UP) {
                spinner_adjust_value(spinner, spinner->step);
                return true;
            }
            if (event->key.key == VG_KEY_DOWN) {
                spinner_adjust_value(spinner, -spinner->step);
                return true;
            }
            return false;

        default:
            return false;
    }
}

static bool spinner_can_focus(vg_widget_t *widget) {
    return widget->enabled && widget->visible;
}

static void update_text_buffer(vg_spinner_t *spinner) {
    if (!spinner || !spinner->text_buffer)
        return;
    if (spinner->decimal_places <= 0) {
        snprintf(spinner->text_buffer, 64, "%.0f", spinner->value);
    } else {
        snprintf(spinner->text_buffer, 64, "%.*f", spinner->decimal_places, spinner->value);
    }
}

/// @brief Spinner set value.
void vg_spinner_set_value(vg_spinner_t *spinner, double value) {
    if (!spinner)
        return;

    if (value < spinner->min_value)
        value = spinner->min_value;
    if (value > spinner->max_value)
        value = spinner->max_value;

    double old = spinner->value;
    spinner->value = value;
    update_text_buffer(spinner);
    spinner->base.needs_paint = true;

    if (old != value && spinner->on_change) {
        spinner->on_change(&spinner->base, value, spinner->on_change_data);
    }
}

/// @brief Spinner get value.
double vg_spinner_get_value(vg_spinner_t *spinner) {
    return spinner ? spinner->value : 0;
}

/// @brief Spinner set range.
void vg_spinner_set_range(vg_spinner_t *spinner, double min_val, double max_val) {
    if (!spinner)
        return;
    spinner->min_value = min_val;
    spinner->max_value = max_val;
    vg_spinner_set_value(spinner, spinner->value);
}

/// @brief Spinner set step.
void vg_spinner_set_step(vg_spinner_t *spinner, double step) {
    if (!spinner)
        return;
    spinner->step = step > 0 ? step : 1;
    spinner->base.needs_paint = true;
}

/// @brief Spinner set decimals.
void vg_spinner_set_decimals(vg_spinner_t *spinner, int decimals) {
    if (!spinner)
        return;
    spinner->decimal_places = decimals > 0 ? decimals : 0;
    update_text_buffer(spinner);
    spinner->base.needs_layout = true;
    spinner->base.needs_paint = true;
}

/// @brief Spinner set font.
void vg_spinner_set_font(vg_spinner_t *spinner, vg_font_t *font, float size) {
    if (!spinner)
        return;
    spinner->font = font;
    spinner->font_size = size;
    spinner->base.needs_layout = true;
    spinner->base.needs_paint = true;
}

/// @brief Spinner set on change.
void vg_spinner_set_on_change(vg_spinner_t *spinner,
                              vg_spinner_callback_t callback,
                              void *user_data) {
    if (!spinner)
        return;
    spinner->on_change = callback;
    spinner->on_change_data = user_data;
}
