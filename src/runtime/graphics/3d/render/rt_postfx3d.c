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
//   - SSAO / DOF / Motion Blur require GPU scene buffers and trap on CPU path.
//   - Tonemap mode 0 is identity (passthrough); modes 1/2 are Reinhard/ACES.
//
// Ownership/Lifetime:
//   - PostFX3D is GC-managed; finalizer frees the heap effect array.
//   - Backend chain snapshots own their own effect-descriptor storage and
//     are reset / freed via vgfx3d_postfx_chain_reset / _free.
//
// Links: rt_postfx3d.h, plans/3d/18-post-processing.md
//
//===----------------------------------------------------------------------===//

#ifdef VIPER_ENABLE_GRAPHICS

#include "rt_postfx3d.h"
#include "rt_canvas3d.h"
#include "rt_canvas3d_internal.h"
#include "rt_graphics3d_ids.h"
#include "vgfx.h"
#include "vgfx3d_backend.h"
#include "vgfx3d_backend_utils.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

extern void *rt_obj_new_i64(int64_t class_id, int64_t byte_size);
extern void rt_obj_set_finalizer(void *obj, void (*fn)(void *));
extern void rt_obj_retain_maybe(void *obj);
extern int rt_obj_release_check0(void *obj);
extern void rt_obj_free(void *obj);
#include "rt_trap.h"

/*==========================================================================
 * Effect types
 *=========================================================================*/

typedef enum {
    POSTFX_BLOOM = 0,
    POSTFX_TONEMAP,
    POSTFX_FXAA,
    POSTFX_COLOR_GRADE,
    POSTFX_VIGNETTE,
    POSTFX_SSAO,
    POSTFX_DOF,
    POSTFX_MOTION_BLUR,
} postfx_type_t;

typedef struct {
    postfx_type_t type;
    int8_t enabled;

    union {
        struct {
            float threshold;
            float intensity;
            int32_t blur_passes;
        } bloom;

        struct {
            int32_t mode;
            float exposure;
        } tonemap;

        struct {
            float edge_threshold;
            float min_threshold;
        } fxaa;

        struct {
            float brightness;
            float contrast;
            float saturation;
        } color_grade;

        struct {
            float radius;
            float softness;
        } vignette;

        struct {
            float ao_radius;
            float ao_intensity;
            int32_t ao_samples;
        } ssao;

        struct {
            float focus_distance;
            float aperture;
            float max_blur;
        } dof;

        struct {
            float mb_intensity;
            int32_t mb_samples;
        } motion_blur;
    } p;
} postfx_entry_t;

typedef struct {
    void *vptr;
    postfx_entry_t *effects;
    int32_t effect_count;
    int32_t effect_capacity;
    int8_t enabled;
} rt_postfx3d;

/// @brief Validate @p obj as a PostFX3D handle and return its typed pointer (NULL on mismatch).
static rt_postfx3d *postfx3d_checked(void *obj) {
    return (rt_postfx3d *)rt_g3d_checked_or_null(obj, RT_G3D_POSTFX3D_CLASS_ID);
}

/*==========================================================================
 * Helpers
 *=========================================================================*/

/// @brief Clamp `v` into the closed interval `[lo, hi]`. Used throughout the effect
/// pipeline to keep intermediate float values inside the displayable range before
/// converting back to 8-bit pixels at the end.
static float clampf(float v, float lo, float hi) {
    if (!isfinite(v))
        return lo;
    return v < lo ? lo : (v > hi ? hi : v);
}

/// @brief Narrow a finite double to float, substituting `fallback` on NaN/inf.
static float sanitize_f32(double value, float fallback) {
    return isfinite(value) ? (float)value : fallback;
}

/// @brief `sanitize_f32` plus clamp-to-zero floor — for strengths/intensities that must be ≥ 0.
static float sanitize_nonnegative_f32(double value, float fallback) {
    float v = sanitize_f32(value, fallback);
    return v < 0.0f ? 0.0f : v;
}

/// @brief Clamp a 64-bit integer into a 32-bit range, truncating outside values.
/// @details Used where IL-side integer knobs (kernel radii, sample counts) need to match
///   backend-side `int32_t` slots without trapping on out-of-range input.
static int32_t clamp_i64_to_i32(int64_t value, int32_t lo, int32_t hi) {
    if (value < lo)
        return lo;
    if (value > hi)
        return hi;
    return (int32_t)value;
}

/// @brief Perceptual luminance of a linear sRGB colour using the Rec. 709 weights
/// (0.2126 R + 0.7152 G + 0.0722 B). Used by the bloom extract pass, the FXAA edge
/// detector, and the saturation term of `apply_color_grade`.
static float luminance(float r, float g, float b) {
    return 0.2126f * r + 0.7152f * g + 0.0722f * b;
}

/// @brief Reserve and zero one more `postfx_entry_t` slot in the chain, growing the heap
/// buffer when capacity is exceeded. Capacity doubles on each grow (starting at 8) so the
/// amortized cost of long chains is O(1). Returns NULL on realloc failure — the caller's
/// chain stays intact and the caller silently drops the add, matching the prior fixed-
/// size-array implementation's "silent drop past N" contract.
static postfx_entry_t *postfx_append_entry(rt_postfx3d *fx) {
    postfx_entry_t *effects;
    int32_t new_capacity;

    if (!fx)
        return NULL;
    if (fx->effect_count < fx->effect_capacity) {
        postfx_entry_t *entry = &fx->effects[fx->effect_count++];
        memset(entry, 0, sizeof(*entry));
        return entry;
    }

    new_capacity = fx->effect_capacity > 0 ? fx->effect_capacity * 2 : 8;
    if (new_capacity < fx->effect_count + 1)
        new_capacity = fx->effect_count + 1;
    effects =
        (postfx_entry_t *)realloc(fx->effects, (size_t)new_capacity * sizeof(postfx_entry_t));
    if (!effects)
        return NULL;
    memset(effects + fx->effect_capacity,
           0,
           (size_t)(new_capacity - fx->effect_capacity) * sizeof(postfx_entry_t));
    fx->effects = effects;
    fx->effect_capacity = new_capacity;
    effects = &fx->effects[fx->effect_count++];
    memset(effects, 0, sizeof(*effects));
    return effects;
}

/// @brief Ensure the backend-facing postfx chain buffer has room for at least `needed`
/// effect descriptors, doubling capacity each grow (starting at 8) and saturating at
/// `INT32_MAX` to avoid integer overflow in the size-in-bytes multiplication. Returns 1
/// on success, 0 on realloc failure. Freshly allocated entries are zeroed so unused
/// tail descriptors carry an `enabled = 0` marker by default.
static int vgfx3d_postfx_chain_reserve(vgfx3d_postfx_chain_t *chain, int32_t needed) {
    vgfx3d_postfx_effect_desc_t *effects;
    int32_t new_capacity;

    if (!chain)
        return 0;
    if (needed <= 0)
        return 1;
    if (chain->effect_capacity >= needed && chain->effects)
        return 1;

    new_capacity = chain->effect_capacity > 0 ? chain->effect_capacity : 8;
    while (new_capacity < needed) {
        if (new_capacity > INT32_MAX / 2) {
            new_capacity = needed;
            break;
        }
        new_capacity *= 2;
    }
    effects = (vgfx3d_postfx_effect_desc_t *)realloc(
        chain->effects, (size_t)new_capacity * sizeof(*effects));
    if (!effects)
        return 0;
    if (new_capacity > chain->effect_capacity) {
        memset(effects + chain->effect_capacity,
               0,
               (size_t)(new_capacity - chain->effect_capacity) * sizeof(*effects));
    }
    chain->effects = effects;
    chain->effect_capacity = new_capacity;
    return 1;
}

/// @brief Translate an internal `postfx_entry_t` into the backend-facing snapshot
/// descriptor consumed by GPU postfx passes. Returns 0 (skip this slot) for disabled
/// effects, NULL inputs, or unknown effect types so the backend never walks partial data.
/// The snapshot is a stable, flat struct so GPU shaders don't have to chase the internal
/// tagged union.
static int vgfx3d_postfx_fill_effect_snapshot(const postfx_entry_t *e,
                                              vgfx3d_postfx_effect_desc_t *out_effect) {
    vgfx3d_postfx_snapshot_t snapshot;

    if (!e || !out_effect || !e->enabled)
        return 0;

    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.enabled = 1;
    out_effect->type = (int32_t)e->type;

    switch (e->type) {
        case POSTFX_BLOOM:
            snapshot.bloom_enabled = 1;
            snapshot.bloom_threshold = e->p.bloom.threshold;
            snapshot.bloom_intensity = e->p.bloom.intensity;
            snapshot.bloom_passes = e->p.bloom.blur_passes;
            break;
        case POSTFX_TONEMAP:
            snapshot.tonemap_mode = (int8_t)e->p.tonemap.mode;
            snapshot.tonemap_exposure = e->p.tonemap.exposure;
            break;
        case POSTFX_FXAA:
            snapshot.fxaa_enabled = 1;
            break;
        case POSTFX_COLOR_GRADE:
            snapshot.color_grade_enabled = 1;
            snapshot.cg_brightness = e->p.color_grade.brightness;
            snapshot.cg_contrast = e->p.color_grade.contrast;
            snapshot.cg_saturation = e->p.color_grade.saturation;
            break;
        case POSTFX_VIGNETTE:
            snapshot.vignette_enabled = 1;
            snapshot.vignette_radius = e->p.vignette.radius;
            snapshot.vignette_softness = e->p.vignette.softness;
            break;
        case POSTFX_SSAO:
            snapshot.ssao_enabled = 1;
            snapshot.ssao_radius = e->p.ssao.ao_radius;
            snapshot.ssao_intensity = e->p.ssao.ao_intensity;
            snapshot.ssao_samples = e->p.ssao.ao_samples;
            break;
        case POSTFX_DOF:
            snapshot.dof_enabled = 1;
            snapshot.dof_focus_distance = e->p.dof.focus_distance;
            snapshot.dof_aperture = e->p.dof.aperture;
            snapshot.dof_max_blur = e->p.dof.max_blur;
            break;
        case POSTFX_MOTION_BLUR:
            snapshot.motion_blur_enabled = 1;
            snapshot.motion_blur_intensity = e->p.motion_blur.mb_intensity;
            snapshot.motion_blur_samples = e->p.motion_blur.mb_samples;
            break;
        default:
            return 0;
    }

    out_effect->snapshot = snapshot;
    return 1;
}

/// @brief Report whether the chain contains any effect that needs GPU-side scene
///        inputs beyond the final color buffer.
/// @details SSAO needs a depth buffer, DOF needs depth (and sometimes velocity), and
///          motion blur needs a velocity buffer — those inputs are expensive to
///          allocate and pipe through, so backends skip them when the chain doesn't
///          need them. Any other effect (bloom, tonemap, color grade, vignette)
///          composes on the color buffer alone and doesn't force the extra targets.
///          This gate is what the backend render-target allocator checks before
///          creating the depth/velocity attachments.
/// @return 1 if the chain requires auxiliary GPU scene buffers, 0 if color-only.
int vgfx3d_postfx_requires_gpu_scene_buffers(void *postfx) {
    rt_postfx3d *fx = postfx3d_checked(postfx);
    if (!fx || !fx->enabled || fx->effect_count <= 0)
        return 0;
    for (int32_t i = 0; i < fx->effect_count; i++) {
        const postfx_entry_t *e = &fx->effects[i];
        if (!e->enabled)
            continue;
        if (e->type == POSTFX_SSAO || e->type == POSTFX_DOF || e->type == POSTFX_MOTION_BLUR)
            return 1;
    }
    return 0;
}

/*==========================================================================
 * Effect implementations (per-pixel on float buffer)
 *=========================================================================*/

/// @brief Apply bloom — the classic bright-extract / separable-Gaussian-blur /
/// composite-back sequence. Downsamples to half-res for the extract + blur phase
/// (four-times fewer fragments on the hot loop) and upsamples with nearest-neighbour
/// during the composite. Uses a 5-tap symmetric Gaussian kernel (0.0625, 0.25, 0.375,
/// 0.25, 0.0625) in each axis, re-applied `blur_passes` times; more passes = wider halo.
/// Returns early on half-res degenerate dimensions or calloc failure.
static void apply_bloom(
    float *buf, int32_t w, int32_t h, float threshold, float intensity, int32_t blur_passes) {
    int32_t hw = w / 2, hh = h / 2;
    if (hw < 1 || hh < 1)
        return;

    /* Extract bright pixels to half-res buffer */
    float *bloom = (float *)calloc((size_t)hw * hh * 3, sizeof(float));
    if (!bloom)
        return;

    for (int32_t y = 0; y < hh; y++)
        for (int32_t x = 0; x < hw; x++) {
            int32_t sx = x * 2, sy = y * 2;
            int32_t si = (sy * w + sx) * 3;
            float r = buf[si], g = buf[si + 1], b = buf[si + 2];
            float lum = luminance(r, g, b);
            if (lum > threshold) {
                float scale = (lum - threshold) / (lum + 1e-6f);
                int32_t di = (y * hw + x) * 3;
                bloom[di] = r * scale;
                bloom[di + 1] = g * scale;
                bloom[di + 2] = b * scale;
            }
        }

    /* Separable Gaussian blur (simplified 5-tap kernel) */
    float *tmp = (float *)calloc((size_t)hw * hh * 3, sizeof(float));
    if (!tmp) {
        free(bloom);
        return;
    }

    static const float kernel[5] = {0.0625f, 0.25f, 0.375f, 0.25f, 0.0625f};

    for (int32_t pass = 0; pass < blur_passes; pass++) {
        /* Horizontal */
        for (int32_t y = 0; y < hh; y++)
            for (int32_t x = 0; x < hw; x++) {
                float r = 0, g = 0, b = 0;
                for (int k = -2; k <= 2; k++) {
                    int32_t sx = x + k;
                    if (sx < 0)
                        sx = 0;
                    if (sx >= hw)
                        sx = hw - 1;
                    int32_t si = (y * hw + sx) * 3;
                    float kw = kernel[k + 2];
                    r += bloom[si] * kw;
                    g += bloom[si + 1] * kw;
                    b += bloom[si + 2] * kw;
                }
                int32_t di = (y * hw + x) * 3;
                tmp[di] = r;
                tmp[di + 1] = g;
                tmp[di + 2] = b;
            }
        /* Vertical */
        for (int32_t y = 0; y < hh; y++)
            for (int32_t x = 0; x < hw; x++) {
                float r = 0, g = 0, b = 0;
                for (int k = -2; k <= 2; k++) {
                    int32_t sy = y + k;
                    if (sy < 0)
                        sy = 0;
                    if (sy >= hh)
                        sy = hh - 1;
                    int32_t si = (sy * hw + x) * 3;
                    float kw = kernel[k + 2];
                    r += tmp[si] * kw;
                    g += tmp[si + 1] * kw;
                    b += tmp[si + 2] * kw;
                }
                int32_t di = (y * hw + x) * 3;
                bloom[di] = r;
                bloom[di + 1] = g;
                bloom[di + 2] = b;
            }
    }
    free(tmp);

    /* Composite: add bloom back to scene (upsampled bilinear) */
    for (int32_t y = 0; y < h; y++)
        for (int32_t x = 0; x < w; x++) {
            int32_t bx = x / 2, by = y / 2;
            if (bx >= hw)
                bx = hw - 1;
            if (by >= hh)
                by = hh - 1;
            int32_t bi = (by * hw + bx) * 3;
            int32_t si = (y * w + x) * 3;
            buf[si] += bloom[bi] * intensity;
            buf[si + 1] += bloom[bi + 1] * intensity;
            buf[si + 2] += bloom[bi + 2] * intensity;
        }

    free(bloom);
}

/// @brief Apply HDR → LDR tone mapping in linear RGB. `mode = 1` selects simple
/// Reinhard (`c / (c + 1)`), `mode = 2` selects the Narkowicz ACES filmic approximation
/// (five-constant rational that matches the Academy curve). Any other mode no-ops so the
/// uninitialised case passes through cleanly. Multiplies by `exposure` before mapping so
/// scenes keyed for EV 0 stay in the mapper's responsive range. Finishes with a 2.2-gamma
/// correction so the 8-bit framebuffer gets perceptual output (the earlier float buffer
/// is linear).
static void apply_tonemap(float *buf, int32_t w, int32_t h, int32_t mode, float exposure) {
    if (mode != 1 && mode != 2)
        return;

    int32_t count = w * h;
    for (int32_t i = 0; i < count; i++) {
        float *p = &buf[i * 3];
        float r = p[0] * exposure, g = p[1] * exposure, b = p[2] * exposure;
        if (mode == 1) {
            /* Reinhard */
            r = r / (r + 1.0f);
            g = g / (g + 1.0f);
            b = b / (b + 1.0f);
        } else {
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

/// @brief Apply a simplified FXAA pass — sample each pixel's luminance plus its four
/// neighbours, compute the luminance range, and if it exceeds both `edge_threshold *
/// lmax` and `min_threshold` (avoids smoothing near-uniform regions), replace the
/// pixel with a 3×3 box average. Writes into a scratch buffer and memcpys back, so
/// the pass is order-independent within a single invocation. Cheaper than real FXAA
/// 3.11 but good enough for jagged-edge cleanup over the software pipeline's output.
static void apply_fxaa(float *buf, int32_t w, int32_t h, float edge_thresh, float min_thresh) {
    float *out = (float *)malloc((size_t)w * h * 3 * sizeof(float));
    if (!out)
        return;
    memcpy(out, buf, (size_t)w * h * 3 * sizeof(float));

    for (int32_t y = 1; y < h - 1; y++)
        for (int32_t x = 1; x < w - 1; x++) {
            /* Sample 5 luminances */
            float lC =
                luminance(buf[(y * w + x) * 3], buf[(y * w + x) * 3 + 1], buf[(y * w + x) * 3 + 2]);
            float lN = luminance(buf[((y - 1) * w + x) * 3],
                                 buf[((y - 1) * w + x) * 3 + 1],
                                 buf[((y - 1) * w + x) * 3 + 2]);
            float lS = luminance(buf[((y + 1) * w + x) * 3],
                                 buf[((y + 1) * w + x) * 3 + 1],
                                 buf[((y + 1) * w + x) * 3 + 2]);
            float lE = luminance(buf[(y * w + x + 1) * 3],
                                 buf[(y * w + x + 1) * 3 + 1],
                                 buf[(y * w + x + 1) * 3 + 2]);
            float lW = luminance(buf[(y * w + x - 1) * 3],
                                 buf[(y * w + x - 1) * 3 + 1],
                                 buf[(y * w + x - 1) * 3 + 2]);

            float lmax = lC;
            if (lN > lmax)
                lmax = lN;
            if (lS > lmax)
                lmax = lS;
            if (lE > lmax)
                lmax = lE;
            if (lW > lmax)
                lmax = lW;
            float lmin = lC;
            if (lN < lmin)
                lmin = lN;
            if (lS < lmin)
                lmin = lS;
            if (lE < lmin)
                lmin = lE;
            if (lW < lmin)
                lmin = lW;

            float range = lmax - lmin;
            float thresh = lmax * edge_thresh;
            if (thresh < min_thresh)
                thresh = min_thresh;
            if (range < thresh)
                continue; /* not an edge */

            /* Simple 3x3 average for edge pixels */
            int32_t oi = (y * w + x) * 3;
            for (int c = 0; c < 3; c++) {
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

/// @brief Apply colour grading in a single pass — contrast scales each channel around
/// mid-grey (0.5), brightness is a signed add, then saturation blends between grayscale
/// (the Rec. 709 luminance computed on the post-contrast values) and the full-chroma
/// result: `lum + (c - lum) * saturation`. All three terms are multiplicative around
/// 1.0 being neutral. Final clamp to `[0, 1]` prevents the 8-bit write back from
/// overflowing.
static void apply_color_grade(
    float *buf, int32_t w, int32_t h, float brightness, float contrast, float saturation) {
    int32_t count = w * h;
    for (int32_t i = 0; i < count; i++) {
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

/// @brief Apply a radial-falloff vignette. Normalises the pixel-to-centre distance by
/// the half-screen diagonal so the effect is resolution-independent, then multiplies
/// the colour by a `1 → 0` ramp that starts at normalized distance `radius` and reaches
/// full black at `radius + softness`. Pixels inside the radius are untouched. Alpha is
/// preserved — only the RGB channels are scaled.
static void apply_vignette(float *buf, int32_t w, int32_t h, float radius, float softness) {
    float cx = (float)w * 0.5f, cy = (float)h * 0.5f;
    float maxdist = sqrtf(cx * cx + cy * cy);

    for (int32_t y = 0; y < h; y++)
        for (int32_t x = 0; x < w; x++) {
            float dx = ((float)x - cx) / maxdist;
            float dy = ((float)y - cy) / maxdist;
            float dist = sqrtf(dx * dx + dy * dy);
            float vig = 1.0f;
            if (dist > radius) {
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

/// @brief Check whether the chain contains an active non-passthrough tonemap pass.
/// @details Tonemap mode 0 is the passthrough identity (no HDR→LDR remapping), so even
///          an enabled tonemap entry with mode=0 doesn't count. Used to gate whether
///          the float-buffer pipeline needs to run before the final 8-bit conversion:
///          bloom + tonemap + color-grade in HDR produces different output than the
///          same chain applied post-quantization.
static int postfx_chain_has_tonemap(const rt_postfx3d *fx) {
    if (!fx || !fx->enabled)
        return 0;
    for (int32_t i = 0; i < fx->effect_count; i++) {
        const postfx_entry_t *e = &fx->effects[i];
        if (e->enabled && e->type == POSTFX_TONEMAP && e->p.tonemap.mode != 0)
            return 1;
    }
    return 0;
}

/// @brief Run the HDR float-buffer stage of the postfx chain in authored order.
/// @details SSAO, DOF, and motion blur are no-ops here because they require GPU
///          scene inputs (depth, velocity) that the CPU path doesn't have — those
///          effects trap on CPU-path binding rather than silently dropping, so the
///          switch intentionally skips them. Bloom / tonemap / color-grade / vignette
///          all compose in linear float space before the final LDR conversion, which
///          is the whole point of running this before `postfx_apply` touches the
///          integer framebuffer.
static void postfx_apply_float_effects(rt_postfx3d *fx, float *fbuf, int32_t w, int32_t h) {
    if (!fx || !fx->enabled || fx->effect_count == 0 || !fbuf)
        return;

    for (int32_t i = 0; i < fx->effect_count; i++) {
        postfx_entry_t *e = &fx->effects[i];
        if (!e->enabled)
            continue;

        switch (e->type) {
            case POSTFX_BLOOM:
                apply_bloom(
                    fbuf, w, h, e->p.bloom.threshold, e->p.bloom.intensity, e->p.bloom.blur_passes);
                break;
            case POSTFX_TONEMAP:
                apply_tonemap(fbuf, w, h, e->p.tonemap.mode, e->p.tonemap.exposure);
                break;
            case POSTFX_FXAA:
                apply_fxaa(fbuf, w, h, e->p.fxaa.edge_threshold, e->p.fxaa.min_threshold);
                break;
            case POSTFX_COLOR_GRADE:
                apply_color_grade(fbuf,
                                  w,
                                  h,
                                  e->p.color_grade.brightness,
                                  e->p.color_grade.contrast,
                                  e->p.color_grade.saturation);
                break;
            case POSTFX_VIGNETTE:
                apply_vignette(fbuf, w, h, e->p.vignette.radius, e->p.vignette.softness);
                break;
            case POSTFX_SSAO:
            case POSTFX_DOF:
            case POSTFX_MOTION_BLUR:
                break;
        }
    }
}

/// @brief Run the CPU-supported effect chain over a framebuffer.
/// @details Converts RGBA8 pixels to a temporary planar-RGB float buffer, applies each
///   enabled CPU effect in insertion order, then writes RGB back with alpha preserved.
///   SSAO, DOF, and motion blur require GPU scene depth/motion buffers and are rejected
///   before this helper is called.
static void postfx_apply(rt_postfx3d *fx, uint8_t *pixels, int32_t w, int32_t h, int32_t stride) {
    if (!fx || !fx->enabled || fx->effect_count == 0 || !pixels)
        return;

    /* Convert framebuffer to float RGB for processing */
    int32_t count = w * h;
    float *fbuf = (float *)malloc((size_t)count * 3 * sizeof(float));
    if (!fbuf)
        return;

    for (int32_t y = 0; y < h; y++)
        for (int32_t x = 0; x < w; x++) {
            const uint8_t *src = &pixels[y * stride + x * 4];
            int32_t di = (y * w + x) * 3;
            fbuf[di] = (float)src[0] / 255.0f;
            fbuf[di + 1] = (float)src[1] / 255.0f;
            fbuf[di + 2] = (float)src[2] / 255.0f;
        }

    postfx_apply_float_effects(fx, fbuf, w, h);

    /* Write back to framebuffer */
    for (int32_t y = 0; y < h; y++)
        for (int32_t x = 0; x < w; x++) {
            uint8_t *dst = &pixels[y * stride + x * 4];
            int32_t si = (y * w + x) * 3;
            dst[0] = (uint8_t)(clampf(fbuf[si], 0.0f, 1.0f) * 255.0f);
            dst[1] = (uint8_t)(clampf(fbuf[si + 1], 0.0f, 1.0f) * 255.0f);
            dst[2] = (uint8_t)(clampf(fbuf[si + 2], 0.0f, 1.0f) * 255.0f);
            /* Preserve alpha */
        }

    free(fbuf);
}

/// @brief Apply post-processing effects to an HDR render target's floating-point color buffer.
/// @details Copies the HDR RGBA16F buffer into a temporary packed RGB float buffer, applies
///          the enabled post-fx chain (tone mapping, bloom, color grading, etc.), then
///          writes the result back into the 8-bit UNORM color buffer for display/readback.
static void postfx_apply_hdr_target(rt_postfx3d *fx, vgfx3d_rendertarget_t *target) {
    int32_t count;
    int tonemapped;
    float *fbuf;

    if (!fx || !target || !target->hdr_color_buf || !target->color_buf || target->width <= 0 ||
        target->height <= 0)
        return;
    count = target->width * target->height;
    fbuf = (float *)malloc((size_t)count * 3u * sizeof(float));
    if (!fbuf)
        return;
    for (int32_t i = 0; i < count; i++) {
        fbuf[(size_t)i * 3u + 0u] = target->hdr_color_buf[(size_t)i * 4u + 0u];
        fbuf[(size_t)i * 3u + 1u] = target->hdr_color_buf[(size_t)i * 4u + 1u];
        fbuf[(size_t)i * 3u + 2u] = target->hdr_color_buf[(size_t)i * 4u + 2u];
    }

    postfx_apply_float_effects(fx, fbuf, target->width, target->height);
    tonemapped = postfx_chain_has_tonemap(fx);
    for (int32_t y = 0; y < target->height; y++) {
        uint8_t *dst = target->color_buf + (size_t)y * (size_t)target->stride;
        for (int32_t x = 0; x < target->width; x++) {
            int32_t i = y * target->width + x;
            float r = fbuf[(size_t)i * 3u + 0u];
            float g = fbuf[(size_t)i * 3u + 1u];
            float b = fbuf[(size_t)i * 3u + 2u];
            target->hdr_color_buf[(size_t)i * 4u + 0u] = r;
            target->hdr_color_buf[(size_t)i * 4u + 1u] = g;
            target->hdr_color_buf[(size_t)i * 4u + 2u] = b;
            dst[(size_t)x * 4u + 0u] =
                tonemapped ? (uint8_t)(clampf(r, 0.0f, 1.0f) * 255.0f) : vgfx3d_hdr_to_unorm8(r);
            dst[(size_t)x * 4u + 1u] =
                tonemapped ? (uint8_t)(clampf(g, 0.0f, 1.0f) * 255.0f) : vgfx3d_hdr_to_unorm8(g);
            dst[(size_t)x * 4u + 2u] =
                tonemapped ? (uint8_t)(clampf(b, 0.0f, 1.0f) * 255.0f) : vgfx3d_hdr_to_unorm8(b);
        }
    }
    free(fbuf);
}

/*==========================================================================
 * PostFX3D lifecycle + API
 *=========================================================================*/

/// @brief GC finalizer for `PostFX3D`. Frees the heap-allocated effect array and zeroes
/// capacity/count so a dangling pointer re-use would fail cleanly. No other resources
/// are owned by the object — backend snapshots are rebuilt on each frame.
static void rt_postfx3d_finalize(void *obj) {
    rt_postfx3d *fx = (rt_postfx3d *)obj;
    if (!fx)
        return;
    free(fx->effects);
    fx->effects = NULL;
    fx->effect_capacity = 0;
    fx->effect_count = 0;
}

/// @brief Create a new post-FX chain. Starts empty (no effects) and with the master
/// enable flag on — a fresh chain is inert until the caller appends effects via
/// `_add_bloom` / `_add_tonemap` / etc. The internal `effects` array is lazily
/// allocated on the first append, so a never-used chain pays zero extra memory
/// beyond the wrapper struct.
void *rt_postfx3d_new(void) {
    rt_postfx3d *fx = (rt_postfx3d *)rt_obj_new_i64(RT_G3D_POSTFX3D_CLASS_ID, (int64_t)sizeof(rt_postfx3d));
    if (!fx) {
        rt_trap("PostFX3D.New: memory allocation failed");
        return NULL;
    }
    fx->vptr = NULL;
    fx->effects = NULL;
    fx->effect_count = 0;
    fx->effect_capacity = 0;
    fx->enabled = 1;
    rt_obj_set_finalizer(fx, rt_postfx3d_finalize);
    return fx;
}

/// @brief Append a Bloom effect: extracts pixels brighter than `threshold`, blurs `blur_passes`
/// times, then composites back at `intensity` strength. Common values: threshold 0.8–1.0,
/// intensity 0.3–1.5, passes 2–6.
void rt_postfx3d_add_bloom(void *obj, double threshold, double intensity, int64_t blur_passes) {
    postfx_entry_t *e;
    rt_postfx3d *fx = postfx3d_checked(obj);
    if (!fx)
        return;
    e = postfx_append_entry(fx);
    if (!e)
        return;
    e->type = POSTFX_BLOOM;
    e->enabled = 1;
    e->p.bloom.threshold = sanitize_nonnegative_f32(threshold, 0.8f);
    e->p.bloom.intensity = sanitize_nonnegative_f32(intensity, 1.0f);
    e->p.bloom.blur_passes = clamp_i64_to_i32(blur_passes, 0, 32);
}

/// @brief Append a tone-map (HDR → LDR compression). `mode`: 0 = off, 1 = Reinhard,
/// 2 = ACES filmic. `exposure` scales the input before mapping (typical 0.5–2.0).
void rt_postfx3d_add_tonemap(void *obj, int64_t mode, double exposure) {
    postfx_entry_t *e;
    rt_postfx3d *fx = postfx3d_checked(obj);
    if (!fx)
        return;
    e = postfx_append_entry(fx);
    if (!e)
        return;
    e->type = POSTFX_TONEMAP;
    e->enabled = 1;
    e->p.tonemap.mode = clamp_i64_to_i32(mode, 0, 2);
    e->p.tonemap.exposure = sanitize_nonnegative_f32(exposure, 1.0f);
}

/// @brief Append FXAA (Fast Approximate Anti-Aliasing). Smooths jagged edges by detecting
/// luminance discontinuities. Defaults to standard edge-threshold 0.166 / min-threshold 0.0833.
void rt_postfx3d_add_fxaa(void *obj) {
    postfx_entry_t *e;
    rt_postfx3d *fx = postfx3d_checked(obj);
    if (!fx)
        return;
    e = postfx_append_entry(fx);
    if (!e)
        return;
    e->type = POSTFX_FXAA;
    e->enabled = 1;
    e->p.fxaa.edge_threshold = 0.166f;
    e->p.fxaa.min_threshold = 0.0833f;
}

/// @brief Append a color-grading effect. All three params are multiplicative (1.0 = neutral):
/// `brightness` adds, `contrast` scales around 0.5, `saturation` interpolates from grayscale.
void rt_postfx3d_add_color_grade(void *obj, double brightness, double contrast, double saturation) {
    postfx_entry_t *e;
    rt_postfx3d *fx = postfx3d_checked(obj);
    if (!fx)
        return;
    e = postfx_append_entry(fx);
    if (!e)
        return;
    e->type = POSTFX_COLOR_GRADE;
    e->enabled = 1;
    e->p.color_grade.brightness = clampf(sanitize_f32(brightness, 0.0f), -1.0f, 1.0f);
    e->p.color_grade.contrast = clampf(sanitize_f32(contrast, 1.0f), 0.0f, 4.0f);
    e->p.color_grade.saturation = clampf(sanitize_f32(saturation, 1.0f), 0.0f, 4.0f);
}

/// @brief Append a vignette (radial darkening toward edges). `radius` is the bright-circle
/// fraction of half-screen-min-axis (typical 0.5–0.8); `softness` controls the falloff width.
void rt_postfx3d_add_vignette(void *obj, double radius, double softness) {
    postfx_entry_t *e;
    rt_postfx3d *fx = postfx3d_checked(obj);
    if (!fx)
        return;
    e = postfx_append_entry(fx);
    if (!e)
        return;
    e->type = POSTFX_VIGNETTE;
    e->enabled = 1;
    e->p.vignette.radius = clampf(sanitize_f32(radius, 0.7f), 0.0f, 1.0f);
    e->p.vignette.softness = clampf(sanitize_f32(softness, 0.3f), 0.001f, 1.0f);
}

/// @brief Master enable/disable for the entire effect chain. Disabled = framebuffer passes
/// through unchanged. Individual effects keep their own configuration.
void rt_postfx3d_set_enabled(void *obj, int8_t enabled) {
    rt_postfx3d *fx = postfx3d_checked(obj);
    if (fx)
        fx->enabled = enabled;
}

/// @brief Returns 1 if the post-FX chain is currently enabled.
int8_t rt_postfx3d_get_enabled(void *obj) {
    rt_postfx3d *fx = postfx3d_checked(obj);
    return fx ? fx->enabled : 0;
}

/// @brief Drop every effect in the chain (fresh state). Master enable flag preserved.
void rt_postfx3d_clear(void *obj) {
    rt_postfx3d *fx = postfx3d_checked(obj);
    if (!fx)
        return;
    fx->effect_count = 0;
    if (fx->effects && fx->effect_capacity > 0) {
        memset(fx->effects, 0, (size_t)fx->effect_capacity * sizeof(*fx->effects));
    }
}

/// @brief Number of effects currently in the chain.
int64_t rt_postfx3d_get_effect_count(void *obj) {
    rt_postfx3d *fx = postfx3d_checked(obj);
    return fx ? fx->effect_count : 0;
}

/*==========================================================================
 * Canvas3D integration
 *=========================================================================*/

/// @brief Attach a PostFX3D chain to a Canvas3D. Pass NULL to detach. The canvas retains a
/// reference; the previous chain is released. Apply runs automatically on `_flip`.
void rt_canvas3d_set_post_fx(void *canvas, void *postfx) {
    rt_canvas3d *c = rt_canvas3d_checked_or_stack(canvas);
    if (!c)
        return;
    if (postfx && !postfx3d_checked(postfx))
        return;
    if (c->postfx == postfx)
        return;
    if (postfx)
        rt_obj_retain_maybe(postfx);
    if (c->postfx && rt_obj_release_check0(c->postfx))
        rt_obj_free(c->postfx);
    c->postfx = postfx;
}

enum {
    POSTFX3D_QUALITY_FALLBACK_NONE = 0,
    POSTFX3D_QUALITY_FALLBACK_GPU_POSTFX_UNAVAILABLE = 1,
};

static int32_t postfx3d_quality_level(int64_t quality) {
    if (quality < RT_GRAPHICS3D_QUALITY_PERFORMANCE)
        return RT_GRAPHICS3D_QUALITY_PERFORMANCE;
    if (quality > RT_GRAPHICS3D_QUALITY_CINEMATIC)
        return RT_GRAPHICS3D_QUALITY_CINEMATIC;
    return (int32_t)quality;
}

static int postfx3d_canvas_supports_gpu_scene_effects(const rt_canvas3d *c) {
    return c && c->backend && c->backend->present_postfx && c->render_target == NULL;
}

static const char *postfx3d_quality_fallback_reason_text(int32_t reason) {
    switch (reason) {
        case POSTFX3D_QUALITY_FALLBACK_GPU_POSTFX_UNAVAILABLE:
            return "gpu-postfx unavailable; using CPU-safe cinematic postfx";
        default:
            return "";
    }
}

static void postfx3d_configure_quality_profile(rt_postfx3d *fx,
                                               rt_canvas3d *canvas,
                                               int32_t quality) {
    int gpu_scene_effects = postfx3d_canvas_supports_gpu_scene_effects(canvas);

    if (!fx)
        return;
    rt_postfx3d_clear(fx);

    if (canvas) {
        canvas->quality_requested = quality;
        canvas->quality_active = quality;
        canvas->quality_fallback = 0;
        canvas->quality_fallback_reason = POSTFX3D_QUALITY_FALLBACK_NONE;
    }

    switch (quality) {
        case RT_GRAPHICS3D_QUALITY_PERFORMANCE:
            rt_postfx3d_add_fxaa(fx);
            break;
        case RT_GRAPHICS3D_QUALITY_BALANCED:
            rt_postfx3d_add_bloom(fx, 0.88, 0.12, 1);
            rt_postfx3d_add_tonemap(fx, 1, 1.05);
            rt_postfx3d_add_fxaa(fx);
            rt_postfx3d_add_color_grade(fx, 0.0, 1.04, 1.03);
            break;
        case RT_GRAPHICS3D_QUALITY_CINEMATIC:
        default:
            rt_postfx3d_add_bloom(fx, 0.78, 0.22, 2);
            rt_postfx3d_add_tonemap(fx, 2, 1.10);
            rt_postfx3d_add_fxaa(fx);
            rt_postfx3d_add_color_grade(fx, 0.015, 1.08, 1.06);
            rt_postfx3d_add_vignette(fx, 0.72, 0.16);
            if (gpu_scene_effects) {
                rt_postfx3d_add_ssao(fx, 0.5, 0.65, 16);
                rt_postfx3d_add_dof(fx, 10.0, 0.08, 3.0);
                rt_postfx3d_add_motion_blur(fx, 0.12, 6);
            } else if (canvas) {
                canvas->quality_fallback = 1;
                canvas->quality_fallback_reason =
                    POSTFX3D_QUALITY_FALLBACK_GPU_POSTFX_UNAVAILABLE;
            }
            break;
    }
}

/// @brief Build a backend-safe PostFX chain for a canvas and requested quality level.
void *rt_postfx3d_new_quality(void *canvas, int64_t quality) {
    rt_canvas3d *c = rt_canvas3d_checked_or_stack(canvas);
    if (!c)
        return NULL;
    rt_postfx3d *fx = (rt_postfx3d *)rt_postfx3d_new();
    if (!fx)
        return NULL;
    postfx3d_configure_quality_profile(fx, c, postfx3d_quality_level(quality));
    return fx;
}

/// @brief Apply a backend-safe quality profile to a Canvas3D.
void rt_canvas3d_set_quality(void *canvas, int64_t quality) {
    void *fx = rt_postfx3d_new_quality(canvas, quality);
    if (!fx)
        return;
    rt_canvas3d_set_post_fx(canvas, fx);
    if (rt_obj_release_check0(fx))
        rt_obj_free(fx);
}

int64_t rt_canvas3d_get_quality_requested(void *canvas) {
    rt_canvas3d *c = rt_canvas3d_checked_or_stack(canvas);
    return c ? c->quality_requested : RT_GRAPHICS3D_QUALITY_PERFORMANCE;
}

int64_t rt_canvas3d_get_quality_active(void *canvas) {
    rt_canvas3d *c = rt_canvas3d_checked_or_stack(canvas);
    return c ? c->quality_active : RT_GRAPHICS3D_QUALITY_PERFORMANCE;
}

int8_t rt_canvas3d_get_quality_fallback(void *canvas) {
    rt_canvas3d *c = rt_canvas3d_checked_or_stack(canvas);
    return c && c->quality_fallback ? 1 : 0;
}

rt_string rt_canvas3d_get_quality_fallback_reason(void *canvas) {
    rt_canvas3d *c = rt_canvas3d_checked_or_stack(canvas);
    const char *reason =
        c ? postfx3d_quality_fallback_reason_text(c->quality_fallback_reason) : "";
    return rt_string_from_bytes(reason, strlen(reason));
}

/// @brief Apply the canvas's attached post-FX chain in place to its framebuffer pixels. Called
/// from `rt_canvas3d_flip` after rendering completes. No-op if no chain attached / disabled /
/// the framebuffer is unmapped (e.g., GPU-only window).
void rt_postfx3d_apply_to_canvas(void *canvas) {
    uint8_t *pixels = NULL;
    int32_t width = 0;
    int32_t height = 0;
    int32_t stride = 0;

    rt_canvas3d *c = rt_canvas3d_checked_or_stack(canvas);
    if (!c)
        return;
    rt_postfx3d *fx = postfx3d_checked(c->postfx);
    if (!fx || !fx->enabled || fx->effect_count == 0)
        return;
    if (vgfx3d_postfx_requires_gpu_scene_buffers(fx)) {
        rt_trap("PostFX3D: SSAO, DOF, and motion blur require GPU window postfx; "
                "RenderTarget3D and software CPU postfx support Bloom, Tonemap, FXAA, "
                "ColorGrade, and Vignette");
        return;
    }
    if (c->render_target) {
        if (!vgfx3d_rendertarget_ensure_color(c->render_target))
            return;
        if (!vgfx3d_rendertarget_sync_color_if_needed(c->render_target))
            return;
        if (vgfx3d_rendertarget_is_hdr(c->render_target) && c->render_target->hdr_color_valid &&
            c->render_target->hdr_color_buf) {
            postfx_apply_hdr_target(fx, c->render_target);
            return;
        }
        pixels = c->render_target->color_buf;
        width = c->render_target->width;
        height = c->render_target->height;
        stride = c->render_target->stride;
    } else {
        vgfx_framebuffer_t fb;
        if (!c->gfx_win)
            return;
        if (!vgfx_get_framebuffer(c->gfx_win, &fb) || !fb.pixels)
            return;
        pixels = fb.pixels;
        width = fb.width;
        height = fb.height;
        stride = fb.stride;
    }

    if (!pixels || width <= 0 || height <= 0 || stride <= 0)
        return;
    postfx_apply(fx, pixels, width, height, stride);
}

/// @brief Append SSAO (Screen-Space Ambient Occlusion). `radius` (world units) is the sample
/// neighborhood; `intensity` is the darkening multiplier; `samples` controls quality (8..64).
/// Higher `samples` = better quality but slower.
void rt_postfx3d_add_ssao(void *obj, double radius, double intensity, int64_t samples) {
    postfx_entry_t *e;
    rt_postfx3d *fx = postfx3d_checked(obj);
    if (!fx)
        return;
    e = postfx_append_entry(fx);
    if (!e)
        return;
    e->type = POSTFX_SSAO;
    e->enabled = 1;
    e->p.ssao.ao_radius = sanitize_nonnegative_f32(radius, 0.5f);
    e->p.ssao.ao_intensity = sanitize_nonnegative_f32(intensity, 1.0f);
    e->p.ssao.ao_samples = clamp_i64_to_i32(samples, 1, 128);
}

/// @brief Append depth-of-field. `focus_distance` (world units) is the sharply-focused depth;
/// `aperture` controls how quickly things outside that depth blur; `max_blur` caps the blur
/// kernel radius in pixels. Larger aperture = shallower DOF.
void rt_postfx3d_add_dof(void *obj, double focus_distance, double aperture, double max_blur) {
    postfx_entry_t *e;
    rt_postfx3d *fx = postfx3d_checked(obj);
    if (!fx)
        return;
    e = postfx_append_entry(fx);
    if (!e)
        return;
    e->type = POSTFX_DOF;
    e->enabled = 1;
    e->p.dof.focus_distance = sanitize_nonnegative_f32(focus_distance, 10.0f);
    e->p.dof.aperture = sanitize_nonnegative_f32(aperture, 0.0f);
    e->p.dof.max_blur = clampf(sanitize_f32(max_blur, 8.0f), 0.0f, 128.0f);
}

/// @brief Append per-pixel motion blur. `intensity` controls the blur length; `samples` is
/// the per-pixel sample count along the motion vector (more = smoother but slower).
void rt_postfx3d_add_motion_blur(void *obj, double intensity, int64_t samples) {
    postfx_entry_t *e;
    rt_postfx3d *fx = postfx3d_checked(obj);
    if (!fx)
        return;
    e = postfx_append_entry(fx);
    if (!e)
        return;
    e->type = POSTFX_MOTION_BLUR;
    e->enabled = 1;
    e->p.motion_blur.mb_intensity = clampf(sanitize_f32(intensity, 0.0f), 0.0f, 1.0f);
    e->p.motion_blur.mb_samples = clamp_i64_to_i32(samples, 1, 64);
}

/*==========================================================================
 * Backend-facing snapshot export (MTL-11)
 *=========================================================================*/

/// @brief Wipe a backend-facing postfx chain to "disabled / empty" state *without* freeing
/// its effect-descriptor storage. Used by the GPU frame loop to reuse a preallocated chain
/// buffer across frames — zeroing the descriptors keeps the allocation warm while discarding
/// last frame's contents. Pair with `_free` to release the buffer when the chain is retired.
void vgfx3d_postfx_chain_reset(vgfx3d_postfx_chain_t *chain) {
    if (!chain)
        return;
    chain->enabled = 0;
    chain->effect_count = 0;
    if (chain->effects && chain->effect_capacity > 0) {
        memset(chain->effects, 0, (size_t)chain->effect_capacity * sizeof(*chain->effects));
    }
}

/// @brief Release the effect-descriptor storage of a backend-facing chain and reset it to
/// a freshly-initialised state. Used at backend teardown / canvas destruction. NULL-safe.
void vgfx3d_postfx_chain_free(vgfx3d_postfx_chain_t *chain) {
    if (!chain)
        return;
    free(chain->effects);
    chain->effects = NULL;
    chain->effect_capacity = 0;
    chain->effect_count = 0;
    chain->enabled = 0;
}

/// @brief Copy the enabled effect descriptors from `src` into `dst`, growing `dst`'s
/// capacity if needed. Falls back to a reset (empty, disabled) on a disabled or empty
/// source, or on realloc failure — the caller can safely treat a zero return as "no
/// chain active" without inspecting `dst` further. Unused tail descriptors in `dst`
/// are zeroed so a stale shader that over-reads sees explicit disables instead of
/// garbage.
int vgfx3d_postfx_chain_copy(vgfx3d_postfx_chain_t *dst, const vgfx3d_postfx_chain_t *src) {
    if (!dst)
        return 0;
    if (!src || !src->enabled || src->effect_count <= 0 || !src->effects) {
        vgfx3d_postfx_chain_reset(dst);
        return 0;
    }
    if (!vgfx3d_postfx_chain_reserve(dst, src->effect_count)) {
        vgfx3d_postfx_chain_reset(dst);
        return 0;
    }
    memcpy(dst->effects, src->effects, (size_t)src->effect_count * sizeof(*src->effects));
    if (dst->effect_capacity > src->effect_count) {
        memset(dst->effects + src->effect_count,
               0,
               (size_t)(dst->effect_capacity - src->effect_count) * sizeof(*dst->effects));
    }
    dst->enabled = src->enabled ? 1 : 0;
    dst->effect_count = src->effect_count;
    return 1;
}

/// @brief Export a gameplay-facing `PostFX3D` as a backend-consumable chain of effect
/// descriptors. Walks the internal effect list and appends only the *enabled* entries
/// (translated via `vgfx3d_postfx_fill_effect_snapshot`) into `out->effects`. Returns 1
/// when at least one effect made it through, 0 when the chain is empty / disabled / all
/// entries disabled / the target buffer couldn't be grown. The per-frame backend reads
/// this into a local snapshot so later edits to the gameplay chain don't affect the
/// in-flight frame.
int vgfx3d_postfx_get_chain(void *postfx, vgfx3d_postfx_chain_t *out) {
    rt_postfx3d *fx;
    int32_t enabled_count = 0;

    if (!out)
        return 0;
    fx = postfx3d_checked(postfx);
    if (!fx) {
        vgfx3d_postfx_chain_reset(out);
        return 0;
    }
    if (!fx->enabled || fx->effect_count == 0) {
        vgfx3d_postfx_chain_reset(out);
        return 0;
    }

    for (int32_t i = 0; i < fx->effect_count; i++) {
        if (fx->effects[i].enabled)
            enabled_count++;
    }
    if (enabled_count == 0) {
        vgfx3d_postfx_chain_reset(out);
        return 0;
    }
    if (!vgfx3d_postfx_chain_reserve(out, enabled_count)) {
        vgfx3d_postfx_chain_reset(out);
        return 0;
    }

    out->enabled = 1;
    out->effect_count = 0;
    for (int32_t i = 0; i < fx->effect_count; i++) {
        if (!vgfx3d_postfx_fill_effect_snapshot(&fx->effects[i], &out->effects[out->effect_count]))
            continue;
        out->effect_count++;
    }
    if (out->effect_capacity > out->effect_count) {
        memset(out->effects + out->effect_count,
               0,
               (size_t)(out->effect_capacity - out->effect_count) * sizeof(*out->effects));
    }
    return out->effect_count > 0 ? 1 : 0;
}

/// @brief Flatten a gameplay-facing `PostFX3D` into a single struct whose fields carry
/// the parameters of the *last* occurrence of each effect type — the legacy (pre-chain)
/// API shape kept for backends that don't yet iterate the chain. When the same effect
/// type appears more than once, later entries stomp earlier ones. Returns 1 when the
/// chain is enabled and has ≥1 entry, 0 otherwise; `out` is always zeroed before use so
/// disabled fields read as zero instead of garbage.
int vgfx3d_postfx_get_snapshot(void *postfx, vgfx3d_postfx_snapshot_t *out) {
    rt_postfx3d *fx;

    if (!out)
        return 0;
    memset(out, 0, sizeof(*out));
    fx = postfx3d_checked(postfx);
    if (!fx)
        return 0;
    if (!fx->enabled || fx->effect_count == 0)
        return 0;

    out->enabled = 1;
    for (int32_t i = 0; i < fx->effect_count; i++) {
        vgfx3d_postfx_effect_desc_t effect;
        if (!vgfx3d_postfx_fill_effect_snapshot(&fx->effects[i], &effect))
            continue;
        switch ((postfx_type_t)effect.type) {
            case POSTFX_BLOOM:
                out->bloom_enabled = 1;
                out->bloom_threshold = effect.snapshot.bloom_threshold;
                out->bloom_intensity = effect.snapshot.bloom_intensity;
                out->bloom_passes = effect.snapshot.bloom_passes;
                break;
            case POSTFX_TONEMAP:
                out->tonemap_mode = effect.snapshot.tonemap_mode;
                out->tonemap_exposure = effect.snapshot.tonemap_exposure;
                break;
            case POSTFX_FXAA:
                out->fxaa_enabled = 1;
                break;
            case POSTFX_COLOR_GRADE:
                out->color_grade_enabled = 1;
                out->cg_brightness = effect.snapshot.cg_brightness;
                out->cg_contrast = effect.snapshot.cg_contrast;
                out->cg_saturation = effect.snapshot.cg_saturation;
                break;
            case POSTFX_VIGNETTE:
                out->vignette_enabled = 1;
                out->vignette_radius = effect.snapshot.vignette_radius;
                out->vignette_softness = effect.snapshot.vignette_softness;
                break;
            case POSTFX_SSAO:
                out->ssao_enabled = 1;
                out->ssao_radius = effect.snapshot.ssao_radius;
                out->ssao_intensity = effect.snapshot.ssao_intensity;
                out->ssao_samples = effect.snapshot.ssao_samples;
                break;
            case POSTFX_DOF:
                out->dof_enabled = 1;
                out->dof_focus_distance = effect.snapshot.dof_focus_distance;
                out->dof_aperture = effect.snapshot.dof_aperture;
                out->dof_max_blur = effect.snapshot.dof_max_blur;
                break;
            case POSTFX_MOTION_BLUR:
                out->motion_blur_enabled = 1;
                out->motion_blur_intensity = effect.snapshot.motion_blur_intensity;
                out->motion_blur_samples = effect.snapshot.motion_blur_samples;
                break;
            default:
                break;
        }
    }
    return 1;
}

#else
typedef int rt_graphics_disabled_tu_guard;
#endif /* VIPER_ENABLE_GRAPHICS */
