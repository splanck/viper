//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_cubemap3d.c
// Purpose: Viper.Graphics3D.CubeMap3D — 6-face cube map texture for
//   skybox rendering and environment reflections.
//
// Key invariants:
//   - All 6 faces must be square and the same dimensions
//   - Faces are Pixels objects (GC-managed, NOT owned by CubeMap3D)
//   - Face order: +X, -X, +Y, -Y, +Z, -Z
//
// Links: rt_canvas3d.h, rt_canvas3d_internal.h, plans/3d/11-cube-maps.md
//
//===----------------------------------------------------------------------===//

#ifdef VIPER_ENABLE_GRAPHICS

#include "rt_canvas3d.h"
#include "rt_canvas3d_internal.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>

extern void *rt_obj_new_i64(int64_t class_id, int64_t byte_size);
extern void rt_trap(const char *msg);

extern int64_t rt_pixels_width(void *pixels);
extern int64_t rt_pixels_height(void *pixels);
extern int64_t rt_pixels_get(void *pixels, int64_t x, int64_t y);

void *rt_cubemap3d_new(void *px, void *nx, void *py, void *ny, void *pz, void *nz)
{
    void *faces[6] = {px, nx, py, ny, pz, nz};

    /* Validate: all faces must be non-null */
    for (int i = 0; i < 6; i++)
    {
        if (!faces[i])
        {
            rt_trap("CubeMap3D.New: all 6 faces must be non-null");
            return NULL;
        }
    }

    /* Validate: all faces must be square and same dimensions */
    int64_t size = rt_pixels_width(faces[0]);
    if (size <= 0 || rt_pixels_height(faces[0]) != size)
    {
        rt_trap("CubeMap3D.New: faces must be square");
        return NULL;
    }

    for (int i = 1; i < 6; i++)
    {
        if (rt_pixels_width(faces[i]) != size || rt_pixels_height(faces[i]) != size)
        {
            rt_trap("CubeMap3D.New: all faces must have same dimensions");
            return NULL;
        }
    }

    rt_cubemap3d *cm = (rt_cubemap3d *)rt_obj_new_i64(0, (int64_t)sizeof(rt_cubemap3d));
    if (!cm)
    {
        rt_trap("CubeMap3D.New: memory allocation failed");
        return NULL;
    }
    cm->vptr = NULL;
    for (int i = 0; i < 6; i++)
        cm->faces[i] = faces[i];
    cm->face_size = size;
    /* No finalizer needed — faces are GC-managed Pixels, not owned by us */
    return cm;
}

//=============================================================================
// Software cube map sampling
//=============================================================================

/* Sample a cube map given a 3D direction vector.
 * Returns color as floats [0.0, 1.0]. */
void rt_cubemap_sample(
    const rt_cubemap3d *cm, float dx, float dy, float dz, float *out_r, float *out_g, float *out_b)
{
    if (!cm)
    {
        *out_r = *out_g = *out_b = 0.0f;
        return;
    }

    /* Guard against zero-length direction (undefined ray) */
    if (fabsf(dx) < 1e-10f && fabsf(dy) < 1e-10f && fabsf(dz) < 1e-10f)
    {
        *out_r = *out_g = *out_b = 0.0f;
        return;
    }

    float ax = fabsf(dx), ay = fabsf(dy), az = fabsf(dz);
    int face;
    float u, v, ma;

    if (ax >= ay && ax >= az)
    {
        ma = ax;
        if (dx > 0)
        {
            face = 0; /* +X */
            u = -dz;
            v = -dy;
        }
        else
        {
            face = 1; /* -X */
            u = dz;
            v = -dy;
        }
    }
    else if (ay >= ax && ay >= az)
    {
        ma = ay;
        if (dy > 0)
        {
            face = 2; /* +Y */
            u = dx;
            v = dz;
        }
        else
        {
            face = 3; /* -Y */
            u = dx;
            v = -dz;
        }
    }
    else
    {
        ma = az;
        if (dz > 0)
        {
            face = 4; /* +Z */
            u = dx;
            v = -dy;
        }
        else
        {
            face = 5; /* -Z */
            u = -dx;
            v = -dy;
        }
    }

    /* Map to [0, 1] */
    if (ma < 1e-8f)
        ma = 1e-8f;
    u = (u / ma + 1.0f) * 0.5f;
    v = (v / ma + 1.0f) * 0.5f;

    /* Sample face texture using public API */
    void *face_pixels = cm->faces[face];
    if (!face_pixels)
    {
        *out_r = *out_g = *out_b = 0.0f;
        return;
    }
    int64_t fw = rt_pixels_width(face_pixels);
    int64_t fh = rt_pixels_height(face_pixels);

    int px = (int)(u * (float)fw);
    int py = (int)(v * (float)fh);
    if (px < 0)
        px = 0;
    if (px >= (int)fw)
        px = (int)fw - 1;
    if (py < 0)
        py = 0;
    if (py >= (int)fh)
        py = (int)fh - 1;

    int64_t pixel = rt_pixels_get(face_pixels, px, py);
    *out_r = (float)((pixel >> 24) & 0xFF) / 255.0f;
    *out_g = (float)((pixel >> 16) & 0xFF) / 255.0f;
    *out_b = (float)((pixel >> 8) & 0xFF) / 255.0f;
}

//=============================================================================
// Canvas3D skybox
//=============================================================================

void rt_canvas3d_set_skybox(void *canvas, void *cubemap)
{
    if (!canvas)
        return;
    ((rt_canvas3d *)canvas)->skybox = (rt_cubemap3d *)cubemap;
}

void rt_canvas3d_clear_skybox(void *canvas)
{
    if (!canvas)
        return;
    ((rt_canvas3d *)canvas)->skybox = NULL;
}

//=============================================================================
// Material3D env map + reflectivity
//=============================================================================

void rt_material3d_set_env_map(void *obj, void *cubemap)
{
    if (!obj)
        return;
    ((rt_material3d *)obj)->env_map = cubemap;
}

void rt_material3d_set_reflectivity(void *obj, double r)
{
    if (!obj)
        return;
    ((rt_material3d *)obj)->reflectivity = r;
}

double rt_material3d_get_reflectivity(void *obj)
{
    if (!obj)
        return 0.0;
    return ((rt_material3d *)obj)->reflectivity;
}

#endif /* VIPER_ENABLE_GRAPHICS */
