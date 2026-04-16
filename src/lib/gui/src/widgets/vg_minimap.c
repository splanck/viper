//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/lib/gui/src/widgets/vg_minimap.c
//
//===----------------------------------------------------------------------===//
// vg_minimap.c - Minimap widget implementation
#include "../../../graphics/include/vgfx.h"
#include "../../include/vg_event.h"
#include "../../include/vg_ide_widgets.h"
#include "../../include/vg_theme.h"
#include <stdlib.h>
#include <string.h>

//=============================================================================
// Forward Declarations
//=============================================================================

static void minimap_destroy(vg_widget_t *widget);
static void minimap_measure(vg_widget_t *widget, float available_width, float available_height);
static void minimap_paint(vg_widget_t *widget, void *canvas);
static bool minimap_handle_event(vg_widget_t *widget, vg_event_t *event);
static int minimap_document_line_count(const vg_minimap_t *minimap);
static int minimap_line_from_local_y(vg_minimap_t *minimap, float local_y);
static void minimap_scroll_editor_to_line(vg_minimap_t *minimap, int line);
static int minimap_trimmed_line_bounds(const char *text, int *out_first, int *out_last);

//=============================================================================
// Minimap VTable
//=============================================================================

static vg_widget_vtable_t g_minimap_vtable = {.destroy = minimap_destroy,
                                              .measure = minimap_measure,
                                              .arrange = NULL,
                                              .paint = minimap_paint,
                                              .handle_event = minimap_handle_event,
                                              .can_focus = NULL,
                                              .on_focus = NULL};

//=============================================================================
// Minimap Implementation
//=============================================================================

vg_minimap_t *vg_minimap_create(vg_codeeditor_t *editor) {
    vg_minimap_t *minimap = calloc(1, sizeof(vg_minimap_t));
    if (!minimap)
        return NULL;

    vg_widget_init(&minimap->base, VG_WIDGET_CUSTOM, &g_minimap_vtable);

    minimap->editor = editor;
    minimap->char_width = 1;
    minimap->line_height = 2;
    minimap->show_viewport = true;
    minimap->scale = 0.1f;
    minimap->viewport_color = 0xFF78D6FF;
    minimap->bg_color = 0xFF111827;
    minimap->text_color = 0xFF3B82F6;
    minimap->buffer_dirty = true;

    return minimap;
}

static void minimap_destroy(vg_widget_t *widget) {
    vg_minimap_t *minimap = (vg_minimap_t *)widget;
    free(minimap->render_buffer);
    free(minimap->markers);
}

/// @brief Minimap destroy.
void vg_minimap_destroy(vg_minimap_t *minimap) {
    if (!minimap)
        return;
    vg_widget_destroy(&minimap->base);
}

static void minimap_measure(vg_widget_t *widget, float available_width, float available_height) {
    (void)available_width;

    float width = widget->constraints.preferred_width > 0.0f ? widget->constraints.preferred_width
                                                              : 96.0f;
    if (widget->constraints.min_width > 0.0f && width < widget->constraints.min_width)
        width = widget->constraints.min_width;
    if (widget->constraints.max_width > 0.0f && width > widget->constraints.max_width)
        width = widget->constraints.max_width;

    widget->measured_width = width;
    widget->measured_height = available_height > 0.0f ? available_height : 160.0f;
    if (widget->constraints.min_height > 0.0f &&
        widget->measured_height < widget->constraints.min_height) {
        widget->measured_height = widget->constraints.min_height;
    }
}

static void render_minimap_buffer(vg_minimap_t *minimap) {
    if (!minimap)
        return;
    minimap->buffer_dirty = false;
}

static int minimap_document_line_count(const vg_minimap_t *minimap) {
    if (!minimap || !minimap->editor || minimap->editor->line_count <= 0)
        return 1;
    return minimap->editor->line_count;
}

static int minimap_line_from_local_y(vg_minimap_t *minimap, float local_y) {
    if (!minimap || !minimap->editor)
        return 0;

    float inner_top = 6.0f;
    float inner_height = minimap->base.height - 12.0f;
    if (inner_height <= 1.0f)
        return 0;

    if (local_y < inner_top)
        local_y = inner_top;
    if (local_y > inner_top + inner_height)
        local_y = inner_top + inner_height;

    int line_count = minimap_document_line_count(minimap);
    float t = (local_y - inner_top) / inner_height;
    int line = (int)(t * (float)line_count);
    if (line < 0)
        line = 0;
    if (line >= line_count)
        line = line_count - 1;
    return line;
}

static void minimap_scroll_editor_to_line(vg_minimap_t *minimap, int line) {
    if (!minimap || !minimap->editor)
        return;

    vg_codeeditor_t *editor = minimap->editor;
    int line_count = editor->line_count > 0 ? editor->line_count : 1;
    int visible_lines = editor->visible_line_count > 0 ? editor->visible_line_count : 1;
    int target = line - visible_lines / 2;
    int max_first = line_count - visible_lines;

    if (target < 0)
        target = 0;
    if (max_first < 0)
        max_first = 0;
    if (target > max_first)
        target = max_first;

    vg_codeeditor_scroll_to_line(editor, target);
    minimap->base.needs_paint = true;
}

static int minimap_trimmed_line_bounds(const char *text, int *out_first, int *out_last) {
    int first = -1;
    int last = -1;

    if (!text) {
        if (out_first)
            *out_first = -1;
        if (out_last)
            *out_last = -1;
        return 0;
    }

    for (int i = 0; text[i]; i++) {
        if (text[i] != ' ' && text[i] != '\t') {
            if (first < 0)
                first = i;
            last = i;
        }
    }

    if (out_first)
        *out_first = first;
    if (out_last)
        *out_last = last;
    return first >= 0 && last >= first;
}

static void minimap_paint(vg_widget_t *widget, void *canvas) {
    vg_minimap_t *minimap = (vg_minimap_t *)widget;
    if (!minimap->editor)
        return;

    if (minimap->buffer_dirty)
        render_minimap_buffer(minimap);

    vgfx_window_t win = (vgfx_window_t)canvas;
    int32_t panel_x = (int32_t)widget->x;
    int32_t panel_y = (int32_t)widget->y;
    int32_t panel_w = (int32_t)widget->width;
    int32_t panel_h = (int32_t)widget->height;
    if (panel_w <= 0 || panel_h <= 0)
        return;

    vgfx_fill_rect(win, panel_x, panel_y, panel_w, panel_h, minimap->bg_color);
    vgfx_rect(win, panel_x, panel_y, panel_w, panel_h, 0xFF243244);

    int32_t inner_x = panel_x + 6;
    int32_t inner_y = panel_y + 6;
    int32_t inner_w = panel_w - 12;
    int32_t inner_h = panel_h - 12;
    if (inner_w <= 2 || inner_h <= 2)
        return;

    vgfx_fill_rect(win, inner_x, inner_y, inner_w, inner_h, 0xFF0B1220);

    vg_codeeditor_t *editor = minimap->editor;
    int line_count = minimap_document_line_count(minimap);
    float scale = minimap->scale;
    if (scale < 0.05f)
        scale = 0.05f;
    if (scale > 0.5f)
        scale = 0.5f;

    float visible_columns = 120.0f * (0.1f / scale);
    if (visible_columns < 24.0f)
        visible_columns = 24.0f;

    for (int line = 0; line < editor->line_count; line++) {
        int32_t y0 = inner_y + (line * inner_h) / line_count;
        int32_t y1 = inner_y + ((line + 1) * inner_h) / line_count;
        int32_t bar_h = y1 - y0;
        if (bar_h < 1)
            bar_h = 1;

        int first = -1;
        int last = -1;
        if (!minimap_trimmed_line_bounds(editor->lines[line].text, &first, &last))
            continue;

        float start_ratio = (float)first / visible_columns;
        float end_ratio = (float)(last + 1) / visible_columns;
        if (start_ratio > 1.0f)
            start_ratio = 1.0f;
        if (end_ratio > 1.0f)
            end_ratio = 1.0f;
        if (end_ratio < start_ratio)
            end_ratio = start_ratio;

        int32_t x0 = inner_x + 2 + (int32_t)(start_ratio * (float)(inner_w - 4));
        int32_t x1 = inner_x + 2 + (int32_t)(end_ratio * (float)(inner_w - 4));
        int32_t bar_w = x1 - x0;
        if (bar_w < 2)
            bar_w = 2;
        if (x0 + bar_w > inner_x + inner_w - 2)
            bar_w = (inner_x + inner_w - 2) - x0;
        if (bar_w > 0)
            vgfx_fill_rect(win, x0, y0, bar_w, bar_h, minimap->text_color);
    }

    for (int i = 0; i < minimap->marker_count; i++) {
        int line = minimap->markers[i].line;
        if (line < 0 || line >= line_count)
            continue;
        int32_t y = inner_y + (line * inner_h) / line_count;
        vgfx_fill_rect(win, inner_x, y, inner_w, 2, minimap->markers[i].color);
    }

    if (minimap->show_viewport) {
        int visible_first = editor->visible_first_line;
        int visible_count = editor->visible_line_count > 0 ? editor->visible_line_count : 1;
        int32_t viewport_y = inner_y + (visible_first * inner_h) / line_count;
        int32_t viewport_end = inner_y + ((visible_first + visible_count) * inner_h) / line_count;
        int32_t viewport_h = viewport_end - viewport_y;
        if (viewport_h < 6)
            viewport_h = 6;
        if (viewport_y + viewport_h > inner_y + inner_h)
            viewport_h = (inner_y + inner_h) - viewport_y;
        if (viewport_h > 0) {
            if (visible_count < line_count) {
                vgfx_fill_rect(win, inner_x, viewport_y, inner_w, viewport_h, 0xFF132235);
            }
            vgfx_rect(win, inner_x, viewport_y, inner_w, viewport_h, minimap->viewport_color);
        }
    }
}

static bool minimap_handle_event(vg_widget_t *widget, vg_event_t *event) {
    vg_minimap_t *minimap = (vg_minimap_t *)widget;
    if (!minimap->editor)
        return false;

    switch (event->type) {
        case VG_EVENT_MOUSE_DOWN:
            minimap->dragging = true;
            minimap->drag_start_y = (int)event->mouse.y;
            minimap_scroll_editor_to_line(minimap, minimap_line_from_local_y(minimap, event->mouse.y));
            return true;

        case VG_EVENT_MOUSE_UP:
            minimap->dragging = false;
            return true;

        case VG_EVENT_MOUSE_MOVE:
            if (minimap->dragging) {
                minimap->drag_start_y = (int)event->mouse.y;
                minimap_scroll_editor_to_line(
                    minimap, minimap_line_from_local_y(minimap, event->mouse.y));
                return true;
            }
            break;

        default:
            break;
    }

    return false;
}

/// @brief Minimap set editor.
void vg_minimap_set_editor(vg_minimap_t *minimap, vg_codeeditor_t *editor) {
    if (!minimap)
        return;

    minimap->editor = editor;
    minimap->buffer_dirty = true;
    minimap->base.needs_paint = true;
}

/// @brief Minimap set scale.
void vg_minimap_set_scale(vg_minimap_t *minimap, float scale) {
    if (!minimap)
        return;

    if (scale < 0.05f)
        scale = 0.05f;
    if (scale > 0.5f)
        scale = 0.5f;

    minimap->scale = scale;
    minimap->buffer_dirty = true;
    minimap->base.needs_paint = true;
}

/// @brief Minimap set show viewport.
void vg_minimap_set_show_viewport(vg_minimap_t *minimap, bool show) {
    if (!minimap)
        return;

    minimap->show_viewport = show;
    minimap->base.needs_paint = true;
}

/// @brief Minimap invalidate.
void vg_minimap_invalidate(vg_minimap_t *minimap) {
    if (!minimap)
        return;

    minimap->buffer_dirty = true;
    minimap->base.needs_paint = true;
}

/// @brief Minimap invalidate lines.
void vg_minimap_invalidate_lines(vg_minimap_t *minimap, uint32_t start_line, uint32_t end_line) {
    if (!minimap)
        return;
    (void)start_line;
    (void)end_line;

    // For simplicity, just mark the whole buffer dirty
    // A more optimized implementation could update only affected pixels
    minimap->buffer_dirty = true;
    minimap->base.needs_paint = true;
}
