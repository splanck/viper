//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/render/rt_canvas3d_overlay.c
// Purpose: Canvas3D screen-space overlay, screenshot, and debug-draw helpers.
//   Implements Viper.Graphics3D.Canvas3D's debug visualizers (lines, points,
//   AABB / sphere wireframes, axis gizmos), HUD primitives (rect, crosshair,
//   text), backend-capability queries, and the screenshot capture path.
//
// Key invariants:
//   - All overlay draws automatically open and close a temporary overlay
//     frame when called outside of an explicit Begin/End bracket.
//   - 3D-anchored overlays project through `canvas3d_active_scene_vp` so
//     gizmos drawn after `End` stay anchored to the scene that was just
//     rendered.
//   - Screenshot RGBA packing follows `rt_pixels`'s 0xRRGGBBAA convention
//     so captured images can be saved without a swizzle pass.
//
// Ownership/Lifetime:
//   - Helpers borrow the canvas / Vec3 inputs; locally constructed Vec3s
//     are released via `canvas3d_release_local` before returning.
//   - `rt_canvas3d_screenshot` returns a freshly allocated Pixels object;
//     the caller takes ownership.
//
// Links: rt_canvas3d.h, rt_canvas3d_internal.h, vgfx3d_backend.h
//
//===----------------------------------------------------------------------===//

#ifdef VIPER_ENABLE_GRAPHICS

#include "rt_canvas3d.h"
#include "rt_canvas3d_internal.h"
#include "rt_object.h"
#include "rt_pixels.h"
#include "rt_string.h"
#include "rt_vec3.h"
#include "vgfx3d_backend.h"

#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/// @brief Project a 3D world-space point onto 2D screen coordinates using the active scene VP.
/// @details Standard `world → clip → NDC → screen` pipeline:
///          1. Multiply (wp.x, wp.y, wp.z, 1) by the cached view-projection
///             matrix to land in homogeneous clip space.
///          2. Reject points behind the camera (`clip.w <= 0`) — those
///             would invert through the perspective divide.
///          3. Perspective-divide to NDC, remap [-1,1] → [0, fb_w/h], and
///             flip Y so origin sits at the top-left to match screen
///             conventions.
///          Uses `canvas3d_active_scene_vp` so debug overlays projected
///          *after* `End` still use the same VP that drew the scene —
///          markers stay anchored across the begin/end boundary.
///          Returns 0 if no scene VP is available or the point is behind
///          the camera (caller should skip the draw).
static int world_to_screen(
    const rt_canvas3d *c, const float *wp, float *sx, float *sy, int32_t fb_w, int32_t fb_h) {
    const float *vp = canvas3d_active_scene_vp(c);
    float pos4[4] = {wp[0], wp[1], wp[2], 1.0f};
    float clip[4];
    if (!vp)
        return 0;
    clip[0] = vp[0] * pos4[0] + vp[1] * pos4[1] + vp[2] * pos4[2] + vp[3] * pos4[3];
    clip[1] = vp[4] * pos4[0] + vp[5] * pos4[1] + vp[6] * pos4[2] + vp[7] * pos4[3];
    clip[2] = vp[8] * pos4[0] + vp[9] * pos4[1] + vp[10] * pos4[2] + vp[11] * pos4[3];
    clip[3] = vp[12] * pos4[0] + vp[13] * pos4[1] + vp[14] * pos4[2] + vp[15] * pos4[3];
    if (clip[3] <= 0.0f)
        return 0;
    {
        const float iw = 1.0f / clip[3];
        *sx = (clip[0] * iw + 1.0f) * 0.5f * (float)fb_w;
        *sy = (1.0f - clip[1] * iw) * 0.5f * (float)fb_h;
    }
    return 1;
}

typedef struct {
    int64_t w;
    int64_t h;
    uint32_t *data;
} canvas3d_pixels_view_t;

/// @brief Resolve the active output surface's public coordinate size.
/// @details When a render target is bound, overlays size to the RTT. Otherwise
///          they size to the Canvas3D logical dimensions, not the framebuffer
///          backing size, so HiDPI windows keep stable public coordinates.
static int overlay_output_size(const rt_canvas3d *c, int32_t *out_w, int32_t *out_h) {
    if (out_w)
        *out_w = 0;
    if (out_h)
        *out_h = 0;
    if (!c)
        return 0;
    if (c->render_target) {
        if (out_w)
            *out_w = c->render_target->width;
        if (out_h)
            *out_h = c->render_target->height;
        return c->render_target->width > 0 && c->render_target->height > 0;
    }
    if (out_w)
        *out_w = c->width;
    if (out_h)
        *out_h = c->height;
    return c->width > 0 && c->height > 0;
}

/// @brief Pack an RGBA byte surface into `Pixels`, scaling to logical size when needed.
static int canvas3d_pack_rgba_to_pixels(canvas3d_pixels_view_t *pv,
                                        const uint8_t *src,
                                        int32_t src_w,
                                        int32_t src_h,
                                        int32_t src_stride,
                                        int32_t dst_w,
                                        int32_t dst_h) {
    if (!pv || !pv->data || !src || src_w <= 0 || src_h <= 0 || dst_w <= 0 || dst_h <= 0)
        return 0;
    if (src_stride < src_w * 4)
        return 0;

    if (src_w == dst_w && src_h == dst_h) {
        for (int32_t y = 0; y < dst_h; y++) {
            for (int32_t x = 0; x < dst_w; x++) {
                const uint8_t *p = src + (size_t)y * (size_t)src_stride + (size_t)x * 4u;
                pv->data[(size_t)y * (size_t)pv->w + (size_t)x] =
                    ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) |
                    (uint32_t)p[3];
            }
        }
        return 1;
    }

    for (int32_t y = 0; y < dst_h; y++) {
        int32_t y0 = (int32_t)(((int64_t)y * src_h) / dst_h);
        int32_t y1 = (int32_t)(((int64_t)(y + 1) * src_h) / dst_h);
        if (y1 <= y0)
            y1 = y0 + 1;
        if (y1 > src_h)
            y1 = src_h;
        for (int32_t x = 0; x < dst_w; x++) {
            int32_t x0 = (int32_t)(((int64_t)x * src_w) / dst_w);
            int32_t x1 = (int32_t)(((int64_t)(x + 1) * src_w) / dst_w);
            uint64_t r = 0;
            uint64_t g = 0;
            uint64_t b = 0;
            uint64_t a = 0;
            uint64_t count = 0;
            if (x1 <= x0)
                x1 = x0 + 1;
            if (x1 > src_w)
                x1 = src_w;
            for (int32_t sy = y0; sy < y1; sy++) {
                const uint8_t *row = src + (size_t)sy * (size_t)src_stride;
                for (int32_t sx = x0; sx < x1; sx++) {
                    const uint8_t *p = row + (size_t)sx * 4u;
                    r += p[0];
                    g += p[1];
                    b += p[2];
                    a += p[3];
                    count++;
                }
            }
            if (count == 0)
                count = 1;
            pv->data[(size_t)y * (size_t)pv->w + (size_t)x] =
                ((uint32_t)((r + count / 2u) / count) << 24) |
                ((uint32_t)((g + count / 2u) / count) << 16) |
                ((uint32_t)((b + count / 2u) / count) << 8) | (uint32_t)((a + count / 2u) / count);
        }
    }
    return 1;
}

/// @brief Drop one reference and free if zero. Safe on NULL.
static void canvas3d_release_local(void *obj) {
    if (obj && rt_obj_release_check0(obj))
        rt_obj_free(obj);
}

/// @brief Draw a 3D world-space line between two Vec3 endpoints in `color`. Useful for debug
/// visualizers, motion trails, gizmos. Color is 0xRRGGBBAA. Auto-projects to screen space.
void rt_canvas3d_draw_line3d_raw(void *obj, const double *from, const double *to, int64_t color) {
    int8_t started_temp_frame = 0;

    rt_canvas3d *c = rt_canvas3d_checked_or_stack(obj);
    if (!c || !from || !to)
        return;
    int32_t out_w = 0;
    int32_t out_h = 0;
    if (!overlay_output_size(c, &out_w, &out_h))
        return;

    {
        float p0[3] = {(float)from[0], (float)from[1], (float)from[2]};
        float p1[3] = {(float)to[0], (float)to[1], (float)to[2]};
        float sx0;
        float sy0;
        float sx1;
        float sy1;
        if (!world_to_screen(c, p0, &sx0, &sy0, out_w, out_h))
            return;
        if (!world_to_screen(c, p1, &sx1, &sy1, out_w, out_h))
            return;
        if (!c->in_frame) {
            if (!canvas3d_begin_overlay_frame(c, 1))
                return;
            started_temp_frame = 1;
        }
        (void)canvas3d_queue_screen_line(c,
                                         sx0,
                                         sy0,
                                         sx1,
                                         sy1,
                                         1.0f,
                                         (float)((color >> 16) & 0xFF) / 255.0f,
                                         (float)((color >> 8) & 0xFF) / 255.0f,
                                         (float)(color & 0xFF) / 255.0f,
                                         1.0f);
    }
    if (started_temp_frame)
        rt_canvas3d_end(c);
}

/// @brief Draw a 3D line segment between two Vec3 world points in the given packed color.
void rt_canvas3d_draw_line3d(void *obj, void *from, void *to, int64_t color) {
    double p0[3];
    double p1[3];
    if (!from || !to)
        return;
    p0[0] = rt_vec3_x(from);
    p0[1] = rt_vec3_y(from);
    p0[2] = rt_vec3_z(from);
    p1[0] = rt_vec3_x(to);
    p1[1] = rt_vec3_y(to);
    p1[2] = rt_vec3_z(to);
    rt_canvas3d_draw_line3d_raw(obj, p0, p1, color);
}

/// @brief Draw a 3D world-space point at `pos` (Vec3) as a `size`-pixel filled square in `color`.
/// Useful for marking spawn points, raycast hits, AI waypoints during debug.
void rt_canvas3d_draw_point3d(void *obj, void *pos, int64_t color, int64_t size) {
    int8_t started_temp_frame = 0;

    rt_canvas3d *c = rt_canvas3d_checked_or_stack(obj);
    if (!c || !pos)
        return;
    int32_t out_w = 0;
    int32_t out_h = 0;
    if (!overlay_output_size(c, &out_w, &out_h))
        return;

    {
        float p[3] = {(float)rt_vec3_x(pos), (float)rt_vec3_y(pos), (float)rt_vec3_z(pos)};
        float sx;
        float sy;
        if (!world_to_screen(c, p, &sx, &sy, out_w, out_h))
            return;
        if (!c->in_frame) {
            if (!canvas3d_begin_overlay_frame(c, 1))
                return;
            started_temp_frame = 1;
        }
        {
            const float side = size > 0 ? (float)size : 1.0f;
            const float half = side * 0.5f;
            (void)canvas3d_queue_screen_rect(c,
                                             sx - half,
                                             sy - half,
                                             side,
                                             side,
                                             (float)((color >> 16) & 0xFF) / 255.0f,
                                             (float)((color >> 8) & 0xFF) / 255.0f,
                                             (float)(color & 0xFF) / 255.0f,
                                             1.0f);
        }
    }
    if (started_temp_frame)
        rt_canvas3d_end(c);
}

/// @brief Draw a screen-space (2D, ignores 3D camera) filled rectangle. Useful for HUDs and
/// debug overlays composited over the 3D scene.
void rt_canvas3d_draw_rect2d(void *obj, int64_t x, int64_t y, int64_t w, int64_t h, int64_t color) {
    int8_t started_temp_frame = 0;

    rt_canvas3d *c = rt_canvas3d_checked_or_stack(obj);
    if (!c)
        return;
    if (w <= 0 || h <= 0)
        return;
    if (!c->in_frame) {
        if (!canvas3d_begin_overlay_frame(c, 1))
            return;
        started_temp_frame = 1;
    }
    rt_canvas3d_draw_rect_3d(c, x, y, w, h, color);
    if (started_temp_frame)
        rt_canvas3d_end(c);
}

/// @brief Draw a centered crosshair (FPS reticle) at screen center with `size` arms in `color`.
void rt_canvas3d_draw_crosshair(void *obj, int64_t color, int64_t size) {
    int8_t started_temp_frame = 0;

    rt_canvas3d *c = rt_canvas3d_checked_or_stack(obj);
    if (!c)
        return;
    int32_t out_w = 0;
    int32_t out_h = 0;
    if (!overlay_output_size(c, &out_w, &out_h))
        return;

    {
        const int32_t cx = out_w / 2;
        const int32_t cy = out_h / 2;
        const int32_t half = (int32_t)(size / 2);
        const float r = (float)((color >> 16) & 0xFF) / 255.0f;
        const float g = (float)((color >> 8) & 0xFF) / 255.0f;
        const float b = (float)(color & 0xFF) / 255.0f;

        if (!c->in_frame) {
            if (!canvas3d_begin_overlay_frame(c, 1))
                return;
            started_temp_frame = 1;
        }
        (void)canvas3d_queue_screen_line(
            c, (float)(cx - half), (float)cy, (float)(cx + half), (float)cy, 1.0f, r, g, b, 1.0f);
        (void)canvas3d_queue_screen_line(
            c, (float)cx, (float)(cy - half), (float)cx, (float)(cy + half), 1.0f, r, g, b, 1.0f);
    }
    if (started_temp_frame)
        rt_canvas3d_end(c);
}

/// @brief Draw screen-space text at (x, y) using the built-in 8×8 font in `color`.
void rt_canvas3d_draw_text2d(void *obj, int64_t x, int64_t y, rt_string text, int64_t color) {
    int8_t started_temp_frame = 0;

    rt_canvas3d *c = rt_canvas3d_checked_or_stack(obj);
    if (!c || !text)
        return;
    if (!c->in_frame) {
        if (!canvas3d_begin_overlay_frame(c, 1))
            return;
        started_temp_frame = 1;
    }
    rt_canvas3d_draw_text_3d(c, x, y, text, color);
    if (started_temp_frame)
        rt_canvas3d_end(c);
}

/// @brief Return the active backend name as a string ("metal", "d3d11", "opengl", ...).
/// Useful for backend-specific debug output / feature gating.
rt_string rt_canvas3d_get_backend(void *obj) {
    rt_canvas3d *c = rt_canvas3d_checked_or_stack(obj);
    if (!c)
        return rt_const_cstr("unknown");
    return rt_const_cstr(c->backend ? c->backend->name : "unknown");
}

/// @brief True when the active backend has the GPU many-light shader/upload path.
/// @details Keep this tied to the real backend vtables, not backend-name strings, so
///          stack/fake unit-test backends do not accidentally advertise production
///          clustered/forward+ support just because they use a GPU-like name.
static int canvas3d_backend_supports_clustered_lighting(const vgfx3d_backend_t *backend) {
    if (!backend)
        return 0;
    if (backend == &vgfx3d_software_backend ||
        (backend->name && strcmp(backend->name, "software") == 0))
        return 1;
#if defined(__APPLE__)
    if (backend == &vgfx3d_metal_backend)
        return 1;
#endif
#if defined(_WIN32)
    if (backend == &vgfx3d_d3d11_backend)
        return 1;
#endif
#if defined(__linux__)
    if (backend == &vgfx3d_opengl_backend)
        return 1;
#endif
    return 0;
}

/// @brief True when the backend can consume multiple shadow slots as primary-light cascades.
static int canvas3d_backend_supports_shadow_csm(const vgfx3d_backend_t *backend) {
    if (!backend || !backend->shadow_begin || !backend->shadow_draw || !backend->shadow_end)
        return 0;
    if (backend == &vgfx3d_software_backend ||
        (backend->name && strcmp(backend->name, "software") == 0))
        return 1;
#if defined(__APPLE__)
    if (backend == &vgfx3d_metal_backend)
        return 1;
#endif
#if defined(_WIN32)
    if (backend == &vgfx3d_d3d11_backend)
        return 1;
#endif
#if defined(__linux__)
    if (backend == &vgfx3d_opengl_backend)
        return 1;
#endif
    return 0;
}

/// @brief Return the feature bits advertised by the active backend.
/// @details The mask is based on backend vtable hooks plus the software
///          fallback paths owned by Canvas3D. This lets applications choose
///          production-safe rendering paths without hardcoding backend names.
int64_t rt_canvas3d_get_backend_capabilities(void *obj) {
    int64_t caps = 0;

    rt_canvas3d *c = rt_canvas3d_checked_or_stack(obj);
    if (!c)
        return 0;
    const vgfx3d_backend_t *backend = c->backend;
    if (!backend)
        return 0;

    if (backend == &vgfx3d_software_backend ||
        (backend->name && strcmp(backend->name, "software") == 0))
        caps |= RT_CANVAS3D_BACKEND_CAP_SOFTWARE;
    else
        caps |= RT_CANVAS3D_BACKEND_CAP_GPU;

    if (backend->set_render_target)
        caps |= RT_CANVAS3D_BACKEND_CAP_RENDER_TARGET;
    if (backend->readback_rgba || (caps & RT_CANVAS3D_BACKEND_CAP_SOFTWARE))
        caps |= RT_CANVAS3D_BACKEND_CAP_WINDOW_READBACK;
    if (backend->shadow_begin && backend->shadow_draw && backend->shadow_end)
        caps |= RT_CANVAS3D_BACKEND_CAP_SHADOWS;
    if (backend->draw_skybox || (caps & RT_CANVAS3D_BACKEND_CAP_SOFTWARE))
        caps |= RT_CANVAS3D_BACKEND_CAP_SKYBOX;
    if (backend->submit_draw_instanced)
        caps |= RT_CANVAS3D_BACKEND_CAP_HARDWARE_INSTANCING;
    if (backend->present_postfx || (caps & RT_CANVAS3D_BACKEND_CAP_SOFTWARE))
        caps |= RT_CANVAS3D_BACKEND_CAP_POSTFX;
    if (backend->present_postfx)
        caps |= RT_CANVAS3D_BACKEND_CAP_GPU_POSTFX;
    if (caps & RT_CANVAS3D_BACKEND_CAP_SOFTWARE)
        caps |= RT_CANVAS3D_BACKEND_CAP_POSTFX_OVERLAY;
    if (backend->present_postfx && backend->apply_postfx && backend->present)
        caps |= RT_CANVAS3D_BACKEND_CAP_POSTFX_OVERLAY | RT_CANVAS3D_BACKEND_CAP_GPU_POSTFX_OVERLAY;
    if (caps & RT_CANVAS3D_BACKEND_CAP_WINDOW_READBACK)
        caps |= RT_CANVAS3D_BACKEND_CAP_FINAL_SCREENSHOT;
    if (canvas3d_backend_supports_clustered_lighting(backend))
        caps |= RT_CANVAS3D_BACKEND_CAP_CLUSTERED_LIGHTING;
    if (canvas3d_backend_supports_shadow_csm(backend))
        caps |= RT_CANVAS3D_BACKEND_CAP_SHADOW_CSM;
    if (backend->get_native_texture_caps)
        caps |= backend->get_native_texture_caps(c->backend_ctx) &
                (RT_CANVAS3D_BACKEND_CAP_BC7 | RT_CANVAS3D_BACKEND_CAP_ASTC |
                 RT_CANVAS3D_BACKEND_CAP_ETC2);
    caps |= RT_CANVAS3D_BACKEND_CAP_OCCLUSION | RT_CANVAS3D_BACKEND_CAP_HLOD;

    return caps;
}

/// @brief Convert a user-facing capability string to its internal bitmask flag.
/// @details `Canvas3D.SupportsCapability("shadows")` flows through here so the Zia-side
///   name survives as a readable string rather than a numeric enum. Several common
///   aliases are accepted per flag ("shadows" / "shadow_maps", "postfx" / "post_fx", etc.)
///   so scripts can use whichever reads more natural. Unknown names return 0, which the
///   caller treats as "capability not supported".
static int64_t canvas3d_capability_from_name(const char *name) {
    if (!name || !*name)
        return 0;
    if (strcmp(name, "software") == 0)
        return RT_CANVAS3D_BACKEND_CAP_SOFTWARE;
    if (strcmp(name, "gpu") == 0)
        return RT_CANVAS3D_BACKEND_CAP_GPU;
    if (strcmp(name, "render_target") == 0 || strcmp(name, "rendertarget") == 0)
        return RT_CANVAS3D_BACKEND_CAP_RENDER_TARGET;
    if (strcmp(name, "window_readback") == 0 || strcmp(name, "readback") == 0 ||
        strcmp(name, "screenshot") == 0)
        return RT_CANVAS3D_BACKEND_CAP_WINDOW_READBACK;
    if (strcmp(name, "shadows") == 0 || strcmp(name, "shadow_maps") == 0)
        return RT_CANVAS3D_BACKEND_CAP_SHADOWS;
    if (strcmp(name, "skybox") == 0 || strcmp(name, "cubemap_skybox") == 0)
        return RT_CANVAS3D_BACKEND_CAP_SKYBOX;
    if (strcmp(name, "hardware_instancing") == 0 || strcmp(name, "instancing") == 0)
        return RT_CANVAS3D_BACKEND_CAP_HARDWARE_INSTANCING;
    if (strcmp(name, "postfx") == 0 || strcmp(name, "post_fx") == 0)
        return RT_CANVAS3D_BACKEND_CAP_POSTFX;
    if (strcmp(name, "gpu_postfx") == 0 || strcmp(name, "gpu_post_fx") == 0)
        return RT_CANVAS3D_BACKEND_CAP_GPU_POSTFX;
    if (strcmp(name, "postfx-overlay") == 0 || strcmp(name, "postfx_overlay") == 0 ||
        strcmp(name, "post_fx_overlay") == 0)
        return RT_CANVAS3D_BACKEND_CAP_POSTFX_OVERLAY;
    if (strcmp(name, "final-screenshot") == 0 || strcmp(name, "final_screenshot") == 0)
        return RT_CANVAS3D_BACKEND_CAP_FINAL_SCREENSHOT;
    if (strcmp(name, "gpu-postfx-overlay") == 0 || strcmp(name, "gpu_postfx_overlay") == 0 ||
        strcmp(name, "gpu_post_fx_overlay") == 0)
        return RT_CANVAS3D_BACKEND_CAP_GPU_POSTFX_OVERLAY;
    if (strcmp(name, "clustered-lighting") == 0 || strcmp(name, "clustered_lighting") == 0 ||
        strcmp(name, "forward_plus") == 0 || strcmp(name, "forward+") == 0)
        return RT_CANVAS3D_BACKEND_CAP_CLUSTERED_LIGHTING;
    if (strcmp(name, "shadow-csm") == 0 || strcmp(name, "shadow_csm") == 0 ||
        strcmp(name, "cascaded-shadows") == 0 || strcmp(name, "cascaded_shadows") == 0)
        return RT_CANVAS3D_BACKEND_CAP_SHADOW_CSM;
    if (strcmp(name, "occlusion") == 0 || strcmp(name, "occlusion-culling") == 0 ||
        strcmp(name, "occlusion_culling") == 0)
        return RT_CANVAS3D_BACKEND_CAP_OCCLUSION;
    if (strcmp(name, "hlod") == 0 || strcmp(name, "impostor") == 0 ||
        strcmp(name, "impostors") == 0 || strcmp(name, "auto-lod") == 0 ||
        strcmp(name, "auto_lod") == 0)
        return RT_CANVAS3D_BACKEND_CAP_HLOD;
    if (strcmp(name, "bc7") == 0)
        return RT_CANVAS3D_BACKEND_CAP_BC7;
    if (strcmp(name, "astc") == 0)
        return RT_CANVAS3D_BACKEND_CAP_ASTC;
    if (strcmp(name, "etc2") == 0)
        return RT_CANVAS3D_BACKEND_CAP_ETC2;
    return 0;
}

/// @brief Return whether the active backend supports a named capability.
int8_t rt_canvas3d_backend_supports(void *obj, rt_string capability) {
    int64_t flag;

    if (!obj || !capability)
        return 0;
    flag = canvas3d_capability_from_name(rt_string_cstr(capability));
    if (!flag)
        return 0;
    return (rt_canvas3d_get_backend_capabilities(obj) & flag) ? 1 : 0;
}

/// @brief Number of main 3D draw submissions queued by the latest ended frame.
int64_t rt_canvas3d_get_draw_count(void *obj) {
    rt_canvas3d *c = rt_canvas3d_checked_or_stack(obj);
    return c ? c->last_draw_count : 0;
}

/// @brief Number of latest Scene3D draw submissions skipped by visibility culling.
int64_t rt_canvas3d_get_occluded_draw_count(void *obj) {
    rt_canvas3d *c = rt_canvas3d_checked_or_stack(obj);
    return c ? c->last_occluded_draw_count : 0;
}

/// @brief Number of opaque draws tested by the CPU occlusion grid in the latest frame.
int64_t rt_canvas3d_get_occlusion_candidate_count(void *obj) {
    rt_canvas3d *c = rt_canvas3d_checked_or_stack(obj);
    return c ? c->last_occlusion_candidate_count : 0;
}

/// @brief Texture payload bytes uploaded to backend storage in the latest ended frame.
int64_t rt_canvas3d_get_texture_upload_bytes(void *obj) {
    rt_canvas3d *c = rt_canvas3d_checked_or_stack(obj);
    return c ? c->last_texture_upload_bytes : 0;
}

/// @brief Set the active backend's per-frame texture upload budget.
void rt_canvas3d_set_texture_upload_budget(void *obj, int64_t bytes) {
    rt_canvas3d *c = rt_canvas3d_checked_or_stack(obj);
    uint64_t budget = bytes < 0 ? UINT64_MAX : (uint64_t)bytes;
    if (c && c->backend && c->backend->set_texture_upload_budget)
        c->backend->set_texture_upload_budget(c->backend_ctx, budget);
}

/// @brief Texture payload bytes still waiting for backend texture upload budget.
int64_t rt_canvas3d_get_texture_upload_pending_bytes(void *obj) {
    rt_canvas3d *c = rt_canvas3d_checked_or_stack(obj);
    uint64_t bytes = 0;
    if (c && c->backend && c->backend->get_texture_upload_pending_bytes)
        bytes = c->backend->get_texture_upload_pending_bytes(c->backend_ctx);
    return bytes > (uint64_t)INT64_MAX ? INT64_MAX : (int64_t)bytes;
}

/// @brief Capture the current canvas contents into a freshly allocated Pixels object.
/// @details Three-way capture path, picked by what's bound:
///          1. RTT bound → call `rendertarget_sync_color_if_needed` to
///             pull GPU contents back to CPU, then RGBA-pack each row
///             into the Pixels buffer.
///          2. GPU backend with `readback_rgba` → allocate a temp
///             RGBA buffer, ask the backend to fill it, repack.
///          3. Software backend / fallback → copy directly from the
///             window's CPU framebuffer.
///          The 0xRRGGBBAA pack here matches the `rt_pixels` storage
///          convention (top byte = red), so the screenshot can be saved
///          to BMP/PNG via `Pixels.Save` without a swizzle pass.
///          Returns NULL on size = 0 or alloc failure.
void *rt_canvas3d_screenshot(void *obj) {
    rt_canvas3d *c = rt_canvas3d_checked_or_stack(obj);
    if (!c)
        return NULL;

    const int32_t shot_w = c->render_target ? c->render_target->width : c->width;
    const int32_t shot_h = c->render_target ? c->render_target->height : c->height;
    int32_t source_w = shot_w;
    int32_t source_h = shot_h;
    if (shot_w <= 0 || shot_h <= 0)
        return NULL;

    void *pixels = rt_pixels_new((int64_t)shot_w, (int64_t)shot_h);
    if (!pixels)
        return NULL;
    canvas3d_pixels_view_t *pv = (canvas3d_pixels_view_t *)pixels;

    if (c->render_target && vgfx3d_rendertarget_ensure_color(c->render_target)) {
        if (!vgfx3d_rendertarget_sync_color_if_needed(c->render_target)) {
            if (rt_obj_release_check0(pixels))
                rt_obj_free(pixels);
            return NULL;
        }
        canvas3d_pack_rgba_to_pixels(pv,
                                     c->render_target->color_buf,
                                     shot_w,
                                     shot_h,
                                     c->render_target->stride,
                                     shot_w,
                                     shot_h);
        return pixels;
    }

    if (c->framebuffer_width > 0 && c->framebuffer_height > 0) {
        source_w = c->framebuffer_width;
        source_h = c->framebuffer_height;
    }

    if (c->backend && c->backend != &vgfx3d_software_backend && c->backend->readback_rgba) {
        const size_t row_bytes = (size_t)source_w * 4u;
        uint8_t *rgba;
        if ((size_t)source_w > SIZE_MAX / 4u || (size_t)source_h > SIZE_MAX / row_bytes) {
            if (rt_obj_release_check0(pixels))
                rt_obj_free(pixels);
            return NULL;
        }
        rgba = (uint8_t *)malloc((size_t)source_h * row_bytes);
        if (rgba && c->backend->readback_rgba(
                        c->backend_ctx, rgba, source_w, source_h, (int32_t)row_bytes)) {
            canvas3d_pack_rgba_to_pixels(
                pv, rgba, source_w, source_h, (int32_t)row_bytes, shot_w, shot_h);
            free(rgba);
            return pixels;
        }
        free(rgba);
    }

    if (c->gfx_win) {
        vgfx_framebuffer_t fb;
        if (!vgfx_get_framebuffer(c->gfx_win, &fb) || !fb.pixels || fb.width <= 0 ||
            fb.height <= 0 || fb.stride < fb.width * 4) {
            if (rt_obj_release_check0(pixels))
                rt_obj_free(pixels);
            return NULL;
        }
        canvas3d_pack_rgba_to_pixels(pv, fb.pixels, fb.width, fb.height, fb.stride, shot_w, shot_h);
    } else {
        if (rt_obj_release_check0(pixels))
            rt_obj_free(pixels);
        return NULL;
    }
    return pixels;
}

/// @brief Draw an axis-aligned bounding box as 12 wireframe edges from raw min/max corner arrays.
void rt_canvas3d_draw_aabb_wire_raw(void *obj,
                                    const double *min_v,
                                    const double *max_v,
                                    int64_t color) {
    if (!obj || !min_v || !max_v)
        return;
    double mn[3] = {min_v[0], min_v[1], min_v[2]};
    double mx[3] = {max_v[0], max_v[1], max_v[2]};
    double corners[8][3];
    for (int i = 0; i < 8; i++) {
        corners[i][0] = (i & 1) ? mx[0] : mn[0];
        corners[i][1] = (i & 2) ? mx[1] : mn[1];
        corners[i][2] = (i & 4) ? mx[2] : mn[2];
    }

    static const int edges[12][2] = {{0, 1},
                                     {1, 3},
                                     {3, 2},
                                     {2, 0},
                                     {4, 5},
                                     {5, 7},
                                     {7, 6},
                                     {6, 4},
                                     {0, 4},
                                     {1, 5},
                                     {2, 6},
                                     {3, 7}};
    for (int e = 0; e < 12; e++)
        rt_canvas3d_draw_line3d_raw(obj, corners[edges[e][0]], corners[edges[e][1]], color);
}

/// @brief Draw an axis-aligned bounding box (12 lines) between `min_v` and `max_v` Vec3s.
/// Useful for collision/culling debug visualization.
void rt_canvas3d_draw_aabb_wire(void *obj, void *min_v, void *max_v, int64_t color) {
    double mn[3];
    double mx[3];
    if (!min_v || !max_v)
        return;
    mn[0] = rt_vec3_x(min_v);
    mn[1] = rt_vec3_y(min_v);
    mn[2] = rt_vec3_z(min_v);
    mx[0] = rt_vec3_x(max_v);
    mx[1] = rt_vec3_y(max_v);
    mx[2] = rt_vec3_z(max_v);
    rt_canvas3d_draw_aabb_wire_raw(obj, mn, mx, color);
}

/// @brief Draw three orthogonal great circles approximating a sphere (XY, XZ, YZ planes) at
/// `center` with `radius`. Cheaper than tessellating a real sphere for debug viz.
void rt_canvas3d_draw_sphere_wire(void *obj, void *center, double radius, int64_t color) {
    if (!obj || !center || !isfinite(radius) || radius <= 0.0)
        return;
    const double cx = rt_vec3_x(center);
    const double cy = rt_vec3_y(center);
    const double cz = rt_vec3_z(center);
    const int segs = 24;
    const double step = 2.0 * 3.14159265358979323846 / segs;

    for (int i = 0; i < segs; i++) {
        const double a0 = i * step;
        const double a1 = (i + 1) * step;
        const double c0 = cos(a0);
        const double s0 = sin(a0);
        const double c1 = cos(a1);
        const double s1 = sin(a1);

        void *a = rt_vec3_new(cx + c0 * radius, cy + s0 * radius, cz);
        void *b = rt_vec3_new(cx + c1 * radius, cy + s1 * radius, cz);
        rt_canvas3d_draw_line3d(obj, a, b, color);
        canvas3d_release_local(a);
        canvas3d_release_local(b);

        a = rt_vec3_new(cx + c0 * radius, cy, cz + s0 * radius);
        b = rt_vec3_new(cx + c1 * radius, cy, cz + s1 * radius);
        rt_canvas3d_draw_line3d(obj, a, b, color);
        canvas3d_release_local(a);
        canvas3d_release_local(b);

        a = rt_vec3_new(cx, cy + c0 * radius, cz + s0 * radius);
        b = rt_vec3_new(cx, cy + c1 * radius, cz + s1 * radius);
        rt_canvas3d_draw_line3d(obj, a, b, color);
        canvas3d_release_local(a);
        canvas3d_release_local(b);
    }
}

/// @brief Draw a ray from `origin` along `dir` (Vec3, normalized internally) for `length`
/// world units. Useful for visualizing physics raycasts and AI line-of-sight.
void rt_canvas3d_draw_debug_ray(void *obj, void *origin, void *dir, double length, int64_t color) {
    if (!obj || !origin || !dir || !isfinite(length))
        return;
    void *end = rt_vec3_new(rt_vec3_x(origin) + rt_vec3_x(dir) * length,
                            rt_vec3_y(origin) + rt_vec3_y(dir) * length,
                            rt_vec3_z(origin) + rt_vec3_z(dir) * length);
    rt_canvas3d_draw_line3d(obj, origin, end, color);
    canvas3d_release_local(end);
}

/// @brief Draw an XYZ axis gizmo at `origin` with arms of length `scale`. Standard color
/// convention: red=X, green=Y, blue=Z. Useful for visualizing world / object orientation.
void rt_canvas3d_draw_axis(void *obj, void *origin, double scale) {
    if (!obj || !origin || !isfinite(scale))
        return;
    const double ox = rt_vec3_x(origin);
    const double oy = rt_vec3_y(origin);
    const double oz = rt_vec3_z(origin);
    void *end = rt_vec3_new(ox + scale, oy, oz);
    rt_canvas3d_draw_line3d(obj, origin, end, 0xFF0000);
    canvas3d_release_local(end);
    end = rt_vec3_new(ox, oy + scale, oz);
    rt_canvas3d_draw_line3d(obj, origin, end, 0x00FF00);
    canvas3d_release_local(end);
    end = rt_vec3_new(ox, oy, oz + scale);
    rt_canvas3d_draw_line3d(obj, origin, end, 0x0000FF);
    canvas3d_release_local(end);
}

#else
typedef int rt_canvas3d_overlay_disabled_tu_guard;
#endif /* VIPER_ENABLE_GRAPHICS */
