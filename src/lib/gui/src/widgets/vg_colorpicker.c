// vg_colorpicker.c - Color picker widget implementation
#include "../../include/vg_widgets.h"
#include "../../include/vg_theme.h"
#include "../../include/vg_event.h"
#include <stdlib.h>
#include <string.h>

//=============================================================================
// Forward Declarations
//=============================================================================

static void colorpicker_destroy(vg_widget_t* widget);
static void colorpicker_measure(vg_widget_t* widget, float available_width, float available_height);
static void colorpicker_arrange(vg_widget_t* widget, float x, float y, float width, float height);
static void colorpicker_paint(vg_widget_t* widget, void* canvas);
static bool colorpicker_handle_event(vg_widget_t* widget, vg_event_t* event);
static bool colorpicker_can_focus(vg_widget_t* widget);

static void colorpicker_update_color_from_components(vg_colorpicker_t* picker);
static void colorpicker_update_components_from_color(vg_colorpicker_t* picker);

//=============================================================================
// ColorPicker VTable
//=============================================================================

static vg_widget_vtable_t g_colorpicker_vtable = {
    .destroy = colorpicker_destroy,
    .measure = colorpicker_measure,
    .arrange = colorpicker_arrange,
    .paint = colorpicker_paint,
    .handle_event = colorpicker_handle_event,
    .can_focus = colorpicker_can_focus,
    .on_focus = NULL
};

//=============================================================================
// Internal Callbacks
//=============================================================================

// Called when R slider changes
static void on_slider_r_change(vg_widget_t* slider, float value, void* user_data) {
    (void)slider;
    vg_colorpicker_t* picker = (vg_colorpicker_t*)user_data;
    if (!picker) return;

    picker->r = (uint8_t)(value + 0.5f);
    colorpicker_update_color_from_components(picker);

    if (picker->preview) {
        vg_colorswatch_set_color(picker->preview, picker->color);
    }

    if (picker->on_change) {
        picker->on_change(&picker->base, picker->color, picker->on_change_data);
    }
}

// Called when G slider changes
static void on_slider_g_change(vg_widget_t* slider, float value, void* user_data) {
    (void)slider;
    vg_colorpicker_t* picker = (vg_colorpicker_t*)user_data;
    if (!picker) return;

    picker->g = (uint8_t)(value + 0.5f);
    colorpicker_update_color_from_components(picker);

    if (picker->preview) {
        vg_colorswatch_set_color(picker->preview, picker->color);
    }

    if (picker->on_change) {
        picker->on_change(&picker->base, picker->color, picker->on_change_data);
    }
}

// Called when B slider changes
static void on_slider_b_change(vg_widget_t* slider, float value, void* user_data) {
    (void)slider;
    vg_colorpicker_t* picker = (vg_colorpicker_t*)user_data;
    if (!picker) return;

    picker->b = (uint8_t)(value + 0.5f);
    colorpicker_update_color_from_components(picker);

    if (picker->preview) {
        vg_colorswatch_set_color(picker->preview, picker->color);
    }

    if (picker->on_change) {
        picker->on_change(&picker->base, picker->color, picker->on_change_data);
    }
}

// Called when A slider changes
static void on_slider_a_change(vg_widget_t* slider, float value, void* user_data) {
    (void)slider;
    vg_colorpicker_t* picker = (vg_colorpicker_t*)user_data;
    if (!picker) return;

    picker->a = (uint8_t)(value + 0.5f);
    colorpicker_update_color_from_components(picker);

    if (picker->preview) {
        vg_colorswatch_set_color(picker->preview, picker->color);
    }

    if (picker->on_change) {
        picker->on_change(&picker->base, picker->color, picker->on_change_data);
    }
}

// Called when a color is selected from the palette
static void on_palette_select(vg_widget_t* palette, uint32_t color, int index, void* user_data) {
    (void)palette;
    (void)index;
    vg_colorpicker_t* picker = (vg_colorpicker_t*)user_data;
    if (!picker) return;

    vg_colorpicker_set_color(picker, color);
}

//=============================================================================
// ColorPicker Implementation
//=============================================================================

vg_colorpicker_t* vg_colorpicker_create(vg_widget_t* parent) {
    vg_colorpicker_t* picker = calloc(1, sizeof(vg_colorpicker_t));
    if (!picker) return NULL;

    // Initialize base widget
    vg_widget_init(&picker->base, VG_WIDGET_COLORPICKER, &g_colorpicker_vtable);

    // Get theme
    vg_theme_t* theme = vg_theme_get_current();

    // Initialize color to black (fully opaque)
    picker->color = 0xFF000000;
    picker->r = 0;
    picker->g = 0;
    picker->b = 0;
    picker->a = 255;

    // Display options
    picker->show_alpha = false;
    picker->show_palette = true;
    picker->show_labels = true;
    picker->show_values = true;
    picker->font = NULL;
    picker->font_size = theme->typography.size_small;

    // Callbacks
    picker->on_change = NULL;
    picker->on_change_data = NULL;

    // Create child widgets

    // Color preview swatch
    picker->preview = vg_colorswatch_create(&picker->base, picker->color);
    if (picker->preview) {
        vg_colorswatch_set_size(picker->preview, 48.0f);
    }

    // RGB sliders
    picker->slider_r = vg_slider_create(&picker->base, VG_SLIDER_HORIZONTAL);
    if (picker->slider_r) {
        vg_slider_set_range(picker->slider_r, 0, 255);
        vg_slider_set_value(picker->slider_r, 0);
        vg_slider_set_on_change(picker->slider_r, on_slider_r_change, picker);
    }

    picker->slider_g = vg_slider_create(&picker->base, VG_SLIDER_HORIZONTAL);
    if (picker->slider_g) {
        vg_slider_set_range(picker->slider_g, 0, 255);
        vg_slider_set_value(picker->slider_g, 0);
        vg_slider_set_on_change(picker->slider_g, on_slider_g_change, picker);
    }

    picker->slider_b = vg_slider_create(&picker->base, VG_SLIDER_HORIZONTAL);
    if (picker->slider_b) {
        vg_slider_set_range(picker->slider_b, 0, 255);
        vg_slider_set_value(picker->slider_b, 0);
        vg_slider_set_on_change(picker->slider_b, on_slider_b_change, picker);
    }

    picker->slider_a = vg_slider_create(&picker->base, VG_SLIDER_HORIZONTAL);
    if (picker->slider_a) {
        vg_slider_set_range(picker->slider_a, 0, 255);
        vg_slider_set_value(picker->slider_a, 255);
        vg_slider_set_on_change(picker->slider_a, on_slider_a_change, picker);
        // Hide by default
        if (!picker->show_alpha) {
            vg_widget_set_visible(&picker->slider_a->base, false);
        }
    }

    // Quick palette
    picker->palette = vg_colorpalette_create(&picker->base);
    if (picker->palette) {
        vg_colorpalette_load_standard_16(picker->palette);
        vg_colorpalette_set_on_select(picker->palette, on_palette_select, picker);
        if (!picker->show_palette) {
            vg_widget_set_visible(&picker->palette->base, false);
        }
    }

    // Set minimum size
    picker->base.constraints.min_width = 200.0f;
    picker->base.constraints.min_height = 150.0f;

    // Add to parent
    if (parent) {
        vg_widget_add_child(parent, &picker->base);
    }

    return picker;
}

static void colorpicker_destroy(vg_widget_t* widget) {
    (void)widget;
    // Child widgets are destroyed automatically through widget hierarchy
}

static void colorpicker_measure(vg_widget_t* widget, float available_width, float available_height) {
    vg_colorpicker_t* picker = (vg_colorpicker_t*)widget;
    (void)available_width;
    (void)available_height;

    // Calculate minimum height needed
    float height = 0;
    float width = 200.0f;

    // Preview swatch row
    height += 48.0f + 8.0f;  // swatch + gap

    // RGB sliders (3 rows, or 4 if alpha shown)
    int slider_count = picker->show_alpha ? 4 : 3;
    height += slider_count * (24.0f + 4.0f);  // slider height + gap

    // Palette if shown
    if (picker->show_palette && picker->palette) {
        height += 8.0f;  // gap
        height += 2 * 20.0f + 2.0f;  // 2 rows of swatches
    }

    widget->measured_width = width;
    widget->measured_height = height;

    // Apply constraints
    if (widget->constraints.min_width > widget->measured_width) {
        widget->measured_width = widget->constraints.min_width;
    }
    if (widget->constraints.min_height > widget->measured_height) {
        widget->measured_height = widget->constraints.min_height;
    }
    if (widget->constraints.max_width > 0 && widget->measured_width > widget->constraints.max_width) {
        widget->measured_width = widget->constraints.max_width;
    }
    if (widget->constraints.max_height > 0 && widget->measured_height > widget->constraints.max_height) {
        widget->measured_height = widget->constraints.max_height;
    }
}

static void colorpicker_arrange(vg_widget_t* widget, float x, float y, float width, float height) {
    vg_colorpicker_t* picker = (vg_colorpicker_t*)widget;

    widget->x = x;
    widget->y = y;
    widget->width = width;
    widget->height = height;

    float current_y = y + 4.0f;
    float padding = 4.0f;
    float slider_height = 24.0f;
    float label_width = picker->show_labels ? 20.0f : 0;
    float value_width = picker->show_values ? 40.0f : 0;

    // Arrange preview swatch (right-aligned)
    if (picker->preview) {
        float swatch_size = 48.0f;
        vg_widget_arrange(&picker->preview->base,
                         x + width - swatch_size - padding,
                         current_y,
                         swatch_size, swatch_size);
    }

    // Calculate slider width
    float slider_width = width - padding * 2 - label_width - value_width - 56.0f;  // Leave space for preview

    // Arrange R slider
    if (picker->slider_r) {
        vg_widget_arrange(&picker->slider_r->base,
                         x + padding + label_width,
                         current_y,
                         slider_width, slider_height);
        current_y += slider_height + 4.0f;
    }

    // Arrange G slider
    if (picker->slider_g) {
        vg_widget_arrange(&picker->slider_g->base,
                         x + padding + label_width,
                         current_y,
                         slider_width, slider_height);
        current_y += slider_height + 4.0f;
    }

    // Arrange B slider
    if (picker->slider_b) {
        vg_widget_arrange(&picker->slider_b->base,
                         x + padding + label_width,
                         current_y,
                         slider_width, slider_height);
        current_y += slider_height + 4.0f;
    }

    // Arrange A slider if visible
    if (picker->slider_a && picker->show_alpha) {
        vg_widget_arrange(&picker->slider_a->base,
                         x + padding + label_width,
                         current_y,
                         slider_width, slider_height);
        current_y += slider_height + 4.0f;
    }

    // Arrange palette if visible
    if (picker->palette && picker->show_palette) {
        current_y += 8.0f;  // Extra gap
        float palette_width = width - padding * 2;
        float palette_height = 2 * 20.0f + 2.0f;  // 2 rows
        vg_widget_arrange(&picker->palette->base,
                         x + padding,
                         current_y,
                         palette_width, palette_height);
    }
}

static void colorpicker_paint(vg_widget_t* widget, void* canvas) {
    vg_colorpicker_t* picker = (vg_colorpicker_t*)widget;
    (void)canvas;

    // Draw labels if enabled
    if (picker->show_labels && picker->font) {
        float label_x = widget->x + 4.0f;
        float label_y = widget->y + 4.0f + 16.0f;  // Baseline

        // Draw "R", "G", "B", "A" labels next to sliders
        // In real implementation, use vg_font_draw_text
        (void)label_x;
        (void)label_y;
    }

    // Draw numeric values if enabled
    if (picker->show_values && picker->font) {
        // Draw values after sliders
        // In real implementation, use vg_font_draw_text
    }

    // Child widgets are painted automatically
}

static bool colorpicker_handle_event(vg_widget_t* widget, vg_event_t* event) {
    (void)widget;
    (void)event;
    // Events are handled by child widgets (sliders, palette)
    return false;
}

static bool colorpicker_can_focus(vg_widget_t* widget) {
    return widget->enabled && widget->visible;
}

//=============================================================================
// Internal Helpers
//=============================================================================

static void colorpicker_update_color_from_components(vg_colorpicker_t* picker) {
    picker->color = ((uint32_t)picker->a << 24) |
                    ((uint32_t)picker->r << 16) |
                    ((uint32_t)picker->g << 8) |
                    ((uint32_t)picker->b);
}

static void colorpicker_update_components_from_color(vg_colorpicker_t* picker) {
    picker->a = (picker->color >> 24) & 0xFF;
    picker->r = (picker->color >> 16) & 0xFF;
    picker->g = (picker->color >> 8) & 0xFF;
    picker->b = picker->color & 0xFF;
}

//=============================================================================
// ColorPicker API
//=============================================================================

void vg_colorpicker_set_color(vg_colorpicker_t* picker, uint32_t color) {
    if (!picker) return;

    picker->color = color;
    colorpicker_update_components_from_color(picker);

    // Update sliders
    if (picker->slider_r) vg_slider_set_value(picker->slider_r, picker->r);
    if (picker->slider_g) vg_slider_set_value(picker->slider_g, picker->g);
    if (picker->slider_b) vg_slider_set_value(picker->slider_b, picker->b);
    if (picker->slider_a) vg_slider_set_value(picker->slider_a, picker->a);

    // Update preview
    if (picker->preview) {
        vg_colorswatch_set_color(picker->preview, picker->color);
    }

    picker->base.needs_paint = true;

    // Call callback
    if (picker->on_change) {
        picker->on_change(&picker->base, picker->color, picker->on_change_data);
    }
}

uint32_t vg_colorpicker_get_color(vg_colorpicker_t* picker) {
    if (!picker) return 0;
    return picker->color;
}

void vg_colorpicker_set_rgb(vg_colorpicker_t* picker, uint8_t r, uint8_t g, uint8_t b) {
    if (!picker) return;

    picker->r = r;
    picker->g = g;
    picker->b = b;
    colorpicker_update_color_from_components(picker);

    // Update sliders
    if (picker->slider_r) vg_slider_set_value(picker->slider_r, r);
    if (picker->slider_g) vg_slider_set_value(picker->slider_g, g);
    if (picker->slider_b) vg_slider_set_value(picker->slider_b, b);

    // Update preview
    if (picker->preview) {
        vg_colorswatch_set_color(picker->preview, picker->color);
    }

    picker->base.needs_paint = true;

    if (picker->on_change) {
        picker->on_change(&picker->base, picker->color, picker->on_change_data);
    }
}

void vg_colorpicker_get_rgb(vg_colorpicker_t* picker, uint8_t* r, uint8_t* g, uint8_t* b) {
    if (!picker) return;
    if (r) *r = picker->r;
    if (g) *g = picker->g;
    if (b) *b = picker->b;
}

void vg_colorpicker_set_alpha(vg_colorpicker_t* picker, uint8_t alpha) {
    if (!picker) return;

    picker->a = alpha;
    colorpicker_update_color_from_components(picker);

    if (picker->slider_a) vg_slider_set_value(picker->slider_a, alpha);

    if (picker->preview) {
        vg_colorswatch_set_color(picker->preview, picker->color);
    }

    picker->base.needs_paint = true;

    if (picker->on_change) {
        picker->on_change(&picker->base, picker->color, picker->on_change_data);
    }
}

uint8_t vg_colorpicker_get_alpha(vg_colorpicker_t* picker) {
    if (!picker) return 255;
    return picker->a;
}

void vg_colorpicker_show_alpha(vg_colorpicker_t* picker, bool show) {
    if (!picker) return;

    picker->show_alpha = show;
    if (picker->slider_a) {
        vg_widget_set_visible(&picker->slider_a->base, show);
    }
    picker->base.needs_layout = true;
    picker->base.needs_paint = true;
}

void vg_colorpicker_show_palette(vg_colorpicker_t* picker, bool show) {
    if (!picker) return;

    picker->show_palette = show;
    if (picker->palette) {
        vg_widget_set_visible(&picker->palette->base, show);
    }
    picker->base.needs_layout = true;
    picker->base.needs_paint = true;
}

void vg_colorpicker_set_on_change(vg_colorpicker_t* picker, vg_colorpicker_callback_t callback, void* user_data) {
    if (!picker) return;

    picker->on_change = callback;
    picker->on_change_data = user_data;
}

void vg_colorpicker_set_font(vg_colorpicker_t* picker, vg_font_t* font, float size) {
    if (!picker) return;

    picker->font = font;
    picker->font_size = size > 0 ? size : 12.0f;
    picker->base.needs_paint = true;
}
