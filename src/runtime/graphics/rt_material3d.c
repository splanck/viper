//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_material3d.c
// Purpose: Viper.Graphics3D.Material3D — legacy + PBR surface appearance.
//
// Key invariants:
//   - Defaults: diffuse=(1,1,1,1), specular=(1,1,1), shininess=32,
//     alpha=1.0, emissive=(0,0,0), reflectivity=0.0, unlit=false.
//   - PBR defaults: metallic=0.0, roughness=0.5, ao=1.0,
//     emissive_intensity=1.0, normal_scale=1.0, alpha_mode=opaque.
//   - Texture/normal_map/specular_map/emissive_map/metallic_roughness_map/
//     ao_map/env_map are retained
//     references to GC-managed Pixels/CubeMap3D objects.
//   - Alpha [0.0=invisible, 1.0=opaque] controls transparency sorting
//     in Canvas3D.End() — opaque draws first, transparent back-to-front.
//   - env_map and reflectivity are forwarded through the backend draw command.
//     OpenGL consumes them for cubemap reflections; other backends may ignore
//     them until their reflection paths are implemented.
//
// Links: rt_canvas3d.h, rt_canvas3d_internal.h
//
//===----------------------------------------------------------------------===//

#ifdef VIPER_ENABLE_GRAPHICS

#include "rt_canvas3d.h"
#include "rt_canvas3d_internal.h"

#include <stdint.h>
#include <string.h>

extern void *rt_obj_new_i64(int64_t class_id, int64_t byte_size);
extern void rt_obj_set_finalizer(void *obj, void (*fn)(void *));
extern void rt_obj_retain_maybe(void *obj);
extern int rt_obj_release_check0(void *obj);
extern void rt_obj_free(void *obj);
#include "rt_trap.h"

static void material_release_ref(void **slot) {
    if (!slot || !*slot)
        return;
    if (rt_obj_release_check0(*slot))
        rt_obj_free(*slot);
    *slot = NULL;
}

static void rt_material3d_finalize(void *obj) {
    rt_material3d *mat = (rt_material3d *)obj;
    if (!mat)
        return;
    material_release_ref(&mat->texture);
    material_release_ref(&mat->normal_map);
    material_release_ref(&mat->specular_map);
    material_release_ref(&mat->emissive_map);
    material_release_ref(&mat->metallic_roughness_map);
    material_release_ref(&mat->ao_map);
    material_release_ref(&mat->env_map);
}

static void material_assign_ref(void **slot, void *value) {
    if (*slot == value)
        return;
    rt_obj_retain_maybe(value);
    material_release_ref(slot);
    *slot = value;
}

static double clamp01(double value) {
    if (value < 0.0)
        return 0.0;
    if (value > 1.0)
        return 1.0;
    return value;
}

static double clamp_min(double value, double min_value) {
    return value < min_value ? min_value : value;
}

static void material_init_defaults(rt_material3d *mat) {
    if (!mat)
        return;
    mat->vptr = NULL;
    mat->diffuse[0] = 1.0;
    mat->diffuse[1] = 1.0;
    mat->diffuse[2] = 1.0;
    mat->diffuse[3] = 1.0;
    mat->specular[0] = mat->specular[1] = mat->specular[2] = 1.0;
    mat->shininess = 32.0;
    mat->workflow = RT_MATERIAL3D_WORKFLOW_LEGACY;
    mat->texture = NULL;
    mat->normal_map = NULL;
    mat->specular_map = NULL;
    mat->emissive_map = NULL;
    mat->metallic_roughness_map = NULL;
    mat->ao_map = NULL;
    mat->emissive[0] = mat->emissive[1] = mat->emissive[2] = 0.0;
    mat->metallic = 0.0;
    mat->roughness = 0.5;
    mat->ao = 1.0;
    mat->emissive_intensity = 1.0;
    mat->normal_scale = 1.0;
    mat->alpha = 1.0;
    mat->alpha_cutoff = 0.5;
    mat->alpha_mode = RT_MATERIAL3D_ALPHA_MODE_OPAQUE;
    mat->env_map = NULL;
    mat->reflectivity = 0.0;
    mat->unlit = 0;
    mat->double_sided = 0;
    mat->shading_model = 0;
    memset(mat->custom_params, 0, sizeof(mat->custom_params));
}

static void material_promote_to_pbr(rt_material3d *mat) {
    if (!mat)
        return;
    mat->workflow = RT_MATERIAL3D_WORKFLOW_PBR;
}

static void *material_clone_like(void *obj) {
    rt_material3d *src = (rt_material3d *)obj;
    rt_material3d *dst;
    if (!src)
        return NULL;
    dst = (rt_material3d *)rt_material3d_new();
    if (!dst)
        return NULL;

    dst->diffuse[0] = src->diffuse[0];
    dst->diffuse[1] = src->diffuse[1];
    dst->diffuse[2] = src->diffuse[2];
    dst->diffuse[3] = src->diffuse[3];
    dst->specular[0] = src->specular[0];
    dst->specular[1] = src->specular[1];
    dst->specular[2] = src->specular[2];
    dst->shininess = src->shininess;
    dst->workflow = src->workflow;
    dst->emissive[0] = src->emissive[0];
    dst->emissive[1] = src->emissive[1];
    dst->emissive[2] = src->emissive[2];
    dst->metallic = src->metallic;
    dst->roughness = src->roughness;
    dst->ao = src->ao;
    dst->emissive_intensity = src->emissive_intensity;
    dst->normal_scale = src->normal_scale;
    dst->alpha = src->alpha;
    dst->alpha_cutoff = src->alpha_cutoff;
    dst->alpha_mode = src->alpha_mode;
    dst->reflectivity = src->reflectivity;
    dst->unlit = src->unlit;
    dst->double_sided = src->double_sided;
    dst->shading_model = src->shading_model;
    memcpy(dst->custom_params, src->custom_params, sizeof(dst->custom_params));

    material_assign_ref(&dst->texture, src->texture);
    material_assign_ref(&dst->normal_map, src->normal_map);
    material_assign_ref(&dst->specular_map, src->specular_map);
    material_assign_ref(&dst->emissive_map, src->emissive_map);
    material_assign_ref(&dst->metallic_roughness_map, src->metallic_roughness_map);
    material_assign_ref(&dst->ao_map, src->ao_map);
    material_assign_ref(&dst->env_map, src->env_map);
    return dst;
}

/// @brief Create a new material with default white diffuse, shininess 32, fully opaque.
/// @details Materials define how light interacts with a mesh surface. The default
///          state is a white, opaque, lit Phong material with no textures. Any
///          assigned texture pointers (diffuse, normal, specular, emissive, env)
///          are retained so they stay alive with the material.
/// @return Opaque material handle, or NULL on allocation failure.
void *rt_material3d_new(void) {
    rt_material3d *mat = (rt_material3d *)rt_obj_new_i64(0, (int64_t)sizeof(rt_material3d));
    if (!mat) {
        rt_trap("Material3D.New: memory allocation failed");
        return NULL;
    }
    material_init_defaults(mat);
    rt_obj_set_finalizer(mat, rt_material3d_finalize);
    return mat;
}

/// @brief Create a material with a solid diffuse color (no texture).
/// @param r Red diffuse component [0.0–1.0].
/// @param g Green diffuse component [0.0–1.0].
/// @param b Blue diffuse component [0.0–1.0].
/// @return Opaque material handle, or NULL on failure.
void *rt_material3d_new_color(double r, double g, double b) {
    rt_material3d *mat = (rt_material3d *)rt_material3d_new();
    if (!mat)
        return NULL;
    mat->diffuse[0] = r;
    mat->diffuse[1] = g;
    mat->diffuse[2] = b;
    mat->diffuse[3] = 1.0;
    return mat;
}

/// @brief Create a material with a diffuse texture map.
/// @details The texture (Pixels object) is retained by the material.
/// @param pixels Pixels handle for the diffuse texture.
/// @return Opaque material handle, or NULL on failure.
void *rt_material3d_new_textured(void *pixels) {
    rt_material3d *mat = (rt_material3d *)rt_material3d_new();
    if (!mat)
        return NULL;
    material_assign_ref(&mat->texture, pixels);
    return mat;
}

/// @brief Create a metallic-roughness PBR material with a solid base color.
void *rt_material3d_new_pbr(double r, double g, double b) {
    rt_material3d *mat = (rt_material3d *)rt_material3d_new_color(r, g, b);
    if (!mat)
        return NULL;
    material_promote_to_pbr(mat);
    mat->metallic = 0.0;
    mat->roughness = 0.5;
    mat->ao = 1.0;
    mat->emissive_intensity = 1.0;
    mat->normal_scale = 1.0;
    mat->alpha_mode = RT_MATERIAL3D_ALPHA_MODE_OPAQUE;
    return mat;
}

/// @brief Deep-copy a material, including all texture references (which are retained).
void *rt_material3d_clone(void *obj) {
    return material_clone_like(obj);
}

/// @brief Alias for `_clone` — produce an independent material instance for per-renderable
/// tweaks without affecting the source. Same texture-retention semantics.
void *rt_material3d_make_instance(void *obj) {
    return material_clone_like(obj);
}

/// @brief Set the diffuse color of a material (overrides existing color, keeps texture).
void rt_material3d_set_color(void *obj, double r, double g, double b) {
    if (!obj)
        return;
    rt_material3d *mat = (rt_material3d *)obj;
    mat->diffuse[0] = r;
    mat->diffuse[1] = g;
    mat->diffuse[2] = b;
}

/// @brief Set or replace the diffuse texture map.
void rt_material3d_set_texture(void *obj, void *pixels) {
    if (!obj)
        return;
    material_assign_ref(&((rt_material3d *)obj)->texture, pixels);
}

/// @brief Alias for `_set_texture` using PBR-style "albedo" terminology.
void rt_material3d_set_albedo_map(void *obj, void *pixels) {
    rt_material3d_set_texture(obj, pixels);
}

/// @brief Set the Phong specular shininess exponent (higher = tighter highlight).
void rt_material3d_set_shininess(void *obj, double s) {
    if (!obj)
        return;
    ((rt_material3d *)obj)->shininess = s;
}

/// @brief Enable or disable unlit mode (ignores scene lighting, renders flat color/texture).
void rt_material3d_set_unlit(void *obj, int8_t unlit) {
    if (!obj)
        return;
    ((rt_material3d *)obj)->unlit = unlit;
}

/// @brief Select the shading model (0=Phong, 1=Blinn-Phong, 2=flat, etc.).
/// @details Model values outside [0,5] are clamped to 0 (Phong).
void rt_material3d_set_shading_model(void *obj, int64_t model) {
    if (!obj)
        return;
    if (model < 0 || model > 5)
        model = 0;
    ((rt_material3d *)obj)->shading_model = (int32_t)model;
}

/// @brief Set a custom shader parameter by index (0–7) for advanced shading effects.
void rt_material3d_set_custom_param(void *obj, int64_t index, double value) {
    if (!obj || index < 0 || index >= 8)
        return;
    ((rt_material3d *)obj)->custom_params[index] = value;
}

/// @brief Set the transparency level (0.0 = invisible, 1.0 = fully opaque).
/// @details Materials with alpha < 1.0 are drawn in the transparency pass,
///          sorted back-to-front by distance from camera to prevent rendering artifacts.
void rt_material3d_set_alpha(void *obj, double alpha) {
    if (!obj)
        return;
    ((rt_material3d *)obj)->alpha = alpha;
}

/// @brief Get the current transparency level of the material.
double rt_material3d_get_alpha(void *obj) {
    if (!obj)
        return 1.0;
    return ((rt_material3d *)obj)->alpha;
}

/// @brief Set PBR metallic factor [0, 1]. 0 = dielectric (plastic, wood), 1 = pure metal. Auto-
/// promotes the material to PBR shading model on first use.
void rt_material3d_set_metallic(void *obj, double value) {
    rt_material3d *mat;
    if (!obj)
        return;
    mat = (rt_material3d *)obj;
    material_promote_to_pbr(mat);
    mat->metallic = clamp01(value);
}

/// @brief Read the metallic factor (default 0).
double rt_material3d_get_metallic(void *obj) {
    if (!obj)
        return 0.0;
    return ((rt_material3d *)obj)->metallic;
}

/// @brief Set PBR roughness [0, 1]. 0 = mirror smooth, 1 = fully diffuse. Auto-promotes to PBR.
void rt_material3d_set_roughness(void *obj, double value) {
    rt_material3d *mat;
    if (!obj)
        return;
    mat = (rt_material3d *)obj;
    material_promote_to_pbr(mat);
    mat->roughness = clamp01(value);
}

/// @brief Read the roughness factor (default 0.5).
double rt_material3d_get_roughness(void *obj) {
    if (!obj)
        return 0.5;
    return ((rt_material3d *)obj)->roughness;
}

/// @brief Set ambient-occlusion factor [0, 1]. 1 = no occlusion, 0 = fully shadowed in cavities.
/// Multiplied into the indirect lighting. Auto-promotes to PBR.
void rt_material3d_set_ao(void *obj, double value) {
    rt_material3d *mat;
    if (!obj)
        return;
    mat = (rt_material3d *)obj;
    material_promote_to_pbr(mat);
    mat->ao = clamp01(value);
}

/// @brief Read the AO factor (default 1.0 = no occlusion).
double rt_material3d_get_ao(void *obj) {
    if (!obj)
        return 1.0;
    return ((rt_material3d *)obj)->ao;
}

/// @brief Multiply the emissive output by `value` (≥ 0). Useful for HDR/bloom: values > 1 push
/// emissive surfaces past clamping range.
void rt_material3d_set_emissive_intensity(void *obj, double value) {
    if (!obj)
        return;
    ((rt_material3d *)obj)->emissive_intensity = clamp_min(value, 0.0);
}

/// @brief Read the emissive intensity multiplier (default 1.0).
double rt_material3d_get_emissive_intensity(void *obj) {
    if (!obj)
        return 1.0;
    return ((rt_material3d *)obj)->emissive_intensity;
}

/// @brief Assign a normal map texture for per-pixel bump/detail lighting.
void rt_material3d_set_normal_map(void *obj, void *pixels) {
    if (!obj)
        return;
    material_assign_ref(&((rt_material3d *)obj)->normal_map, pixels);
}

/// @brief Assign a glTF-style metallic-roughness texture (B = metallic, G = roughness, R/A
/// unused). Auto-promotes the material to PBR shading.
void rt_material3d_set_metallic_roughness_map(void *obj, void *pixels) {
    rt_material3d *mat;
    if (!obj)
        return;
    mat = (rt_material3d *)obj;
    material_promote_to_pbr(mat);
    material_assign_ref(&mat->metallic_roughness_map, pixels);
}

/// @brief Assign an ambient-occlusion texture (R channel). Multiplied into indirect lighting.
/// Auto-promotes the material to PBR shading.
void rt_material3d_set_ao_map(void *obj, void *pixels) {
    rt_material3d *mat;
    if (!obj)
        return;
    mat = (rt_material3d *)obj;
    material_promote_to_pbr(mat);
    material_assign_ref(&mat->ao_map, pixels);
}

/// @brief Assign a specular map texture to control per-pixel highlight intensity.
void rt_material3d_set_specular_map(void *obj, void *pixels) {
    if (!obj)
        return;
    material_assign_ref(&((rt_material3d *)obj)->specular_map, pixels);
}

/// @brief Assign an emissive map texture for self-illuminated surface regions.
void rt_material3d_set_emissive_map(void *obj, void *pixels) {
    if (!obj)
        return;
    material_assign_ref(&((rt_material3d *)obj)->emissive_map, pixels);
}

/// @brief Set the emissive (self-illumination) color, independent of scene lights.
void rt_material3d_set_emissive_color(void *obj, double r, double g, double b) {
    if (!obj)
        return;
    rt_material3d *m = (rt_material3d *)obj;
    m->emissive[0] = r;
    m->emissive[1] = g;
    m->emissive[2] = b;
}

/// @brief Set normal-map intensity (≥ 0). 1.0 = no scaling, > 1 amplifies bumps, < 1 flattens.
void rt_material3d_set_normal_scale(void *obj, double value) {
    if (!obj)
        return;
    ((rt_material3d *)obj)->normal_scale = clamp_min(value, 0.0);
}

/// @brief Read the normal-map scale factor (default 1.0).
double rt_material3d_get_normal_scale(void *obj) {
    if (!obj)
        return 1.0;
    return ((rt_material3d *)obj)->normal_scale;
}

/// @brief Set the alpha-handling mode (RT_MATERIAL3D_ALPHA_MODE_OPAQUE / MASK / BLEND).
/// OPAQUE: ignore alpha. MASK: discard fragments below cutoff. BLEND: depth-sorted transparency.
/// Out-of-range values are clamped to OPAQUE.
void rt_material3d_set_alpha_mode(void *obj, int64_t mode) {
    if (!obj)
        return;
    if (mode < RT_MATERIAL3D_ALPHA_MODE_OPAQUE || mode > RT_MATERIAL3D_ALPHA_MODE_BLEND)
        mode = RT_MATERIAL3D_ALPHA_MODE_OPAQUE;
    ((rt_material3d *)obj)->alpha_mode = (int32_t)mode;
}

/// @brief Read the alpha mode (default OPAQUE).
int64_t rt_material3d_get_alpha_mode(void *obj) {
    if (!obj)
        return RT_MATERIAL3D_ALPHA_MODE_OPAQUE;
    return ((rt_material3d *)obj)->alpha_mode;
}

/// @brief Toggle double-sided rendering. Disables backface culling for this material — useful
/// for foliage, banners, anything that should look correct from both sides. Increases fillrate.
void rt_material3d_set_double_sided(void *obj, int8_t enabled) {
    if (!obj)
        return;
    ((rt_material3d *)obj)->double_sided = enabled ? 1 : 0;
}

/// @brief Returns 1 if double-sided rendering is enabled, 0 otherwise.
int8_t rt_material3d_get_double_sided(void *obj) {
    if (!obj)
        return 0;
    return ((rt_material3d *)obj)->double_sided ? 1 : 0;
}

#else
typedef int rt_graphics_disabled_tu_guard;
#endif /* VIPER_ENABLE_GRAPHICS */
