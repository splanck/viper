//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_postfx3d.c
// Purpose: PostFX3D — full-screen post-processing effects applied per-pixel
//   to the rendered framebuffer. Supports bloom (bright extract + blur +
//   composite), tone mapping (Reinhard / ACES filmic), FXAA, color grading,
//   and vignette.
//
// Key invariants:
//   - All effects operate on the software framebuffer (RGBA uint8 pixels).
//   - Bloom uses a half-resolution scratch buffer for performance.
//   - Effects chain in order: first added = first applied.
//   - Temporary buffers are allocated per-apply and freed after.
//
// Links: rt_postfx3d.h, plans/3d/18-post-processing.md
//
//===----------------------------------------------------------------------===//

#ifdef VIPER_ENABLE_GRAPHICS

#include "rt_postfx3d.h"
#include "rt_canvas3d.h"
#include "rt_canvas3d_internal.h"
#include "vgfx.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

extern void *rt_obj_new_i64(int64_t class_id, int64_t byte_size);
extern void rt_obj_set_finalizer(void *obj, void (*fn)(void *));
extern void rt_trap(const char *msg);

/*==========================================================================
 * Effect types
 *=========================================================================*/

typedef enum
{
    POSTFX_BLOOM = 0,
    POSTFX_TONEMAP,
    POSTFX_FXAA,
    POSTFX_COLOR_GRADE,
    POSTFX_VIGNETTE,
} postfx_type_t;

typedef struct
{
    postfx_type_t type;
    int8_t enabled;
    union
    {
        struct { float threshold; float intensity; int32_t blur_passes; } bloom;
        struct { int32_t mode; float exposure; } tonemap;
        struct { float edge_threshold; float min_threshold; } fxaa;
        struct { float brightness; float contrast; float saturation; } color_grade;
        struct { float radius; float softness; } vignette;
    } p;
} postfx_entry_t;

typedef struct
{
    void *vptr;
    postfx_entry_t effects[8];
    int32_t effect_count;
    int8_t enabled;
} rt_postfx3d;

/*==========================================================================
 * Helpers
 *=========================================================================*/

static float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static float luminance(float r, float g, float b)
{
    return 0.2126f * r + 0.7152f * g + 0.0722f * b;
}

/*==========================================================================
 * Effect implementations (per-pixel on float buffer)
 *=========================================================================*/

/// @brief Apply bloom: extract bright pixels, blur, composite.
static void apply_bloom(float *buf, int32_t w, int32_t h,
                         float threshold, float intensity, int32_t blur_passes)
{
    int32_t hw = w / 2, hh = h / 2;
    if (hw < 1 || hh < 1) return;

    /* Extract bright pixels to half-res buffer */
    float *bloom = (float *)calloc((size_t)hw * hh * 3, sizeof(float));
    if (!bloom) return;

    for (int32_t y = 0; y < hh; y++)
        for (int32_t x = 0; x < hw; x++)
        {
            int32_t sx = x * 2, sy = y * 2;
            int32_t si = (sy * w + sx) * 3;
            float r = buf[si], g = buf[si + 1], b = buf[si + 2];
            float lum = luminance(r, g, b);
            if (lum > threshold)
            {
                float scale = (lum - threshold) / (lum + 1e-6f);
                int32_t di = (y * hw + x) * 3;
                bloom[di] = r * scale;
                bloom[di + 1] = g * scale;
                bloom[di + 2] = b * scale;
            }
        }

    /* Separable Gaussian blur (simplified 5-tap kernel) */
    float *tmp = (float *)calloc((size_t)hw * hh * 3, sizeof(float));
    if (!tmp) { free(bloom); return; }

    static const float kernel[5] = {0.0625f, 0.25f, 0.375f, 0.25f, 0.0625f};

    for (int32_t pass = 0; pass < blur_passes; pass++)
    {
        /* Horizontal */
        for (int32_t y = 0; y < hh; y++)
            for (int32_t x = 0; x < hw; x++)
            {
                float r = 0, g = 0, b = 0;
                for (int k = -2; k <= 2; k++)
                {
                    int32_t sx = x + k;
                    if (sx < 0) sx = 0;
                    if (sx >= hw) sx = hw - 1;
                    int32_t si = (y * hw + sx) * 3;
                    float kw = kernel[k + 2];
                    r += bloom[si] * kw;
                    g += bloom[si + 1] * kw;
                    b += bloom[si + 2] * kw;
                }
                int32_t di = (y * hw + x) * 3;
                tmp[di] = r; tmp[di + 1] = g; tmp[di + 2] = b;
            }
        /* Vertical */
        for (int32_t y = 0; y < hh; y++)
            for (int32_t x = 0; x < hw; x++)
            {
                float r = 0, g = 0, b = 0;
                for (int k = -2; k <= 2; k++)
                {
                    int32_t sy = y + k;
                    if (sy < 0) sy = 0;
                    if (sy >= hh) sy = hh - 1;
                    int32_t si = (sy * hw + x) * 3;
                    float kw = kernel[k + 2];
                    r += tmp[si] * kw;
                    g += tmp[si + 1] * kw;
                    b += tmp[si + 2] * kw;
                }
                int32_t di = (y * hw + x) * 3;
                bloom[di] = r; bloom[di + 1] = g; bloom[di + 2] = b;
            }
    }
    free(tmp);

    /* Composite: add bloom back to scene (upsampled bilinear) */
    for (int32_t y = 0; y < h; y++)
        for (int32_t x = 0; x < w; x++)
        {
            int32_t bx = x / 2, by = y / 2;
            if (bx >= hw) bx = hw - 1;
            if (by >= hh) by = hh - 1;
            int32_t bi = (by * hw + bx) * 3;
            int32_t si = (y * w + x) * 3;
            buf[si] += bloom[bi] * intensity;
            buf[si + 1] += bloom[bi + 1] * intensity;
            buf[si + 2] += bloom[bi + 2] * intensity;
        }

    free(bloom);
}

/// @brief Apply tone mapping (Reinhard or ACES filmic).
static void apply_tonemap(float *buf, int32_t w, int32_t h, int32_t mode, float exposure)
{
    int32_t count = w * h;
    for (int32_t i = 0; i < count; i++)
    {
        float *p = &buf[i * 3];
        float r = p[0] * exposure, g = p[1] * exposure, b = p[2] * exposure;
        if (mode == 0)
        {
            /* Reinhard */
            r = r / (r + 1.0f);
            g = g / (g + 1.0f);
            b = b / (b + 1.0f);
        }
        else
        {
            /* ACES filmic approximation (Narkowicz) */
            float ar = r * (r * 2.51f + 0.03f);
            float br = r * (r * 2.43f + 0.59f) + 0.14f;
            r = clampf(ar / br, 0.0f, 1.0f);
            float ag = g * (g * 2.51f + 0.03f);
            float bg = g * (g * 2.43f + 0.59f) + 0.14f;
            g = clampf(ag / bg, 0.0f, 1.0f);
            float ab = b * (b * 2.51f + 0.03f);
            float bb = b * (b * 2.43f + 0.59f) + 0.14f;
            b = clampf(ab / bb, 0.0f, 1.0f);
        }
        /* Gamma correction */
        p[0] = powf(r, 1.0f / 2.2f);
        p[1] = powf(g, 1.0f / 2.2f);
        p[2] = powf(b, 1.0f / 2.2f);
    }
}

/// @brief Apply simplified FXAA (edge-aware 3x3 blur on high-contrast edges).
static void apply_fxaa(float *buf, int32_t w, int32_t h,
                        float edge_thresh, float min_thresh)
{
    float *out = (float *)malloc((size_t)w * h * 3 * sizeof(float));
    if (!out) return;
    memcpy(out, buf, (size_t)w * h * 3 * sizeof(float));

    for (int32_t y = 1; y < h - 1; y++)
        for (int32_t x = 1; x < w - 1; x++)
        {
            /* Sample 5 luminances */
            float lC = luminance(buf[(y * w + x) * 3], buf[(y * w + x) * 3 + 1], buf[(y * w + x) * 3 + 2]);
            float lN = luminance(buf[((y - 1) * w + x) * 3], buf[((y - 1) * w + x) * 3 + 1], buf[((y - 1) * w + x) * 3 + 2]);
            float lS = luminance(buf[((y + 1) * w + x) * 3], buf[((y + 1) * w + x) * 3 + 1], buf[((y + 1) * w + x) * 3 + 2]);
            float lE = luminance(buf[(y * w + x + 1) * 3], buf[(y * w + x + 1) * 3 + 1], buf[(y * w + x + 1) * 3 + 2]);
            float lW = luminance(buf[(y * w + x - 1) * 3], buf[(y * w + x - 1) * 3 + 1], buf[(y * w + x - 1) * 3 + 2]);

            float lmax = lC;
            if (lN > lmax) lmax = lN;
            if (lS > lmax) lmax = lS;
            if (lE > lmax) lmax = lE;
            if (lW > lmax) lmax = lW;
            float lmin = lC;
            if (lN < lmin) lmin = lN;
            if (lS < lmin) lmin = lS;
            if (lE < lmin) lmin = lE;
            if (lW < lmin) lmin = lW;

            float range = lmax - lmin;
            float thresh = lmax * edge_thresh;
            if (thresh < min_thresh) thresh = min_thresh;
            if (range < thresh) continue; /* not an edge */

            /* Simple 3x3 average for edge pixels */
            int32_t oi = (y * w + x) * 3;
            for (int c = 0; c < 3; c++)
            {
                float sum = 0;
                for (int dy = -1; dy <= 1; dy++)
                    for (int dx = -1; dx <= 1; dx++)
                        sum += buf[((y + dy) * w + (x + dx)) * 3 + c];
                out[oi + c] = sum / 9.0f;
            }
        }

    memcpy(buf, out, (size_t)w * h * 3 * sizeof(float));
    free(out);
}

/// @brief Apply color grading (brightness, contrast, saturation).
static void apply_color_grade(float *buf, int32_t w, int32_t h,
                                float brightness, float contrast, float saturation)
{
    int32_t count = w * h;
    for (int32_t i = 0; i < count; i++)
    {
        float *p = &buf[i * 3];
        /* Brightness + contrast */
        p[0] = (p[0] - 0.5f) * contrast + 0.5f + brightness;
        p[1] = (p[1] - 0.5f) * contrast + 0.5f + brightness;
        p[2] = (p[2] - 0.5f) * contrast + 0.5f + brightness;
        /* Saturation */
        float lum = luminance(p[0], p[1], p[2]);
        p[0] = lum + (p[0] - lum) * saturation;
        p[1] = lum + (p[1] - lum) * saturation;
        p[2] = lum + (p[2] - lum) * saturation;
        /* Clamp */
        p[0] = clampf(p[0], 0.0f, 1.0f);
        p[1] = clampf(p[1], 0.0f, 1.0f);
        p[2] = clampf(p[2], 0.0f, 1.0f);
    }
}

/// @brief Apply vignette (darken corners).
static void apply_vignette(float *buf, int32_t w, int32_t h,
                             float radius, float softness)
{
    float cx = (float)w * 0.5f, cy = (float)h * 0.5f;
    float maxdist = sqrtf(cx * cx + cy * cy);

    for (int32_t y = 0; y < h; y++)
        for (int32_t x = 0; x < w; x++)
        {
            float dx = ((float)x - cx) / maxdist;
            float dy = ((float)y - cy) / maxdist;
            float dist = sqrtf(dx * dx + dy * dy);
            float vig = 1.0f;
            if (dist > radius)
            {
                vig = 1.0f - clampf((dist - radius) / (softness + 1e-6f), 0.0f, 1.0f);
            }
            int32_t si = (y * w + x) * 3;
            buf[si] *= vig;
            buf[si + 1] *= vig;
            buf[si + 2] *= vig;
        }
}

/*==========================================================================
 * Apply entire effect chain to a framebuffer
 *=========================================================================*/

static void postfx_apply(rt_postfx3d *fx, uint8_t *pixels, int32_t w, int32_t h, int32_t stride)
{
    if (!fx || !fx->enabled || fx->effect_count == 0 || !pixels) return;

    /* Convert framebuffer to float RGB for processing */
    int32_t count = w * h;
    float *fbuf = (float *)malloc((size_t)count * 3 * sizeof(float));
    if (!fbuf) return;

    for (int32_t y = 0; y < h; y++)
        for (int32_t x = 0; x < w; x++)
        {
            const uint8_t *src = &pixels[y * stride + x * 4];
            int32_t di = (y * w + x) * 3;
            fbuf[di] = (float)src[0] / 255.0f;
            fbuf[di + 1] = (float)src[1] / 255.0f;
            fbuf[di + 2] = (float)src[2] / 255.0f;
        }

    /* Apply each enabled effect in chain order */
    for (int32_t i = 0; i < fx->effect_count; i++)
    {
        postfx_entry_t *e = &fx->effects[i];
        if (!e->enabled) continue;

        switch (e->type)
        {
        case POSTFX_BLOOM:
            apply_bloom(fbuf, w, h, e->p.bloom.threshold, e->p.bloom.intensity, e->p.bloom.blur_passes);
            break;
        case POSTFX_TONEMAP:
            apply_tonemap(fbuf, w, h, e->p.tonemap.mode, e->p.tonemap.exposure);
            break;
        case POSTFX_FXAA:
            apply_fxaa(fbuf, w, h, e->p.fxaa.edge_threshold, e->p.fxaa.min_threshold);
            break;
        case POSTFX_COLOR_GRADE:
            apply_color_grade(fbuf, w, h, e->p.color_grade.brightness,
                               e->p.color_grade.contrast, e->p.color_grade.saturation);
            break;
        case POSTFX_VIGNETTE:
            apply_vignette(fbuf, w, h, e->p.vignette.radius, e->p.vignette.softness);
            break;
        }
    }

    /* Write back to framebuffer */
    for (int32_t y = 0; y < h; y++)
        for (int32_t x = 0; x < w; x++)
        {
            uint8_t *dst = &pixels[y * stride + x * 4];
            int32_t si = (y * w + x) * 3;
            dst[0] = (uint8_t)(clampf(fbuf[si], 0.0f, 1.0f) * 255.0f);
            dst[1] = (uint8_t)(clampf(fbuf[si + 1], 0.0f, 1.0f) * 255.0f);
            dst[2] = (uint8_t)(clampf(fbuf[si + 2], 0.0f, 1.0f) * 255.0f);
            /* Preserve alpha */
        }

    free(fbuf);
}

/*==========================================================================
 * PostFX3D lifecycle + API
 *=========================================================================*/

static void rt_postfx3d_finalize(void *obj)
{
    (void)obj; /* no heap allocations in the struct itself */
}

void *rt_postfx3d_new(void)
{
    rt_postfx3d *fx = (rt_postfx3d *)rt_obj_new_i64(0, (int64_t)sizeof(rt_postfx3d));
    if (!fx) { rt_trap("PostFX3D.New: memory allocation failed"); return NULL; }
    fx->vptr = NULL;
    fx->effect_count = 0;
    fx->enabled = 1;
    memset(fx->effects, 0, sizeof(fx->effects));
    rt_obj_set_finalizer(fx, rt_postfx3d_finalize);
    return fx;
}

void rt_postfx3d_add_bloom(void *obj, double threshold, double intensity, int64_t blur_passes)
{
    if (!obj) return;
    rt_postfx3d *fx = (rt_postfx3d *)obj;
    if (fx->effect_count >= 8) return;
    postfx_entry_t *e = &fx->effects[fx->effect_count++];
    e->type = POSTFX_BLOOM;
    e->enabled = 1;
    e->p.bloom.threshold = (float)threshold;
    e->p.bloom.intensity = (float)intensity;
    e->p.bloom.blur_passes = (int32_t)blur_passes;
}

void rt_postfx3d_add_tonemap(void *obj, int64_t mode, double exposure)
{
    if (!obj) return;
    rt_postfx3d *fx = (rt_postfx3d *)obj;
    if (fx->effect_count >= 8) return;
    postfx_entry_t *e = &fx->effects[fx->effect_count++];
    e->type = POSTFX_TONEMAP;
    e->enabled = 1;
    e->p.tonemap.mode = (int32_t)mode;
    e->p.tonemap.exposure = (float)exposure;
}

void rt_postfx3d_add_fxaa(void *obj)
{
    if (!obj) return;
    rt_postfx3d *fx = (rt_postfx3d *)obj;
    if (fx->effect_count >= 8) return;
    postfx_entry_t *e = &fx->effects[fx->effect_count++];
    e->type = POSTFX_FXAA;
    e->enabled = 1;
    e->p.fxaa.edge_threshold = 0.166f;
    e->p.fxaa.min_threshold = 0.0833f;
}

void rt_postfx3d_add_color_grade(void *obj, double brightness, double contrast, double saturation)
{
    if (!obj) return;
    rt_postfx3d *fx = (rt_postfx3d *)obj;
    if (fx->effect_count >= 8) return;
    postfx_entry_t *e = &fx->effects[fx->effect_count++];
    e->type = POSTFX_COLOR_GRADE;
    e->enabled = 1;
    e->p.color_grade.brightness = (float)brightness;
    e->p.color_grade.contrast = (float)contrast;
    e->p.color_grade.saturation = (float)saturation;
}

void rt_postfx3d_add_vignette(void *obj, double radius, double softness)
{
    if (!obj) return;
    rt_postfx3d *fx = (rt_postfx3d *)obj;
    if (fx->effect_count >= 8) return;
    postfx_entry_t *e = &fx->effects[fx->effect_count++];
    e->type = POSTFX_VIGNETTE;
    e->enabled = 1;
    e->p.vignette.radius = (float)radius;
    e->p.vignette.softness = (float)softness;
}

void rt_postfx3d_set_enabled(void *obj, int8_t enabled)
{
    if (obj) ((rt_postfx3d *)obj)->enabled = enabled;
}

int8_t rt_postfx3d_get_enabled(void *obj)
{
    return obj ? ((rt_postfx3d *)obj)->enabled : 0;
}

void rt_postfx3d_clear(void *obj)
{
    if (!obj) return;
    rt_postfx3d *fx = (rt_postfx3d *)obj;
    fx->effect_count = 0;
    memset(fx->effects, 0, sizeof(fx->effects));
}

int64_t rt_postfx3d_get_effect_count(void *obj)
{
    return obj ? ((rt_postfx3d *)obj)->effect_count : 0;
}

/*==========================================================================
 * Canvas3D integration
 *=========================================================================*/

void rt_canvas3d_set_post_fx(void *canvas, void *postfx)
{
    if (!canvas) return;
    rt_canvas3d *c = (rt_canvas3d *)canvas;
    c->postfx = postfx;
}

/// @brief Called from rt_canvas3d_flip to apply post-processing effects.
void rt_postfx3d_apply_to_canvas(void *canvas)
{
    if (!canvas) return;
    rt_canvas3d *c = (rt_canvas3d *)canvas;
    rt_postfx3d *fx = (rt_postfx3d *)c->postfx;
    if (!fx || !fx->enabled || fx->effect_count == 0)
        return;
    if (!c->gfx_win) return;

    vgfx_framebuffer_t fb;
    if (!vgfx_get_framebuffer(c->gfx_win, &fb) || !fb.pixels)
        return;

    postfx_apply(fx, fb.pixels, fb.width, fb.height, fb.stride);
}

#endif /* VIPER_ENABLE_GRAPHICS */
