//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/lib/gui/src/widgets/vg_button.c
//
//===----------------------------------------------------------------------===//
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
// Helpers
//=============================================================================

static int button_corner_radius(const vg_button_t *button, const vg_theme_t *theme) {
    float radius = button && button->border_radius > 0.0f
                       ? button->border_radius
                       : (theme ? theme->button.border_radius : 4.0f);
    if (radius < 2.0f)
        radius = 2.0f;
    return (int)radius;
}

static void button_fill_round_rect(vgfx_window_t win,
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

static void button_stroke_round_rect(vgfx_window_t win,
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

//=============================================================================
// Button Implementation
//=============================================================================

vg_button_t *vg_button_create(vg_widget_t *parent, const char *text) {
    vg_button_t *button = calloc(1, sizeof(vg_button_t));
    if (!button)
        return NULL;

    // Initialize base widget
    vg_widget_init(&button->base, VG_WIDGET_BUTTON, &g_button_vtable);

    // Get theme
    vg_theme_t *theme = vg_theme_get_current();

    // Initialize button-specific fields
    button->text = text ? strdup(text) : strdup("");
    button->font = theme->typography.font_regular;
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
    if (parent) {
        vg_widget_add_child(parent, &button->base);
    }

    return button;
}

static void button_destroy(vg_widget_t *widget) {
    vg_button_t *button = (vg_button_t *)widget;
    if (button->text) {
        free((void *)button->text);
        button->text = NULL;
    }
    free(button->icon_text);
    button->icon_text = NULL;
}

static void button_measure(vg_widget_t *widget, float available_width, float available_height) {
    vg_button_t *button = (vg_button_t *)widget;
    (void)available_width;
    (void)available_height;

    vg_theme_t *theme = vg_theme_get_current();
    float padding = theme->button.padding_h;

    // Start with minimum size
    float width = widget->constraints.min_width;
    float height = theme->button.height;

    // If we have a font, measure text and/or icon
    if (button->font) {
        float content_w = 0;

        if (button->text && button->text[0]) {
            vg_text_metrics_t metrics;
            vg_font_measure_text(button->font, button->font_size, button->text, &metrics);
            content_w = metrics.width;
        }

        if (button->icon_text && button->icon_text[0]) {
            vg_text_metrics_t icon_metrics;
            vg_font_measure_text(button->font, button->font_size, button->icon_text, &icon_metrics);
            content_w += icon_metrics.width;
            if (button->text && button->text[0])
                content_w += 4.0f; // gap between icon and label
        }

        if (content_w > 0) {
            width = content_w + padding * 2;
            if (width < widget->constraints.min_width)
                width = widget->constraints.min_width;
        }
    }

    widget->measured_width = width;
    widget->measured_height = height;

    // Apply constraints
    if (widget->constraints.max_width > 0 &&
        widget->measured_width > widget->constraints.max_width) {
        widget->measured_width = widget->constraints.max_width;
    }
    if (widget->constraints.max_height > 0 &&
        widget->measured_height > widget->constraints.max_height) {
        widget->measured_height = widget->constraints.max_height;
    }
}

static void button_paint(vg_widget_t *widget, void *canvas) {
    vg_button_t *button = (vg_button_t *)widget;
    vg_theme_t *theme = vg_theme_get_current();
    vgfx_window_t win = (vgfx_window_t)canvas;
    int radius = button_corner_radius(button, theme);

    uint32_t bg_color = button->bg_color ? button->bg_color : theme->colors.bg_tertiary;
    uint32_t fg_color = button->fg_color ? button->fg_color : theme->colors.fg_primary;
    uint32_t border_color = button->border_color ? button->border_color : theme->colors.border_primary;

    if (button->style == VG_BUTTON_STYLE_PRIMARY) {
        bg_color = theme->colors.accent_primary;
        fg_color = 0x00FFFFFF;
        border_color = vg_color_blend(theme->colors.accent_primary, theme->colors.border_focus, 0.45f);
    } else if (button->style == VG_BUTTON_STYLE_DANGER) {
        bg_color = theme->colors.accent_danger;
        fg_color = 0x00FFFFFF;
        border_color = vg_color_darken(theme->colors.accent_danger, 0.18f);
    } else if (button->style == VG_BUTTON_STYLE_SECONDARY) {
        bg_color = vg_color_blend(theme->colors.bg_secondary, theme->colors.bg_primary, 0.35f);
        fg_color = theme->colors.fg_primary;
        border_color = theme->colors.border_secondary;
    } else if (button->style == VG_BUTTON_STYLE_TEXT) {
        bg_color = theme->colors.bg_primary;
        fg_color = theme->colors.fg_link;
        border_color = bg_color;
    }

    if (widget->state & VG_STATE_DISABLED) {
        bg_color = theme->colors.bg_disabled;
        fg_color = theme->colors.fg_disabled;
        border_color = theme->colors.border_secondary;
    } else if (widget->state & VG_STATE_PRESSED) {
        bg_color = vg_color_darken(bg_color, button->style == VG_BUTTON_STYLE_TEXT ? 0.04f : 0.10f);
    } else if (widget->state & VG_STATE_HOVERED) {
        bg_color = vg_color_lighten(bg_color, button->style == VG_BUTTON_STYLE_TEXT ? 0.03f : 0.05f);
    }

    button_fill_round_rect(
        win, (int32_t)widget->x, (int32_t)widget->y, (int32_t)widget->width, (int32_t)widget->height, radius, bg_color);
    if (button->style != VG_BUTTON_STYLE_TEXT) {
        vgfx_fill_rect(win,
                       (int32_t)widget->x + radius,
                       (int32_t)widget->y + 1,
                       (int32_t)widget->width - radius * 2,
                       1,
                       vg_color_lighten(bg_color, 0.08f));
    }

    if (widget->state & VG_STATE_FOCUSED) {
        border_color = theme->colors.border_focus;
    }
    if (button->style != VG_BUTTON_STYLE_TEXT || (widget->state & (VG_STATE_HOVERED | VG_STATE_FOCUSED))) {
        button_stroke_round_rect(win,
                                 (int32_t)widget->x,
                                 (int32_t)widget->y,
                                 (int32_t)widget->width,
                                 (int32_t)widget->height,
                                 radius,
                                 border_color);
    }

    if (button->font) {
        vg_font_metrics_t font_metrics;
        vg_font_get_metrics(button->font, button->font_size, &font_metrics);
        float baseline_y = widget->y +
                           (widget->height - (font_metrics.ascent - font_metrics.descent)) / 2.0f +
                           font_metrics.ascent;
        int32_t clip_x = (int32_t)widget->x + 6;
        int32_t clip_y = (int32_t)widget->y + 2;
        int32_t clip_w = (int32_t)widget->width - 12;
        int32_t clip_h = (int32_t)widget->height - 4;
        if (clip_w < 0)
            clip_w = 0;
        if (clip_h < 0)
            clip_h = 0;
        vgfx_set_clip(win, clip_x, clip_y, clip_w, clip_h);

        bool has_text = button->text && button->text[0];
        bool has_icon = button->icon_text && button->icon_text[0];

        if (!has_icon) {
            // Text-only (original behaviour)
            if (has_text) {
                vg_text_metrics_t metrics;
                vg_font_measure_text(button->font, button->font_size, button->text, &metrics);
                float text_x = widget->x + (widget->width - metrics.width) / 2.0f;
                vg_font_draw_text(canvas,
                                  button->font,
                                  button->font_size,
                                  text_x,
                                  baseline_y,
                                  button->text,
                                  fg_color);
            }
        } else {
            // Measure both pieces
            vg_text_metrics_t icon_m = {0};
            vg_text_metrics_t text_m = {0};
            vg_font_measure_text(button->font, button->font_size, button->icon_text, &icon_m);
            if (has_text)
                vg_font_measure_text(button->font, button->font_size, button->text, &text_m);

            float gap = has_text ? 4.0f : 0.0f;
            float total_w = icon_m.width + gap + text_m.width;
            float start_x = widget->x + (widget->width - total_w) / 2.0f;

            if (button->icon_pos == 1) {
                // icon on the right
                float text_x = start_x;
                if (has_text)
                    vg_font_draw_text(canvas,
                                      button->font,
                                      button->font_size,
                                      text_x,
                                      baseline_y,
                                      button->text,
                                      fg_color);
                float icon_x = start_x + text_m.width + gap;
                vg_font_draw_text(canvas,
                                  button->font,
                                  button->font_size,
                                  icon_x,
                                  baseline_y,
                                  button->icon_text,
                                  fg_color);
            } else {
                // icon on the left (default)
                vg_font_draw_text(canvas,
                                  button->font,
                                  button->font_size,
                                  start_x,
                                  baseline_y,
                                  button->icon_text,
                                  fg_color);
                if (has_text) {
                    float text_x = start_x + icon_m.width + gap;
                    vg_font_draw_text(canvas,
                                      button->font,
                                      button->font_size,
                                      text_x,
                                      baseline_y,
                                      button->text,
                                      fg_color);
                }
            }
        }
        vgfx_clear_clip(win);
    }
}

static bool button_handle_event(vg_widget_t *widget, vg_event_t *event) {
    vg_button_t *button = (vg_button_t *)widget;

    if (widget->state & VG_STATE_DISABLED) {
        return false;
    }

    if (event->type == VG_EVENT_CLICK) {
        if (button->on_click) {
            button->on_click(widget, button->user_data);
        }
        // Also call widget's generic on_click if set
        if (widget->on_click) {
            widget->on_click(widget, widget->callback_data);
        }
        return true;
    }

    if (event->type == VG_EVENT_KEY_DOWN) {
        if (event->key.key == VG_KEY_SPACE || event->key.key == VG_KEY_ENTER) {
            if (event->key.repeat) {
                event->handled = true;
                return true;
            }
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

static bool button_can_focus(vg_widget_t *widget) {
    return widget->enabled && widget->visible;
}

//=============================================================================
// Button API
//=============================================================================

void vg_button_set_text(vg_button_t *button, const char *text) {
    if (!button)
        return;

    if (button->text) {
        free((void *)button->text);
    }
    button->text = text ? strdup(text) : strdup("");
    button->base.needs_layout = true;
    button->base.needs_paint = true;
}

const char *vg_button_get_text(vg_button_t *button) {
    return button ? button->text : NULL;
}

/// @brief Button set on click.
void vg_button_set_on_click(vg_button_t *button, vg_button_callback_t callback, void *user_data) {
    if (!button)
        return;

    button->on_click = callback;
    button->user_data = user_data;
}

/// @brief Button set style.
void vg_button_set_style(vg_button_t *button, vg_button_style_t style) {
    if (!button)
        return;

    button->style = style;
    vg_theme_t *theme = vg_theme_get_current();

    // Update colors based on style
    switch (style) {
        case VG_BUTTON_STYLE_PRIMARY:
            button->bg_color = theme->colors.accent_primary;
            button->fg_color = 0x00FFFFFF; // White
            button->border_color =
                vg_color_blend(theme->colors.accent_primary, theme->colors.border_focus, 0.45f);
            break;
        case VG_BUTTON_STYLE_SECONDARY:
            button->bg_color = vg_color_blend(theme->colors.bg_secondary, theme->colors.bg_primary, 0.35f);
            button->fg_color = theme->colors.fg_primary;
            button->border_color = theme->colors.border_secondary;
            break;
        case VG_BUTTON_STYLE_DANGER:
            button->bg_color = theme->colors.accent_danger;
            button->fg_color = 0x00FFFFFF;
            button->border_color = vg_color_darken(theme->colors.accent_danger, 0.18f);
            break;
        case VG_BUTTON_STYLE_TEXT:
            button->bg_color = theme->colors.bg_primary;
            button->fg_color = theme->colors.fg_link;
            button->border_color = theme->colors.bg_primary;
            break;
        default:
            button->bg_color = theme->colors.bg_tertiary;
            button->fg_color = theme->colors.fg_primary;
            button->border_color = theme->colors.border_primary;
            break;
    }

    button->base.needs_paint = true;
}

/// @brief Button set font.
void vg_button_set_font(vg_button_t *button, vg_font_t *font, float size) {
    if (!button)
        return;

    button->font = font;
    button->font_size = size > 0 ? size : vg_theme_get_current()->typography.size_normal;
    button->base.needs_layout = true;
    button->base.needs_paint = true;
}

/// @brief Button set icon.
void vg_button_set_icon(vg_button_t *button, const char *icon) {
    if (!button)
        return;

    free(button->icon_text);
    button->icon_text = icon ? strdup(icon) : NULL;
    button->base.needs_layout = true;
    button->base.needs_paint = true;
}

/// @brief Button set icon position.
void vg_button_set_icon_position(vg_button_t *button, int pos) {
    if (!button)
        return;

    button->icon_pos = pos;
    button->base.needs_paint = true;
}
