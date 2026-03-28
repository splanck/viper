//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_rendertarget3d.c
// Purpose: Viper.Graphics3D.RenderTarget3D — offscreen rendering target
//   with color + depth buffers. Enables render-to-texture for effects
//   like shadow maps, reflections, and post-processing.
//
// Key invariants:
//   - Color buffer is RGBA uint8_t (same format as vgfx framebuffer)
//   - Depth buffer is float (same as Z-buffer)
//   - AsPixels() returns a NEW Pixels object (fresh copy each call)
//   - GC finalizer frees both buffers
//
// Links: rt_canvas3d.h, rt_canvas3d_internal.h, plans/3d/08-render-to-texture.md
//
//===----------------------------------------------------------------------===//

#ifdef VIPER_ENABLE_GRAPHICS

#include "rt_canvas3d.h"
#include "rt_canvas3d_internal.h"
#include "vgfx3d_backend.h"

#include <float.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

extern void *rt_obj_new_i64(int64_t class_id, int64_t byte_size);
extern void rt_obj_set_finalizer(void *obj, void (*fn)(void *));
extern void rt_trap(const char *msg);
extern void *rt_pixels_new(int64_t width, int64_t height);

//=============================================================================
// Render target allocation
//=============================================================================

static vgfx3d_rendertarget_t *rt_alloc(int32_t w, int32_t h) {
    vgfx3d_rendertarget_t *rt = (vgfx3d_rendertarget_t *)calloc(1, sizeof(vgfx3d_rendertarget_t));
    if (!rt)
        return NULL;

    rt->width = w;
    rt->height = h;
    rt->stride = w * 4;

    size_t color_size = (size_t)w * (size_t)h * 4;
    size_t depth_size = (size_t)w * (size_t)h * sizeof(float);

    rt->color_buf = (uint8_t *)malloc(color_size);
    rt->depth_buf = (float *)malloc(depth_size);

    if (!rt->color_buf || !rt->depth_buf) {
        free(rt->color_buf);
        free(rt->depth_buf);
        free(rt);
        return NULL;
    }

    /* Clear to black, depth to FLT_MAX */
    memset(rt->color_buf, 0, color_size);
    int32_t total = w * h;
    for (int32_t i = 0; i < total; i++)
        rt->depth_buf[i] = FLT_MAX;

    return rt;
}

static void rt_free(vgfx3d_rendertarget_t *rt) {
    if (!rt)
        return;
    free(rt->color_buf);
    free(rt->depth_buf);
    free(rt);
}

//=============================================================================
// Runtime type
//=============================================================================

static void rt_rendertarget3d_finalize(void *obj) {
    rt_rendertarget3d *rtd = (rt_rendertarget3d *)obj;
    if (rtd->target) {
        rt_free(rtd->target);
        rtd->target = NULL;
    }
}

void *rt_rendertarget3d_new(int64_t width, int64_t height) {
    if (width <= 0 || height <= 0 || width > 8192 || height > 8192) {
        rt_trap("RenderTarget3D.New: dimensions must be 1-8192");
        return NULL;
    }

    rt_rendertarget3d *rtd =
        (rt_rendertarget3d *)rt_obj_new_i64(0, (int64_t)sizeof(rt_rendertarget3d));
    if (!rtd) {
        rt_trap("RenderTarget3D.New: memory allocation failed");
        return NULL;
    }

    rtd->vptr = NULL;
    rtd->width = width;
    rtd->height = height;
    rtd->target = rt_alloc((int32_t)width, (int32_t)height);

    if (!rtd->target) {
        rt_trap("RenderTarget3D.New: buffer allocation failed");
        return NULL;
    }

    rt_obj_set_finalizer(rtd, rt_rendertarget3d_finalize);
    return rtd;
}

int64_t rt_rendertarget3d_get_width(void *obj) {
    return obj ? ((rt_rendertarget3d *)obj)->width : 0;
}

int64_t rt_rendertarget3d_get_height(void *obj) {
    return obj ? ((rt_rendertarget3d *)obj)->height : 0;
}

void *rt_rendertarget3d_as_pixels(void *obj) {
    if (!obj)
        return NULL;
    rt_rendertarget3d *rtd = (rt_rendertarget3d *)obj;
    if (!rtd->target || !rtd->target->color_buf)
        return NULL;

    void *pixels = rt_pixels_new(rtd->width, rtd->height);
    if (!pixels)
        return NULL;

    /* Copy render target RGBA bytes → Pixels 0xRRGGBBAA uint32_t */
    typedef struct {
        int64_t w;
        int64_t h;
        uint32_t *data;
    } px_view;

    px_view *pv = (px_view *)pixels;

    int32_t w = rtd->target->width;
    int32_t h = rtd->target->height;
    int32_t stride = rtd->target->stride;

    for (int32_t y = 0; y < h; y++) {
        for (int32_t x = 0; x < w; x++) {
            const uint8_t *src = &rtd->target->color_buf[y * stride + x * 4];
            pv->data[y * pv->w + x] = ((uint32_t)src[0] << 24) | ((uint32_t)src[1] << 16) |
                                      ((uint32_t)src[2] << 8) | (uint32_t)src[3];
        }
    }

    return pixels;
}

//=============================================================================
// Canvas3D render target binding
//=============================================================================

/// @brief Bind an offscreen render target. All subsequent Begin/DrawMesh/End
/// calls render to the target instead of the window. The active backend
/// (Metal, software, etc.) handles RTT natively — no backend switching needed.
void rt_canvas3d_set_render_target(void *canvas, void *target) {
    if (!canvas || !target)
        return;
    rt_canvas3d *c = (rt_canvas3d *)canvas;
    rt_rendertarget3d *rtd = (rt_rendertarget3d *)target;
    c->render_target = rtd->target;

    if (c->backend && c->backend->set_render_target)
        c->backend->set_render_target(c->backend_ctx, rtd->target);
}

/// @brief Unbind the render target. Subsequent rendering goes to the window.
void rt_canvas3d_reset_render_target(void *canvas) {
    if (!canvas)
        return;
    rt_canvas3d *c = (rt_canvas3d *)canvas;

    if (c->backend && c->backend->set_render_target)
        c->backend->set_render_target(c->backend_ctx, NULL);

    c->render_target = NULL;
}

#endif /* VIPER_ENABLE_GRAPHICS */
