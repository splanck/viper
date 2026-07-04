//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/backend/vgfx3d_backend_sw.c
// Purpose: Software rasterizer backend for Viper.Graphics3D. Implements the
//   vgfx3d_backend_t vtable using CPU-based edge-function rasterization.
//
// Key invariants:
//   - Writes directly to vgfx framebuffer (uint8_t RGBA)
//   - Z-buffer is float[width*height], cleared to FLT_MAX
//   - CCW winding is front-facing
//   - Sutherland-Hodgman frustum clipping in 4D clip space
//   - Blinn-Phong per-vertex lighting (Gouraud shading)
//   - Perspective-correct texture mapping
//   - Shadow pass clips each tri against the shadow frustum before rasterization.
//
// Ownership/Lifetime:
//   - Software backend context is owned by Canvas3D; freed in its destroy_ctx hook.
//   - Z-buffer and vertex scratch are heap allocations resized on demand.
//   - The context owns one persistent worker pool, created once and shut down
//     before the context storage is released.
//
// Links: vgfx3d_backend.h, rt_canvas3d_internal.h, src/runtime/threads/rt_threadpool.h
//
//===----------------------------------------------------------------------===//

#ifdef VIPER_ENABLE_GRAPHICS

#include "rt_canvas3d.h"
#include "rt_canvas3d_internal.h"
#include "rt_object.h"
#include "rt_parallel.h"
#include "rt_platform.h"
#include "rt_threadpool.h"
#include "vgfx3d_backend.h"
#include "vgfx3d_backend_utils.h"

#include <errno.h>
#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SW_TILE_SIZE 64
#define SW_MAX_WORKERS 8
#define SW_MAX_TASKS (SW_MAX_WORKERS + 1)
#define SW_PARALLEL_MIN_INDICES 192u

/// @brief Submit a typed task callback to the runtime thread pool.
/// @details The public Pool API accepts callbacks through the generic runtime
///          object-call surface. The software renderer uses this internal entry
///          point so C function pointers are passed with their real type.
/// @param pool Pool object pointer.
/// @param callback Task function to execute.
/// @param arg Argument passed to @p callback.
/// @return 1 when queued, 0 when the pool is invalid or shutting down.
extern int8_t rt_threadpool_submit_fn(void *pool, void (*callback)(void *), void *arg);

/*==========================================================================
 * Software backend context
 *=========================================================================*/

typedef struct pipe_vert pipe_vert_t;

typedef struct {
    float *zbuf;
    pipe_vert_t *vertex_scratch;
    uint32_t vertex_scratch_capacity;
    int32_t width, height;
    float vp[16]; /* view * projection (float, row-major) */
    float cam_pos[3];
    float cam_forward[3];
    int8_t cam_is_ortho;
    /* Render target override (NULL = render to window framebuffer) */
    vgfx3d_rendertarget_t *render_target;
    /* Fog parameters (copied from Canvas3D each begin_frame) */
    int8_t fog_enabled;
    float fog_near, fog_far;
    float fog_color[3];
    /* Image-based lighting (copied from Canvas3D each begin_frame) */
    int8_t ibl_enabled;
    float ibl_intensity;
    float ibl_sh[27];
    /* Shadow mapping state */
    float *shadow_depth[VGFX3D_MAX_SHADOW_LIGHTS];
    int32_t shadow_w[VGFX3D_MAX_SHADOW_LIGHTS], shadow_h[VGFX3D_MAX_SHADOW_LIGHTS];
    float shadow_vp[VGFX3D_MAX_SHADOW_LIGHTS][16];
    float shadow_bias;
    /* Plan 06: per-frame shadow filtering params from camera params. */
    float shadow_strength;
    float shadow_slope_bias;
    int32_t shadow_quality;
    int8_t shadow_pass_slot;
    int8_t shadow_count;
    int8_t shadow_complete[VGFX3D_MAX_SHADOW_LIGHTS];
    /* Persistent worker pool for deterministic tiled rasterization. */
    void *worker_pool;
    int64_t worker_count;
    int32_t debug_draw_count;
    int32_t debug_tri_count;
} sw_context_t;

static inline void sw_compute_view_vector(const sw_context_t *ctx,
                                          float wx,
                                          float wy,
                                          float wz,
                                          float *out_vx,
                                          float *out_vy,
                                          float *out_vz);

/// @brief Compute an overflow-resistant Euclidean length for a 3-vector.
/// @details Scales components by their maximum absolute lane before squaring,
///          so very large finite vectors do not overflow to infinity during
///          normalization, fog, or light attenuation calculations.
/// @param x X component.
/// @param y Y component.
/// @param z Z component.
/// @return Vector length, or INFINITY when inputs are non-finite/overflowed.
static float sw_length3(float x, float y, float z) {
    if (!isfinite(x) || !isfinite(y) || !isfinite(z))
        return INFINITY;
    float ax = fabsf(x);
    float ay = fabsf(y);
    float az = fabsf(z);
    float m = ax;
    if (ay > m)
        m = ay;
    if (az > m)
        m = az;
    if (m <= 0.0f)
        return 0.0f;
    x /= m;
    y /= m;
    z /= m;
    return m * sqrtf(x * x + y * y + z * z);
}

/// @brief Fill a software depth buffer with a constant depth value.
/// @details Uses a small unrolled loop for the hot clear paths shared by the
///          main framebuffer, render targets, and shadow maps. Keeping this in
///          one helper also makes future platform-specific vectorization local.
/// @param depth Depth-buffer pointer; NULL is ignored.
/// @param count Number of float entries to write.
/// @param value Depth value to store in every entry.
static void sw_fill_depth_buffer(float *depth, size_t count, float value) {
    if (!depth)
        return;
    size_t i = 0;
    for (; i + 4u <= count; i += 4u) {
        depth[i + 0u] = value;
        depth[i + 1u] = value;
        depth[i + 2u] = value;
        depth[i + 3u] = value;
    }
    for (; i < count; i++)
        depth[i] = value;
}

/// @brief Normalize the vector (*x,*y,*z) in place; on non-finite or near-zero length,
///   substitute the fallback vector and return 0, else return 1.
static int sw_normalize3(
    float *x, float *y, float *z, float fallback_x, float fallback_y, float fallback_z) {
    float len;
    if (!x || !y || !z || !isfinite(*x) || !isfinite(*y) || !isfinite(*z)) {
        if (x)
            *x = fallback_x;
        if (y)
            *y = fallback_y;
        if (z)
            *z = fallback_z;
        return 0;
    }
    len = sw_length3(*x, *y, *z);
    if (!isfinite(len) || len <= 1e-7f) {
        *x = fallback_x;
        *y = fallback_y;
        *z = fallback_z;
        return 0;
    }
    *x /= len;
    *y /= len;
    *z /= len;
    return 1;
}

/// @brief Recount the highest complete shadow slot and cache the scan bound.
/// @details Shadow slots can be sparse while lights are reconfigured. Counting
///          through the highest valid slot lets later valid slots remain visible
///          even when an earlier slot is empty; invalid gaps simply sample as lit.
static void sw_recompute_shadow_count(sw_context_t *ctx) {
    int8_t count = 0;
    if (!ctx)
        return;
    for (int slot = 0; slot < VGFX3D_MAX_SHADOW_LIGHTS; slot++) {
        if (ctx->shadow_complete[slot] && ctx->shadow_depth[slot] && ctx->shadow_w[slot] > 0 &&
            ctx->shadow_h[slot] > 0)
            count = (int8_t)(slot + 1);
    }
    ctx->shadow_count = count;
}

/* Plan 06: 16-point Poisson disk (unit-disk offsets), deterministic dart-throwing
 * (64-bit LCG, seed 80, min separation 0.4369) — the same table as the GPU shader
 * twins. Tap subsets stride the table so smaller tiers stay well-distributed. */
static const float sw_shadow_poisson[16][2] = {
    {0.482408f, 0.345286f},
    {0.079872f, 0.641222f},
    {-0.211339f, -0.305133f},
    {0.485624f, -0.638176f},
    {-0.197270f, 0.252333f},
    {-0.823845f, 0.551135f},
    {-0.333578f, 0.886038f},
    {0.835697f, 0.028104f},
    {-0.921729f, -0.289255f},
    {-0.184344f, -0.769094f},
    {0.449912f, -0.195238f},
    {0.534685f, 0.795828f},
    {-0.661694f, -0.644587f},
    {-0.599728f, 0.062200f},
    {0.899330f, -0.413558f},
    {0.200012f, -0.976846f},
};

/// @brief Sample the shadow map for @p slot at a world-space position and return a visibility
/// factor.
/// @details Plan 06 sampling model: the receiver is normal-offset ~1.5 shadow texels (world
///          units, derived from the shadow VP row lengths), the compare bias adds the map-gradient
///          slope term scaled by the canvas slope-bias knob, and filtering uses a rotated-Poisson
///          PCF (4 taps at PERFORMANCE, 8 at BALANCED/CINEMATIC — CPU-cost bounded) rotated by
///          interleaved-gradient noise over the shadow-map texel coordinate. Fully-occluded taps
///          contribute `1 - shadow_strength` so `ShadowStrength = 1` yields black shadows.
/// @return Visibility in [1 - strength, 1]: 1.0 = fully lit. Returns 1.0 (fully lit) when the
///         slot is invalid, outside the shadow frustum, or the context is null.
static float sw_sample_shadow_visibility(const sw_context_t *ctx,
                                         int32_t slot,
                                         float wx,
                                         float wy,
                                         float wz,
                                         float nx,
                                         float ny,
                                         float nz) {
    const float *svp;
    float lx;
    float ly;
    float lz;
    float lw;
    float su;
    float sv;
    float sd;
    int32_t shadow_w;
    int32_t shadow_h;
    int sxi;
    int syi;
    float sz_map;
    float slope_bias;
    float dz_du = 0.0f;
    float dz_dv = 0.0f;
    float slope;
    float occluded_vis;
    float texel_world = 0.0f;
    float row0_len;
    float visibility_sum = 0.0f;
    int sample_count = 0;
    int taps;
    float ign;
    float ca;
    float sa;

    if (!ctx || slot < 0 || slot >= ctx->shadow_count || slot >= VGFX3D_MAX_SHADOW_LIGHTS ||
        !ctx->shadow_complete[slot] || !ctx->shadow_depth[slot] || ctx->shadow_w[slot] <= 0 ||
        ctx->shadow_h[slot] <= 0)
        return 1.0f;

    shadow_w = ctx->shadow_w[slot];
    shadow_h = ctx->shadow_h[slot];
    svp = ctx->shadow_vp[slot];

    /* Normal-offset the receiver by ~1.5 shadow texels in world units (row 0 of the
     * row-major VP maps world X-extent onto clip x in [-1, 1]). */
    row0_len = sqrtf(svp[0] * svp[0] + svp[1] * svp[1] + svp[2] * svp[2]);
    if (isfinite(row0_len) && row0_len > 1e-9f)
        texel_world = 2.0f / (row0_len * (float)shadow_w);
    wx += nx * texel_world * 1.5f;
    wy += ny * texel_world * 1.5f;
    wz += nz * texel_world * 1.5f;

    lx = wx * svp[0] + wy * svp[1] + wz * svp[2] + svp[3];
    ly = wx * svp[4] + wy * svp[5] + wz * svp[6] + svp[7];
    lz = wx * svp[8] + wy * svp[9] + wz * svp[10] + svp[11];
    lw = wx * svp[12] + wy * svp[13] + wz * svp[14] + svp[15];
    if (lw <= 1e-7f || !isfinite(lw))
        return 1.0f;

    su = (lx / lw) * 0.5f + 0.5f;
    sv = (1.0f - ly / lw) * 0.5f;
    sd = (lz / lw) * 0.5f + 0.5f;
    if (!isfinite(su) || !isfinite(sv) || !isfinite(sd) || su < 0.0f || su >= 1.0f || sv < 0.0f ||
        sv >= 1.0f || sd < 0.0f || sd > 1.0f)
        return 1.0f;

    sxi = (int)(su * (float)shadow_w);
    syi = (int)(sv * (float)shadow_h);
    if (sxi < 0 || sxi >= shadow_w || syi < 0 || syi >= shadow_h)
        return 1.0f;

    sz_map = ctx->shadow_depth[slot][syi * shadow_w + sxi];
    slope_bias = ctx->shadow_bias;
    if (!isfinite(sz_map) || sz_map > FLT_MAX * 0.5f)
        sz_map = 1.0f;
    if (sxi + 1 < shadow_w) {
        float neighbor = ctx->shadow_depth[slot][syi * shadow_w + sxi + 1];
        if (isfinite(neighbor) && neighbor < FLT_MAX * 0.5f)
            dz_du = neighbor - sz_map;
    }
    if (syi + 1 < shadow_h) {
        float neighbor = ctx->shadow_depth[slot][(syi + 1) * shadow_w + sxi];
        if (isfinite(neighbor) && neighbor < FLT_MAX * 0.5f)
            dz_dv = neighbor - sz_map;
    }
    slope = sqrtf(dz_du * dz_du + dz_dv * dz_dv);
    slope_bias += ctx->shadow_bias * slope * 4.0f * ctx->shadow_slope_bias;

    occluded_vis = 1.0f - (isfinite(ctx->shadow_strength)
                               ? (ctx->shadow_strength < 0.0f
                                      ? 0.0f
                                      : (ctx->shadow_strength > 1.0f ? 1.0f
                                                                     : ctx->shadow_strength))
                               : 0.85f);
    taps = ctx->shadow_quality <= 0 ? 4 : 8; /* CPU cost bound: BALANCED cap */
    /* Interleaved-gradient noise over the map texel coordinate (fract emulation). */
    {
        double t = 0.06711056 * (double)sxi + 0.00583715 * (double)syi;
        t -= (double)(int64_t)t;
        t *= 52.9829189;
        t -= (double)(int64_t)t;
        ign = (float)t;
    }
    ca = cosf(6.2831853f * ign);
    sa = sinf(6.2831853f * ign);

    for (int i = 0; i < taps; i++) {
        const float *p = sw_shadow_poisson[i * (16 / taps)];
        float ox = (p[0] * ca - p[1] * sa) * 1.5f;
        float oy = (p[0] * sa + p[1] * ca) * 1.5f;
        int px = sxi + (int)(ox >= 0.0f ? ox + 0.5f : ox - 0.5f);
        int py = syi + (int)(oy >= 0.0f ? oy + 0.5f : oy - 0.5f);
        float sample_depth;
        if (px < 0 || px >= shadow_w || py < 0 || py >= shadow_h) {
            visibility_sum += 1.0f;
            sample_count++;
            continue;
        }
        sample_depth = ctx->shadow_depth[slot][py * shadow_w + px];
        if (!isfinite(sample_depth) || sample_depth > FLT_MAX * 0.5f) {
            visibility_sum += 1.0f;
            sample_count++;
            continue;
        }
        visibility_sum += (sd > sample_depth + slope_bias) ? occluded_vis : 1.0f;
        sample_count++;
    }

    return sample_count > 0 ? visibility_sum / (float)sample_count : 1.0f;
}

/// @brief Resolve the concrete shadow-map slot for a light at the given world position.
static int32_t sw_resolve_shadow_slot(
    const sw_context_t *ctx, const vgfx3d_light_params_t *light, float wx, float wy, float wz) {
    int32_t base_slot;
    int32_t cascade_count;
    float view_depth;

    if (!ctx || !light || light->shadow_index < 0)
        return -1;
    base_slot = light->shadow_index;
    cascade_count = light->shadow_cascade_count;
    if (cascade_count <= 1)
        return base_slot;
    if (cascade_count > VGFX3D_MAX_SHADOW_LIGHTS - base_slot)
        cascade_count = VGFX3D_MAX_SHADOW_LIGHTS - base_slot;
    if (cascade_count > ctx->shadow_count - base_slot)
        cascade_count = ctx->shadow_count - base_slot;
    if (cascade_count <= 1)
        return base_slot;
    view_depth = (wx - ctx->cam_pos[0]) * ctx->cam_forward[0] +
                 (wy - ctx->cam_pos[1]) * ctx->cam_forward[1] +
                 (wz - ctx->cam_pos[2]) * ctx->cam_forward[2];
    for (int32_t cascade = 0; cascade < cascade_count - 1; cascade++) {
        if (view_depth <= light->shadow_cascade_splits[cascade])
            return base_slot + cascade;
    }
    return base_slot + cascade_count - 1;
}

/// @brief Resize the depth buffer if dimensions changed; idempotent on match.
///
/// Reallocates `width × height` floats. Used during resize and on
/// first frame after context creation. Returns 0 on overflow / OOM.
static int sw_ensure_zbuf_capacity(sw_context_t *ctx, int32_t width, int32_t height) {
    if (!ctx || width <= 0 || height <= 0)
        return 0;
    if (ctx->zbuf && ctx->width == width && ctx->height == height)
        return 1;

    if ((size_t)width > SIZE_MAX / (size_t)height)
        return 0;
    size_t pixel_count = (size_t)width * (size_t)height;
    if (pixel_count > SIZE_MAX / sizeof(float))
        return 0;

    float *new_zbuf = (float *)realloc(ctx->zbuf, pixel_count * sizeof(float));
    if (!new_zbuf)
        return 0;

    ctx->zbuf = new_zbuf;
    ctx->width = width;
    ctx->height = height;
    return 1;
}

/// @brief Clamp a software-rasterizer worker count to [1, SW_MAX_WORKERS].
static int64_t sw_clamp_worker_count(int64_t count) {
    if (count < 1)
        return 1;
    if (count > SW_MAX_WORKERS)
        return SW_MAX_WORKERS;
    return count;
}

/// @brief Default software-rasterizer worker count: hardware parallelism capped to 8.
static int64_t sw_default_worker_count(void) {
    return sw_clamp_worker_count(rt_parallel_default_workers());
}

/// @brief Resolve VIPER_3D_SW_THREADS into a deterministic worker budget.
static int64_t sw_resolve_worker_count_from_env(void) {
    const char *env = getenv("VIPER_3D_SW_THREADS");
    char *end = NULL;
    long long parsed;
    if (!env || !*env)
        return sw_default_worker_count();
    errno = 0;
    parsed = strtoll(env, &end, 10);
    if (errno == ERANGE || end == env || (end && *end != '\0'))
        return sw_default_worker_count();
    return sw_clamp_worker_count((int64_t)parsed);
}

/// @brief Create the context-owned worker pool unless the requested count is one.
static void sw_init_worker_pool(sw_context_t *ctx) {
    if (!ctx)
        return;
    ctx->worker_count = sw_resolve_worker_count_from_env();
    ctx->worker_pool = NULL;
    if (ctx->worker_count <= 1)
        return;
    ctx->worker_pool = rt_threadpool_new(ctx->worker_count);
    if (!ctx->worker_pool)
        ctx->worker_count = 1;
}

/// @brief Shut down and release the context-owned worker pool.
static void sw_release_worker_pool(sw_context_t *ctx) {
    if (!ctx || !ctx->worker_pool)
        return;
    void *pool = ctx->worker_pool;
    ctx->worker_pool = NULL;
    ctx->worker_count = 1;
    rt_threadpool_shutdown(pool);
    if (rt_obj_release_check0(pool))
        rt_obj_free(pool);
}

/// @brief Test-only probe for unit tests; not part of the script-facing API.
int64_t vgfx3d_software_backend_thread_count_for_test(const void *ctx_ptr) {
    const sw_context_t *ctx = (const sw_context_t *)ctx_ptr;
    return ctx ? sw_clamp_worker_count(ctx->worker_count) : 1;
}

/*==========================================================================
 * Matrix helpers
 *=========================================================================*/

/// @brief Row-major 4×4 matrix multiply: `out = a * b`.
static void mat4f_mul(const float *a, const float *b, float *out) {
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
            out[r * 4 + c] = a[r * 4 + 0] * b[0 * 4 + c] + a[r * 4 + 1] * b[1 * 4 + c] +
                             a[r * 4 + 2] * b[2 * 4 + c] + a[r * 4 + 3] * b[3 * 4 + c];
}

/// @brief Transform a homogeneous 4-vector by a row-major 4×4 matrix.
static void mat4f_transform4(const float *m, const float *in, float *out) {
    out[0] = m[0] * in[0] + m[1] * in[1] + m[2] * in[2] + m[3] * in[3];
    out[1] = m[4] * in[0] + m[5] * in[1] + m[6] * in[2] + m[7] * in[3];
    out[2] = m[8] * in[0] + m[9] * in[1] + m[10] * in[2] + m[11] * in[3];
    out[3] = m[12] * in[0] + m[13] * in[1] + m[14] * in[2] + m[15] * in[3];
}

/// @brief Compute a positive width*height pixel count without overflowing size_t.
/// @details Software backend render targets use signed 32-bit dimensions, while
///          depth/color buffer loops use `size_t`. This helper rejects invalid or
///          overflowing dimensions before callers clear linear buffers.
static int sw_pixel_count_checked(int32_t width, int32_t height, size_t *out_count) {
    if (out_count)
        *out_count = 0;
    if (!out_count || width <= 0 || height <= 0)
        return 0;
    if ((size_t)width > SIZE_MAX / (size_t)height)
        return 0;
    *out_count = (size_t)width * (size_t)height;
    return 1;
}

// clang-format off
/* These implementation fragments have type/function dependencies. */
#include "vgfx3d_backend_sw_texture.inc"
#include "vgfx3d_backend_sw_vertex.inc"
#include "vgfx3d_backend_sw_raster.inc"
#include "vgfx3d_backend_sw_shadow.inc"
#include "vgfx3d_backend_sw_vtable.inc"
// clang-format on
/*==========================================================================
 * Exported backend + selection
 *=========================================================================*/

const vgfx3d_backend_t vgfx3d_software_backend = {
    .name = "software",
    .create_ctx = sw_create_ctx,
    .destroy_ctx = sw_destroy_ctx,
    .clear = sw_clear,
    .resize = sw_resize,
    .begin_frame = sw_begin_frame,
    .submit_draw = sw_submit_draw,
    .end_frame = sw_end_frame,
    .set_render_target = sw_set_render_target,
    .shadow_begin = sw_shadow_begin,
    .shadow_draw = sw_shadow_draw,
    .shadow_end = sw_shadow_end,
    .present = NULL, /* software renders to CPU framebuffer; vgfx_update handles display */
    .show_gpu_layer = NULL,
    .hide_gpu_layer = NULL,
};

static const vgfx3d_backend_t *vgfx3d_backend_from_name(const char *name) {
    if (!name)
        return NULL;
    if (strcmp(name, "software") == 0)
        return &vgfx3d_software_backend;
#if RT_PLATFORM_MACOS
    if (strcmp(name, "metal") == 0)
        return &vgfx3d_metal_backend;
#elif RT_PLATFORM_WINDOWS
    if (strcmp(name, "d3d11") == 0)
        return &vgfx3d_d3d11_backend;
#elif RT_PLATFORM_LINUX
    if (strcmp(name, "opengl") == 0)
        return &vgfx3d_opengl_backend;
#endif
    return NULL;
}

/// @brief Public: pick the best 3D backend at startup.
///
/// Tries each compiled-in backend in priority order (D3D11 on
/// Windows, Metal on macOS, OpenGL on Linux), falling back to the
/// software backend when no GPU backend is available. Called once
/// at canvas-create time and the choice is cached.
const vgfx3d_backend_t *vgfx3d_select_backend(void) {
    vgfx3d_backend_platform_t platform = VGFX3D_BACKEND_PLATFORM_OTHER;
    const vgfx3d_backend_t *backend;

    /* Only honor overrides for backends compiled on this platform. */
    const char *env = getenv("VIPER_3D_BACKEND");
    if (env) {
        backend = vgfx3d_backend_from_name(env);
        if (backend)
            return backend;
    }

    /* Prefer platform GPU backends by default; Canvas3D falls back to the
     * software backend at context creation when the GPU path is unavailable. */
#if RT_PLATFORM_MACOS
    platform = VGFX3D_BACKEND_PLATFORM_MACOS;
#elif RT_PLATFORM_WINDOWS
#if defined(_M_ARM64) || defined(__aarch64__)
    /* Several Windows-on-ARM GPU stacks expose D3D11 but crash inside the
     * display driver during Present. Keep x64 on D3D11, but default ARM64 to
     * the portable backend so Canvas3D demos launch reliably. Users can still
     * opt into D3D11 with VIPER_3D_BACKEND=d3d11. */
    platform = VGFX3D_BACKEND_PLATFORM_WINDOWS_ARM64;
#else
    platform = VGFX3D_BACKEND_PLATFORM_WINDOWS;
#endif
#elif RT_PLATFORM_LINUX
    platform = VGFX3D_BACKEND_PLATFORM_LINUX;
#endif

    backend = vgfx3d_backend_from_name(vgfx3d_default_backend_name_for_platform(platform));
    return backend ? backend : &vgfx3d_software_backend;
}

#else
typedef int rt_graphics_disabled_tu_guard;
#endif /* VIPER_ENABLE_GRAPHICS */
