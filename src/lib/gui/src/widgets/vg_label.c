// vg_label.c - Label widget implementation
#include "../../include/vg_theme.h"
#include "../../include/vg_widgets.h"
#include <stdlib.h>
#include <string.h>

//=============================================================================
// Forward Declarations
//=============================================================================

static void label_destroy(vg_widget_t *widget);
static void label_measure(vg_widget_t *widget, float available_width, float available_height);
static void label_paint(vg_widget_t *widget, void *canvas);

//=============================================================================
// Label VTable
//=============================================================================

static vg_widget_vtable_t g_label_vtable = {.destroy = label_destroy,
                                            .measure = label_measure,
                                            .arrange = NULL, // Labels don't have children to layout
                                            .paint = label_paint,
                                            .handle_event =
                                                NULL, // Labels don't handle events by default
                                            .can_focus = NULL,
                                            .on_focus = NULL};

//=============================================================================
// Label Implementation
//=============================================================================

vg_label_t *vg_label_create(vg_widget_t *parent, const char *text)
{
    vg_label_t *label = calloc(1, sizeof(vg_label_t));
    if (!label)
        return NULL;

    // Initialize base widget
    vg_widget_init(&label->base, VG_WIDGET_LABEL, &g_label_vtable);

    // Initialize label-specific fields
    label->text = text ? strdup(text) : strdup("");
    label->font = NULL;
    label->font_size = 13.0f; // Default size
    label->text_color = vg_theme_get_current()->colors.fg_primary;
    label->h_align = VG_ALIGN_H_LEFT;
    label->v_align = VG_ALIGN_V_CENTER;
    label->word_wrap = false;
    label->max_lines = 0;

    // Add to parent
    if (parent)
    {
        vg_widget_add_child(parent, &label->base);
    }

    return label;
}

static void label_destroy(vg_widget_t *widget)
{
    vg_label_t *label = (vg_label_t *)widget;
    if (label->text)
    {
        free((void *)label->text);
        label->text = NULL;
    }
}

static void label_measure(vg_widget_t *widget, float available_width, float available_height)
{
    vg_label_t *label = (vg_label_t *)widget;

    if (!label->text || !label->font)
    {
        // No text or font - use minimum size
        widget->measured_width = widget->constraints.min_width;
        widget->measured_height = widget->constraints.min_height;
        return;
    }

    // Measure text
    vg_text_metrics_t metrics;
    vg_font_measure_text(label->font, label->font_size, label->text, &metrics);

    widget->measured_width = metrics.width;
    widget->measured_height = metrics.height;

    // Apply constraints
    if (widget->measured_width < widget->constraints.min_width)
    {
        widget->measured_width = widget->constraints.min_width;
    }
    if (widget->measured_height < widget->constraints.min_height)
    {
        widget->measured_height = widget->constraints.min_height;
    }
    if (widget->constraints.max_width > 0 && widget->measured_width > widget->constraints.max_width)
    {
        widget->measured_width = widget->constraints.max_width;
    }
    if (widget->constraints.max_height > 0 &&
        widget->measured_height > widget->constraints.max_height)
    {
        widget->measured_height = widget->constraints.max_height;
    }
}

static void label_paint(vg_widget_t *widget, void *canvas)
{
    vg_label_t *label = (vg_label_t *)widget;

    if (!label->text || !label->text[0])
    {
        return; // Nothing to draw
    }

    if (!label->font)
    {
        return; // No font set
    }

    // Calculate text position based on alignment
    float text_x = widget->x;
    float text_y = widget->y;

    vg_text_metrics_t metrics;
    vg_font_measure_text(label->font, label->font_size, label->text, &metrics);

    // Get font metrics for baseline
    vg_font_metrics_t font_metrics;
    vg_font_get_metrics(label->font, label->font_size, &font_metrics);

    // Horizontal alignment
    switch (label->h_align)
    {
        case VG_ALIGN_H_CENTER:
            text_x += (widget->width - metrics.width) / 2.0f;
            break;
        case VG_ALIGN_H_RIGHT:
            text_x += widget->width - metrics.width;
            break;
        default:
            // Left alignment - use x
            break;
    }

    // Vertical alignment
    switch (label->v_align)
    {
        case VG_ALIGN_V_CENTER:
            text_y += (widget->height - metrics.height) / 2.0f + font_metrics.ascent;
            break;
        case VG_ALIGN_V_BOTTOM:
            text_y += widget->height - font_metrics.descent;
            break;
        case VG_ALIGN_V_BASELINE:
            text_y += font_metrics.ascent;
            break;
        default:
            // Top alignment
            text_y += font_metrics.ascent;
            break;
    }

    // Draw text
    uint32_t color = (widget->state & VG_STATE_DISABLED)
                         ? vg_theme_get_current()->colors.fg_disabled
                         : label->text_color;

    vg_font_draw_text(canvas, label->font, label->font_size, text_x, text_y, label->text, color);
}

//=============================================================================
// Label API
//=============================================================================

void vg_label_set_text(vg_label_t *label, const char *text)
{
    if (!label)
        return;

    if (label->text)
    {
        free((void *)label->text);
    }
    label->text = text ? strdup(text) : strdup("");
    label->base.needs_layout = true;
    label->base.needs_paint = true;
}

const char *vg_label_get_text(vg_label_t *label)
{
    return label ? label->text : NULL;
}

void vg_label_set_font(vg_label_t *label, vg_font_t *font, float size)
{
    if (!label)
        return;

    label->font = font;
    label->font_size = size > 0 ? size : 13.0f;
    label->base.needs_layout = true;
    label->base.needs_paint = true;
}

void vg_label_set_color(vg_label_t *label, uint32_t color)
{
    if (!label)
        return;

    label->text_color = color;
    label->base.needs_paint = true;
}

void vg_label_set_alignment(vg_label_t *label, vg_h_align_t h_align, vg_v_align_t v_align)
{
    if (!label)
        return;

    label->h_align = h_align;
    label->v_align = v_align;
    label->base.needs_paint = true;
}
