// vg_slider.c - Slider widget implementation
#include "../../include/vg_widgets.h"
#include <stdlib.h>

vg_slider_t* vg_slider_create(vg_widget_t* parent, vg_slider_orientation_t orientation) {
    vg_slider_t* slider = calloc(1, sizeof(vg_slider_t));
    if (!slider) return NULL;

    slider->base.type = VG_WIDGET_SLIDER;
    slider->base.visible = true;
    slider->base.enabled = true;
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

    if (parent) {
        vg_widget_add_child(parent, &slider->base);
    }

    return slider;
}

void vg_slider_set_value(vg_slider_t* slider, float value) {
    if (!slider) return;

    // Clamp to range
    if (value < slider->min_value) value = slider->min_value;
    if (value > slider->max_value) value = slider->max_value;

    // Snap to step if specified
    if (slider->step > 0) {
        float steps = (value - slider->min_value) / slider->step;
        value = slider->min_value + ((int)(steps + 0.5f)) * slider->step;
    }

    float old = slider->value;
    slider->value = value;

    if (old != value && slider->on_change) {
        slider->on_change(&slider->base, value, slider->on_change_data);
    }
}

float vg_slider_get_value(vg_slider_t* slider) {
    return slider ? slider->value : 0;
}

void vg_slider_set_range(vg_slider_t* slider, float min_val, float max_val) {
    if (!slider) return;
    slider->min_value = min_val;
    slider->max_value = max_val;
    // Re-clamp current value
    vg_slider_set_value(slider, slider->value);
}

void vg_slider_set_step(vg_slider_t* slider, float step) {
    if (!slider) return;
    slider->step = step > 0 ? step : 0;
}

void vg_slider_set_on_change(vg_slider_t* slider, vg_slider_callback_t callback, void* user_data) {
    if (!slider) return;
    slider->on_change = callback;
    slider->on_change_data = user_data;
}
