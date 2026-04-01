//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/lib/gui/src/widgets/vg_tooltip.c
//
//===----------------------------------------------------------------------===//
// vg_tooltip.c - Tooltip widget implementation
#include "../../include/vg_event.h"
#include "../../include/vg_ide_widgets.h"
#include "../../include/vg_theme.h"
#include <stdint.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif
#include <stdlib.h>
#include <string.h>

//=============================================================================
// Forward Declarations
//=============================================================================

static void tooltip_destroy(vg_widget_t *widget);
static void tooltip_measure(vg_widget_t *widget, float available_width, float available_height);
static void tooltip_paint(vg_widget_t *widget, void *canvas);

//=============================================================================
// Tooltip VTable
//=============================================================================

static vg_widget_vtable_t g_tooltip_vtable = {.destroy = tooltip_destroy,
                                              .measure = tooltip_measure,
                                              .arrange = NULL,
                                              .paint = tooltip_paint,
                                              .handle_event = NULL,
                                              .can_focus = NULL,
                                              .on_focus = NULL};

//=============================================================================
// Global Tooltip Manager
//=============================================================================

static vg_tooltip_manager_t g_tooltip_manager = {0};

static uint64_t tooltip_now_ms(void) {
#ifdef _WIN32
    return (uint64_t)GetTickCount64();
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
#endif
}

vg_tooltip_manager_t *vg_tooltip_manager_get(void) {
    return &g_tooltip_manager;
}

//=============================================================================
// Tooltip Implementation
//=============================================================================

vg_tooltip_t *vg_tooltip_create(void) {
    vg_tooltip_t *tooltip = calloc(1, sizeof(vg_tooltip_t));
    if (!tooltip)
        return NULL;

    vg_widget_init(&tooltip->base, VG_WIDGET_CUSTOM, &g_tooltip_vtable);

    vg_theme_t *theme = vg_theme_get_current();

    // Defaults
    tooltip->show_delay_ms = 500;
    tooltip->hide_delay_ms = 100;
    tooltip->duration_ms = 0; // Stay until leave

    tooltip->position_mode = VG_TOOLTIP_FOLLOW_CURSOR;
    tooltip->offset_x = 10;
    tooltip->offset_y = 20;

    tooltip->max_width = 300;
    tooltip->padding = 6;
    tooltip->corner_radius = 4;
    tooltip->bg_color = 0xF0333333; // Dark semi-transparent
    tooltip->text_color = 0xFFFFFFFF;
    tooltip->border_color = 0xFF555555;

    tooltip->font_size = theme->typography.size_small;
    tooltip->is_visible = false;

    return tooltip;
}

static void tooltip_destroy(vg_widget_t *widget) {
    vg_tooltip_t *tooltip = (vg_tooltip_t *)widget;
    free(tooltip->text);
}

/// @brief Tooltip destroy.
void vg_tooltip_destroy(vg_tooltip_t *tooltip) {
    if (!tooltip)
        return;
    vg_widget_destroy(&tooltip->base);
}

static void tooltip_measure(vg_widget_t *widget, float available_width, float available_height) {
    vg_tooltip_t *tooltip = (vg_tooltip_t *)widget;
    (void)available_width;
    (void)available_height;

    if (!tooltip->text || !tooltip->font) {
        widget->measured_width = 0;
        widget->measured_height = 0;
        return;
    }

    vg_text_metrics_t metrics;
    vg_font_measure_text(tooltip->font, tooltip->font_size, tooltip->text, &metrics);

    float text_width = metrics.width;
    if (text_width > tooltip->max_width - tooltip->padding * 2) {
        text_width = tooltip->max_width - tooltip->padding * 2;
    }

    widget->measured_width = text_width + tooltip->padding * 2;
    widget->measured_height = metrics.height + tooltip->padding * 2;
}

static void tooltip_paint(vg_widget_t *widget, void *canvas) {
    vg_tooltip_t *tooltip = (vg_tooltip_t *)widget;

    if (!tooltip->is_visible || !tooltip->text)
        return;

    // Background (placeholder - actual drawing via vgfx)
    (void)tooltip->bg_color;
    (void)tooltip->border_color;

    // Draw text
    if (tooltip->font && tooltip->text[0]) {
        float text_x = widget->x + tooltip->padding;
        float text_y = widget->y + tooltip->padding;

        vg_font_draw_text(canvas,
                          tooltip->font,
                          tooltip->font_size,
                          text_x,
                          text_y,
                          tooltip->text,
                          tooltip->text_color);
    }
}

/// @brief Tooltip set text.
void vg_tooltip_set_text(vg_tooltip_t *tooltip, const char *text) {
    if (!tooltip)
        return;

    free(tooltip->text);
    tooltip->text = text ? strdup(text) : NULL;
    tooltip->base.needs_layout = true;
}

/// @brief Tooltip show at.
void vg_tooltip_show_at(vg_tooltip_t *tooltip, int x, int y) {
    if (!tooltip)
        return;

    tooltip->base.x = (float)x + tooltip->offset_x;
    tooltip->base.y = (float)y + tooltip->offset_y;
    tooltip->is_visible = true;
    tooltip->base.visible = true;
    tooltip->base.needs_paint = true;
}

/// @brief Tooltip hide.
void vg_tooltip_hide(vg_tooltip_t *tooltip) {
    if (!tooltip)
        return;

    tooltip->is_visible = false;
    tooltip->base.visible = false;
}

/// @brief Tooltip set anchor.
void vg_tooltip_set_anchor(vg_tooltip_t *tooltip, vg_widget_t *anchor) {
    if (!tooltip)
        return;

    tooltip->anchor_widget = anchor;
    tooltip->position_mode = VG_TOOLTIP_ANCHOR_WIDGET;
}

/// @brief Tooltip set timing.
void vg_tooltip_set_timing(vg_tooltip_t *tooltip,
                           uint32_t show_delay_ms,
                           uint32_t hide_delay_ms,
                           uint32_t duration_ms) {
    if (!tooltip)
        return;

    tooltip->show_delay_ms = show_delay_ms;
    tooltip->hide_delay_ms = hide_delay_ms;
    tooltip->duration_ms = duration_ms;
}

//=============================================================================
// Tooltip Manager Implementation
//=============================================================================

void vg_tooltip_manager_update(vg_tooltip_manager_t *mgr, uint64_t now_ms) {
    if (!mgr)
        return;

    // Check if pending show should trigger
    if (mgr->pending_show && mgr->hovered_widget) {
        if (mgr->active_tooltip &&
            now_ms >= mgr->hover_start_time + mgr->active_tooltip->show_delay_ms) {
            vg_tooltip_show_at(mgr->active_tooltip, mgr->cursor_x, mgr->cursor_y);
            mgr->pending_show = false;
        }
    }

    // Check auto-hide
    if (mgr->active_tooltip && mgr->active_tooltip->is_visible &&
        mgr->active_tooltip->duration_ms > 0) {
        // Auto-hide logic would go here
        (void)now_ms;
    }
}

/// @brief Tooltip manager on hover.
void vg_tooltip_manager_on_hover(vg_tooltip_manager_t *mgr, vg_widget_t *widget, int x, int y) {
    if (!mgr)
        return;

    mgr->cursor_x = x;
    mgr->cursor_y = y;

    if (widget != mgr->hovered_widget) {
        // Widget changed
        if (mgr->active_tooltip) {
            vg_tooltip_hide(mgr->active_tooltip);
        }

        mgr->hovered_widget = widget;
        if (widget && widget->tooltip_text && widget->tooltip_text[0]) {
            if (!mgr->active_tooltip) {
                mgr->active_tooltip = vg_tooltip_create();
            }
            if (mgr->active_tooltip) {
                vg_tooltip_set_text(mgr->active_tooltip, widget->tooltip_text);
                mgr->pending_show = true;
            } else {
                mgr->pending_show = false;
            }
        } else {
            mgr->pending_show = false;
        }
        mgr->hover_start_time = tooltip_now_ms();
    }
}

/// @brief Tooltip manager on leave.
void vg_tooltip_manager_on_leave(vg_tooltip_manager_t *mgr) {
    if (!mgr)
        return;

    if (mgr->active_tooltip) {
        vg_tooltip_hide(mgr->active_tooltip);
    }

    mgr->hovered_widget = NULL;
    mgr->pending_show = false;
}

/// @brief Widget set tooltip text.
void vg_widget_set_tooltip_text(vg_widget_t *widget, const char *text) {
    if (!widget)
        return;
    free(widget->tooltip_text);
    widget->tooltip_text = (text && text[0]) ? strdup(text) : NULL;
}
