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
//   - Texture/normal_map/specular_map/emissive_map/env_map are borrowed
//     pointers to GC-managed Pixels/CubeMap3D objects (not owned).
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
extern void rt_trap(const char *msg);

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
    return mat;
}

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

void *rt_material3d_new_textured(void *pixels) {
    rt_material3d *mat = (rt_material3d *)rt_material3d_new();
    if (!mat)
        return NULL;
    mat->texture = pixels;
    return mat;
}

/// @brief Set the color of the material3d.
void rt_material3d_set_color(void *obj, double r, double g, double b) {
    if (!obj)
        return;
    rt_material3d *mat = (rt_material3d *)obj;
    mat->diffuse[0] = r;
    mat->diffuse[1] = g;
    mat->diffuse[2] = b;
}

/// @brief Set the texture of the material3d.
void rt_material3d_set_texture(void *obj, void *pixels) {
    if (!obj)
        return;
    ((rt_material3d *)obj)->texture = pixels;
}

/// @brief Set the shininess of the material3d.
void rt_material3d_set_shininess(void *obj, double s) {
    if (!obj)
        return;
    ((rt_material3d *)obj)->shininess = s;
}

/// @brief Set the unlit of the material3d.
void rt_material3d_set_unlit(void *obj, int8_t unlit) {
    if (!obj)
        return;
    ((rt_material3d *)obj)->unlit = unlit;
}

void rt_material3d_set_shading_model(void *obj, int64_t model) {
    if (!obj)
        return;
    if (model < 0 || model > 5)
        model = 0;
    ((rt_material3d *)obj)->shading_model = (int32_t)model;
}

void rt_material3d_set_custom_param(void *obj, int64_t index, double value) {
    if (!obj || index < 0 || index >= 8)
        return;
    ((rt_material3d *)obj)->custom_params[index] = value;
}

/// @brief Set the alpha of the material3d.
void rt_material3d_set_alpha(void *obj, double alpha) {
    if (!obj)
        return;
    ((rt_material3d *)obj)->alpha = alpha;
}

/// @brief Get the alpha of the material3d.
double rt_material3d_get_alpha(void *obj) {
    if (!obj)
        return 1.0;
    return ((rt_material3d *)obj)->alpha;
}

/// @brief Set the normal map of the material3d.
void rt_material3d_set_normal_map(void *obj, void *pixels) {
    if (!obj)
        return;
    ((rt_material3d *)obj)->normal_map = pixels;
}

/// @brief Set the specular map of the material3d.
void rt_material3d_set_specular_map(void *obj, void *pixels) {
    if (!obj)
        return;
    ((rt_material3d *)obj)->specular_map = pixels;
}

/// @brief Set the emissive map of the material3d.
void rt_material3d_set_emissive_map(void *obj, void *pixels) {
    if (!obj)
        return;
    ((rt_material3d *)obj)->emissive_map = pixels;
}

/// @brief Set the emissive color of the material3d.
void rt_material3d_set_emissive_color(void *obj, double r, double g, double b) {
    if (!obj)
        return;
    rt_material3d *m = (rt_material3d *)obj;
    m->emissive[0] = r;
    m->emissive[1] = g;
    m->emissive[2] = b;
}

#endif /* VIPER_ENABLE_GRAPHICS */
