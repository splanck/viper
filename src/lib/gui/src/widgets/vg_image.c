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
#include "../../../graphics/src/vgfx_internal.h"
#include "vgfx.h"
#include <stdlib.h>
#include <string.h>

static void image_destroy(vg_widget_t *widget);
static void image_measure(vg_widget_t *widget, float available_width, float available_height);
static void image_paint(vg_widget_t *widget, void *canvas);

static vg_widget_vtable_t g_image_vtable = {
    .destroy = image_destroy,
    .measure = image_measure,
    .arrange = NULL,
    .paint = image_paint,
    .handle_event = NULL,
};

static int clampi(int value, int min_value, int max_value) {
    if (value < min_value)
        return min_value;
    if (value > max_value)
        return max_value;
    return value;
}

static void image_blend_pixel(uint8_t *dst, const uint8_t *src, uint8_t alpha) {
    if (alpha == 0)
        return;

    if (alpha == 255) {
        dst[0] = src[0];
        dst[1] = src[1];
        dst[2] = src[2];
        dst[3] = 255;
        return;
    }

    const uint32_t inv = (uint32_t)(255 - alpha);
    dst[0] = (uint8_t)(((uint32_t)src[0] * alpha + (uint32_t)dst[0] * inv + 127u) / 255u);
    dst[1] = (uint8_t)(((uint32_t)src[1] * alpha + (uint32_t)dst[1] * inv + 127u) / 255u);
    dst[2] = (uint8_t)(((uint32_t)src[2] * alpha + (uint32_t)dst[2] * inv + 127u) / 255u);
    dst[3] = (uint8_t)(alpha + (((uint32_t)dst[3] * inv + 127u) / 255u));
}

static void image_destroy(vg_widget_t *widget) {
    vg_image_t *image = (vg_image_t *)widget;
    free(image->pixels);
    image->pixels = NULL;
    image->img_width = 0;
    image->img_height = 0;
}

static void image_measure(vg_widget_t *widget, float available_width, float available_height) {
    vg_image_t *image = (vg_image_t *)widget;

    float width = widget->constraints.preferred_width;
    float height = widget->constraints.preferred_height;

    if (width <= 0.0f) {
        width = image->img_width > 0 ? (float)image->img_width : available_width;
    }
    if (height <= 0.0f) {
        height = image->img_height > 0 ? (float)image->img_height : available_height;
    }

    if (width < widget->constraints.min_width)
        width = widget->constraints.min_width;
    if (height < widget->constraints.min_height)
        height = widget->constraints.min_height;
    if (widget->constraints.max_width > 0.0f && width > widget->constraints.max_width)
        width = widget->constraints.max_width;
    if (widget->constraints.max_height > 0.0f && height > widget->constraints.max_height)
        height = widget->constraints.max_height;

    widget->measured_width = width;
    widget->measured_height = height;
}

static void image_paint(vg_widget_t *widget, void *canvas) {
    vg_image_t *image = (vg_image_t *)widget;
    vgfx_framebuffer_t fb;
    if (!vgfx_get_framebuffer((vgfx_window_t)canvas, &fb))
        return;

    const int dx = (int)widget->x;
    const int dy = (int)widget->y;
    const int dw = (int)widget->width;
    const int dh = (int)widget->height;
    if (dw <= 0 || dh <= 0)
        return;

    if ((image->bg_color >> 24) != 0) {
        vgfx_fill_rect((vgfx_window_t)canvas, dx, dy, dw, dh, image->bg_color);
    }

    if (!image->pixels || image->img_width <= 0 || image->img_height <= 0 || image->opacity <= 0.0f)
        return;

    const int sw = image->img_width;
    const int sh = image->img_height;
    const uint8_t opacity = (uint8_t)(image->opacity * 255.0f + 0.5f);

    float draw_x = (float)dx;
    float draw_y = (float)dy;
    float draw_w = (float)dw;
    float draw_h = (float)dh;

    if (image->scale_mode == VG_IMAGE_SCALE_NONE) {
        draw_w = (float)sw;
        draw_h = (float)sh;
    } else if (image->scale_mode == VG_IMAGE_SCALE_FIT || image->scale_mode == VG_IMAGE_SCALE_FILL) {
        const float scale_x = (float)dw / (float)sw;
        const float scale_y = (float)dh / (float)sh;
        const float scale = (image->scale_mode == VG_IMAGE_SCALE_FILL)
                                ? (scale_x > scale_y ? scale_x : scale_y)
                                : (scale_x < scale_y ? scale_x : scale_y);
        draw_w = (float)sw * scale;
        draw_h = (float)sh * scale;
        draw_x = (float)dx + ((float)dw - draw_w) * 0.5f;
        draw_y = (float)dy + ((float)dh - draw_h) * 0.5f;
    }

    int start_x = clampi((int)draw_x, dx, dx + dw);
    int start_y = clampi((int)draw_y, dy, dy + dh);
    int end_x = clampi((int)(draw_x + draw_w), dx, dx + dw);
    int end_y = clampi((int)(draw_y + draw_h), dy, dy + dh);
    const struct vgfx_window *internal = (const struct vgfx_window *)canvas;
    int clip_x = 0;
    int clip_y = 0;
    int clip_w = fb.width;
    int clip_h = fb.height;

    if (internal && internal->clip_enabled) {
        clip_x = internal->clip_x;
        clip_y = internal->clip_y;
        clip_w = internal->clip_w;
        clip_h = internal->clip_h;
    }

    if (start_x < 0)
        start_x = 0;
    if (start_y < 0)
        start_y = 0;
    if (end_x > fb.width)
        end_x = fb.width;
    if (end_y > fb.height)
        end_y = fb.height;
    if (start_x < clip_x)
        start_x = clip_x;
    if (start_y < clip_y)
        start_y = clip_y;
    if (end_x > clip_x + clip_w)
        end_x = clip_x + clip_w;
    if (end_y > clip_y + clip_h)
        end_y = clip_y + clip_h;

    for (int fb_y = start_y; fb_y < end_y; fb_y++) {
        for (int fb_x = start_x; fb_x < end_x; fb_x++) {
            int sx = 0;
            int sy = 0;

            if (image->scale_mode == VG_IMAGE_SCALE_NONE) {
                sx = fb_x - (int)draw_x;
                sy = fb_y - (int)draw_y;
                if (sx < 0 || sx >= sw || sy < 0 || sy >= sh)
                    continue;
            } else {
                const float u = draw_w > 0.0f ? ((float)fb_x - draw_x) / draw_w : 0.0f;
                const float v = draw_h > 0.0f ? ((float)fb_y - draw_y) / draw_h : 0.0f;
                sx = clampi((int)(u * sw), 0, sw - 1);
                sy = clampi((int)(v * sh), 0, sh - 1);
            }

            const uint8_t *src = &image->pixels[(sy * sw + sx) * 4];
            uint8_t alpha = src[3];
            if (alpha == 0)
                continue;

            alpha = (uint8_t)(((uint32_t)alpha * opacity + 127u) / 255u);
            if (alpha == 0)
                continue;

            uint8_t *dst = &fb.pixels[fb_y * fb.stride + fb_x * 4];
            image_blend_pixel(dst, src, alpha);
        }
    }
}

vg_image_t *vg_image_create(vg_widget_t *parent) {
    vg_image_t *image = calloc(1, sizeof(vg_image_t));
    if (!image)
        return NULL;

    vg_widget_init(&image->base, VG_WIDGET_IMAGE, &g_image_vtable);

    image->scale_mode = VG_IMAGE_SCALE_FIT;
    image->opacity = 1.0f;
    image->bg_color = 0x00000000;

    if (parent) {
        vg_widget_add_child(parent, &image->base);
    }

    return image;
}

/// @brief Image set pixels.
void vg_image_set_pixels(vg_image_t *image, const uint8_t *pixels, int width, int height) {
    if (!image)
        return;

    free(image->pixels);
    image->pixels = NULL;
    image->img_width = 0;
    image->img_height = 0;

    if (!pixels || width <= 0 || height <= 0) {
        image->base.needs_layout = true;
        image->base.needs_paint = true;
        return;
    }

    size_t size = (size_t)width * (size_t)height * 4;
    image->pixels = malloc(size);
    if (!image->pixels) {
        image->base.needs_layout = true;
        image->base.needs_paint = true;
        return;
    }

    memcpy(image->pixels, pixels, size);
    image->img_width = width;
    image->img_height = height;
    image->base.needs_layout = true;
    image->base.needs_paint = true;
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
    image->base.needs_layout = true;
    image->base.needs_paint = true;
}

/// @brief Image set scale mode.
void vg_image_set_scale_mode(vg_image_t *image, vg_image_scale_t mode) {
    if (!image)
        return;
    image->scale_mode = mode;
    image->base.needs_paint = true;
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
    image->base.needs_paint = true;
}
