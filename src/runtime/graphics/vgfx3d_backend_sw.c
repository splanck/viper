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

typedef struct
{
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
} sw_context_t;

static int sw_ensure_zbuf_capacity(sw_context_t *ctx, int32_t width, int32_t height)
{
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

static void mat4f_mul(const float *a, const float *b, float *out)
{
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
            out[r * 4 + c] = a[r * 4 + 0] * b[0 * 4 + c] + a[r * 4 + 1] * b[1 * 4 + c] +
                             a[r * 4 + 2] * b[2 * 4 + c] + a[r * 4 + 3] * b[3 * 4 + c];
}

static void mat4f_transform4(const float *m, const float *in, float *out)
{
    out[0] = m[0] * in[0] + m[1] * in[1] + m[2] * in[2] + m[3] * in[3];
    out[1] = m[4] * in[0] + m[5] * in[1] + m[6] * in[2] + m[7] * in[3];
    out[2] = m[8] * in[0] + m[9] * in[1] + m[10] * in[2] + m[11] * in[3];
    out[3] = m[12] * in[0] + m[13] * in[1] + m[14] * in[2] + m[15] * in[3];
}

/*==========================================================================
 * Pipeline vertex
 *=========================================================================*/

typedef struct
{
    float clip[4];
    float world[3];
    float normal[3];
    float uv[2];
    float color[4];
} pipe_vert_t;

#define MAX_CLIP_VERTS 9

static void pipe_lerp(const pipe_vert_t *a, const pipe_vert_t *b, float t, pipe_vert_t *out)
{
    float s = 1.0f - t;
    for (int i = 0; i < 4; i++)
        out->clip[i] = s * a->clip[i] + t * b->clip[i];
    for (int i = 0; i < 3; i++)
        out->world[i] = s * a->world[i] + t * b->world[i];
    for (int i = 0; i < 3; i++)
        out->normal[i] = s * a->normal[i] + t * b->normal[i];
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
static float clip_dist(const pipe_vert_t *v, int plane)
{
    float x = v->clip[0], y = v->clip[1], z = v->clip[2], w = v->clip[3];
    switch (plane)
    {
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
static int clip_poly_plane(const pipe_vert_t *in, int in_count, pipe_vert_t *out, int plane)
{
    if (in_count < 1)
        return 0;
    int out_count = 0;
    for (int i = 0; i < in_count; i++)
    {
        int j = (i + 1) % in_count;
        float di = clip_dist(&in[i], plane);
        float dj = clip_dist(&in[j], plane);
        if (di >= 0.0f)
        {
            if (out_count < MAX_CLIP_VERTS)
                out[out_count++] = in[i];
            if (dj < 0.0f)
            {
                float denom = di - dj;
                if (fabsf(denom) > 1e-10f)
                {
                    float t = di / denom;
                    if (out_count < MAX_CLIP_VERTS)
                        pipe_lerp(&in[i], &in[j], t, &out[out_count++]);
                }
            }
        }
        else if (dj >= 0.0f)
        {
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
static int clip_triangle(const pipe_vert_t *tri, pipe_vert_t *out)
{
    pipe_vert_t buf_a[MAX_CLIP_VERTS], buf_b[MAX_CLIP_VERTS];
    memcpy(buf_a, tri, 3 * sizeof(pipe_vert_t));
    int count = 3;
    for (int plane = 0; plane < 6; plane++)
    {
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
                             const float *ambient)
{
    if (cmd->unlit)
    {
        v->color[0] = cmd->diffuse_color[0];
        v->color[1] = cmd->diffuse_color[1];
        v->color[2] = cmd->diffuse_color[2];
        v->color[3] = cmd->diffuse_color[3];
        return;
    }

    float nx = v->normal[0], ny = v->normal[1], nz = v->normal[2];
    float nlen = sqrtf(nx * nx + ny * ny + nz * nz);
    if (nlen > 1e-7f)
    {
        nx /= nlen;
        ny /= nlen;
        nz /= nlen;
    }

    float vx = cam_pos[0] - v->world[0];
    float vy = cam_pos[1] - v->world[1];
    float vz = cam_pos[2] - v->world[2];
    float vlen = sqrtf(vx * vx + vy * vy + vz * vz);
    if (vlen > 1e-7f)
    {
        vx /= vlen;
        vy /= vlen;
        vz /= vlen;
    }

    float r = ambient[0] * cmd->diffuse_color[0];
    float g = ambient[1] * cmd->diffuse_color[1];
    float b = ambient[2] * cmd->diffuse_color[2];

    for (int32_t li = 0; li < light_count; li++)
    {
        const vgfx3d_light_params_t *light = &lights[li];
        float lx, ly, lz, atten = 1.0f;

        if (light->type == 0) /* directional */
        {
            lx = -light->direction[0];
            ly = -light->direction[1];
            lz = -light->direction[2];
            float ll = sqrtf(lx * lx + ly * ly + lz * lz);
            if (ll > 1e-7f)
            {
                lx /= ll;
                ly /= ll;
                lz /= ll;
            }
        }
        else if (light->type == 1) /* point */
        {
            lx = light->position[0] - v->world[0];
            ly = light->position[1] - v->world[1];
            lz = light->position[2] - v->world[2];
            float dist = sqrtf(lx * lx + ly * ly + lz * lz);
            if (dist > 1e-7f)
            {
                lx /= dist;
                ly /= dist;
                lz /= dist;
            }
            atten = 1.0f / (1.0f + light->attenuation * dist * dist);
        }
        else /* ambient */
        {
            r += light->color[0] * light->intensity * cmd->diffuse_color[0];
            g += light->color[1] * light->intensity * cmd->diffuse_color[1];
            b += light->color[2] * light->intensity * cmd->diffuse_color[2];
            continue;
        }

        float intensity = light->intensity;
        float ndl = nx * lx + ny * ly + nz * lz;
        if (ndl < 0.0f)
            ndl = 0.0f;

        r += light->color[0] * intensity * ndl * cmd->diffuse_color[0] * atten;
        g += light->color[1] * intensity * ndl * cmd->diffuse_color[1] * atten;
        b += light->color[2] * intensity * ndl * cmd->diffuse_color[2] * atten;

        if (ndl > 0.0f && cmd->shininess > 0.0f)
        {
            float hx = lx + vx, hy = ly + vy, hz = lz + vz;
            float hlen = sqrtf(hx * hx + hy * hy + hz * hz);
            if (hlen > 1e-7f)
            {
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
    v->color[3] = cmd->diffuse_color[3] * cmd->alpha;
}

/*==========================================================================
 * Texture sampling
 *=========================================================================*/

typedef struct
{
    int64_t width;
    int64_t height;
    uint32_t *data;
} sw_pixels_view;

static void sample_texture(
    const sw_pixels_view *tex, float u, float v, float *r, float *g, float *b, float *a)
{
    u = u - floorf(u);
    v = v - floorf(v);
    int x = (int)(u * (float)tex->width);
    int y = (int)(v * (float)tex->height);
    if (x < 0)
        x = 0;
    if (x >= (int)tex->width)
        x = (int)tex->width - 1;
    if (y < 0)
        y = 0;
    if (y >= (int)tex->height)
        y = (int)tex->height - 1;
    uint32_t pixel = tex->data[y * tex->width + x];
    *r = (float)((pixel >> 24) & 0xFF) / 255.0f;
    *g = (float)((pixel >> 16) & 0xFF) / 255.0f;
    *b = (float)((pixel >> 8) & 0xFF) / 255.0f;
    *a = (float)(pixel & 0xFF) / 255.0f;
}

/*==========================================================================
 * Edge-function triangle rasterizer
 *=========================================================================*/

typedef struct
{
    float sx, sy, sz;
    float r, g, b, a;
    float u_over_w, v_over_w, inv_w;
    float wx, wy, wz; /* world position (for fog distance computation) */
} screen_vert_t;

static inline float clamp01f(float x)
{
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
                            int8_t backface_cull,
                            const sw_context_t *fog_ctx)
{
    float area = (v1->sx - v0->sx) * (v2->sy - v0->sy) - (v2->sx - v0->sx) * (v1->sy - v0->sy);

    /* After viewport Y-flip, CCW world-space triangles have NEGATIVE screen-space
     * area. So negative area = front face, positive area = back face.
     * Cull back faces (positive area) when backface culling is enabled. */
    if (backface_cull && area >= 0.0f)
        return;
    if (area < 0.0f)
    {
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

    for (int y = min_y; y <= max_y; y++)
    {
        float w0 = row_w0, w1 = row_w1, w2 = row_w2;
        for (int x = min_x; x <= max_x; x++)
        {
            if (w0 >= 0.0f && w1 >= 0.0f && w2 >= 0.0f)
            {
                /* Barycentric weights from edge functions; z is linearly
                 * interpolated in screen space (not perspective-correct,
                 * but sufficient for depth testing). */
                float b0 = w0 * inv_area, b1 = w1 * inv_area, b2 = w2 * inv_area;
                float z = b0 * v0->sz + b1 * v1->sz + b2 * v2->sz;
                int idx = y * fb_w + x;
                if (z < zbuf[idx])
                {
                    float fr = b0 * v0->r + b1 * v1->r + b2 * v2->r;
                    float fg = b0 * v0->g + b1 * v1->g + b2 * v2->g;
                    float fb_c = b0 * v0->b + b1 * v1->b + b2 * v2->b;
                    float tex_alpha = 1.0f; /* per-texel alpha (for foliage, fences) */
                    if (tex)
                    {
                        float iw = b0 * v0->inv_w + b1 * v1->inv_w + b2 * v2->inv_w;
                        if (fabsf(iw) > 1e-7f)
                        {
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
                    /* Emissive map sampling (per-pixel, additive) */
                    if (emissive_tex)
                    {
                        float iw = b0 * v0->inv_w + b1 * v1->inv_w + b2 * v2->inv_w;
                        if (fabsf(iw) > 1e-7f)
                        {
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

                    /* Distance fog — interpolate world position, compute camera distance */
                    if (fog_ctx && fog_ctx->fog_enabled)
                    {
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
                    if (fa >= 1.0f)
                    {
                        /* Opaque: overwrite pixel + update Z-buffer */
                        zbuf[idx] = z;
                        dst[0] = (uint8_t)(clamp01f(fr) * 255.0f);
                        dst[1] = (uint8_t)(clamp01f(fg) * 255.0f);
                        dst[2] = (uint8_t)(clamp01f(fb_c) * 255.0f);
                        dst[3] = 0xFF;
                    }
                    else if (fa > 0.0f)
                    {
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
                      uint8_t b)
{
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;)
    {
        if (x0 >= 0 && x0 < fb_w && y0 >= 0 && y0 < fb_h)
        {
            uint8_t *dst = &pixels[y0 * stride + x0 * 4];
            dst[0] = r;
            dst[1] = g;
            dst[2] = b;
            dst[3] = 0xFF;
        }
        if (x0 == x1 && y0 == y1)
            break;
        int e2 = 2 * err;
        if (e2 >= dy)
        {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx)
        {
            err += dx;
            y0 += sy;
        }
    }
}

/*==========================================================================
 * Backend vtable implementation
 *=========================================================================*/

static void *sw_create_ctx(vgfx_window_t win, int32_t w, int32_t h)
{
    sw_context_t *ctx = (sw_context_t *)calloc(1, sizeof(sw_context_t));
    if (!ctx)
        return NULL;

    /* Use physical framebuffer dimensions (HiDPI-aware), not logical dimensions.
     * The rasterizer writes to fb.pixels which is at physical resolution. */
    vgfx_framebuffer_t fb;
    if (win && vgfx_get_framebuffer(win, &fb))
    {
        ctx->width = fb.width;
        ctx->height = fb.height;
    }
    else
    {
        ctx->width = w;
        ctx->height = h;
    }

    if (!sw_ensure_zbuf_capacity(ctx, ctx->width, ctx->height))
    {
        free(ctx);
        return NULL;
    }
    return ctx;
}

static void sw_destroy_ctx(void *ctx_ptr)
{
    if (!ctx_ptr)
        return;
    sw_context_t *ctx = (sw_context_t *)ctx_ptr;
    free(ctx->zbuf);
    free(ctx);
}

static void sw_clear(void *ctx_ptr, vgfx_window_t win, float r, float g, float b)
{
    sw_context_t *ctx = (sw_context_t *)ctx_ptr;
    if (!ctx)
        return;

    uint8_t cr = (uint8_t)(clamp01f(r) * 255.0f);
    uint8_t cg = (uint8_t)(clamp01f(g) * 255.0f);
    uint8_t cb = (uint8_t)(clamp01f(b) * 255.0f);

    if (ctx->render_target)
    {
        vgfx3d_rendertarget_t *rt = ctx->render_target;
        for (int32_t y = 0; y < rt->height; y++)
            for (int32_t x = 0; x < rt->width; x++)
            {
                uint8_t *px = &rt->color_buf[y * rt->stride + x * 4];
                px[0] = cr;
                px[1] = cg;
                px[2] = cb;
                px[3] = 0xFF;
            }
        int32_t total = rt->width * rt->height;
        for (int32_t i = 0; i < total; i++)
            rt->depth_buf[i] = FLT_MAX;
    }
    else
    {
        vgfx_framebuffer_t fb;
        if (vgfx_get_framebuffer(win, &fb))
        {
            if (!sw_ensure_zbuf_capacity(ctx, fb.width, fb.height))
                return;
            for (int32_t y = 0; y < fb.height; y++)
                for (int32_t x = 0; x < fb.width; x++)
                {
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

static void sw_begin_frame(void *ctx_ptr, const vgfx3d_camera_params_t *cam)
{
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
}

static void sw_submit_draw(void *ctx_ptr,
                           vgfx_window_t win,
                           const vgfx3d_draw_cmd_t *cmd,
                           const vgfx3d_light_params_t *lights,
                           int32_t light_count,
                           const float *ambient,
                           int8_t wireframe,
                           int8_t backface_cull)
{
    sw_context_t *ctx = (sw_context_t *)ctx_ptr;
    if (!ctx || !cmd || cmd->vertex_count == 0 || cmd->index_count == 0)
        return;

    /* Determine output buffers: render target or window framebuffer */
    uint8_t *out_pixels;
    float *out_zbuf;
    int32_t out_w, out_h, out_stride;

    if (ctx->render_target)
    {
        vgfx3d_rendertarget_t *rt = ctx->render_target;
        out_pixels = rt->color_buf;
        out_zbuf = rt->depth_buf;
        out_w = rt->width;
        out_h = rt->height;
        out_stride = rt->stride;
    }
    else
    {
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
    if (cmd->texture)
    {
        const sw_pixels_view *pv = (const sw_pixels_view *)cmd->texture;
        tex_view = *pv;
        if (tex_view.width > 0 && tex_view.height > 0 && tex_view.data)
            tex_ptr = &tex_view;
    }
    if (cmd->emissive_map)
    {
        const sw_pixels_view *pv = (const sw_pixels_view *)cmd->emissive_map;
        emissive_view = *pv;
        if (emissive_view.width > 0 && emissive_view.height > 0 && emissive_view.data)
            emissive_ptr = &emissive_view;
    }

    float half_w = (float)out_w * 0.5f;
    float half_h = (float)out_h * 0.5f;

    /* Transform mesh vertices */
    uint32_t vc = cmd->vertex_count;
    pipe_vert_t *pv = (pipe_vert_t *)malloc(vc * sizeof(pipe_vert_t));
    if (!pv)
        return;

    for (uint32_t i = 0; i < vc; i++)
    {
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

        /* Clip-space */
        float clip[4];
        mat4f_transform4(mvp, pos4, clip);
        dst->clip[0] = clip[0];
        dst->clip[1] = clip[1];
        dst->clip[2] = clip[2];
        dst->clip[3] = clip[3];

        dst->uv[0] = src->uv[0];
        dst->uv[1] = src->uv[1];

        /* Per-vertex lighting */
        compute_lighting(dst, ctx->cam_pos, cmd, lights, light_count, ambient);
    }

    /* Process triangles: clip → rasterize */
    pipe_vert_t clipped[MAX_CLIP_VERTS];

    for (uint32_t i = 0; i + 2 < cmd->index_count; i += 3)
    {
        uint32_t i0 = cmd->indices[i], i1 = cmd->indices[i + 1], i2 = cmd->indices[i + 2];
        if (i0 >= vc || i1 >= vc || i2 >= vc)
            continue;

        pipe_vert_t tri[3] = {pv[i0], pv[i1], pv[i2]};
        int clip_count = clip_triangle(tri, clipped);
        if (clip_count < 3)
            continue;

        for (int t = 1; t < clip_count - 1; t++)
        {
            screen_vert_t sv[3];
            const pipe_vert_t *fan[3] = {&clipped[0], &clipped[t], &clipped[t + 1]};
            int ok = 1;
            for (int vi = 0; vi < 3; vi++)
            {
                const pipe_vert_t *p = fan[vi];
                if (fabsf(p->clip[3]) < 1e-7f)
                {
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
            }
            if (!ok)
                continue;

            if (wireframe)
            {
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
            }
            else
            {
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
                                backface_cull,
                                ctx);
            }
        }
    }

    free(pv);
}

static void sw_end_frame(void *ctx_ptr)
{
    (void)ctx_ptr;
}

static void sw_set_render_target(void *ctx_ptr, vgfx3d_rendertarget_t *rt)
{
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
    .present = NULL, /* software renders to CPU framebuffer; vgfx_update handles display */
};

/* Stub for platforms without a GPU layer to hide */
#if !defined(__APPLE__)
void vgfx3d_hide_gpu_layer(void *backend_ctx)
{
    (void)backend_ctx;
}

void vgfx3d_show_gpu_layer(void *backend_ctx)
{
    (void)backend_ctx;
}
#endif

const vgfx3d_backend_t *vgfx3d_select_backend(void)
{
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
