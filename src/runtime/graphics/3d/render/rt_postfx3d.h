//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/render/rt_postfx3d.h
// Purpose: PostFX3D — full-screen post-processing effect chain (bloom, tone
//   mapping, FXAA, color grading, vignette). Applied to the rendered
//   framebuffer after scene drawing completes.
//
// Key invariants:
//   - Effects are applied in chain order (first added = first applied).
//   - Chain storage grows dynamically as effects are appended.
//   - Software path: per-pixel operations on the CPU framebuffer.
//   - SetPostFX on Canvas3D enables automatic application in Flip().
//
// Links: plans/3d/18-post-processing.md, rt_canvas3d.h
//
//===----------------------------------------------------------------------===//
#pragma once

#include "rt_string.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Create an empty PostFX chain.
void *rt_postfx3d_new(void);
/// @brief Append a bloom pass (threshold = brightness cutoff, intensity = mix amount, blur_passes =
/// quality).
void rt_postfx3d_add_bloom(void *obj, double threshold, double intensity, int64_t blur_passes);
/// @brief Append a tonemap pass (mode: 0=off, 1=Reinhard, 2=ACES) with exposure stops.
void rt_postfx3d_add_tonemap(void *obj, int64_t mode, double exposure);
/// @brief Append an FXAA antialiasing pass.
void rt_postfx3d_add_fxaa(void *obj);
/// @brief Append a color-grading pass.
/// @details `brightness` is a signed additive offset centered on 0.0. `contrast` and `saturation`
/// are multipliers centered on 1.0.
void rt_postfx3d_add_color_grade(void *obj, double brightness, double contrast, double saturation);
/// @brief Append a vignette pass (radius = circle of full brightness, softness = falloff width).
void rt_postfx3d_add_vignette(void *obj, double radius, double softness);
/// @brief Enable or disable the entire chain (without removing effects).
void rt_postfx3d_set_enabled(void *obj, int8_t enabled);
/// @brief True if the chain is enabled.
int8_t rt_postfx3d_get_enabled(void *obj);
/// @brief Remove all effects from the chain.
void rt_postfx3d_clear(void *obj);
/// @brief Number of effects currently in the chain.
int64_t rt_postfx3d_get_effect_count(void *obj);
/// @brief Bind a PostFX chain to a Canvas3D for automatic application during Flip to the active
/// output.
void rt_canvas3d_set_post_fx(void *canvas, void *postfx);

#define RT_GRAPHICS3D_QUALITY_PERFORMANCE 0
#define RT_GRAPHICS3D_QUALITY_BALANCED 1
#define RT_GRAPHICS3D_QUALITY_CINEMATIC 2

/// @brief Build a backend-safe PostFX profile for a canvas and quality level.
void *rt_postfx3d_new_quality(void *canvas, int64_t quality);
/// @brief Apply a backend-safe quality profile to a Canvas3D.
void rt_canvas3d_set_quality(void *canvas, int64_t quality);
/// @brief Last requested quality profile.
int64_t rt_canvas3d_get_quality_requested(void *canvas);
/// @brief Active quality profile after capability fallback.
int64_t rt_canvas3d_get_quality_active(void *canvas);
/// @brief True when the last quality application degraded for backend safety.
int8_t rt_canvas3d_get_quality_fallback(void *canvas);
/// @brief Human-readable reason for the last quality fallback, or empty string.
rt_string rt_canvas3d_get_quality_fallback_reason(void *canvas);

/* Phase F additions */
/// @brief Append an SSAO (screen-space ambient occlusion) pass with sample radius and intensity.
void rt_postfx3d_add_ssao(void *obj, double radius, double intensity, int64_t samples);
/// @brief Append a depth-of-field pass (focus distance in world units, aperture controls bokeh
/// size).
void rt_postfx3d_add_dof(void *obj, double focus_distance, double aperture, double max_blur);
/// @brief Append a motion-blur pass (intensity 0..1, sample count for blur quality).
void rt_postfx3d_add_motion_blur(void *obj, double intensity, int64_t samples);
/// @brief Append a TAA (temporal antialiasing) resolve pass. `blend` is the history weight
/// (0.5..0.98; higher = more temporal smoothing). GPU window backends only.
void rt_postfx3d_add_taa(void *obj, double blend);
/// @brief Append a screen-space reflections pass (Plan 10). `intensity` scales the
/// reflection contribution (0..1); `max_roughness` is the roughness cutoff above which
/// surfaces fall back to the environment map alone. GPU window backends only; applies to
/// materials flagged `SsrEnabled` (Water3D sets it automatically).
void rt_postfx3d_add_ssr(void *obj, double intensity, double max_roughness);
/// @brief Last recoverable PostFX configuration error ("" when none) — set when
/// `Canvas3D.SetPostFX` refuses a chain whose effects the canvas cannot run.
rt_string rt_postfx3d_get_last_error(void *obj);

/* Backend-facing PostFX snapshot (MTL-11): compact effect params for GPU backends.
 * Exported from rt_postfx3d.c — backends should NOT inspect the private rt_postfx3d struct. */
/// @brief Compact, backend-readable snapshot of a PostFX chain's enabled effects and
///   their parameters — GPU backends consume this instead of the private rt_postfx3d struct.
typedef struct {
    int8_t enabled;
    int8_t bloom_enabled;
    float bloom_threshold;
    float bloom_intensity;
    int32_t bloom_passes;
    int8_t tonemap_mode; /* 0=off, 1=reinhard, 2=aces */
    float tonemap_exposure;
    int8_t fxaa_enabled;
    int8_t color_grade_enabled;
    float cg_brightness, cg_contrast, cg_saturation;
    int8_t vignette_enabled;
    float vignette_radius, vignette_softness;
    int8_t ssao_enabled;
    float ssao_radius, ssao_intensity;
    int32_t ssao_samples;
    int8_t dof_enabled;
    float dof_focus_distance, dof_aperture, dof_max_blur;
    int8_t motion_blur_enabled;
    float motion_blur_intensity;
    int32_t motion_blur_samples;
    /* Appended fields (ABI-stable extension: existing backends memcpy the whole struct). */
    int8_t taa_enabled;
    float taa_blend;
    /* 1 when this snapshot came from an explicit tonemap chain entry (any mode). Lets
     * backends apply the mode-0 gamma-out transform on linear-HDR scene targets without
     * gamma-lifting passes whose snapshot merely defaults to tonemap_mode == 0. */
    int8_t tonemap_explicit;
    /* Plan 10: screen-space reflections (appended; ABI-stable extension). */
    int8_t ssr_enabled;
    float ssr_intensity;
    float ssr_max_roughness;
    int32_t ssr_steps;
} vgfx3d_postfx_snapshot_t;

/// @brief Discriminator identifying which post-processing effect a chain entry describes.
typedef enum {
    VGFX3D_POSTFX_EFFECT_BLOOM = 0,
    VGFX3D_POSTFX_EFFECT_TONEMAP,
    VGFX3D_POSTFX_EFFECT_FXAA,
    VGFX3D_POSTFX_EFFECT_COLOR_GRADE,
    VGFX3D_POSTFX_EFFECT_VIGNETTE,
    VGFX3D_POSTFX_EFFECT_SSAO,
    VGFX3D_POSTFX_EFFECT_DOF,
    VGFX3D_POSTFX_EFFECT_MOTION_BLUR,
    VGFX3D_POSTFX_EFFECT_TAA, /* appended: backend switches key on the raw type value */
    VGFX3D_POSTFX_EFFECT_SSR, /* Plan 10 */
} vgfx3d_postfx_effect_kind_t;

/// @brief One ordered chain entry: an effect kind plus its parameter snapshot.
typedef struct {
    int32_t type; /* vgfx3d_postfx_effect_kind_t */
    vgfx3d_postfx_snapshot_t snapshot;
} vgfx3d_postfx_effect_desc_t;

/// @brief Backend-facing ordered effect chain: an enabled flag and a growable array of
///   effect descriptors in application order.
typedef struct {
    int8_t enabled;
    int32_t effect_count;
    int32_t effect_capacity;
    vgfx3d_postfx_effect_desc_t *effects;
} vgfx3d_postfx_chain_t;

/// @brief Fill a flat snapshot from a PostFX3D object; returns 0 if it is NULL or disabled.
int vgfx3d_postfx_get_snapshot(void *postfx, vgfx3d_postfx_snapshot_t *out);
/// @brief Export the ordered PostFX chain for GPU backends, reusing @p out's storage when possible.
int vgfx3d_postfx_get_chain(void *postfx, vgfx3d_postfx_chain_t *out);
/// @brief Non-zero if the chain has effects (SSAO/DoF/motion blur) needing scene depth/motion
/// buffers.
int vgfx3d_postfx_requires_gpu_scene_buffers(void *postfx);
/// @brief Deep-copy one exported PostFX chain into another (grows @p dst as needed).
int vgfx3d_postfx_chain_copy(vgfx3d_postfx_chain_t *dst, const vgfx3d_postfx_chain_t *src);
/// @brief Reset an exported PostFX chain to empty while keeping its allocation for reuse.
void vgfx3d_postfx_chain_reset(vgfx3d_postfx_chain_t *chain);
/// @brief Release any storage owned by an exported PostFX chain.
void vgfx3d_postfx_chain_free(vgfx3d_postfx_chain_t *chain);

#ifdef __cplusplus
}
#endif
