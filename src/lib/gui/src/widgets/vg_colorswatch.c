// vg_colorswatch.c - Color swatch widget implementation
#include "../../include/vg_widgets.h"
#include "../../include/vg_theme.h"
#include "../../include/vg_event.h"
#include <stdlib.h>
#include <string.h>

//=============================================================================
// Forward Declarations
//=============================================================================

static void colorswatch_destroy(vg_widget_t* widget);
static void colorswatch_measure(vg_widget_t* widget, float available_width, float available_height);
static void colorswatch_paint(vg_widget_t* widget, void* canvas);
static bool colorswatch_handle_event(vg_widget_t* widget, vg_event_t* event);
static bool colorswatch_can_focus(vg_widget_t* widget);

//=============================================================================
// ColorSwatch VTable
//=============================================================================

static vg_widget_vtable_t g_colorswatch_vtable = {
    .destroy = colorswatch_destroy,
    .measure = colorswatch_measure,
    .arrange = NULL,
    .paint = colorswatch_paint,
    .handle_event = colorswatch_handle_event,
    .can_focus = colorswatch_can_focus,
    .on_focus = NULL
};

//=============================================================================
// Helper: Draw checkerboard pattern for transparency
//=============================================================================

static void draw_checkerboard(void* canvas, float x, float y, float w, float h, int check_size) {
    (void)canvas;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    (void)check_size;
    // Placeholder - would draw checkerboard pattern for alpha visualization
}

//=============================================================================
// ColorSwatch Implementation
//=============================================================================

vg_colorswatch_t* vg_colorswatch_create(vg_widget_t* parent, uint32_t color) {
    vg_colorswatch_t* swatch = calloc(1, sizeof(vg_colorswatch_t));
    if (!swatch) return NULL;

    // Initialize base widget
    vg_widget_init(&swatch->base, VG_WIDGET_COLORSWATCH, &g_colorswatch_vtable);

    // Get theme
    vg_theme_t* theme = vg_theme_get_current();

    // Initialize swatch-specific fields
    swatch->color = color;
    swatch->selected = false;
    swatch->show_border = true;

    // Default appearance
    swatch->size = 24.0f;
    swatch->border_color = theme->colors.border_primary;
    swatch->selected_border = theme->colors.accent_primary;
    swatch->border_width = 1.0f;
    swatch->corner_radius = 2.0f;

    // Callbacks
    swatch->on_select = NULL;
    swatch->on_select_data = NULL;

    // Set constraints
    swatch->base.constraints.min_width = swatch->size;
    swatch->base.constraints.min_height = swatch->size;
    swatch->base.constraints.preferred_width = swatch->size;
    swatch->base.constraints.preferred_height = swatch->size;

    // Add to parent
    if (parent) {
        vg_widget_add_child(parent, &swatch->base);
    }

    return swatch;
}

static void colorswatch_destroy(vg_widget_t* widget) {
    (void)widget;
    // No owned resources to free
}

static void colorswatch_measure(vg_widget_t* widget, float available_width, float available_height) {
    vg_colorswatch_t* swatch = (vg_colorswatch_t*)widget;
    (void)available_width;
    (void)available_height;

    widget->measured_width = swatch->size;
    widget->measured_height = swatch->size;

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

static void colorswatch_paint(vg_widget_t* widget, void* canvas) {
    vg_colorswatch_t* swatch = (vg_colorswatch_t*)widget;
    (void)canvas;

    // Get alpha component
    uint8_t alpha = (swatch->color >> 24) & 0xFF;

    // If color has transparency, draw checkerboard first
    if (alpha < 255) {
        draw_checkerboard(canvas, widget->x, widget->y, widget->width, widget->height, 4);
    }

    // Draw color fill
    // In a real implementation, this would call vgfx_draw_rect_filled
    // For now, this is a placeholder - actual rendering happens in the graphics layer

    // Draw border
    if (swatch->show_border) {
        uint32_t border = swatch->selected ? swatch->selected_border : swatch->border_color;

        // If hovered, slightly brighten border
        if (widget->state & VG_STATE_HOVERED) {
            border = swatch->selected_border;
        }

        // Border drawing placeholder
        (void)border;
    }

    // Draw selection indicator if selected
    if (swatch->selected) {
        // Draw inner highlight or checkmark
        // Placeholder
    }
}

static bool colorswatch_handle_event(vg_widget_t* widget, vg_event_t* event) {
    vg_colorswatch_t* swatch = (vg_colorswatch_t*)widget;

    if (widget->state & VG_STATE_DISABLED) {
        return false;
    }

    if (event->type == VG_EVENT_CLICK) {
        // Call selection callback
        if (swatch->on_select) {
            swatch->on_select(widget, swatch->color, swatch->on_select_data);
        }
        // Also call widget's generic on_click if set
        if (widget->on_click) {
            widget->on_click(widget, widget->callback_data);
        }
        return true;
    }

    return false;
}

static bool colorswatch_can_focus(vg_widget_t* widget) {
    return widget->enabled && widget->visible;
}

//=============================================================================
// ColorSwatch API
//=============================================================================

void vg_colorswatch_set_color(vg_colorswatch_t* swatch, uint32_t color) {
    if (!swatch) return;

    swatch->color = color;
    swatch->base.needs_paint = true;
}

uint32_t vg_colorswatch_get_color(vg_colorswatch_t* swatch) {
    if (!swatch) return 0;
    return swatch->color;
}

void vg_colorswatch_set_selected(vg_colorswatch_t* swatch, bool selected) {
    if (!swatch) return;

    swatch->selected = selected;
    if (selected) {
        swatch->base.state |= VG_STATE_SELECTED;
    } else {
        swatch->base.state &= ~VG_STATE_SELECTED;
    }
    swatch->base.needs_paint = true;
}

bool vg_colorswatch_is_selected(vg_colorswatch_t* swatch) {
    if (!swatch) return false;
    return swatch->selected;
}

void vg_colorswatch_set_on_select(vg_colorswatch_t* swatch, vg_colorswatch_callback_t callback, void* user_data) {
    if (!swatch) return;

    swatch->on_select = callback;
    swatch->on_select_data = user_data;
}

void vg_colorswatch_set_size(vg_colorswatch_t* swatch, float size) {
    if (!swatch || size <= 0) return;

    swatch->size = size;
    swatch->base.constraints.min_width = size;
    swatch->base.constraints.min_height = size;
    swatch->base.constraints.preferred_width = size;
    swatch->base.constraints.preferred_height = size;
    swatch->base.needs_layout = true;
    swatch->base.needs_paint = true;
}
