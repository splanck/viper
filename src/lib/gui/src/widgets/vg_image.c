//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/lib/gui/src/widgets/vg_image.c
//
//===----------------------------------------------------------------------===//
// vg_image.c - Image widget implementation
#include "../../include/vg_widget.h"
#include "../../include/vg_widgets.h"
#include "vgfx.h"
#include <stdlib.h>
#include <string.h>

/*==========================================================================
 * Image paint function — blits pixel data to the vgfx framebuffer
 *=========================================================================*/

static void image_paint(vg_widget_t *widget, void *canvas) {
    vg_image_t *image = (vg_image_t *)widget;
    if (!image->pixels || image->img_width <= 0 || image->img_height <= 0)
        return;

    vgfx_framebuffer_t fb;
    if (!vgfx_get_framebuffer((vgfx_window_t)canvas, &fb))
        return;

    int dx = (int)widget->x;
    int dy = (int)widget->y;
    int dw = (int)widget->width;
    int dh = (int)widget->height;
    int sw = image->img_width;
    int sh = image->img_height;

    /* Simple nearest-neighbor blit (scale to fit widget bounds) */
    if (dw <= 0 || dh <= 0)
        return;

    for (int row = 0; row < dh; row++) {
        int sy = row * sh / dh;
        if (sy >= sh)
            sy = sh - 1;
        int fb_y = dy + row;
        if (fb_y < 0 || fb_y >= fb.height)
            continue;

        for (int col = 0; col < dw; col++) {
            int sx = col * sw / dw;
            if (sx >= sw)
                sx = sw - 1;
            int fb_x = dx + col;
            if (fb_x < 0 || fb_x >= fb.width)
                continue;

            /* Source is RGBA bytes (R,G,B,A) */
            const uint8_t *src = &image->pixels[(sy * sw + sx) * 4];
            uint8_t r = src[0], g = src[1], b = src[2];

            /* Write to framebuffer (RGBA byte order) */
            int fb_idx = fb_y * fb.stride + fb_x * 4;
            fb.pixels[fb_idx + 0] = r;
            fb.pixels[fb_idx + 1] = g;
            fb.pixels[fb_idx + 2] = b;
            fb.pixels[fb_idx + 3] = 0xFF;
        }
    }
}

static vg_widget_vtable_t g_image_vtable = {
    .destroy = NULL,
    .measure = NULL,
    .arrange = NULL,
    .paint = image_paint,
    .handle_event = NULL,
};

vg_image_t *vg_image_create(vg_widget_t *parent) {
    vg_image_t *image = calloc(1, sizeof(vg_image_t));
    if (!image)
        return NULL;

    image->base.type = VG_WIDGET_IMAGE;
    image->base.visible = true;
    image->base.enabled = true;
    image->base.vtable = &g_image_vtable;

    // Default values
    image->scale_mode = VG_IMAGE_SCALE_FIT;
    image->opacity = 1.0f;
    image->bg_color = 0x00000000; // Transparent

    if (parent) {
        vg_widget_add_child(parent, &image->base);
    }

    return image;
}

/// @brief Image set pixels.
void vg_image_set_pixels(vg_image_t *image, const uint8_t *pixels, int width, int height) {
    if (!image)
        return;

    // Free existing
    free(image->pixels);
    image->pixels = NULL;
    image->img_width = 0;
    image->img_height = 0;

    if (!pixels || width <= 0 || height <= 0)
        return;

    // Copy pixel data (RGBA format)
    size_t size = (size_t)width * (size_t)height * 4;
    image->pixels = malloc(size);
    if (!image->pixels)
        return;

    memcpy(image->pixels, pixels, size);
    image->img_width = width;
    image->img_height = height;
}

bool vg_image_load_file(vg_image_t *image, const char *path) {
    if (!image || !path)
        return false;

    // Image file loading requires a decode library (e.g. stb_image).
    // Use vg_image_set_pixels() to supply pre-decoded RGBA pixel data directly.
    (void)path;
    return false;
}

/// @brief Image clear.
void vg_image_clear(vg_image_t *image) {
    if (!image)
        return;
    free(image->pixels);
    image->pixels = NULL;
    image->img_width = 0;
    image->img_height = 0;
}

/// @brief Image set scale mode.
void vg_image_set_scale_mode(vg_image_t *image, vg_image_scale_t mode) {
    if (!image)
        return;
    image->scale_mode = mode;
}

/// @brief Image set opacity.
void vg_image_set_opacity(vg_image_t *image, float opacity) {
    if (!image)
        return;
    if (opacity < 0)
        opacity = 0;
    if (opacity > 1)
        opacity = 1;
    image->opacity = opacity;
}
