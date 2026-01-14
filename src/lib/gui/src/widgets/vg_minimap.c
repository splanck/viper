// vg_minimap.c - Minimap widget implementation
#include "../../include/vg_ide_widgets.h"
#include "../../include/vg_theme.h"
#include "../../include/vg_event.h"
#include <stdlib.h>
#include <string.h>

//=============================================================================
// Forward Declarations
//=============================================================================

static void minimap_destroy(vg_widget_t* widget);
static void minimap_measure(vg_widget_t* widget, float available_width, float available_height);
static void minimap_paint(vg_widget_t* widget, void* canvas);
static bool minimap_handle_event(vg_widget_t* widget, vg_event_t* event);

//=============================================================================
// Minimap VTable
//=============================================================================

static vg_widget_vtable_t g_minimap_vtable = {
    .destroy = minimap_destroy,
    .measure = minimap_measure,
    .arrange = NULL,
    .paint = minimap_paint,
    .handle_event = minimap_handle_event,
    .can_focus = NULL,
    .on_focus = NULL
};

//=============================================================================
// Minimap Implementation
//=============================================================================

vg_minimap_t* vg_minimap_create(vg_codeeditor_t* editor) {
    vg_minimap_t* minimap = calloc(1, sizeof(vg_minimap_t));
    if (!minimap) return NULL;

    vg_widget_init(&minimap->base, VG_WIDGET_CUSTOM, &g_minimap_vtable);

    // Defaults
    minimap->editor = editor;
    minimap->char_width = 1;
    minimap->line_height = 2;
    minimap->show_viewport = true;
    minimap->scale = 0.1f;

    minimap->viewport_color = 0x40FFFFFF;  // Semi-transparent white
    minimap->bg_color = 0xFF1E1E1E;
    minimap->text_color = 0xFF808080;

    minimap->buffer_dirty = true;

    return minimap;
}

static void minimap_destroy(vg_widget_t* widget) {
    vg_minimap_t* minimap = (vg_minimap_t*)widget;

    free(minimap->render_buffer);
}

void vg_minimap_destroy(vg_minimap_t* minimap) {
    if (!minimap) return;
    vg_widget_destroy(&minimap->base);
}

static void minimap_measure(vg_widget_t* widget, float available_width, float available_height) {
    (void)available_width;
    (void)available_height;

    // Minimap typically has a fixed width
    widget->measured_width = 80;  // Default minimap width
    widget->measured_height = available_height;
}

static void render_minimap_buffer(vg_minimap_t* minimap) {
    if (!minimap->editor) return;

    // Calculate buffer dimensions
    uint32_t width = (uint32_t)minimap->base.width;
    uint32_t height = (uint32_t)minimap->base.height;

    if (width == 0 || height == 0) return;

    // Reallocate buffer if needed
    if (width != minimap->buffer_width || height != minimap->buffer_height) {
        free(minimap->render_buffer);
        minimap->render_buffer = calloc(width * height, 4);  // RGBA
        minimap->buffer_width = width;
        minimap->buffer_height = height;
    }

    if (!minimap->render_buffer) return;

    // Clear buffer with background color
    uint8_t bg_r = (minimap->bg_color >> 16) & 0xFF;
    uint8_t bg_g = (minimap->bg_color >> 8) & 0xFF;
    uint8_t bg_b = minimap->bg_color & 0xFF;

    for (uint32_t i = 0; i < width * height; i++) {
        minimap->render_buffer[i * 4 + 0] = bg_r;
        minimap->render_buffer[i * 4 + 1] = bg_g;
        minimap->render_buffer[i * 4 + 2] = bg_b;
        minimap->render_buffer[i * 4 + 3] = 255;
    }

    // Render lines
    vg_codeeditor_t* ed = minimap->editor;
    uint8_t text_r = (minimap->text_color >> 16) & 0xFF;
    uint8_t text_g = (minimap->text_color >> 8) & 0xFF;
    uint8_t text_b = minimap->text_color & 0xFF;

    for (int line = 0; line < ed->line_count; line++) {
        uint32_t y = (uint32_t)(line * minimap->line_height);
        if (y >= height) break;

        const char* text = ed->lines[line].text;
        if (!text) continue;

        uint32_t x = 0;
        for (const char* p = text; *p && x < width; p++) {
            if (*p != ' ' && *p != '\t') {
                // Draw a pixel for this character
                uint32_t idx = (y * width + x) * 4;
                minimap->render_buffer[idx + 0] = text_r;
                minimap->render_buffer[idx + 1] = text_g;
                minimap->render_buffer[idx + 2] = text_b;
                minimap->render_buffer[idx + 3] = 255;
            }
            x += minimap->char_width;
        }
    }

    minimap->buffer_dirty = false;
}

static void minimap_paint(vg_widget_t* widget, void* canvas) {
    vg_minimap_t* minimap = (vg_minimap_t*)widget;
    (void)canvas;

    if (!minimap->editor) return;

    // Render buffer if dirty
    if (minimap->buffer_dirty) {
        render_minimap_buffer(minimap);
    }

    // Draw buffer (placeholder - actual texture upload to vgfx)
    // In a real implementation, this would upload the render_buffer as a texture

    // Draw viewport indicator
    if (minimap->show_viewport && minimap->editor) {
        vg_codeeditor_t* ed = minimap->editor;

        // Calculate viewport rectangle
        float start_y = widget->y + ed->visible_first_line * minimap->line_height;
        float end_y = start_y + ed->visible_line_count * minimap->line_height;

        // Draw viewport highlight (placeholder)
        (void)minimap->viewport_color;
        (void)start_y;
        (void)end_y;
    }
}

static bool minimap_handle_event(vg_widget_t* widget, vg_event_t* event) {
    vg_minimap_t* minimap = (vg_minimap_t*)widget;

    if (!minimap->editor) return false;

    switch (event->type) {
        case VG_EVENT_MOUSE_DOWN: {
            // Start dragging viewport
            minimap->dragging = true;
            minimap->drag_start_y = (int)event->mouse.y;

            // Jump to clicked line
            int clicked_line = (int)(event->mouse.y / minimap->line_height);
            if (clicked_line >= 0 && clicked_line < minimap->editor->line_count) {
                vg_codeeditor_scroll_to_line(minimap->editor, clicked_line);
            }
            return true;
        }

        case VG_EVENT_MOUSE_UP:
            minimap->dragging = false;
            return true;

        case VG_EVENT_MOUSE_MOVE:
            if (minimap->dragging) {
                // Drag to scroll
                int delta_y = (int)event->mouse.y - minimap->drag_start_y;
                int delta_lines = delta_y / (int)minimap->line_height;
                minimap->drag_start_y = (int)event->mouse.y;

                if (delta_lines != 0) {
                    int new_line = minimap->editor->visible_first_line + delta_lines;
                    if (new_line < 0) new_line = 0;
                    if (new_line >= minimap->editor->line_count) {
                        new_line = minimap->editor->line_count - 1;
                    }
                    vg_codeeditor_scroll_to_line(minimap->editor, new_line);
                }
                return true;
            }
            break;

        default:
            break;
    }

    return false;
}

void vg_minimap_set_editor(vg_minimap_t* minimap, vg_codeeditor_t* editor) {
    if (!minimap) return;

    minimap->editor = editor;
    minimap->buffer_dirty = true;
    minimap->base.needs_paint = true;
}

void vg_minimap_set_scale(vg_minimap_t* minimap, float scale) {
    if (!minimap) return;

    if (scale < 0.05f) scale = 0.05f;
    if (scale > 0.5f) scale = 0.5f;

    minimap->scale = scale;
    minimap->buffer_dirty = true;
    minimap->base.needs_paint = true;
}

void vg_minimap_set_show_viewport(vg_minimap_t* minimap, bool show) {
    if (!minimap) return;

    minimap->show_viewport = show;
    minimap->base.needs_paint = true;
}

void vg_minimap_invalidate(vg_minimap_t* minimap) {
    if (!minimap) return;

    minimap->buffer_dirty = true;
    minimap->base.needs_paint = true;
}

void vg_minimap_invalidate_lines(vg_minimap_t* minimap,
    uint32_t start_line, uint32_t end_line) {

    if (!minimap) return;
    (void)start_line;
    (void)end_line;

    // For simplicity, just mark the whole buffer dirty
    // A more optimized implementation could update only affected pixels
    minimap->buffer_dirty = true;
    minimap->base.needs_paint = true;
}
