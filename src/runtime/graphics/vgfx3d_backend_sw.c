//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/vgfx3d_backend_sw.c
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
//
// Links: vgfx3d_backend.h, rt_canvas3d_internal.h
//
//===----------------------------------------------------------------------===//

#ifdef VIPER_ENABLE_GRAPHICS

#include "rt_canvas3d.h"
#include "rt_canvas3d_internal.h"
#include "vgfx3d_backend.h"

#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*==========================================================================
 * Software backend context
 *=========================================================================*/

typedef struct {
    float *zbuf;
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
    float *shadow_depth; /* shadow depth buffer (during shadow pass) */
    int32_t shadow_w, shadow_h;
    float shadow_vp[16]; /* light view-projection matrix */
    float shadow_bias;
    int8_t shadow_active; /* 1 = shadow map is populated and ready for lookup */
} sw_context_t;

static inline void sw_compute_view_vector(const sw_context_t *ctx,
                                          float wx,
                                          float wy,
                                          float wz,
                                          float *out_vx,
                                          float *out_vy,
                                          float *out_vz);

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

typedef struct {
    float clip[4];
    float world[3];
    float normal[3];
    float tangent[4];
    float uv[2];
    float color[4];
} pipe_vert_t;

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
    if (cmd->unlit) {
        v->color[0] = cmd->diffuse_color[0] * v->color[0];
        v->color[1] = cmd->diffuse_color[1] * v->color[1];
        v->color[2] = cmd->diffuse_color[2] * v->color[2];
        v->color[3] = cmd->diffuse_color[3] * v->color[3];
        return;
    }

    float nx = v->normal[0], ny = v->normal[1], nz = v->normal[2];
    float nlen = sqrtf(nx * nx + ny * ny + nz * nz);
    if (nlen > 1e-7f) {
        nx /= nlen;
        ny /= nlen;
        nz /= nlen;
    }

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
            float ll = sqrtf(lx * lx + ly * ly + lz * lz);
            if (ll > 1e-7f) {
                lx /= ll;
                ly /= ll;
                lz /= ll;
            }
        } else if (light->type == 1) /* point */
        {
            lx = light->position[0] - v->world[0];
            ly = light->position[1] - v->world[1];
            lz = light->position[2] - v->world[2];
            float dist = sqrtf(lx * lx + ly * ly + lz * lz);
            if (dist > 1e-7f) {
                lx /= dist;
                ly /= dist;
                lz /= dist;
            }
            atten = 1.0f / (1.0f + light->attenuation * dist * dist);
        } else if (light->type == 3) /* spot */
        {
            lx = light->position[0] - v->world[0];
            ly = light->position[1] - v->world[1];
            lz = light->position[2] - v->world[2];
            float dist = sqrtf(lx * lx + ly * ly + lz * lz);
            if (dist > 1e-7f) {
                lx /= dist;
                ly /= dist;
                lz /= dist;
            }
            atten = 1.0f / (1.0f + light->attenuation * dist * dist);
            /* Cone attenuation: smoothstep between outer and inner cosines */
            float spot_dot =
                -(lx * light->direction[0] + ly * light->direction[1] + lz * light->direction[2]);
            if (spot_dot < light->outer_cos)
                atten = 0.0f; /* outside cone */
            else if (spot_dot < light->inner_cos) {
                float t = (spot_dot - light->outer_cos) / (light->inner_cos - light->outer_cos);
                atten *= t * t * (3.0f - 2.0f * t); /* smoothstep */
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
            float hlen = sqrtf(hx * hx + hy * hy + hz * hz);
            if (hlen > 1e-7f) {
                hx /= hlen;
                hy /= hlen;
                hz /= hlen;
            }
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
        default: /* 0=BlinnPhong (already computed), 2=reserved, 3=Unlit (handled above) */
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
    if (!pixels_obj)
        return 0;
    const sw_pixels_view *pv = (const sw_pixels_view *)pixels_obj;
    *out = *pv;
    return (out->width > 0 && out->height > 0 && out->data != NULL);
}

/// @brief Bilinear texture sampler with REPEAT wrap.
///
/// Maps UV → texel center (subtract 0.5), looks up the 4 surrounding
/// texels with wrap-around indexing, then weights them by the
/// fractional component. Produces normalized [0,1] RGBA.
static void sample_texture(
    const sw_pixels_view *tex, float u, float v, float *r, float *g, float *b, float *a) {
    int w = (int)tex->width, h = (int)tex->height;
    if (w == 0 || h == 0) {
        *r = *g = *b = 1.0f;
        *a = 1.0f;
        return;
    }

    u = u - floorf(u);
    v = v - floorf(v);

    /* Bilinear: map UV to texel center, then interpolate the 4 neighbors */
    float fx = u * (float)w - 0.5f;
    float fy = v * (float)h - 0.5f;
    int x0 = (int)floorf(fx);
    int y0 = (int)floorf(fy);
    float xf = fx - (float)x0; /* fractional part [0,1) */
    float yf = fy - (float)y0;

    /* Wrap coordinates for repeat mode */
    int x1 = x0 + 1;
    int y1 = y0 + 1;
    x0 = ((x0 % w) + w) % w;
    y0 = ((y0 % h) + h) % h;
    x1 = ((x1 % w) + w) % w;
    y1 = ((y1 % h) + h) % h;

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

/*==========================================================================
 * Edge-function triangle rasterizer
 *=========================================================================*/

typedef struct {
    float sx, sy, sz;
    float r, g, b, a;
    float u_over_w, v_over_w, inv_w;
    float wx, wy, wz; /* world position (for fog distance computation) */
    float nx, ny, nz; /* world normal (for per-pixel lighting with normal maps) */
    float tx, ty, tz, tw; /* world tangent plus handedness sign (for TBN construction) */
} screen_vert_t;

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
    float len = sqrtf((*x) * (*x) + (*y) * (*y) + (*z) * (*z));
    if (len > 1e-7f) {
        *x /= len;
        *y /= len;
        *z /= len;
    }
}

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
    float pp_iw;
    float pp_u = 0.0f;
    float pp_v = 0.0f;

    if (!cmd || !ctx || !inout_r || !inout_g || !inout_b || !cmd->env_map || cmd->reflectivity <= 0.0001f)
        return;

    wx = b0 * v0->wx + b1 * v1->wx + b2 * v2->wx;
    wy = b0 * v0->wy + b1 * v1->wy + b2 * v2->wy;
    wz = b0 * v0->wz + b1 * v1->wz + b2 * v2->wz;
    pnx = b0 * v0->nx + b1 * v1->nx + b2 * v2->nx;
    pny = b0 * v0->ny + b1 * v1->ny + b2 * v2->ny;
    pnz = b0 * v0->nz + b1 * v1->nz + b2 * v2->nz;
    normalize3f(&pnx, &pny, &pnz);

    pp_iw = b0 * v0->inv_w + b1 * v1->inv_w + b2 * v2->inv_w;
    if (fabsf(pp_iw) > 1e-7f) {
        pp_u = (b0 * v0->u_over_w + b1 * v1->u_over_w + b2 * v2->u_over_w) / pp_iw;
        pp_v = (b0 * v0->v_over_w + b1 * v1->v_over_w + b2 * v2->v_over_w) / pp_iw;
    }

    if (normal_map) {
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

            sample_texture(normal_map, pp_u, pp_v, &tnr, &tng, &tnb, &tna);
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

    sw_compute_view_vector(ctx, wx, wy, wz, &vdx, &vdy, &vdz);
    roughness = clamp01f(cmd->roughness);
    if (cmd->workflow == RT_MATERIAL3D_WORKFLOW_PBR && metallic_roughness_map) {
        float mrr;
        float mrg;
        float mrb;
        float mra;
        sample_texture(metallic_roughness_map, pp_u, pp_v, &mrr, &mrg, &mrb, &mra);
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
    return pbr_geometry_schlick_ggx(ndv, roughness) *
           pbr_geometry_schlick_ggx(ndl, roughness);
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
    float area = (v1->sx - v0->sx) * (v2->sy - v0->sy) - (v2->sx - v0->sx) * (v1->sy - v0->sy);
    float original_area = area;
    int emit_debug = 0;
    int min_x = 0, max_x = 0, min_y = 0, max_y = 0;
    int inside_samples = 0;
    int depth_passes = 0;
    int pixels_written = 0;
    static int debug_tri_count = 0;

    if (sw_debug_enabled() && debug_tri_count < 16)
        emit_debug = 1;

    /* After viewport Y-flip, CCW world-space triangles have NEGATIVE screen-space
     * area. So negative area = front face, positive area = back face.
     * Cull back faces (positive area) when backface culling is enabled. */
    if (backface_cull && area >= 0.0f) {
        if (emit_debug) {
            fprintf(stderr,
                    "[sw3d] tri %d culled area=%.3f v0=(%.2f %.2f %.3f) v1=(%.2f %.2f %.3f) "
                    "v2=(%.2f %.2f %.3f)\n",
                    debug_tri_count++,
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
        return;
    }
    if (area < 0.0f) {
        const screen_vert_t *tmp = v1;
        v1 = v2;
        v2 = tmp;
        area = -area;
    }
    if (area < 1e-6f) {
        if (emit_debug) {
            fprintf(stderr,
                    "[sw3d] tri %d degenerate area=%.6f orig=%.6f\n",
                    debug_tri_count++,
                    area,
                    original_area);
        }
        return;
    }

    float inv_area = 1.0f / area;
    min_x = (int)fmaxf(fminf(fminf(v0->sx, v1->sx), v2->sx), 0.0f);
    max_x = (int)fminf(fmaxf(fmaxf(v0->sx, v1->sx), v2->sx), (float)(fb_w - 1));
    min_y = (int)fmaxf(fminf(fminf(v0->sy, v1->sy), v2->sy), 0.0f);
    max_y = (int)fminf(fmaxf(fmaxf(v0->sy, v1->sy), v2->sy), (float)(fb_h - 1));
    if (min_x > max_x || min_y > max_y) {
        if (emit_debug) {
            fprintf(stderr,
                    "[sw3d] tri %d clipped bbox=(%d..%d,%d..%d) area=%.3f orig=%.3f\n",
                    debug_tri_count++,
                    min_x,
                    max_x,
                    min_y,
                    max_y,
                    area,
                    original_area);
        }
        return;
    }

    /* Half-space edge functions for rasterization.
     * w0/w1/w2 are the signed distances from pixel center to each edge.
     * A pixel is inside when all three are >= 0. The edge function increments
     * by -dy/+dx per pixel step, enabling efficient scanline traversal. */
    float e12_dx = v2->sx - v1->sx, e12_dy = v2->sy - v1->sy;
    float e20_dx = v0->sx - v2->sx, e20_dy = v0->sy - v2->sy;
    float e01_dx = v1->sx - v0->sx, e01_dy = v1->sy - v0->sy;
    float px0 = (float)min_x + 0.5f, py0 = (float)min_y + 0.5f;
    float row_w0 = e12_dx * (py0 - v1->sy) - e12_dy * (px0 - v1->sx);
    float row_w1 = e20_dx * (py0 - v2->sy) - e20_dy * (px0 - v2->sx);
    float row_w2 = e01_dx * (py0 - v0->sy) - e01_dy * (px0 - v0->sx);

    for (int y = min_y; y <= max_y; y++) {
        float w0 = row_w0, w1 = row_w1, w2 = row_w2;
        for (int x = min_x; x <= max_x; x++) {
            if (w0 >= 0.0f && w1 >= 0.0f && w2 >= 0.0f) {
                inside_samples++;
                /* Barycentric weights from edge functions; z is linearly
                 * interpolated in screen space (not perspective-correct,
                 * but sufficient for depth testing). */
                float b0 = w0 * inv_area, b1 = w1 * inv_area, b2 = w2 * inv_area;
                float z = b0 * v0->sz + b1 * v1->sz + b2 * v2->sz;
                int idx = y * fb_w + x;
                if (z < zbuf[idx]) {
                    depth_passes++;
                    float fr = b0 * v0->r + b1 * v1->r + b2 * v2->r;
                    float fg = b0 * v0->g + b1 * v1->g + b2 * v2->g;
                    float fb_c = b0 * v0->b + b1 * v1->b + b2 * v2->b;
                    float tex_alpha = 1.0f; /* per-texel alpha (for foliage, fences) */
                    if (tex) {
                        float iw = b0 * v0->inv_w + b1 * v1->inv_w + b2 * v2->inv_w;
                        if (fabsf(iw) > 1e-7f) {
                            float u =
                                (b0 * v0->u_over_w + b1 * v1->u_over_w + b2 * v2->u_over_w) / iw;
                            float vc =
                                (b0 * v0->v_over_w + b1 * v1->v_over_w + b2 * v2->v_over_w) / iw;
                            float tr, tg, tb, ta;
                            sample_texture(tex, u, vc, &tr, &tg, &tb, &ta);
                            fr *= tr;
                            fg *= tg;
                            fb_c *= tb;
                            tex_alpha = ta;
                        }
                    }
                    /* Terrain splat: replace diffuse with per-pixel layer blend */
                    if (cmd && cmd->has_splat && cmd->splat_map) {
                        float iw = b0 * v0->inv_w + b1 * v1->inv_w + b2 * v2->inv_w;
                        if (fabsf(iw) > 1e-7f) {
                            float sp_u =
                                (b0 * v0->u_over_w + b1 * v1->u_over_w + b2 * v2->u_over_w) / iw;
                            float sp_v =
                                (b0 * v0->v_over_w + b1 * v1->v_over_w + b2 * v2->v_over_w) / iw;
                            sw_pixels_view splat_view;
                            if (setup_pixels_view(cmd->splat_map, &splat_view)) {
                                float sr, sg, sb, sa;
                                sample_texture(&splat_view, sp_u, sp_v, &sr, &sg, &sb, &sa);
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
                                    if (w[L] < 0.001f || !cmd->splat_layers[L])
                                        continue;
                                    sw_pixels_view lv;
                                    if (!setup_pixels_view(cmd->splat_layers[L], &lv))
                                        continue;
                                    float lu = sp_u * cmd->splat_layer_scales[L];
                                    float lvc = sp_v * cmd->splat_layer_scales[L];
                                    float lr, lg2, lb, la;
                                    sample_texture(&lv, lu, lvc, &lr, &lg2, &lb, &la);
                                    blr += lr * w[L];
                                    blg += lg2 * w[L];
                                    blb += lb * w[L];
                                }
                                fr = blr;
                                fg = blg;
                                fb_c = blb;
                            }
                        }
                    }

                    /* Emissive map sampling (legacy path only; PBR handles it in the lighting
                     * branch so emissiveIntensity can scale both the color and the map.) */
                    if (emissive_tex &&
                        !(cmd && cmd->workflow == RT_MATERIAL3D_WORKFLOW_PBR)) {
                        float iw = b0 * v0->inv_w + b1 * v1->inv_w + b2 * v2->inv_w;
                        if (fabsf(iw) > 1e-7f) {
                            float u =
                                (b0 * v0->u_over_w + b1 * v1->u_over_w + b2 * v2->u_over_w) / iw;
                            float vc =
                                (b0 * v0->v_over_w + b1 * v1->v_over_w + b2 * v2->v_over_w) / iw;
                            float er, eg, eb, ea;
                            sample_texture(emissive_tex, u, vc, &er, &eg, &eb, &ea);
                            float emissive_scale =
                                (cmd ? cmd->emissive_intensity : 1.0f);
                            fr += er * emissive_color[0] * emissive_scale;
                            fg += eg * emissive_color[1] * emissive_scale;
                            fb_c += eb * emissive_color[2] * emissive_scale;
                        }
                    }

                    /* Per-pixel lighting for PBR or normal-mapped legacy materials. */
                    float pixel_ndv = 1.0f;
                    if (cmd && !cmd->unlit &&
                        (cmd->workflow == RT_MATERIAL3D_WORKFLOW_PBR || normal_map)) {
                        float pp_iw = b0 * v0->inv_w + b1 * v1->inv_w + b2 * v2->inv_w;
                        if (fabsf(pp_iw) > 1e-7f) {
                            float pp_u =
                                (b0 * v0->u_over_w + b1 * v1->u_over_w + b2 * v2->u_over_w) / pp_iw;
                            float pp_vc =
                                (b0 * v0->v_over_w + b1 * v1->v_over_w + b2 * v2->v_over_w) / pp_iw;

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
                                sample_texture(normal_map, pp_u, pp_vc, &tnr, &tng, &tnb, &tna);
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

                            /* Per-pixel Blinn-Phong with perturbed normal */
                            float wx = b0 * v0->wx + b1 * v1->wx + b2 * v2->wx;
                            float wy = b0 * v0->wy + b1 * v1->wy + b2 * v2->wy;
                            float wz = b0 * v0->wz + b1 * v1->wz + b2 * v2->wz;

                            float vdx;
                            float vdy;
                            float vdz;
                            sw_compute_view_vector(fog_ctx, wx, wy, wz, &vdx, &vdy, &vdz);
                            pixel_ndv = clamp01f(dot3f(pnx, pny, pnz, vdx, vdy, vdz));

                            if (cmd->workflow == RT_MATERIAL3D_WORKFLOW_PBR) {
                                float base_r = fr;
                                float base_g = fg;
                                float base_b = fb_c;
                                float metallic = clamp01f(cmd->metallic);
                                float roughness = clamp01f(cmd->roughness);
                                float ao = clamp01f(cmd->ao);
                                if (metallic_roughness_map) {
                                    float mrr, mrg, mrb, mra;
                                    sample_texture(
                                        metallic_roughness_map, pp_u, pp_vc, &mrr, &mrg, &mrb, &mra);
                                    roughness *= mrg;
                                    metallic *= mrb;
                                }
                                if (ao_map) {
                                    float aor, aog, aob, aoa;
                                    sample_texture(ao_map, pp_u, pp_vc, &aor, &aog, &aob, &aoa);
                                    ao *= aor;
                                }
                                metallic = clamp01f(metallic);
                                roughness = roughness < 0.045f ? 0.045f : clamp01f(roughness);
                                ao = clamp01f(ao);

                                float ndv = pixel_ndv;
                                if (ndv < 0.001f)
                                    ndv = 0.001f;

                                float lit_r =
                                    (ambient ? ambient[0] : 0.0f) * base_r * ao;
                                float lit_g =
                                    (ambient ? ambient[1] : 0.0f) * base_g * ao;
                                float lit_b =
                                    (ambient ? ambient[2] : 0.0f) * base_b * ao;

                                for (int32_t li = 0; li < light_count; li++) {
                                    const vgfx3d_light_params_t *lt = &lights[li];
                                    float llx, lly, llz, la = 1.0f;

                                    if (lt->type == 0) { /* directional */
                                        llx = -lt->direction[0];
                                        lly = -lt->direction[1];
                                        llz = -lt->direction[2];
                                        normalize3f(&llx, &lly, &llz);
                                    } else if (lt->type == 1) { /* point */
                                        llx = lt->position[0] - wx;
                                        lly = lt->position[1] - wy;
                                        llz = lt->position[2] - wz;
                                        float dist = sqrtf(llx * llx + lly * lly + llz * llz);
                                        if (dist > 1e-7f) {
                                            llx /= dist;
                                            lly /= dist;
                                            llz /= dist;
                                        }
                                        la = 1.0f / (1.0f + lt->attenuation * dist * dist);
                                    } else if (lt->type == 3) { /* spot */
                                        llx = lt->position[0] - wx;
                                        lly = lt->position[1] - wy;
                                        llz = lt->position[2] - wz;
                                        float dist = sqrtf(llx * llx + lly * lly + llz * llz);
                                        if (dist > 1e-7f) {
                                            llx /= dist;
                                            lly /= dist;
                                            llz /= dist;
                                        }
                                        la = 1.0f / (1.0f + lt->attenuation * dist * dist);
                                        float sd = -(llx * lt->direction[0] + lly * lt->direction[1] +
                                                     llz * lt->direction[2]);
                                        if (sd < lt->outer_cos)
                                            la = 0.0f;
                                        else if (sd < lt->inner_cos) {
                                            float st =
                                                (sd - lt->outer_cos) / (lt->inner_cos - lt->outer_cos);
                                            la *= st * st * (3.0f - 2.0f * st);
                                        }
                                    } else { /* ambient */
                                        lit_r += lt->color[0] * lt->intensity * base_r * ao;
                                        lit_g += lt->color[1] * lt->intensity * base_g * ao;
                                        lit_b += lt->color[2] * lt->intensity * base_b * ao;
                                        continue;
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
                                    float g = pbr_geometry_smith(ndv, ndl, roughness);
                                    float spec_denom = 4.0f * ndv * ndl + 1e-4f;
                                    float spec_r = (d * g * f_r) / spec_denom;
                                    float spec_g = (d * g * f_g) / spec_denom;
                                    float spec_b = (d * g * f_b) / spec_denom;

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

                                float emissive_r =
                                    cmd->emissive_color[0] * cmd->emissive_intensity;
                                float emissive_g =
                                    cmd->emissive_color[1] * cmd->emissive_intensity;
                                float emissive_b =
                                    cmd->emissive_color[2] * cmd->emissive_intensity;
                                if (emissive_tex) {
                                    float er, eg, eb, ea;
                                    sample_texture(emissive_tex, pp_u, pp_vc, &er, &eg, &eb, &ea);
                                    emissive_r *= er;
                                    emissive_g *= eg;
                                    emissive_b *= eb;
                                }
                                lit_r += emissive_r;
                                lit_g += emissive_g;
                                lit_b += emissive_b;

                                if (cmd->shading_model == 1) {
                                    float bands =
                                        cmd->custom_params[0] > 0.5f ? cmd->custom_params[0] : 4.0f;
                                    lit_r = floorf(lit_r * bands) / bands;
                                    lit_g = floorf(lit_g * bands) / bands;
                                    lit_b = floorf(lit_b * bands) / bands;
                                } else if (cmd->shading_model == 5) {
                                    float strength =
                                        cmd->custom_params[0] > 0.0f ? cmd->custom_params[0] : 2.0f;
                                    lit_r += emissive_r * (strength - 1.0f);
                                    lit_g += emissive_g * (strength - 1.0f);
                                    lit_b += emissive_b * (strength - 1.0f);
                                }

                                fr = lit_r;
                                fg = lit_g;
                                fb_c = lit_b;
                            } else {
                                /* Ambient */
                                float lit_r = (ambient ? ambient[0] : 0.0f) * fr;
                                float lit_g = (ambient ? ambient[1] : 0.0f) * fg;
                                float lit_b = (ambient ? ambient[2] : 0.0f) * fb_c;

                                /* Specular properties (possibly from specular map) */
                                float sp_r = cmd->specular[0];
                                float sp_g = cmd->specular[1];
                                float sp_b = cmd->specular[2];
                                if (specular_map) {
                                    float smr, smg, smb, sma;
                                    sample_texture(specular_map, pp_u, pp_vc, &smr, &smg, &smb, &sma);
                                    sp_r *= smr;
                                    sp_g *= smg;
                                    sp_b *= smb;
                                }

                                for (int32_t li = 0; li < light_count; li++) {
                                    const vgfx3d_light_params_t *lt = &lights[li];
                                    float llx, lly, llz, la = 1.0f;

                                    if (lt->type == 0) { /* directional */
                                        llx = -lt->direction[0];
                                        lly = -lt->direction[1];
                                        llz = -lt->direction[2];
                                        normalize3f(&llx, &lly, &llz);
                                    } else if (lt->type == 1) { /* point */
                                        llx = lt->position[0] - wx;
                                        lly = lt->position[1] - wy;
                                        llz = lt->position[2] - wz;
                                        float dist = sqrtf(llx * llx + lly * lly + llz * llz);
                                        if (dist > 1e-7f) {
                                            llx /= dist;
                                            lly /= dist;
                                            llz /= dist;
                                        }
                                        la = 1.0f / (1.0f + lt->attenuation * dist * dist);
                                    } else if (lt->type == 3) { /* spot */
                                        llx = lt->position[0] - wx;
                                        lly = lt->position[1] - wy;
                                        llz = lt->position[2] - wz;
                                        float dist = sqrtf(llx * llx + lly * lly + llz * llz);
                                        if (dist > 1e-7f) {
                                            llx /= dist;
                                            lly /= dist;
                                            llz /= dist;
                                        }
                                        la = 1.0f / (1.0f + lt->attenuation * dist * dist);
                                        float sd = -(llx * lt->direction[0] + lly * lt->direction[1] +
                                                     llz * lt->direction[2]);
                                        if (sd < lt->outer_cos)
                                            la = 0.0f;
                                        else if (sd < lt->inner_cos) {
                                            float st =
                                                (sd - lt->outer_cos) / (lt->inner_cos - lt->outer_cos);
                                            la *= st * st * (3.0f - 2.0f * st);
                                        }
                                    } else { /* ambient */
                                        lit_r += lt->color[0] * lt->intensity * fr;
                                        lit_g += lt->color[1] * lt->intensity * fg;
                                        lit_b += lt->color[2] * lt->intensity * fb_c;
                                        continue;
                                    }

                                    float ndl = pnx * llx + pny * lly + pnz * llz;
                                    if (ndl < 0.0f)
                                        ndl = 0.0f;
                                    float li_i = lt->intensity;
                                    lit_r += lt->color[0] * li_i * ndl * fr * la;
                                    lit_g += lt->color[1] * li_i * ndl * fg * la;
                                    lit_b += lt->color[2] * li_i * ndl * fb_c * la;

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
                                    float bands =
                                        cmd->custom_params[0] > 0.5f ? cmd->custom_params[0] : 4.0f;
                                    lit_r = floorf(lit_r * bands) / bands;
                                    lit_g = floorf(lit_g * bands) / bands;
                                    lit_b = floorf(lit_b * bands) / bands;
                                } else if (cmd->shading_model == 5) {
                                    float strength =
                                        cmd->custom_params[0] > 0.0f ? cmd->custom_params[0] : 2.0f;
                                    lit_r += cmd->emissive_color[0] * cmd->emissive_intensity *
                                             (strength - 1.0f);
                                    lit_g += cmd->emissive_color[1] * cmd->emissive_intensity *
                                             (strength - 1.0f);
                                    lit_b += cmd->emissive_color[2] * cmd->emissive_intensity *
                                             (strength - 1.0f);
                                }

                                fr = lit_r;
                                fg = lit_g;
                                fb_c = lit_b;
                            }
                        }
                    }

                    /* Shadow map lookup — darken direct lighting if in shadow */
                    if (fog_ctx && fog_ctx->shadow_active && fog_ctx->shadow_depth) {
                        float swx = b0 * v0->wx + b1 * v1->wx + b2 * v2->wx;
                        float swy = b0 * v0->wy + b1 * v1->wy + b2 * v2->wy;
                        float swz = b0 * v0->wz + b1 * v1->wz + b2 * v2->wz;
                        const float *svp = fog_ctx->shadow_vp;
                        float lx = swx * svp[0] + swy * svp[1] + swz * svp[2] + svp[3];
                        float ly = swx * svp[4] + swy * svp[5] + swz * svp[6] + svp[7];
                        float lz = swx * svp[8] + swy * svp[9] + swz * svp[10] + svp[11];
                        float lw = swx * svp[12] + swy * svp[13] + swz * svp[14] + svp[15];
                        if (fabsf(lw) > 1e-7f) {
                            float su = (lx / lw) * 0.5f + 0.5f;
                            float sv = (1.0f - ly / lw) * 0.5f;
                            float sd = (lz / lw) * 0.5f + 0.5f;
                            if (su >= 0.0f && su < 1.0f && sv >= 0.0f && sv < 1.0f) {
                                int sxi = (int)(su * (float)fog_ctx->shadow_w);
                                int syi = (int)(sv * (float)fog_ctx->shadow_h);
                                if (sxi >= 0 && sxi < fog_ctx->shadow_w && syi >= 0 &&
                                    syi < fog_ctx->shadow_h) {
                                    float sz_map =
                                        fog_ctx->shadow_depth[syi * fog_ctx->shadow_w + sxi];
                                    /* Slope-scaled shadow bias: steeper angles get more bias */
                                    float slope_bias = fog_ctx->shadow_bias;
                                    {
                                        /* Approximate slope from depth gradient in shadow map */
                                        float dz_du = 0.0f, dz_dv = 0.0f;
                                        if (sxi + 1 < fog_ctx->shadow_w)
                                            dz_du = fog_ctx->shadow_depth[syi * fog_ctx->shadow_w +
                                                                          sxi + 1] -
                                                    sz_map;
                                        if (syi + 1 < fog_ctx->shadow_h)
                                            dz_dv =
                                                fog_ctx
                                                    ->shadow_depth[(syi + 1) * fog_ctx->shadow_w +
                                                                   sxi] -
                                                sz_map;
                                        float slope = sqrtf(dz_du * dz_du + dz_dv * dz_dv);
                                        slope_bias += fog_ctx->shadow_bias * slope * 4.0f;
                                    }
                                    if (sd > sz_map + slope_bias) {
                                        /* In shadow — keep only ambient contribution */
                                        fr *= 0.3f;
                                        fg *= 0.3f;
                                        fb_c *= 0.3f;
                                    }
                                }
                            }
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
                                                        &fr,
                                                        &fg,
                                                        &fb_c);
                    }

                    /* Distance fog — interpolate world position, compute camera distance */
                    if (fog_ctx && fog_ctx->fog_enabled) {
                        float wx = b0 * v0->wx + b1 * v1->wx + b2 * v2->wx;
                        float wy = b0 * v0->wy + b1 * v1->wy + b2 * v2->wy;
                        float wz = b0 * v0->wz + b1 * v1->wz + b2 * v2->wz;
                        float dist = sw_compute_fog_distance(fog_ctx, wx, wy, wz);
                        float fog_range = fog_ctx->fog_far - fog_ctx->fog_near;
                        float fog_f =
                            (fog_range > 1e-6f) ? (dist - fog_ctx->fog_near) / fog_range : 0.0f;
                        fog_f = fog_f < 0.0f ? 0.0f : (fog_f > 1.0f ? 1.0f : fog_f);
                        fr = fr * (1.0f - fog_f) + fog_ctx->fog_color[0] * fog_f;
                        fg = fg * (1.0f - fog_f) + fog_ctx->fog_color[1] * fog_f;
                        fb_c = fb_c * (1.0f - fog_f) + fog_ctx->fog_color[2] * fog_f;
                    }

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
                        /* Opaque: overwrite pixel + update Z-buffer */
                        zbuf[idx] = z;
                        dst[0] = (uint8_t)(clamp01f(fr) * 255.0f);
                        dst[1] = (uint8_t)(clamp01f(fg) * 255.0f);
                        dst[2] = (uint8_t)(clamp01f(fb_c) * 255.0f);
                        dst[3] = 0xFF;
                        pixels_written++;
                    } else if (!discard_fragment && fa > 0.0f) {
                        /* Transparent: additive keeps the destination, alpha uses source-over.
                         * Both skip Z writes so they don't occlude later transparent draws. */
                        if (cmd && cmd->additive_blend) {
                            dst[0] = (uint8_t)fminf(
                                255.0f, clamp01f(fr) * 255.0f * fa + (float)dst[0]);
                            dst[1] = (uint8_t)fminf(
                                255.0f, clamp01f(fg) * 255.0f * fa + (float)dst[1]);
                            dst[2] = (uint8_t)fminf(
                                255.0f, clamp01f(fb_c) * 255.0f * fa + (float)dst[2]);
                        } else {
                            float inv_a = 1.0f - fa;
                            dst[0] =
                                (uint8_t)(clamp01f(fr) * 255.0f * fa + (float)dst[0] * inv_a);
                            dst[1] =
                                (uint8_t)(clamp01f(fg) * 255.0f * fa + (float)dst[1] * inv_a);
                            dst[2] =
                                (uint8_t)(clamp01f(fb_c) * 255.0f * fa + (float)dst[2] * inv_a);
                        }
                        dst[3] = 0xFF;
                        pixels_written++;
                    }
                    /* else: alpha <= 0 → fully invisible, skip */
                }
            }
            w0 -= e12_dy;
            w1 -= e20_dy;
            w2 -= e01_dy;
        }
        row_w0 += e12_dx;
        row_w1 += e20_dx;
        row_w2 += e01_dx;
    }

    if (emit_debug) {
        fprintf(stderr,
                "[sw3d] tri %d area=%.3f orig=%.3f bbox=(%d..%d,%d..%d) inside=%d depth=%d "
                "written=%d v0=(%.2f %.2f %.3f) v1=(%.2f %.2f %.3f) v2=(%.2f %.2f %.3f)\n",
                debug_tri_count++,
                area,
                original_area,
                min_x,
                max_x,
                min_y,
                max_y,
                inside_samples,
                depth_passes,
                pixels_written,
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
static void shadow_raster_tri(
    float *depth, int32_t sw, int32_t sh, float *sx, float *sy, float *sz) {
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

    for (int y = min_y; y <= max_y; y++) {
        float w0 = row_w0, w1 = row_w1, w2 = row_w2;
        for (int x = min_x; x <= max_x; x++) {
            if (w0 >= 0.0f && w1 >= 0.0f && w2 >= 0.0f) {
                float b0 = w0 * inv_area, b1 = w1 * inv_area, b2 = w2 * inv_area;
                float z = b0 * sz[0] + b1 * sz[1] + b2 * sz[2];
                int idx = y * sw + x;
                if (z < depth[idx])
                    depth[idx] = z;
            }
            w0 -= e12_dy;
            w1 -= e20_dy;
            w2 -= e01_dy;
        }
        row_w0 += e12_dx;
        row_w1 += e20_dx;
        row_w2 += e01_dx;
    }
}

/// @brief Backend `shadow_begin` op — allocate / clear the shadow depth buffer.
///
/// Caller-provided depth buffer (so the canvas owns the storage and
/// can resize without consulting the backend). Captures the light VP
/// matrix that `shadow_draw` will use.
static void sw_shadow_begin(
    void *ctx_ptr, float *depth_buf, int32_t w, int32_t h, const float *light_vp) {
    sw_context_t *ctx = (sw_context_t *)ctx_ptr;
    if (!ctx || !depth_buf)
        return;
    ctx->shadow_depth = depth_buf;
    ctx->shadow_w = w;
    ctx->shadow_h = h;
    memcpy(ctx->shadow_vp, light_vp, 16 * sizeof(float));
    /* Clear shadow depth to FLT_MAX */
    for (int32_t i = 0; i < w * h; i++)
        depth_buf[i] = FLT_MAX;
}

/// @brief Backend `shadow_draw` op — rasterize one mesh into the shadow depth buffer.
///
/// Same vertex transform path as `submit_draw` (model matrix + light VP)
/// but feeds each triangle to `shadow_raster_tri` instead of the full
/// rasterizer. No clipping needed — out-of-bounds writes are bounds-
/// checked per-pixel.
static void sw_shadow_draw(void *ctx_ptr, const vgfx3d_draw_cmd_t *cmd) {
    sw_context_t *ctx = (sw_context_t *)ctx_ptr;
    if (!ctx || !cmd || !ctx->shadow_depth || cmd->vertex_count == 0 || cmd->index_count == 0)
        return;
    /* Skip transparent objects */
    if (cmd->additive_blend || vgfx3d_draw_cmd_uses_alpha_blend(cmd))
        return;

    /* Build light MVP = shadow_vp * model */
    float lmvp[16];
    mat4f_mul(ctx->shadow_vp, cmd->model_matrix, lmvp);

    float half_w = (float)ctx->shadow_w * 0.5f;
    float half_h = (float)ctx->shadow_h * 0.5f;

    for (uint32_t i = 0; i + 2 < cmd->index_count; i += 3) {
        uint32_t i0 = cmd->indices[i], i1 = cmd->indices[i + 1], i2 = cmd->indices[i + 2];
        if (i0 >= cmd->vertex_count || i1 >= cmd->vertex_count || i2 >= cmd->vertex_count)
            continue;

        /* Transform 3 vertices by light MVP */
        float screen_x[3], screen_y[3], screen_z[3];
        int ok = 1;
        for (int vi = 0; vi < 3; vi++) {
            const uint32_t idx[3] = {i0, i1, i2};
            const vgfx3d_vertex_t *v = &cmd->vertices[idx[vi]];
            float pos4[4] = {v->pos[0], v->pos[1], v->pos[2], 1.0f};
            float clip[4];
            mat4f_transform4(lmvp, pos4, clip);
            if (fabsf(clip[3]) < 1e-7f) {
                ok = 0;
                break;
            }
            float iw = 1.0f / clip[3];
            screen_x[vi] = (clip[0] * iw + 1.0f) * half_w;
            screen_y[vi] = (1.0f - clip[1] * iw) * half_h;
            screen_z[vi] = clip[2] * iw;
        }
        if (!ok)
            continue;

        shadow_raster_tri(
            ctx->shadow_depth, ctx->shadow_w, ctx->shadow_h, screen_x, screen_y, screen_z);
    }
}

/// @brief Backend `shadow_end` op — mark shadow map ready and store the bias.
static void sw_shadow_end(void *ctx_ptr, float bias) {
    sw_context_t *ctx = (sw_context_t *)ctx_ptr;
    if (!ctx)
        return;
    ctx->shadow_bias = bias;
    ctx->shadow_active = 1;
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
        for (int32_t y = 0; y < rt->height; y++)
            for (int32_t x = 0; x < rt->width; x++) {
                uint8_t *px = &rt->color_buf[y * rt->stride + x * 4];
                px[0] = cr;
                px[1] = cg;
                px[2] = cb;
                px[3] = 0xFF;
            }
        int32_t total = rt->width * rt->height;
        for (int32_t i = 0; i < total; i++)
            rt->depth_buf[i] = FLT_MAX;
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
        int32_t total = ctx->width * ctx->height;
        for (int32_t i = 0; i < total; i++)
            ctx->zbuf[i] = FLT_MAX;
    }
}

/// @brief Backend `begin_frame` op — capture camera + compute view-projection.
///
/// Multiplies projection × view, captures fog state and camera
/// position. Doesn't touch any pixels — drawing happens in `submit_draw`.
static void sw_begin_frame(void *ctx_ptr, const vgfx3d_camera_params_t *cam) {
    sw_context_t *ctx = (sw_context_t *)ctx_ptr;
    if (!ctx)
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
    /* Reset shadow state — rebuilt if shadows are enabled this frame */
    ctx->shadow_active = 0;
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
static void sw_submit_draw(void *ctx_ptr,
                           vgfx_window_t win,
                           const vgfx3d_draw_cmd_t *cmd,
                           const vgfx3d_light_params_t *lights,
                           int32_t light_count,
                           const float *ambient,
                           int8_t wireframe,
                           int8_t backface_cull) {
    sw_context_t *ctx = (sw_context_t *)ctx_ptr;
    if (!ctx || !cmd || cmd->vertex_count == 0 || cmd->index_count == 0)
        return;

    /* Determine output buffers: render target or window framebuffer */
    uint8_t *out_pixels;
    float *out_zbuf;
    int32_t out_w, out_h, out_stride;

    if (ctx->render_target) {
        vgfx3d_rendertarget_t *rt = ctx->render_target;
        out_pixels = rt->color_buf;
        out_zbuf = rt->depth_buf;
        out_w = rt->width;
        out_h = rt->height;
        out_stride = rt->stride;
    } else {
        vgfx_framebuffer_t fb;
        if (!vgfx_get_framebuffer(win, &fb))
            return;
        if (!sw_ensure_zbuf_capacity(ctx, fb.width, fb.height))
            return;
        out_pixels = fb.pixels;
        out_zbuf = ctx->zbuf;
        out_w = fb.width;
        out_h = fb.height;
        out_stride = fb.stride;
    }

    /* Build MVP = VP * model */
    float mvp[16];
    mat4f_mul(ctx->vp, cmd->model_matrix, mvp);

    /* Texture setup */
    sw_pixels_view tex_view, emissive_view;
    sw_pixels_view *tex_ptr = NULL, *emissive_ptr = NULL;
    if (cmd->texture) {
        const sw_pixels_view *pv = (const sw_pixels_view *)cmd->texture;
        tex_view = *pv;
        if (tex_view.width > 0 && tex_view.height > 0 && tex_view.data)
            tex_ptr = &tex_view;
    }
    if (cmd->emissive_map) {
        const sw_pixels_view *pv = (const sw_pixels_view *)cmd->emissive_map;
        emissive_view = *pv;
        if (emissive_view.width > 0 && emissive_view.height > 0 && emissive_view.data)
            emissive_ptr = &emissive_view;
    }
    sw_pixels_view normal_view, specular_view, metallic_roughness_view, ao_view;
    sw_pixels_view *normal_ptr = NULL, *specular_ptr = NULL, *metallic_roughness_ptr = NULL,
                   *ao_ptr = NULL;
    if (cmd->normal_map) {
        const sw_pixels_view *pv = (const sw_pixels_view *)cmd->normal_map;
        normal_view = *pv;
        if (normal_view.width > 0 && normal_view.height > 0 && normal_view.data)
            normal_ptr = &normal_view;
    }
    if (cmd->specular_map) {
        const sw_pixels_view *pv = (const sw_pixels_view *)cmd->specular_map;
        specular_view = *pv;
        if (specular_view.width > 0 && specular_view.height > 0 && specular_view.data)
            specular_ptr = &specular_view;
    }
    if (cmd->metallic_roughness_map) {
        const sw_pixels_view *pv = (const sw_pixels_view *)cmd->metallic_roughness_map;
        metallic_roughness_view = *pv;
        if (metallic_roughness_view.width > 0 && metallic_roughness_view.height > 0 &&
            metallic_roughness_view.data)
            metallic_roughness_ptr = &metallic_roughness_view;
    }
    if (cmd->ao_map) {
        const sw_pixels_view *pv = (const sw_pixels_view *)cmd->ao_map;
        ao_view = *pv;
        if (ao_view.width > 0 && ao_view.height > 0 && ao_view.data)
            ao_ptr = &ao_view;
    }

    float half_w = (float)out_w * 0.5f;
    float half_h = (float)out_h * 0.5f;
    int emit_debug = 0;
    static int debug_draw_count = 0;
    if (sw_debug_enabled() && debug_draw_count < 8)
        emit_debug = 1;

    /* Transform mesh vertices */
    uint32_t vc = cmd->vertex_count;
    pipe_vert_t *pv = (pipe_vert_t *)malloc(vc * sizeof(pipe_vert_t));
    if (!pv)
        return;

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
        mat4f_transform4(cmd->model_matrix, nrm4, wnrm4);
        dst->normal[0] = wnrm4[0];
        dst->normal[1] = wnrm4[1];
        dst->normal[2] = wnrm4[2];

        /* Transform tangent to world space (for TBN construction with normal maps) */
        float tan4[4] = {src->tangent[0], src->tangent[1], src->tangent[2], 0.0f};
        float wtan4[4];
        mat4f_transform4(cmd->model_matrix, tan4, wtan4);
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

        /* Vertex color (defaults to white {1,1,1,1} if not set) */
        dst->color[0] = src->color[0];
        dst->color[1] = src->color[1];
        dst->color[2] = src->color[2];
        dst->color[3] = src->color[3];

        /* Per-vertex lighting (Gouraud) — skipped when normal/PBR work is done per-pixel. */
        if (!cmd->unlit &&
            (cmd->workflow == RT_MATERIAL3D_WORKFLOW_PBR || cmd->normal_map)) {
            /* Store raw albedo: vertex_color * diffuse (lighting computed per-pixel) */
            dst->color[0] = cmd->diffuse_color[0] * dst->color[0];
            dst->color[1] = cmd->diffuse_color[1] * dst->color[1];
            dst->color[2] = cmd->diffuse_color[2] * dst->color[2];
            dst->color[3] = dst->color[3] * cmd->diffuse_color[3] * cmd->alpha;
        } else {
            compute_lighting(dst, ctx, cmd, lights, light_count, ambient);
        }
    }

    if (emit_debug) {
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
                const pipe_vert_t *p = fan[vi];
                if (fabsf(p->clip[3]) < 1e-7f) {
                    ok = 0;
                    break;
                }
                float iw = 1.0f / p->clip[3];
                sv[vi].sx = (p->clip[0] * iw + 1.0f) * half_w;
                sv[vi].sy = (1.0f - p->clip[1] * iw) * half_h;
                sv[vi].sz = p->clip[2] * iw;
                sv[vi].r = p->color[0];
                sv[vi].g = p->color[1];
                sv[vi].b = p->color[2];
                sv[vi].a = p->color[3];
                sv[vi].inv_w = iw;
                sv[vi].u_over_w = p->uv[0] * iw;
                sv[vi].v_over_w = p->uv[1] * iw;
                sv[vi].wx = p->world[0];
                sv[vi].wy = p->world[1];
                sv[vi].wz = p->world[2];
                sv[vi].nx = p->normal[0];
                sv[vi].ny = p->normal[1];
                sv[vi].nz = p->normal[2];
                sv[vi].tx = p->tangent[0];
                sv[vi].ty = p->tangent[1];
                sv[vi].tz = p->tangent[2];
                sv[vi].tw = p->tangent[3];
            }
            if (!ok)
                continue;

            if (wireframe) {
                uint8_t wr = (uint8_t)(clamp01f(sv[0].r) * 255.0f);
                uint8_t wg = (uint8_t)(clamp01f(sv[0].g) * 255.0f);
                uint8_t wb = (uint8_t)(clamp01f(sv[0].b) * 255.0f);
                draw_line(out_pixels,
                          out_w,
                          out_h,
                          out_stride,
                          (int)sv[0].sx,
                          (int)sv[0].sy,
                          (int)sv[1].sx,
                          (int)sv[1].sy,
                          wr,
                          wg,
                          wb);
                draw_line(out_pixels,
                          out_w,
                          out_h,
                          out_stride,
                          (int)sv[1].sx,
                          (int)sv[1].sy,
                          (int)sv[2].sx,
                          (int)sv[2].sy,
                          wr,
                          wg,
                          wb);
                draw_line(out_pixels,
                          out_w,
                          out_h,
                          out_stride,
                          (int)sv[2].sx,
                          (int)sv[2].sy,
                          (int)sv[0].sx,
                          (int)sv[0].sy,
                          wr,
                          wg,
                          wb);
            } else {
                raster_triangle(out_pixels,
                                out_zbuf,
                                out_w,
                                out_h,
                                out_stride,
                                &sv[0],
                                &sv[1],
                                &sv[2],
                                tex_ptr,
                                emissive_ptr,
                                cmd->emissive_color,
                                normal_ptr,
                                specular_ptr,
                                metallic_roughness_ptr,
                                ao_ptr,
                                cmd,
                                lights,
                                light_count,
                                ambient,
                                backface_cull,
                                ctx);
            }
        }
    }

    free(pv);
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
    if (ctx)
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
