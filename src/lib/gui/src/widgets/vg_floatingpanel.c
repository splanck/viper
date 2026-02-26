// vg_floatingpanel.c - Floating overlay panel widget
//
// A lightweight overlay that draws at an absolute screen position regardless
// of the normal layout hierarchy.  Children added via vg_floatingpanel_add_child()
// are kept in a private array (not the widget tree) and are painted during the
// paint_overlay pass so they always appear above all other content.
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
static void floatingpanel_paint(vg_widget_t *widget, void *canvas);
static void floatingpanel_paint_overlay(vg_widget_t *widget, void *canvas);

//=============================================================================
// VTable
//=============================================================================

static vg_widget_vtable_t g_floatingpanel_vtable = {
    .destroy = floatingpanel_destroy,
    .measure = floatingpanel_measure,
    .arrange = NULL,
    .paint = floatingpanel_paint,
    .paint_overlay = floatingpanel_paint_overlay,
    .handle_event = NULL,
    .can_focus = NULL,
    .on_focus = NULL,
};

//=============================================================================
// Implementation
//=============================================================================

vg_floatingpanel_t *vg_floatingpanel_create(vg_widget_t *root)
{
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

static void floatingpanel_destroy(vg_widget_t *widget)
{
    vg_floatingpanel_t *panel = (vg_floatingpanel_t *)widget;
    // Free private child array (does NOT destroy children — caller owns them)
    free(panel->children);
    panel->children = NULL;
    panel->child_count = 0;
    panel->child_cap = 0;
}

void vg_floatingpanel_destroy(vg_floatingpanel_t *panel)
{
    if (!panel)
        return;
    vg_widget_destroy(&panel->base);
    free(panel);
}

static void floatingpanel_measure(vg_widget_t *widget,
                                  float available_width,
                                  float available_height)
{
    // The panel takes zero space in the normal layout pass.
    (void)available_width;
    (void)available_height;
    widget->measured_width = 0.0f;
    widget->measured_height = 0.0f;
}

// Normal paint pass — do nothing; all drawing happens in paint_overlay.
static void floatingpanel_paint(vg_widget_t *widget, void *canvas)
{
    (void)widget;
    (void)canvas;
}

// Overlay pass — paint background, border, and private children.
static void floatingpanel_paint_overlay(vg_widget_t *widget, void *canvas)
{
    vg_floatingpanel_t *panel = (vg_floatingpanel_t *)widget;

    if (!panel->base.visible || panel->abs_w <= 0.0f || panel->abs_h <= 0.0f)
        return;

    vgfx_window_t win = (vgfx_window_t)canvas;

    int32_t px = (int32_t)panel->abs_x;
    int32_t py = (int32_t)panel->abs_y;
    int32_t pw = (int32_t)panel->abs_w;
    int32_t ph = (int32_t)panel->abs_h;

    // Background fill
    vgfx_fill_rect(win, px, py, pw, ph, panel->bg_color);

    // Border (1-px rectangles on each edge)
    if (panel->border_width > 0.0f)
    {
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

    // Lay out and paint each private child to fill the panel's rect.
    for (int i = 0; i < panel->child_count; i++)
    {
        vg_widget_t *child = panel->children[i];
        if (!child || !child->visible)
            continue;
        vg_widget_measure(child, panel->abs_w, panel->abs_h);
        vg_widget_arrange(child, panel->abs_x, panel->abs_y, panel->abs_w, panel->abs_h);
        vg_widget_paint(child, canvas);
    }
}

void vg_floatingpanel_set_position(vg_floatingpanel_t *panel, float x, float y)
{
    if (!panel)
        return;
    panel->abs_x = x;
    panel->abs_y = y;
    panel->base.needs_paint = true;
}

void vg_floatingpanel_set_size(vg_floatingpanel_t *panel, float w, float h)
{
    if (!panel)
        return;
    panel->abs_w = w;
    panel->abs_h = h;
    panel->base.needs_paint = true;
}

void vg_floatingpanel_set_visible(vg_floatingpanel_t *panel, int visible)
{
    if (!panel)
        return;
    panel->base.visible = (visible != 0);
    panel->base.needs_paint = true;
}

void vg_floatingpanel_add_child(vg_floatingpanel_t *panel, vg_widget_t *child)
{
    if (!panel || !child)
        return;

    // Grow private array if needed (initial cap = 4, double on overflow)
    if (panel->child_count >= panel->child_cap)
    {
        int new_cap = panel->child_cap > 0 ? panel->child_cap * 2 : 4;
        vg_widget_t **new_arr = realloc(panel->children, (size_t)new_cap * sizeof(vg_widget_t *));
        if (!new_arr)
            return;
        panel->children = new_arr;
        panel->child_cap = new_cap;
    }

    panel->children[panel->child_count++] = child;
    panel->base.needs_paint = true;
}
