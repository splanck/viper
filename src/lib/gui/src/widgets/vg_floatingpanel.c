//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/lib/gui/src/widgets/vg_floatingpanel.c
//
//===----------------------------------------------------------------------===//
// vg_floatingpanel.c - Floating overlay panel widget
//
// A lightweight overlay that draws at an absolute screen position regardless
// of the normal layout hierarchy. Children are part of the widget tree so hit
// testing, focus, and destruction use the normal widget machinery, but the
// panel renders them during the overlay pass so they appear above normal content.
//
#include "../../../graphics/include/vgfx.h"
#include "../../include/vg_event.h"
#include "../../include/vg_ide_widgets.h"
#include "../../include/vg_widget.h"
#include <stdlib.h>
#include <string.h>

//=============================================================================
// Forward Declarations
//=============================================================================

static void floatingpanel_destroy(vg_widget_t *widget);
static void floatingpanel_measure(vg_widget_t *widget,
                                  float available_width,
                                  float available_height);
static void floatingpanel_arrange(vg_widget_t *widget, float x, float y, float width, float height);
static void floatingpanel_paint(vg_widget_t *widget, void *canvas);
static void floatingpanel_paint_overlay(vg_widget_t *widget, void *canvas);
static void floatingpanel_render_normal_subtree(vg_widget_t *widget,
                                                void *canvas,
                                                float parent_abs_x,
                                                float parent_abs_y);
static void floatingpanel_render_overlay_subtree(vg_widget_t *widget,
                                                 void *canvas,
                                                 float parent_abs_x,
                                                 float parent_abs_y);

//=============================================================================
// VTable
//=============================================================================

static vg_widget_vtable_t g_floatingpanel_vtable = {
    .destroy = floatingpanel_destroy,
    .measure = floatingpanel_measure,
    .arrange = floatingpanel_arrange,
    .paint = floatingpanel_paint,
    .paint_overlay = floatingpanel_paint_overlay,
    .handle_event = NULL,
    .can_focus = NULL,
    .on_focus = NULL,
};

//=============================================================================
// Implementation
//=============================================================================

vg_floatingpanel_t *vg_floatingpanel_create(vg_widget_t *root) {
    vg_floatingpanel_t *panel = calloc(1, sizeof(vg_floatingpanel_t));
    if (!panel)
        return NULL;

    vg_widget_init(&panel->base, VG_WIDGET_CUSTOM, &g_floatingpanel_vtable);

    // Defaults — dark popup background matching IDE theme
    panel->bg_color = 0xFF252526u; // VS-Code-style dark panel
    panel->border_color = 0xFF454545u;
    panel->border_width = 1.0f;

    // Initially hidden
    panel->base.visible = false;

    // Attach to root widget so paint_overlay is dispatched
    if (root)
        vg_widget_add_child(root, &panel->base);

    return panel;
}

static void floatingpanel_destroy(vg_widget_t *widget) {
    (void)widget;
}

/// @brief Floatingpanel destroy.
void vg_floatingpanel_destroy(vg_floatingpanel_t *panel) {
    if (!panel)
        return;
    vg_widget_destroy(&panel->base);
}

static void floatingpanel_measure(vg_widget_t *widget,
                                  float available_width,
                                  float available_height) {
    // The panel takes zero space in the normal layout pass.
    (void)available_width;
    (void)available_height;
    widget->measured_width = 0.0f;
    widget->measured_height = 0.0f;
}

static void floatingpanel_arrange(vg_widget_t *widget, float x, float y, float width, float height) {
    (void)x;
    (void)y;
    (void)width;
    (void)height;

    vg_floatingpanel_t *panel = (vg_floatingpanel_t *)widget;
    widget->x = panel->abs_x;
    widget->y = panel->abs_y;
    widget->width = panel->abs_w;
    widget->height = panel->abs_h;

    float content_x = widget->layout.padding_left;
    float content_y = widget->layout.padding_top;
    float content_w = widget->width - widget->layout.padding_left - widget->layout.padding_right;
    float content_h = widget->height - widget->layout.padding_top - widget->layout.padding_bottom;

    VG_FOREACH_VISIBLE_CHILD(widget, child) {
        float child_w = content_w - child->layout.margin_left - child->layout.margin_right;
        float child_h = content_h - child->layout.margin_top - child->layout.margin_bottom;
        vg_widget_measure(child, content_w, content_h);
        vg_widget_arrange(child,
                          content_x + child->layout.margin_left,
                          content_y + child->layout.margin_top,
                          child_w > 0.0f ? child_w : child->measured_width,
                          child_h > 0.0f ? child_h : child->measured_height);
    }
}

// Normal paint pass — do nothing; all drawing happens in paint_overlay.
static void floatingpanel_paint(vg_widget_t *widget, void *canvas) {
    (void)widget;
    (void)canvas;
}

// Overlay pass — paint background, border, and children.
static void floatingpanel_paint_overlay(vg_widget_t *widget, void *canvas) {
    vg_floatingpanel_t *panel = (vg_floatingpanel_t *)widget;

    if (!panel->base.visible || panel->abs_w <= 0.0f || panel->abs_h <= 0.0f)
        return;

    vgfx_window_t win = (vgfx_window_t)canvas;

    int32_t px = (int32_t)widget->x;
    int32_t py = (int32_t)widget->y;
    int32_t pw = (int32_t)widget->width;
    int32_t ph = (int32_t)widget->height;

    // Background fill
    vgfx_fill_rect(win, px, py, pw, ph, panel->bg_color);

    // Border (1-px rectangles on each edge)
    if (panel->border_width > 0.0f) {
        int32_t bw = (int32_t)panel->border_width;
        uint32_t bc = panel->border_color;
        // Top
        vgfx_fill_rect(win, px, py, pw, bw, bc);
        // Bottom
        vgfx_fill_rect(win, px, py + ph - bw, pw, bw, bc);
        // Left
        vgfx_fill_rect(win, px, py, bw, ph, bc);
        // Right
        vgfx_fill_rect(win, px + pw - bw, py, bw, ph, bc);
    }

    if (pw > 0 && ph > 0) {
        vgfx_set_clip(win, px, py, pw, ph);
        VG_FOREACH_VISIBLE_CHILD(widget, child) {
            floatingpanel_render_normal_subtree(child, canvas, widget->x, widget->y);
            vgfx_set_clip(win, px, py, pw, ph);
            floatingpanel_render_overlay_subtree(child, canvas, widget->x, widget->y);
            vgfx_set_clip(win, px, py, pw, ph);
        }
        vgfx_clear_clip(win);
    }
}

static bool floatingpanel_child_render_boundary(const vg_widget_t *widget) {
    if (!widget)
        return false;
    if (widget->type == VG_WIDGET_SCROLLVIEW)
        return true;
    return widget->type == VG_WIDGET_CUSTOM && widget->vtable && widget->vtable->paint_overlay;
}

static void floatingpanel_render_normal_subtree(vg_widget_t *widget,
                                                void *canvas,
                                                float parent_abs_x,
                                                float parent_abs_y) {
    if (!widget || !widget->visible)
        return;

    float abs_x = parent_abs_x + widget->x;
    float abs_y = parent_abs_y + widget->y;
    float rel_x = widget->x;
    float rel_y = widget->y;
    widget->x = abs_x;
    widget->y = abs_y;

    if (widget->vtable && widget->vtable->paint)
        widget->vtable->paint(widget, canvas);

    widget->x = rel_x;
    widget->y = rel_y;

    if (floatingpanel_child_render_boundary(widget))
        return;

    VG_FOREACH_CHILD(widget, child) {
        floatingpanel_render_normal_subtree(child, canvas, abs_x, abs_y);
    }
}

static void floatingpanel_render_overlay_subtree(vg_widget_t *widget,
                                                 void *canvas,
                                                 float parent_abs_x,
                                                 float parent_abs_y) {
    if (!widget || !widget->visible)
        return;

    float abs_x = parent_abs_x + widget->x;
    float abs_y = parent_abs_y + widget->y;
    float rel_x = widget->x;
    float rel_y = widget->y;
    widget->x = abs_x;
    widget->y = abs_y;

    if (widget->vtable && widget->vtable->paint_overlay)
        widget->vtable->paint_overlay(widget, canvas);

    widget->x = rel_x;
    widget->y = rel_y;

    if (floatingpanel_child_render_boundary(widget))
        return;

    VG_FOREACH_CHILD(widget, child) {
        floatingpanel_render_overlay_subtree(child, canvas, abs_x, abs_y);
    }
}

/// @brief Floatingpanel set position.
void vg_floatingpanel_set_position(vg_floatingpanel_t *panel, float x, float y) {
    if (!panel)
        return;
    panel->abs_x = x;
    panel->abs_y = y;
    panel->base.needs_layout = true;
    panel->base.needs_paint = true;
}

/// @brief Floatingpanel set size.
void vg_floatingpanel_set_size(vg_floatingpanel_t *panel, float w, float h) {
    if (!panel)
        return;
    panel->abs_w = w;
    panel->abs_h = h;
    panel->base.needs_layout = true;
    panel->base.needs_paint = true;
}

/// @brief Floatingpanel set visible.
void vg_floatingpanel_set_visible(vg_floatingpanel_t *panel, int visible) {
    if (!panel)
        return;
    panel->base.visible = (visible != 0);
    panel->base.needs_layout = true;
    panel->base.needs_paint = true;
}

/// @brief Floatingpanel add child.
void vg_floatingpanel_add_child(vg_floatingpanel_t *panel, vg_widget_t *child) {
    if (!panel || !child)
        return;
    vg_widget_add_child(&panel->base, child);
    panel->base.needs_layout = true;
    panel->base.needs_paint = true;
}
