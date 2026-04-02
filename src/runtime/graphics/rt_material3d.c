//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_material3d.c
// Purpose: Viper.Graphics3D.Material3D — surface appearance properties.
//
// Key invariants:
//   - Defaults: diffuse=(1,1,1,1), specular=(1,1,1), shininess=32,
//     alpha=1.0, emissive=(0,0,0), reflectivity=0.0, unlit=false.
//   - Texture/normal_map/specular_map/emissive_map/env_map are retained
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
extern void rt_trap(const char *msg);

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
    material_release_ref(&mat->env_map);
}

static void material_assign_ref(void **slot, void *value) {
    if (*slot == value)
        return;
    rt_obj_retain_maybe(value);
    material_release_ref(slot);
    *slot = value;
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
    mat->vptr = NULL;
    mat->diffuse[0] = 1.0;
    mat->diffuse[1] = 1.0;
    mat->diffuse[2] = 1.0;
    mat->diffuse[3] = 1.0;
    mat->specular[0] = mat->specular[1] = mat->specular[2] = 1.0;
    mat->shininess = 32.0;
    mat->texture = NULL;
    mat->normal_map = NULL;
    mat->specular_map = NULL;
    mat->emissive_map = NULL;
    mat->emissive[0] = mat->emissive[1] = mat->emissive[2] = 0.0;
    mat->alpha = 1.0;
    mat->env_map = NULL;
    mat->reflectivity = 0.0;
    mat->unlit = 0;
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

/// @brief Assign a normal map texture for per-pixel bump/detail lighting.
void rt_material3d_set_normal_map(void *obj, void *pixels) {
    if (!obj)
        return;
    material_assign_ref(&((rt_material3d *)obj)->normal_map, pixels);
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

#endif /* VIPER_ENABLE_GRAPHICS */
