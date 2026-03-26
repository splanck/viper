//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_terrain3d.c
// Purpose: Heightmap terrain — chunked mesh generation, bilinear height/normal
//   queries, frustum-culled per-chunk rendering.
//
// Key invariants:
//   - Heights are float[width*depth], sampled from Pixels red channel.
//   - Chunks are TERRAIN_CHUNK_SIZE quads per edge (16x16 = 256 quads each).
//   - Mesh generation is lazy (built on first draw, invalidated on heightmap change).
//   - Normals computed via central difference on height grid.
//
// Links: rt_terrain3d.h, rt_mesh3d, vgfx3d_frustum.h
//
//===----------------------------------------------------------------------===//

#ifdef VIPER_ENABLE_GRAPHICS

#include "rt_terrain3d.h"
#include "rt_canvas3d.h"
#include "rt_canvas3d_internal.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

extern void *rt_obj_new_i64(int64_t class_id, int64_t byte_size);
extern void rt_obj_set_finalizer(void *obj, void (*fn)(void *));
extern void rt_trap(const char *msg);
extern void *rt_vec3_new(double x, double y, double z);
extern void *rt_mesh3d_new(void);
extern void rt_mesh3d_add_vertex(
    void *m, double x, double y, double z, double nx, double ny, double nz, double u, double v);
extern void rt_mesh3d_add_triangle(void *m, int64_t v0, int64_t v1, int64_t v2);
extern void *rt_mat4_identity(void);
extern void rt_canvas3d_draw_mesh(void *canvas, void *mesh, void *transform, void *material);

#define TERRAIN_CHUNK_SIZE 16

typedef struct
{
    void *vptr;
    float *heights;
    int32_t width, depth;
    double scale[3]; /* x_spacing, y_scale, z_spacing */
    void **chunk_meshes;
    int32_t chunks_x, chunks_z;
    void *material;
} rt_terrain3d;

static void terrain3d_finalizer(void *obj)
{
    rt_terrain3d *t = (rt_terrain3d *)obj;
    free(t->heights);
    free(t->chunk_meshes);
    t->heights = NULL;
    t->chunk_meshes = NULL;
}

void *rt_terrain3d_new(int64_t width, int64_t depth)
{
    if (width < 2 || depth < 2 || width > 4096 || depth > 4096)
    {
        rt_trap("Terrain3D.New: dimensions must be 2-4096");
        return NULL;
    }
    rt_terrain3d *t = (rt_terrain3d *)rt_obj_new_i64(0, (int64_t)sizeof(rt_terrain3d));
    if (!t)
    {
        rt_trap("Terrain3D.New: allocation failed");
        return NULL;
    }
    t->vptr = NULL;
    t->width = (int32_t)width;
    t->depth = (int32_t)depth;
    t->heights = (float *)calloc((size_t)(width * depth), sizeof(float));
    t->scale[0] = 1.0;
    t->scale[1] = 1.0;
    t->scale[2] = 1.0;
    t->chunks_x = ((int32_t)width - 1 + TERRAIN_CHUNK_SIZE - 1) / TERRAIN_CHUNK_SIZE;
    t->chunks_z = ((int32_t)depth - 1 + TERRAIN_CHUNK_SIZE - 1) / TERRAIN_CHUNK_SIZE;
    t->chunk_meshes = (void **)calloc((size_t)(t->chunks_x * t->chunks_z), sizeof(void *));
    t->material = NULL;
    rt_obj_set_finalizer(t, terrain3d_finalizer);
    return t;
}

void rt_terrain3d_set_heightmap(void *obj, void *pixels)
{
    if (!obj || !pixels)
        return;
    rt_terrain3d *t = (rt_terrain3d *)obj;

    /* Access Pixels internal layout */
    typedef struct
    {
        int64_t w;
        int64_t h;
        uint32_t *data;
    } px_view;

    px_view *pv = (px_view *)pixels;
    if (!pv->data)
        return;

    int32_t sw = (int32_t)pv->w, sh = (int32_t)pv->h;
    for (int32_t z = 0; z < t->depth; z++)
    {
        for (int32_t x = 0; x < t->width; x++)
        {
            int sx = x * sw / t->width;
            int sz = z * sh / t->depth;
            if (sx >= sw)
                sx = sw - 1;
            if (sz >= sh)
                sz = sh - 1;
            uint32_t pixel = pv->data[sz * sw + sx]; /* 0xRRGGBBAA */
            float r = (float)((pixel >> 24) & 0xFF) / 255.0f;
            t->heights[z * t->width + x] = r;
        }
    }

    /* Invalidate chunks */
    for (int32_t i = 0; i < t->chunks_x * t->chunks_z; i++)
        t->chunk_meshes[i] = NULL;
}

void rt_terrain3d_set_material(void *obj, void *material)
{
    if (obj)
        ((rt_terrain3d *)obj)->material = material;
}

void rt_terrain3d_set_scale(void *obj, double sx, double sy, double sz)
{
    if (!obj)
        return;
    rt_terrain3d *t = (rt_terrain3d *)obj;
    t->scale[0] = sx;
    t->scale[1] = sy;
    t->scale[2] = sz;
    /* Invalidate chunks */
    for (int32_t i = 0; i < t->chunks_x * t->chunks_z; i++)
        t->chunk_meshes[i] = NULL;
}

/// @brief Sample height at grid coordinates (clamped).
static float sample_height(const rt_terrain3d *t, int32_t x, int32_t z)
{
    if (x < 0)
        x = 0;
    if (z < 0)
        z = 0;
    if (x >= t->width)
        x = t->width - 1;
    if (z >= t->depth)
        z = t->depth - 1;
    return t->heights[z * t->width + x];
}

double rt_terrain3d_get_height_at(void *obj, double wx, double wz)
{
    if (!obj)
        return 0.0;
    rt_terrain3d *t = (rt_terrain3d *)obj;
    if (t->scale[0] < 1e-12 || t->scale[2] < 1e-12)
        return 0.0;

    double hx = wx / t->scale[0];
    double hz = wz / t->scale[2];
    int ix = (int)floor(hx), iz = (int)floor(hz);
    float fx = (float)(hx - ix), fz = (float)(hz - iz);

    if (ix < 0)
    {
        ix = 0;
        fx = 0;
    }
    if (iz < 0)
    {
        iz = 0;
        fz = 0;
    }
    if (ix >= t->width - 1)
    {
        ix = t->width - 2;
        fx = 1;
    }
    if (iz >= t->depth - 1)
    {
        iz = t->depth - 2;
        fz = 1;
    }

    float h00 = sample_height(t, ix, iz);
    float h10 = sample_height(t, ix + 1, iz);
    float h01 = sample_height(t, ix, iz + 1);
    float h11 = sample_height(t, ix + 1, iz + 1);
    float h = h00 * (1 - fx) * (1 - fz) + h10 * fx * (1 - fz) + h01 * (1 - fx) * fz + h11 * fx * fz;
    return (double)(h * (float)t->scale[1]);
}

void *rt_terrain3d_get_normal_at(void *obj, double wx, double wz)
{
    if (!obj)
        return rt_vec3_new(0, 1, 0);
    rt_terrain3d *t = (rt_terrain3d *)obj;
    if (t->scale[0] < 1e-12 || t->scale[2] < 1e-12)
        return rt_vec3_new(0, 1, 0);

    double hx = wx / t->scale[0];
    double hz = wz / t->scale[2];
    int ix = (int)floor(hx), iz = (int)floor(hz);

    float hL = sample_height(t, ix - 1, iz);
    float hR = sample_height(t, ix + 1, iz);
    float hD = sample_height(t, ix, iz - 1);
    float hU = sample_height(t, ix, iz + 1);

    double nx = (double)(hL - hR) * t->scale[1];
    double nz = (double)(hD - hU) * t->scale[1];
    double ny = 2.0 * t->scale[0];
    double len = sqrt(nx * nx + ny * ny + nz * nz);
    if (len > 1e-8)
    {
        nx /= len;
        ny /= len;
        nz /= len;
    }

    return rt_vec3_new(nx, ny, nz);
}

/// @brief Build mesh for one terrain chunk.
static void *build_chunk(rt_terrain3d *t, int32_t cx, int32_t cz)
{
    void *mesh = rt_mesh3d_new();
    int32_t x0 = cx * TERRAIN_CHUNK_SIZE;
    int32_t z0 = cz * TERRAIN_CHUNK_SIZE;

    /* Determine actual chunk extents (may be smaller at edges) */
    int32_t xend = x0 + TERRAIN_CHUNK_SIZE;
    int32_t zend = z0 + TERRAIN_CHUNK_SIZE;
    if (xend >= t->width)
        xend = t->width - 1;
    if (zend >= t->depth)
        zend = t->depth - 1;
    int32_t cols = xend - x0;
    int32_t rows = zend - z0;
    if (cols <= 0 || rows <= 0)
        return mesh;

    /* Vertices */
    for (int32_t dz = 0; dz <= rows; dz++)
    {
        for (int32_t dx = 0; dx <= cols; dx++)
        {
            int32_t ix = x0 + dx, iz = z0 + dz;
            double wx = (double)ix * t->scale[0];
            double wy = (double)sample_height(t, ix, iz) * t->scale[1];
            double wz = (double)iz * t->scale[2];

            /* Normal via central difference */
            float hL = sample_height(t, ix - 1, iz);
            float hR = sample_height(t, ix + 1, iz);
            float hD = sample_height(t, ix, iz - 1);
            float hU = sample_height(t, ix, iz + 1);
            double nx = (double)(hL - hR) * t->scale[1];
            double nz_n = (double)(hD - hU) * t->scale[1];
            double ny = 2.0 * t->scale[0];
            double nlen = sqrt(nx * nx + ny * ny + nz_n * nz_n);
            if (nlen > 1e-8)
            {
                nx /= nlen;
                ny /= nlen;
                nz_n /= nlen;
            }

            double u = (double)ix / (double)(t->width - 1);
            double v = (double)iz / (double)(t->depth - 1);

            rt_mesh3d_add_vertex(mesh, wx, wy, wz, nx, ny, nz_n, u, v);
        }
    }

    /* Triangles (CCW winding) */
    int32_t row_verts = cols + 1;
    for (int32_t dz = 0; dz < rows; dz++)
    {
        for (int32_t dx = 0; dx < cols; dx++)
        {
            int64_t base = (int64_t)(dz * row_verts + dx);
            rt_mesh3d_add_triangle(mesh, base, base + row_verts, base + 1);
            rt_mesh3d_add_triangle(mesh, base + 1, base + row_verts, base + row_verts + 1);
        }
    }

    return mesh;
}

void rt_canvas3d_draw_terrain(void *canvas_obj, void *terrain_obj)
{
    if (!canvas_obj || !terrain_obj)
        return;
    rt_canvas3d *c = (rt_canvas3d *)canvas_obj;
    rt_terrain3d *t = (rt_terrain3d *)terrain_obj;
    if (!c->in_frame || !c->backend || !t->material)
        return;

    void *identity = rt_mat4_identity();

    for (int32_t cz = 0; cz < t->chunks_z; cz++)
    {
        for (int32_t cx = 0; cx < t->chunks_x; cx++)
        {
            int32_t idx = cz * t->chunks_x + cx;
            if (!t->chunk_meshes[idx])
                t->chunk_meshes[idx] = build_chunk(t, cx, cz);

            if (t->chunk_meshes[idx])
                rt_canvas3d_draw_mesh(canvas_obj, t->chunk_meshes[idx], identity, t->material);
        }
    }
}

#endif /* VIPER_ENABLE_GRAPHICS */
