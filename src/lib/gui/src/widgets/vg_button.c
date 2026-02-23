// vg_button.c - Button widget implementation
#include "../../../graphics/include/vgfx.h"
#include "../../include/vg_event.h"
#include "../../include/vg_theme.h"
#include "../../include/vg_widgets.h"
#include <stdlib.h>
#include <string.h>

//=============================================================================
// Forward Declarations
//=============================================================================

static void button_destroy(vg_widget_t *widget);
static void button_measure(vg_widget_t *widget, float available_width, float available_height);
static void button_paint(vg_widget_t *widget, void *canvas);
static bool button_handle_event(vg_widget_t *widget, vg_event_t *event);
static bool button_can_focus(vg_widget_t *widget);

//=============================================================================
// Button VTable
//=============================================================================

static vg_widget_vtable_t g_button_vtable = {.destroy = button_destroy,
                                             .measure = button_measure,
                                             .arrange = NULL,
                                             .paint = button_paint,
                                             .handle_event = button_handle_event,
                                             .can_focus = button_can_focus,
                                             .on_focus = NULL};

//=============================================================================
// Helper: Draw rounded rectangle
//=============================================================================

static void draw_filled_rect(void *canvas, float x, float y, float w, float h, uint32_t color)
{
    vgfx_fill_rect((vgfx_window_t)canvas, (int32_t)x, (int32_t)y, (int32_t)w, (int32_t)h, color);
}

//=============================================================================
// Button Implementation
//=============================================================================

vg_button_t *vg_button_create(vg_widget_t *parent, const char *text)
{
    vg_button_t *button = calloc(1, sizeof(vg_button_t));
    if (!button)
        return NULL;

    // Initialize base widget
    vg_widget_init(&button->base, VG_WIDGET_BUTTON, &g_button_vtable);

    // Get theme
    vg_theme_t *theme = vg_theme_get_current();

    // Initialize button-specific fields
    button->text = text ? strdup(text) : strdup("");
    button->font = NULL;
    button->font_size = theme->typography.size_normal;
    button->style = VG_BUTTON_STYLE_DEFAULT;
    button->on_click = NULL;
    button->user_data = NULL;

    // Default appearance from theme
    button->bg_color = theme->colors.bg_tertiary;
    button->fg_color = theme->colors.fg_primary;
    button->border_color = theme->colors.border_primary;
    button->border_radius = theme->button.border_radius;

    // Set minimum size
    button->base.constraints.min_height = theme->button.height;
    button->base.constraints.min_width = 60.0f;

    // Add to parent
    if (parent)
    {
        vg_widget_add_child(parent, &button->base);
    }

    return button;
}

static void button_destroy(vg_widget_t *widget)
{
    vg_button_t *button = (vg_button_t *)widget;
    if (button->text)
    {
        free((void *)button->text);
        button->text = NULL;
    }
    free(button->icon_text);
    button->icon_text = NULL;
}

static void button_measure(vg_widget_t *widget, float available_width, float available_height)
{
    vg_button_t *button = (vg_button_t *)widget;
    (void)available_width;
    (void)available_height;

    vg_theme_t *theme = vg_theme_get_current();
    float padding = theme->button.padding_h;

    // Start with minimum size
    float width = widget->constraints.min_width;
    float height = theme->button.height;

    // If we have a font, measure text and/or icon
    if (button->font)
    {
        float content_w = 0;

        if (button->text && button->text[0])
        {
            vg_text_metrics_t metrics;
            vg_font_measure_text(button->font, button->font_size, button->text, &metrics);
            content_w = metrics.width;
        }

        if (button->icon_text && button->icon_text[0])
        {
            vg_text_metrics_t icon_metrics;
            vg_font_measure_text(button->font, button->font_size, button->icon_text, &icon_metrics);
            content_w += icon_metrics.width;
            if (button->text && button->text[0])
                content_w += 4.0f; // gap between icon and label
        }

        if (content_w > 0)
        {
            width = content_w + padding * 2;
            if (width < widget->constraints.min_width)
                width = widget->constraints.min_width;
        }
    }

    widget->measured_width = width;
    widget->measured_height = height;

    // Apply constraints
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

static void button_paint(vg_widget_t *widget, void *canvas)
{
    vg_button_t *button = (vg_button_t *)widget;
    vg_theme_t *theme = vg_theme_get_current();

    // Determine colors based on state
    uint32_t bg_color = button->bg_color;
    uint32_t fg_color = button->fg_color;

    if (widget->state & VG_STATE_DISABLED)
    {
        bg_color = theme->colors.bg_disabled;
        fg_color = theme->colors.fg_disabled;
    }
    else if (widget->state & VG_STATE_PRESSED)
    {
        bg_color = theme->colors.bg_active;
        fg_color = 0xFFFFFFFF; // White text on active
    }
    else if (widget->state & VG_STATE_HOVERED)
    {
        bg_color = theme->colors.bg_hover;
    }

    // Draw background
    draw_filled_rect(canvas, widget->x, widget->y, widget->width, widget->height, bg_color);

    // Draw border; use focus color when the button has keyboard focus
    uint32_t border = (widget->state & VG_STATE_FOCUSED) ? theme->colors.border_focus
                                                         : button->border_color;
    vgfx_rect((vgfx_window_t)canvas,
              (int32_t)widget->x,
              (int32_t)widget->y,
              (int32_t)widget->width,
              (int32_t)widget->height,
              border);

    // Draw icon and/or text
    if (button->font)
    {
        vg_font_metrics_t font_metrics;
        vg_font_get_metrics(button->font, button->font_size, &font_metrics);
        float baseline_y =
            widget->y + (widget->height - (font_metrics.ascent - font_metrics.descent)) / 2.0f +
            font_metrics.ascent;

        bool has_text = button->text && button->text[0];
        bool has_icon = button->icon_text && button->icon_text[0];

        if (!has_icon)
        {
            // Text-only (original behaviour)
            if (has_text)
            {
                vg_text_metrics_t metrics;
                vg_font_measure_text(button->font, button->font_size, button->text, &metrics);
                float text_x = widget->x + (widget->width - metrics.width) / 2.0f;
                vg_font_draw_text(
                    canvas, button->font, button->font_size, text_x, baseline_y,
                    button->text, fg_color);
            }
        }
        else
        {
            // Measure both pieces
            vg_text_metrics_t icon_m = {0};
            vg_text_metrics_t text_m = {0};
            vg_font_measure_text(button->font, button->font_size, button->icon_text, &icon_m);
            if (has_text)
                vg_font_measure_text(button->font, button->font_size, button->text, &text_m);

            float gap = has_text ? 4.0f : 0.0f;
            float total_w = icon_m.width + gap + text_m.width;
            float start_x = widget->x + (widget->width - total_w) / 2.0f;

            if (button->icon_pos == 1)
            {
                // icon on the right
                float text_x = start_x;
                if (has_text)
                    vg_font_draw_text(canvas, button->font, button->font_size,
                                      text_x, baseline_y, button->text, fg_color);
                float icon_x = start_x + text_m.width + gap;
                vg_font_draw_text(canvas, button->font, button->font_size,
                                  icon_x, baseline_y, button->icon_text, fg_color);
            }
            else
            {
                // icon on the left (default)
                vg_font_draw_text(canvas, button->font, button->font_size,
                                  start_x, baseline_y, button->icon_text, fg_color);
                if (has_text)
                {
                    float text_x = start_x + icon_m.width + gap;
                    vg_font_draw_text(canvas, button->font, button->font_size,
                                      text_x, baseline_y, button->text, fg_color);
                }
            }
        }
    }
}

static bool button_handle_event(vg_widget_t *widget, vg_event_t *event)
{
    vg_button_t *button = (vg_button_t *)widget;

    if (widget->state & VG_STATE_DISABLED)
    {
        return false;
    }

    if (event->type == VG_EVENT_CLICK)
    {
        if (button->on_click)
        {
            button->on_click(widget, button->user_data);
        }
        // Also call widget's generic on_click if set
        if (widget->on_click)
        {
            widget->on_click(widget, widget->callback_data);
        }
        return true;
    }

    if (event->type == VG_EVENT_KEY_DOWN)
    {
        if (event->key.key == VG_KEY_SPACE || event->key.key == VG_KEY_ENTER)
        {
            if (button->on_click)
                button->on_click(widget, button->user_data);
            if (widget->on_click)
                widget->on_click(widget, widget->callback_data);
            event->handled = true;
            return true;
        }
    }

    return false;
}

static bool button_can_focus(vg_widget_t *widget)
{
    return widget->enabled && widget->visible;
}

//=============================================================================
// Button API
//=============================================================================

void vg_button_set_text(vg_button_t *button, const char *text)
{
    if (!button)
        return;

    if (button->text)
    {
        free((void *)button->text);
    }
    button->text = text ? strdup(text) : strdup("");
    button->base.needs_layout = true;
    button->base.needs_paint = true;
}

const char *vg_button_get_text(vg_button_t *button)
{
    return button ? button->text : NULL;
}

void vg_button_set_on_click(vg_button_t *button, vg_button_callback_t callback, void *user_data)
{
    if (!button)
        return;

    button->on_click = callback;
    button->user_data = user_data;
}

void vg_button_set_style(vg_button_t *button, vg_button_style_t style)
{
    if (!button)
        return;

    button->style = style;
    vg_theme_t *theme = vg_theme_get_current();

    // Update colors based on style
    switch (style)
    {
        case VG_BUTTON_STYLE_PRIMARY:
            button->bg_color = theme->colors.accent_primary;
            button->fg_color = 0xFFFFFFFF; // White
            break;
        case VG_BUTTON_STYLE_SECONDARY:
            button->bg_color = theme->colors.bg_tertiary;
            button->fg_color = theme->colors.fg_primary;
            break;
        case VG_BUTTON_STYLE_DANGER:
            button->bg_color = theme->colors.accent_danger;
            button->fg_color = 0xFFFFFFFF;
            break;
        case VG_BUTTON_STYLE_TEXT:
            button->bg_color = 0x00000000; // Transparent
            button->fg_color = theme->colors.fg_link;
            break;
        default:
            button->bg_color = theme->colors.bg_tertiary;
            button->fg_color = theme->colors.fg_primary;
            break;
    }

    button->base.needs_paint = true;
}

void vg_button_set_font(vg_button_t *button, vg_font_t *font, float size)
{
    if (!button)
        return;

    button->font = font;
    button->font_size = size > 0 ? size : vg_theme_get_current()->typography.size_normal;
    button->base.needs_layout = true;
    button->base.needs_paint = true;
}

void vg_button_set_icon(vg_button_t *button, const char *icon)
{
    if (!button)
        return;

    free(button->icon_text);
    button->icon_text = icon ? strdup(icon) : NULL;
    button->base.needs_layout = true;
    button->base.needs_paint = true;
}

void vg_button_set_icon_position(vg_button_t *button, int pos)
{
    if (!button)
        return;

    button->icon_pos = pos;
    button->base.needs_paint = true;
}
