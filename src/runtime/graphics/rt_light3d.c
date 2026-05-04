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
//   - Up to VGFX3D_MAX_LIGHTS (16) per Canvas3D, set via SetLight(canvas, slot, light).
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

static rt_light3d *light3d_checked(void *obj) {
    return (rt_light3d *)rt_g3d_checked_or_null(obj, RT_G3D_LIGHT3D_CLASS_ID);
}

/// @brief Clamp a value at zero from below — negatives collapse to zero.
/// @details Used for physical quantities like intensity, radius, and
///   attenuation where negative values are nonsensical. Chosen over a
///   `fmax(0, value)` call to keep the hot path branch-predictable on
///   inputs that are almost always non-negative.
static double clamp_min0(double value) {
    if (!isfinite(value))
        return 0.0;
    return value < 0.0 ? 0.0 : value;
}

/// @brief Clamp a value to the [0, 1] range, mapping NaN/inf to 0.
/// @details Used for RGB color components, which the runtime treats as linear [0, 1]
///   scalars. Anything NaN-poisoned would produce black pixels downstream, so sanitising
///   at the boundary keeps the material pipeline free of infectious non-finite values.
static double clamp01(double value) {
    if (!isfinite(value))
        return 0.0;
    if (value < 0.0)
        return 0.0;
    if (value > 1.0)
        return 1.0;
    return value;
}

/// @brief Return `value` if finite, else 0. Boundary cleanup for positions/directions.
static double finite_or_zero(double value) {
    return isfinite(value) ? value : 0.0;
}

/// @brief Clamp a spot-light cone angle to [0°, 89°], substituting `fallback` when NaN.
/// @details Spot lights convert angles to cosines in the shader; letting the half-angle
///   reach 90° would produce `cos ≈ 0`, collapsing the cone to a single ray and killing
///   the light. Capping at 89° guarantees a non-degenerate cone while still letting
///   artists author very wide spots.
static double sanitize_spot_angle(double value, double fallback) {
    if (!isfinite(value))
        value = fallback;
    if (value < 0.0)
        return 0.0;
    if (value > 89.0)
        return 89.0;
    return value;
}

/// @brief Clamp and mutually-validate inner and outer spot-light cone angles.
/// @details Ensures `inner` < `outer` with at least a 0.01° gap, and both angles
///          are in [0°, 89°]. When outer ≤ inner + eps the outer is widened; when
///          either pointer is NULL, its angle defaults to 0° (inner) or inner+eps (outer).
static void sanitize_spot_angles(double *inner_angle, double *outer_angle) {
    const double eps = 0.01;
    double inner = sanitize_spot_angle(inner_angle ? *inner_angle : 0.0, 0.0);
    double outer = sanitize_spot_angle(outer_angle ? *outer_angle : inner + eps, inner + eps);
    if (outer <= inner + eps) {
        if (inner > 89.0 - eps)
            inner = 89.0 - eps;
        outer = inner + eps;
    }
    if (outer > 89.0) {
        outer = 89.0;
        if (inner >= outer)
            inner = outer - eps;
    }
    if (inner < 0.0)
        inner = 0.0;
    if (inner_angle)
        *inner_angle = inner;
    if (outer_angle)
        *outer_angle = outer;
}

/// @brief Normalize a light's direction vector, defaulting to down on zero input.
/// @details When a caller hands in a degenerate (zero-length) direction,
///   rather than producing NaN we fall back to `(0, -1, 0)` — straight-down
///   sun — so subsequent lighting math stays finite. The 1e-8 threshold
///   is permissive enough that any artistically-meaningful input passes,
///   but catches explicit zeroing.
static void normalize_light_direction(double *x, double *y, double *z) {
    double len;

    if (!x || !y || !z)
        return;
    *x = finite_or_zero(*x);
    *y = finite_or_zero(*y);
    *z = finite_or_zero(*z);
    len = sqrt((*x) * (*x) + (*y) * (*y) + (*z) * (*z));
    if (!isfinite(len) || len <= 1e-8) {
        *x = 0.0;
        *y = -1.0;
        *z = 0.0;
        return;
    }
    *x /= len;
    *y /= len;
    *z /= len;
}

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
    rt_light3d *light = (rt_light3d *)rt_obj_new_i64(RT_G3D_LIGHT3D_CLASS_ID, (int64_t)sizeof(rt_light3d));
    if (!light) {
        rt_trap("Light3D.NewDirectional: memory allocation failed");
        return NULL;
    }
    light->vptr = NULL;
    light->type = 0; /* directional */
    light->direction[0] = rt_vec3_x(direction);
    light->direction[1] = rt_vec3_y(direction);
    light->direction[2] = rt_vec3_z(direction);
    normalize_light_direction(&light->direction[0], &light->direction[1], &light->direction[2]);
    light->position[0] = light->position[1] = light->position[2] = 0.0;
    light->color[0] = clamp01(r);
    light->color[1] = clamp01(g);
    light->color[2] = clamp01(b);
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
    rt_light3d *light = (rt_light3d *)rt_obj_new_i64(RT_G3D_LIGHT3D_CLASS_ID, (int64_t)sizeof(rt_light3d));
    if (!light) {
        rt_trap("Light3D.NewPoint: memory allocation failed");
        return NULL;
    }
    light->vptr = NULL;
    light->type = 1; /* point */
    light->direction[0] = light->direction[1] = light->direction[2] = 0.0;
    light->position[0] = finite_or_zero(rt_vec3_x(position));
    light->position[1] = finite_or_zero(rt_vec3_y(position));
    light->position[2] = finite_or_zero(rt_vec3_z(position));
    light->color[0] = clamp01(r);
    light->color[1] = clamp01(g);
    light->color[2] = clamp01(b);
    light->intensity = 1.0;
    light->attenuation = clamp_min0(attenuation);
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
    rt_light3d *light = (rt_light3d *)rt_obj_new_i64(RT_G3D_LIGHT3D_CLASS_ID, (int64_t)sizeof(rt_light3d));
    if (!light) {
        rt_trap("Light3D.NewAmbient: memory allocation failed");
        return NULL;
    }
    light->vptr = NULL;
    light->type = 2; /* ambient */
    memset(light->direction, 0, sizeof(light->direction));
    memset(light->position, 0, sizeof(light->position));
    light->color[0] = clamp01(r);
    light->color[1] = clamp01(g);
    light->color[2] = clamp01(b);
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
    rt_light3d *light = (rt_light3d *)rt_obj_new_i64(RT_G3D_LIGHT3D_CLASS_ID, (int64_t)sizeof(rt_light3d));
    if (!light) {
        rt_trap("Light3D.NewSpot: memory allocation failed");
        return NULL;
    }
    light->vptr = NULL;
    light->type = 3; /* spot */
    light->direction[0] = rt_vec3_x(direction);
    light->direction[1] = rt_vec3_y(direction);
    light->direction[2] = rt_vec3_z(direction);
    normalize_light_direction(&light->direction[0], &light->direction[1], &light->direction[2]);
    light->position[0] = finite_or_zero(rt_vec3_x(position));
    light->position[1] = finite_or_zero(rt_vec3_y(position));
    light->position[2] = finite_or_zero(rt_vec3_z(position));
    light->color[0] = clamp01(r);
    light->color[1] = clamp01(g);
    light->color[2] = clamp01(b);
    light->intensity = 1.0;
    light->attenuation = clamp_min0(attenuation);
    /* Convert angles (degrees) to cosines for shader comparison */
    double pi = 3.14159265358979323846;
    sanitize_spot_angles(&inner_angle, &outer_angle);
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
    rt_light3d *light = light3d_checked(obj);
    if (!light)
        return;
    light->intensity = clamp_min0(intensity);
}

/// @brief Change the RGB color of a light after creation.
/// @param obj Light handle.
/// @param r   Red component [0.0–1.0].
/// @param g   Green component [0.0–1.0].
/// @param b   Blue component [0.0–1.0].
void rt_light3d_set_color(void *obj, double r, double g, double b) {
    rt_light3d *l = light3d_checked(obj);
    if (!l)
        return;
    l->color[0] = clamp01(r);
    l->color[1] = clamp01(g);
    l->color[2] = clamp01(b);
}

#else
typedef int rt_graphics_disabled_tu_guard;
#endif /* VIPER_ENABLE_GRAPHICS */
