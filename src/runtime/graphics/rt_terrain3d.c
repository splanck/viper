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

#include "vgfx3d_frustum.h"

#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

extern void *rt_obj_new_i64(int64_t class_id, int64_t byte_size);
extern void rt_obj_set_finalizer(void *obj, void (*fn)(void *));
#include "rt_trap.h"
extern void *rt_vec3_new(double x, double y, double z);
extern void *rt_mesh3d_new(void);
extern void rt_mesh3d_add_vertex(
    void *m, double x, double y, double z, double nx, double ny, double nz, double u, double v);
extern void rt_mesh3d_add_triangle(void *m, int64_t v0, int64_t v1, int64_t v2);
extern void *rt_mat4_identity(void);
extern void rt_canvas3d_draw_mesh(void *canvas, void *mesh, void *transform, void *material);

#define TERRAIN_CHUNK_SIZE 16

#define TERRAIN_MAX_SPLAT_LAYERS 4
#define TERRAIN_LOD_LEVELS 3

typedef struct {
    void *vptr;
    float *heights;
    int32_t width, depth;
    double scale[3];          /* x_spacing, y_scale, z_spacing */
    void **chunk_meshes;      /* LOD 0 (full res) mesh cache */
    void **chunk_meshes_lod1; /* LOD 1 (step=2) mesh cache */
    void **chunk_meshes_lod2; /* LOD 2 (step=4) mesh cache */
    float *chunk_aabbs;       /* 6 floats per chunk: min[3], max[3] */
    int32_t chunks_x, chunks_z;
    void *material;
    /* LOD distance thresholds */
    float lod_dist1;   /* distance beyond which LOD 1 is used */
    float lod_dist2;   /* distance beyond which LOD 2 is used */
    float skirt_depth; /* depth of crack-hiding skirts (0 = disabled) */
    /* Splat map: RGBA Pixels where R/G/B/A = weight for layers 0-3 */
    void *splat_map;
    void *layer_textures[TERRAIN_MAX_SPLAT_LAYERS];
    double layer_scales[TERRAIN_MAX_SPLAT_LAYERS]; /* UV tiling per layer */
} rt_terrain3d;

static void terrain3d_finalizer(void *obj) {
    rt_terrain3d *t = (rt_terrain3d *)obj;
    free(t->heights);
    free(t->chunk_meshes);
    free(t->chunk_meshes_lod1);
    free(t->chunk_meshes_lod2);
    free(t->chunk_aabbs);
    t->heights = NULL;
    t->chunk_meshes = NULL;
    t->chunk_meshes_lod1 = NULL;
    t->chunk_meshes_lod2 = NULL;
    t->chunk_aabbs = NULL;
}

/// @brief Invalidate all cached chunk meshes across all LOD levels.
static void invalidate_all_chunks(rt_terrain3d *t) {
    int32_t n = t->chunks_x * t->chunks_z;
    for (int32_t i = 0; i < n; i++) {
        t->chunk_meshes[i] = NULL;
        t->chunk_meshes_lod1[i] = NULL;
        t->chunk_meshes_lod2[i] = NULL;
    }
}

void *rt_terrain3d_new(int64_t width, int64_t depth) {
    if (width < 2 || depth < 2 || width > 4096 || depth > 4096) {
        rt_trap("Terrain3D.New: dimensions must be 2-4096");
        return NULL;
    }
    rt_terrain3d *t = (rt_terrain3d *)rt_obj_new_i64(0, (int64_t)sizeof(rt_terrain3d));
    if (!t) {
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
    int32_t num_chunks = t->chunks_x * t->chunks_z;
    t->chunk_meshes = (void **)calloc((size_t)num_chunks, sizeof(void *));
    t->chunk_meshes_lod1 = (void **)calloc((size_t)num_chunks, sizeof(void *));
    t->chunk_meshes_lod2 = (void **)calloc((size_t)num_chunks, sizeof(void *));
    t->chunk_aabbs = (float *)calloc((size_t)(num_chunks * 6), sizeof(float));
    t->lod_dist1 = 100.0f;
    t->lod_dist2 = 250.0f;
    t->skirt_depth = 2.0f;
    t->material = NULL;
    t->splat_map = NULL;
    for (int i = 0; i < TERRAIN_MAX_SPLAT_LAYERS; i++) {
        t->layer_textures[i] = NULL;
        t->layer_scales[i] = 1.0;
    }
    rt_obj_set_finalizer(t, terrain3d_finalizer);
    return t;
}

/// @brief Generate terrain heights directly from Perlin noise (fast native path).
/// Bypasses the Pixels intermediate — writes directly to float heightmap.
extern double rt_perlin_octave2d(
    void *obj, double x, double y, int64_t octaves, double persistence);

void rt_terrain3d_generate_perlin(
    void *obj, void *perlin, double scale, int64_t octaves, double persistence) {
    if (!obj || !perlin)
        return;
    rt_terrain3d *t = (rt_terrain3d *)obj;
    if (scale < 1e-8)
        scale = 1.0;

    for (int32_t z = 0; z < t->depth; z++) {
        for (int32_t x = 0; x < t->width; x++) {
            double nx = (double)x * scale / (double)t->width;
            double nz = (double)z * scale / (double)t->depth;
            double h = rt_perlin_octave2d(perlin, nx, nz, octaves, persistence);
            /* Map [-1, 1] -> [0, 1] */
            t->heights[z * t->width + x] = (float)((h + 1.0) * 0.5);
        }
    }

    invalidate_all_chunks(t);
}

void rt_terrain3d_set_heightmap(void *obj, void *pixels) {
    if (!obj || !pixels)
        return;
    rt_terrain3d *t = (rt_terrain3d *)obj;

    /* Access Pixels internal layout */
    typedef struct {
        int64_t w;
        int64_t h;
        uint32_t *data;
    } px_view;

    px_view *pv = (px_view *)pixels;
    if (!pv->data)
        return;

    int32_t sw = (int32_t)pv->w, sh = (int32_t)pv->h;
    for (int32_t z = 0; z < t->depth; z++) {
        for (int32_t x = 0; x < t->width; x++) {
            int sx = x * sw / t->width;
            int sz = z * sh / t->depth;
            if (sx >= sw)
                sx = sw - 1;
            if (sz >= sh)
                sz = sh - 1;
            uint32_t pixel = pv->data[sz * sw + sx]; /* 0xRRGGBBAA */
            /* 16-bit height from R (high byte) + G (low byte) for smooth terrain */
            uint32_t hi = (pixel >> 24) & 0xFF;
            uint32_t lo = (pixel >> 16) & 0xFF;
            t->heights[z * t->width + x] = (float)((hi << 8) | lo) / 65535.0f;
        }
    }

    invalidate_all_chunks(t);
}

void rt_terrain3d_set_material(void *obj, void *material) {
    if (obj)
        ((rt_terrain3d *)obj)->material = material;
}

void rt_terrain3d_set_scale(void *obj, double sx, double sy, double sz) {
    if (!obj)
        return;
    rt_terrain3d *t = (rt_terrain3d *)obj;
    t->scale[0] = sx;
    t->scale[1] = sy;
    t->scale[2] = sz;
    invalidate_all_chunks(t);
}

void rt_terrain3d_set_splat_map(void *obj, void *pixels) {
    if (!obj)
        return;
    rt_terrain3d *t = (rt_terrain3d *)obj;
    t->splat_map = pixels;
    invalidate_all_chunks(t);
}

void rt_terrain3d_set_layer_texture(void *obj, int64_t layer, void *pixels) {
    if (!obj || layer < 0 || layer >= TERRAIN_MAX_SPLAT_LAYERS)
        return;
    rt_terrain3d *t = (rt_terrain3d *)obj;
    t->layer_textures[layer] = pixels;
    invalidate_all_chunks(t);
}

void rt_terrain3d_set_layer_scale(void *obj, int64_t layer, double scale) {
    if (!obj || layer < 0 || layer >= TERRAIN_MAX_SPLAT_LAYERS)
        return;
    rt_terrain3d *t = (rt_terrain3d *)obj;
    t->layer_scales[layer] = scale;
    invalidate_all_chunks(t);
}

/// @brief Sample a pixel from a Pixels object at UV coordinates (clamped).
static uint32_t sample_pixels_uv(void *pixels, double u, double v) {
    if (!pixels)
        return 0xFFFFFFFF;

    typedef struct {
        int64_t w, h;
        uint32_t *data;
    } px_view;

    px_view *pv = (px_view *)pixels;
    if (!pv->data || pv->w == 0 || pv->h == 0)
        return 0xFFFFFFFF;
    /* Wrap UV */
    u = u - floor(u);
    v = v - floor(v);
    int32_t px = (int32_t)(u * pv->w);
    int32_t py = (int32_t)(v * pv->h);
    if (px >= pv->w)
        px = (int32_t)(pv->w - 1);
    if (py >= pv->h)
        py = (int32_t)(pv->h - 1);
    return pv->data[py * pv->w + px];
}

/// @brief Sample height at grid coordinates (clamped).
static float sample_height(const rt_terrain3d *t, int32_t x, int32_t z) {
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

double rt_terrain3d_get_height_at(void *obj, double wx, double wz) {
    if (!obj)
        return 0.0;
    rt_terrain3d *t = (rt_terrain3d *)obj;
    if (t->scale[0] < 1e-12 || t->scale[2] < 1e-12)
        return 0.0;

    double hx = wx / t->scale[0];
    double hz = wz / t->scale[2];
    int ix = (int)floor(hx), iz = (int)floor(hz);
    float fx = (float)(hx - ix), fz = (float)(hz - iz);

    if (ix < 0) {
        ix = 0;
        fx = 0;
    }
    if (iz < 0) {
        iz = 0;
        fz = 0;
    }
    if (ix >= t->width - 1) {
        ix = t->width - 2;
        fx = 1;
    }
    if (iz >= t->depth - 1) {
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

void *rt_terrain3d_get_normal_at(void *obj, double wx, double wz) {
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
    if (len > 1e-8) {
        nx /= len;
        ny /= len;
        nz /= len;
    }

    return rt_vec3_new(nx, ny, nz);
}

/// @brief Bake splat-blended texture onto the terrain material.
/// Generates a Pixels texture where each texel is the weighted blend of the 4
/// layer textures, sampled at their respective UV scales, weighted by the splat
/// map RGBA channels. Applied once when chunks are invalidated.
extern void *rt_pixels_new(int64_t width, int64_t height);
extern void rt_pixels_set(void *pixels, int64_t x, int64_t y, int64_t color);
extern void rt_material3d_set_texture(void *material, void *pixels);

static void bake_splat_texture(rt_terrain3d *t) {
    if (!t->splat_map || !t->material)
        return;

    typedef struct {
        int64_t w, h;
        uint32_t *data;
    } px_view;

    px_view *splat = (px_view *)t->splat_map;
    if (!splat->data || splat->w == 0 || splat->h == 0)
        return;

    /* Generate a blended texture at splat map resolution */
    int32_t tw = (int32_t)splat->w, th = (int32_t)splat->h;
    void *baked = rt_pixels_new(tw, th);
    if (!baked)
        return;

    for (int32_t y = 0; y < th; y++) {
        for (int32_t x = 0; x < tw; x++) {
            double u = (double)x / (double)(tw - 1);
            double v = (double)y / (double)(th - 1);

            /* Sample splat weights (RGBA → 4 layer weights) */
            uint32_t sp = splat->data[y * tw + x];
            double w0 = (double)((sp >> 24) & 0xFF) / 255.0;
            double w1 = (double)((sp >> 16) & 0xFF) / 255.0;
            double w2 = (double)((sp >> 8) & 0xFF) / 255.0;
            double w3 = (double)(sp & 0xFF) / 255.0;

            /* Normalize weights */
            double wsum = w0 + w1 + w2 + w3;
            if (wsum > 0.001) {
                w0 /= wsum;
                w1 /= wsum;
                w2 /= wsum;
                w3 /= wsum;
            } else {
                w0 = 1.0;
                w1 = w2 = w3 = 0.0;
            }

            /* Sample each layer texture at its UV scale and blend */
            double weights[4] = {w0, w1, w2, w3};
            double br = 0, bg = 0, bb = 0;
            for (int layer = 0; layer < TERRAIN_MAX_SPLAT_LAYERS; layer++) {
                if (weights[layer] < 0.001 || !t->layer_textures[layer])
                    continue;
                double lu = u * t->layer_scales[layer];
                double lv = v * t->layer_scales[layer];
                uint32_t lp = sample_pixels_uv(t->layer_textures[layer], lu, lv);
                double lr = (double)((lp >> 24) & 0xFF) / 255.0;
                double lg2 = (double)((lp >> 16) & 0xFF) / 255.0;
                double lb = (double)((lp >> 8) & 0xFF) / 255.0;
                br += lr * weights[layer];
                bg += lg2 * weights[layer];
                bb += lb * weights[layer];
            }

            int32_t cr = (int32_t)(br * 255);
            int32_t cg = (int32_t)(bg * 255);
            int32_t cb = (int32_t)(bb * 255);
            if (cr > 255)
                cr = 255;
            if (cg > 255)
                cg = 255;
            if (cb > 255)
                cb = 255;
            int64_t color = ((int64_t)cr << 16) | ((int64_t)cg << 8) | (int64_t)cb;
            rt_pixels_set(baked, x, y, color);
        }
    }

    rt_material3d_set_texture(t->material, baked);
}

/// @brief Compute per-vertex data at grid position (ix, iz).
static void terrain_vertex(rt_terrain3d *t,
                           int32_t ix,
                           int32_t iz,
                           double *wx,
                           double *wy,
                           double *wz,
                           double *nx,
                           double *ny,
                           double *nz_n,
                           double *u,
                           double *v) {
    *wx = (double)ix * t->scale[0];
    *wy = (double)sample_height(t, ix, iz) * t->scale[1];
    *wz = (double)iz * t->scale[2];
    float hL = sample_height(t, ix - 1, iz);
    float hR = sample_height(t, ix + 1, iz);
    float hD = sample_height(t, ix, iz - 1);
    float hU = sample_height(t, ix, iz + 1);
    *nx = (double)(hL - hR) * t->scale[1];
    *nz_n = (double)(hD - hU) * t->scale[1];
    *ny = 2.0 * t->scale[0];
    double nlen = sqrt(*nx * *nx + *ny * *ny + *nz_n * *nz_n);
    if (nlen > 1e-8) {
        *nx /= nlen;
        *ny /= nlen;
        *nz_n /= nlen;
    }
    *u = (double)ix / (double)(t->width - 1);
    *v = (double)iz / (double)(t->depth - 1);
}

/// @brief Build mesh for one terrain chunk at a given LOD step.
/// @param step 1=full res (LOD 0), 2=half (LOD 1), 4=quarter (LOD 2).
/// @param aabb_out If non-NULL, receives the chunk's AABB (6 floats: min[3], max[3]).
static void *build_chunk(rt_terrain3d *t, int32_t cx, int32_t cz, int32_t step, float *aabb_out) {
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

    /* Track AABB during vertex generation */
    float aabb_min[3] = {FLT_MAX, FLT_MAX, FLT_MAX};
    float aabb_max[3] = {-FLT_MAX, -FLT_MAX, -FLT_MAX};

    /* Vertices (with LOD step) */
    int32_t vert_cols = 0, vert_rows = 0;
    for (int32_t dz = 0; dz <= rows; dz += step) {
        vert_cols = 0;
        for (int32_t dx = 0; dx <= cols; dx += step) {
            int32_t ix = x0 + dx, iz = z0 + dz;
            /* Clamp to terrain bounds */
            if (ix >= t->width)
                ix = t->width - 1;
            if (iz >= t->depth)
                iz = t->depth - 1;

            double wx, wy, wz, nx, ny, nz_n, u, v;
            terrain_vertex(t, ix, iz, &wx, &wy, &wz, &nx, &ny, &nz_n, &u, &v);
            rt_mesh3d_add_vertex(mesh, wx, wy, wz, nx, ny, nz_n, u, v);

            /* Update AABB */
            if ((float)wx < aabb_min[0])
                aabb_min[0] = (float)wx;
            if ((float)wy < aabb_min[1])
                aabb_min[1] = (float)wy;
            if ((float)wz < aabb_min[2])
                aabb_min[2] = (float)wz;
            if ((float)wx > aabb_max[0])
                aabb_max[0] = (float)wx;
            if ((float)wy > aabb_max[1])
                aabb_max[1] = (float)wy;
            if ((float)wz > aabb_max[2])
                aabb_max[2] = (float)wz;

            vert_cols++;
        }
        vert_rows++;
    }

    /* Triangles (CCW winding) */
    for (int32_t rz = 0; rz < vert_rows - 1; rz++) {
        for (int32_t rx = 0; rx < vert_cols - 1; rx++) {
            int64_t base = (int64_t)(rz * vert_cols + rx);
            rt_mesh3d_add_triangle(mesh, base, base + vert_cols, base + 1);
            rt_mesh3d_add_triangle(mesh, base + 1, base + vert_cols, base + vert_cols + 1);
        }
    }

    /* Skirt geometry: extend edges downward to hide cracks between LOD levels */
    if (t->skirt_depth > 0.0f && step > 1) {
        double sd = (double)t->skirt_depth;
        /* For each of the 4 edges, add skirt triangles */
        /* Top edge (dz=0), bottom edge (dz=rows), left (dx=0), right (dx=cols) */
        int64_t skirt_base = (int64_t)(vert_rows * vert_cols);

        /* Top edge (z = z0) */
        for (int32_t rx = 0; rx < vert_cols; rx++) {
            int32_t ix = x0 + rx * step;
            if (ix >= t->width)
                ix = t->width - 1;
            double wx, wy, wz, nx, ny, nz_n, u, v;
            terrain_vertex(t, ix, z0, &wx, &wy, &wz, &nx, &ny, &nz_n, &u, &v);
            rt_mesh3d_add_vertex(mesh, wx, wy - sd, wz, 0, -1, 0, u, v);
        }
        for (int32_t rx = 0; rx < vert_cols - 1; rx++) {
            int64_t top = (int64_t)rx;              /* top edge vertex */
            int64_t bot = skirt_base + (int64_t)rx; /* skirt vertex */
            rt_mesh3d_add_triangle(mesh, top, bot, top + 1);
            rt_mesh3d_add_triangle(mesh, top + 1, bot, bot + 1);
        }
        skirt_base += vert_cols;

        /* Bottom edge (z = z0 + rows) */
        int64_t bottom_row_start = (int64_t)((vert_rows - 1) * vert_cols);
        for (int32_t rx = 0; rx < vert_cols; rx++) {
            int32_t ix = x0 + rx * step;
            int32_t iz = z0 + rows;
            if (ix >= t->width)
                ix = t->width - 1;
            if (iz >= t->depth)
                iz = t->depth - 1;
            double wx, wy, wz, nx, ny, nz_n, u, v;
            terrain_vertex(t, ix, iz, &wx, &wy, &wz, &nx, &ny, &nz_n, &u, &v);
            rt_mesh3d_add_vertex(mesh, wx, wy - sd, wz, 0, -1, 0, u, v);
        }
        for (int32_t rx = 0; rx < vert_cols - 1; rx++) {
            int64_t top = bottom_row_start + (int64_t)rx;
            int64_t bot = skirt_base + (int64_t)rx;
            rt_mesh3d_add_triangle(mesh, top, top + 1, bot);
            rt_mesh3d_add_triangle(mesh, top + 1, bot + 1, bot);
        }
        skirt_base += vert_cols;

        /* Left edge (x = x0) */
        for (int32_t rz = 0; rz < vert_rows; rz++) {
            int32_t iz = z0 + rz * step;
            if (iz >= t->depth)
                iz = t->depth - 1;
            double wx, wy, wz, nx, ny, nz_n, u, v;
            terrain_vertex(t, x0, iz, &wx, &wy, &wz, &nx, &ny, &nz_n, &u, &v);
            rt_mesh3d_add_vertex(mesh, wx, wy - sd, wz, -1, 0, 0, u, v);
        }
        for (int32_t rz = 0; rz < vert_rows - 1; rz++) {
            int64_t top = (int64_t)(rz * vert_cols); /* left column vertex */
            int64_t bot = skirt_base + (int64_t)rz;
            rt_mesh3d_add_triangle(mesh, top, bot, (int64_t)((rz + 1) * vert_cols));
            rt_mesh3d_add_triangle(mesh, (int64_t)((rz + 1) * vert_cols), bot, bot + 1);
        }
        skirt_base += vert_rows;

        /* Right edge (x = x0 + cols) */
        for (int32_t rz = 0; rz < vert_rows; rz++) {
            int32_t ix = x0 + cols;
            int32_t iz = z0 + rz * step;
            if (ix >= t->width)
                ix = t->width - 1;
            if (iz >= t->depth)
                iz = t->depth - 1;
            double wx, wy, wz, nx, ny, nz_n, u, v;
            terrain_vertex(t, ix, iz, &wx, &wy, &wz, &nx, &ny, &nz_n, &u, &v);
            rt_mesh3d_add_vertex(mesh, wx, wy - sd, wz, 1, 0, 0, u, v);
        }
        for (int32_t rz = 0; rz < vert_rows - 1; rz++) {
            int64_t top = (int64_t)(rz * vert_cols + vert_cols - 1);
            int64_t bot = skirt_base + (int64_t)rz;
            rt_mesh3d_add_triangle(mesh, top, (int64_t)((rz + 1) * vert_cols + vert_cols - 1), bot);
            rt_mesh3d_add_triangle(
                mesh, (int64_t)((rz + 1) * vert_cols + vert_cols - 1), bot + 1, bot);
        }
    }

    /* Output AABB */
    if (aabb_out) {
        aabb_out[0] = aabb_min[0];
        aabb_out[1] = aabb_min[1];
        aabb_out[2] = aabb_min[2];
        aabb_out[3] = aabb_max[0];
        aabb_out[4] = aabb_max[1];
        aabb_out[5] = aabb_max[2];
    }

    return mesh;
}

void rt_terrain3d_set_lod_distances(void *obj, double near_dist, double far_dist) {
    if (!obj)
        return;
    rt_terrain3d *t = (rt_terrain3d *)obj;
    t->lod_dist1 = (float)near_dist;
    t->lod_dist2 = (float)far_dist;
}

void rt_terrain3d_set_skirt_depth(void *obj, double depth) {
    if (!obj)
        return;
    rt_terrain3d *t = (rt_terrain3d *)obj;
    t->skirt_depth = (float)depth;
    invalidate_all_chunks(t);
}

void rt_canvas3d_draw_terrain(void *canvas_obj, void *terrain_obj) {
    if (!canvas_obj || !terrain_obj)
        return;
    rt_canvas3d *c = (rt_canvas3d *)canvas_obj;
    rt_terrain3d *t = (rt_terrain3d *)terrain_obj;
    if (!c->in_frame || !c->backend || !t->material)
        return;

    /* Bake splat-blended texture if splat map is set and chunks are invalid */
    if (t->splat_map) {
        int any_invalid = 0;
        for (int32_t i = 0; i < t->chunks_x * t->chunks_z && !any_invalid; i++)
            if (!t->chunk_meshes[i])
                any_invalid = 1;
        if (any_invalid)
            bake_splat_texture(t);
    }

    /* Extract frustum from cached VP matrix for culling */
    vgfx3d_frustum_t frustum;
    vgfx3d_frustum_extract(&frustum, c->cached_vp);

    void *identity = rt_mat4_identity();

    for (int32_t cz = 0; cz < t->chunks_z; cz++) {
        for (int32_t cx = 0; cx < t->chunks_x; cx++) {
            int32_t idx = cz * t->chunks_x + cx;

            /* Ensure LOD 0 mesh + AABB are built (AABB computed from LOD 0) */
            if (!t->chunk_meshes[idx])
                t->chunk_meshes[idx] = build_chunk(t, cx, cz, 1, &t->chunk_aabbs[idx * 6]);

            /* Phase A: Frustum culling */
            float *aabb = &t->chunk_aabbs[idx * 6];
            if (vgfx3d_frustum_test_aabb(&frustum, aabb, aabb + 3) == 0)
                continue;

            /* Phase B: LOD selection based on distance to camera */
            float chunk_cx = (aabb[0] + aabb[3]) * 0.5f;
            float chunk_cz = (aabb[2] + aabb[5]) * 0.5f;
            float dx = chunk_cx - c->cached_cam_pos[0];
            float dz = chunk_cz - c->cached_cam_pos[2];
            float dist = sqrtf(dx * dx + dz * dz);

            void *draw_mesh = NULL;
            if (dist >= t->lod_dist2) {
                /* LOD 2: quarter resolution */
                if (!t->chunk_meshes_lod2[idx])
                    t->chunk_meshes_lod2[idx] = build_chunk(t, cx, cz, 4, NULL);
                draw_mesh = t->chunk_meshes_lod2[idx];
            } else if (dist >= t->lod_dist1) {
                /* LOD 1: half resolution */
                if (!t->chunk_meshes_lod1[idx])
                    t->chunk_meshes_lod1[idx] = build_chunk(t, cx, cz, 2, NULL);
                draw_mesh = t->chunk_meshes_lod1[idx];
            } else {
                /* LOD 0: full resolution */
                draw_mesh = t->chunk_meshes[idx];
            }

            if (draw_mesh) {
                /* Set pending splat data for per-pixel terrain splatting */
                if (t->splat_map) {
                    c->pending_has_splat = 1;
                    c->pending_splat_map = t->splat_map;
                    for (int si = 0; si < 4; si++) {
                        c->pending_splat_layers[si] = t->layer_textures[si];
                        c->pending_splat_layer_scales[si] = (float)t->layer_scales[si];
                    }
                }
                rt_canvas3d_draw_mesh(canvas_obj, draw_mesh, identity, t->material);
            }
        }
    }
}

#else
typedef int rt_graphics_disabled_tu_guard;
#endif /* VIPER_ENABLE_GRAPHICS */
