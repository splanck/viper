// vg_checkbox.c - Checkbox widget implementation
#include "../../include/vg_widgets.h"
#include "../../include/vg_theme.h"
#include "../../include/vg_event.h"
#include <stdlib.h>
#include <string.h>

//=============================================================================
// Forward Declarations
//=============================================================================

static void checkbox_destroy(vg_widget_t* widget);
static void checkbox_measure(vg_widget_t* widget, float available_width, float available_height);
static void checkbox_paint(vg_widget_t* widget, void* canvas);
static bool checkbox_handle_event(vg_widget_t* widget, vg_event_t* event);
static bool checkbox_can_focus(vg_widget_t* widget);

//=============================================================================
// Checkbox VTable
//=============================================================================

static vg_widget_vtable_t g_checkbox_vtable = {
    .destroy = checkbox_destroy,
    .measure = checkbox_measure,
    .arrange = NULL,
    .paint = checkbox_paint,
    .handle_event = checkbox_handle_event,
    .can_focus = checkbox_can_focus,
    .on_focus = NULL
};

//=============================================================================
// Checkbox Implementation
//=============================================================================

vg_checkbox_t* vg_checkbox_create(vg_widget_t* parent, const char* text) {
    vg_checkbox_t* checkbox = calloc(1, sizeof(vg_checkbox_t));
    if (!checkbox) return NULL;

    // Initialize base widget
    vg_widget_init(&checkbox->base, VG_WIDGET_CHECKBOX, &g_checkbox_vtable);

    // Get theme
    vg_theme_t* theme = vg_theme_get_current();

    // Initialize checkbox-specific fields
    checkbox->text = text ? strdup(text) : strdup("");
    checkbox->font = NULL;
    checkbox->font_size = theme->typography.size_normal;
    checkbox->checked = false;
    checkbox->indeterminate = false;

    // Appearance
    checkbox->box_size = 16.0f;
    checkbox->gap = 8.0f;
    checkbox->check_color = theme->colors.fg_primary;
    checkbox->box_color = theme->colors.bg_tertiary;
    checkbox->text_color = theme->colors.fg_primary;

    // Callback
    checkbox->on_change = NULL;
    checkbox->on_change_data = NULL;

    // Set minimum size
    checkbox->base.constraints.min_height = checkbox->box_size;
    checkbox->base.constraints.min_width = checkbox->box_size;

    // Add to parent
    if (parent) {
        vg_widget_add_child(parent, &checkbox->base);
    }

    return checkbox;
}

static void checkbox_destroy(vg_widget_t* widget) {
    vg_checkbox_t* checkbox = (vg_checkbox_t*)widget;
    if (checkbox->text) {
        free((void*)checkbox->text);
        checkbox->text = NULL;
    }
}

static void checkbox_measure(vg_widget_t* widget, float available_width, float available_height) {
    vg_checkbox_t* checkbox = (vg_checkbox_t*)widget;
    (void)available_width;
    (void)available_height;

    float width = checkbox->box_size;
    float height = checkbox->box_size;

    // Add text width if we have text and font
    if (checkbox->text && checkbox->text[0] && checkbox->font) {
        vg_text_metrics_t metrics;
        vg_font_measure_text(checkbox->font, checkbox->font_size, checkbox->text, &metrics);
        width += checkbox->gap + metrics.width;
        if (metrics.height > height) {
            height = metrics.height;
        }
    }

    widget->measured_width = width;
    widget->measured_height = height;

    // Apply constraints
    if (widget->measured_width < widget->constraints.min_width) {
        widget->measured_width = widget->constraints.min_width;
    }
    if (widget->measured_height < widget->constraints.min_height) {
        widget->measured_height = widget->constraints.min_height;
    }
}

static void checkbox_paint(vg_widget_t* widget, void* canvas) {
    vg_checkbox_t* checkbox = (vg_checkbox_t*)widget;
    vg_theme_t* theme = vg_theme_get_current();

    // Determine colors based on state
    uint32_t box_color = checkbox->box_color;
    uint32_t check_color = checkbox->check_color;
    uint32_t text_color = checkbox->text_color;

    if (widget->state & VG_STATE_DISABLED) {
        box_color = theme->colors.bg_disabled;
        check_color = theme->colors.fg_disabled;
        text_color = theme->colors.fg_disabled;
    } else if (widget->state & VG_STATE_HOVERED) {
        box_color = theme->colors.bg_hover;
    }

    // Draw checkbox box
    float box_x = widget->x;
    float box_y = widget->y + (widget->height - checkbox->box_size) / 2.0f;

    // TODO: Draw box background and border using vgfx primitives
    (void)box_x;
    (void)box_y;
    (void)box_color;

    // Draw check mark or indeterminate mark
    if (checkbox->checked || checkbox->indeterminate) {
        // TODO: Draw check mark or dash using vgfx primitives
        (void)check_color;
    }

    // Draw focus ring
    if (widget->state & VG_STATE_FOCUSED) {
        // TODO: Draw focus ring
    }

    // Draw label text
    if (checkbox->text && checkbox->text[0] && checkbox->font) {
        float text_x = widget->x + checkbox->box_size + checkbox->gap;

        vg_font_metrics_t font_metrics;
        vg_font_get_metrics(checkbox->font, checkbox->font_size, &font_metrics);
        float text_y = widget->y + (widget->height + font_metrics.ascent - font_metrics.descent) / 2.0f;

        vg_font_draw_text(canvas, checkbox->font, checkbox->font_size,
                          text_x, text_y, checkbox->text, text_color);
    }
}

static bool checkbox_handle_event(vg_widget_t* widget, vg_event_t* event) {
    vg_checkbox_t* checkbox = (vg_checkbox_t*)widget;

    if (widget->state & VG_STATE_DISABLED) {
        return false;
    }

    if (event->type == VG_EVENT_CLICK) {
        vg_checkbox_toggle(checkbox);
        return true;
    }

    if (event->type == VG_EVENT_KEY_DOWN) {
        if (event->key.key == VG_KEY_SPACE || event->key.key == VG_KEY_ENTER) {
            vg_checkbox_toggle(checkbox);
            return true;
        }
    }

    return false;
}

static bool checkbox_can_focus(vg_widget_t* widget) {
    return widget->enabled && widget->visible;
}

//=============================================================================
// Checkbox API
//=============================================================================

void vg_checkbox_set_checked(vg_checkbox_t* checkbox, bool checked) {
    if (!checkbox) return;

    if (checkbox->checked != checked) {
        checkbox->checked = checked;
        checkbox->indeterminate = false;
        checkbox->base.needs_paint = true;

        if (checked) {
            checkbox->base.state |= VG_STATE_CHECKED;
        } else {
            checkbox->base.state &= ~VG_STATE_CHECKED;
        }

        if (checkbox->on_change) {
            checkbox->on_change(&checkbox->base, checked, checkbox->on_change_data);
        }
    }
}

bool vg_checkbox_is_checked(vg_checkbox_t* checkbox) {
    return checkbox ? checkbox->checked : false;
}

void vg_checkbox_toggle(vg_checkbox_t* checkbox) {
    if (!checkbox) return;

    vg_checkbox_set_checked(checkbox, !checkbox->checked);
}

void vg_checkbox_set_text(vg_checkbox_t* checkbox, const char* text) {
    if (!checkbox) return;

    if (checkbox->text) {
        free((void*)checkbox->text);
    }
    checkbox->text = text ? strdup(text) : strdup("");
    checkbox->base.needs_layout = true;
    checkbox->base.needs_paint = true;
}

void vg_checkbox_set_on_change(vg_checkbox_t* checkbox, vg_checkbox_callback_t callback, void* user_data) {
    if (!checkbox) return;

    checkbox->on_change = callback;
    checkbox->on_change_data = user_data;
}
