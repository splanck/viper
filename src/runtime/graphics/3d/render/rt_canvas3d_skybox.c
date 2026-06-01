//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/render/rt_canvas3d_skybox.c
// Purpose: Canvas3D CPU skybox fallback — rasterize the bound cubemap into a
//   pixel buffer (per-pixel inverse-VP ray for perspective, single-color fill
//   for ortho) with a per-frame cache keyed by viewport/camera/cubemap
//   generation. Used when the backend can't render the skybox directly (e.g.
//   the software renderer). Split out of rt_canvas3d.c.
// Key invariants:
//   - The cache is valid only while viewport, cubemap generation, projection
//     mode, and VP/camera (or forward, for ortho) are unchanged.
//   - OOM on cache (re)allocation invalidates the cache rather than leaving a
//     half-valid state.
// Links: rt_canvas3d_internal.h
//
//===----------------------------------------------------------------------===//

#ifdef VIPER_ENABLE_GRAPHICS

#include "rt_canvas3d_internal.h"
#include "vgfx3d_backend_utils.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

void rt_canvas3d_invalidate_skybox_cache(rt_canvas3d *c) {
    if (!c)
        return;
    free(c->skybox_cpu_cache);
    c->skybox_cpu_cache = NULL;
    c->skybox_cpu_cache_w = 0;
    c->skybox_cpu_cache_h = 0;
    c->skybox_cpu_cache_generation = 0;
    c->skybox_cpu_cache_is_ortho = 0;
    memset(c->skybox_cpu_cache_vp, 0, sizeof(c->skybox_cpu_cache_vp));
    memset(c->skybox_cpu_cache_cam_pos, 0, sizeof(c->skybox_cpu_cache_cam_pos));
    memset(c->skybox_cpu_cache_forward, 0, sizeof(c->skybox_cpu_cache_forward));
}

/// @brief True if two float arrays match element-wise within @p eps; any non-finite
///   element or NULL/negative input fails the comparison.
static int canvas3d_float_array_close(const float *a, const float *b, int32_t count, float eps) {
    if (!a || !b || count < 0)
        return 0;
    for (int32_t i = 0; i < count; i++) {
        if (!isfinite(a[i]) || !isfinite(b[i]))
            return 0;
        if (fabsf(a[i] - b[i]) > eps)
            return 0;
    }
    return 1;
}

/// @brief CPU-rasterize the bound skybox cubemap into a destination pixel buffer.
/// @details For perspective cameras, unprojects each destination pixel from NDC
///   back to a world-space direction by multiplying through `inverse(VP)`, then
///   samples the cubemap along that ray. Orthographic cameras collapse to a
///   single-color fill along the camera forward direction because an ortho
///   projection has no per-pixel ray divergence — all pixels see the same
///   skybox direction. This is the slow fallback path used when the GPU
///   backend can't render the skybox directly (e.g. the software renderer).
/// @return 1 on success, 0 when the VP matrix is non-invertible or inputs
///   are otherwise malformed.
static int canvas3d_render_skybox_cpu(
    rt_canvas3d *c, uint8_t *dst_pixels, int32_t dst_w, int32_t dst_h, int32_t dst_stride) {
    if (!c || !c->skybox || !dst_pixels || !canvas3d_rgba8_stride_valid(dst_w, dst_h, dst_stride))
        return 0;

    if (c->cached_cam_is_ortho) {
        float r;
        float g;
        float b;

        rt_cubemap_sample(c->skybox,
                          c->cached_cam_forward[0],
                          c->cached_cam_forward[1],
                          c->cached_cam_forward[2],
                          &r,
                          &g,
                          &b);
        uint8_t r8 = canvas3d_clamp01_to_u8(r);
        uint8_t g8 = canvas3d_clamp01_to_u8(g);
        uint8_t b8 = canvas3d_clamp01_to_u8(b);
        size_t row_bytes = (size_t)dst_w * 4u;
        uint8_t *first_row = dst_pixels;
        for (int32_t x = 0; x < dst_w; x++) {
            uint8_t *dst = &first_row[(size_t)x * 4u];
            dst[0] = r8;
            dst[1] = g8;
            dst[2] = b8;
            dst[3] = 0xFF;
        }
        for (int32_t y = 1; y < dst_h; y++) {
            uint8_t *dst = &dst_pixels[(size_t)y * (size_t)dst_stride];
            memcpy(dst, first_row, row_bytes);
        }
        return 1;
    }

    float inv_vp[16];
    if (vgfx3d_invert_matrix4(c->cached_vp, inv_vp) != 0)
        return 0;

    float inv_w = 1.0f / (float)dst_w;
    float inv_h = 1.0f / (float)dst_h;
    for (int32_t y = 0; y < dst_h; y++) {
        float ndc_y = 1.0f - 2.0f * ((float)y + 0.5f) * inv_h;
        uint8_t *row = &dst_pixels[(size_t)y * (size_t)dst_stride];
        for (int32_t x = 0; x < dst_w; x++) {
            float ndc_x = 2.0f * ((float)x + 0.5f) * inv_w - 1.0f;
            float clip[4] = {ndc_x, ndc_y, 1.0f, 1.0f};
            float world[4];
            float dx;
            float dy;
            float dz;
            float dl;
            float r;
            float g;
            float b;
            uint8_t *dst;

            world[0] = inv_vp[0] * clip[0] + inv_vp[1] * clip[1] + inv_vp[2] * clip[2] +
                       inv_vp[3] * clip[3];
            world[1] = inv_vp[4] * clip[0] + inv_vp[5] * clip[1] + inv_vp[6] * clip[2] +
                       inv_vp[7] * clip[3];
            world[2] = inv_vp[8] * clip[0] + inv_vp[9] * clip[1] + inv_vp[10] * clip[2] +
                       inv_vp[11] * clip[3];
            world[3] = inv_vp[12] * clip[0] + inv_vp[13] * clip[1] + inv_vp[14] * clip[2] +
                       inv_vp[15] * clip[3];
            if (fabsf(world[3]) > 1e-7f) {
                world[0] /= world[3];
                world[1] /= world[3];
                world[2] /= world[3];
            }
            dx = world[0] - c->cached_cam_pos[0];
            dy = world[1] - c->cached_cam_pos[1];
            dz = world[2] - c->cached_cam_pos[2];
            dl = sqrtf(dx * dx + dy * dy + dz * dz);
            if (dl > 1e-7f) {
                dx /= dl;
                dy /= dl;
                dz /= dl;
            }
            rt_cubemap_sample(c->skybox, dx, dy, dz, &r, &g, &b);
            dst = &row[(size_t)x * 4u];
            dst[0] = canvas3d_clamp01_to_u8(r);
            dst[1] = canvas3d_clamp01_to_u8(g);
            dst[2] = canvas3d_clamp01_to_u8(b);
            dst[3] = 0xFF;
        }
    }
    return 1;
}

/// @brief Check whether the cached CPU skybox can satisfy the current frame.
/// @details Composite key: viewport (w, h), cubemap content generation
///   (bumped whenever a face is uploaded), projection mode (ortho vs
///   perspective), and then either the camera forward vector (ortho) or the
///   full VP matrix + camera position (perspective). The split on projection
///   mode exists because ortho cameras fill the entire target with one
///   sampled direction, so only the forward vector needs to match — the VP
///   matrix can drift without affecting the rendered skybox.
/// @return 1 if the cache is valid for this frame, 0 if it must be re-rendered.
static int canvas3d_skybox_cache_matches(const rt_canvas3d *c,
                                         int32_t w,
                                         int32_t h,
                                         uint64_t generation) {
    if (!c || !c->skybox_cpu_cache || w <= 0 || h <= 0)
        return 0;
    if (c->skybox_cpu_cache_w != w || c->skybox_cpu_cache_h != h ||
        c->skybox_cpu_cache_generation != generation ||
        c->skybox_cpu_cache_is_ortho != c->cached_cam_is_ortho)
        return 0;
    if (c->cached_cam_is_ortho)
        return canvas3d_float_array_close(
            c->skybox_cpu_cache_forward, c->cached_cam_forward, 3, 1e-6f);
    return canvas3d_float_array_close(c->skybox_cpu_cache_vp, c->cached_vp, 16, 1e-6f) &&
           canvas3d_float_array_close(c->skybox_cpu_cache_cam_pos, c->cached_cam_pos, 3, 1e-6f);
}

/// @brief Populate (or refresh) the CPU skybox cache so it's ready for blitting.
/// @details If the cache already matches the current viewport/camera/generation
///   we return immediately (cheap fast path). Otherwise we (re)allocate the
///   backing buffer only when its pixel dimensions actually change, then call
///   `canvas3d_render_skybox_cpu` to paint the fresh image and snapshot the
///   cache key. OOM on reallocation invalidates the cache rather than leaving
///   it in a half-valid state. Also guards against `w * h * 4` overflow on
///   32-bit size_t targets.
/// @return 1 when a usable cache is ready, 0 on failure (caller should skip
///   the skybox for this frame rather than render garbage).
int canvas3d_ensure_skybox_cpu_cache(rt_canvas3d *c, int32_t w, int32_t h) {
    uint64_t generation;
    size_t bytes;

    if (!c || !c->skybox || w <= 0 || h <= 0 || (int64_t)w > INT32_MAX / 4)
        return 0;
    generation = vgfx3d_get_cubemap_generation(c->skybox);
    if (canvas3d_skybox_cache_matches(c, w, h, generation))
        return 1;
    bytes = (size_t)w * (size_t)h * 4u;
    if (bytes / 4u / (size_t)w != (size_t)h)
        return 0;
    if (c->skybox_cpu_cache_w != w || c->skybox_cpu_cache_h != h || !c->skybox_cpu_cache) {
        uint8_t *new_cache = (uint8_t *)realloc(c->skybox_cpu_cache, bytes);
        if (!new_cache) {
            rt_canvas3d_invalidate_skybox_cache(c);
            return 0;
        }
        c->skybox_cpu_cache = new_cache;
    }
    c->skybox_cpu_cache_w = w;
    c->skybox_cpu_cache_h = h;
    if (!canvas3d_render_skybox_cpu(c, c->skybox_cpu_cache, w, h, (int32_t)((int64_t)w * 4))) {
        rt_canvas3d_invalidate_skybox_cache(c);
        return 0;
    }
    c->skybox_cpu_cache_generation = generation;
    c->skybox_cpu_cache_is_ortho = c->cached_cam_is_ortho;
    memcpy(c->skybox_cpu_cache_vp, c->cached_vp, sizeof(c->skybox_cpu_cache_vp));
    memcpy(c->skybox_cpu_cache_cam_pos, c->cached_cam_pos, sizeof(c->skybox_cpu_cache_cam_pos));
    memcpy(c->skybox_cpu_cache_forward, c->cached_cam_forward, sizeof(c->skybox_cpu_cache_forward));
    return 1;
}

/// @brief Copy the cached CPU skybox into the frame's destination buffer row-by-row.
/// @details Performs no rendering — it assumes `canvas3d_ensure_skybox_cpu_cache`
///   has already refreshed the cache for this frame. Silently no-ops when the
///   cache size doesn't match the destination, which is by design: the caller
///   uses it as a "try the fast path" hook before falling back to a full
///   CPU render.
void canvas3d_blit_skybox_cpu_cache(
    rt_canvas3d *c, uint8_t *dst_pixels, int32_t dst_w, int32_t dst_h, int32_t dst_stride) {
    int32_t src_stride;

    if (!c || !c->skybox_cpu_cache || !dst_pixels || dst_w <= 0 || dst_h <= 0 ||
        c->skybox_cpu_cache_w != dst_w || c->skybox_cpu_cache_h != dst_h ||
        !canvas3d_rgba8_stride_valid(dst_w, dst_h, dst_stride))
        return;
    src_stride = (int32_t)((int64_t)dst_w * 4);
    for (int32_t y = 0; y < dst_h; y++)
        memcpy(
            &dst_pixels[y * dst_stride], &c->skybox_cpu_cache[y * src_stride], (size_t)src_stride);
}

#endif /* VIPER_ENABLE_GRAPHICS */
