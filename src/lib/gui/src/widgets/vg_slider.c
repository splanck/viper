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

static void slider_measure(vg_widget_t *widget, float available_width, float available_height)
{
    vg_slider_t *slider = (vg_slider_t *)widget;
    (void)available_width;
    (void)available_height;
    if (slider->orientation == VG_SLIDER_HORIZONTAL)
    {
        widget->measured_width = 100.0f;
        widget->measured_height = slider->thumb_size > 0 ? slider->thumb_size : 24.0f;
    }
    else
    {
        widget->measured_width = slider->thumb_size > 0 ? slider->thumb_size : 24.0f;
        widget->measured_height = 100.0f;
    }
}

static void slider_arrange(vg_widget_t *widget, float x, float y, float w, float h)
{
    widget->x = x;
    widget->y = y;
    widget->width = w;
    widget->height = h;
}

static bool slider_can_focus(vg_widget_t *widget)
{
    return widget->enabled && widget->visible;
}

static void slider_paint(vg_widget_t *widget, void *canvas)
{
    vg_slider_t *slider = (vg_slider_t *)widget;
    vgfx_window_t win = (vgfx_window_t)canvas;
    float x = widget->x, y = widget->y, w = widget->width, h = widget->height;

    float range = slider->max_value - slider->min_value;
    float norm = (range > 0.0f) ? (slider->value - slider->min_value) / range : 0.0f;
    if (norm < 0.0f)
        norm = 0.0f;
    if (norm > 1.0f)
        norm = 1.0f;

    if (slider->orientation == VG_SLIDER_HORIZONTAL)
    {
        float track_th = slider->track_thickness > 0 ? slider->track_thickness : 4.0f;
        int32_t track_y = (int32_t)(y + (h - track_th) / 2.0f);
        int32_t track_h = (int32_t)track_th;

        /* Track background */
        vgfx_fill_rect(win, (int32_t)x, track_y, (int32_t)w, track_h, slider->track_color);

        /* Fill (value portion) */
        int32_t fill_w = (int32_t)(norm * w);
        if (fill_w > 0)
            vgfx_fill_rect(win, (int32_t)x, track_y, fill_w, track_h, slider->fill_color);

        /* Thumb */
        float thumb_cx = x + norm * w;
        float thumb_cy = y + h / 2.0f;
        int32_t thumb_r = (int32_t)(slider->thumb_size / 2.0f);
        uint32_t tc = slider->thumb_hovered ? slider->thumb_hover_color : slider->thumb_color;
        vgfx_fill_circle(win, (int32_t)thumb_cx, (int32_t)thumb_cy, thumb_r, tc);
    }
    else
    {
        /* Vertical orientation */
        float track_th = slider->track_thickness > 0 ? slider->track_thickness : 4.0f;
        int32_t track_x = (int32_t)(x + (w - track_th) / 2.0f);
        int32_t track_w = (int32_t)track_th;

        vgfx_fill_rect(win, track_x, (int32_t)y, track_w, (int32_t)h, slider->track_color);

        int32_t fill_h = (int32_t)(norm * h);
        int32_t fill_y = (int32_t)(y + h - fill_h);
        if (fill_h > 0)
            vgfx_fill_rect(win, track_x, fill_y, track_w, fill_h, slider->fill_color);

        float thumb_cx = x + w / 2.0f;
        float thumb_cy = y + h - norm * h;
        int32_t thumb_r = (int32_t)(slider->thumb_size / 2.0f);
        uint32_t tc = slider->thumb_hovered ? slider->thumb_hover_color : slider->thumb_color;
        vgfx_fill_circle(win, (int32_t)thumb_cx, (int32_t)thumb_cy, thumb_r, tc);
    }

    /* Draw focus ring when the slider has keyboard focus */
    if (widget->state & VG_STATE_FOCUSED)
    {
        vg_theme_t *theme = vg_theme_get_current();
        vgfx_rect(win, (int32_t)x, (int32_t)y, (int32_t)w, (int32_t)h, theme->colors.border_focus);
    }
}

static bool slider_handle_event(vg_widget_t *widget, vg_event_t *event)
{
    vg_slider_t *slider = (vg_slider_t *)widget;
    float x = widget->x, y = widget->y, w = widget->width, h = widget->height;

    float range = slider->max_value - slider->min_value;
    bool horizontal = (slider->orientation == VG_SLIDER_HORIZONTAL);
    float thumb_r = slider->thumb_size / 2.0f;

    switch (event->type)
    {
        case VG_EVENT_MOUSE_DOWN:
        {
            float norm = (range > 0.0f) ? (slider->value - slider->min_value) / range : 0.0f;
            float thumb_cx = horizontal ? (x + norm * w) : (x + w / 2.0f);
            float thumb_cy = horizontal ? (y + h / 2.0f) : (y + h - norm * h);
            float mx = event->mouse.screen_x, my = event->mouse.screen_y;
            float dx = mx - thumb_cx, dy = my - thumb_cy;
            if (dx * dx + dy * dy <= thumb_r * thumb_r)
            {
                slider->dragging = true;
                event->handled = true;
                return true;
            }
            break;
        }

        case VG_EVENT_MOUSE_MOVE:
        {
            float mx = event->mouse.screen_x, my = event->mouse.screen_y;
            if (slider->dragging)
            {
                float norm;
                if (horizontal)
                    norm = (w > 0.0f) ? (mx - x) / w : 0.0f;
                else
                    norm = (h > 0.0f) ? (y + h - my) / h : 0.0f;
                if (norm < 0.0f)
                    norm = 0.0f;
                if (norm > 1.0f)
                    norm = 1.0f;
                float new_val = slider->min_value + norm * range;
                vg_slider_set_value(slider, new_val);
                event->handled = true;
                return true;
            }
            /* Update thumb hover state */
            float norm = (range > 0.0f) ? (slider->value - slider->min_value) / range : 0.0f;
            float thumb_cx = horizontal ? (x + norm * w) : (x + w / 2.0f);
            float thumb_cy = horizontal ? (y + h / 2.0f) : (y + h - norm * h);
            float dx = mx - thumb_cx, dy = my - thumb_cy;
            slider->thumb_hovered = (dx * dx + dy * dy <= thumb_r * thumb_r);
            break;
        }

        case VG_EVENT_MOUSE_UP:
        {
            if (slider->dragging)
            {
                slider->dragging = false;
                event->handled = true;
                return true;
            }
            break;
        }

        case VG_EVENT_MOUSE_LEAVE:
        {
            slider->thumb_hovered = false;
            slider->dragging = false;
            break;
        }

        case VG_EVENT_KEY_DOWN:
        {
            /* Arrow keys adjust the slider value by one step (or 1% of range
             * when step == 0).  Home/End jump to the min/max extremes. */
            float step = (slider->step > 0.0f) ? slider->step
                                               : (slider->max_value - slider->min_value) * 0.01f;
            bool horiz = (slider->orientation == VG_SLIDER_HORIZONTAL);
            switch (event->key.key)
            {
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

vg_slider_t *vg_slider_create(vg_widget_t *parent, vg_slider_orientation_t orientation)
{
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
    slider->track_thickness = 4;
    slider->thumb_size = 16;
    slider->track_color = 0xFF3C3C3C;
    slider->fill_color = 0xFF0078D4;
    slider->thumb_color = 0xFFFFFFFF;
    slider->thumb_hover_color = 0xFFE0E0E0;
    slider->font_size = 12;

    if (parent)
    {
        vg_widget_add_child(parent, &slider->base);
    }

    return slider;
}

void vg_slider_set_value(vg_slider_t *slider, float value)
{
    if (!slider)
        return;

    // Clamp to range
    if (value < slider->min_value)
        value = slider->min_value;
    if (value > slider->max_value)
        value = slider->max_value;

    // Snap to step if specified
    if (slider->step > 0)
    {
        float steps = (value - slider->min_value) / slider->step;
        value = slider->min_value + ((int)(steps + 0.5f)) * slider->step;
    }

    float old = slider->value;
    slider->value = value;

    if (old != value && slider->on_change)
    {
        slider->on_change(&slider->base, value, slider->on_change_data);
    }
}

float vg_slider_get_value(vg_slider_t *slider)
{
    return slider ? slider->value : 0;
}

void vg_slider_set_range(vg_slider_t *slider, float min_val, float max_val)
{
    if (!slider)
        return;
    slider->min_value = min_val;
    slider->max_value = max_val;
    // Re-clamp current value
    vg_slider_set_value(slider, slider->value);
}

void vg_slider_set_step(vg_slider_t *slider, float step)
{
    if (!slider)
        return;
    slider->step = step > 0 ? step : 0;
}

void vg_slider_set_on_change(vg_slider_t *slider, vg_slider_callback_t callback, void *user_data)
{
    if (!slider)
        return;
    slider->on_change = callback;
    slider->on_change_data = user_data;
}
