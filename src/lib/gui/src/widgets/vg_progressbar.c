//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/lib/gui/src/widgets/vg_progressbar.c
//
//===----------------------------------------------------------------------===//
// vg_progressbar.c - ProgressBar widget implementation
#include "../../../graphics/include/vgfx.h"
#include "../../include/vg_theme.h"
#include "../../include/vg_widgets.h"
#include <stdio.h>
#include <stdlib.h>

//=============================================================================
// Forward declarations
//=============================================================================

static void progressbar_measure(vg_widget_t *widget, float avail_w, float avail_h);
static void progressbar_arrange(vg_widget_t *widget, float x, float y, float w, float h);
static void progressbar_paint(vg_widget_t *widget, void *canvas);
static void progressbar_fill_round_rect(vgfx_window_t win,
                                        int32_t x,
                                        int32_t y,
                                        int32_t w,
                                        int32_t h,
                                        int32_t radius,
                                        uint32_t color);

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

static void progressbar_measure(vg_widget_t *widget, float avail_w, float avail_h) {
    vg_progressbar_t *pb = (vg_progressbar_t *)widget;
    vg_theme_t *theme = vg_theme_get_current();
    (void)avail_w;
    (void)avail_h;
    widget->measured_width = 100.0f;
    widget->measured_height = 8.0f;
    if (pb->show_percentage && pb->font) {
        vg_font_metrics_t metrics = {0};
        vg_font_get_metrics(pb->font, pb->font_size, &metrics);
        float text_height = (float)metrics.line_height + 6.0f;
        if (text_height > widget->measured_height)
            widget->measured_height = text_height;
    } else if (theme && theme->input.height * 0.32f > widget->measured_height) {
        widget->measured_height = theme->input.height * 0.32f;
    }
}

static void progressbar_arrange(vg_widget_t *widget, float x, float y, float w, float h) {
    widget->x = x;
    widget->y = y;
    widget->width = w;
    widget->height = h;
}

static void progressbar_fill_round_rect(vgfx_window_t win,
                                        int32_t x,
                                        int32_t y,
                                        int32_t w,
                                        int32_t h,
                                        int32_t radius,
                                        uint32_t color) {
    if (w <= 0 || h <= 0)
        return;
    if (radius <= 0 || w <= radius * 2 || h <= radius * 2) {
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

static void progressbar_paint(vg_widget_t *widget, void *canvas) {
    vg_progressbar_t *pb = (vg_progressbar_t *)widget;
    vgfx_window_t win = (vgfx_window_t)canvas;
    vg_theme_t *theme = vg_theme_get_current();
    int32_t x = (int32_t)widget->x, y = (int32_t)widget->y;
    int32_t w = (int32_t)widget->width, h = (int32_t)widget->height;
    uint32_t track_color = pb->track_color ? pb->track_color
                                           : vg_color_blend(theme->colors.bg_secondary, theme->colors.bg_primary, 0.45f);
    uint32_t fill_color = pb->fill_color ? pb->fill_color : theme->colors.accent_primary;
    int32_t radius = (int32_t)pb->corner_radius;
    if (radius < 2)
        radius = 2;

    progressbar_fill_round_rect(win, x, y, w, h, radius, track_color);
    vgfx_rect(win, x, y, w, h, theme->colors.border_secondary);
    if (w > 2 && h > 2) {
        vgfx_fill_rect(win, x + 1, y + 1, w - 2, 1, vg_color_lighten(track_color, 0.08f));
    }

    if (pb->style == VG_PROGRESS_BAR) {
        float clamped = pb->value < 0.0f ? 0.0f : (pb->value > 1.0f ? 1.0f : pb->value);
        int32_t fill_w = (int32_t)(clamped * (float)w);
        if (fill_w > 0)
            progressbar_fill_round_rect(win, x, y, fill_w, h, radius, fill_color);
    } else if (pb->style == VG_PROGRESS_INDETERMINATE) {
        float phase = pb->animation_phase - (int)pb->animation_phase;
        int32_t block_w = w / 4;
        int32_t block_x = x + (int32_t)(phase * (float)(w + block_w)) - block_w;
        if (block_x < x)
            block_x = x;
        int32_t block_end = block_x + block_w;
        if (block_end > x + w)
            block_end = x + w;
        if (block_end > block_x)
            progressbar_fill_round_rect(win, block_x, y, block_end - block_x, h, radius, fill_color);
    }

    if (pb->show_percentage && pb->font && pb->style == VG_PROGRESS_BAR) {
        char buf[8];
        int pct = (int)(pb->value * 100.0f + 0.5f);
        snprintf(buf, sizeof(buf), "%d%%", pct);
        vg_text_metrics_t text_metrics = {0};
        vg_font_measure_text(pb->font, pb->font_size, buf, &text_metrics);
        vg_font_metrics_t font_metrics = {0};
        vg_font_get_metrics(pb->font, pb->font_size, &font_metrics);
        float text_x = widget->x + (widget->width - text_metrics.width) * 0.5f;
        float text_y =
            widget->y + (widget->height - (font_metrics.ascent - font_metrics.descent)) * 0.5f +
            font_metrics.ascent;
        uint32_t text_color = pct >= 50 ? 0x00FFFFFF : theme->colors.fg_primary;
        int32_t clip_w = w - 4;
        int32_t clip_h = h - 2;
        if (clip_w > 0 && clip_h > 0) {
            vgfx_set_clip(win, x + 2, y + 1, clip_w, clip_h);
            vg_font_draw_text(canvas, pb->font, pb->font_size, text_x, text_y, buf, text_color);
            vgfx_clear_clip(win);
        }
    }
}

vg_progressbar_t *vg_progressbar_create(vg_widget_t *parent) {
    vg_progressbar_t *progress = calloc(1, sizeof(vg_progressbar_t));
    if (!progress)
        return NULL;

    vg_widget_init(&progress->base, VG_WIDGET_PROGRESS, &g_progressbar_vtable);

    // Default values
    progress->value = 0;
    progress->style = VG_PROGRESS_BAR;

    // Default appearance
    vg_theme_t *theme = vg_theme_get_current();
    progress->track_color = vg_color_blend(theme->colors.bg_secondary, theme->colors.bg_primary, 0.45f);
    progress->fill_color = theme->colors.accent_primary;
    progress->corner_radius = 4;
    progress->font = theme->typography.font_regular;
    progress->font_size = theme->typography.size_small;

    if (parent) {
        vg_widget_add_child(parent, &progress->base);
    }

    return progress;
}

/// @brief Progressbar set value.
void vg_progressbar_set_value(vg_progressbar_t *progress, float value) {
    if (!progress)
        return;
    if (value < 0)
        value = 0;
    if (value > 1)
        value = 1;
    progress->value = value;
    progress->base.needs_paint = true;
}

/// @brief Progressbar get value.
float vg_progressbar_get_value(vg_progressbar_t *progress) {
    return progress ? progress->value : 0;
}

/// @brief Progressbar set style.
void vg_progressbar_set_style(vg_progressbar_t *progress, vg_progress_style_t style) {
    if (!progress)
        return;
    progress->style = style;
    progress->base.needs_paint = true;
}

/// @brief Progressbar show percentage.
void vg_progressbar_show_percentage(vg_progressbar_t *progress, bool show) {
    if (!progress)
        return;
    progress->show_percentage = show;
    progress->base.needs_layout = true;
    progress->base.needs_paint = true;
}

/// @brief Progressbar tick.
void vg_progressbar_tick(vg_progressbar_t *progress, float dt) {
    if (!progress)
        return;
    if (progress->style == VG_PROGRESS_INDETERMINATE) {
        // Advance animation phase at a moderate speed; wrap at 1.0
        progress->animation_phase += dt * 0.7f;
        if (progress->animation_phase >= 1.0f)
            progress->animation_phase -= 1.0f;
        progress->base.needs_paint = true;
    }
}
