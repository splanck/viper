// vg_image.c - Image widget implementation
#include "../../include/vg_widgets.h"
#include <stdlib.h>
#include <string.h>

vg_image_t* vg_image_create(vg_widget_t* parent) {
    vg_image_t* image = calloc(1, sizeof(vg_image_t));
    if (!image) return NULL;

    image->base.type = VG_WIDGET_IMAGE;
    image->base.visible = true;
    image->base.enabled = true;

    // Default values
    image->scale_mode = VG_IMAGE_SCALE_FIT;
    image->opacity = 1.0f;
    image->bg_color = 0x00000000; // Transparent

    if (parent) {
        vg_widget_add_child(parent, &image->base);
    }

    return image;
}

void vg_image_set_pixels(vg_image_t* image, const uint8_t* pixels, int width, int height) {
    if (!image) return;

    // Free existing
    free(image->pixels);
    image->pixels = NULL;
    image->img_width = 0;
    image->img_height = 0;

    if (!pixels || width <= 0 || height <= 0) return;

    // Copy pixel data (RGBA format)
    size_t size = (size_t)width * (size_t)height * 4;
    image->pixels = malloc(size);
    if (!image->pixels) return;

    memcpy(image->pixels, pixels, size);
    image->img_width = width;
    image->img_height = height;
}

bool vg_image_load_file(vg_image_t* image, const char* path) {
    if (!image || !path) return false;

    // TODO: Implement image file loading (PNG, JPEG, BMP)
    // This would require an image decoding library like stb_image
    // For now, return false
    (void)path;
    return false;
}

void vg_image_clear(vg_image_t* image) {
    if (!image) return;
    free(image->pixels);
    image->pixels = NULL;
    image->img_width = 0;
    image->img_height = 0;
}

void vg_image_set_scale_mode(vg_image_t* image, vg_image_scale_t mode) {
    if (!image) return;
    image->scale_mode = mode;
}

void vg_image_set_opacity(vg_image_t* image, float opacity) {
    if (!image) return;
    if (opacity < 0) opacity = 0;
    if (opacity > 1) opacity = 1;
    image->opacity = opacity;
}
