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
//     GPU backends consume them for cubemap reflections; the software backend
//     may still ignore them.
//
// Links: rt_canvas3d.h, rt_canvas3d_internal.h
//
//===----------------------------------------------------------------------===//

#ifdef VIPER_ENABLE_GRAPHICS

#include "rt_canvas3d.h"
#include "rt_canvas3d_internal.h"
#include "rt_pixels.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

#define MATERIAL3D_UV_TRANSFORM_ABS_MAX 1000000.0
#define MATERIAL3D_TWO_PI 6.28318530717958647692

extern void *rt_obj_new_i64(int64_t class_id, int64_t byte_size);
extern void rt_obj_set_finalizer(void *obj, void (*fn)(void *));
extern void rt_obj_retain_maybe(void *obj);
extern int rt_obj_release_check0(void *obj);
extern void rt_obj_free(void *obj);
#include "rt_trap.h"

void rt_material3d_set_import_texture_slot(void *obj,
                                           int64_t slot,
                                           int64_t uv_set,
                                           double offset_u,
                                           double offset_v,
                                           double scale_u,
                                           double scale_v,
                                           double rotation,
                                           int64_t wrap_s,
                                           int64_t wrap_t,
                                           int64_t filter);

/// @brief Release the GC reference at `*slot` and NULL it. NULL-safe both ways (slot ==
/// NULL or *slot == NULL). Only frees the underlying object when the release drops its
/// retain count to zero — bystander references keep the texture alive.
static void material_release_ref(void **slot) {
    if (!slot || !*slot)
        return;
    if (rt_obj_release_check0(*slot))
        rt_obj_free(*slot);
    *slot = NULL;
}

/// @brief GC finalizer for Material3D. Walks all seven texture slots (diffuse, normal,
/// specular, emissive, metallic-roughness, AO, environment) and releases each, regardless
/// of which subset the material actually used. Unused slots are NULL and release is
/// NULL-safe, so legacy-Phong materials pay the same teardown cost as full PBR ones.
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

/// @brief Retain-then-release swap for a texture slot: if `value` differs from the
/// current occupant, retain the new one *first*, then release the old one. Retain-
/// before-release keeps the refcount above zero through the transition so re-assigning
/// a slot to its own contents (or to a shared texture whose only owner is this slot)
/// can't briefly drop the refcount and trigger a finalize.
static void material_assign_ref(void **slot, void *value) {
    if (*slot == value)
        return;
    rt_obj_retain_maybe(value);
    material_release_ref(slot);
    *slot = value;
}

static rt_material3d *material_checked(void *obj) {
    return (rt_material3d *)rt_g3d_checked_or_null(obj, RT_G3D_MATERIAL3D_CLASS_ID);
}

static int material_pixels_handle_valid(void *pixels) {
    return pixels && rt_obj_class_id(pixels) == RT_PIXELS_CLASS_ID;
}

static int material_cubemap_handle_valid(void *cubemap) {
    return cubemap && rt_g3d_has_class(cubemap, RT_G3D_CUBEMAP3D_CLASS_ID);
}

static void material_assign_pixels_ref(void **slot, void *pixels) {
    if (pixels && !material_pixels_handle_valid(pixels))
        return;
    material_assign_ref(slot, pixels);
}

void rt_material3d_assign_env_map_checked(void *obj, void *cubemap) {
    rt_material3d *mat = material_checked(obj);
    if (!mat)
        return;
    if (cubemap && !material_cubemap_handle_valid(cubemap))
        return;
    material_assign_ref(&mat->env_map, cubemap);
}

/// @brief Clamp into the closed `[0, 1]` range — the common normalized-parameter guard
/// used across PBR material setters (metallic, roughness, AO, alpha, reflectivity, etc.).
static double clamp01(double value) {
    if (!isfinite(value))
        return 0.0;
    if (value < 0.0)
        return 0.0;
    if (value > 1.0)
        return 1.0;
    return value;
}

/// @brief Clamp into the half-open range `[min_value, +inf)`. Used by setters whose
/// input only needs a floor (shininess, normal scale, emissive intensity, etc.) and
/// don't have a natural upper bound.
static double clamp_min(double value, double min_value) {
    if (!isfinite(value))
        return min_value;
    return value < min_value ? min_value : value;
}

static double material_clamp_uv_transform(double value, double fallback) {
    if (!isfinite(value))
        return fallback;
    if (value > MATERIAL3D_UV_TRANSFORM_ABS_MAX)
        return MATERIAL3D_UV_TRANSFORM_ABS_MAX;
    if (value < -MATERIAL3D_UV_TRANSFORM_ABS_MAX)
        return -MATERIAL3D_UV_TRANSFORM_ABS_MAX;
    return value;
}

/// @brief Clamp a color component to the canonical `[0, 1]` range.
/// @details Thin wrapper over `clamp01` whose separate name documents
///   intent at call sites — a reader scanning a setter sees "this channel
///   is a color component with normalized range" rather than a generic
///   01 clamp that happens to be applied to RGBA. Values above 1.0 (HDR
///   color authoring) are deliberately clamped down: the legacy material
///   model is SDR-only, and HDR authoring goes through the separate PBR
///   workflow.
static double sanitize_color(double value) {
    return clamp01(value);
}

/// @brief Initialize all six texture slots to neutral defaults so materials that
///        never set slot metadata render as they did before the per-slot machinery
///        was introduced.
/// @details Per slot: REPEAT wrap on both axes, LINEAR filter, UV set 0, and a 2×3
///          identity UV transform stored as six doubles in the layout
///          `[scale_u, shear_u, shear_v, scale_v, offset_u, offset_v]` — i.e. row-
///          major 2-row matrix with `(1,0,0,1,0,0)` = identity.
///
///          The legacy material-wide `texture_wrap_s` / `_t` / `_filter` scalars are
///          mirrored from slot 0 (base color) so older code reading those fields
///          directly sees a sensible default instead of an uninitialized value.
static void material_init_texture_slots(rt_material3d *mat) {
    if (!mat)
        return;
    for (int slot = 0; slot < RT_MATERIAL3D_TEXTURE_SLOT_COUNT; slot++) {
        mat->texture_slot_wrap_s[slot] = RT_MATERIAL3D_TEXTURE_WRAP_REPEAT;
        mat->texture_slot_wrap_t[slot] = RT_MATERIAL3D_TEXTURE_WRAP_REPEAT;
        mat->texture_slot_filter[slot] = RT_MATERIAL3D_TEXTURE_FILTER_LINEAR;
        mat->texture_slot_uv_set[slot] = 0;
        mat->texture_slot_uv_transform[slot][0] = 1.0;
        mat->texture_slot_uv_transform[slot][1] = 0.0;
        mat->texture_slot_uv_transform[slot][2] = 0.0;
        mat->texture_slot_uv_transform[slot][3] = 1.0;
        mat->texture_slot_uv_transform[slot][4] = 0.0;
        mat->texture_slot_uv_transform[slot][5] = 0.0;
    }
    mat->texture_wrap_s = mat->texture_slot_wrap_s[RT_MATERIAL3D_TEXTURE_SLOT_BASE_COLOR];
    mat->texture_wrap_t = mat->texture_slot_wrap_t[RT_MATERIAL3D_TEXTURE_SLOT_BASE_COLOR];
    mat->texture_filter = mat->texture_slot_filter[RT_MATERIAL3D_TEXTURE_SLOT_BASE_COLOR];
}

/// @brief Initialize a freshly allocated material to the legacy-Phong default — white
/// diffuse (RGBA 1,1,1,1), white specular, shininess 32, zero emissive, metallic 0 /
/// roughness 0.5 (safe PBR fallback even though workflow starts legacy), opaque alpha
/// mode, no textures bound. Zeroes the custom-param scratch buffer so backends see a
/// stable initial state.
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
    mat->alpha_mode_auto = 0;
    material_init_texture_slots(mat);
    mat->env_map = NULL;
    mat->reflectivity = 0.0;
    mat->unlit = 0;
    mat->double_sided = 0;
    mat->additive_blend = 0;
    mat->shading_model = 0;
    memset(mat->custom_params, 0, sizeof(mat->custom_params));
}

/// @brief Switch the material from legacy-Phong to PBR workflow. Called implicitly by
/// any PBR-specific setter (metallic / roughness / metallic-roughness-map / AO / etc.)
/// so a caller that touches those fields automatically opts into the PBR lighting path
/// without having to call a separate "SetPBR" entry. Legacy-only setters leave the
/// workflow alone so a pure-Phong material stays in that mode.
static void material_promote_to_pbr(rt_material3d *mat) {
    if (!mat)
        return;
    mat->workflow = RT_MATERIAL3D_WORKFLOW_PBR;
}

/// @brief Deep-copy the material shell, retaining (not cloning) the underlying texture
/// objects. Scalar / color / flag fields are memcpy-like copied; texture pointers go
/// through `material_assign_ref` so the clone holds its own retain count. Shared texture
/// data keeps GPU uploads cheap — callers who want independent textures clone them
/// separately. Returns NULL if `rt_material3d_new` fails to allocate.
static void *material_clone_like(void *obj) {
    rt_material3d *src = material_checked(obj);
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
    dst->alpha_mode_auto = src->alpha_mode_auto;
    dst->texture_wrap_s = src->texture_wrap_s;
    dst->texture_wrap_t = src->texture_wrap_t;
    dst->texture_filter = src->texture_filter;
    memcpy(dst->texture_slot_wrap_s, src->texture_slot_wrap_s, sizeof(dst->texture_slot_wrap_s));
    memcpy(dst->texture_slot_wrap_t, src->texture_slot_wrap_t, sizeof(dst->texture_slot_wrap_t));
    memcpy(dst->texture_slot_filter, src->texture_slot_filter, sizeof(dst->texture_slot_filter));
    memcpy(dst->texture_slot_uv_set, src->texture_slot_uv_set, sizeof(dst->texture_slot_uv_set));
    memcpy(dst->texture_slot_uv_transform,
           src->texture_slot_uv_transform,
           sizeof(dst->texture_slot_uv_transform));
    dst->reflectivity = src->reflectivity;
    dst->unlit = src->unlit;
    dst->double_sided = src->double_sided;
    dst->additive_blend = src->additive_blend;
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
    rt_material3d *mat = (rt_material3d *)rt_obj_new_i64(RT_G3D_MATERIAL3D_CLASS_ID, (int64_t)sizeof(rt_material3d));
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
    mat->diffuse[0] = sanitize_color(r);
    mat->diffuse[1] = sanitize_color(g);
    mat->diffuse[2] = sanitize_color(b);
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
    material_assign_pixels_ref(&mat->texture, pixels);
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
    rt_material3d *mat = material_checked(obj);
    if (!mat)
        return;
    mat->diffuse[0] = sanitize_color(r);
    mat->diffuse[1] = sanitize_color(g);
    mat->diffuse[2] = sanitize_color(b);
}

/// @brief Set or replace the diffuse texture map.
void rt_material3d_set_texture(void *obj, void *pixels) {
    rt_material3d *mat = material_checked(obj);
    if (!mat)
        return;
    material_assign_pixels_ref(&mat->texture, pixels);
}

/// @brief Coerce an incoming texture-wrap mode to a known-valid enum value.
/// @details Accepts CLAMP_TO_EDGE and MIRRORED_REPEAT; everything else (including
///   unknown future modes or out-of-range values from importers) falls back to
///   REPEAT, which is the safe default and matches the texture-slot init in
///   material_init_texture_slots.
static int32_t material_sanitize_wrap(int64_t value) {
    if (value == RT_MATERIAL3D_TEXTURE_WRAP_CLAMP_TO_EDGE ||
        value == RT_MATERIAL3D_TEXTURE_WRAP_MIRRORED_REPEAT)
        return (int32_t)value;
    return RT_MATERIAL3D_TEXTURE_WRAP_REPEAT;
}

/// @brief Coerce an incoming texture-filter mode to LINEAR or NEAREST.
/// @details Only NEAREST is accepted as an explicit override; everything else
///   maps to LINEAR so unknown future filter modes degrade gracefully rather
///   than producing undefined backend behaviour.
static int32_t material_sanitize_filter(int64_t value) {
    return value == RT_MATERIAL3D_TEXTURE_FILTER_NEAREST ? RT_MATERIAL3D_TEXTURE_FILTER_NEAREST
                                                         : RT_MATERIAL3D_TEXTURE_FILTER_LINEAR;
}

/// @brief Validate a texture-slot index and narrow it to int32_t.
/// @details Returns -1 for any out-of-range value so that callers can perform a
///   single negative-check before indexing the per-slot arrays, keeping bounds
///   enforcement in one place rather than scattered across every importer hook.
static int32_t material_sanitize_texture_slot(int64_t slot) {
    if (slot < 0 || slot >= RT_MATERIAL3D_TEXTURE_SLOT_COUNT)
        return -1;
    return (int32_t)slot;
}

/// @brief Internal importer hook: store glTF-style sampler state on the material.
void rt_material3d_set_import_sampler(void *obj,
                                      int64_t wrap_s,
                                      int64_t wrap_t,
                                      int64_t filter) {
    rt_material3d *mat = material_checked(obj);
    if (!mat)
        return;
    rt_material3d_set_import_texture_slot(obj,
                                          RT_MATERIAL3D_TEXTURE_SLOT_BASE_COLOR,
                                          0,
                                          0.0,
                                          0.0,
                                          1.0,
                                          1.0,
                                          0.0,
                                          wrap_s,
                                          wrap_t,
                                          filter);
}

/// @brief Internal importer hook: store sampler, UV set, and KHR_texture_transform
/// metadata for one material texture slot.
void rt_material3d_set_import_texture_slot(void *obj,
                                           int64_t slot,
                                           int64_t uv_set,
                                           double offset_u,
                                           double offset_v,
                                           double scale_u,
                                           double scale_v,
                                           double rotation,
                                           int64_t wrap_s,
                                           int64_t wrap_t,
                                           int64_t filter) {
    rt_material3d *mat = material_checked(obj);
    int32_t slot_index = material_sanitize_texture_slot(slot);
    double c;
    double s;
    if (!mat || slot_index < 0)
        return;
    offset_u = material_clamp_uv_transform(offset_u, 0.0);
    offset_v = material_clamp_uv_transform(offset_v, 0.0);
    scale_u = material_clamp_uv_transform(scale_u, 1.0);
    scale_v = material_clamp_uv_transform(scale_v, 1.0);
    if (!isfinite(rotation))
        rotation = 0.0;
    else
        rotation = fmod(rotation, MATERIAL3D_TWO_PI);
    c = cos(rotation);
    s = sin(rotation);
    mat->texture_slot_wrap_s[slot_index] = material_sanitize_wrap(wrap_s);
    mat->texture_slot_wrap_t[slot_index] = material_sanitize_wrap(wrap_t);
    mat->texture_slot_filter[slot_index] = material_sanitize_filter(filter);
    mat->texture_slot_uv_set[slot_index] = uv_set > 0 ? 1 : 0;
    mat->texture_slot_uv_transform[slot_index][0] =
        material_clamp_uv_transform(scale_u * c, 1.0);
    mat->texture_slot_uv_transform[slot_index][1] =
        material_clamp_uv_transform(-scale_v * s, 0.0);
    mat->texture_slot_uv_transform[slot_index][2] =
        material_clamp_uv_transform(scale_u * s, 0.0);
    mat->texture_slot_uv_transform[slot_index][3] =
        material_clamp_uv_transform(scale_v * c, 1.0);
    mat->texture_slot_uv_transform[slot_index][4] = offset_u;
    mat->texture_slot_uv_transform[slot_index][5] = offset_v;
    if (slot_index == RT_MATERIAL3D_TEXTURE_SLOT_BASE_COLOR) {
        mat->texture_wrap_s = mat->texture_slot_wrap_s[slot_index];
        mat->texture_wrap_t = mat->texture_slot_wrap_t[slot_index];
        mat->texture_filter = mat->texture_slot_filter[slot_index];
    }
}

/// @brief Alias for `_set_texture` using PBR-style "albedo" terminology.
void rt_material3d_set_albedo_map(void *obj, void *pixels) {
    rt_material3d_set_texture(obj, pixels);
}

/// @brief Set the Phong specular shininess exponent (higher = tighter highlight).
void rt_material3d_set_shininess(void *obj, double s) {
    rt_material3d *mat = material_checked(obj);
    if (!mat)
        return;
    mat->shininess = clamp_min(s, 0.0);
}

/// @brief Enable or disable unlit mode (ignores scene lighting, renders flat color/texture).
void rt_material3d_set_unlit(void *obj, int8_t unlit) {
    rt_material3d *mat = material_checked(obj);
    if (!mat)
        return;
    mat->unlit = unlit;
}

/// @brief Select the shading model (0=Phong, 1=Blinn-Phong, 2=flat, etc.).
/// @details Model values outside [0,5] are clamped to 0 (Phong).
void rt_material3d_set_shading_model(void *obj, int64_t model) {
    rt_material3d *mat = material_checked(obj);
    if (!mat)
        return;
    if (model < 0 || model > 5)
        model = 0;
    mat->shading_model = (int32_t)model;
}

/// @brief Set a custom shader parameter by index (0–7) for advanced shading effects.
void rt_material3d_set_custom_param(void *obj, int64_t index, double value) {
    rt_material3d *mat = material_checked(obj);
    if (!mat || index < 0 || index >= 8)
        return;
    mat->custom_params[index] = isfinite(value) ? value : 0.0;
}

/// @brief Set the transparency level (0.0 = invisible, 1.0 = fully opaque).
/// @details Values are clamped to [0,1]. Setting alpha below 1.0 promotes an
///          opaque material to BLEND so the visible result matches the scalar.
///          Call SetAlphaMode afterwards when explicit MASK/OPAQUE behavior is needed.
void rt_material3d_set_alpha(void *obj, double alpha) {
    rt_material3d *mat = material_checked(obj);
    if (!mat)
        return;
    mat->alpha = clamp01(alpha);
    if (mat->alpha < 0.999 && mat->alpha_mode == RT_MATERIAL3D_ALPHA_MODE_OPAQUE) {
        mat->alpha_mode = RT_MATERIAL3D_ALPHA_MODE_BLEND;
        mat->alpha_mode_auto = 1;
    } else if (mat->alpha >= 0.999 && mat->alpha_mode == RT_MATERIAL3D_ALPHA_MODE_BLEND &&
               mat->alpha_mode_auto) {
        mat->alpha_mode = RT_MATERIAL3D_ALPHA_MODE_OPAQUE;
        mat->alpha_mode_auto = 0;
    }
}

/// @brief Get the current transparency level of the material.
double rt_material3d_get_alpha(void *obj) {
    rt_material3d *mat = material_checked(obj);
    if (!mat)
        return 1.0;
    return mat->alpha;
}

/// @brief Set PBR metallic factor [0, 1]. 0 = dielectric (plastic, wood), 1 = pure metal. Auto-
/// promotes the material to PBR shading model on first use.
void rt_material3d_set_metallic(void *obj, double value) {
    rt_material3d *mat = material_checked(obj);
    if (!mat)
        return;
    material_promote_to_pbr(mat);
    mat->metallic = clamp01(value);
}

/// @brief Read the metallic factor (default 0).
double rt_material3d_get_metallic(void *obj) {
    rt_material3d *mat = material_checked(obj);
    if (!mat)
        return 0.0;
    return mat->metallic;
}

/// @brief Set PBR roughness [0, 1]. 0 = mirror smooth, 1 = fully diffuse. Auto-promotes to PBR.
void rt_material3d_set_roughness(void *obj, double value) {
    rt_material3d *mat = material_checked(obj);
    if (!mat)
        return;
    material_promote_to_pbr(mat);
    mat->roughness = clamp01(value);
}

/// @brief Read the roughness factor (default 0.5).
double rt_material3d_get_roughness(void *obj) {
    rt_material3d *mat = material_checked(obj);
    if (!mat)
        return 0.5;
    return mat->roughness;
}

/// @brief Set ambient-occlusion factor [0, 1]. 1 = no occlusion, 0 = fully shadowed in cavities.
/// Multiplied into the indirect lighting. Auto-promotes to PBR.
void rt_material3d_set_ao(void *obj, double value) {
    rt_material3d *mat = material_checked(obj);
    if (!mat)
        return;
    material_promote_to_pbr(mat);
    mat->ao = clamp01(value);
}

/// @brief Read the AO factor (default 1.0 = no occlusion).
double rt_material3d_get_ao(void *obj) {
    rt_material3d *mat = material_checked(obj);
    if (!mat)
        return 1.0;
    return mat->ao;
}

/// @brief Multiply the emissive output by `value` (≥ 0). Useful for HDR/bloom: values > 1 push
/// emissive surfaces past clamping range.
void rt_material3d_set_emissive_intensity(void *obj, double value) {
    rt_material3d *mat = material_checked(obj);
    if (!mat)
        return;
    mat->emissive_intensity = clamp_min(value, 0.0);
}

/// @brief Read the emissive intensity multiplier (default 1.0).
double rt_material3d_get_emissive_intensity(void *obj) {
    rt_material3d *mat = material_checked(obj);
    if (!mat)
        return 1.0;
    return mat->emissive_intensity;
}

/// @brief Assign a normal map texture for per-pixel bump/detail lighting.
void rt_material3d_set_normal_map(void *obj, void *pixels) {
    rt_material3d *mat = material_checked(obj);
    if (!mat)
        return;
    material_assign_pixels_ref(&mat->normal_map, pixels);
}

/// @brief Assign a glTF-style metallic-roughness texture (B = metallic, G = roughness, R/A
/// unused). Auto-promotes the material to PBR shading.
void rt_material3d_set_metallic_roughness_map(void *obj, void *pixels) {
    rt_material3d *mat = material_checked(obj);
    if (!mat)
        return;
    material_promote_to_pbr(mat);
    material_assign_pixels_ref(&mat->metallic_roughness_map, pixels);
}

/// @brief Assign an ambient-occlusion texture (R channel). Multiplied into indirect lighting.
/// Auto-promotes the material to PBR shading.
void rt_material3d_set_ao_map(void *obj, void *pixels) {
    rt_material3d *mat = material_checked(obj);
    if (!mat)
        return;
    material_promote_to_pbr(mat);
    material_assign_pixels_ref(&mat->ao_map, pixels);
}

/// @brief Assign a specular map texture to control per-pixel highlight intensity.
void rt_material3d_set_specular_map(void *obj, void *pixels) {
    rt_material3d *mat = material_checked(obj);
    if (!mat)
        return;
    material_assign_pixels_ref(&mat->specular_map, pixels);
}

/// @brief Assign an emissive map texture for self-illuminated surface regions.
void rt_material3d_set_emissive_map(void *obj, void *pixels) {
    rt_material3d *mat = material_checked(obj);
    if (!mat)
        return;
    material_assign_pixels_ref(&mat->emissive_map, pixels);
}

/// @brief Set the emissive (self-illumination) color, independent of scene lights.
void rt_material3d_set_emissive_color(void *obj, double r, double g, double b) {
    rt_material3d *m = material_checked(obj);
    if (!m)
        return;
    m->emissive[0] = sanitize_color(r);
    m->emissive[1] = sanitize_color(g);
    m->emissive[2] = sanitize_color(b);
}

/// @brief Set normal-map intensity (≥ 0). 1.0 = no scaling, > 1 amplifies bumps, < 1 flattens.
void rt_material3d_set_normal_scale(void *obj, double value) {
    rt_material3d *mat = material_checked(obj);
    if (!mat)
        return;
    mat->normal_scale = clamp_min(value, 0.0);
}

/// @brief Read the normal-map scale factor (default 1.0).
double rt_material3d_get_normal_scale(void *obj) {
    rt_material3d *mat = material_checked(obj);
    if (!mat)
        return 1.0;
    return mat->normal_scale;
}

/// @brief Set the alpha-handling mode (RT_MATERIAL3D_ALPHA_MODE_OPAQUE / MASK / BLEND).
/// OPAQUE: ignore alpha. MASK: discard fragments below cutoff. BLEND: depth-sorted transparency.
/// Out-of-range values are clamped to OPAQUE.
void rt_material3d_set_alpha_mode(void *obj, int64_t mode) {
    rt_material3d *mat = material_checked(obj);
    if (!mat)
        return;
    if (mode < RT_MATERIAL3D_ALPHA_MODE_OPAQUE || mode > RT_MATERIAL3D_ALPHA_MODE_BLEND)
        mode = RT_MATERIAL3D_ALPHA_MODE_OPAQUE;
    mat->alpha_mode = (int32_t)mode;
    mat->alpha_mode_auto = 0;
}

/// @brief Read the alpha mode (default OPAQUE).
int64_t rt_material3d_get_alpha_mode(void *obj) {
    rt_material3d *mat = material_checked(obj);
    if (!mat)
        return RT_MATERIAL3D_ALPHA_MODE_OPAQUE;
    return mat->alpha_mode;
}

/// @brief Toggle double-sided rendering. Disables backface culling for this material — useful
/// for foliage, banners, anything that should look correct from both sides. Increases fillrate.
void rt_material3d_set_double_sided(void *obj, int8_t enabled) {
    rt_material3d *mat = material_checked(obj);
    if (!mat)
        return;
    mat->double_sided = enabled ? 1 : 0;
}

/// @brief Returns 1 if double-sided rendering is enabled, 0 otherwise.
int8_t rt_material3d_get_double_sided(void *obj) {
    rt_material3d *mat = material_checked(obj);
    if (!mat)
        return 0;
    return mat->double_sided ? 1 : 0;
}

#else
typedef int rt_graphics_disabled_tu_guard;
#endif /* VIPER_ENABLE_GRAPHICS */
