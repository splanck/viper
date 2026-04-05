//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_light3d.c
// Purpose: Viper.Graphics3D.Light3D — directional, point, and ambient lights.
//
// Key invariants:
//   - Light types: 0=directional (uses direction), 1=point (uses position
//     + attenuation), 2=ambient (uniform, uses color * intensity only).
//   - Up to VGFX3D_MAX_LIGHTS (8) per Canvas3D, set via SetLight(canvas, slot, light).
//   - Default intensity=1.0, attenuation=0.0 (no falloff for point lights).
//   - Direction/position are borrowed Vec3 values, copied at creation time.
//
// Links: rt_canvas3d.h, rt_canvas3d_internal.h
//
//===----------------------------------------------------------------------===//

#ifdef VIPER_ENABLE_GRAPHICS

#include "rt_canvas3d.h"
#include "rt_canvas3d_internal.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

extern void *rt_obj_new_i64(int64_t class_id, int64_t byte_size);
#include "rt_trap.h"
extern double rt_vec3_x(void *v);
extern double rt_vec3_y(void *v);
extern double rt_vec3_z(void *v);

/// @brief Create a directional light (e.g., sun or moon).
/// @details Directional lights have no position — only a direction vector.
///          All surfaces in the scene receive parallel rays along this direction,
///          making them ideal for outdoor/global illumination. The direction
///          vector is copied from the provided Vec3 at creation time.
/// @param direction Vec3 indicating the light direction (copied, not borrowed).
/// @param r Red color component [0.0–1.0].
/// @param g Green color component [0.0–1.0].
/// @param b Blue color component [0.0–1.0].
/// @return Opaque light handle, or NULL on failure.
void *rt_light3d_new_directional(void *direction, double r, double g, double b) {
    if (!direction) {
        rt_trap("Light3D.NewDirectional: direction must not be null");
        return NULL;
    }
    rt_light3d *light = (rt_light3d *)rt_obj_new_i64(0, (int64_t)sizeof(rt_light3d));
    if (!light) {
        rt_trap("Light3D.NewDirectional: memory allocation failed");
        return NULL;
    }
    light->vptr = NULL;
    light->type = 0; /* directional */
    light->direction[0] = rt_vec3_x(direction);
    light->direction[1] = rt_vec3_y(direction);
    light->direction[2] = rt_vec3_z(direction);
    light->position[0] = light->position[1] = light->position[2] = 0.0;
    light->color[0] = r;
    light->color[1] = g;
    light->color[2] = b;
    light->intensity = 1.0;
    light->attenuation = 0.0;
    return light;
}

/// @brief Create a point light that radiates from a position in all directions.
/// @details Point lights simulate light bulbs, torches, etc. Intensity falls
///          off with distance according to the attenuation factor. An attenuation
///          of 0.0 means no falloff (constant brightness at all distances).
/// @param position    Vec3 world-space position (copied at creation time).
/// @param r           Red color component [0.0–1.0].
/// @param g           Green color component [0.0–1.0].
/// @param b           Blue color component [0.0–1.0].
/// @param attenuation Distance falloff factor (0.0 = no falloff).
/// @return Opaque light handle, or NULL on failure.
void *rt_light3d_new_point(void *position, double r, double g, double b, double attenuation) {
    if (!position) {
        rt_trap("Light3D.NewPoint: position must not be null");
        return NULL;
    }
    rt_light3d *light = (rt_light3d *)rt_obj_new_i64(0, (int64_t)sizeof(rt_light3d));
    if (!light) {
        rt_trap("Light3D.NewPoint: memory allocation failed");
        return NULL;
    }
    light->vptr = NULL;
    light->type = 1; /* point */
    light->direction[0] = light->direction[1] = light->direction[2] = 0.0;
    light->position[0] = rt_vec3_x(position);
    light->position[1] = rt_vec3_y(position);
    light->position[2] = rt_vec3_z(position);
    light->color[0] = r;
    light->color[1] = g;
    light->color[2] = b;
    light->intensity = 1.0;
    light->attenuation = attenuation;
    return light;
}

/// @brief Create an ambient light that illuminates all surfaces uniformly.
/// @details Ambient lights have no direction or position — they apply a flat
///          color contribution to every surface equally. Used to provide a base
///          illumination level so shadowed areas aren't completely black.
/// @param r Red color component [0.0–1.0].
/// @param g Green color component [0.0–1.0].
/// @param b Blue color component [0.0–1.0].
/// @return Opaque light handle, or NULL on failure.
void *rt_light3d_new_ambient(double r, double g, double b) {
    rt_light3d *light = (rt_light3d *)rt_obj_new_i64(0, (int64_t)sizeof(rt_light3d));
    if (!light) {
        rt_trap("Light3D.NewAmbient: memory allocation failed");
        return NULL;
    }
    light->vptr = NULL;
    light->type = 2; /* ambient */
    memset(light->direction, 0, sizeof(light->direction));
    memset(light->position, 0, sizeof(light->position));
    light->color[0] = r;
    light->color[1] = g;
    light->color[2] = b;
    light->intensity = 1.0;
    light->attenuation = 0.0;
    return light;
}

/// @brief Create a spot light (cone-shaped illumination from a position).
/// @details Spot lights combine a position, direction, and two cone angles.
///          Surfaces inside the inner angle receive full intensity; between
///          inner and outer angles intensity falls off smoothly. The angles
///          are converted to cosines at creation time for efficient shader
///          comparison (dot product vs. cosine threshold).
/// @param position    Vec3 world-space position of the light source.
/// @param direction   Vec3 direction the cone points toward.
/// @param r           Red color component [0.0–1.0].
/// @param g           Green color component [0.0–1.0].
/// @param b           Blue color component [0.0–1.0].
/// @param attenuation Distance falloff factor.
/// @param inner_angle Full-brightness cone half-angle in degrees.
/// @param outer_angle Outer cone half-angle in degrees (falloff edge).
/// @return Opaque light handle, or NULL on failure.
void *rt_light3d_new_spot(void *position,
                          void *direction,
                          double r,
                          double g,
                          double b,
                          double attenuation,
                          double inner_angle,
                          double outer_angle) {
    if (!position || !direction) {
        rt_trap("Light3D.NewSpot: position and direction must not be null");
        return NULL;
    }
    rt_light3d *light = (rt_light3d *)rt_obj_new_i64(0, (int64_t)sizeof(rt_light3d));
    if (!light) {
        rt_trap("Light3D.NewSpot: memory allocation failed");
        return NULL;
    }
    light->vptr = NULL;
    light->type = 3; /* spot */
    light->direction[0] = rt_vec3_x(direction);
    light->direction[1] = rt_vec3_y(direction);
    light->direction[2] = rt_vec3_z(direction);
    light->position[0] = rt_vec3_x(position);
    light->position[1] = rt_vec3_y(position);
    light->position[2] = rt_vec3_z(position);
    light->color[0] = r;
    light->color[1] = g;
    light->color[2] = b;
    light->intensity = 1.0;
    light->attenuation = attenuation;
    /* Convert angles (degrees) to cosines for shader comparison */
    double pi = 3.14159265358979323846;
    light->inner_cos = cos(inner_angle * pi / 180.0);
    light->outer_cos = cos(outer_angle * pi / 180.0);
    return light;
}

/// @brief Set the brightness multiplier for a light.
/// @details Scales the light's color contribution. Default is 1.0. Values
///          above 1.0 create over-bright highlights; 0.0 effectively disables
///          the light without removing it from the scene.
/// @param obj       Light handle.
/// @param intensity Brightness multiplier (default 1.0).
void rt_light3d_set_intensity(void *obj, double intensity) {
    if (!obj)
        return;
    ((rt_light3d *)obj)->intensity = intensity;
}

/// @brief Change the RGB color of a light after creation.
/// @param obj Light handle.
/// @param r   Red component [0.0–1.0].
/// @param g   Green component [0.0–1.0].
/// @param b   Blue component [0.0–1.0].
void rt_light3d_set_color(void *obj, double r, double g, double b) {
    if (!obj)
        return;
    rt_light3d *l = (rt_light3d *)obj;
    l->color[0] = r;
    l->color[1] = g;
    l->color[2] = b;
}

#else
typedef int rt_graphics_disabled_tu_guard;
#endif /* VIPER_ENABLE_GRAPHICS */
