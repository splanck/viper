//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_postfx3d.h
// Purpose: PostFX3D — full-screen post-processing effect chain (bloom, tone
//   mapping, FXAA, color grading, vignette). Applied to the rendered
//   framebuffer after scene drawing completes.
//
// Key invariants:
//   - Effects are applied in chain order (first added = first applied).
//   - Max 8 effects per chain.
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

void *rt_postfx3d_new(void);
void rt_postfx3d_add_bloom(void *obj, double threshold, double intensity, int64_t blur_passes);
void rt_postfx3d_add_tonemap(void *obj, int64_t mode, double exposure);
void rt_postfx3d_add_fxaa(void *obj);
void rt_postfx3d_add_color_grade(void *obj, double brightness, double contrast, double saturation);
void rt_postfx3d_add_vignette(void *obj, double radius, double softness);
void rt_postfx3d_set_enabled(void *obj, int8_t enabled);
int8_t rt_postfx3d_get_enabled(void *obj);
void rt_postfx3d_clear(void *obj);
int64_t rt_postfx3d_get_effect_count(void *obj);
void rt_canvas3d_set_post_fx(void *canvas, void *postfx);

/* Phase F additions */
void rt_postfx3d_add_ssao(void *obj, double radius, double intensity, int64_t samples);
void rt_postfx3d_add_dof(void *obj, double focus_distance, double aperture, double max_blur);
void rt_postfx3d_add_motion_blur(void *obj, double intensity, int64_t samples);

/* Backend-facing PostFX snapshot (MTL-11): compact effect params for GPU backends.
 * Exported from rt_postfx3d.c — backends should NOT inspect the private rt_postfx3d struct. */
typedef struct {
    int8_t enabled;
    int8_t bloom_enabled;
    float bloom_threshold;
    float bloom_intensity;
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
} vgfx3d_postfx_snapshot_t;

/* Fill snapshot from a PostFX3D object. Returns 0 if postfx is NULL or disabled. */
int vgfx3d_postfx_get_snapshot(void *postfx, vgfx3d_postfx_snapshot_t *out);

#ifdef __cplusplus
}
#endif
