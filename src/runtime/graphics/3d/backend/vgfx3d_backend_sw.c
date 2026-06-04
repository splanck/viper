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
//
// Links: vgfx3d_backend.h, rt_canvas3d_internal.h
//
//===----------------------------------------------------------------------===//

#ifdef VIPER_ENABLE_GRAPHICS

#include "rt_canvas3d.h"
#include "rt_canvas3d_internal.h"
#include "vgfx3d_backend.h"
#include "vgfx3d_backend_utils.h"

#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    /* Shadow mapping state */
    float *shadow_depth[VGFX3D_MAX_SHADOW_LIGHTS];
    int32_t shadow_w[VGFX3D_MAX_SHADOW_LIGHTS], shadow_h[VGFX3D_MAX_SHADOW_LIGHTS];
    float shadow_vp[VGFX3D_MAX_SHADOW_LIGHTS][16];
    float shadow_bias;
    int8_t shadow_pass_slot;
    int8_t shadow_count;
    int8_t shadow_complete[VGFX3D_MAX_SHADOW_LIGHTS];
} sw_context_t;

static inline void sw_compute_view_vector(const sw_context_t *ctx,
                                          float wx,
                                          float wy,
                                          float wz,
                                          float *out_vx,
                                          float *out_vy,
                                          float *out_vz);

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
    len = sqrtf((*x) * (*x) + (*y) * (*y) + (*z) * (*z));
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

/// @brief Recount the contiguous run of complete shadow slots (complete flag + valid
///   depth buffer/dimensions), stopping at the first gap, and cache it on the context.
static void sw_recompute_shadow_count(sw_context_t *ctx) {
    int8_t count = 0;
    if (!ctx)
        return;
    for (int slot = 0; slot < VGFX3D_MAX_SHADOW_LIGHTS; slot++) {
        if (!ctx->shadow_complete[slot] || !ctx->shadow_depth[slot] || ctx->shadow_w[slot] <= 0 ||
            ctx->shadow_h[slot] <= 0)
            break;
        count = (int8_t)(slot + 1);
    }
    ctx->shadow_count = count;
}

/// @brief Sample the shadow map for @p slot at a world-space position and return a visibility
/// factor.
/// @details Transforms the world position into shadow NDC via the stored shadow VP matrix, then
///          samples the shadow depth map with a 3×3 percentage-closer filter (PCF) to soften
///          shadow edges. A slope-scaled depth bias (derived from the depth map gradient) is
///          added to sd before comparison to reduce shadow acne on slanted surfaces.
/// @return Visibility in [0, 1]: 1.0 = fully lit, ~0.15 = fully shadowed (with PCF blending).
///         Returns 1.0 (fully lit) when the slot is invalid, outside the shadow frustum, or the
///         context is null.
static float sw_sample_shadow_visibility(
    const sw_context_t *ctx, int32_t slot, float wx, float wy, float wz) {
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
    float visibility_sum = 0.0f;
    int sample_count = 0;

    if (!ctx || slot < 0 || slot >= ctx->shadow_count || slot >= VGFX3D_MAX_SHADOW_LIGHTS ||
        !ctx->shadow_complete[slot] || !ctx->shadow_depth[slot] || ctx->shadow_w[slot] <= 0 ||
        ctx->shadow_h[slot] <= 0)
        return 1.0f;

    svp = ctx->shadow_vp[slot];
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

    shadow_w = ctx->shadow_w[slot];
    shadow_h = ctx->shadow_h[slot];
    sxi = (int)(su * (float)shadow_w);
    syi = (int)(sv * (float)shadow_h);
    if (sxi < 0 || sxi >= shadow_w || syi < 0 || syi >= shadow_h)
        return 1.0f;

    sz_map = ctx->shadow_depth[slot][syi * shadow_w + sxi];
    slope_bias = ctx->shadow_bias;
    if (sxi + 1 < shadow_w)
        dz_du = ctx->shadow_depth[slot][syi * shadow_w + sxi + 1] - sz_map;
    if (syi + 1 < shadow_h)
        dz_dv = ctx->shadow_depth[slot][(syi + 1) * shadow_w + sxi] - sz_map;
    slope = sqrtf(dz_du * dz_du + dz_dv * dz_dv);
    slope_bias += ctx->shadow_bias * slope * 4.0f;

    for (int oy = -1; oy <= 1; oy++) {
        int py = syi + oy;
        if (py < 0 || py >= shadow_h)
            continue;
        for (int ox = -1; ox <= 1; ox++) {
            int px = sxi + ox;
            float sample_depth;
            if (px < 0 || px >= shadow_w)
                continue;
            sample_depth = ctx->shadow_depth[slot][py * shadow_w + px];
            visibility_sum += (sd > sample_depth + slope_bias) ? 0.15f : 1.0f;
            sample_count++;
        }
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

/*==========================================================================
 * Pipeline vertex
 *=========================================================================*/

struct pipe_vert {
    float clip[4];
    float world[3];
    float normal[3];
    float tangent[4];
    float uv[2];
    float uv1[2];
    float color[4];
};

/// @brief Grow-on-demand scratch buffer for per-draw transformed vertices.
/// @details The software backend transforms each draw's vertices into a working buffer
///   before rasterisation. Reallocating the buffer per draw would pressure the heap and
///   waste cycles on memory-only work, so the context caches the largest buffer ever
///   needed; subsequent smaller draws reuse it free. Returns the scratch pointer or
///   `NULL` on overflow / allocation failure.
static pipe_vert_t *sw_acquire_pipe_vertices(sw_context_t *ctx, uint32_t count) {
    if (!ctx || count == 0)
        return NULL;
    if (ctx->vertex_scratch_capacity >= count)
        return ctx->vertex_scratch;
    if ((size_t)count > SIZE_MAX / sizeof(pipe_vert_t))
        return NULL;
    pipe_vert_t *new_scratch =
        (pipe_vert_t *)realloc(ctx->vertex_scratch, (size_t)count * sizeof(pipe_vert_t));
    if (!new_scratch)
        return NULL;
    ctx->vertex_scratch = new_scratch;
    ctx->vertex_scratch_capacity = count;
    return ctx->vertex_scratch;
}

/// @brief Cached `VIPER_3D_DEBUG` env-var query for tracing.
///
/// Set the env var to anything non-zero to enable per-draw debug
/// output (vertex counts, shader paths, etc.). Cached on first call
/// to avoid repeated `getenv` overhead in the hot path.
static int sw_debug_enabled(void) {
    static int cached = -1;
    if (cached == -1) {
        const char *env = getenv("VIPER_3D_DEBUG");
        cached = (env && *env && strcmp(env, "0") != 0) ? 1 : 0;
    }
    return cached;
}

#define MAX_CLIP_VERTS 9

/// @brief Linearly interpolate every attribute of two pipeline vertices.
///
/// Used during clipping to construct intersection vertices on the
/// frustum boundary. Interpolates clip pos, world pos, normal,
/// tangent, UV, and color.
static void pipe_lerp(const pipe_vert_t *a, const pipe_vert_t *b, float t, pipe_vert_t *out) {
    float s = 1.0f - t;
    for (int i = 0; i < 4; i++)
        out->clip[i] = s * a->clip[i] + t * b->clip[i];
    for (int i = 0; i < 3; i++)
        out->world[i] = s * a->world[i] + t * b->world[i];
    for (int i = 0; i < 3; i++)
        out->normal[i] = s * a->normal[i] + t * b->normal[i];
    for (int i = 0; i < 4; i++)
        out->tangent[i] = s * a->tangent[i] + t * b->tangent[i];
    for (int i = 0; i < 2; i++)
        out->uv[i] = s * a->uv[i] + t * b->uv[i];
    for (int i = 0; i < 2; i++)
        out->uv1[i] = s * a->uv1[i] + t * b->uv1[i];
    for (int i = 0; i < 4; i++)
        out->color[i] = s * a->color[i] + t * b->color[i];
}

/*==========================================================================
 * Sutherland-Hodgman frustum clipping
 *=========================================================================*/

/// @brief Signed distance from a clip-space vertex to one of six frustum planes.
/// In homogeneous clip coordinates, a point is inside the view volume when
/// -w <= x,y,z <= w. Each plane's distance is arranged so that >= 0 = inside:
///   0: left   (-x <= w  →  x+w >= 0)    1: right (x <= w  →  w-x >= 0)
///   2: bottom (-y <= w  →  y+w >= 0)    3: top   (y <= w  →  w-y >= 0)
///   4: near   (-z <= w  →  z+w >= 0)    5: far   (z <= w  →  w-z >= 0)
static float clip_dist(const pipe_vert_t *v, int plane) {
    float x = v->clip[0], y = v->clip[1], z = v->clip[2], w = v->clip[3];
    switch (plane) {
        case 0:
            return x + w;
        case 1:
            return w - x;
        case 2:
            return y + w;
        case 3:
            return w - y;
        case 4:
            return z + w;
        case 5:
            return w - z;
        default:
            return 0.0f;
    }
}

/// @brief Sutherland-Hodgman: clip a convex polygon against one frustum plane.
///
/// Walks each edge `i → j`. If `i` is inside, emit it; if the edge
/// crosses the plane, emit the intersection. Result is a new convex
/// polygon in `out`. Bounded by `MAX_CLIP_VERTS` (9) — extra
/// intersections beyond that are dropped (won't happen for one
/// triangle clipped against six planes).
static int clip_poly_plane(const pipe_vert_t *in, int in_count, pipe_vert_t *out, int plane) {
    if (in_count < 1)
        return 0;
    int out_count = 0;
    for (int i = 0; i < in_count; i++) {
        int j = (i + 1) % in_count;
        float di = clip_dist(&in[i], plane);
        float dj = clip_dist(&in[j], plane);
        if (di >= 0.0f) {
            if (out_count < MAX_CLIP_VERTS)
                out[out_count++] = in[i];
            if (dj < 0.0f) {
                float denom = di - dj;
                if (fabsf(denom) > 1e-10f) {
                    float t = di / denom;
                    if (out_count < MAX_CLIP_VERTS)
                        pipe_lerp(&in[i], &in[j], t, &out[out_count++]);
                }
            }
        } else if (dj >= 0.0f) {
            float denom = di - dj;
            if (fabsf(denom) < 1e-10f)
                continue;
            float t = di / denom;
            if (out_count < MAX_CLIP_VERTS)
                pipe_lerp(&in[i], &in[j], t, &out[out_count++]);
        }
    }
    return out_count;
}

/// @brief Clip a triangle against all 6 frustum planes, producing a convex polygon.
/// Uses double-buffer ping-pong (buf_a ↔ buf_b) to avoid allocation.
/// Returns vertex count (>= 3) or 0 if fully clipped.
static int clip_triangle(const pipe_vert_t *tri, pipe_vert_t *out) {
    pipe_vert_t buf_a[MAX_CLIP_VERTS], buf_b[MAX_CLIP_VERTS];
    memcpy(buf_a, tri, 3 * sizeof(pipe_vert_t));
    int count = 3;
    for (int plane = 0; plane < 6; plane++) {
        pipe_vert_t *src = (plane % 2 == 0) ? buf_a : buf_b;
        pipe_vert_t *dst = (plane % 2 == 0) ? buf_b : buf_a;
        count = clip_poly_plane(src, count, dst, plane);
        if (count < 3)
            return 0;
    }
    /* After 6 planes (even count), last output landed in buf_b */
    pipe_vert_t *result = (5 % 2 == 0) ? buf_b : buf_a;
    memcpy(out, result, (size_t)count * sizeof(pipe_vert_t));
    return count;
}

/*==========================================================================
 * Blinn-Phong per-vertex lighting (Gouraud)
 *=========================================================================*/

/// @brief Per-vertex Blinn-Phong lighting (Gouraud shading).
///
/// For each light, computes diffuse + Blinn-Phong specular contribution
/// and accumulates into the vertex color. Light types: 0=directional,
/// 1=point, 2=ambient (color-only), 3=spot (with smooth cone falloff).
/// Shaded color is later interpolated across triangle pixels (Gouraud).
/// Materials with `unlit` flag bypass lighting entirely.
static void compute_lighting(pipe_vert_t *v,
                             const sw_context_t *ctx,
                             const vgfx3d_draw_cmd_t *cmd,
                             const vgfx3d_light_params_t *lights,
                             int32_t light_count,
                             const float *ambient) {
    static const float zero_ambient[3] = {0.0f, 0.0f, 0.0f};
    if (!v || !cmd)
        return;
    if (!ambient)
        ambient = zero_ambient;
    if (!lights || light_count < 0)
        light_count = 0;
    if (cmd->unlit) {
        v->color[0] = cmd->diffuse_color[0] * v->color[0];
        v->color[1] = cmd->diffuse_color[1] * v->color[1];
        v->color[2] = cmd->diffuse_color[2] * v->color[2];
        v->color[3] = cmd->diffuse_color[3] * v->color[3];
        return;
    }

    float nx = v->normal[0], ny = v->normal[1], nz = v->normal[2];
    sw_normalize3(&nx, &ny, &nz, 0.0f, 0.0f, 1.0f);

    float vx;
    float vy;
    float vz;

    sw_compute_view_vector(ctx, v->world[0], v->world[1], v->world[2], &vx, &vy, &vz);

    /* Effective diffuse = material diffuse × vertex color */
    float dr = cmd->diffuse_color[0] * v->color[0];
    float dg = cmd->diffuse_color[1] * v->color[1];
    float db = cmd->diffuse_color[2] * v->color[2];

    float r = ambient[0] * dr;
    float g = ambient[1] * dg;
    float b = ambient[2] * db;

    for (int32_t li = 0; li < light_count; li++) {
        const vgfx3d_light_params_t *light = &lights[li];
        float lx, ly, lz, atten = 1.0f;

        if (light->type == 0) /* directional */
        {
            lx = -light->direction[0];
            ly = -light->direction[1];
            lz = -light->direction[2];
            if (!sw_normalize3(&lx, &ly, &lz, 0.0f, 0.0f, 0.0f))
                continue;
        } else if (light->type == 1) /* point */
        {
            lx = light->position[0] - v->world[0];
            ly = light->position[1] - v->world[1];
            lz = light->position[2] - v->world[2];
            float dist = sqrtf(lx * lx + ly * ly + lz * lz);
            if (!isfinite(dist) || dist <= 1e-7f)
                continue;
            lx /= dist;
            ly /= dist;
            lz /= dist;
            atten = 1.0f / (1.0f + light->attenuation * dist * dist);
        } else if (light->type == 3) /* spot */
        {
            lx = light->position[0] - v->world[0];
            ly = light->position[1] - v->world[1];
            lz = light->position[2] - v->world[2];
            float dist = sqrtf(lx * lx + ly * ly + lz * lz);
            if (!isfinite(dist) || dist <= 1e-7f)
                continue;
            lx /= dist;
            ly /= dist;
            lz /= dist;
            atten = 1.0f / (1.0f + light->attenuation * dist * dist);
            /* Cone attenuation: smoothstep between outer and inner cosines */
            float sx = -light->direction[0];
            float sy = -light->direction[1];
            float sz = -light->direction[2];
            if (!sw_normalize3(&sx, &sy, &sz, 0.0f, 0.0f, 0.0f))
                continue;
            float spot_dot = lx * sx + ly * sy + lz * sz;
            if (spot_dot < light->outer_cos)
                atten = 0.0f; /* outside cone */
            else if (spot_dot < light->inner_cos) {
                float cone_range = light->inner_cos - light->outer_cos;
                if (cone_range <= 1e-6f) {
                    atten = 0.0f;
                } else {
                    float t = (spot_dot - light->outer_cos) / cone_range;
                    atten *= t * t * (3.0f - 2.0f * t); /* smoothstep */
                }
            }
        } else /* ambient */
        {
            r += light->color[0] * light->intensity * dr;
            g += light->color[1] * light->intensity * dg;
            b += light->color[2] * light->intensity * db;
            continue;
        }

        float intensity = light->intensity;
        float ndl = nx * lx + ny * ly + nz * lz;
        if (ndl < 0.0f)
            ndl = 0.0f;

        r += light->color[0] * intensity * ndl * dr * atten;
        g += light->color[1] * intensity * ndl * dg * atten;
        b += light->color[2] * intensity * ndl * db * atten;

        if (ndl > 0.0f && cmd->shininess > 0.0f) {
            float hx = lx + vx, hy = ly + vy, hz = lz + vz;
            if (!sw_normalize3(&hx, &hy, &hz, 0.0f, 0.0f, 1.0f))
                continue;
            float ndh = nx * hx + ny * hy + nz * hz;
            if (ndh < 0.0f)
                ndh = 0.0f;
            float spec = powf(ndh, cmd->shininess);
            r += light->color[0] * intensity * spec * cmd->specular[0] * atten;
            g += light->color[1] * intensity * spec * cmd->specular[1] * atten;
            b += light->color[2] * intensity * spec * cmd->specular[2] * atten;
        }
    }

    /* Emissive contribution (additive, independent of lighting) */
    float emissive_scale = cmd->emissive_intensity;
    r += cmd->emissive_color[0] * emissive_scale;
    g += cmd->emissive_color[1] * emissive_scale;
    b += cmd->emissive_color[2] * emissive_scale;

    /* Apply shading model post-processing */
    switch (cmd->shading_model) {
        case 1: { /* Toon: quantize diffuse to N bands */
            float bands = cmd->custom_params[0] > 0.5f ? cmd->custom_params[0] : 4.0f;
            r = floorf(r * bands) / bands;
            g = floorf(g * bands) / bands;
            b = floorf(b * bands) / bands;
            break;
        }
        case 4: { /* Fresnel: angle-dependent alpha */
            float ndv = nx * vx + ny * vy + nz * vz;
            if (ndv < 0.0f)
                ndv = 0.0f;
            float power = cmd->custom_params[0] > 0.1f ? cmd->custom_params[0] : 3.0f;
            float bias = cmd->custom_params[1];
            float fresnel = powf(1.0f - ndv, power) + bias;
            if (fresnel < 0.0f)
                fresnel = 0.0f;
            if (fresnel > 1.0f)
                fresnel = 1.0f;
            v->color[3] *= fresnel;
            break;
        }
        case 5: { /* Emissive glow: boost emissive by strength */
            float strength = cmd->custom_params[0] > 0.0f ? cmd->custom_params[0] : 2.0f;
            r += cmd->emissive_color[0] * emissive_scale * (strength - 1.0f);
            g += cmd->emissive_color[1] * emissive_scale * (strength - 1.0f);
            b += cmd->emissive_color[2] * emissive_scale * (strength - 1.0f);
            break;
        }
        default: /* 0=BlinnPhong (already computed), 2=PBR, 3=Unlit (handled above) */
            break;
    }

    v->color[0] = r;
    v->color[1] = g;
    v->color[2] = b;
    v->color[3] = v->color[3] * cmd->diffuse_color[3] * cmd->alpha;
}

/*==========================================================================
 * Texture sampling
 *=========================================================================*/

typedef struct {
    int64_t width;
    int64_t height;
    uint32_t *data;
} sw_pixels_view;

/// @brief Cast an `rt_pixels` opaque pointer to a `sw_pixels_view` for sampling.
///
/// The underlying struct layout is duplicated locally (matches the
/// public ABI of `rt_pixels_new`). Returns 1 if the view is sampleable
/// (non-null + non-empty), 0 otherwise.
static int setup_pixels_view(const void *pixels_obj, sw_pixels_view *out) {
    if (!pixels_obj || !out)
        return 0;
    const sw_pixels_view *pv = (const sw_pixels_view *)pixels_obj;
    *out = *pv;
    if (out->width <= 0 || out->height <= 0 || out->width > INT32_MAX || out->height > INT32_MAX ||
        !out->data)
        return 0;
    if (out->width > INT32_MAX / out->height)
        return 0;
    return 1;
}

/// @brief Validate that a draw has full terrain-splat data (weight map + all 4 layers)
///   and open Pixels views over each; returns 0 if splatting is inactive or incomplete.
static int sw_setup_complete_splat(const vgfx3d_draw_cmd_t *cmd,
                                   sw_pixels_view *splat_view,
                                   sw_pixels_view layer_views[4]) {
    if (!cmd || !cmd->has_splat || !cmd->splat_map || !cmd->splat_layers[0] ||
        !cmd->splat_layers[1] || !cmd->splat_layers[2] || !cmd->splat_layers[3]) {
        return 0;
    }
    if (!setup_pixels_view(cmd->splat_map, splat_view))
        return 0;
    for (int i = 0; i < 4; i++) {
        if (!setup_pixels_view(cmd->splat_layers[i], &layer_views[i]))
            return 0;
    }
    return 1;
}

/// @brief Wrap a continuous UV coordinate into [0, 1] according to the material wrap mode.
/// @details CLAMP_TO_EDGE saturates to [0, 1]. MIRRORED_REPEAT folds across even/odd periods
///          so the texture reads forward then backward. Default (repeat) uses fract().
static float sw_wrap_coord(float value, int32_t mode) {
    if (!isfinite(value))
        return 0.0f;
    if (mode == RT_MATERIAL3D_TEXTURE_WRAP_CLAMP_TO_EDGE) {
        if (value < 0.0f)
            return 0.0f;
        if (value > 1.0f)
            return 1.0f;
        return value;
    }
    if (mode == RT_MATERIAL3D_TEXTURE_WRAP_MIRRORED_REPEAT) {
        float period = floorf(value);
        float frac = value - period;
        int64_t iperiod = (int64_t)period;
        if (frac < 0.0f) {
            frac += 1.0f;
            iperiod--;
        }
        return (iperiod & 1) ? 1.0f - frac : frac;
    }
    return value - floorf(value);
}

/// @brief Convert a potentially-out-of-range texel coordinate to a valid array index.
/// @details Implements all three material wrap modes on integer texel indices after
///   the continuous UV coordinate has been mapped to the texel grid:
///     - `RT_MATERIAL3D_TEXTURE_WRAP_CLAMP_TO_EDGE`: saturates to `[0, size-1]`.
///     - `RT_MATERIAL3D_TEXTURE_WRAP_MIRRORED_REPEAT`: maps the index into `[0, size-1]`
///       by folding across the period `2*size`; odd periods are mirrored so the
///       sequence reads `0,1,...,size-1,size-1,...,1,0,1,...` etc.
///     - Default (repeat): standard modulo with `((index % size) + size) % size`
///       so negative indices wrap correctly (plain `%` can return negative values in C).
///   Returns 0 immediately when @p size is zero or negative to prevent division by zero.
/// @param index  Texel coordinate, possibly negative or beyond `size`.
/// @param size   Texture dimension in texels along the relevant axis (must be > 0).
/// @param mode   One of the `RT_MATERIAL3D_TEXTURE_WRAP_*` constants.
/// @return Clamped/wrapped index in `[0, size-1]`.
static int sw_wrap_index(int index, int size, int32_t mode) {
    if (size <= 0)
        return 0;
    if (mode == RT_MATERIAL3D_TEXTURE_WRAP_CLAMP_TO_EDGE) {
        if (index < 0)
            return 0;
        if (index >= size)
            return size - 1;
        return index;
    }
    if (mode == RT_MATERIAL3D_TEXTURE_WRAP_MIRRORED_REPEAT) {
        if (size > INT32_MAX / 2)
            return index < 0 ? 0 : (index >= size ? size - 1 : index);
        int period = size * 2;
        int wrapped = index % period;
        if (wrapped < 0)
            wrapped += period;
        return wrapped >= size ? period - 1 - wrapped : wrapped;
    }
    return ((index % size) + size) % size;
}

/// @brief Texture sampler with imported wrap/filter state.
///
/// Maps UV → texel center (subtract 0.5), looks up the 4 surrounding
/// texels with selected wrap indexing, then weights them by the
/// fractional component. Produces normalized [0,1] RGBA.
static void sample_texture_ex(const sw_pixels_view *tex,
                              float u,
                              float v,
                              int32_t wrap_s,
                              int32_t wrap_t,
                              int32_t filter,
                              float *r,
                              float *g,
                              float *b,
                              float *a) {
    if (!r || !g || !b || !a)
        return;
    if (!tex || !tex->data || tex->width <= 0 || tex->height <= 0 || tex->width > INT32_MAX ||
        tex->height > INT32_MAX || tex->width > INT32_MAX / tex->height) {
        *r = *g = *b = 1.0f;
        *a = 1.0f;
        return;
    }
    int w = (int)tex->width, h = (int)tex->height;

    u = sw_wrap_coord(u, wrap_s);
    v = sw_wrap_coord(v, wrap_t);

    if (filter == RT_MATERIAL3D_TEXTURE_FILTER_NEAREST) {
        int x = sw_wrap_index((int)floorf(u * (float)w), w, wrap_s);
        int y = sw_wrap_index((int)floorf(v * (float)h), h, wrap_t);
        uint32_t p = tex->data[y * w + x];
        *r = (float)((p >> 24) & 0xFF) / 255.0f;
        *g = (float)((p >> 16) & 0xFF) / 255.0f;
        *b = (float)((p >> 8) & 0xFF) / 255.0f;
        *a = (float)(p & 0xFF) / 255.0f;
        return;
    }

    /* Bilinear: map UV to texel center, then interpolate the 4 neighbors */
    float fx = u * (float)w - 0.5f;
    float fy = v * (float)h - 0.5f;
    int x0 = (int)floorf(fx);
    int y0 = (int)floorf(fy);
    float xf = fx - (float)x0; /* fractional part [0,1) */
    float yf = fy - (float)y0;

    /* Wrap coordinates for the material sampler mode */
    int x1 = x0 + 1;
    int y1 = y0 + 1;
    x0 = sw_wrap_index(x0, w, wrap_s);
    y0 = sw_wrap_index(y0, h, wrap_t);
    x1 = sw_wrap_index(x1, w, wrap_s);
    y1 = sw_wrap_index(y1, h, wrap_t);

    /* Sample 4 texels */
    uint32_t p00 = tex->data[y0 * w + x0];
    uint32_t p10 = tex->data[y0 * w + x1];
    uint32_t p01 = tex->data[y1 * w + x0];
    uint32_t p11 = tex->data[y1 * w + x1];

    /* Bilinear weights */
    float w00 = (1.0f - xf) * (1.0f - yf);
    float w10 = xf * (1.0f - yf);
    float w01 = (1.0f - xf) * yf;
    float w11 = xf * yf;

    *r = (((p00 >> 24) & 0xFF) * w00 + ((p10 >> 24) & 0xFF) * w10 + ((p01 >> 24) & 0xFF) * w01 +
          ((p11 >> 24) & 0xFF) * w11) /
         255.0f;
    *g = (((p00 >> 16) & 0xFF) * w00 + ((p10 >> 16) & 0xFF) * w10 + ((p01 >> 16) & 0xFF) * w01 +
          ((p11 >> 16) & 0xFF) * w11) /
         255.0f;
    *b = (((p00 >> 8) & 0xFF) * w00 + ((p10 >> 8) & 0xFF) * w10 + ((p01 >> 8) & 0xFF) * w01 +
          ((p11 >> 8) & 0xFF) * w11) /
         255.0f;
    *a = ((p00 & 0xFF) * w00 + (p10 & 0xFF) * w10 + (p01 & 0xFF) * w01 + (p11 & 0xFF) * w11) /
         255.0f;
}

/// @brief Sample a texture with default repeat wrap and linear filter.
/// @details Convenience wrapper around `sample_texture_ex` that supplies
///   `RT_MATERIAL3D_TEXTURE_WRAP_REPEAT` for both axes and
///   `RT_MATERIAL3D_TEXTURE_FILTER_LINEAR` as the filter mode. Used for
///   internal texture lookups (e.g., environment cubemap faces) that are not
///   driven by per-material draw command state.
/// @param tex  Pixel view to sample.
/// @param u    Horizontal texture coordinate (unnormalized; repeat wraps to [0,1)).
/// @param v    Vertical texture coordinate.
/// @param r    Output red channel in [0, 1].
/// @param g    Output green channel in [0, 1].
/// @param b    Output blue channel in [0, 1].
/// @param a    Output alpha channel in [0, 1].
static void sample_texture(
    const sw_pixels_view *tex, float u, float v, float *r, float *g, float *b, float *a) {
    sample_texture_ex(tex,
                      u,
                      v,
                      RT_MATERIAL3D_TEXTURE_WRAP_REPEAT,
                      RT_MATERIAL3D_TEXTURE_WRAP_REPEAT,
                      RT_MATERIAL3D_TEXTURE_FILTER_LINEAR,
                      r,
                      g,
                      b,
                      a);
}

/// @brief Convert one channel from sRGB-encoded to linear space (IEC 61966-2-1 curve).
/// @details Standard two-segment sRGB EOTF: linear for dark values, `((v+0.055)/1.055)^2.4`
///   otherwise. Used by `sample_texture_srgb` so textures authored in sRGB are lit
///   correctly — lighting math is linear, so we have to undo the storage gamma first.
///   Alpha is intentionally *not* converted, matching the sRGB spec.
static float sw_srgb_to_linear(float value) {
    if (value < 0.0f)
        value = 0.0f;
    if (value > 1.0f)
        value = 1.0f;
    if (value <= 0.04045f)
        return value / 12.92f;
    return powf((value + 0.055f) / 1.055f, 2.4f);
}

/// @brief Return the primary texture S-axis wrap mode from @p cmd, defaulting to REPEAT.
static int32_t sw_cmd_wrap_s(const vgfx3d_draw_cmd_t *cmd) {
    return cmd ? cmd->texture_wrap_s : RT_MATERIAL3D_TEXTURE_WRAP_REPEAT;
}

/// @brief Return the primary texture T-axis wrap mode from @p cmd, defaulting to REPEAT.
static int32_t sw_cmd_wrap_t(const vgfx3d_draw_cmd_t *cmd) {
    return cmd ? cmd->texture_wrap_t : RT_MATERIAL3D_TEXTURE_WRAP_REPEAT;
}

/// @brief Return the primary texture filter mode from @p cmd, defaulting to LINEAR.
static int32_t sw_cmd_filter(const vgfx3d_draw_cmd_t *cmd) {
    return cmd ? cmd->texture_filter : RT_MATERIAL3D_TEXTURE_FILTER_LINEAR;
}

typedef struct screen_vert_t {
    float sx, sy, sz;
    float r, g, b, a;
    float u_over_w, v_over_w, inv_w;
    float u1_over_w, v1_over_w;
    float wx, wy, wz;     /* world position (for fog distance computation) */
    float nx, ny, nz;     /* world normal (for per-pixel lighting with normal maps) */
    float tx, ty, tz, tw; /* world tangent plus handedness sign (for TBN construction) */
} screen_vert_t;

/// @brief Return the S-axis wrap mode for a specific texture slot, falling back to the primary wrap
/// when out of range.
static int32_t sw_cmd_slot_wrap_s(const vgfx3d_draw_cmd_t *cmd, int32_t slot) {
    if (!cmd || slot < 0 || slot >= RT_MATERIAL3D_TEXTURE_SLOT_COUNT)
        return sw_cmd_wrap_s(cmd);
    return cmd->texture_slot_wrap_s[slot];
}

/// @brief Return the T-axis wrap mode for a specific texture slot, falling back to the primary wrap
/// when out of range.
static int32_t sw_cmd_slot_wrap_t(const vgfx3d_draw_cmd_t *cmd, int32_t slot) {
    if (!cmd || slot < 0 || slot >= RT_MATERIAL3D_TEXTURE_SLOT_COUNT)
        return sw_cmd_wrap_t(cmd);
    return cmd->texture_slot_wrap_t[slot];
}

/// @brief Return the filter mode for a specific texture slot, falling back to the primary filter
/// when out of range.
static int32_t sw_cmd_slot_filter(const vgfx3d_draw_cmd_t *cmd, int32_t slot) {
    if (!cmd || slot < 0 || slot >= RT_MATERIAL3D_TEXTURE_SLOT_COUNT)
        return sw_cmd_filter(cmd);
    return cmd->texture_slot_filter[slot];
}

/// @brief Perspective-correct UV interpolation with per-slot UV-set selection and transform.
/// @details Performs perspective-correct barycentric UV interpolation by dividing the
///   interpolated `u_over_w` / `v_over_w` quantities by the interpolated `inv_w`. This
///   avoids the affine texture distortion visible on large triangles when using plain
///   linear barycentric interpolation. The denominator guard (`fabsf(iw) <= 1e-7f`) skips
///   degenerate near-zero-W fragments.
///
///   After interpolation, the UV is optionally transformed by the per-slot 2×3 affine
///   matrix stored in `cmd->texture_slot_uv_transform[slot]` as `[a,b,c,d,tx,ty]`,
///   where `[0..3]` are the 2x2 linear row-major affine part and `[4..5]` are
///   translation. This allows per-slot UV scroll, scale, and rotation without
///   modifying geometry.
///
///   UV-set selection: when `cmd->texture_slot_uv_set[slot] > 0` the secondary UV channel
///   (`u1_over_w` / `v1_over_w`) is used instead of the primary, matching the glTF
///   `TEXCOORD_1` convention for lightmap or detail textures.
/// @param cmd     Draw command containing per-slot UV-set and transform state.
/// @param slot    Material texture slot index; negative or out-of-range disables per-slot
/// overrides.
/// @param b0,b1,b2  Barycentric weights for vertices v0, v1, v2 (must sum to 1).
/// @param v0,v1,v2  Screen-space vertices holding `u_over_w`, `v_over_w`, `inv_w`, etc.
/// @param out_u   Receives the perspective-corrected, transformed U coordinate.
/// @param out_v   Receives the perspective-corrected, transformed V coordinate.
static void sw_interpolate_uv_for_slot(const vgfx3d_draw_cmd_t *cmd,
                                       int32_t slot,
                                       float b0,
                                       float b1,
                                       float b2,
                                       const screen_vert_t *v0,
                                       const screen_vert_t *v1,
                                       const screen_vert_t *v2,
                                       float *out_u,
                                       float *out_v) {
    float iw;
    float u;
    float v;
    const float *m;
    int use_uv1 = cmd && slot >= 0 && slot < RT_MATERIAL3D_TEXTURE_SLOT_COUNT &&
                  cmd->texture_slot_uv_set[slot] > 0;
    if (!out_u || !out_v)
        return;
    *out_u = 0.0f;
    *out_v = 0.0f;
    if (!v0 || !v1 || !v2)
        return;
    iw = b0 * v0->inv_w + b1 * v1->inv_w + b2 * v2->inv_w;
    if (fabsf(iw) <= 1e-7f)
        return;
    if (use_uv1) {
        u = (b0 * v0->u1_over_w + b1 * v1->u1_over_w + b2 * v2->u1_over_w) / iw;
        v = (b0 * v0->v1_over_w + b1 * v1->v1_over_w + b2 * v2->v1_over_w) / iw;
    } else {
        u = (b0 * v0->u_over_w + b1 * v1->u_over_w + b2 * v2->u_over_w) / iw;
        v = (b0 * v0->v_over_w + b1 * v1->v_over_w + b2 * v2->v_over_w) / iw;
    }
    if (cmd && slot >= 0 && slot < RT_MATERIAL3D_TEXTURE_SLOT_COUNT) {
        m = cmd->texture_slot_uv_transform[slot];
        *out_u = u * m[0] + v * m[1] + m[4];
        *out_v = u * m[2] + v * m[3] + m[5];
    } else {
        *out_u = u;
        *out_v = v;
    }
}

/// @brief Sample a texture using the wrap and filter parameters from one material slot.
/// @details Routes to `sample_texture_ex` using per-slot wrap/filter accessors
///   (`sw_cmd_slot_wrap_s`, `sw_cmd_slot_wrap_t`, `sw_cmd_slot_filter`) that
///   gracefully fall back to the command's global wrap/filter when @p slot is
///   out of range or @p cmd is NULL. This ensures consistent sampling state
///   regardless of whether the draw command has per-slot overrides.
///   Does NOT apply sRGB linearisation — see `sample_texture_slot_srgb_ex` for that.
/// @param tex   Pixel view to sample.
/// @param cmd   Draw command carrying per-slot wrap/filter state; may be NULL.
/// @param slot  Material texture slot index; out-of-range falls back to global fields.
/// @param u     Texture U coordinate (will be wrapped per the slot's wrap_s mode).
/// @param v     Texture V coordinate (will be wrapped per the slot's wrap_t mode).
/// @param r     Output red channel in [0, 1].
/// @param g     Output green channel in [0, 1].
/// @param b     Output blue channel in [0, 1].
/// @param a     Output alpha channel in [0, 1].
static void sample_texture_slot_ex(const sw_pixels_view *tex,
                                   const vgfx3d_draw_cmd_t *cmd,
                                   int32_t slot,
                                   float u,
                                   float v,
                                   float *r,
                                   float *g,
                                   float *b,
                                   float *a) {
    sample_texture_ex(tex,
                      u,
                      v,
                      sw_cmd_slot_wrap_s(cmd, slot),
                      sw_cmd_slot_wrap_t(cmd, slot),
                      sw_cmd_slot_filter(cmd, slot),
                      r,
                      g,
                      b,
                      a);
}

/// @brief Sample a texture and linearise the RGB channels from sRGB storage.
/// @details Calls `sample_texture_slot_ex` to obtain raw [0,1] RGBA values, then
///   applies `sw_srgb_to_linear` to each of the three colour channels. Alpha is
///   intentionally left in its stored form (sRGB spec does not gamma-encode alpha).
///   Used for base-colour / albedo textures whose pixel data was authored in sRGB
///   space; lighting and PBR math must operate in linear light, so we undo the
///   storage gamma here before those computations.
/// @param tex   Pixel view to sample.
/// @param cmd   Draw command carrying per-slot wrap/filter state; may be NULL.
/// @param slot  Material texture slot index.
/// @param u     Texture U coordinate.
/// @param v     Texture V coordinate.
/// @param r     Output linearised red channel in [0, 1].
/// @param g     Output linearised green channel in [0, 1].
/// @param b     Output linearised blue channel in [0, 1].
/// @param a     Output alpha channel in [0, 1] (not linearised).
static void sample_texture_slot_srgb_ex(const sw_pixels_view *tex,
                                        const vgfx3d_draw_cmd_t *cmd,
                                        int32_t slot,
                                        float u,
                                        float v,
                                        float *r,
                                        float *g,
                                        float *b,
                                        float *a) {
    sample_texture_slot_ex(tex, cmd, slot, u, v, r, g, b, a);
    *r = sw_srgb_to_linear(*r);
    *g = sw_srgb_to_linear(*g);
    *b = sw_srgb_to_linear(*b);
}

/*==========================================================================
 * Edge-function triangle rasterizer
 *=========================================================================*/

static inline void sw_compute_view_vector(const sw_context_t *ctx,
                                          float wx,
                                          float wy,
                                          float wz,
                                          float *out_vx,
                                          float *out_vy,
                                          float *out_vz);
static inline float sw_compute_fog_distance(const sw_context_t *ctx, float wx, float wy, float wz);
static void sw_apply_environment_reflection(const vgfx3d_draw_cmd_t *cmd,
                                            const sw_pixels_view *normal_map,
                                            const sw_pixels_view *metallic_roughness_map,
                                            float b0,
                                            float b1,
                                            float b2,
                                            const screen_vert_t *v0,
                                            const screen_vert_t *v1,
                                            const screen_vert_t *v2,
                                            const sw_context_t *ctx,
                                            const float *precomputed_world_normal,
                                            float *inout_r,
                                            float *inout_g,
                                            float *inout_b);

// Inline math helpers — used heavily in the per-pixel PBR shading path,
// inlined for the obvious reason that one indirect call per pixel
// would dominate the cost.

/// @brief Clamp `x` to `[0, 1]`.
static inline float clamp01f(float x) {
    return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x);
}

/// @brief 3-vector dot product (six args, avoids array packing overhead).
static inline float dot3f(float ax, float ay, float az, float bx, float by, float bz) {
    return ax * bx + ay * by + az * bz;
}

/// @brief In-place 3-vector normalization (no-op for zero-length vectors).
static inline void normalize3f(float *x, float *y, float *z) {
    if (!x || !y || !z || !isfinite(*x) || !isfinite(*y) || !isfinite(*z)) {
        if (x)
            *x = 0.0f;
        if (y)
            *y = 0.0f;
        if (z)
            *z = 0.0f;
        return;
    }
    float len = sqrtf((*x) * (*x) + (*y) * (*y) + (*z) * (*z));
    if (!isfinite(len) || len <= 1e-7f) {
        *x = 0.0f;
        *y = 0.0f;
        *z = 0.0f;
        return;
    }
    *x /= len;
    *y /= len;
    *z /= len;
}

/// @brief Compute the unit view vector from a world-space point to the camera.
/// @details Branches on projection mode: orthographic cameras have a
///   constant view direction (camera looks along `-cam_forward` everywhere
///   in the scene), so we return the negated forward vector regardless of
///   the surface position. Perspective cameras have a per-fragment view
///   direction that points from the fragment back to the eye position.
///   The result is renormalized because subtle floating-point error in
///   the subtraction can drift the magnitude away from 1.0 enough to
///   affect specular highlight shape.
static inline void sw_compute_view_vector(const sw_context_t *ctx,
                                          float wx,
                                          float wy,
                                          float wz,
                                          float *out_vx,
                                          float *out_vy,
                                          float *out_vz) {
    float vx;
    float vy;
    float vz;

    if (!ctx || !out_vx || !out_vy || !out_vz) {
        return;
    }
    if (ctx->cam_is_ortho) {
        vx = -ctx->cam_forward[0];
        vy = -ctx->cam_forward[1];
        vz = -ctx->cam_forward[2];
    } else {
        vx = ctx->cam_pos[0] - wx;
        vy = ctx->cam_pos[1] - wy;
        vz = ctx->cam_pos[2] - wz;
    }
    normalize3f(&vx, &vy, &vz);
    *out_vx = vx;
    *out_vy = vy;
    *out_vz = vz;
}

/// @brief Compute the effective fog distance from the camera to a world-space point.
/// @details Orthographic cameras use the *depth along forward* (projection
///   of the world→camera delta onto `cam_forward`, absolute value) so fog
///   density depends only on how far in front of the camera a fragment
///   sits, not its lateral offset — matches how fog attenuation behaves
///   in real ortho-rendered scenes. Perspective cameras use plain
///   euclidean distance to the eye, giving the familiar radial fog
///   falloff. Null context returns 0 so missed-setup paths produce no
///   fog rather than crashing.
static inline float sw_compute_fog_distance(const sw_context_t *ctx, float wx, float wy, float wz) {
    if (!ctx)
        return 0.0f;
    if (ctx->cam_is_ortho) {
        return fabsf((wx - ctx->cam_pos[0]) * ctx->cam_forward[0] +
                     (wy - ctx->cam_pos[1]) * ctx->cam_forward[1] +
                     (wz - ctx->cam_pos[2]) * ctx->cam_forward[2]);
    }
    {
        float dx = wx - ctx->cam_pos[0];
        float dy = wy - ctx->cam_pos[1];
        float dz = wz - ctx->cam_pos[2];
        return sqrtf(dx * dx + dy * dy + dz * dz);
    }
}

/// @brief Blend an environment-cubemap reflection into the shaded fragment color.
/// @details Barycentric-interpolates the world position, perturbs the
///   surface normal through the optional tangent-space normal map,
///   computes the reflection vector about the view direction, samples
///   the bound environment cubemap (optionally roughness-blurred via
///   `rt_cubemap_sample_roughness` when a metallic/roughness map is
///   bound), and lerps the result into `inout_rgb` by the material's
///   reflectivity scalar. All early-out branches (no cubemap bound,
///   zero reflectivity, no valid view) leave `inout_rgb` untouched so
///   the caller can invoke this unconditionally and not pay for
///   reflection work on surfaces that don't want it.
static void sw_apply_environment_reflection(const vgfx3d_draw_cmd_t *cmd,
                                            const sw_pixels_view *normal_map,
                                            const sw_pixels_view *metallic_roughness_map,
                                            float b0,
                                            float b1,
                                            float b2,
                                            const screen_vert_t *v0,
                                            const screen_vert_t *v1,
                                            const screen_vert_t *v2,
                                            const sw_context_t *ctx,
                                            const float *precomputed_world_normal,
                                            float *inout_r,
                                            float *inout_g,
                                            float *inout_b) {
    float wx;
    float wy;
    float wz;
    float pnx;
    float pny;
    float pnz;
    float vdx;
    float vdy;
    float vdz;
    float reflectivity;
    float roughness;
    float normal_u = 0.0f;
    float normal_v = 0.0f;
    float mr_u = 0.0f;
    float mr_v = 0.0f;

    if (!cmd || !ctx || !inout_r || !inout_g || !inout_b || !cmd->env_map ||
        cmd->reflectivity <= 0.0001f)
        return;

    wx = b0 * v0->wx + b1 * v1->wx + b2 * v2->wx;
    wy = b0 * v0->wy + b1 * v1->wy + b2 * v2->wy;
    wz = b0 * v0->wz + b1 * v1->wz + b2 * v2->wz;
    pnx = b0 * v0->nx + b1 * v1->nx + b2 * v2->nx;
    pny = b0 * v0->ny + b1 * v1->ny + b2 * v2->ny;
    pnz = b0 * v0->nz + b1 * v1->nz + b2 * v2->nz;
    normalize3f(&pnx, &pny, &pnz);

    sw_interpolate_uv_for_slot(
        cmd, RT_MATERIAL3D_TEXTURE_SLOT_NORMAL, b0, b1, b2, v0, v1, v2, &normal_u, &normal_v);
    sw_interpolate_uv_for_slot(
        cmd, RT_MATERIAL3D_TEXTURE_SLOT_METALLIC_ROUGHNESS, b0, b1, b2, v0, v1, v2, &mr_u, &mr_v);

    /* When the main shading path already produced this fragment's perturbed
     * world normal, reuse it (below) and skip the redundant per-pixel normal-map
     * sample + TBN rebuild here. */
    if (!precomputed_world_normal && normal_map) {
        float ptx = b0 * v0->tx + b1 * v1->tx + b2 * v2->tx;
        float pty = b0 * v0->ty + b1 * v1->ty + b2 * v2->ty;
        float ptz = b0 * v0->tz + b1 * v1->tz + b2 * v2->tz;
        float ptw = b0 * v0->tw + b1 * v1->tw + b2 * v2->tw;
        float tlen = sqrtf(ptx * ptx + pty * pty + ptz * ptz);

        if (tlen > 1e-7f) {
            float tnr;
            float tng;
            float tnb;
            float tna;
            float map_x;
            float map_y;
            float map_z;
            float tdotn;
            float tangent_sign;
            float bbx;
            float bby;
            float bbz;

            ptx /= tlen;
            pty /= tlen;
            ptz /= tlen;
            tdotn = ptx * pnx + pty * pny + ptz * pnz;
            ptx -= tdotn * pnx;
            pty -= tdotn * pny;
            ptz -= tdotn * pnz;
            normalize3f(&ptx, &pty, &ptz);

            sample_texture_slot_ex(normal_map,
                                   cmd,
                                   RT_MATERIAL3D_TEXTURE_SLOT_NORMAL,
                                   normal_u,
                                   normal_v,
                                   &tnr,
                                   &tng,
                                   &tnb,
                                   &tna);
            map_x = (tnr * 2.0f - 1.0f) * cmd->normal_scale;
            map_y = (tng * 2.0f - 1.0f) * cmd->normal_scale;
            map_z = tnb * 2.0f - 1.0f;
            tangent_sign = ptw < 0.0f ? -1.0f : 1.0f;
            bbx = (pny * ptz - pnz * pty) * tangent_sign;
            bby = (pnz * ptx - pnx * ptz) * tangent_sign;
            bbz = (pnx * pty - pny * ptx) * tangent_sign;
            pnx = ptx * map_x + bbx * map_y + pnx * map_z;
            pny = pty * map_x + bby * map_y + pny * map_z;
            pnz = ptz * map_x + bbz * map_y + pnz * map_z;
            normalize3f(&pnx, &pny, &pnz);
        }
    }
    if (precomputed_world_normal) {
        pnx = precomputed_world_normal[0];
        pny = precomputed_world_normal[1];
        pnz = precomputed_world_normal[2];
    }

    sw_compute_view_vector(ctx, wx, wy, wz, &vdx, &vdy, &vdz);
    roughness = clamp01f(cmd->roughness);
    if (cmd->workflow == RT_MATERIAL3D_WORKFLOW_PBR && metallic_roughness_map) {
        float mrr;
        float mrg;
        float mrb;
        float mra;
        sample_texture_slot_ex(metallic_roughness_map,
                               cmd,
                               RT_MATERIAL3D_TEXTURE_SLOT_METALLIC_ROUGHNESS,
                               mr_u,
                               mr_v,
                               &mrr,
                               &mrg,
                               &mrb,
                               &mra);
        roughness = clamp01f(roughness * mrg);
        if (roughness < 0.045f)
            roughness = 0.045f;
    }
    reflectivity = clamp01f(cmd->reflectivity);
    if (reflectivity > 0.0f) {
        float dot_nv = dot3f(pnx, pny, pnz, vdx, vdy, vdz);
        float rx = 2.0f * dot_nv * pnx - vdx;
        float ry = 2.0f * dot_nv * pny - vdy;
        float rz = 2.0f * dot_nv * pnz - vdz;
        float env_r;
        float env_g;
        float env_b;

        normalize3f(&rx, &ry, &rz);
        rt_cubemap_sample_roughness(
            (const rt_cubemap3d *)cmd->env_map, rx, ry, rz, roughness, &env_r, &env_g, &env_b);
        *inout_r = *inout_r * (1.0f - reflectivity) + env_r * reflectivity;
        *inout_g = *inout_g * (1.0f - reflectivity) + env_g * reflectivity;
        *inout_b = *inout_b * (1.0f - reflectivity) + env_b * reflectivity;
    }
}

/// @brief PBR D term: GGX/Trowbridge-Reitz normal distribution.
///
/// `(α² / (π · ((N·H)² · (α² - 1) + 1)²))` with `α = roughness²`.
/// Standard formula from "Microfacet Models for Refraction" (Walter
/// et al, 2007). Used in the Cook-Torrance specular term.
static inline float pbr_distribution_ggx(float ndh, float roughness) {
    const float kPi = 3.14159265358979323846f;
    float a = roughness * roughness;
    float a2 = a * a;
    float denom = ndh * ndh * (a2 - 1.0f) + 1.0f;
    return a2 / (kPi * denom * denom + 1e-6f);
}

/// @brief PBR G1 term: Schlick-GGX geometry function for one direction.
///
/// `N·V / (N·V · (1 - k) + k)` with k = (roughness+1)²/8 (per-direction
/// k; pairs with `pbr_geometry_smith` to combine view + light). Models
/// microfacet self-shadowing/masking.
static inline float pbr_geometry_schlick_ggx(float ndv, float roughness) {
    float r = roughness + 1.0f;
    float k = (r * r) / 8.0f;
    return ndv / (ndv * (1.0f - k) + k + 1e-6f);
}

/// @brief PBR G term: Smith geometry — combines view and light G1 contributions.
static inline float pbr_geometry_smith(float ndv, float ndl, float roughness) {
    return pbr_geometry_schlick_ggx(ndv, roughness) * pbr_geometry_schlick_ggx(ndl, roughness);
}

/// @brief Per-triangle invariants shared by every fragment of one raster_triangle
///   call. Built once before the pixel loop; sw_shade_fragment reads it per pixel
///   so the rasterizer's inner loop stays a thin barycentric/depth-test skeleton.
typedef struct {
    uint8_t *pixels;
    float *zbuf;
    int32_t stride;
    const screen_vert_t *v0;
    const screen_vert_t *v1;
    const screen_vert_t *v2;
    const sw_pixels_view *tex;
    const sw_pixels_view *emissive_tex;
    const float *emissive_color;
    const sw_pixels_view *normal_map;
    const sw_pixels_view *specular_map;
    const sw_pixels_view *metallic_roughness_map;
    const sw_pixels_view *ao_map;
    const vgfx3d_draw_cmd_t *cmd;
    const vgfx3d_light_params_t *lights;
    int32_t light_count;
    const float *ambient;
    const sw_context_t *fog_ctx;
    int depth_disabled;
    int have_splat;
    const sw_pixels_view *splat_view;
    const sw_pixels_view *layer_views;
} sw_fragment_ctx;

/// @brief Vertex-attribute UVs interpolated once per fragment for each texture
///   slot consumed by the per-pixel lighting stages.
typedef struct {
    float normal_u, normal_v;
    float specular_u, specular_v;
    float emissive_u, emissive_v;
    float mr_u, mr_v;
    float ao_u, ao_v;
} sw_slot_uvs;

/// @brief Per-fragment geometric inputs shared by the PBR and Phong lighting
///   stages: the (possibly normal-mapped) shading normal, world position, view
///   direction, and clamped N·V.
typedef struct {
    float nx, ny, nz;
    float wx, wy, wz;
    float vx, vy, vz;
    float ndv;
} sw_shade_geom;

/// @brief Resolve the pre-lighting albedo: interpolated vertex color modulated by
///   the base-color texture, the optional terrain-splat layer blend, and the
///   legacy emissive map. Writes the albedo to (*fr,*fg,*fb_c) and the per-texel
///   base-color alpha to *tex_alpha.
static void sw_shade_resolve_albedo(const sw_fragment_ctx *fc,
                                    float b0,
                                    float b1,
                                    float b2,
                                    float *fr,
                                    float *fg,
                                    float *fb_c,
                                    float *tex_alpha) {
    const screen_vert_t *v0 = fc->v0;
    const screen_vert_t *v1 = fc->v1;
    const screen_vert_t *v2 = fc->v2;
    const sw_pixels_view *tex = fc->tex;
    const sw_pixels_view *emissive_tex = fc->emissive_tex;
    const float *emissive_color = fc->emissive_color;
    const vgfx3d_draw_cmd_t *cmd = fc->cmd;
    int have_splat = fc->have_splat;
    const sw_pixels_view *layer_views = fc->layer_views;

    float cr = b0 * v0->r + b1 * v1->r + b2 * v2->r;
    float cg = b0 * v0->g + b1 * v1->g + b2 * v2->g;
    float cb = b0 * v0->b + b1 * v1->b + b2 * v2->b;
    float material_r = cr;
    float material_g = cg;
    float material_b = cb;
    float ta_out = 1.0f; /* per-texel alpha (for foliage, fences) */
    if (tex) {
        float u;
        float vc;
        sw_interpolate_uv_for_slot(
            cmd, RT_MATERIAL3D_TEXTURE_SLOT_BASE_COLOR, b0, b1, b2, v0, v1, v2, &u, &vc);
        {
            float tr, tg, tb, ta;
            if (cmd && cmd->workflow == RT_MATERIAL3D_WORKFLOW_PBR)
                sample_texture_slot_srgb_ex(
                    tex, cmd, RT_MATERIAL3D_TEXTURE_SLOT_BASE_COLOR, u, vc, &tr, &tg, &tb, &ta);
            else
                sample_texture_slot_ex(
                    tex, cmd, RT_MATERIAL3D_TEXTURE_SLOT_BASE_COLOR, u, vc, &tr, &tg, &tb, &ta);
            cr *= tr;
            cg *= tg;
            cb *= tb;
            ta_out = ta;
        }
    }
    /* Terrain splat: replace diffuse with per-pixel layer blend. Views were
     * resolved once per triangle (splat_view / layer_views). */
    if (have_splat) {
        float iw = b0 * v0->inv_w + b1 * v1->inv_w + b2 * v2->inv_w;
        if (fabsf(iw) > 1e-7f) {
            float sp_u = (b0 * v0->u_over_w + b1 * v1->u_over_w + b2 * v2->u_over_w) / iw;
            float sp_v = (b0 * v0->v_over_w + b1 * v1->v_over_w + b2 * v2->v_over_w) / iw;
            {
                float sr, sg, sb, sa;
                sample_texture(fc->splat_view, sp_u, sp_v, &sr, &sg, &sb, &sa);
                float w[4] = {sr, sg, sb, sa};
                float wsum = w[0] + w[1] + w[2] + w[3];
                if (wsum > 0.001f) {
                    for (int wi = 0; wi < 4; wi++)
                        w[wi] /= wsum;
                } else {
                    w[0] = 1.0f;
                    w[1] = w[2] = w[3] = 0.0f;
                }
                float blr = 0, blg = 0, blb = 0;
                for (int L = 0; L < 4; L++) {
                    if (w[L] < 0.001f)
                        continue;
                    float lu = sp_u * cmd->splat_layer_scales[L];
                    float lvc = sp_v * cmd->splat_layer_scales[L];
                    float pr, pg, pb, pa;
                    sample_texture(&layer_views[L], lu, lvc, &pr, &pg, &pb, &pa);
                    blr += pr * w[L];
                    blg += pg * w[L];
                    blb += pb * w[L];
                }
                cr = blr * material_r;
                cg = blg * material_g;
                cb = blb * material_b;
            }
        }
    }

    /* Emissive map sampling (legacy path only; PBR handles it in the lighting
     * branch so emissiveIntensity can scale both the color and the map.) */
    if (emissive_tex && !(cmd && cmd->workflow == RT_MATERIAL3D_WORKFLOW_PBR)) {
        float u;
        float vc;
        float er, eg, eb, ea;
        sw_interpolate_uv_for_slot(
            cmd, RT_MATERIAL3D_TEXTURE_SLOT_EMISSIVE, b0, b1, b2, v0, v1, v2, &u, &vc);
        sample_texture_slot_srgb_ex(
            emissive_tex, cmd, RT_MATERIAL3D_TEXTURE_SLOT_EMISSIVE, u, vc, &er, &eg, &eb, &ea);
        float emissive_scale = (cmd ? cmd->emissive_intensity : 1.0f);
        cr += er * emissive_color[0] * emissive_scale;
        cg += eg * emissive_color[1] * emissive_scale;
        cb += eb * emissive_color[2] * emissive_scale;
        (void)ea;
    }

    *fr = cr;
    *fg = cg;
    *fb_c = cb;
    *tex_alpha = ta_out;
}

/// @brief Interpolate the world-space vertex normal and, when a normal map is
///   bound and the tangent is non-degenerate, perturb it through the TBN basis.
///   Writes the final shading normal to (*out_nx,*out_ny,*out_nz).
static void sw_shade_perturb_normal(const sw_fragment_ctx *fc,
                                    float b0,
                                    float b1,
                                    float b2,
                                    float normal_u,
                                    float normal_v,
                                    float *out_nx,
                                    float *out_ny,
                                    float *out_nz) {
    const screen_vert_t *v0 = fc->v0;
    const screen_vert_t *v1 = fc->v1;
    const screen_vert_t *v2 = fc->v2;
    const sw_pixels_view *normal_map = fc->normal_map;
    const vgfx3d_draw_cmd_t *cmd = fc->cmd;

    /* Interpolate world normal */
    float pnx = b0 * v0->nx + b1 * v1->nx + b2 * v2->nx;
    float pny = b0 * v0->ny + b1 * v1->ny + b2 * v2->ny;
    float pnz = b0 * v0->nz + b1 * v1->nz + b2 * v2->nz;
    normalize3f(&pnx, &pny, &pnz);

    /* Interpolate tangent + build TBN */
    float ptx = b0 * v0->tx + b1 * v1->tx + b2 * v2->tx;
    float pty = b0 * v0->ty + b1 * v1->ty + b2 * v2->ty;
    float ptz = b0 * v0->tz + b1 * v1->tz + b2 * v2->tz;
    float ptw = b0 * v0->tw + b1 * v1->tw + b2 * v2->tw;
    float tlen = sqrtf(ptx * ptx + pty * pty + ptz * ptz);

    if (normal_map && tlen > 1e-7f) {
        ptx /= tlen;
        pty /= tlen;
        ptz /= tlen;
        /* Gram-Schmidt orthogonalize T against N */
        float tdotn = ptx * pnx + pty * pny + ptz * pnz;
        ptx -= tdotn * pnx;
        pty -= tdotn * pny;
        ptz -= tdotn * pnz;
        tlen = sqrtf(ptx * ptx + pty * pty + ptz * ptz);
        if (tlen > 1e-7f) {
            ptx /= tlen;
            pty /= tlen;
            ptz /= tlen;
        }

        /* Sample normal map: [0,1] → [-1,1] */
        float tnr, tng, tnb, tna;
        sample_texture_slot_ex(normal_map,
                               cmd,
                               RT_MATERIAL3D_TEXTURE_SLOT_NORMAL,
                               normal_u,
                               normal_v,
                               &tnr,
                               &tng,
                               &tnb,
                               &tna);
        float map_x = (tnr * 2.0f - 1.0f) * cmd->normal_scale;
        float map_y = (tng * 2.0f - 1.0f) * cmd->normal_scale;
        float map_z = tnb * 2.0f - 1.0f;

        /* Bitangent = sign * (N × T) */
        float tangent_sign = ptw < 0.0f ? -1.0f : 1.0f;
        float bbx = (pny * ptz - pnz * pty) * tangent_sign;
        float bby = (pnz * ptx - pnx * ptz) * tangent_sign;
        float bbz = (pnx * pty - pny * ptx) * tangent_sign;

        /* TBN transform: tangent-space → world-space */
        float wn_x = ptx * map_x + bbx * map_y + pnx * map_z;
        float wn_y = pty * map_x + bby * map_y + pny * map_z;
        float wn_z = ptz * map_x + bbz * map_y + pnz * map_z;
        float wlen = sqrtf(wn_x * wn_x + wn_y * wn_y + wn_z * wn_z);
        if (wlen > 1e-7f) {
            pnx = wn_x / wlen;
            pny = wn_y / wlen;
            pnz = wn_z / wlen;
        }
    }
    /* else: degenerate tangent → use unperturbed normal */

    *out_nx = pnx;
    *out_ny = pny;
    *out_nz = pnz;
}

/// @brief Compute the normalized light direction and distance attenuation for a
///   directional (type 0), point (type 1), or spot (type 3) light at world point
///   (wx,wy,wz). Returns 1 with (*llx,*lly,*llz,*la) filled, or 0 if the light is
///   degenerate and should be skipped. Ambient lights are handled by the caller.
static int sw_eval_light_dir(const vgfx3d_light_params_t *lt,
                             float wx,
                             float wy,
                             float wz,
                             float *llx,
                             float *lly,
                             float *llz,
                             float *la) {
    float lx, ly, lz;
    float atten = 1.0f;
    if (lt->type == 0) { /* directional */
        lx = -lt->direction[0];
        ly = -lt->direction[1];
        lz = -lt->direction[2];
        normalize3f(&lx, &ly, &lz);
    } else if (lt->type == 1) { /* point */
        lx = lt->position[0] - wx;
        ly = lt->position[1] - wy;
        lz = lt->position[2] - wz;
        float dist = sqrtf(lx * lx + ly * ly + lz * lz);
        if (!isfinite(dist) || dist <= 1e-7f)
            return 0;
        lx /= dist;
        ly /= dist;
        lz /= dist;
        atten = 1.0f / (1.0f + lt->attenuation * dist * dist);
    } else { /* spot (type 3) */
        lx = lt->position[0] - wx;
        ly = lt->position[1] - wy;
        lz = lt->position[2] - wz;
        float dist = sqrtf(lx * lx + ly * ly + lz * lz);
        if (!isfinite(dist) || dist <= 1e-7f)
            return 0;
        lx /= dist;
        ly /= dist;
        lz /= dist;
        atten = 1.0f / (1.0f + lt->attenuation * dist * dist);
        float sdx = -lt->direction[0];
        float sdy = -lt->direction[1];
        float sdz = -lt->direction[2];
        normalize3f(&sdx, &sdy, &sdz);
        float sd = lx * sdx + ly * sdy + lz * sdz;
        if (sd < lt->outer_cos)
            atten = 0.0f;
        else if (sd < lt->inner_cos) {
            float cone_range = lt->inner_cos - lt->outer_cos;
            if (cone_range <= 1e-6f) {
                atten = 0.0f;
            } else {
                float st = (sd - lt->outer_cos) / cone_range;
                atten *= st * st * (3.0f - 2.0f * st);
            }
        }
    }
    *llx = lx;
    *lly = ly;
    *llz = lz;
    *la = atten;
    return 1;
}

/// @brief Cook-Torrance PBR lighting for one fragment. Reads the albedo from
///   (*fr,*fg,*fb_c) and overwrites it with the lit color. Samples the
///   metallic-roughness, AO, and emissive maps; accumulates ambient, per-light
///   direct, and emissive contributions; applies the toon/emissive shading models.
static void sw_shade_lighting_pbr(const sw_fragment_ctx *fc,
                                  const sw_shade_geom *g,
                                  const sw_slot_uvs *uv,
                                  float *fr,
                                  float *fg,
                                  float *fb_c) {
    const vgfx3d_draw_cmd_t *cmd = fc->cmd;
    const sw_pixels_view *emissive_tex = fc->emissive_tex;
    const sw_pixels_view *metallic_roughness_map = fc->metallic_roughness_map;
    const sw_pixels_view *ao_map = fc->ao_map;
    const vgfx3d_light_params_t *lights = fc->lights;
    int32_t light_count = fc->light_count;
    const float *ambient = fc->ambient;
    const sw_context_t *fog_ctx = fc->fog_ctx;
    float pnx = g->nx, pny = g->ny, pnz = g->nz;
    float wx = g->wx, wy = g->wy, wz = g->wz;
    float vdx = g->vx, vdy = g->vy, vdz = g->vz;

    float base_r = *fr;
    float base_g = *fg;
    float base_b = *fb_c;
    float metallic = clamp01f(cmd->metallic);
    float roughness = clamp01f(cmd->roughness);
    float ao = clamp01f(cmd->ao);
    if (metallic_roughness_map) {
        float mrr, mrg, mrb, mra;
        sample_texture_slot_ex(metallic_roughness_map,
                               cmd,
                               RT_MATERIAL3D_TEXTURE_SLOT_METALLIC_ROUGHNESS,
                               uv->mr_u,
                               uv->mr_v,
                               &mrr,
                               &mrg,
                               &mrb,
                               &mra);
        roughness *= mrg;
        metallic *= mrb;
    }
    if (ao_map) {
        float aor, aog, aob, aoa;
        sample_texture_slot_ex(
            ao_map, cmd, RT_MATERIAL3D_TEXTURE_SLOT_AO, uv->ao_u, uv->ao_v, &aor, &aog, &aob, &aoa);
        ao *= aor;
    }
    metallic = clamp01f(metallic);
    roughness = roughness < 0.045f ? 0.045f : clamp01f(roughness);
    ao = clamp01f(ao);

    float ndv = g->ndv;
    if (ndv < 0.001f)
        ndv = 0.001f;

    float lit_r = (ambient ? ambient[0] : 0.0f) * base_r * ao;
    float lit_g = (ambient ? ambient[1] : 0.0f) * base_g * ao;
    float lit_b = (ambient ? ambient[2] : 0.0f) * base_b * ao;

    for (int32_t li = 0; li < light_count; li++) {
        const vgfx3d_light_params_t *lt = &lights[li];
        float llx, lly, llz, la = 1.0f;

        if (lt->type != 0 && lt->type != 1 && lt->type != 3) { /* ambient */
            lit_r += lt->color[0] * lt->intensity * base_r * ao;
            lit_g += lt->color[1] * lt->intensity * base_g * ao;
            lit_b += lt->color[2] * lt->intensity * base_b * ao;
            continue;
        }
        if (!sw_eval_light_dir(lt, wx, wy, wz, &llx, &lly, &llz, &la))
            continue;

        if (lt->type == 0 && lt->shadow_index >= 0) {
            int32_t shadow_slot = sw_resolve_shadow_slot(fog_ctx, lt, wx, wy, wz);
            la *= sw_sample_shadow_visibility(fog_ctx, shadow_slot, wx, wy, wz);
        }

        float ndl = dot3f(pnx, pny, pnz, llx, lly, llz);
        if (ndl <= 0.0f)
            continue;

        float hx = llx + vdx;
        float hy = lly + vdy;
        float hz = llz + vdz;
        normalize3f(&hx, &hy, &hz);
        float ndh = clamp01f(dot3f(pnx, pny, pnz, hx, hy, hz));
        float vdh = clamp01f(dot3f(vdx, vdy, vdz, hx, hy, hz));

        float f0_r = 0.04f + (base_r - 0.04f) * metallic;
        float f0_g = 0.04f + (base_g - 0.04f) * metallic;
        float f0_b = 0.04f + (base_b - 0.04f) * metallic;
        float fresnel_w = powf(1.0f - vdh, 5.0f);
        float f_r = f0_r + (1.0f - f0_r) * fresnel_w;
        float f_g = f0_g + (1.0f - f0_g) * fresnel_w;
        float f_b = f0_b + (1.0f - f0_b) * fresnel_w;

        float d = pbr_distribution_ggx(ndh, roughness);
        float gg = pbr_geometry_smith(ndv, ndl, roughness);
        float spec_denom = 4.0f * ndv * ndl + 1e-4f;
        float spec_r = (d * gg * f_r) / spec_denom;
        float spec_g = (d * gg * f_g) / spec_denom;
        float spec_b = (d * gg * f_b) / spec_denom;

        const float inv_pi = 0.31830988618f;
        float kd_r = (1.0f - f_r) * (1.0f - metallic);
        float kd_g = (1.0f - f_g) * (1.0f - metallic);
        float kd_b = (1.0f - f_b) * (1.0f - metallic);
        float diff_r = kd_r * base_r * inv_pi;
        float diff_g = kd_g * base_g * inv_pi;
        float diff_b = kd_b * base_b * inv_pi;

        float radiance_r = lt->color[0] * lt->intensity * la;
        float radiance_g = lt->color[1] * lt->intensity * la;
        float radiance_b = lt->color[2] * lt->intensity * la;
        lit_r += (diff_r + spec_r) * radiance_r * ndl;
        lit_g += (diff_g + spec_g) * radiance_g * ndl;
        lit_b += (diff_b + spec_b) * radiance_b * ndl;
    }

    float emissive_r = cmd->emissive_color[0] * cmd->emissive_intensity;
    float emissive_g = cmd->emissive_color[1] * cmd->emissive_intensity;
    float emissive_b = cmd->emissive_color[2] * cmd->emissive_intensity;
    if (emissive_tex) {
        float er, eg, eb, ea;
        sample_texture_slot_srgb_ex(emissive_tex,
                                    cmd,
                                    RT_MATERIAL3D_TEXTURE_SLOT_EMISSIVE,
                                    uv->emissive_u,
                                    uv->emissive_v,
                                    &er,
                                    &eg,
                                    &eb,
                                    &ea);
        emissive_r *= er;
        emissive_g *= eg;
        emissive_b *= eb;
    }
    lit_r += emissive_r;
    lit_g += emissive_g;
    lit_b += emissive_b;

    if (cmd->shading_model == 1) {
        float bands = cmd->custom_params[0] > 0.5f ? cmd->custom_params[0] : 4.0f;
        lit_r = floorf(lit_r * bands) / bands;
        lit_g = floorf(lit_g * bands) / bands;
        lit_b = floorf(lit_b * bands) / bands;
    } else if (cmd->shading_model == 5) {
        float strength = cmd->custom_params[0] > 0.0f ? cmd->custom_params[0] : 2.0f;
        lit_r += emissive_r * (strength - 1.0f);
        lit_g += emissive_g * (strength - 1.0f);
        lit_b += emissive_b * (strength - 1.0f);
    }

    *fr = lit_r;
    *fg = lit_g;
    *fb_c = lit_b;
}

/// @brief Blinn-Phong lighting for one normal-mapped legacy fragment. Reads the
///   albedo from (*fr,*fg,*fb_c) and overwrites it with the lit color. Samples the
///   specular map; accumulates ambient, per-light diffuse+specular, and emissive
///   contributions; applies the toon/emissive shading models.
static void sw_shade_lighting_phong(const sw_fragment_ctx *fc,
                                    const sw_shade_geom *g,
                                    const sw_slot_uvs *uv,
                                    float *fr,
                                    float *fg,
                                    float *fb_c) {
    const vgfx3d_draw_cmd_t *cmd = fc->cmd;
    const sw_pixels_view *specular_map = fc->specular_map;
    const vgfx3d_light_params_t *lights = fc->lights;
    int32_t light_count = fc->light_count;
    const float *ambient = fc->ambient;
    const sw_context_t *fog_ctx = fc->fog_ctx;
    float pnx = g->nx, pny = g->ny, pnz = g->nz;
    float wx = g->wx, wy = g->wy, wz = g->wz;
    float vdx = g->vx, vdy = g->vy, vdz = g->vz;

    float base_r = *fr;
    float base_g = *fg;
    float base_b = *fb_c;

    /* Ambient */
    float lit_r = (ambient ? ambient[0] : 0.0f) * base_r;
    float lit_g = (ambient ? ambient[1] : 0.0f) * base_g;
    float lit_b = (ambient ? ambient[2] : 0.0f) * base_b;

    /* Specular properties (possibly from specular map) */
    float sp_r = cmd->specular[0];
    float sp_g = cmd->specular[1];
    float sp_b = cmd->specular[2];
    if (specular_map) {
        float smr, smg, smb, sma;
        sample_texture_slot_ex(specular_map,
                               cmd,
                               RT_MATERIAL3D_TEXTURE_SLOT_SPECULAR,
                               uv->specular_u,
                               uv->specular_v,
                               &smr,
                               &smg,
                               &smb,
                               &sma);
        sp_r *= smr;
        sp_g *= smg;
        sp_b *= smb;
    }

    for (int32_t li = 0; li < light_count; li++) {
        const vgfx3d_light_params_t *lt = &lights[li];
        float llx, lly, llz, la = 1.0f;

        if (lt->type != 0 && lt->type != 1 && lt->type != 3) { /* ambient */
            lit_r += lt->color[0] * lt->intensity * base_r;
            lit_g += lt->color[1] * lt->intensity * base_g;
            lit_b += lt->color[2] * lt->intensity * base_b;
            continue;
        }
        if (!sw_eval_light_dir(lt, wx, wy, wz, &llx, &lly, &llz, &la))
            continue;

        if (lt->type == 0 && lt->shadow_index >= 0) {
            int32_t shadow_slot = sw_resolve_shadow_slot(fog_ctx, lt, wx, wy, wz);
            la *= sw_sample_shadow_visibility(fog_ctx, shadow_slot, wx, wy, wz);
        }

        float ndl = pnx * llx + pny * lly + pnz * llz;
        if (ndl < 0.0f)
            ndl = 0.0f;
        float li_i = lt->intensity;
        lit_r += lt->color[0] * li_i * ndl * base_r * la;
        lit_g += lt->color[1] * li_i * ndl * base_g * la;
        lit_b += lt->color[2] * li_i * ndl * base_b * la;

        if (ndl > 0.0f && cmd->shininess > 0.0f) {
            float hx = llx + vdx, hy = lly + vdy, hz = llz + vdz;
            normalize3f(&hx, &hy, &hz);
            float ndh = pnx * hx + pny * hy + pnz * hz;
            if (ndh < 0.0f)
                ndh = 0.0f;
            float spec = powf(ndh, cmd->shininess);
            lit_r += lt->color[0] * li_i * spec * sp_r * la;
            lit_g += lt->color[1] * li_i * spec * sp_g * la;
            lit_b += lt->color[2] * li_i * spec * sp_b * la;
        }
    }

    /* Emissive color base */
    lit_r += cmd->emissive_color[0] * cmd->emissive_intensity;
    lit_g += cmd->emissive_color[1] * cmd->emissive_intensity;
    lit_b += cmd->emissive_color[2] * cmd->emissive_intensity;

    if (cmd->shading_model == 1) {
        float bands = cmd->custom_params[0] > 0.5f ? cmd->custom_params[0] : 4.0f;
        lit_r = floorf(lit_r * bands) / bands;
        lit_g = floorf(lit_g * bands) / bands;
        lit_b = floorf(lit_b * bands) / bands;
    } else if (cmd->shading_model == 5) {
        float strength = cmd->custom_params[0] > 0.0f ? cmd->custom_params[0] : 2.0f;
        lit_r += cmd->emissive_color[0] * cmd->emissive_intensity * (strength - 1.0f);
        lit_g += cmd->emissive_color[1] * cmd->emissive_intensity * (strength - 1.0f);
        lit_b += cmd->emissive_color[2] * cmd->emissive_intensity * (strength - 1.0f);
    }

    *fr = lit_r;
    *fg = lit_g;
    *fb_c = lit_b;
}

/// @brief Blend the fragment color toward the fog color by interpolated camera
///   distance. Caller guards on fog_ctx->fog_enabled.
static void sw_shade_apply_fog(const sw_context_t *fog_ctx,
                               float b0,
                               float b1,
                               float b2,
                               const screen_vert_t *v0,
                               const screen_vert_t *v1,
                               const screen_vert_t *v2,
                               float *fr,
                               float *fg,
                               float *fb_c) {
    float wx = b0 * v0->wx + b1 * v1->wx + b2 * v2->wx;
    float wy = b0 * v0->wy + b1 * v1->wy + b2 * v2->wy;
    float wz = b0 * v0->wz + b1 * v1->wz + b2 * v2->wz;
    float dist = sw_compute_fog_distance(fog_ctx, wx, wy, wz);
    float fog_range = fog_ctx->fog_far - fog_ctx->fog_near;
    float fog_f = (fog_range > 1e-6f) ? (dist - fog_ctx->fog_near) / fog_range : 0.0f;
    fog_f = fog_f < 0.0f ? 0.0f : (fog_f > 1.0f ? 1.0f : fog_f);
    *fr = *fr * (1.0f - fog_f) + fog_ctx->fog_color[0] * fog_f;
    *fg = *fg * (1.0f - fog_f) + fog_ctx->fog_color[1] * fog_f;
    *fb_c = *fb_c * (1.0f - fog_f) + fog_ctx->fog_color[2] * fog_f;
}

/// @brief Apply alpha mode (mask/opaque/blend), the shading_model==4 fresnel-alpha
///   term, then write or blend the fragment into the framebuffer (with optional
///   depth write). Returns 1 if a pixel was written, 0 if discarded.
static int sw_blend_and_write(const sw_fragment_ctx *fc,
                              int x,
                              int y,
                              int idx,
                              float z,
                              float b0,
                              float b1,
                              float b2,
                              float fr,
                              float fg,
                              float fb_c,
                              float tex_alpha,
                              float pixel_ndv) {
    uint8_t *pixels = fc->pixels;
    float *zbuf = fc->zbuf;
    int32_t stride = fc->stride;
    const screen_vert_t *v0 = fc->v0;
    const screen_vert_t *v1 = fc->v1;
    const screen_vert_t *v2 = fc->v2;
    const vgfx3d_draw_cmd_t *cmd = fc->cmd;
    const sw_pixels_view *normal_map = fc->normal_map;
    int depth_disabled = fc->depth_disabled;

    /* Interpolate alpha, respecting material alpha modes. */
    float material_alpha = b0 * v0->a + b1 * v1->a + b2 * v2->a;
    float fa = material_alpha * tex_alpha;
    int discard_fragment = 0;
    if (cmd && cmd->alpha_mode == RT_MATERIAL3D_ALPHA_MODE_MASK) {
        if (fa < cmd->alpha_cutoff)
            discard_fragment = 1;
        fa = 1.0f;
    } else if (cmd && cmd->alpha_mode == RT_MATERIAL3D_ALPHA_MODE_OPAQUE) {
        fa = 1.0f;
    }
    if (cmd && (cmd->workflow == RT_MATERIAL3D_WORKFLOW_PBR || normal_map) &&
        cmd->shading_model == 4) {
        float ndv = pixel_ndv;
        float power = cmd->custom_params[0] > 0.1f ? cmd->custom_params[0] : 3.0f;
        float bias = cmd->custom_params[1];
        float fresnel = powf(1.0f - ndv, power) + bias;
        fa *= clamp01f(fresnel);
    }

    uint8_t *dst = &pixels[y * stride + x * 4];
    if (!discard_fragment && fa >= 1.0f) {
        /* Opaque: overwrite pixel; screen-space overlays skip Z writes. */
        if (!depth_disabled)
            zbuf[idx] = z;
        dst[0] = (uint8_t)(clamp01f(fr) * 255.0f);
        dst[1] = (uint8_t)(clamp01f(fg) * 255.0f);
        dst[2] = (uint8_t)(clamp01f(fb_c) * 255.0f);
        dst[3] = 0xFF;
        return 1;
    } else if (!discard_fragment && fa > 0.0f) {
        /* Transparent: additive keeps the destination, alpha uses source-over.
         * Both skip Z writes so they don't occlude later transparent draws. */
        if (cmd && cmd->additive_blend) {
            dst[0] = (uint8_t)fminf(255.0f, clamp01f(fr) * 255.0f * fa + (float)dst[0]);
            dst[1] = (uint8_t)fminf(255.0f, clamp01f(fg) * 255.0f * fa + (float)dst[1]);
            dst[2] = (uint8_t)fminf(255.0f, clamp01f(fb_c) * 255.0f * fa + (float)dst[2]);
        } else {
            float inv_a = 1.0f - fa;
            dst[0] = (uint8_t)(clamp01f(fr) * 255.0f * fa + (float)dst[0] * inv_a);
            dst[1] = (uint8_t)(clamp01f(fg) * 255.0f * fa + (float)dst[1] * inv_a);
            dst[2] = (uint8_t)(clamp01f(fb_c) * 255.0f * fa + (float)dst[2] * inv_a);
        }
        dst[3] = 0xFF;
        return 1;
    }
    /* else: alpha <= 0 → fully invisible, skip */

    return 0;
}

/// @brief Shade and write one fragment that already passed the in-triangle and
///   depth tests. Resolves albedo (base/splat/emissive), then per-pixel normal
///   mapping and PBR or Phong lighting with shadows, environment reflection,
///   distance fog, and alpha-mode-aware blending into the framebuffer.
/// @param fc Per-triangle invariants (see sw_fragment_ctx).
/// @param x,y Pixel coordinates; idx the z-buffer index; z the interpolated depth.
/// @param b0,b1,b2 Barycentric weights of the pixel within the triangle.
/// @return 1 if a pixel was written (opaque or blended), 0 if discarded.
static int sw_shade_fragment(
    const sw_fragment_ctx *fc, int x, int y, int idx, float z, float b0, float b1, float b2) {
    const screen_vert_t *v0 = fc->v0;
    const screen_vert_t *v1 = fc->v1;
    const screen_vert_t *v2 = fc->v2;
    const sw_pixels_view *emissive_tex = fc->emissive_tex;
    const sw_pixels_view *normal_map = fc->normal_map;
    const sw_pixels_view *specular_map = fc->specular_map;
    const sw_pixels_view *metallic_roughness_map = fc->metallic_roughness_map;
    const sw_pixels_view *ao_map = fc->ao_map;
    const vgfx3d_draw_cmd_t *cmd = fc->cmd;
    const sw_context_t *fog_ctx = fc->fog_ctx;

    float fr, fg, fb_c, tex_alpha;
    sw_shade_resolve_albedo(fc, b0, b1, b2, &fr, &fg, &fb_c, &tex_alpha);

    /* Per-pixel lighting for PBR or normal-mapped legacy materials. */
    float pixel_ndv = 1.0f;
    /* Final shading normal; reused by the env-reflection pass below to avoid
     * recomputing the TBN a second time. */
    float shading_normal[3] = {0.0f, 0.0f, 0.0f};
    int shading_normal_valid = 0;
    if (cmd && !cmd->unlit && (cmd->workflow == RT_MATERIAL3D_WORKFLOW_PBR || normal_map)) {
        float pp_iw = b0 * v0->inv_w + b1 * v1->inv_w + b2 * v2->inv_w;
        if (fabsf(pp_iw) > 1e-7f) {
            sw_slot_uvs uv = {0};
            sw_interpolate_uv_for_slot(cmd,
                                       RT_MATERIAL3D_TEXTURE_SLOT_NORMAL,
                                       b0,
                                       b1,
                                       b2,
                                       v0,
                                       v1,
                                       v2,
                                       &uv.normal_u,
                                       &uv.normal_v);
            if (specular_map)
                sw_interpolate_uv_for_slot(cmd,
                                           RT_MATERIAL3D_TEXTURE_SLOT_SPECULAR,
                                           b0,
                                           b1,
                                           b2,
                                           v0,
                                           v1,
                                           v2,
                                           &uv.specular_u,
                                           &uv.specular_v);
            if (emissive_tex)
                sw_interpolate_uv_for_slot(cmd,
                                           RT_MATERIAL3D_TEXTURE_SLOT_EMISSIVE,
                                           b0,
                                           b1,
                                           b2,
                                           v0,
                                           v1,
                                           v2,
                                           &uv.emissive_u,
                                           &uv.emissive_v);
            if (metallic_roughness_map)
                sw_interpolate_uv_for_slot(cmd,
                                           RT_MATERIAL3D_TEXTURE_SLOT_METALLIC_ROUGHNESS,
                                           b0,
                                           b1,
                                           b2,
                                           v0,
                                           v1,
                                           v2,
                                           &uv.mr_u,
                                           &uv.mr_v);
            if (ao_map)
                sw_interpolate_uv_for_slot(
                    cmd, RT_MATERIAL3D_TEXTURE_SLOT_AO, b0, b1, b2, v0, v1, v2, &uv.ao_u, &uv.ao_v);

            sw_shade_geom geom;
            sw_shade_perturb_normal(
                fc, b0, b1, b2, uv.normal_u, uv.normal_v, &geom.nx, &geom.ny, &geom.nz);
            shading_normal[0] = geom.nx;
            shading_normal[1] = geom.ny;
            shading_normal[2] = geom.nz;
            shading_normal_valid = 1;

            geom.wx = b0 * v0->wx + b1 * v1->wx + b2 * v2->wx;
            geom.wy = b0 * v0->wy + b1 * v1->wy + b2 * v2->wy;
            geom.wz = b0 * v0->wz + b1 * v1->wz + b2 * v2->wz;
            sw_compute_view_vector(
                fog_ctx, geom.wx, geom.wy, geom.wz, &geom.vx, &geom.vy, &geom.vz);
            geom.ndv = clamp01f(dot3f(geom.nx, geom.ny, geom.nz, geom.vx, geom.vy, geom.vz));
            pixel_ndv = geom.ndv;

            if (cmd->workflow == RT_MATERIAL3D_WORKFLOW_PBR)
                sw_shade_lighting_pbr(fc, &geom, &uv, &fr, &fg, &fb_c);
            else
                sw_shade_lighting_phong(fc, &geom, &uv, &fr, &fg, &fb_c);
        }
    }

    if (cmd && cmd->env_map && cmd->reflectivity > 0.0001f) {
        sw_apply_environment_reflection(cmd,
                                        normal_map,
                                        metallic_roughness_map,
                                        b0,
                                        b1,
                                        b2,
                                        v0,
                                        v1,
                                        v2,
                                        fog_ctx,
                                        shading_normal_valid ? shading_normal : NULL,
                                        &fr,
                                        &fg,
                                        &fb_c);
    }

    /* Distance fog — interpolate world position, compute camera distance */
    if (fog_ctx && fog_ctx->fog_enabled)
        sw_shade_apply_fog(fog_ctx, b0, b1, b2, v0, v1, v2, &fr, &fg, &fb_c);

    return sw_blend_and_write(fc, x, y, idx, z, b0, b1, b2, fr, fg, fb_c, tex_alpha, pixel_ndv);
}

/// @brief Edge-function triangle rasterizer with PBR + Phong + shadow + fog support.
///
/// Centerpiece of the software backend. Steps:
///   1. Backface cull / winding flip via signed triangle area.
///   2. Compute screen-space bounding box, clip to viewport.
///   3. For each pixel: barycentric weights → in-triangle test → Z
///      compare → texture sample → lighting (Phong or PBR Cook-Torrance)
///      → shadow lookup → fog blend → alpha blend → write.
/// Honours every flag on `cmd` (unlit, alpha mode, normal map, etc.).
/// Optimization: perspective-correct attribute interpolation via
/// `u/w`, `v/w`, `1/w` vertex attributes.
/// @brief Tri-count gate shared by raster_triangle's reject + summary debug logs.
static int g_sw_debug_tri_count = 0;

/// @brief Computed rasterization setup for one screen-space triangle: the
///   (possibly winding-swapped) vertices, clipped bounding box, inverse area, and
///   half-space edge-function deltas + first-pixel values.
typedef struct {
    const screen_vert_t *v0, *v1, *v2;
    int min_x, max_x, min_y, max_y;
    float area, original_area, inv_area;
    float e12_dx, e12_dy, e20_dx, e20_dy, e01_dx, e01_dy;
    float row_w0, row_w1, row_w2;
} sw_raster_geom;

/// @brief Backface-cull, fix winding, reject degenerate/offscreen triangles, and
///   compute the bounding box + half-space edge functions. Returns 1 with *g
///   filled when the triangle should be rasterized, or 0 (emitting the matching
///   reject debug line when emit_debug is set) when it is rejected.
static int sw_raster_prepare(const screen_vert_t *v0,
                             const screen_vert_t *v1,
                             const screen_vert_t *v2,
                             int32_t fb_w,
                             int32_t fb_h,
                             int8_t backface_cull,
                             int emit_debug,
                             sw_raster_geom *g) {
    float area = (v1->sx - v0->sx) * (v2->sy - v0->sy) - (v2->sx - v0->sx) * (v1->sy - v0->sy);
    float original_area = area;

    const float area_epsilon = 1e-6f;

    if (fabsf(area) <= area_epsilon) {
        if (emit_debug) {
            fprintf(stderr,
                    "[sw3d] tri %d degenerate area=%.6f orig=%.6f\n",
                    g_sw_debug_tri_count++,
                    fabsf(area),
                    original_area);
        }
        return 0;
    }

    /* After viewport Y-flip, CCW world-space triangles have NEGATIVE screen-space
     * area. So negative area = front face, positive area = back face.
     * Cull back faces (positive area) when backface culling is enabled. */
    if (backface_cull && area > area_epsilon) {
        if (emit_debug) {
            fprintf(stderr,
                    "[sw3d] tri %d culled area=%.3f v0=(%.2f %.2f %.3f) v1=(%.2f %.2f %.3f) "
                    "v2=(%.2f %.2f %.3f)\n",
                    g_sw_debug_tri_count++,
                    original_area,
                    v0->sx,
                    v0->sy,
                    v0->sz,
                    v1->sx,
                    v1->sy,
                    v1->sz,
                    v2->sx,
                    v2->sy,
                    v2->sz);
        }
        return 0;
    }
    if (area < 0.0f) {
        const screen_vert_t *tmp = v1;
        v1 = v2;
        v2 = tmp;
        area = -area;
    }

    int min_x = (int)fmaxf(fminf(fminf(v0->sx, v1->sx), v2->sx), 0.0f);
    int max_x = (int)fminf(fmaxf(fmaxf(v0->sx, v1->sx), v2->sx), (float)(fb_w - 1));
    int min_y = (int)fmaxf(fminf(fminf(v0->sy, v1->sy), v2->sy), 0.0f);
    int max_y = (int)fminf(fmaxf(fmaxf(v0->sy, v1->sy), v2->sy), (float)(fb_h - 1));
    if (min_x > max_x || min_y > max_y) {
        if (emit_debug) {
            fprintf(stderr,
                    "[sw3d] tri %d clipped bbox=(%d..%d,%d..%d) area=%.3f orig=%.3f\n",
                    g_sw_debug_tri_count++,
                    min_x,
                    max_x,
                    min_y,
                    max_y,
                    area,
                    original_area);
        }
        return 0;
    }

    /* Half-space edge functions for rasterization.
     * w0/w1/w2 are the signed distances from pixel center to each edge.
     * A pixel is inside when all three are >= 0. The edge function increments
     * by -dy/+dx per pixel step, enabling efficient scanline traversal. */
    float e12_dx = v2->sx - v1->sx, e12_dy = v2->sy - v1->sy;
    float e20_dx = v0->sx - v2->sx, e20_dy = v0->sy - v2->sy;
    float e01_dx = v1->sx - v0->sx, e01_dy = v1->sy - v0->sy;
    float px0 = (float)min_x + 0.5f, py0 = (float)min_y + 0.5f;

    g->v0 = v0;
    g->v1 = v1;
    g->v2 = v2;
    g->min_x = min_x;
    g->max_x = max_x;
    g->min_y = min_y;
    g->max_y = max_y;
    g->area = area;
    g->original_area = original_area;
    g->inv_area = 1.0f / area;
    g->e12_dx = e12_dx;
    g->e12_dy = e12_dy;
    g->e20_dx = e20_dx;
    g->e20_dy = e20_dy;
    g->e01_dx = e01_dx;
    g->e01_dy = e01_dy;
    g->row_w0 = e12_dx * (py0 - v1->sy) - e12_dy * (px0 - v1->sx);
    g->row_w1 = e20_dx * (py0 - v2->sy) - e20_dy * (px0 - v2->sx);
    g->row_w2 = e01_dx * (py0 - v0->sy) - e01_dy * (px0 - v0->sx);
    return 1;
}

/// @brief Rasterize one screen-space triangle into the framebuffer with depth testing and
///   per-pixel shading, sampling the bound albedo/emissive/normal/specular/metallic-roughness/
///   AO maps. This is the software backend's inner fill loop.
static void raster_triangle(uint8_t *pixels,
                            float *zbuf,
                            int32_t fb_w,
                            int32_t fb_h,
                            int32_t stride,
                            const screen_vert_t *v0,
                            const screen_vert_t *v1,
                            const screen_vert_t *v2,
                            const sw_pixels_view *tex,
                            const sw_pixels_view *emissive_tex,
                            const float *emissive_color,
                            const sw_pixels_view *normal_map,
                            const sw_pixels_view *specular_map,
                            const sw_pixels_view *metallic_roughness_map,
                            const sw_pixels_view *ao_map,
                            const vgfx3d_draw_cmd_t *cmd,
                            const vgfx3d_light_params_t *lights,
                            int32_t light_count,
                            const float *ambient,
                            int8_t backface_cull,
                            const sw_context_t *fog_ctx) {
    int emit_debug = sw_debug_enabled() && g_sw_debug_tri_count < 16;
    sw_raster_geom g;
    if (!sw_raster_prepare(v0, v1, v2, fb_w, fb_h, backface_cull, emit_debug, &g))
        return;

    const int depth_disabled = cmd && cmd->disable_depth_test;
    const float depth_bias =
        cmd ? fmaxf(-0.05f, fminf(0.05f, cmd->depth_bias)) : 0.0f;
    float slope_depth_bias = 0.0f;
    if (cmd && fabsf(cmd->slope_scaled_depth_bias) > 1e-8f) {
        float denom = g.v0->sx * (g.v1->sy - g.v2->sy) +
                      g.v1->sx * (g.v2->sy - g.v0->sy) +
                      g.v2->sx * (g.v0->sy - g.v1->sy);
        if (fabsf(denom) > 1e-8f) {
            float dzdx = (g.v0->sz * (g.v1->sy - g.v2->sy) +
                          g.v1->sz * (g.v2->sy - g.v0->sy) +
                          g.v2->sz * (g.v0->sy - g.v1->sy)) /
                         denom;
            float dzdy = (g.v0->sz * (g.v2->sx - g.v1->sx) +
                          g.v1->sz * (g.v0->sx - g.v2->sx) +
                          g.v2->sz * (g.v1->sx - g.v0->sx)) /
                         denom;
            slope_depth_bias = cmd->slope_scaled_depth_bias * fmaxf(fabsf(dzdx), fabsf(dzdy));
            slope_depth_bias = fmaxf(-0.05f, fminf(0.05f, slope_depth_bias));
        }
    }
    const float edge_epsilon = -1e-5f;
    int inside_samples = 0;
    int depth_passes = 0;
    int pixels_written = 0;

    /* Terrain-splat views are constant across the whole triangle; resolve them
     * once here instead of re-opening the five Pixels views for every pixel. */
    sw_pixels_view splat_view;
    sw_pixels_view layer_views[4];
    int have_splat = (cmd && cmd->has_splat && cmd->splat_map)
                         ? sw_setup_complete_splat(cmd, &splat_view, layer_views)
                         : 0;

    sw_fragment_ctx fc = {.pixels = pixels,
                          .zbuf = zbuf,
                          .stride = stride,
                          .v0 = g.v0,
                          .v1 = g.v1,
                          .v2 = g.v2,
                          .tex = tex,
                          .emissive_tex = emissive_tex,
                          .emissive_color = emissive_color,
                          .normal_map = normal_map,
                          .specular_map = specular_map,
                          .metallic_roughness_map = metallic_roughness_map,
                          .ao_map = ao_map,
                          .cmd = cmd,
                          .lights = lights,
                          .light_count = light_count,
                          .ambient = ambient,
                          .fog_ctx = fog_ctx,
                          .depth_disabled = depth_disabled,
                          .have_splat = have_splat,
                          .splat_view = &splat_view,
                          .layer_views = layer_views};

    float row_w0 = g.row_w0, row_w1 = g.row_w1, row_w2 = g.row_w2;
    for (int y = g.min_y; y <= g.max_y; y++) {
        float w0 = row_w0, w1 = row_w1, w2 = row_w2;
        for (int x = g.min_x; x <= g.max_x; x++) {
            if (w0 >= edge_epsilon && w1 >= edge_epsilon && w2 >= edge_epsilon) {
                inside_samples++;
                /* Barycentric weights from edge functions; z is linearly
                 * interpolated in screen space (not perspective-correct,
                 * but sufficient for depth testing). */
                float b0 = w0 * g.inv_area, b1 = w1 * g.inv_area, b2 = w2 * g.inv_area;
                float z = b0 * g.v0->sz + b1 * g.v1->sz + b2 * g.v2->sz;
                z += depth_bias + slope_depth_bias;
                int idx = y * fb_w + x;
                if (depth_disabled || z < zbuf[idx]) {
                    depth_passes++;
                    pixels_written += sw_shade_fragment(&fc, x, y, idx, z, b0, b1, b2);
                }
            }
            w0 -= g.e12_dy;
            w1 -= g.e20_dy;
            w2 -= g.e01_dy;
        }
        row_w0 += g.e12_dx;
        row_w1 += g.e20_dx;
        row_w2 += g.e01_dx;
    }

    if (emit_debug) {
        fprintf(stderr,
                "[sw3d] tri %d area=%.3f orig=%.3f bbox=(%d..%d,%d..%d) inside=%d depth=%d "
                "written=%d v0=(%.2f %.2f %.3f) v1=(%.2f %.2f %.3f) v2=(%.2f %.2f %.3f)\n",
                g_sw_debug_tri_count++,
                g.area,
                g.original_area,
                g.min_x,
                g.max_x,
                g.min_y,
                g.max_y,
                inside_samples,
                depth_passes,
                pixels_written,
                g.v0->sx,
                g.v0->sy,
                g.v0->sz,
                g.v1->sx,
                g.v1->sy,
                g.v1->sz,
                g.v2->sx,
                g.v2->sy,
                g.v2->sz);
    }
}

/*==========================================================================
 * Wireframe — Bresenham line
 *=========================================================================*/

/// @brief Bresenham-style line rasterizer used for wireframe mode.
///
/// Walks the longer of dx or dy, stepping the shorter axis via an
/// error-accumulator. No anti-aliasing — we trade quality for the
/// software backend's per-pixel speed.
static void draw_line(uint8_t *pixels,
                      int32_t fb_w,
                      int32_t fb_h,
                      int32_t stride,
                      int x0,
                      int y0,
                      int x1,
                      int y1,
                      uint8_t r,
                      uint8_t g,
                      uint8_t b) {
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        if (x0 >= 0 && x0 < fb_w && y0 >= 0 && y0 < fb_h) {
            uint8_t *dst = &pixels[y0 * stride + x0 * 4];
            dst[0] = r;
            dst[1] = g;
            dst[2] = b;
            dst[3] = 0xFF;
        }
        if (x0 == x1 && y0 == y1)
            break;
        int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

/*==========================================================================
 * Shadow map — depth-only rasterization
 *=========================================================================*/

/// @brief Rasterize a single triangle into a depth buffer (no color, no lighting).
/// @brief Depth-only triangle rasterizer for the shadow map pass.
///
/// Slimmer than `raster_triangle` — only writes Z (no color, lighting,
/// or texturing). Same edge-function approach.
static void shadow_raster_tri(float *depth,
                              int32_t sw,
                              int32_t sh,
                              float *sx,
                              float *sy,
                              float *sz,
                              const float *su,
                              const float *sv,
                              const float *su1,
                              const float *sv1,
                              const float *sa,
                              const vgfx3d_draw_cmd_t *cmd,
                              const sw_pixels_view *alpha_tex) {
    float u[3] = {su ? su[0] : 0.0f, su ? su[1] : 0.0f, su ? su[2] : 0.0f};
    float vcoord[3] = {sv ? sv[0] : 0.0f, sv ? sv[1] : 0.0f, sv ? sv[2] : 0.0f};
    float u1[3] = {su1 ? su1[0] : 0.0f, su1 ? su1[1] : 0.0f, su1 ? su1[2] : 0.0f};
    float v1coord[3] = {sv1 ? sv1[0] : 0.0f, sv1 ? sv1[1] : 0.0f, sv1 ? sv1[2] : 0.0f};
    float alpha_v[3] = {sa ? sa[0] : 1.0f, sa ? sa[1] : 1.0f, sa ? sa[2] : 1.0f};
    /* Screen-space area (winding check) */
    float area = (sx[1] - sx[0]) * (sy[2] - sy[0]) - (sx[2] - sx[0]) * (sy[1] - sy[0]);
    if (area < 0.0f) {
        /* Swap v1/v2 to ensure positive area */
        float t;
        t = sx[1];
        sx[1] = sx[2];
        sx[2] = t;
        t = sy[1];
        sy[1] = sy[2];
        sy[2] = t;
        t = sz[1];
        sz[1] = sz[2];
        sz[2] = t;
        t = u[1];
        u[1] = u[2];
        u[2] = t;
        t = vcoord[1];
        vcoord[1] = vcoord[2];
        vcoord[2] = t;
        t = u1[1];
        u1[1] = u1[2];
        u1[2] = t;
        t = v1coord[1];
        v1coord[1] = v1coord[2];
        v1coord[2] = t;
        t = alpha_v[1];
        alpha_v[1] = alpha_v[2];
        alpha_v[2] = t;
        area = -area;
    }
    if (area < 1e-6f)
        return;

    float inv_area = 1.0f / area;
    int min_x = (int)fmaxf(fminf(fminf(sx[0], sx[1]), sx[2]), 0.0f);
    int max_x = (int)fminf(fmaxf(fmaxf(sx[0], sx[1]), sx[2]), (float)(sw - 1));
    int min_y = (int)fmaxf(fminf(fminf(sy[0], sy[1]), sy[2]), 0.0f);
    int max_y = (int)fminf(fmaxf(fmaxf(sy[0], sy[1]), sy[2]), (float)(sh - 1));
    if (min_x > max_x || min_y > max_y)
        return;

    float e12_dx = sx[2] - sx[1], e12_dy = sy[2] - sy[1];
    float e20_dx = sx[0] - sx[2], e20_dy = sy[0] - sy[2];
    float e01_dx = sx[1] - sx[0], e01_dy = sy[1] - sy[0];
    float px0 = (float)min_x + 0.5f, py0 = (float)min_y + 0.5f;
    float row_w0 = e12_dx * (py0 - sy[1]) - e12_dy * (px0 - sx[1]);
    float row_w1 = e20_dx * (py0 - sy[2]) - e20_dy * (px0 - sx[2]);
    float row_w2 = e01_dx * (py0 - sy[0]) - e01_dy * (px0 - sx[0]);
    const float edge_epsilon = -1e-5f;

    for (int y = min_y; y <= max_y; y++) {
        float w0 = row_w0, w1 = row_w1, w2 = row_w2;
        for (int x = min_x; x <= max_x; x++) {
            if (w0 >= edge_epsilon && w1 >= edge_epsilon && w2 >= edge_epsilon) {
                float b0 = w0 * inv_area, b1 = w1 * inv_area, b2 = w2 * inv_area;
                float z = b0 * sz[0] + b1 * sz[1] + b2 * sz[2];
                int idx = y * sw + x;
                if (cmd && cmd->alpha_mode == RT_MATERIAL3D_ALPHA_MODE_MASK) {
                    float alpha = cmd->diffuse_color[3] * cmd->alpha *
                                  (b0 * alpha_v[0] + b1 * alpha_v[1] + b2 * alpha_v[2]);
                    if (alpha_tex) {
                        float tr, tg, tb, ta;
                        int use_uv1 =
                            cmd->texture_slot_uv_set[RT_MATERIAL3D_TEXTURE_SLOT_BASE_COLOR] > 0;
                        const float *m =
                            cmd->texture_slot_uv_transform[RT_MATERIAL3D_TEXTURE_SLOT_BASE_COLOR];
                        float base_u = use_uv1 ? (b0 * u1[0] + b1 * u1[1] + b2 * u1[2])
                                               : (b0 * u[0] + b1 * u[1] + b2 * u[2]);
                        float base_v = use_uv1
                                           ? (b0 * v1coord[0] + b1 * v1coord[1] + b2 * v1coord[2])
                                           : (b0 * vcoord[0] + b1 * vcoord[1] + b2 * vcoord[2]);
                        float tex_u = base_u * m[0] + base_v * m[1] + m[4];
                        float tex_v = base_u * m[2] + base_v * m[3] + m[5];
                        sample_texture_slot_ex(alpha_tex,
                                               cmd,
                                               RT_MATERIAL3D_TEXTURE_SLOT_BASE_COLOR,
                                               tex_u,
                                               tex_v,
                                               &tr,
                                               &tg,
                                               &tb,
                                               &ta);
                        alpha *= ta;
                    }
                    if (alpha < cmd->alpha_cutoff)
                        goto next_shadow_pixel;
                }
                if (z < depth[idx])
                    depth[idx] = z;
            }
        next_shadow_pixel:
            w0 -= e12_dy;
            w1 -= e20_dy;
            w2 -= e01_dy;
        }
        row_w0 += e12_dx;
        row_w1 += e20_dx;
        row_w2 += e01_dx;
    }
}

typedef struct {
    float clip[4];
    float uv[2];
    float uv1[2];
    float color_alpha;
} shadow_clip_vertex_t;

/// @brief Signed distance from a clip-space vertex to the @p plane-th frustum plane.
/// @details Plane indexing (0..5) is left, right, bottom, top, near, far. Positive
///   results mean the vertex is on the inside half-space of that plane. Used by the
///   shadow-pass Sutherland-Hodgman polygon clipper to test which side each input
///   vertex falls on.
static float shadow_clip_distance(const shadow_clip_vertex_t *v, int plane) {
    switch (plane) {
        case 0:
            return v->clip[0] + v->clip[3]; /* left */
        case 1:
            return v->clip[3] - v->clip[0]; /* right */
        case 2:
            return v->clip[1] + v->clip[3]; /* bottom */
        case 3:
            return v->clip[3] - v->clip[1]; /* top */
        case 4:
            return v->clip[2] + v->clip[3]; /* near */
        default:
            return v->clip[3] - v->clip[2]; /* far */
    }
}

/// @brief Linearly interpolate two clip-space vertices at parameter @p t.
/// @details Used by the shadow polygon clipper to construct the new vertex generated
///   when an edge crosses a clip plane. The interpolation runs on the homogeneous
///   clip-space coordinates so the resulting position lies on the original edge.
static shadow_clip_vertex_t shadow_clip_lerp(const shadow_clip_vertex_t *a,
                                             const shadow_clip_vertex_t *b,
                                             float t) {
    shadow_clip_vertex_t out;
    for (int i = 0; i < 4; i++)
        out.clip[i] = a->clip[i] + (b->clip[i] - a->clip[i]) * t;
    out.uv[0] = a->uv[0] + (b->uv[0] - a->uv[0]) * t;
    out.uv[1] = a->uv[1] + (b->uv[1] - a->uv[1]) * t;
    out.uv1[0] = a->uv1[0] + (b->uv1[0] - a->uv1[0]) * t;
    out.uv1[1] = a->uv1[1] + (b->uv1[1] - a->uv1[1]) * t;
    out.color_alpha = a->color_alpha + (b->color_alpha - a->color_alpha) * t;
    return out;
}

/// @brief Sutherland-Hodgman clip a polygon in 4D clip space against the 6 frustum planes.
/// @details Walks each plane in turn; for every input edge, emits the entry intersection
///   when one endpoint is outside, and emits the inside endpoint unchanged. The output
///   polygon replaces the input via a `tmp[]` swap buffer (max 16 vertices). Returns the
///   final clipped polygon vertex count, with the result written back to @p poly. Used
///   by the shadow-pass shadow-map rasterizer to clip triangles against the shadow frustum.
static int shadow_clip_polygon(shadow_clip_vertex_t *poly, int count) {
    shadow_clip_vertex_t tmp[16];
    for (int plane = 0; plane < 6; plane++) {
        int out_count = 0;
        if (count <= 0)
            return 0;
        shadow_clip_vertex_t prev = poly[count - 1];
        float prev_d = shadow_clip_distance(&prev, plane);
        int prev_inside = prev_d >= 0.0f;
        for (int i = 0; i < count; i++) {
            shadow_clip_vertex_t cur = poly[i];
            float cur_d = shadow_clip_distance(&cur, plane);
            int cur_inside = cur_d >= 0.0f;
            if (cur_inside != prev_inside) {
                float denom = prev_d - cur_d;
                float t = fabsf(denom) > 1e-8f ? prev_d / denom : 0.0f;
                if (out_count < (int)(sizeof(tmp) / sizeof(tmp[0])))
                    tmp[out_count++] = shadow_clip_lerp(&prev, &cur, t);
            }
            if (cur_inside && out_count < (int)(sizeof(tmp) / sizeof(tmp[0])))
                tmp[out_count++] = cur;
            prev = cur;
            prev_d = cur_d;
            prev_inside = cur_inside;
        }
        if (out_count <= 0)
            return 0;
        memcpy(poly, tmp, (size_t)out_count * sizeof(*poly));
        count = out_count;
    }
    return count;
}

/// @brief Backend `shadow_begin` op — allocate / clear the shadow depth buffer.
///
/// Caller-provided depth buffer (so the canvas owns the storage and
/// can resize without consulting the backend). Captures the light VP
/// matrix that `shadow_draw` will use.
static void sw_shadow_begin(
    void *ctx_ptr, int32_t slot, float *depth_buf, int32_t w, int32_t h, const float *light_vp) {
    sw_context_t *ctx = (sw_context_t *)ctx_ptr;
    size_t pixel_count;
    if (!ctx)
        return;
    ctx->shadow_pass_slot = -1;
    if (slot < 0 || slot >= VGFX3D_MAX_SHADOW_LIGHTS)
        return;
    ctx->shadow_complete[slot] = 0;
    if (!depth_buf || !light_vp || w <= 0 || h <= 0) {
        sw_recompute_shadow_count(ctx);
        return;
    }
    if ((size_t)w > SIZE_MAX / (size_t)h) {
        sw_recompute_shadow_count(ctx);
        return;
    }
    pixel_count = (size_t)w * (size_t)h;
    if (pixel_count > (size_t)INT32_MAX) {
        sw_recompute_shadow_count(ctx);
        return;
    }
    ctx->shadow_pass_slot = (int8_t)slot;
    ctx->shadow_depth[slot] = depth_buf;
    ctx->shadow_w[slot] = w;
    ctx->shadow_h[slot] = h;
    memcpy(ctx->shadow_vp[slot], light_vp, 16 * sizeof(float));
    /* Clear shadow depth to FLT_MAX */
    for (size_t i = 0; i < pixel_count; i++)
        depth_buf[i] = FLT_MAX;
}

/// @brief Backend `shadow_draw` op — rasterize one mesh into the shadow depth buffer.
///
/// Same vertex transform path as `submit_draw` (model matrix + light VP)
/// but feeds each triangle to `shadow_raster_tri` instead of the full
/// rasterizer. Triangles are clipped in homogeneous light clip-space before
/// perspective divide so near-plane crossings cannot poison the shadow map.
static void sw_shadow_draw(void *ctx_ptr, const vgfx3d_draw_cmd_t *cmd) {
    sw_context_t *ctx = (sw_context_t *)ctx_ptr;
    int32_t slot;
    sw_pixels_view alpha_view;
    sw_pixels_view *alpha_tex = NULL;
    if (!ctx || !cmd || !cmd->vertices || !cmd->indices || cmd->vertex_count == 0 ||
        cmd->index_count == 0)
        return;
    slot = ctx->shadow_pass_slot;
    if (slot < 0 || slot >= VGFX3D_MAX_SHADOW_LIGHTS || !ctx->shadow_depth[slot])
        return;
    /* Skip transparent objects */
    if (cmd->additive_blend || vgfx3d_draw_cmd_uses_alpha_blend(cmd))
        return;
    if (cmd->alpha_mode == RT_MATERIAL3D_ALPHA_MODE_MASK && cmd->texture) {
        const sw_pixels_view *pv = (const sw_pixels_view *)cmd->texture;
        alpha_view = *pv;
        if (alpha_view.width > 0 && alpha_view.height > 0 && alpha_view.data)
            alpha_tex = &alpha_view;
    }

    /* Build light MVP = shadow_vp * model */
    float lmvp[16];
    mat4f_mul(ctx->shadow_vp[slot], cmd->model_matrix, lmvp);

    float half_w = (float)ctx->shadow_w[slot] * 0.5f;
    float half_h = (float)ctx->shadow_h[slot] * 0.5f;

    for (uint32_t i = 0; i + 2 < cmd->index_count; i += 3) {
        uint32_t i0 = cmd->indices[i], i1 = cmd->indices[i + 1], i2 = cmd->indices[i + 2];
        if (i0 >= cmd->vertex_count || i1 >= cmd->vertex_count || i2 >= cmd->vertex_count)
            continue;

        shadow_clip_vertex_t clipped[16];
        for (int vi = 0; vi < 3; vi++) {
            const uint32_t idx[3] = {i0, i1, i2};
            const vgfx3d_vertex_t *v = &cmd->vertices[idx[vi]];
            float pos4[4] = {v->pos[0], v->pos[1], v->pos[2], 1.0f};
            mat4f_transform4(lmvp, pos4, clipped[vi].clip);
            clipped[vi].uv[0] = v->uv[0];
            clipped[vi].uv[1] = v->uv[1];
            clipped[vi].uv1[0] = v->uv1[0];
            clipped[vi].uv1[1] = v->uv1[1];
            clipped[vi].color_alpha = v->color[3];
        }
        int clipped_count = shadow_clip_polygon(clipped, 3);
        if (clipped_count < 3)
            continue;

        for (int tri = 1; tri + 1 < clipped_count; tri++) {
            const shadow_clip_vertex_t *fan[3] = {&clipped[0], &clipped[tri], &clipped[tri + 1]};
            float screen_x[3], screen_y[3], screen_z[3];
            float tex_u[3], tex_v[3], tex_u1[3], tex_v1[3], vert_alpha[3];
            int ok = 1;
            for (int vi = 0; vi < 3; vi++) {
                float w = fan[vi]->clip[3];
                if (w <= 1e-7f || !isfinite(w)) {
                    ok = 0;
                    break;
                }
                float iw = 1.0f / w;
                float ndc_x = fan[vi]->clip[0] * iw;
                float ndc_y = fan[vi]->clip[1] * iw;
                float ndc_z = fan[vi]->clip[2] * iw;
                if (!isfinite(ndc_x) || !isfinite(ndc_y) || !isfinite(ndc_z)) {
                    ok = 0;
                    break;
                }
                screen_x[vi] = (ndc_x + 1.0f) * half_w;
                screen_y[vi] = (1.0f - ndc_y) * half_h;
                screen_z[vi] = ndc_z * 0.5f + 0.5f;
                if (screen_z[vi] < 0.0f || screen_z[vi] > 1.0f) {
                    ok = 0;
                    break;
                }
                tex_u[vi] = fan[vi]->uv[0];
                tex_v[vi] = fan[vi]->uv[1];
                tex_u1[vi] = fan[vi]->uv1[0];
                tex_v1[vi] = fan[vi]->uv1[1];
                vert_alpha[vi] = fan[vi]->color_alpha;
            }
            if (!ok)
                continue;
            shadow_raster_tri(ctx->shadow_depth[slot],
                              ctx->shadow_w[slot],
                              ctx->shadow_h[slot],
                              screen_x,
                              screen_y,
                              screen_z,
                              tex_u,
                              tex_v,
                              tex_u1,
                              tex_v1,
                              vert_alpha,
                              cmd,
                              alpha_tex);
        }
    }
}

/// @brief Backend `shadow_end` op — mark shadow map ready and store the bias.
static void sw_shadow_end(void *ctx_ptr, int32_t slot, float bias) {
    sw_context_t *ctx = (sw_context_t *)ctx_ptr;
    if (!ctx || slot < 0 || slot >= VGFX3D_MAX_SHADOW_LIGHTS)
        return;
    if (ctx->shadow_pass_slot != slot)
        return;
    ctx->shadow_bias = bias;
    ctx->shadow_complete[slot] =
        (ctx->shadow_depth[slot] && ctx->shadow_w[slot] > 0 && ctx->shadow_h[slot] > 0) ? 1 : 0;
    ctx->shadow_pass_slot = -1;
    sw_recompute_shadow_count(ctx);
}

/*==========================================================================
 * Backend vtable implementation
 *=========================================================================*/

/// @brief Backend `create_ctx` op — allocate the software-renderer state struct.
///
/// Trivial compared to hardware backends — no shaders, no FBOs, no
/// device. Just a `sw_context_t` with a deferred-allocated zbuffer.
static void *sw_create_ctx(vgfx_window_t win, int32_t w, int32_t h) {
    sw_context_t *ctx = (sw_context_t *)calloc(1, sizeof(sw_context_t));
    if (!ctx)
        return NULL;

    /* Use physical framebuffer dimensions (HiDPI-aware), not logical dimensions.
     * The rasterizer writes to fb.pixels which is at physical resolution. */
    vgfx_framebuffer_t fb;
    if (win && vgfx_get_framebuffer(win, &fb)) {
        ctx->width = fb.width;
        ctx->height = fb.height;
    } else {
        ctx->width = w;
        ctx->height = h;
    }

    if (!sw_ensure_zbuf_capacity(ctx, ctx->width, ctx->height)) {
        free(ctx);
        return NULL;
    }
    return ctx;
}

/// @brief Backend `destroy_ctx` op — free zbuffer + struct.
static void sw_destroy_ctx(void *ctx_ptr) {
    if (!ctx_ptr)
        return;
    sw_context_t *ctx = (sw_context_t *)ctx_ptr;
    free(ctx->zbuf);
    free(ctx->vertex_scratch);
    free(ctx);
}

/// @brief Backend `clear` op — clear the framebuffer to a solid color and reset Z.
///
/// Fills every pixel with `(r, g, b, 1)` packed as 0xAARRGGBB and
/// sets every Z value to +infinity (well, FLT_MAX) so the next draw's
/// "less than" depth tests succeed unconditionally.
static void sw_clear(void *ctx_ptr, vgfx_window_t win, float r, float g, float b) {
    sw_context_t *ctx = (sw_context_t *)ctx_ptr;
    if (!ctx)
        return;

    uint8_t cr = (uint8_t)(clamp01f(r) * 255.0f);
    uint8_t cg = (uint8_t)(clamp01f(g) * 255.0f);
    uint8_t cb = (uint8_t)(clamp01f(b) * 255.0f);

    if (ctx->render_target) {
        vgfx3d_rendertarget_t *rt = ctx->render_target;
        if (!vgfx3d_rendertarget_ensure_color(rt) || !vgfx3d_rendertarget_ensure_depth(rt))
            return;
        for (int32_t y = 0; y < rt->height; y++)
            for (int32_t x = 0; x < rt->width; x++) {
                uint8_t *px = &rt->color_buf[y * rt->stride + x * 4];
                px[0] = cr;
                px[1] = cg;
                px[2] = cb;
                px[3] = 0xFF;
            }
        size_t total = (size_t)rt->width * (size_t)rt->height;
        for (size_t i = 0; i < total; i++)
            rt->depth_buf[i] = FLT_MAX;
        rt->hdr_color_valid = 0;
    } else {
        vgfx_framebuffer_t fb;
        if (vgfx_get_framebuffer(win, &fb)) {
            if (!sw_ensure_zbuf_capacity(ctx, fb.width, fb.height))
                return;
            for (int32_t y = 0; y < fb.height; y++)
                for (int32_t x = 0; x < fb.width; x++) {
                    uint8_t *px = &fb.pixels[y * fb.stride + x * 4];
                    px[0] = cr;
                    px[1] = cg;
                    px[2] = cb;
                    px[3] = 0xFF;
                }
        }
        size_t total = (size_t)ctx->width * (size_t)ctx->height;
        for (size_t i = 0; i < total; i++)
            ctx->zbuf[i] = FLT_MAX;
    }
}

/// @brief Backend `begin_frame` op — capture camera + compute view-projection.
///
/// Multiplies projection × view, captures fog state and camera
/// position. Doesn't touch any pixels — drawing happens in `submit_draw`.
static void sw_begin_frame(void *ctx_ptr, const vgfx3d_camera_params_t *cam) {
    sw_context_t *ctx = (sw_context_t *)ctx_ptr;
    if (!ctx || !cam)
        return;
    /* VP = projection * view */
    mat4f_mul(cam->projection, cam->view, ctx->vp);
    ctx->cam_pos[0] = cam->position[0];
    ctx->cam_pos[1] = cam->position[1];
    ctx->cam_pos[2] = cam->position[2];
    ctx->cam_forward[0] = cam->forward[0];
    ctx->cam_forward[1] = cam->forward[1];
    ctx->cam_forward[2] = cam->forward[2];
    ctx->cam_is_ortho = cam->is_ortho ? 1 : 0;
    ctx->fog_enabled = cam->fog_enabled;
    ctx->fog_near = cam->fog_near;
    ctx->fog_far = cam->fog_far;
    ctx->fog_color[0] = cam->fog_color[0];
    ctx->fog_color[1] = cam->fog_color[1];
    ctx->fog_color[2] = cam->fog_color[2];
    if (!cam->load_existing_depth) {
        if (ctx->render_target) {
            vgfx3d_rendertarget_t *rt = ctx->render_target;
            if (vgfx3d_rendertarget_ensure_depth(rt)) {
                size_t total = (size_t)rt->width * (size_t)rt->height;
                for (size_t i = 0; i < total; i++)
                    rt->depth_buf[i] = FLT_MAX;
            }
        } else {
            size_t total = (size_t)ctx->width * (size_t)ctx->height;
            for (size_t i = 0; i < total; i++)
                ctx->zbuf[i] = FLT_MAX;
        }
    }
    /* Reset shadow state — rebuilt if shadows are enabled this frame */
    ctx->shadow_pass_slot = -1;
    ctx->shadow_count = 0;
    memset(ctx->shadow_complete, 0, sizeof(ctx->shadow_complete));
}

/// @brief A draw call's resolved material texture views plus the (possibly NULL)
///   pointers handed to the rasterizer. The pointers alias the views stored in
///   this struct, so it must outlive the rasterization loop that reads them.
typedef struct {
    sw_pixels_view tex_view, emissive_view, normal_view;
    sw_pixels_view specular_view, metallic_roughness_view, ao_view;
    sw_pixels_view *tex_ptr;
    sw_pixels_view *emissive_ptr;
    sw_pixels_view *normal_ptr;
    sw_pixels_view *specular_ptr;
    sw_pixels_view *metallic_roughness_ptr;
    sw_pixels_view *ao_ptr;
} sw_material_views;

/// @brief Copy each present material map (base color, emissive, normal, specular,
///   metallic-roughness, AO) into @p mv, setting the matching pointer non-NULL
///   only when the view has non-empty dimensions and backing data.
static void sw_resolve_material_views(const vgfx3d_draw_cmd_t *cmd, sw_material_views *mv) {
    mv->tex_ptr = NULL;
    mv->emissive_ptr = NULL;
    mv->normal_ptr = NULL;
    mv->specular_ptr = NULL;
    mv->metallic_roughness_ptr = NULL;
    mv->ao_ptr = NULL;
    if (cmd->texture) {
        const sw_pixels_view *pv = (const sw_pixels_view *)cmd->texture;
        mv->tex_view = *pv;
        if (mv->tex_view.width > 0 && mv->tex_view.height > 0 && mv->tex_view.data)
            mv->tex_ptr = &mv->tex_view;
    }
    if (cmd->emissive_map) {
        const sw_pixels_view *pv = (const sw_pixels_view *)cmd->emissive_map;
        mv->emissive_view = *pv;
        if (mv->emissive_view.width > 0 && mv->emissive_view.height > 0 && mv->emissive_view.data)
            mv->emissive_ptr = &mv->emissive_view;
    }
    if (cmd->normal_map) {
        const sw_pixels_view *pv = (const sw_pixels_view *)cmd->normal_map;
        mv->normal_view = *pv;
        if (mv->normal_view.width > 0 && mv->normal_view.height > 0 && mv->normal_view.data)
            mv->normal_ptr = &mv->normal_view;
    }
    if (cmd->specular_map) {
        const sw_pixels_view *pv = (const sw_pixels_view *)cmd->specular_map;
        mv->specular_view = *pv;
        if (mv->specular_view.width > 0 && mv->specular_view.height > 0 && mv->specular_view.data)
            mv->specular_ptr = &mv->specular_view;
    }
    if (cmd->metallic_roughness_map) {
        const sw_pixels_view *pv = (const sw_pixels_view *)cmd->metallic_roughness_map;
        mv->metallic_roughness_view = *pv;
        if (mv->metallic_roughness_view.width > 0 && mv->metallic_roughness_view.height > 0 &&
            mv->metallic_roughness_view.data)
            mv->metallic_roughness_ptr = &mv->metallic_roughness_view;
    }
    if (cmd->ao_map) {
        const sw_pixels_view *pv = (const sw_pixels_view *)cmd->ao_map;
        mv->ao_view = *pv;
        if (mv->ao_view.width > 0 && mv->ao_view.height > 0 && mv->ao_view.data)
            mv->ao_ptr = &mv->ao_view;
    }
}

/// @brief Backend `submit_draw` op — render one indexed mesh in software.
///
/// End-to-end pipeline:
///   1. Resolve framebuffer + ensure zbuffer is sized.
///   2. Compute world × view × projection matrix.
///   3. Set up texture views from material maps.
///   4. For each triangle: transform vertices to clip space, light
///      (Gouraud), clip against frustum, project to screen, rasterize.
/// Either runs the wireframe path (3 line draws per triangle) or the
/// full `raster_triangle` shader.
/// @brief Select the render output buffers — either the bound render target or
///   the window framebuffer — ensuring their color/depth allocations. Returns 1
///   with the out-params filled, or 0 if a required buffer could not be ensured.
static int sw_resolve_output_target(sw_context_t *ctx,
                                    vgfx_window_t win,
                                    uint8_t **out_pixels,
                                    float **out_zbuf,
                                    int32_t *out_w,
                                    int32_t *out_h,
                                    int32_t *out_stride) {
    if (ctx->render_target) {
        vgfx3d_rendertarget_t *rt = ctx->render_target;
        if (!vgfx3d_rendertarget_ensure_color(rt) || !vgfx3d_rendertarget_ensure_depth(rt))
            return 0;
        *out_pixels = rt->color_buf;
        *out_zbuf = rt->depth_buf;
        *out_w = rt->width;
        *out_h = rt->height;
        *out_stride = rt->stride;
        rt->hdr_color_valid = 0;
    } else {
        vgfx_framebuffer_t fb;
        if (!vgfx_get_framebuffer(win, &fb))
            return 0;
        if (!sw_ensure_zbuf_capacity(ctx, fb.width, fb.height))
            return 0;
        *out_pixels = fb.pixels;
        *out_zbuf = ctx->zbuf;
        *out_w = fb.width;
        *out_h = fb.height;
        *out_stride = fb.stride;
    }
    return 1;
}

/// @brief Transform every mesh vertex into the pipeline-vertex scratch buffer:
///   world position, world normal + tangent (via the normal matrix), clip-space
///   position (via MVP), UVs, and color. Computes per-vertex Gouraud lighting for
///   legacy unlit/non-normal-mapped materials; otherwise stores raw albedo so the
///   fragment stage can light per-pixel.
static void sw_transform_vertices(sw_context_t *ctx,
                                  const vgfx3d_draw_cmd_t *cmd,
                                  pipe_vert_t *pv,
                                  uint32_t vc,
                                  const float *mvp,
                                  const float *normal_matrix,
                                  const vgfx3d_light_params_t *lights,
                                  int32_t light_count,
                                  const float *ambient) {
    for (uint32_t i = 0; i < vc; i++) {
        const vgfx3d_vertex_t *src = &cmd->vertices[i];
        pipe_vert_t *dst = &pv[i];
        float pos4[4] = {src->pos[0], src->pos[1], src->pos[2], 1.0f};

        /* World-space */
        float world4[4];
        mat4f_transform4(cmd->model_matrix, pos4, world4);
        dst->world[0] = world4[0];
        dst->world[1] = world4[1];
        dst->world[2] = world4[2];

        float nrm4[4] = {src->normal[0], src->normal[1], src->normal[2], 0.0f};
        float wnrm4[4];
        mat4f_transform4(normal_matrix, nrm4, wnrm4);
        dst->normal[0] = wnrm4[0];
        dst->normal[1] = wnrm4[1];
        dst->normal[2] = wnrm4[2];

        /* Transform tangent to world space (for TBN construction with normal maps) */
        float tan4[4] = {src->tangent[0], src->tangent[1], src->tangent[2], 0.0f};
        float wtan4[4];
        mat4f_transform4(normal_matrix, tan4, wtan4);
        dst->tangent[0] = wtan4[0];
        dst->tangent[1] = wtan4[1];
        dst->tangent[2] = wtan4[2];
        dst->tangent[3] = src->tangent[3];

        /* Clip-space */
        float clip[4];
        mat4f_transform4(mvp, pos4, clip);
        dst->clip[0] = clip[0];
        dst->clip[1] = clip[1];
        dst->clip[2] = clip[2];
        dst->clip[3] = clip[3];

        dst->uv[0] = src->uv[0];
        dst->uv[1] = src->uv[1];
        dst->uv1[0] = src->uv1[0];
        dst->uv1[1] = src->uv1[1];

        /* Vertex color (defaults to white {1,1,1,1} if not set) */
        dst->color[0] = src->color[0];
        dst->color[1] = src->color[1];
        dst->color[2] = src->color[2];
        dst->color[3] = src->color[3];

        /* Per-vertex lighting (Gouraud) — skipped when normal/PBR work is done per-pixel. */
        if (!cmd->unlit && (cmd->workflow == RT_MATERIAL3D_WORKFLOW_PBR || cmd->normal_map)) {
            /* Store raw albedo: vertex_color * diffuse (lighting computed per-pixel) */
            dst->color[0] = cmd->diffuse_color[0] * dst->color[0];
            dst->color[1] = cmd->diffuse_color[1] * dst->color[1];
            dst->color[2] = cmd->diffuse_color[2] * dst->color[2];
            dst->color[3] = dst->color[3] * cmd->diffuse_color[3] * cmd->alpha;
        } else {
            compute_lighting(dst, ctx, cmd, lights, light_count, ambient);
        }
    }
}

/// @brief Emit verbose per-draw diagnostics (command params, MVP rows, first few
///   transformed vertices) to stderr for the first 8 draws when SW debug is on.
static void sw_debug_log_draw(const vgfx3d_draw_cmd_t *cmd,
                              const float *mvp,
                              const pipe_vert_t *pv,
                              uint32_t vc,
                              const float *ambient,
                              int32_t light_count) {
    static int debug_draw_count = 0;
    if (!sw_debug_enabled() || debug_draw_count >= 8)
        return;
    uint32_t limit = vc < 4 ? vc : 4;
    fprintf(stderr,
            "[sw3d] draw %d vc=%u ic=%u ambient=(%.3f %.3f %.3f) lights=%d alpha=%.3f "
            "diffuse=(%.3f %.3f %.3f %.3f)\n",
            debug_draw_count,
            cmd->vertex_count,
            cmd->index_count,
            ambient ? ambient[0] : 0.0f,
            ambient ? ambient[1] : 0.0f,
            ambient ? ambient[2] : 0.0f,
            light_count,
            cmd->alpha,
            cmd->diffuse_color[0],
            cmd->diffuse_color[1],
            cmd->diffuse_color[2],
            cmd->diffuse_color[3]);
    fprintf(stderr,
            "[sw3d] mvp rows=(%.3f %.3f %.3f %.3f) (%.3f %.3f %.3f %.3f) (%.3f %.3f %.3f "
            "%.3f) (%.3f %.3f %.3f %.3f)\n",
            mvp[0],
            mvp[1],
            mvp[2],
            mvp[3],
            mvp[4],
            mvp[5],
            mvp[6],
            mvp[7],
            mvp[8],
            mvp[9],
            mvp[10],
            mvp[11],
            mvp[12],
            mvp[13],
            mvp[14],
            mvp[15]);
    for (uint32_t i = 0; i < limit; i++) {
        fprintf(stderr,
                "[sw3d] v%u pos=(%.3f %.3f %.3f) clip=(%.3f %.3f %.3f %.3f) color=(%.3f "
                "%.3f %.3f %.3f)\n",
                i,
                cmd->vertices[i].pos[0],
                cmd->vertices[i].pos[1],
                cmd->vertices[i].pos[2],
                pv[i].clip[0],
                pv[i].clip[1],
                pv[i].clip[2],
                pv[i].clip[3],
                pv[i].color[0],
                pv[i].color[1],
                pv[i].color[2],
                pv[i].color[3]);
    }
    debug_draw_count++;
}

/// @brief Perspective-divide one clipped pipeline vertex into a screen-space
///   vertex (viewport-mapped position + perspective-correct attribute carriers).
///   Returns 0 (skip the triangle) when the clip-space w is degenerate.
static int sw_project_clip_to_screen(const pipe_vert_t *p,
                                     float half_w,
                                     float half_h,
                                     screen_vert_t *sv) {
    if (fabsf(p->clip[3]) < 1e-7f)
        return 0;
    float iw = 1.0f / p->clip[3];
    sv->sx = (p->clip[0] * iw + 1.0f) * half_w;
    sv->sy = (1.0f - p->clip[1] * iw) * half_h;
    sv->sz = p->clip[2] * iw;
    sv->r = p->color[0];
    sv->g = p->color[1];
    sv->b = p->color[2];
    sv->a = p->color[3];
    sv->inv_w = iw;
    sv->u_over_w = p->uv[0] * iw;
    sv->v_over_w = p->uv[1] * iw;
    sv->u1_over_w = p->uv1[0] * iw;
    sv->v1_over_w = p->uv1[1] * iw;
    sv->wx = p->world[0];
    sv->wy = p->world[1];
    sv->wz = p->world[2];
    sv->nx = p->normal[0];
    sv->ny = p->normal[1];
    sv->nz = p->normal[2];
    sv->tx = p->tangent[0];
    sv->ty = p->tangent[1];
    sv->tz = p->tangent[2];
    sv->tw = p->tangent[3];
    return 1;
}

/// @brief Draw the three edges of a screen-space triangle as wireframe lines,
///   colored from the first vertex's interpolated color.
static void sw_draw_wireframe_tri(
    uint8_t *pixels, int32_t fb_w, int32_t fb_h, int32_t stride, const screen_vert_t *sv) {
    uint8_t wr = (uint8_t)(clamp01f(sv[0].r) * 255.0f);
    uint8_t wg = (uint8_t)(clamp01f(sv[0].g) * 255.0f);
    uint8_t wb = (uint8_t)(clamp01f(sv[0].b) * 255.0f);
    draw_line(pixels,
              fb_w,
              fb_h,
              stride,
              (int)sv[0].sx,
              (int)sv[0].sy,
              (int)sv[1].sx,
              (int)sv[1].sy,
              wr,
              wg,
              wb);
    draw_line(pixels,
              fb_w,
              fb_h,
              stride,
              (int)sv[1].sx,
              (int)sv[1].sy,
              (int)sv[2].sx,
              (int)sv[2].sy,
              wr,
              wg,
              wb);
    draw_line(pixels,
              fb_w,
              fb_h,
              stride,
              (int)sv[2].sx,
              (int)sv[2].sy,
              (int)sv[0].sx,
              (int)sv[0].sy,
              wr,
              wg,
              wb);
}

/// @brief Backend `submit_draw` op for the software rasterizer. Resolves output
///   buffers, transforms vertices, then clips and rasterizes (or wireframes) each
///   triangle of the mesh.
static void sw_submit_draw(void *ctx_ptr,
                           vgfx_window_t win,
                           const vgfx3d_draw_cmd_t *cmd,
                           const vgfx3d_light_params_t *lights,
                           int32_t light_count,
                           const float *ambient,
                           int8_t wireframe,
                           int8_t backface_cull) {
    sw_context_t *ctx = (sw_context_t *)ctx_ptr;
    static const float zero_ambient[3] = {0.0f, 0.0f, 0.0f};
    if (!ctx || !cmd || !cmd->vertices || !cmd->indices || cmd->vertex_count == 0 ||
        cmd->index_count == 0)
        return;
    if (!ambient)
        ambient = zero_ambient;
    if (!lights || light_count < 0)
        light_count = 0;

    /* Determine output buffers: render target or window framebuffer */
    uint8_t *out_pixels;
    float *out_zbuf;
    int32_t out_w, out_h, out_stride;
    if (!sw_resolve_output_target(ctx, win, &out_pixels, &out_zbuf, &out_w, &out_h, &out_stride))
        return;

    /* Build MVP = VP * model */
    float mvp[16];
    float normal_matrix[16];
    mat4f_mul(ctx->vp, cmd->model_matrix, mvp);
    vgfx3d_compute_normal_matrix4(cmd->model_matrix, normal_matrix);

    /* Texture setup — resolve up to six material maps into stack-local views. */
    sw_material_views mv;
    sw_resolve_material_views(cmd, &mv);

    float half_w = (float)out_w * 0.5f;
    float half_h = (float)out_h * 0.5f;

    /* Transform mesh vertices */
    uint32_t vc = cmd->vertex_count;
    pipe_vert_t *pv = sw_acquire_pipe_vertices(ctx, vc);
    if (!pv)
        return;
    sw_transform_vertices(ctx, cmd, pv, vc, mvp, normal_matrix, lights, light_count, ambient);
    sw_debug_log_draw(cmd, mvp, pv, vc, ambient, light_count);

    /* Process triangles: clip → rasterize */
    pipe_vert_t clipped[MAX_CLIP_VERTS];

    for (uint32_t i = 0; i + 2 < cmd->index_count; i += 3) {
        uint32_t i0 = cmd->indices[i], i1 = cmd->indices[i + 1], i2 = cmd->indices[i + 2];
        if (i0 >= vc || i1 >= vc || i2 >= vc)
            continue;

        pipe_vert_t tri[3] = {pv[i0], pv[i1], pv[i2]};
        int clip_count = clip_triangle(tri, clipped);
        if (clip_count < 3)
            continue;

        for (int t = 1; t < clip_count - 1; t++) {
            screen_vert_t sv[3];
            const pipe_vert_t *fan[3] = {&clipped[0], &clipped[t], &clipped[t + 1]};
            int ok = 1;
            for (int vi = 0; vi < 3; vi++) {
                if (!sw_project_clip_to_screen(fan[vi], half_w, half_h, &sv[vi])) {
                    ok = 0;
                    break;
                }
            }
            if (!ok)
                continue;

            if (wireframe) {
                sw_draw_wireframe_tri(out_pixels, out_w, out_h, out_stride, sv);
            } else {
                raster_triangle(out_pixels,
                                out_zbuf,
                                out_w,
                                out_h,
                                out_stride,
                                &sv[0],
                                &sv[1],
                                &sv[2],
                                mv.tex_ptr,
                                mv.emissive_ptr,
                                cmd->emissive_color,
                                mv.normal_ptr,
                                mv.specular_ptr,
                                mv.metallic_roughness_ptr,
                                mv.ao_ptr,
                                cmd,
                                lights,
                                light_count,
                                ambient,
                                backface_cull,
                                ctx);
            }
        }
    }
}

/// @brief Backend `end_frame` op — no-op for software (frames write directly to fb).
static void sw_end_frame(void *ctx_ptr) {
    (void)ctx_ptr;
}

/// @brief Backend `resize` op — re-allocate the depth buffer to the new size.
static void sw_resize(void *ctx_ptr, int32_t w, int32_t h) {
    sw_context_t *ctx = (sw_context_t *)ctx_ptr;
    if (!ctx || ctx->render_target)
        return;
    sw_ensure_zbuf_capacity(ctx, w, h);
}

/// @brief Backend `set_render_target` op — bind / unbind a host-memory RTT.
///
/// Software backend RTT is just "render directly to the user-supplied
/// host buffer instead of the swapchain". No GPU resources to manage.
/// NULL `rt` reverts to swapchain rendering.
static void sw_set_render_target(void *ctx_ptr, vgfx3d_rendertarget_t *rt) {
    sw_context_t *ctx = (sw_context_t *)ctx_ptr;
    if (!ctx)
        return;
    if (rt) {
        if (!vgfx3d_rendertarget_ensure_color(rt) || !vgfx3d_rendertarget_ensure_depth(rt)) {
            ctx->render_target = NULL;
            return;
        }
    }
    ctx->render_target = rt;
}

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

/// @brief Public: pick the best 3D backend at startup.
///
/// Tries each compiled-in backend in priority order (D3D11 on
/// Windows, Metal on macOS, OpenGL on Linux), falling back to the
/// software backend when no GPU backend is available. Called once
/// at canvas-create time and the choice is cached.
const vgfx3d_backend_t *vgfx3d_select_backend(void) {
    /* Only honor overrides for backends compiled on this platform. */
    const char *env = getenv("VIPER_3D_BACKEND");
    if (env) {
        if (strcmp(env, "software") == 0)
            return &vgfx3d_software_backend;
#if defined(__APPLE__)
        if (strcmp(env, "metal") == 0)
            return &vgfx3d_metal_backend;
#elif defined(_WIN32)
        if (strcmp(env, "d3d11") == 0)
            return &vgfx3d_d3d11_backend;
#elif defined(__linux__)
        if (strcmp(env, "opengl") == 0)
            return &vgfx3d_opengl_backend;
#endif
    }

    /* Linux OpenGL is still under active bring-up, so prefer the software
     * backend by default there until the GL path is stable across frames. */
#if defined(__APPLE__)
    return &vgfx3d_metal_backend;
#elif defined(_WIN32) && (defined(_M_ARM64) || defined(__aarch64__))
    /* Several Windows-on-ARM GPU stacks expose D3D11 but crash inside the
     * display driver during Present. Keep x64 on D3D11, but default ARM64 to
     * the portable backend so Canvas3D demos launch reliably. Users can still
     * opt into D3D11 with VIPER_3D_BACKEND=d3d11. */
    return &vgfx3d_software_backend;
#elif defined(_WIN32)
    return &vgfx3d_d3d11_backend;
#elif defined(__linux__)
    return &vgfx3d_software_backend;
#else
    return &vgfx3d_software_backend;
#endif
}

#else
typedef int rt_graphics_disabled_tu_guard;
#endif /* VIPER_ENABLE_GRAPHICS */
