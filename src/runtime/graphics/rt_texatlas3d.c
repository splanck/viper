//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_texatlas3d.c
// Purpose: Texture atlas with shelf-based bin packing.
//
// Key invariants:
//   - Shelf Next-Fit: textures placed left-to-right, new row when full.
//   - 1-pixel padding border around each sub-texture.
//   - UV rect returned in normalized [0,1] atlas coordinates.
//
// Links: rt_texatlas3d.h
//
//===----------------------------------------------------------------------===//

#ifdef VIPER_ENABLE_GRAPHICS

#include "rt_texatlas3d.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

extern void *rt_obj_new_i64(int64_t class_id, int64_t byte_size);
extern void rt_obj_set_finalizer(void *obj, void (*fn)(void *));
#include "rt_trap.h"
extern void *rt_pixels_new(int64_t w, int64_t h);

typedef struct {
    int32_t x, y, w, h; /* position and size in atlas */
} atlas_region_t;

#define ATLAS_MAX_REGIONS 256

typedef struct {
    void *vptr;
    uint32_t *data;
    int32_t width, height;
    atlas_region_t regions[ATLAS_MAX_REGIONS];
    int32_t region_count;
    int32_t shelf_x, shelf_y, shelf_h; /* current shelf position */
    void *cached_pixels;               /* Pixels object, rebuilt when dirty */
    int8_t dirty;
} rt_texatlas3d;

static void texatlas3d_finalizer(void *obj) {
    rt_texatlas3d *a = (rt_texatlas3d *)obj;
    free(a->data);
    a->data = NULL;
}

/// @brief Construct a 3D texture atlas with `width × height` blank pixels (zero-initialized).
/// Tracks shelf packing state so subsequent `_add` calls fit textures left-to-right with 1-pixel
/// padding. Traps if dimensions are outside [16, 8192] or on allocation failure.
void *rt_texatlas3d_new(int64_t width, int64_t height) {
    if (width < 16 || height < 16 || width > 8192 || height > 8192) {
        rt_trap("TextureAtlas3D.New: dimensions must be 16-8192");
        return NULL;
    }
    rt_texatlas3d *a = (rt_texatlas3d *)rt_obj_new_i64(0, (int64_t)sizeof(rt_texatlas3d));
    if (!a) {
        rt_trap("TextureAtlas3D.New: allocation failed");
        return NULL;
    }
    a->vptr = NULL;
    a->width = (int32_t)width;
    a->height = (int32_t)height;
    a->data = (uint32_t *)calloc((size_t)(width * height), sizeof(uint32_t));
    a->region_count = 0;
    a->shelf_x = 0;
    a->shelf_y = 0;
    a->shelf_h = 0;
    a->cached_pixels = NULL;
    a->dirty = 1;
    rt_obj_set_finalizer(a, texatlas3d_finalizer);
    return a;
}

/// @brief Pack a Pixels sub-image into the next free shelf slot. Allocates 1-pixel padding
/// around the image, advances the shelf cursor, and returns the integer region ID for later
/// `_get_uv_rect` lookup. Returns -1 if the atlas is full (256 regions) or if the texture
/// won't fit in the remaining vertical space. Marks the atlas dirty for the next texture rebuild.
int64_t rt_texatlas3d_add(void *obj, void *pixels) {
    if (!obj || !pixels)
        return -1;
    rt_texatlas3d *a = (rt_texatlas3d *)obj;
    if (a->region_count >= ATLAS_MAX_REGIONS)
        return -1;

    typedef struct {
        int64_t w;
        int64_t h;
        uint32_t *data;
    } px_view;

    px_view *pv = (px_view *)pixels;
    if (!pv->data)
        return -1;

    int32_t tw = (int32_t)pv->w + 2; /* +2 for 1px border padding */
    int32_t th = (int32_t)pv->h + 2;

    /* Try to fit on current shelf */
    if (a->shelf_x + tw > a->width) {
        /* Start new shelf */
        a->shelf_y += a->shelf_h;
        a->shelf_x = 0;
        a->shelf_h = 0;
    }
    if (a->shelf_y + th > a->height)
        return -1; /* atlas full */

    /* Place texture at (shelf_x + 1, shelf_y + 1) — skip 1px border */
    int32_t dx = a->shelf_x + 1;
    int32_t dy = a->shelf_y + 1;

    for (int32_t y = 0; y < (int32_t)pv->h; y++) {
        for (int32_t x = 0; x < (int32_t)pv->w; x++) {
            int32_t ax = dx + x, ay = dy + y;
            if (ax < a->width && ay < a->height)
                a->data[ay * a->width + ax] = pv->data[y * pv->w + x];
        }
    }

    atlas_region_t *r = &a->regions[a->region_count];
    r->x = dx;
    r->y = dy;
    r->w = (int32_t)pv->w;
    r->h = (int32_t)pv->h;

    a->shelf_x += tw;
    if (th > a->shelf_h)
        a->shelf_h = th;
    a->dirty = 1;

    return a->region_count++;
}

/// @brief Return the atlas as a Pixels object, lazily rebuilding it from the internal data
/// buffer when dirty. The returned Pixels is owned by the atlas — do not free or release. Each
/// call after a `_add` rebuilds; results in stable storage when no changes have been made.
void *rt_texatlas3d_get_texture(void *obj) {
    if (!obj)
        return NULL;
    rt_texatlas3d *a = (rt_texatlas3d *)obj;

    if (a->dirty || !a->cached_pixels) {
        /* Create a Pixels object from atlas data */
        a->cached_pixels = rt_pixels_new(a->width, a->height);
        if (a->cached_pixels) {
            typedef struct {
                int64_t w;
                int64_t h;
                uint32_t *data;
            } px_view;

            px_view *pv = (px_view *)a->cached_pixels;
            memcpy(pv->data, a->data, (size_t)(a->width * a->height) * sizeof(uint32_t));
        }
        a->dirty = 0;
    }
    return a->cached_pixels;
}

/// @brief Output the normalized UV rectangle [0,1] for region `id` into the four out-pointers.
/// Use these UVs as mesh texture coordinates to sample the packed sub-image. Out-of-range IDs
/// return the full atlas rect (0..1) so missing textures degrade visibly rather than silently.
void rt_texatlas3d_get_uv_rect(
    void *obj, int64_t id, double *u0, double *v0, double *u1, double *v1) {
    if (!obj || !u0 || !v0 || !u1 || !v1)
        return;
    rt_texatlas3d *a = (rt_texatlas3d *)obj;
    if (id < 0 || id >= a->region_count) {
        *u0 = *v0 = 0.0;
        *u1 = *v1 = 1.0;
        return;
    }
    atlas_region_t *r = &a->regions[id];
    *u0 = (double)r->x / a->width;
    *v0 = (double)r->y / a->height;
    *u1 = (double)(r->x + r->w) / a->width;
    *v1 = (double)(r->y + r->h) / a->height;
}

#else
typedef int rt_graphics_disabled_tu_guard;
#endif /* VIPER_ENABLE_GRAPHICS */
