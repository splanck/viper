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

void *rt_material3d_new(void)
{
    rt_material3d *mat = (rt_material3d *)rt_obj_new_i64(0, (int64_t)sizeof(rt_material3d));
    if (!mat)
    {
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
    mat->unlit = 0;
    return mat;
}

void *rt_material3d_new_color(double r, double g, double b)
{
    rt_material3d *mat = (rt_material3d *)rt_material3d_new();
    if (!mat)
        return NULL;
    mat->diffuse[0] = r;
    mat->diffuse[1] = g;
    mat->diffuse[2] = b;
    mat->diffuse[3] = 1.0;
    return mat;
}

void *rt_material3d_new_textured(void *pixels)
{
    rt_material3d *mat = (rt_material3d *)rt_material3d_new();
    if (!mat)
        return NULL;
    mat->texture = pixels;
    return mat;
}

void rt_material3d_set_color(void *obj, double r, double g, double b)
{
    if (!obj)
        return;
    rt_material3d *mat = (rt_material3d *)obj;
    mat->diffuse[0] = r;
    mat->diffuse[1] = g;
    mat->diffuse[2] = b;
}

void rt_material3d_set_texture(void *obj, void *pixels)
{
    if (!obj)
        return;
    ((rt_material3d *)obj)->texture = pixels;
}

void rt_material3d_set_shininess(void *obj, double s)
{
    if (!obj)
        return;
    ((rt_material3d *)obj)->shininess = s;
}

void rt_material3d_set_unlit(void *obj, int8_t unlit)
{
    if (!obj)
        return;
    ((rt_material3d *)obj)->unlit = unlit;
}

#endif /* VIPER_ENABLE_GRAPHICS */
