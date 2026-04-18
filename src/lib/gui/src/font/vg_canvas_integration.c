//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
// Purpose: Font-to-canvas integration — renders rasterized glyphs onto ViperGFX canvas surfaces.
//
//===----------------------------------------------------------------------===//
//
// File: src/lib/gui/src/font/vg_canvas_integration.c
//
//===----------------------------------------------------------------------===//
// vg_canvas_integration.c - Integration with vgfx canvas
#include "../../include/vg_font.h"
#include "../../../graphics/src/vgfx_internal.h"
#include "vgfx.h"
#include <stddef.h>
#include <stdint.h>

//=============================================================================
// Canvas Glyph Drawing
//
// This function draws a glyph bitmap to the canvas using alpha blending.
// The glyph bitmap contains 8-bit alpha values where 255 = fully opaque.
// The canvas parameter is a vgfx_window_t handle.
//=============================================================================

void vg_canvas_draw_glyph(
    void *canvas, int x, int y, const uint8_t *bitmap, int width, int height, uint32_t color) {
    if (!canvas || !bitmap || width <= 0 || height <= 0)
        return;

    vgfx_window_t window = (vgfx_window_t)canvas;

    // Extract color components
    uint8_t r = (color >> 16) & 0xFF;
    uint8_t g = (color >> 8) & 0xFF;
    uint8_t b = color & 0xFF;

    // Get framebuffer for direct pixel access with alpha blending
    vgfx_framebuffer_t fb;
    if (!vgfx_get_framebuffer(window, &fb)) {
        return; // vgfx_get_framebuffer returns 1 on success, 0 on failure
    }

    int canvas_w = fb.width;
    int canvas_h = fb.height;
    const struct vgfx_window *internal = (const struct vgfx_window *)window;

    int clip_x = 0;
    int clip_y = 0;
    int clip_w = canvas_w;
    int clip_h = canvas_h;
    if (internal && internal->clip_enabled) {
        clip_x = internal->clip_x;
        clip_y = internal->clip_y;
        clip_w = internal->clip_w;
        clip_h = internal->clip_h;
    }

    int start_x = x;
    int start_y = y;
    int end_x = x + width;
    int end_y = y + height;

    if (start_x < clip_x)
        start_x = clip_x;
    if (start_y < clip_y)
        start_y = clip_y;
    if (end_x > clip_x + clip_w)
        end_x = clip_x + clip_w;
    if (end_y > clip_y + clip_h)
        end_y = clip_y + clip_h;
    if (start_x < 0)
        start_x = 0;
    if (start_y < 0)
        start_y = 0;
    if (end_x > canvas_w)
        end_x = canvas_w;
    if (end_y > canvas_h)
        end_y = canvas_h;
    if (start_x >= end_x || start_y >= end_y)
        return;

    // Draw each pixel with alpha blending
    // Use byte-level access matching vgfx's RGBA format
    for (int screen_y = start_y; screen_y < end_y; screen_y++) {
        int py = screen_y - y;
        for (int screen_x = start_x; screen_x < end_x; screen_x++) {
            int px = screen_x - x;
            uint8_t alpha = bitmap[py * width + px];
            if (alpha == 0)
                continue;

            // Calculate pixel address (RGBA format, 4 bytes per pixel)
            uint8_t *pixel = fb.pixels + (screen_y * fb.stride) + (screen_x * 4);

            if (alpha == 255) {
                // Fully opaque - just write the color (RGBA order)
                pixel[0] = r;
                pixel[1] = g;
                pixel[2] = b;
                pixel[3] = 0xFF;
            } else {
                // Alpha blend with background
                uint8_t bg_r = pixel[0];
                uint8_t bg_g = pixel[1];
                uint8_t bg_b = pixel[2];

                // Fast alpha blending
                uint32_t inv_alpha = 255 - alpha;
                pixel[0] = (uint8_t)((r * alpha + bg_r * inv_alpha + 127) / 255);
                pixel[1] = (uint8_t)((g * alpha + bg_g * inv_alpha + 127) / 255);
                pixel[2] = (uint8_t)((b * alpha + bg_b * inv_alpha + 127) / 255);
                pixel[3] = 0xFF;
            }
        }
    }
}

//=============================================================================
// Alternative: Draw using vgfx_pset (slower but doesn't need framebuffer access)
//=============================================================================

void vg_canvas_draw_glyph_pset(
    void *canvas, int x, int y, const uint8_t *bitmap, int width, int height, uint32_t color) {
    if (!canvas || !bitmap || width <= 0 || height <= 0)
        return;

    vgfx_window_t window = (vgfx_window_t)canvas;

    uint8_t r = (color >> 16) & 0xFF;
    uint8_t g = (color >> 8) & 0xFF;
    uint8_t b = color & 0xFF;

    for (int py = 0; py < height; py++) {
        for (int px = 0; px < width; px++) {
            uint8_t alpha = bitmap[py * width + px];
            if (alpha == 0)
                continue;

            int screen_x = x + px;
            int screen_y = y + py;

            if (alpha >= 128) {
                // Simple threshold for non-framebuffer version
                vgfx_pset(window, screen_x, screen_y, vgfx_rgb(r, g, b));
            }
        }
    }
}
