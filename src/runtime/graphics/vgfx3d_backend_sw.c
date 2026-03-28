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

#include "rt_canvas3d_internal.h"
#include "vgfx3d_backend.h"

#include <float.h>
#include <math.h>
#include <stdint.h>
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
    /* Render target override (NULL = render to window framebuffer) */
    vgfx3d_rendertarget_t *render_target;
    /* Fog parameters (copied from Canvas3D each begin_frame) */
    int8_t fog_enabled;
    float fog_near, fog_far;
    float fog_color[3];
    /* Shadow mapping state */
    float *shadow_depth;       /* shadow depth buffer (during shadow pass) */
    int32_t shadow_w, shadow_h;
    float shadow_vp[16];       /* light view-projection matrix */
    float shadow_bias;
    int8_t shadow_active;      /* 1 = shadow map is populated and ready for lookup */
} sw_context_t;

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

static void mat4f_mul(const float *a, const float *b, float *out) {
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
            out[r * 4 + c] = a[r * 4 + 0] * b[0 * 4 + c] + a[r * 4 + 1] * b[1 * 4 + c] +
                             a[r * 4 + 2] * b[2 * 4 + c] + a[r * 4 + 3] * b[3 * 4 + c];
}

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
    float tangent[3];
    float uv[2];
    float color[4];
} pipe_vert_t;

#define MAX_CLIP_VERTS 9

static void pipe_lerp(const pipe_vert_t *a, const pipe_vert_t *b, float t, pipe_vert_t *out) {
    float s = 1.0f - t;
    for (int i = 0; i < 4; i++)
        out->clip[i] = s * a->clip[i] + t * b->clip[i];
    for (int i = 0; i < 3; i++)
        out->world[i] = s * a->world[i] + t * b->world[i];
    for (int i = 0; i < 3; i++)
        out->normal[i] = s * a->normal[i] + t * b->normal[i];
    for (int i = 0; i < 3; i++)
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
/// Walks each edge (i→j). If i is inside, emit it; if the edge crosses the
/// plane, emit the intersection. Result is a new convex polygon in @p out.
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

static void compute_lighting(pipe_vert_t *v,
                             const float *cam_pos,
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

    float vx = cam_pos[0] - v->world[0];
    float vy = cam_pos[1] - v->world[1];
    float vz = cam_pos[2] - v->world[2];
    float vlen = sqrtf(vx * vx + vy * vy + vz * vz);
    if (vlen > 1e-7f) {
        vx /= vlen;
        vy /= vlen;
        vz /= vlen;
    }

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
            float spot_dot = -(lx * light->direction[0] + ly * light->direction[1] +
                               lz * light->direction[2]);
            if (spot_dot < light->outer_cos)
                atten = 0.0f; /* outside cone */
            else if (spot_dot < light->inner_cos) {
                float t = (spot_dot - light->outer_cos) /
                          (light->inner_cos - light->outer_cos);
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
    r += cmd->emissive_color[0];
    g += cmd->emissive_color[1];
    b += cmd->emissive_color[2];

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

static int setup_pixels_view(const void *pixels_obj, sw_pixels_view *out) {
    if (!pixels_obj)
        return 0;
    const sw_pixels_view *pv = (const sw_pixels_view *)pixels_obj;
    *out = *pv;
    return (out->width > 0 && out->height > 0 && out->data != NULL);
}

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

    *r = (((p00 >> 24) & 0xFF) * w00 + ((p10 >> 24) & 0xFF) * w10 +
          ((p01 >> 24) & 0xFF) * w01 + ((p11 >> 24) & 0xFF) * w11) /
         255.0f;
    *g = (((p00 >> 16) & 0xFF) * w00 + ((p10 >> 16) & 0xFF) * w10 +
          ((p01 >> 16) & 0xFF) * w01 + ((p11 >> 16) & 0xFF) * w11) /
         255.0f;
    *b = (((p00 >> 8) & 0xFF) * w00 + ((p10 >> 8) & 0xFF) * w10 +
          ((p01 >> 8) & 0xFF) * w01 + ((p11 >> 8) & 0xFF) * w11) /
         255.0f;
    *a = ((p00 & 0xFF) * w00 + (p10 & 0xFF) * w10 + (p01 & 0xFF) * w01 +
          (p11 & 0xFF) * w11) /
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
    float tx, ty, tz; /* world tangent (for TBN matrix construction) */
} screen_vert_t;

static inline float clamp01f(float x) {
    return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x);
}

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
                            const vgfx3d_draw_cmd_t *cmd,
                            const vgfx3d_light_params_t *lights,
                            int32_t light_count,
                            const float *ambient,
                            int8_t backface_cull,
                            const sw_context_t *fog_ctx) {
    float area = (v1->sx - v0->sx) * (v2->sy - v0->sy) - (v2->sx - v0->sx) * (v1->sy - v0->sy);

    /* After viewport Y-flip, CCW world-space triangles have NEGATIVE screen-space
     * area. So negative area = front face, positive area = back face.
     * Cull back faces (positive area) when backface culling is enabled. */
    if (backface_cull && area >= 0.0f)
        return;
    if (area < 0.0f) {
        const screen_vert_t *tmp = v1;
        v1 = v2;
        v2 = tmp;
        area = -area;
    }
    if (area < 1e-6f)
        return;

    float inv_area = 1.0f / area;
    int min_x = (int)fmaxf(fminf(fminf(v0->sx, v1->sx), v2->sx), 0.0f);
    int max_x = (int)fminf(fmaxf(fmaxf(v0->sx, v1->sx), v2->sx), (float)(fb_w - 1));
    int min_y = (int)fmaxf(fminf(fminf(v0->sy, v1->sy), v2->sy), 0.0f);
    int max_y = (int)fminf(fmaxf(fmaxf(v0->sy, v1->sy), v2->sy), (float)(fb_h - 1));
    if (min_x > max_x || min_y > max_y)
        return;

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
                /* Barycentric weights from edge functions; z is linearly
                 * interpolated in screen space (not perspective-correct,
                 * but sufficient for depth testing). */
                float b0 = w0 * inv_area, b1 = w1 * inv_area, b2 = w2 * inv_area;
                float z = b0 * v0->sz + b1 * v1->sz + b2 * v2->sz;
                int idx = y * fb_w + x;
                if (z < zbuf[idx]) {
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

                    /* Emissive map sampling (per-pixel, additive) */
                    if (emissive_tex) {
                        float iw = b0 * v0->inv_w + b1 * v1->inv_w + b2 * v2->inv_w;
                        if (fabsf(iw) > 1e-7f) {
                            float u =
                                (b0 * v0->u_over_w + b1 * v1->u_over_w + b2 * v2->u_over_w) / iw;
                            float vc =
                                (b0 * v0->v_over_w + b1 * v1->v_over_w + b2 * v2->v_over_w) / iw;
                            float er, eg, eb, ea;
                            sample_texture(emissive_tex, u, vc, &er, &eg, &eb, &ea);
                            fr += er * emissive_color[0];
                            fg += eg * emissive_color[1];
                            fb_c += eb * emissive_color[2];
                        }
                    }

                    /* Per-pixel lighting with normal map (replaces Gouraud colors) */
                    if (normal_map && cmd && !cmd->unlit && lights) {
                        float pp_iw = b0 * v0->inv_w + b1 * v1->inv_w + b2 * v2->inv_w;
                        if (fabsf(pp_iw) > 1e-7f) {
                            float pp_u =
                                (b0 * v0->u_over_w + b1 * v1->u_over_w + b2 * v2->u_over_w) /
                                pp_iw;
                            float pp_vc =
                                (b0 * v0->v_over_w + b1 * v1->v_over_w + b2 * v2->v_over_w) /
                                pp_iw;

                            /* Interpolate world normal */
                            float pnx = b0 * v0->nx + b1 * v1->nx + b2 * v2->nx;
                            float pny = b0 * v0->ny + b1 * v1->ny + b2 * v2->ny;
                            float pnz = b0 * v0->nz + b1 * v1->nz + b2 * v2->nz;
                            float nlen = sqrtf(pnx * pnx + pny * pny + pnz * pnz);
                            if (nlen > 1e-7f) {
                                pnx /= nlen;
                                pny /= nlen;
                                pnz /= nlen;
                            }

                            /* Interpolate tangent + build TBN */
                            float ptx = b0 * v0->tx + b1 * v1->tx + b2 * v2->tx;
                            float pty = b0 * v0->ty + b1 * v1->ty + b2 * v2->ty;
                            float ptz = b0 * v0->tz + b1 * v1->tz + b2 * v2->tz;
                            float tlen = sqrtf(ptx * ptx + pty * pty + ptz * ptz);

                            if (tlen > 1e-7f) {
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
                                float map_x = tnr * 2.0f - 1.0f;
                                float map_y = tng * 2.0f - 1.0f;
                                float map_z = tnb * 2.0f - 1.0f;

                                /* Bitangent = N × T */
                                float bbx = pny * ptz - pnz * pty;
                                float bby = pnz * ptx - pnx * ptz;
                                float bbz = pnx * pty - pny * ptx;

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

                            float vdx = fog_ctx->cam_pos[0] - wx;
                            float vdy = fog_ctx->cam_pos[1] - wy;
                            float vdz = fog_ctx->cam_pos[2] - wz;
                            float vdlen = sqrtf(vdx * vdx + vdy * vdy + vdz * vdz);
                            if (vdlen > 1e-7f) {
                                vdx /= vdlen;
                                vdy /= vdlen;
                                vdz /= vdlen;
                            }

                            /* Ambient */
                            float lit_r = ambient[0] * fr;
                            float lit_g = ambient[1] * fg;
                            float lit_b = ambient[2] * fb_c;

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
                                    float ll = sqrtf(llx * llx + lly * lly + llz * llz);
                                    if (ll > 1e-7f) {
                                        llx /= ll;
                                        lly /= ll;
                                        llz /= ll;
                                    }
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
                                        float st = (sd - lt->outer_cos) /
                                                   (lt->inner_cos - lt->outer_cos);
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
                                    float hlen = sqrtf(hx * hx + hy * hy + hz * hz);
                                    if (hlen > 1e-7f) {
                                        hx /= hlen;
                                        hy /= hlen;
                                        hz /= hlen;
                                    }
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
                            lit_r += cmd->emissive_color[0];
                            lit_g += cmd->emissive_color[1];
                            lit_b += cmd->emissive_color[2];

                            fr = lit_r;
                            fg = lit_g;
                            fb_c = lit_b;
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
                                    if (sd > sz_map + fog_ctx->shadow_bias) {
                                        /* In shadow — keep only ambient contribution */
                                        fr *= 0.3f;
                                        fg *= 0.3f;
                                        fb_c *= 0.3f;
                                    }
                                }
                            }
                        }
                    }

                    /* Distance fog — interpolate world position, compute camera distance */
                    if (fog_ctx && fog_ctx->fog_enabled) {
                        float wx = b0 * v0->wx + b1 * v1->wx + b2 * v2->wx;
                        float wy = b0 * v0->wy + b1 * v1->wy + b2 * v2->wy;
                        float wz = b0 * v0->wz + b1 * v1->wz + b2 * v2->wz;
                        float fdx = wx - fog_ctx->cam_pos[0];
                        float fdy = wy - fog_ctx->cam_pos[1];
                        float fdz = wz - fog_ctx->cam_pos[2];
                        float dist = sqrtf(fdx * fdx + fdy * fdy + fdz * fdz);
                        float fog_range = fog_ctx->fog_far - fog_ctx->fog_near;
                        float fog_f =
                            (fog_range > 1e-6f) ? (dist - fog_ctx->fog_near) / fog_range : 0.0f;
                        fog_f = fog_f < 0.0f ? 0.0f : (fog_f > 1.0f ? 1.0f : fog_f);
                        fr = fr * (1.0f - fog_f) + fog_ctx->fog_color[0] * fog_f;
                        fg = fg * (1.0f - fog_f) + fog_ctx->fog_color[1] * fog_f;
                        fb_c = fb_c * (1.0f - fog_f) + fog_ctx->fog_color[2] * fog_f;
                    }

                    /* Interpolate alpha, including per-texel alpha */
                    float fa = (b0 * v0->a + b1 * v1->a + b2 * v2->a) * tex_alpha;

                    uint8_t *dst = &pixels[y * stride + x * 4];
                    if (fa >= 1.0f) {
                        /* Opaque: overwrite pixel + update Z-buffer */
                        zbuf[idx] = z;
                        dst[0] = (uint8_t)(clamp01f(fr) * 255.0f);
                        dst[1] = (uint8_t)(clamp01f(fg) * 255.0f);
                        dst[2] = (uint8_t)(clamp01f(fb_c) * 255.0f);
                        dst[3] = 0xFF;
                    } else if (fa > 0.0f) {
                        /* Transparent: alpha blend (src*a + dst*(1-a)).
                         * No Z-buffer write — transparent fragments don't occlude. */
                        float inv_a = 1.0f - fa;
                        dst[0] = (uint8_t)(clamp01f(fr) * 255.0f * fa + (float)dst[0] * inv_a);
                        dst[1] = (uint8_t)(clamp01f(fg) * 255.0f * fa + (float)dst[1] * inv_a);
                        dst[2] = (uint8_t)(clamp01f(fb_c) * 255.0f * fa + (float)dst[2] * inv_a);
                        dst[3] = 0xFF;
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
}

/*==========================================================================
 * Wireframe — Bresenham line
 *=========================================================================*/

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
static void shadow_raster_tri(float *depth,
                              int32_t sw,
                              int32_t sh,
                              float *sx,
                              float *sy,
                              float *sz) {
    /* Screen-space area (winding check) */
    float area = (sx[1] - sx[0]) * (sy[2] - sy[0]) - (sx[2] - sx[0]) * (sy[1] - sy[0]);
    if (area < 0.0f) {
        /* Swap v1/v2 to ensure positive area */
        float t;
        t = sx[1]; sx[1] = sx[2]; sx[2] = t;
        t = sy[1]; sy[1] = sy[2]; sy[2] = t;
        t = sz[1]; sz[1] = sz[2]; sz[2] = t;
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

static void sw_shadow_begin(void *ctx_ptr, float *depth_buf, int32_t w, int32_t h,
                            const float *light_vp) {
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

static void sw_shadow_draw(void *ctx_ptr, const vgfx3d_draw_cmd_t *cmd) {
    sw_context_t *ctx = (sw_context_t *)ctx_ptr;
    if (!ctx || !cmd || !ctx->shadow_depth || cmd->vertex_count == 0 || cmd->index_count == 0)
        return;
    /* Skip transparent objects */
    if (cmd->alpha < 1.0f)
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

        shadow_raster_tri(ctx->shadow_depth, ctx->shadow_w, ctx->shadow_h, screen_x, screen_y,
                          screen_z);
    }
}

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

static void sw_destroy_ctx(void *ctx_ptr) {
    if (!ctx_ptr)
        return;
    sw_context_t *ctx = (sw_context_t *)ctx_ptr;
    free(ctx->zbuf);
    free(ctx);
}

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

static void sw_begin_frame(void *ctx_ptr, const vgfx3d_camera_params_t *cam) {
    sw_context_t *ctx = (sw_context_t *)ctx_ptr;
    if (!ctx)
        return;
    /* VP = projection * view */
    mat4f_mul(cam->projection, cam->view, ctx->vp);
    ctx->cam_pos[0] = cam->position[0];
    ctx->cam_pos[1] = cam->position[1];
    ctx->cam_pos[2] = cam->position[2];
    ctx->fog_enabled = cam->fog_enabled;
    ctx->fog_near = cam->fog_near;
    ctx->fog_far = cam->fog_far;
    ctx->fog_color[0] = cam->fog_color[0];
    ctx->fog_color[1] = cam->fog_color[1];
    ctx->fog_color[2] = cam->fog_color[2];
    /* Reset shadow state — rebuilt if shadows are enabled this frame */
    ctx->shadow_active = 0;
}

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
    sw_pixels_view normal_view, specular_view;
    sw_pixels_view *normal_ptr = NULL, *specular_ptr = NULL;
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

    float half_w = (float)out_w * 0.5f;
    float half_h = (float)out_h * 0.5f;

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

        /* Per-vertex lighting (Gouraud) — skipped when normal map is present
         * because per-pixel lighting will be computed in raster_triangle instead */
        if (!cmd->unlit && cmd->normal_map) {
            /* Store raw albedo: vertex_color * diffuse (lighting computed per-pixel) */
            dst->color[0] = cmd->diffuse_color[0] * dst->color[0];
            dst->color[1] = cmd->diffuse_color[1] * dst->color[1];
            dst->color[2] = cmd->diffuse_color[2] * dst->color[2];
            dst->color[3] = dst->color[3] * cmd->diffuse_color[3] * cmd->alpha;
        } else {
            compute_lighting(dst, ctx->cam_pos, cmd, lights, light_count, ambient);
        }
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

static void sw_end_frame(void *ctx_ptr) {
    (void)ctx_ptr;
}

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
    .begin_frame = sw_begin_frame,
    .submit_draw = sw_submit_draw,
    .end_frame = sw_end_frame,
    .set_render_target = sw_set_render_target,
    .shadow_begin = sw_shadow_begin,
    .shadow_draw = sw_shadow_draw,
    .shadow_end = sw_shadow_end,
    .present = NULL, /* software renders to CPU framebuffer; vgfx_update handles display */
};

/* Stub for platforms without a GPU layer to hide */
#if !defined(__APPLE__)
void vgfx3d_hide_gpu_layer(void *backend_ctx) {
    (void)backend_ctx;
}

void vgfx3d_show_gpu_layer(void *backend_ctx) {
    (void)backend_ctx;
}
#endif

const vgfx3d_backend_t *vgfx3d_select_backend(void) {
    /* Try GPU backend first, fall back to software.
     * GPU backend init is tested in create_ctx — if it returns NULL,
     * Canvas3D.New will detect the failure and we don't get here.
     * So selection just returns the preferred backend for the platform. */
#if defined(__APPLE__)
    return &vgfx3d_metal_backend;
#elif defined(_WIN32)
    return &vgfx3d_d3d11_backend;
#elif defined(__linux__)
    return &vgfx3d_opengl_backend;
#else
    return &vgfx3d_software_backend;
#endif
}

#endif /* VIPER_ENABLE_GRAPHICS */
