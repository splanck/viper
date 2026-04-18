//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/lib/gui/src/widgets/vg_slider.c
//
//===----------------------------------------------------------------------===//
// vg_slider.c - Slider widget implementation
#include "../../../graphics/include/vgfx.h"
#include "../../include/vg_event.h"
#include "../../include/vg_theme.h"
#include "../../include/vg_widgets.h"
#include <stdlib.h>

//=============================================================================
// Forward declarations
//=============================================================================

static void slider_measure(vg_widget_t *widget, float available_width, float available_height);
static void slider_arrange(vg_widget_t *widget, float x, float y, float w, float h);
static void slider_paint(vg_widget_t *widget, void *canvas);
static bool slider_handle_event(vg_widget_t *widget, vg_event_t *event);
static bool slider_can_focus(vg_widget_t *widget);

//=============================================================================
// VTable
//=============================================================================

static vg_widget_vtable_t g_slider_vtable = {
    .destroy = NULL,
    .measure = slider_measure,
    .arrange = slider_arrange,
    .paint = slider_paint,
    .handle_event = slider_handle_event,
    .can_focus = slider_can_focus,
    .on_focus = NULL,
};

//=============================================================================
// Vtable Implementations
//=============================================================================

static float slider_normalized_value(const vg_slider_t *slider) {
    float range = slider->max_value - slider->min_value;
    float norm = (range > 0.0f) ? (slider->value - slider->min_value) / range : 0.0f;
    if (norm < 0.0f)
        norm = 0.0f;
    if (norm > 1.0f)
        norm = 1.0f;
    return norm;
}

static float slider_normalized_from_point(const vg_slider_t *slider, float x, float y) {
    float thumb_r = slider->thumb_size > 0.0f ? slider->thumb_size * 0.5f : 8.0f;
    float norm = 0.0f;
    if (slider->orientation == VG_SLIDER_HORIZONTAL) {
        float track_len = slider->base.width - thumb_r * 2.0f;
        norm = track_len > 0.0f ? (x - thumb_r) / track_len : 0.0f;
    } else {
        float track_len = slider->base.height - thumb_r * 2.0f;
        norm = track_len > 0.0f ? ((slider->base.height - thumb_r) - y) / track_len : 0.0f;
    }
    if (norm < 0.0f)
        norm = 0.0f;
    if (norm > 1.0f)
        norm = 1.0f;
    return norm;
}

static void slider_measure(vg_widget_t *widget, float available_width, float available_height) {
    vg_slider_t *slider = (vg_slider_t *)widget;
    (void)available_width;
    (void)available_height;
    if (slider->orientation == VG_SLIDER_HORIZONTAL) {
        widget->measured_width = 100.0f;
        widget->measured_height = slider->thumb_size > 0 ? slider->thumb_size : 24.0f;
    } else {
        widget->measured_width = slider->thumb_size > 0 ? slider->thumb_size : 24.0f;
        widget->measured_height = 100.0f;
    }
}

static void slider_arrange(vg_widget_t *widget, float x, float y, float w, float h) {
    widget->x = x;
    widget->y = y;
    widget->width = w;
    widget->height = h;
}

static bool slider_can_focus(vg_widget_t *widget) {
    return widget->enabled && widget->visible;
}

static void slider_paint(vg_widget_t *widget, void *canvas) {
    vg_slider_t *slider = (vg_slider_t *)widget;
    vg_theme_t *theme = vg_theme_get_current();
    vgfx_window_t win = (vgfx_window_t)canvas;
    float x = widget->x, y = widget->y, w = widget->width, h = widget->height;
    float norm = slider_normalized_value(slider);
    uint32_t track_color = slider->track_color ? slider->track_color
                                               : vg_color_blend(theme->colors.bg_secondary, theme->colors.bg_primary, 0.45f);
    uint32_t fill_color = slider->fill_color ? slider->fill_color : theme->colors.accent_primary;
    uint32_t thumb_color = slider->thumb_color ? slider->thumb_color : theme->colors.bg_primary;
    if (slider->dragging) {
        thumb_color = vg_color_lighten(fill_color, 0.30f);
    } else if (slider->thumb_hovered) {
        thumb_color = slider->thumb_hover_color ? slider->thumb_hover_color
                                                : vg_color_lighten(thumb_color, 0.12f);
    }

    if (slider->orientation == VG_SLIDER_HORIZONTAL) {
        float track_th = slider->track_thickness > 0 ? slider->track_thickness : 4.0f;
        float thumb_rf = slider->thumb_size * 0.5f;
        float track_xf = x + thumb_rf;
        float track_wf = w - thumb_rf * 2.0f;
        if (track_wf < 1.0f)
            track_wf = 1.0f;
        int32_t track_y = (int32_t)(y + (h - track_th) / 2.0f);
        int32_t track_h = (int32_t)track_th;

        vgfx_fill_rect(win, (int32_t)track_xf, track_y, (int32_t)track_wf, track_h, track_color);
        vgfx_fill_rect(win,
                       (int32_t)track_xf,
                       track_y,
                       (int32_t)track_wf,
                       1,
                       vg_color_lighten(track_color, 0.08f));

        int32_t fill_w = (int32_t)(norm * track_wf);
        if (fill_w > 0)
            vgfx_fill_rect(win, (int32_t)track_xf, track_y, fill_w, track_h, fill_color);

        float thumb_cx = track_xf + norm * track_wf;
        float thumb_cy = y + h / 2.0f;
        int32_t thumb_ri = (int32_t)(slider->thumb_size / 2.0f);
        vgfx_fill_circle(win, (int32_t)thumb_cx, (int32_t)thumb_cy, thumb_ri + 1, theme->colors.border_primary);
        vgfx_fill_circle(win, (int32_t)thumb_cx, (int32_t)thumb_cy, thumb_ri, thumb_color);
        vgfx_circle(win, (int32_t)thumb_cx, (int32_t)thumb_cy, thumb_ri, vg_color_darken(thumb_color, 0.18f));
    } else {
        float track_th = slider->track_thickness > 0 ? slider->track_thickness : 4.0f;
        float thumb_rf = slider->thumb_size * 0.5f;
        float track_yf = y + thumb_rf;
        float track_hf = h - thumb_rf * 2.0f;
        if (track_hf < 1.0f)
            track_hf = 1.0f;
        int32_t track_x = (int32_t)(x + (w - track_th) / 2.0f);
        int32_t track_w = (int32_t)track_th;

        vgfx_fill_rect(win, track_x, (int32_t)track_yf, track_w, (int32_t)track_hf, track_color);
        vgfx_fill_rect(win,
                       track_x,
                       (int32_t)track_yf,
                       1,
                       (int32_t)track_hf,
                       vg_color_lighten(track_color, 0.08f));

        int32_t fill_h = (int32_t)(norm * track_hf);
        int32_t fill_y = (int32_t)(track_yf + track_hf - fill_h);
        if (fill_h > 0)
            vgfx_fill_rect(win, track_x, fill_y, track_w, fill_h, fill_color);

        float thumb_cx = x + w / 2.0f;
        float thumb_cy = track_yf + track_hf - norm * track_hf;
        int32_t thumb_ri = (int32_t)(slider->thumb_size / 2.0f);
        vgfx_fill_circle(win, (int32_t)thumb_cx, (int32_t)thumb_cy, thumb_ri + 1, theme->colors.border_primary);
        vgfx_fill_circle(win, (int32_t)thumb_cx, (int32_t)thumb_cy, thumb_ri, thumb_color);
        vgfx_circle(win, (int32_t)thumb_cx, (int32_t)thumb_cy, thumb_ri, vg_color_darken(thumb_color, 0.18f));
    }

    if (widget->state & VG_STATE_FOCUSED) {
        vgfx_rect(win, (int32_t)x, (int32_t)y, (int32_t)w, (int32_t)h, theme->colors.border_focus);
    }
}

static bool slider_handle_event(vg_widget_t *widget, vg_event_t *event) {
    vg_slider_t *slider = (vg_slider_t *)widget;
    float w = widget->width, h = widget->height;

    bool horizontal = (slider->orientation == VG_SLIDER_HORIZONTAL);
    float thumb_r = slider->thumb_size / 2.0f;

    switch (event->type) {
        case VG_EVENT_MOUSE_DOWN: {
            float norm = slider_normalized_value(slider);
            float track_w = horizontal ? (w - thumb_r * 2.0f) : w;
            float track_h = horizontal ? h : (h - thumb_r * 2.0f);
            float thumb_cx = horizontal ? (thumb_r + norm * track_w) : (w / 2.0f);
            float thumb_cy = horizontal ? (h / 2.0f) : (thumb_r + track_h - norm * track_h);
            float mx = event->mouse.x, my = event->mouse.y;
            float dx = mx - thumb_cx, dy = my - thumb_cy;
            if (dx * dx + dy * dy <= thumb_r * thumb_r) {
                slider->dragging = true;
                vg_widget_set_input_capture(widget);
                widget->needs_paint = true;
                event->handled = true;
                return true;
            }
            if (event->mouse.x >= 0.0f && event->mouse.x <= w && event->mouse.y >= 0.0f &&
                event->mouse.y <= h) {
                float click_norm = slider_normalized_from_point(slider, event->mouse.x, event->mouse.y);
                float range = slider->max_value - slider->min_value;
                vg_slider_set_value(slider, slider->min_value + click_norm * range);
                slider->dragging = true;
                slider->thumb_hovered = true;
                vg_widget_set_input_capture(widget);
                widget->needs_paint = true;
                event->handled = true;
                return true;
            }
            break;
        }

        case VG_EVENT_MOUSE_MOVE: {
            float mx = event->mouse.x, my = event->mouse.y;
            if (slider->dragging) {
                float norm = slider_normalized_from_point(slider, mx, my);
                float range = slider->max_value - slider->min_value;
                float new_val = slider->min_value + norm * range;
                vg_slider_set_value(slider, new_val);
                slider->thumb_hovered = true;
                widget->needs_paint = true;
                event->handled = true;
                return true;
            }
            float norm = slider_normalized_value(slider);
            float track_w = horizontal ? (w - thumb_r * 2.0f) : w;
            float track_h = horizontal ? h : (h - thumb_r * 2.0f);
            float thumb_cx = horizontal ? (thumb_r + norm * track_w) : (w / 2.0f);
            float thumb_cy = horizontal ? (h / 2.0f) : (thumb_r + track_h - norm * track_h);
            float dx = mx - thumb_cx, dy = my - thumb_cy;
            bool hovered = (dx * dx + dy * dy <= thumb_r * thumb_r);
            if (hovered != slider->thumb_hovered) {
                slider->thumb_hovered = hovered;
                widget->needs_paint = true;
            }
            break;
        }

        case VG_EVENT_MOUSE_UP: {
            if (slider->dragging) {
                slider->dragging = false;
                if (vg_widget_get_input_capture() == widget)
                    vg_widget_release_input_capture();
                widget->needs_paint = true;
                event->handled = true;
                return true;
            }
            break;
        }

        case VG_EVENT_MOUSE_LEAVE: {
            if (!slider->dragging && slider->thumb_hovered) {
                slider->thumb_hovered = false;
                widget->needs_paint = true;
            }
            break;
        }

        case VG_EVENT_KEY_DOWN: {
            /* Arrow keys adjust the slider value by one step (or 1% of range
             * when step == 0).  Home/End jump to the min/max extremes. */
            float step = (slider->step > 0.0f) ? slider->step
                                               : (slider->max_value - slider->min_value) * 0.01f;
            bool horiz = (slider->orientation == VG_SLIDER_HORIZONTAL);
            switch (event->key.key) {
                case VG_KEY_RIGHT:
                case VG_KEY_UP:
                    vg_slider_set_value(slider, slider->value + (horiz ? step : step));
                    event->handled = true;
                    return true;
                case VG_KEY_LEFT:
                case VG_KEY_DOWN:
                    vg_slider_set_value(slider, slider->value - (horiz ? step : step));
                    event->handled = true;
                    return true;
                case VG_KEY_HOME:
                    vg_slider_set_value(slider, slider->min_value);
                    event->handled = true;
                    return true;
                case VG_KEY_END:
                    vg_slider_set_value(slider, slider->max_value);
                    event->handled = true;
                    return true;
                default:
                    break;
            }
            break;
        }

        default:
            break;
    }
    return false;
}

vg_slider_t *vg_slider_create(vg_widget_t *parent, vg_slider_orientation_t orientation) {
    vg_slider_t *slider = calloc(1, sizeof(vg_slider_t));
    if (!slider)
        return NULL;

    vg_widget_init(&slider->base, VG_WIDGET_SLIDER, &g_slider_vtable);
    slider->orientation = orientation;

    // Default values
    slider->min_value = 0;
    slider->max_value = 100;
    slider->value = 0;
    slider->step = 0; // continuous

    // Default appearance
    vg_theme_t *theme = vg_theme_get_current();
    float scale = theme && theme->ui_scale > 0.0f ? theme->ui_scale : 1.0f;
    slider->track_thickness = 4.0f * scale;
    slider->thumb_size = 16.0f * scale;
    slider->track_color =
        theme ? vg_color_blend(theme->colors.bg_secondary, theme->colors.bg_primary, 0.45f)
              : 0x003C3C3C;
    slider->fill_color = theme ? theme->colors.accent_primary : 0x000078D4;
    slider->thumb_color = theme ? theme->colors.bg_primary : 0x00FFFFFF;
    slider->thumb_hover_color =
        theme ? vg_color_lighten(slider->thumb_color, 0.12f) : 0x00E0E0E0;
    slider->font_size = theme ? theme->typography.size_normal : 12.0f;

    if (parent) {
        vg_widget_add_child(parent, &slider->base);
    }

    return slider;
}

/// @brief Slider set value.
void vg_slider_set_value(vg_slider_t *slider, float value) {
    if (!slider)
        return;

    // Clamp to range
    if (value < slider->min_value)
        value = slider->min_value;
    if (value > slider->max_value)
        value = slider->max_value;

    // Snap to step if specified
    if (slider->step > 0) {
        float steps = (value - slider->min_value) / slider->step;
        value = slider->min_value + ((int)(steps + 0.5f)) * slider->step;
    }

    float old = slider->value;
    slider->value = value;

    slider->base.needs_paint = true;

    if (old != value && slider->on_change) {
        slider->on_change(&slider->base, value, slider->on_change_data);
    }
}

/// @brief Slider get value.
float vg_slider_get_value(vg_slider_t *slider) {
    return slider ? slider->value : 0;
}

/// @brief Slider set range.
void vg_slider_set_range(vg_slider_t *slider, float min_val, float max_val) {
    if (!slider)
        return;
    slider->min_value = min_val;
    slider->max_value = max_val;
    // Re-clamp current value
    vg_slider_set_value(slider, slider->value);
    slider->base.needs_paint = true;
}

/// @brief Slider set step.
void vg_slider_set_step(vg_slider_t *slider, float step) {
    if (!slider)
        return;
    slider->step = step > 0 ? step : 0;
    slider->base.needs_paint = true;
}

/// @brief Slider set on change.
void vg_slider_set_on_change(vg_slider_t *slider, vg_slider_callback_t callback, void *user_data) {
    if (!slider)
        return;
    slider->on_change = callback;
    slider->on_change_data = user_data;
}
