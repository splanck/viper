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

#include <stdint.h>
#include <string.h>

extern void *rt_obj_new_i64(int64_t class_id, int64_t byte_size);
extern void rt_trap(const char *msg);
extern double rt_vec3_x(void *v);
extern double rt_vec3_y(void *v);
extern double rt_vec3_z(void *v);

void *rt_light3d_new_directional(void *direction, double r, double g, double b)
{
    if (!direction)
    {
        rt_trap("Light3D.NewDirectional: direction must not be null");
        return NULL;
    }
    rt_light3d *light = (rt_light3d *)rt_obj_new_i64(0, (int64_t)sizeof(rt_light3d));
    if (!light)
    {
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

void *rt_light3d_new_point(void *position, double r, double g, double b,
                           double attenuation)
{
    if (!position)
    {
        rt_trap("Light3D.NewPoint: position must not be null");
        return NULL;
    }
    rt_light3d *light = (rt_light3d *)rt_obj_new_i64(0, (int64_t)sizeof(rt_light3d));
    if (!light)
    {
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

void *rt_light3d_new_ambient(double r, double g, double b)
{
    rt_light3d *light = (rt_light3d *)rt_obj_new_i64(0, (int64_t)sizeof(rt_light3d));
    if (!light)
    {
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

void rt_light3d_set_intensity(void *obj, double intensity)
{
    if (!obj)
        return;
    ((rt_light3d *)obj)->intensity = intensity;
}

void rt_light3d_set_color(void *obj, double r, double g, double b)
{
    if (!obj)
        return;
    rt_light3d *l = (rt_light3d *)obj;
    l->color[0] = r;
    l->color[1] = g;
    l->color[2] = b;
}

#endif /* VIPER_ENABLE_GRAPHICS */
