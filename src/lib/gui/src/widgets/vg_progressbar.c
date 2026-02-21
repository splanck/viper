// vg_progressbar.c - ProgressBar widget implementation
#include "../../include/vg_widgets.h"
#include "../../../graphics/include/vgfx.h"
#include <stdio.h>
#include <stdlib.h>

//=============================================================================
// Forward declarations
//=============================================================================

static void progressbar_measure(vg_widget_t *widget, float avail_w, float avail_h);
static void progressbar_arrange(vg_widget_t *widget, float x, float y, float w, float h);
static void progressbar_paint(vg_widget_t *widget, void *canvas);

//=============================================================================
// VTable
//=============================================================================

static vg_widget_vtable_t g_progressbar_vtable = {
    .destroy = NULL,
    .measure = progressbar_measure,
    .arrange = progressbar_arrange,
    .paint = progressbar_paint,
    .handle_event = NULL,
    .can_focus = NULL,
    .on_focus = NULL,
};

//=============================================================================
// VTable Implementations
//=============================================================================

static void progressbar_measure(vg_widget_t *widget, float avail_w, float avail_h)
{
    (void)avail_w;
    (void)avail_h;
    widget->measured_width = 100.0f;
    widget->measured_height = 8.0f;
}

static void progressbar_arrange(vg_widget_t *widget, float x, float y, float w, float h)
{
    widget->x = x;
    widget->y = y;
    widget->width = w;
    widget->height = h;
}

static void progressbar_paint(vg_widget_t *widget, void *canvas)
{
    vg_progressbar_t *pb = (vg_progressbar_t *)widget;
    vgfx_window_t win = (vgfx_window_t)canvas;
    int32_t x = (int32_t)widget->x, y = (int32_t)widget->y;
    int32_t w = (int32_t)widget->width, h = (int32_t)widget->height;

    /* Track background */
    vgfx_fill_rect(win, x, y, w, h, pb->track_color);

    if (pb->style == VG_PROGRESS_BAR)
    {
        /* Determinate fill bar */
        float clamped = pb->value < 0.0f ? 0.0f : (pb->value > 1.0f ? 1.0f : pb->value);
        int32_t fill_w = (int32_t)(clamped * (float)w);
        if (fill_w > 0)
            vgfx_fill_rect(win, x, y, fill_w, h, pb->fill_color);
    }
    else if (pb->style == VG_PROGRESS_INDETERMINATE)
    {
        /* Indeterminate: sliding block using animation_phase [0,1) */
        float phase = pb->animation_phase - (int)pb->animation_phase; /* fractional part */
        int32_t block_w = w / 4;
        int32_t block_x = x + (int32_t)(phase * (float)(w + block_w)) - block_w;
        /* Clamp to widget bounds */
        if (block_x < x)
            block_x = x;
        int32_t block_end = block_x + block_w;
        if (block_end > x + w)
            block_end = x + w;
        if (block_end > block_x)
            vgfx_fill_rect(win, block_x, y, block_end - block_x, h, pb->fill_color);
    }

    /* Optional percentage text */
    if (pb->show_percentage && pb->font && pb->style == VG_PROGRESS_BAR)
    {
        char buf[8];
        int pct = (int)(pb->value * 100.0f + 0.5f);
        snprintf(buf, sizeof(buf), "%d%%", pct);
        float cx = widget->x + widget->width / 2.0f;
        float cy = widget->y + widget->height / 2.0f + pb->font_size * 0.35f;
        vg_font_draw_text(canvas, pb->font, pb->font_size, cx, cy, buf, 0x00FFFFFF);
    }
}

vg_progressbar_t *vg_progressbar_create(vg_widget_t *parent)
{
    vg_progressbar_t *progress = calloc(1, sizeof(vg_progressbar_t));
    if (!progress)
        return NULL;

    vg_widget_init(&progress->base, VG_WIDGET_PROGRESS, &g_progressbar_vtable);

    // Default values
    progress->value = 0;
    progress->style = VG_PROGRESS_BAR;

    // Default appearance
    progress->track_color = 0xFF3C3C3C;
    progress->fill_color = 0xFF0078D4;
    progress->corner_radius = 4;
    progress->font_size = 12;

    if (parent)
    {
        vg_widget_add_child(parent, &progress->base);
    }

    return progress;
}

void vg_progressbar_set_value(vg_progressbar_t *progress, float value)
{
    if (!progress)
        return;
    if (value < 0)
        value = 0;
    if (value > 1)
        value = 1;
    progress->value = value;
}

float vg_progressbar_get_value(vg_progressbar_t *progress)
{
    return progress ? progress->value : 0;
}

void vg_progressbar_set_style(vg_progressbar_t *progress, vg_progress_style_t style)
{
    if (!progress)
        return;
    progress->style = style;
}

void vg_progressbar_show_percentage(vg_progressbar_t *progress, bool show)
{
    if (!progress)
        return;
    progress->show_percentage = show;
}
