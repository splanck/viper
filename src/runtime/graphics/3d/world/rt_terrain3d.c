//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/world/rt_terrain3d.c
// Purpose: Heightmap terrain — chunked mesh generation, bilinear height/normal
//   queries, frustum-culled per-chunk rendering.
//
// Key invariants:
//   - Heights are float[width*depth], sampled from Pixels red channel.
//   - Chunks are TERRAIN_CHUNK_SIZE quads per edge (16x16 = 256 quads each).
//   - Mesh generation is lazy (built on first draw, invalidated on heightmap change).
//   - Normals computed via central difference on height grid.
//   - Three LOD levels keyed by camera distance (full / step=2 / step=4).
//   - Splat-map composites up to 4 layer textures into a baked diffuse.
//
// Ownership/Lifetime:
//   - Terrain3D is GC-managed; finalizer releases all per-chunk LOD meshes,
//     the material, splat map, layer textures, and base + baked textures.
//
// Links: rt_terrain3d.h, rt_mesh3d, vgfx3d_frustum.h
//
//===----------------------------------------------------------------------===//

#ifdef VIPER_ENABLE_GRAPHICS

#include "rt_terrain3d.h"
#include "rt_world3d_common.h"
#include "rt_canvas3d.h"
#include "rt_canvas3d_internal.h"
#include "rt_graphics3d_ids.h"
#include "rt_pixels_internal.h"

#include "vgfx3d_frustum.h"

#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

extern void *rt_obj_new_i64(int64_t class_id, int64_t byte_size);
extern void rt_obj_set_finalizer(void *obj, void (*fn)(void *));
extern void rt_obj_retain_maybe(void *obj);
extern int32_t rt_obj_release_check0(void *obj);
extern void rt_obj_free(void *obj);
#include "rt_trap.h"
extern void *rt_vec3_new(double x, double y, double z);
extern void *rt_mesh3d_new(void);
extern void rt_mesh3d_add_vertex(
    void *m, double x, double y, double z, double nx, double ny, double nz, double u, double v);
extern void rt_mesh3d_add_triangle(void *m, int64_t v0, int64_t v1, int64_t v2);
extern void rt_canvas3d_draw_mesh(void *canvas, void *mesh, void *transform, void *material);
extern void rt_canvas3d_draw_mesh_matrix(void *canvas,
                                         void *mesh,
                                         const double *transform,
                                         void *material);
extern void rt_material3d_set_texture(void *material, void *pixels);
extern void *rt_pixels_new(int64_t width, int64_t height);
extern void rt_pixels_set(void *pixels, int64_t x, int64_t y, int64_t color);

#define TERRAIN_CHUNK_SIZE 16
#define TERRAIN3D_ABS_MAX 1000000.0
#define TERRAIN3D_MAX_DIM 4096
#define TERRAIN3D_MAX_CHUNKS 65536

#define TERRAIN_MAX_SPLAT_LAYERS 4
#define TERRAIN_LOD_LEVELS 3

typedef struct {
    void *vptr;
    float *heights;
    int32_t width, depth;
    int64_t height_count;
    double scale[3];          /* x_spacing, y_scale, z_spacing */
    void **chunk_meshes;      /* LOD 0 (full res) mesh cache */
    void **chunk_meshes_lod1; /* LOD 1 (step=2) mesh cache */
    void **chunk_meshes_lod2; /* LOD 2 (step=4) mesh cache */
    float *chunk_aabbs;       /* 6 floats per chunk: min[3], max[3] */
    int32_t chunks_x, chunks_z;
    int32_t chunk_capacity;
    void *material;
    /* LOD distance thresholds */
    float lod_dist1;   /* distance beyond which LOD 1 is used */
    float lod_dist2;   /* distance beyond which LOD 2 is used */
    float skirt_depth; /* depth of crack-hiding skirts (0 = disabled) */
    /* Splat map: RGBA Pixels where R/G/B/A = weight for layers 0-3 */
    void *splat_map;
    void *layer_textures[TERRAIN_MAX_SPLAT_LAYERS];
    double layer_scales[TERRAIN_MAX_SPLAT_LAYERS]; /* UV tiling per layer */
    void *base_texture;
    void *baked_texture;
    int8_t splat_dirty;
} rt_terrain3d;

/// @brief Release and null a GC-tracked reference slot.
/// @details Shared plumbing for the terrain's texture slots (`base_texture`,
///   `baked_texture`, splat layer textures) and LOD mesh grids. Idempotent on null.
static void terrain_release_ref(void **slot) {
    if (!slot || !*slot)
        return;
    if (rt_obj_release_check0(*slot))
        rt_obj_free(*slot);
    *slot = NULL;
}

/// @brief True if @p ref is a usable terrain texture reference (a Pixels image or a TextureAsset3D).
static int terrain_texture_ref_supported(void *ref) {
    return ref && (rt_pixels_checked_impl_or_null(ref) ||
                   rt_g3d_has_class(ref, RT_G3D_TEXTUREASSET3D_CLASS_ID));
}

/// @brief Release a retained texture slot only if it still holds a supported texture;
///   otherwise just clear the slot (the stale value is not a releasable handle).
static void terrain_release_texture_slot(void **slot) {
    if (!slot || !*slot)
        return;
    if (!terrain_texture_ref_supported(*slot)) {
        *slot = NULL;
        return;
    }
    terrain_release_ref(slot);
}

/// @brief Clear a texture slot in place when its contents are no longer a supported texture
///   (defensive; performs no release).
static void terrain_repair_texture_slot(void **slot) {
    if (slot && *slot && !terrain_texture_ref_supported(*slot))
        *slot = NULL;
}

/// @brief Release a retained material slot only if it still holds a Material3D; otherwise
///   just clear the slot.
static void terrain_release_material_slot(void **slot) {
    if (!slot || !*slot)
        return;
    if (!rt_g3d_has_class(*slot, RT_G3D_MATERIAL3D_CLASS_ID)) {
        *slot = NULL;
        return;
    }
    terrain_release_ref(slot);
}

/// @brief Clear a material slot in place when its contents are no longer a Material3D
///   (defensive; performs no release).
static void terrain_repair_material_slot(void **slot) {
    if (slot && *slot && !rt_g3d_has_class(*slot, RT_G3D_MATERIAL3D_CLASS_ID))
        *slot = NULL;
}

/// @brief Release a retained mesh slot only if it still holds a Mesh3D; otherwise just clear it.
static void terrain_release_mesh_slot(void **slot) {
    if (!slot || !*slot)
        return;
    if (!rt_g3d_has_class(*slot, RT_G3D_MESH3D_CLASS_ID)) {
        *slot = NULL;
        return;
    }
    terrain_release_ref(slot);
}

/// @brief Clamp `value` into `[-TERRAIN3D_ABS_MAX, TERRAIN3D_ABS_MAX]`, substituting `fallback`
/// when not finite.
static double terrain_clamp_abs_or(double value, double fallback) {
    return rt_world3d_clamp_abs_or(value, fallback, TERRAIN3D_ABS_MAX);
}

/// @brief Return a finite terrain scale axis, repairing degenerate X/Z spacing to fallback.
static double terrain_scale_axis_or(const rt_terrain3d *t, int axis, double fallback) {
    double value;
    if (!t || axis < 0 || axis > 2)
        return fallback;
    value = t->scale[axis];
    if (!isfinite(value))
        return fallback;
    if ((axis == 0 || axis == 2) && fabs(value) < 1e-12)
        return fallback;
    return terrain_clamp_abs_or(value, fallback);
}

/// @brief Repair a private/corrupt height sample before it enters queries or generated geometry.
static float terrain_sanitize_height_sample(float value) {
    if (!isfinite(value))
        return 0.0f;
    if (value > (float)TERRAIN3D_ABS_MAX)
        return (float)TERRAIN3D_ABS_MAX;
    if (value < (float)-TERRAIN3D_ABS_MAX)
        return (float)-TERRAIN3D_ABS_MAX;
    return value;
}

/// @brief Return a safe skirt depth for mesh generation.
static float terrain_skirt_depth_or(const rt_terrain3d *t) {
    double depth = t ? (double)t->skirt_depth : 0.0;
    if (!isfinite(depth) || depth <= 0.0)
        return 0.0f;
    if (depth > 1000.0)
        depth = 1000.0;
    return (float)depth;
}

/// @brief Return a safe positive UV tiling scale for a splat layer.
static double terrain_layer_scale_or(const rt_terrain3d *t, int layer) {
    double scale;
    if (!t || layer < 0 || layer >= TERRAIN_MAX_SPLAT_LAYERS)
        return 1.0;
    scale = t->layer_scales[layer];
    if (!isfinite(scale) || scale <= 0.0)
        return 1.0;
    if (scale > TERRAIN3D_ABS_MAX)
        return TERRAIN3D_ABS_MAX;
    return scale;
}

/// @brief Read LOD thresholds, repairing private corruption without mutating the terrain object.
static void terrain_lod_distances_or(const rt_terrain3d *t, float *out_near, float *out_far) {
    double near_dist = t ? (double)t->lod_dist1 : 100.0;
    double far_dist = t ? (double)t->lod_dist2 : 250.0;

    if (!isfinite(near_dist) || near_dist < 0.0)
        near_dist = 50.0;
    if (near_dist > TERRAIN3D_ABS_MAX)
        near_dist = TERRAIN3D_ABS_MAX;
    if (!isfinite(far_dist) || far_dist <= near_dist)
        far_dist = near_dist + 100.0;
    if (far_dist > TERRAIN3D_ABS_MAX)
        far_dist = TERRAIN3D_ABS_MAX;
    if (far_dist <= near_dist) {
        far_dist = near_dist + 1.0;
        if (far_dist > TERRAIN3D_ABS_MAX) {
            far_dist = TERRAIN3D_ABS_MAX;
            near_dist = far_dist - 1.0;
            if (near_dist < 0.0)
                near_dist = 0.0;
        }
    }
    if (out_near)
        *out_near = (float)near_dist;
    if (out_far)
        *out_far = (float)far_dist;
}

/// @brief Floor a continuous heightfield coordinate to a grid index, clamped to
///        `[0, max_index]`. Non-finite or non-positive input maps to 0.
static int32_t terrain_coord_to_index(double coord, int32_t max_index) {
    if (!isfinite(coord))
        return 0;
    if (coord <= 0.0)
        return 0;
    if (coord >= (double)max_index)
        return max_index;
    return (int32_t)floor(coord);
}

/// @brief Map destination sample index to source index for nearest-neighbor resampling.
static int32_t terrain_resample_index(int32_t dst, int32_t dst_count, int32_t src_count) {
    int64_t numerator;
    int64_t denominator;
    int64_t value;
    if (dst_count <= 1 || src_count <= 1 || dst <= 0)
        return 0;
    if (dst >= dst_count - 1)
        return src_count - 1;
    numerator = (int64_t)dst * (int64_t)(src_count - 1);
    denominator = (int64_t)(dst_count - 1);
    value = (numerator + denominator / 2) / denominator;
    if (value < 0)
        return 0;
    if (value >= src_count)
        return src_count - 1;
    return (int32_t)value;
}

/// @brief Assign @p value into a texture slot, retaining the new value and releasing the old
///   (no-op when unchanged).
static void terrain_assign_texture_ref(void **slot, void *value) {
    if (!slot || *slot == value)
        return;
    rt_obj_retain_maybe(value);
    terrain_release_texture_slot(slot);
    *slot = value;
}

/// @brief Assign @p value into a material slot, retaining the new value and releasing the old
///   (no-op when unchanged).
static void terrain_assign_material_ref(void **slot, void *value) {
    if (!slot || *slot == value)
        return;
    rt_obj_retain_maybe(value);
    terrain_release_material_slot(slot);
    *slot = value;
}

/// @brief Re-bind the terrain's base texture onto its material's albedo slot, first dropping a
///   stale (non-Material3D) material or an unsupported base-texture reference.
static void terrain_restore_material_base_texture(rt_terrain3d *t) {
    void *base_texture;
    if (!t || !t->material)
        return;
    if (!rt_g3d_has_class(t->material, RT_G3D_MATERIAL3D_CLASS_ID)) {
        terrain_release_material_slot(&t->material);
        return;
    }
    base_texture = terrain_texture_ref_supported(t->base_texture) ? t->base_texture : NULL;
    if (!base_texture)
        terrain_release_texture_slot(&t->base_texture);
    rt_material3d_set_texture(t->material, base_texture);
}

/// @brief True when width/depth still fit inside the allocated height buffer.
static int terrain_has_valid_grid(const rt_terrain3d *t) {
    int64_t needed;
    if (!t || !t->heights || t->width < 2 || t->depth < 2 || t->height_count <= 0)
        return 0;
    if ((int64_t)t->width > INT64_MAX / (int64_t)t->depth)
        return 0;
    needed = (int64_t)t->width * (int64_t)t->depth;
    return needed > 0 && needed <= t->height_count;
}

/// @brief Clamp a terrain chunk grid to the number of slots actually allocated.
static int32_t terrain_safe_chunk_count(const rt_terrain3d *t) {
    int64_t product;
    if (!t || t->chunk_capacity <= 0)
        return 0;
    if (t->chunk_capacity > TERRAIN3D_MAX_CHUNKS)
        return 0;
    if (t->chunks_x <= 0 || t->chunks_z <= 0)
        return 0;
    product = (int64_t)t->chunks_x * (int64_t)t->chunks_z;
    if (product <= 0)
        return t->chunk_capacity;
    if (product > t->chunk_capacity)
        return t->chunk_capacity;
    return (int32_t)product;
}

/// @brief True when all chunk-side arrays can be indexed by chunks_x * chunks_z.
static int terrain_has_valid_chunk_grid(const rt_terrain3d *t) {
    int64_t product;
    if (!t || !t->chunk_meshes || !t->chunk_meshes_lod1 || !t->chunk_meshes_lod2 ||
        !t->chunk_aabbs || t->chunk_capacity <= 0 || t->chunks_x <= 0 || t->chunks_z <= 0)
        return 0;
    if (t->chunk_capacity > TERRAIN3D_MAX_CHUNKS)
        return 0;
    product = (int64_t)t->chunks_x * (int64_t)t->chunks_z;
    return product > 0 && product <= t->chunk_capacity;
}

/// @brief Release every LOD mesh stored in the chunk grid.
/// @details Each of the three LOD arrays (`chunk_meshes`, `chunk_meshes_lod1`,
///   `chunk_meshes_lod2`) is a flat `chunks_x * chunks_z` array of optional
///   mesh slots. A null LOD array means that LOD has never been populated
///   and is simply skipped. Individual slot nulls inside a populated array
///   are handled by `terrain_release_ref`'s internal guard, so partial
///   grids (only some chunks meshed) are safe.
static void terrain_release_chunk_meshes(rt_terrain3d *t) {
    int32_t n;
    if (!t)
        return;
    n = terrain_safe_chunk_count(t);
    for (int32_t i = 0; i < n; i++) {
        if (t->chunk_meshes)
            terrain_release_mesh_slot(&t->chunk_meshes[i]);
        if (t->chunk_meshes_lod1)
            terrain_release_mesh_slot(&t->chunk_meshes_lod1[i]);
        if (t->chunk_meshes_lod2)
            terrain_release_mesh_slot(&t->chunk_meshes_lod2[i]);
    }
}

/// @brief GC finalizer — release every owned resource on a terrain.
/// @details Tears down, in order: per-chunk mesh slots (all three LODs),
///   the material reference, the splat map, each of the four layer
///   textures, the original base texture and the baked splat composite,
///   then frees the heightmap + chunk mesh / AABB arrays. Every slot is
///   nulled so a subsequent sweep (possible during shutdown) becomes a
///   no-op rather than a double-free. Order matters only loosely — meshes
///   are released before the arrays that hold them, but nothing here has
///   cross-allocation data dependencies.
static void terrain3d_finalizer(void *obj) {
    rt_terrain3d *t = (rt_terrain3d *)obj;
    if (!t)
        return;
    terrain_release_chunk_meshes(t);
    terrain_release_material_slot(&t->material);
    terrain_release_texture_slot(&t->splat_map);
    for (int i = 0; i < TERRAIN_MAX_SPLAT_LAYERS; i++)
        terrain_release_texture_slot(&t->layer_textures[i]);
    terrain_release_texture_slot(&t->base_texture);
    terrain_release_texture_slot(&t->baked_texture);
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
    t->height_count = 0;
    t->chunk_capacity = 0;
}

/// @brief Invalidate all cached chunk meshes across all LOD levels.
static void invalidate_all_chunks(rt_terrain3d *t) {
    int32_t n;
    if (!t)
        return;
    n = terrain_safe_chunk_count(t);
    for (int32_t i = 0; i < n; i++) {
        if (t->chunk_meshes)
            terrain_release_mesh_slot(&t->chunk_meshes[i]);
        if (t->chunk_meshes_lod1)
            terrain_release_mesh_slot(&t->chunk_meshes_lod1[i]);
        if (t->chunk_meshes_lod2)
            terrain_release_mesh_slot(&t->chunk_meshes_lod2[i]);
    }
}

/// @brief Construct a `width × depth` heightmap terrain (heights initially zero). Allocates
/// chunked mesh caches at three LOD levels plus per-chunk AABB storage. Default scale 1×1×1,
/// LOD switches at 100/250 world units, 2-unit crack-hiding skirts. Traps if dimensions are
/// outside [2, 4096] or on allocation failure.
void *rt_terrain3d_new(int64_t width, int64_t depth) {
    size_t height_count;
    size_t chunk_count;
    size_t aabb_count;
    if (width < 2 || depth < 2 || width > TERRAIN3D_MAX_DIM || depth > TERRAIN3D_MAX_DIM) {
        rt_trap("Terrain3D.New: dimensions must be 2-4096");
        return NULL;
    }
    if ((uint64_t)width > SIZE_MAX / (uint64_t)depth / sizeof(float)) {
        rt_trap("Terrain3D.New: height allocation overflow");
        return NULL;
    }
    height_count = (size_t)width * (size_t)depth;
    rt_terrain3d *t =
        (rt_terrain3d *)rt_obj_new_i64(RT_G3D_TERRAIN3D_CLASS_ID, (int64_t)sizeof(rt_terrain3d));
    if (!t) {
        rt_trap("Terrain3D.New: allocation failed");
        return NULL;
    }
    memset(t, 0, sizeof(*t));
    t->vptr = NULL;
    t->width = (int32_t)width;
    t->depth = (int32_t)depth;
    t->height_count = (int64_t)height_count;
    t->heights = (float *)calloc(height_count, sizeof(float));
    t->scale[0] = 1.0;
    t->scale[1] = 1.0;
    t->scale[2] = 1.0;
    t->chunks_x = ((int32_t)width - 1 + TERRAIN_CHUNK_SIZE - 1) / TERRAIN_CHUNK_SIZE;
    t->chunks_z = ((int32_t)depth - 1 + TERRAIN_CHUNK_SIZE - 1) / TERRAIN_CHUNK_SIZE;
    int32_t num_chunks = t->chunks_x * t->chunks_z;
    if (num_chunks <= 0 || (size_t)t->chunks_x > SIZE_MAX / (size_t)t->chunks_z ||
        (size_t)num_chunks > SIZE_MAX / 6u) {
        terrain3d_finalizer(t);
        if (rt_obj_release_check0(t))
            rt_obj_free(t);
        rt_trap("Terrain3D.New: chunk allocation overflow");
        return NULL;
    }
    t->chunk_capacity = num_chunks;
    chunk_count = (size_t)num_chunks;
    aabb_count = chunk_count * 6u;
    t->chunk_meshes = (void **)calloc(chunk_count, sizeof(void *));
    t->chunk_meshes_lod1 = (void **)calloc(chunk_count, sizeof(void *));
    t->chunk_meshes_lod2 = (void **)calloc(chunk_count, sizeof(void *));
    t->chunk_aabbs = (float *)calloc(aabb_count, sizeof(float));
    if (!t->heights || !t->chunk_meshes || !t->chunk_meshes_lod1 || !t->chunk_meshes_lod2 ||
        !t->chunk_aabbs) {
        terrain3d_finalizer(t);
        if (rt_obj_release_check0(t))
            rt_obj_free(t);
        rt_trap("Terrain3D.New: allocation failed");
        return NULL;
    }
    t->lod_dist1 = 100.0f;
    t->lod_dist2 = 250.0f;
    t->skirt_depth = 2.0f;
    t->material = NULL;
    t->splat_map = NULL;
    t->base_texture = NULL;
    t->baked_texture = NULL;
    t->splat_dirty = 0;
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

/// @brief Fill heights directly from a Perlin noise object (octave fractal sum), bypassing the
/// Pixels intermediate. Output values are mapped from [-1, 1] to [0, 1]. `scale` controls the
/// noise frequency (higher = more detail). Invalidates all cached chunk meshes.
void rt_terrain3d_generate_perlin(
    void *obj, void *perlin, double scale, int64_t octaves, double persistence) {
    rt_terrain3d *t = (rt_terrain3d *)rt_g3d_checked_or_null(obj, RT_G3D_TERRAIN3D_CLASS_ID);
    if (!t || !perlin || !terrain_has_valid_grid(t))
        return;
    if (!isfinite(scale) || scale < 1e-8)
        scale = 1.0;
    if (octaves < 1)
        octaves = 1;
    if (octaves > 16)
        octaves = 16;
    if (!isfinite(persistence))
        persistence = 0.5;
    if (persistence < 0.0)
        persistence = 0.0;
    if (persistence > 1.0)
        persistence = 1.0;

    for (int32_t z = 0; z < t->depth; z++) {
        for (int32_t x = 0; x < t->width; x++) {
            double nx = (double)x * scale / (double)t->width;
            double nz = (double)z * scale / (double)t->depth;
            double h = rt_perlin_octave2d(perlin, nx, nz, octaves, persistence);
            if (!isfinite(h))
                h = 0.0;
            /* Map [-1, 1] -> [0, 1] and clamp malformed generators. */
            h = (h + 1.0) * 0.5;
            if (h < 0.0)
                h = 0.0;
            if (h > 1.0)
                h = 1.0;
            t->heights[z * t->width + x] = (float)h;
        }
    }

    invalidate_all_chunks(t);
}

/// @brief Resample a Pixels heightmap into the terrain's height grid. Reads 16-bit precision per
/// sample (R = high byte, G = low byte) for smooth gradients without staircasing. Source pixels
/// are nearest-neighbor sampled to the terrain resolution. Invalidates all cached chunk meshes.
void rt_terrain3d_set_heightmap(void *obj, void *pixels) {
    if (!obj || !pixels)
        return;
    rt_terrain3d *t = (rt_terrain3d *)rt_g3d_checked_or_null(obj, RT_G3D_TERRAIN3D_CLASS_ID);
    if (!t || !terrain_has_valid_grid(t))
        return;

    rt_pixels_impl *pv = rt_pixels_checked_impl(pixels, "Terrain3D.SetHeightmap: expected Pixels");
    if (!pv || !pv->data || pv->width <= 0 || pv->height <= 0 || pv->width > INT32_MAX ||
        pv->height > INT32_MAX) {
        rt_trap("Terrain3D.SetHeightmap: heightmap must be non-empty Pixels");
        return;
    }

    int32_t sw = (int32_t)pv->width, sh = (int32_t)pv->height;
    for (int32_t z = 0; z < t->depth; z++) {
        int32_t sz = terrain_resample_index(z, t->depth, sh);
        for (int32_t x = 0; x < t->width; x++) {
            int32_t sx = terrain_resample_index(x, t->width, sw);
            uint32_t pixel = pv->data[(int64_t)sz * sw + sx]; /* 0xRRGGBBAA */
            /* 16-bit height from R (high byte) + G (low byte) for smooth terrain */
            uint32_t hi = (pixel >> 24) & 0xFF;
            uint32_t lo = (pixel >> 16) & 0xFF;
            t->heights[z * t->width + x] = (float)((hi << 8) | lo) / 65535.0f;
        }
    }

    invalidate_all_chunks(t);
}

/// @brief Attach a Material3D used when rendering chunks. Required before draw — the chunks are
/// rendered with this material, optionally overridden by the splat-bake texture if a splat map
/// is set. Does not invalidate chunks (the mesh data is independent of the material).
void rt_terrain3d_set_material(void *obj, void *material) {
    if (!obj)
        return;
    rt_terrain3d *t = (rt_terrain3d *)rt_g3d_checked_or_null(obj, RT_G3D_TERRAIN3D_CLASS_ID);
    if (!t)
        return;
    terrain_repair_material_slot(&t->material);
    if (material && !rt_g3d_has_class(material, RT_G3D_MATERIAL3D_CLASS_ID))
        return;
    if (t->material == material)
        return;
    if (t->material && (t->base_texture || t->baked_texture || t->splat_map))
        terrain_restore_material_base_texture(t);
    terrain_assign_material_ref(&t->material, material);
    terrain_release_texture_slot(&t->base_texture);
    terrain_release_texture_slot(&t->baked_texture);
    t->splat_dirty = t->splat_map ? 1 : 0;
}

/// @brief Set per-axis world-space scale: `sx` and `sz` are grid-cell spacing in world units;
/// `sy` is the height multiplier (heights in [0,1] become world-Y in [0, sy]). Invalidates
/// cached chunks since vertex world positions change.
void rt_terrain3d_set_scale(void *obj, double sx, double sy, double sz) {
    if (!obj)
        return;
    rt_terrain3d *t = (rt_terrain3d *)rt_g3d_checked_or_null(obj, RT_G3D_TERRAIN3D_CLASS_ID);
    if (!t)
        return;
    if (!isfinite(sx) || sx <= 0.0)
        sx = 1.0;
    if (!isfinite(sy))
        sy = 1.0;
    if (!isfinite(sz) || sz <= 0.0)
        sz = 1.0;
    sx = terrain_clamp_abs_or(sx, 1.0);
    sy = terrain_clamp_abs_or(sy, 1.0);
    sz = terrain_clamp_abs_or(sz, 1.0);
    if (t->scale[0] == sx && t->scale[1] == sy && t->scale[2] == sz)
        return;
    t->scale[0] = sx;
    t->scale[1] = sy;
    t->scale[2] = sz;
    invalidate_all_chunks(t);
}

/// @brief Attach a splat-weight Pixels map. Each pixel's RGBA channels are the per-texel weights
/// (normalized at bake time) for layers 0..3. Marks the splat bake dirty; mesh geometry is
/// independent of the splat data and remains cached.
void rt_terrain3d_set_splat_map(void *obj, void *pixels) {
    if (!obj)
        return;
    rt_terrain3d *t = (rt_terrain3d *)rt_g3d_checked_or_null(obj, RT_G3D_TERRAIN3D_CLASS_ID);
    if (!t)
        return;
    terrain_repair_texture_slot(&t->splat_map);
    if (pixels) {
        rt_pixels_impl *p =
            rt_pixels_checked_impl(pixels, "Terrain3D.SetSplatMap: expected Pixels");
        if (!p || p->width <= 0 || p->height <= 0 || p->width > INT32_MAX ||
            p->height > INT32_MAX || !p->data) {
            rt_trap("Terrain3D.SetSplatMap: splat map must be non-empty Pixels");
            return;
        }
    }
    terrain_assign_texture_ref(&t->splat_map, pixels);
    if (!pixels) {
        void *base_texture = terrain_texture_ref_supported(t->base_texture) ? t->base_texture : NULL;
        if (!base_texture)
            terrain_release_texture_slot(&t->base_texture);
        terrain_restore_material_base_texture(t);
        terrain_release_texture_slot(&t->baked_texture);
        t->splat_dirty = 0;
    } else {
        t->splat_dirty = 1;
    }
}

/// @brief Set the texture for a splat layer (0..3). Layer index outside that range is silently
/// ignored. Marks the splat bake dirty without invalidating geometry caches.
void rt_terrain3d_set_layer_texture(void *obj, int64_t layer, void *pixels) {
    if (!obj || layer < 0 || layer >= TERRAIN_MAX_SPLAT_LAYERS)
        return;
    rt_terrain3d *t = (rt_terrain3d *)rt_g3d_checked_or_null(obj, RT_G3D_TERRAIN3D_CLASS_ID);
    if (!t)
        return;
    terrain_repair_texture_slot(&t->layer_textures[layer]);
    if (pixels && rt_g3d_has_class(pixels, RT_G3D_TEXTUREASSET3D_CLASS_ID)) {
        void *resolved_pixels = rt_material3d_resolve_texture_pixels(pixels);
        rt_pixels_impl *p = rt_pixels_checked_impl_or_null(resolved_pixels);
        if (!p || p->width <= 0 || p->height <= 0 || p->width > INT32_MAX ||
            p->height > INT32_MAX || !p->data) {
            rt_trap("Terrain3D.SetLayerTexture: TextureAsset3D splat layers must expose a "
                    "non-empty RGBA8 Pixels fallback");
            return;
        }
    } else if (pixels) {
        rt_pixels_impl *p =
            rt_pixels_checked_impl(pixels, "Terrain3D.SetLayerTexture: expected Pixels");
        if (!p || p->width <= 0 || p->height <= 0 || p->width > INT32_MAX ||
            p->height > INT32_MAX || !p->data) {
            rt_trap("Terrain3D.SetLayerTexture: layer texture must be non-empty Pixels or "
                    "TextureAsset3D");
            return;
        }
    }
    terrain_assign_texture_ref(&t->layer_textures[layer], pixels);
    t->splat_dirty = 1;
}

/// @brief Set UV-tiling scale for splat layer N. Higher values pack the layer texture into
/// smaller tiles (more repetitions across the terrain). Marks the splat bake dirty when changed.
void rt_terrain3d_set_layer_scale(void *obj, int64_t layer, double scale) {
    if (!obj || layer < 0 || layer >= TERRAIN_MAX_SPLAT_LAYERS)
        return;
    rt_terrain3d *t = (rt_terrain3d *)rt_g3d_checked_or_null(obj, RT_G3D_TERRAIN3D_CLASS_ID);
    if (!t)
        return;
    if (!isfinite(scale) || scale <= 0.0)
        scale = 1.0;
    if (scale > TERRAIN3D_ABS_MAX)
        scale = TERRAIN3D_ABS_MAX;
    if (t->layer_scales[layer] == scale)
        return;
    t->layer_scales[layer] = scale;
    t->splat_dirty = 1;
}

/// @brief Resolve a texture reference to its underlying Pixels and return it only when that is
///   a valid non-empty image, else NULL.
static void *terrain_valid_pixels_or_null(void *pixels) {
    rt_pixels_impl *p;
    if (!pixels)
        return NULL;
    pixels = rt_material3d_resolve_texture_pixels(pixels);
    p = rt_pixels_checked_impl_or_null(pixels);
    if (!p || !p->data || p->width <= 0 || p->height <= 0 || p->width > INT32_MAX ||
        p->height > INT32_MAX)
        return NULL;
    return pixels;
}

/// @brief Return @p pixels only when it is a valid non-empty Pixels image (splat maps use raw
///   Pixels rather than resolved texture references), else NULL.
static void *terrain_valid_splat_pixels_or_null(void *pixels) {
    rt_pixels_impl *p;
    if (!pixels)
        return NULL;
    p = rt_pixels_checked_impl_or_null(pixels);
    if (!p || !p->data || p->width <= 0 || p->height <= 0 || p->width > INT32_MAX ||
        p->height > INT32_MAX)
        return NULL;
    return pixels;
}

/// @brief Reset the terrain's splat state: release/clear the splat map, restore the base-texture
///   material binding, drop the baked splat texture, and clear the splat-dirty flag.
static void terrain_clear_invalid_splat_state(rt_terrain3d *t) {
    if (!t)
        return;
    if (rt_pixels_checked_impl_or_null(t->splat_map))
        terrain_release_texture_slot(&t->splat_map);
    else
        t->splat_map = NULL;
    terrain_restore_material_base_texture(t);
    terrain_release_texture_slot(&t->baked_texture);
    t->splat_dirty = 0;
}

/// @brief Sample a pixel from a texture reference at UV coordinates (wrapped).
static uint32_t sample_pixels_uv(void *texture_ref, double u, double v) {
    void *pixels;
    if (!texture_ref)
        return 0xFFFFFFFF;

    pixels = rt_material3d_resolve_texture_pixels(texture_ref);
    rt_pixels_impl *pv = rt_pixels_checked_impl_or_null(pixels);
    if (!pv || !pv->data || pv->width <= 0 || pv->height <= 0 || pv->width > INT32_MAX ||
        pv->height > INT32_MAX)
        return 0xFFFFFFFF;
    if (!isfinite(u))
        u = 0.0;
    if (!isfinite(v))
        v = 0.0;
    /* Wrap UV */
    u = u - floor(u);
    v = v - floor(v);
    int64_t px = (int64_t)(u * (double)pv->width);
    int64_t py = (int64_t)(v * (double)pv->height);
    if (px < 0)
        px = 0;
    if (py < 0)
        py = 0;
    if (px >= pv->width)
        px = pv->width - 1;
    if (py >= pv->height)
        py = pv->height - 1;
    return pv->data[py * pv->width + px];
}

/// @brief Convert a normalized floating color channel to 8-bit with finite lower/upper clamps.
static int32_t terrain_channel_to_u8(double value) {
    if (!isfinite(value) || value < 0.0)
        value = 0.0;
    if (value > 1.0)
        value = 1.0;
    return (int32_t)(value * 255.0 + 0.5);
}

/// @brief Pack the terrain material's diffuse RGB + alpha into a single 0xRRGGBBAA pixel
///   (opaque white when there is no valid material) — the fallback color for splat baking.
static uint32_t terrain_material_fallback_pixel(const rt_terrain3d *t) {
    if (!t || !t->material || !rt_g3d_has_class(t->material, RT_G3D_MATERIAL3D_CLASS_ID))
        return 0xFFFFFFFFu;
    const rt_material3d *mat = (const rt_material3d *)t->material;
    uint32_t r = (uint32_t)terrain_channel_to_u8(mat->diffuse[0]);
    uint32_t g = (uint32_t)terrain_channel_to_u8(mat->diffuse[1]);
    uint32_t b = (uint32_t)terrain_channel_to_u8(mat->diffuse[2]);
    uint32_t a = (uint32_t)terrain_channel_to_u8(mat->alpha);
    return (r << 24) | (g << 16) | (b << 8) | a;
}

/// @brief Sample height at grid coordinates (clamped).
static float sample_height(const rt_terrain3d *t, int32_t x, int32_t z) {
    if (!terrain_has_valid_grid(t))
        return 0.0f;
    if (x < 0)
        x = 0;
    if (z < 0)
        z = 0;
    if (x >= t->width)
        x = t->width - 1;
    if (z >= t->depth)
        z = t->depth - 1;
    return terrain_sanitize_height_sample(t->heights[z * t->width + x]);
}

/// @brief Number of samples that lie on a terrain edge.
static int32_t terrain_edge_sample_count(const rt_terrain3d *t, int64_t edge) {
    if (!terrain_has_valid_grid(t))
        return 0;
    switch (edge) {
        case RT_TERRAIN3D_EDGE_WEST:
        case RT_TERRAIN3D_EDGE_EAST:
            return t->depth;
        case RT_TERRAIN3D_EDGE_NORTH:
        case RT_TERRAIN3D_EDGE_SOUTH:
            return t->width;
        default:
            return 0;
    }
}

/// @brief Return a mutable height sample slot on the requested edge.
static float *terrain_edge_height_slot(rt_terrain3d *t, int64_t edge, int32_t sample) {
    if (!terrain_has_valid_grid(t))
        return NULL;
    switch (edge) {
        case RT_TERRAIN3D_EDGE_WEST:
            if (sample < 0 || sample >= t->depth)
                return NULL;
            return &t->heights[(int64_t)sample * t->width];
        case RT_TERRAIN3D_EDGE_EAST:
            if (sample < 0 || sample >= t->depth)
                return NULL;
            return &t->heights[(int64_t)sample * t->width + (t->width - 1)];
        case RT_TERRAIN3D_EDGE_NORTH:
            if (sample < 0 || sample >= t->width)
                return NULL;
            return &t->heights[sample];
        case RT_TERRAIN3D_EDGE_SOUTH:
            if (sample < 0 || sample >= t->width)
                return NULL;
            return &t->heights[(int64_t)(t->depth - 1) * t->width + sample];
        default:
            return NULL;
    }
}

/// @brief Convert a normalized height sample into world-Y units.
static double terrain_sample_to_world_height(const rt_terrain3d *t, float h) {
    double sy = terrain_scale_axis_or(t, 1, 1.0);
    return (double)terrain_sanitize_height_sample(h) * sy;
}

/// @brief Compute the terrain surface normal at grid cell (@p ix, @p iz) from neighboring height
///   samples via central differences scaled by the world cell size, writing the unit normal to
///   out_nx/out_ny/out_nz.
static void terrain_grid_normal_at(const rt_terrain3d *t,
                                   int32_t ix,
                                   int32_t iz,
                                   double sx,
                                   double sy,
                                   double sz,
                                   double *out_nx,
                                   double *out_ny,
                                   double *out_nz) {
    int32_t xl;
    int32_t xr;
    int32_t zd;
    int32_t zu;
    double span_x;
    double span_z;
    double nx;
    double ny;
    double nz;
    double len;
    float hL;
    float hR;
    float hD;
    float hU;
    if (!out_nx || !out_ny || !out_nz)
        return;
    *out_nx = 0.0;
    *out_ny = 1.0;
    *out_nz = 0.0;
    if (!terrain_has_valid_grid(t))
        return;

    ix = terrain_coord_to_index((double)ix, t->width - 1);
    iz = terrain_coord_to_index((double)iz, t->depth - 1);
    xl = ix > 0 ? ix - 1 : ix;
    xr = ix < t->width - 1 ? ix + 1 : ix;
    zd = iz > 0 ? iz - 1 : iz;
    zu = iz < t->depth - 1 ? iz + 1 : iz;
    span_x = (double)(xr - xl) * sx;
    span_z = (double)(zu - zd) * sz;
    if (!isfinite(span_x) || !isfinite(span_z) || fabs(span_x) < 1e-12 ||
        fabs(span_z) < 1e-12)
        return;

    hL = sample_height(t, xl, iz);
    hR = sample_height(t, xr, iz);
    hD = sample_height(t, ix, zd);
    hU = sample_height(t, ix, zu);

    nx = (double)(hL - hR) * sy * span_z;
    nz = (double)(hD - hU) * sy * span_x;
    ny = span_x * span_z;
    len = hypot(hypot(nx, ny), nz);
    if (isfinite(len) && len > 1e-8) {
        *out_nx = nx / len;
        *out_ny = ny / len;
        *out_nz = nz / len;
    }
}

/// @brief Convert a world-Y height back into a terrain height sample.
static float terrain_world_height_to_sample(const rt_terrain3d *t, double h) {
    double sy = terrain_scale_axis_or(t, 1, 1.0);
    double sample = h / sy;
    if (!isfinite(sample))
        sample = 0.0;
    if (sample > TERRAIN3D_ABS_MAX)
        sample = TERRAIN3D_ABS_MAX;
    if (sample < -TERRAIN3D_ABS_MAX)
        sample = -TERRAIN3D_ABS_MAX;
    return (float)sample;
}

/// @brief Average two border edges in world-height space and invalidate affected LOD meshes.
/// @details Used by WorldStream3D after adjacent terrain tiles become resident. The helper maps
///   differing edge sample counts by normalized edge distance so a coarse tile can stitch to a
///   denser neighbor without relying on visual skirts to cover the shared border.
int64_t rt_terrain3d_stitch_edge(void *terrain_obj,
                                 int64_t edge,
                                 void *neighbor_obj,
                                 int64_t neighbor_edge) {
    rt_terrain3d *terrain =
        (rt_terrain3d *)rt_g3d_checked_or_null(terrain_obj, RT_G3D_TERRAIN3D_CLASS_ID);
    rt_terrain3d *neighbor =
        (rt_terrain3d *)rt_g3d_checked_or_null(neighbor_obj, RT_G3D_TERRAIN3D_CLASS_ID);
    if (!terrain || !neighbor || terrain == neighbor)
        return 0;
    int32_t terrain_count = terrain_edge_sample_count(terrain, edge);
    int32_t neighbor_count = terrain_edge_sample_count(neighbor, neighbor_edge);
    if (terrain_count <= 0 || neighbor_count <= 0)
        return 0;
    int32_t stitch_count = terrain_count > neighbor_count ? terrain_count : neighbor_count;
    if (stitch_count <= 0)
        return 0;

    int64_t changed = 0;
    for (int32_t i = 0; i < stitch_count; ++i) {
        double u = stitch_count > 1 ? (double)i / (double)(stitch_count - 1) : 0.0;
        int32_t terrain_i =
            terrain_count > 1 ? (int32_t)llround(u * (double)(terrain_count - 1)) : 0;
        int32_t neighbor_i =
            neighbor_count > 1 ? (int32_t)llround(u * (double)(neighbor_count - 1)) : 0;
        float *terrain_h = terrain_edge_height_slot(terrain, edge, terrain_i);
        float *neighbor_h = terrain_edge_height_slot(neighbor, neighbor_edge, neighbor_i);
        if (!terrain_h || !neighbor_h)
            continue;
        double world_h = (terrain_sample_to_world_height(terrain, *terrain_h) +
                          terrain_sample_to_world_height(neighbor, *neighbor_h)) *
                         0.5;
        float stitched_terrain = terrain_world_height_to_sample(terrain, world_h);
        float stitched_neighbor = terrain_world_height_to_sample(neighbor, world_h);
        if (*terrain_h != stitched_terrain || *neighbor_h != stitched_neighbor) {
            *terrain_h = stitched_terrain;
            *neighbor_h = stitched_neighbor;
            changed++;
        }
    }
    if (changed > 0) {
        invalidate_all_chunks(terrain);
        invalidate_all_chunks(neighbor);
    }
    return changed;
}

/// @brief Build a grayscale Pixels heightmap from the terrain's current height samples.
void *rt_terrain3d_build_heightmap_pixels(void *terrain_obj) {
    rt_terrain3d *t =
        (rt_terrain3d *)rt_g3d_checked_or_null(terrain_obj, RT_G3D_TERRAIN3D_CLASS_ID);
    if (!terrain_has_valid_grid(t))
        return NULL;
    void *pixels = rt_pixels_new(t->width, t->depth);
    if (!pixels)
        return NULL;
    for (int32_t z = 0; z < t->depth; ++z) {
        for (int32_t x = 0; x < t->width; ++x) {
            double h = (double)t->heights[(int64_t)z * t->width + x];
            if (!isfinite(h))
                h = 0.0;
            if (h < 0.0)
                h = 0.0;
            if (h > 1.0)
                h = 1.0;
            uint32_t sample = (uint32_t)llround(h * 65535.0);
            if (sample > 65535u)
                sample = 65535u;
            uint32_t rgba = ((sample >> 8) & 0xFFu) << 24 | (sample & 0xFFu) << 16 | 0x000000FFu;
            rt_pixels_set(pixels, x, z, (int64_t)rgba);
        }
    }
    return pixels;
}

/// @brief Bilinearly sample the terrain height at world-space (wx, wz). Coordinates outside the
/// grid are clamped to the nearest edge cell. Returns 0 for an invalid handle or a degenerate
/// scale. Result is in world Y units (heights times scale[1]).
double rt_terrain3d_get_height_at(void *obj, double wx, double wz) {
    rt_terrain3d *t = (rt_terrain3d *)rt_g3d_checked_or_null(obj, RT_G3D_TERRAIN3D_CLASS_ID);
    double sx;
    double sy;
    double sz;
    if (!terrain_has_valid_grid(t) || !isfinite(wx) || !isfinite(wz))
        return 0.0;
    sx = terrain_scale_axis_or(t, 0, 1.0);
    sy = terrain_scale_axis_or(t, 1, 1.0);
    sz = terrain_scale_axis_or(t, 2, 1.0);

    double hx = wx / sx;
    double hz = wz / sz;
    int ix, iz;
    float fx, fz;
    if (hx <= 0.0) {
        ix = 0;
        fx = 0;
    } else if (hx >= (double)(t->width - 1)) {
        ix = t->width - 2;
        fx = 1;
    } else {
        double floor_hx = floor(hx);
        ix = (int)floor_hx;
        fx = (float)(hx - floor_hx);
    }
    if (hz <= 0.0) {
        iz = 0;
        fz = 0;
    } else if (hz >= (double)(t->depth - 1)) {
        iz = t->depth - 2;
        fz = 1;
    } else {
        double floor_hz = floor(hz);
        iz = (int)floor_hz;
        fz = (float)(hz - floor_hz);
    }

    float h00 = sample_height(t, ix, iz);
    float h10 = sample_height(t, ix + 1, iz);
    float h01 = sample_height(t, ix, iz + 1);
    float h11 = sample_height(t, ix + 1, iz + 1);
    if (!isfinite(h00))
        h00 = 0.0f;
    if (!isfinite(h10))
        h10 = 0.0f;
    if (!isfinite(h01))
        h01 = 0.0f;
    if (!isfinite(h11))
        h11 = 0.0f;
    float h = h00 * (1 - fx) * (1 - fz) + h10 * fx * (1 - fz) + h01 * (1 - fx) * fz + h11 * fx * fz;
    if (!isfinite(h))
        h = 0.0f;
    return (double)h * sy;
}

/// @brief Compute the surface normal at world-space (wx, wz) using central-difference of the
/// height grid. Returns a fresh Vec3 (always points "up-ish"; defaults to (0,1,0) on invalid
/// input). Normal is normalized; useful for placing props or aligning rotations to slope.
void *rt_terrain3d_get_normal_at(void *obj, double wx, double wz) {
    rt_terrain3d *t = (rt_terrain3d *)rt_g3d_checked_or_null(obj, RT_G3D_TERRAIN3D_CLASS_ID);
    double sx;
    double sy;
    double sz;
    if (!terrain_has_valid_grid(t) || !isfinite(wx) || !isfinite(wz))
        return rt_vec3_new(0, 1, 0);
    sx = terrain_scale_axis_or(t, 0, 1.0);
    sy = terrain_scale_axis_or(t, 1, 1.0);
    sz = terrain_scale_axis_or(t, 2, 1.0);

    double hx = wx / sx;
    double hz = wz / sz;
    int ix = terrain_coord_to_index(hx, t->width - 1);
    int iz = terrain_coord_to_index(hz, t->depth - 1);

    double nx;
    double ny;
    double nz;
    terrain_grid_normal_at(t, ix, iz, sx, sy, sz, &nx, &ny, &nz);

    return rt_vec3_new(nx, ny, nz);
}

/// @brief Bake splat-blended texture onto the terrain material.
/// Generates a Pixels texture where each texel is the weighted blend of the 4
/// layer textures, sampled at their respective UV scales, weighted by the splat
/// map RGBA channels. Applied once when chunks are invalidated.
/// @brief Pre-blend the four splat-map layers into a single baked terrain texture.
/// @details Real-time splat-mapping — sampling four layer textures and
///   RGBA-weight blending per fragment — is bandwidth-expensive, so this CPU
///   bake walks every splat-map texel once, reads its RGBA weights, normalizes
///   them, samples each enabled layer texture at its configured UV scale
///   (letting rock/moss tile at different frequencies), and writes the
///   weighted sum into a new `Pixels` object that replaces the terrain's
///   material texture. Degenerate zero-weight texels default to pure layer 0
///   rather than producing black holes. Called only when the splat map or a
///   layer texture is invalidated — subsequent frames just sample the baked
///   result.
static void bake_splat_texture(rt_terrain3d *t) {
    if (!t->splat_map || !t->material ||
        !rt_g3d_has_class(t->material, RT_G3D_MATERIAL3D_CLASS_ID))
        return;

    void *splat_obj = terrain_valid_splat_pixels_or_null(t->splat_map);
    rt_pixels_impl *splat = rt_pixels_checked_impl_or_null(splat_obj);
    if (!splat)
        return;

    /* Generate a blended texture at splat map resolution */
    int32_t tw = (int32_t)splat->width, th = (int32_t)splat->height;
    void *baked = rt_pixels_new(tw, th);
    if (!baked)
        return;

    if (!t->base_texture) {
        rt_material3d *material = (rt_material3d *)t->material;
        if (material && material->texture != t->baked_texture &&
            terrain_texture_ref_supported(material->texture))
            terrain_assign_texture_ref(&t->base_texture, material->texture);
    }

    for (int32_t y = 0; y < th; y++) {
        for (int32_t x = 0; x < tw; x++) {
            double u = tw > 1 ? (double)x / (double)(tw - 1) : 0.0;
            double v = th > 1 ? (double)y / (double)(th - 1) : 0.0;

            /* Sample splat weights (RGBA → 4 layer weights) */
            uint32_t sp = splat->data[(int64_t)y * tw + x];
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

            uint32_t fallback = t->base_texture ? sample_pixels_uv(t->base_texture, u, v)
                                                : terrain_material_fallback_pixel(t);

            /* Sample each layer texture at its UV scale and blend. Missing layer textures
             * fall back to the base material texture (or white when absent) so partial
             * splat setups do not bake black texels. */
            double weights[4] = {w0, w1, w2, w3};
            double br = 0, bg = 0, bb = 0, ba = 0;
            for (int layer = 0; layer < TERRAIN_MAX_SPLAT_LAYERS; layer++) {
                if (weights[layer] < 0.001)
                    continue;
                double layer_scale = terrain_layer_scale_or(t, layer);
                double lu = u * layer_scale;
                double lv = v * layer_scale;
                void *layer_pixels = terrain_valid_pixels_or_null(t->layer_textures[layer]);
                uint32_t lp = layer_pixels ? sample_pixels_uv(layer_pixels, lu, lv) : fallback;
                double lr = (double)((lp >> 24) & 0xFF) / 255.0;
                double lg2 = (double)((lp >> 16) & 0xFF) / 255.0;
                double lb = (double)((lp >> 8) & 0xFF) / 255.0;
                double la = (double)(lp & 0xFF) / 255.0;
                br += lr * weights[layer];
                bg += lg2 * weights[layer];
                bb += lb * weights[layer];
                ba += la * weights[layer];
            }

            int32_t cr = terrain_channel_to_u8(br);
            int32_t cg = terrain_channel_to_u8(bg);
            int32_t cb = terrain_channel_to_u8(bb);
            int32_t ca = terrain_channel_to_u8(ba);
            int64_t color =
                ((int64_t)cr << 24) | ((int64_t)cg << 16) | ((int64_t)cb << 8) | ca;
            rt_pixels_set(baked, x, y, color);
        }
    }

    rt_material3d_set_texture(t->material, baked);
    terrain_assign_texture_ref(&t->baked_texture, baked);
    if (rt_obj_release_check0(baked))
        rt_obj_free(baked);
    t->splat_dirty = 0;
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
    double h = (double)sample_height(t, ix, iz);
    double sx = terrain_scale_axis_or(t, 0, 1.0);
    double sy = terrain_scale_axis_or(t, 1, 1.0);
    double sz = terrain_scale_axis_or(t, 2, 1.0);
    if (!isfinite(h))
        h = 0.0;
    *wx = (double)ix * sx;
    *wy = h * sy;
    *wz = (double)iz * sz;
    terrain_grid_normal_at(t, ix, iz, sx, sy, sz, nx, ny, nz_n);
    *u = (double)ix / (double)(t->width - 1);
    *v = (double)iz / (double)(t->depth - 1);
}

/// @brief Build mesh for one terrain chunk at a given LOD step.
/// @param step 1=full res (LOD 0), 2=half (LOD 1), 4=quarter (LOD 2).
/// @param aabb_out If non-NULL, receives the chunk's AABB (6 floats: min[3], max[3]).
static void *build_chunk(rt_terrain3d *t, int32_t cx, int32_t cz, int32_t step, float *aabb_out) {
    int32_t x0 = cx * TERRAIN_CHUNK_SIZE;
    int32_t z0 = cz * TERRAIN_CHUNK_SIZE;
    void *mesh;
    if (!terrain_has_valid_grid(t) || step <= 0 || cx < 0 || cz < 0) {
        if (aabb_out) {
            for (int i = 0; i < 6; i++)
                aabb_out[i] = 0.0f;
        }
        return NULL;
    }
    mesh = rt_mesh3d_new();
    if (!mesh)
        return NULL;

    /* Determine actual chunk extents (may be smaller at edges) */
    int32_t xend = x0 + TERRAIN_CHUNK_SIZE;
    int32_t zend = z0 + TERRAIN_CHUNK_SIZE;
    if (xend >= t->width)
        xend = t->width - 1;
    if (zend >= t->depth)
        zend = t->depth - 1;
    int32_t cols = xend - x0;
    int32_t rows = zend - z0;
    if (cols <= 0 || rows <= 0) {
        if (aabb_out) {
            for (int i = 0; i < 6; i++)
                aabb_out[i] = 0.0f;
        }
        return mesh;
    }

    /* Track AABB during vertex generation */
    float aabb_min[3] = {FLT_MAX, FLT_MAX, FLT_MAX};
    float aabb_max[3] = {-FLT_MAX, -FLT_MAX, -FLT_MAX};

    /* Vertices (with LOD step). Edge chunks can be smaller than the selected
     * step, so force the final row/column to be present. */
    int32_t vert_cols = cols / step + 1;
    int32_t vert_rows = rows / step + 1;
    if (cols % step != 0)
        vert_cols++;
    if (rows % step != 0)
        vert_rows++;
    for (int32_t rz = 0; rz < vert_rows; rz++) {
        int32_t dz = rz * step;
        if (dz > rows || rz == vert_rows - 1)
            dz = rows;
        for (int32_t rx = 0; rx < vert_cols; rx++) {
            int32_t dx = rx * step;
            if (dx > cols || rx == vert_cols - 1)
                dx = cols;
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
        }
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
    float skirt_depth = terrain_skirt_depth_or(t);
    if (skirt_depth > 0.0f)
        aabb_min[1] -= skirt_depth;
    if (skirt_depth > 0.0f && step > 1) {
        double sd = (double)skirt_depth;
        /* For each of the 4 edges, add skirt triangles */
        /* Top edge (dz=0), bottom edge (dz=rows), left (dx=0), right (dx=cols) */
        int64_t skirt_base = (int64_t)(vert_rows * vert_cols);

        /* Top edge (z = z0) */
        for (int32_t rx = 0; rx < vert_cols; rx++) {
            int32_t sample_dx = rx * step;
            if (sample_dx > cols || rx == vert_cols - 1)
                sample_dx = cols;
            int32_t ix = x0 + sample_dx;
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
            int32_t sample_dx = rx * step;
            if (sample_dx > cols || rx == vert_cols - 1)
                sample_dx = cols;
            int32_t ix = x0 + sample_dx;
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
            int32_t sample_dz = rz * step;
            if (sample_dz > rows || rz == vert_rows - 1)
                sample_dz = rows;
            int32_t iz = z0 + sample_dz;
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
            int32_t sample_dz = rz * step;
            if (sample_dz > rows || rz == vert_rows - 1)
                sample_dz = rows;
            int32_t iz = z0 + sample_dz;
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

    if (((rt_mesh3d *)mesh)->build_failed) {
        if (rt_obj_release_check0(mesh))
            rt_obj_free(mesh);
        if (aabb_out) {
            for (int i = 0; i < 6; i++)
                aabb_out[i] = 0.0f;
        }
        return NULL;
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

/// @brief Set chunk LOD switch distances. Within `near_dist` chunks render at full resolution;
/// between near and far they use step=2 (¼ triangles); beyond `far_dist` they use step=4
/// (1/16 triangles). Lower distances trade visual quality for triangle count.
void rt_terrain3d_set_lod_distances(void *obj, double near_dist, double far_dist) {
    rt_terrain3d *t = (rt_terrain3d *)rt_g3d_checked_or_null(obj, RT_G3D_TERRAIN3D_CLASS_ID);
    if (!t)
        return;
    if (!isfinite(near_dist) || near_dist < 0.0)
        near_dist = 50.0;
    if (!isfinite(far_dist) || far_dist <= near_dist)
        far_dist = near_dist + 100.0;
    if (near_dist < 0.0)
        near_dist = 0.0;
    if (near_dist > TERRAIN3D_ABS_MAX)
        near_dist = TERRAIN3D_ABS_MAX;
    if (far_dist > TERRAIN3D_ABS_MAX)
        far_dist = TERRAIN3D_ABS_MAX;
    if (far_dist <= near_dist) {
        far_dist = near_dist + 1.0;
        if (far_dist > TERRAIN3D_ABS_MAX) {
            far_dist = TERRAIN3D_ABS_MAX;
            near_dist = far_dist - 1.0;
            if (near_dist < 0.0)
                near_dist = 0.0;
        }
    }
    if (t->lod_dist1 == (float)near_dist && t->lod_dist2 == (float)far_dist)
        return;
    t->lod_dist1 = (float)near_dist;
    t->lod_dist2 = (float)far_dist;
}

/// @brief Set the depth (world units) of the downward-extruded skirt geometry generated along
/// LOD>0 chunk edges. Skirts hide T-junction cracks where adjacent chunks render at different
/// LODs. Set to 0 to disable. Invalidates cached chunks (skirts are baked into the mesh).
void rt_terrain3d_set_skirt_depth(void *obj, double depth) {
    rt_terrain3d *t = (rt_terrain3d *)rt_g3d_checked_or_null(obj, RT_G3D_TERRAIN3D_CLASS_ID);
    if (!t)
        return;
    if (!isfinite(depth) || depth < 0.0)
        depth = 0.0;
    if (depth > 1000.0)
        depth = 1000.0;
    if (t->skirt_depth == (float)depth)
        return;
    t->skirt_depth = (float)depth;
    invalidate_all_chunks(t);
}

/// @brief Number of nav-mesh grid samples along an axis of @p max_index cells decimated by @p step.
/// @details Always includes the final cell (adds an extra sample when max_index isn't a multiple of
///          step) and yields at least 2, so the nav source spans the full terrain footprint.
static int32_t terrain_nav_sample_count(int32_t max_index, int32_t step) {
    int32_t count;
    if (max_index <= 0)
        return 1;
    if (step <= 0)
        step = 1;
    count = max_index / step + 1;
    if (max_index % step != 0)
        count++;
    if (count < 2)
        count = 2;
    return count;
}

/// @brief Map a decimated nav sample @p index to its grid coordinate, snapping the ends to
/// 0/max_index.
static int32_t terrain_nav_sample_coord(int32_t index,
                                        int32_t count,
                                        int32_t max_index,
                                        int32_t step) {
    if (index <= 0)
        return 0;
    if (index >= count - 1)
        return max_index;
    int32_t value = index * step;
    return value > max_index ? max_index : value;
}

/// @brief Build a Mesh3D approximation of the whole terrain for scene/nav baking.
/// @details This is intentionally separate from the render chunk cache: nav only needs a
///   stable triangle source that follows the terrain's heightmap and scale, while rendering
///   keeps its own LOD/skirt chunk meshes. `step` decimates grid samples but always includes
///   the final row/column so the nav source keeps the streamed tile footprint.
void *rt_terrain3d_build_nav_mesh(void *terrain_obj, int64_t step64) {
    rt_terrain3d *t =
        (rt_terrain3d *)rt_g3d_checked_or_null(terrain_obj, RT_G3D_TERRAIN3D_CLASS_ID);
    if (!terrain_has_valid_grid(t))
        return NULL;
    int32_t step;
    if (step64 < 1)
        step64 = 1;
    if (step64 > 16)
        step64 = 16;
    step = (int32_t)step64;
    if (step < 1)
        step = 1;
    int32_t cols = terrain_nav_sample_count(t->width - 1, step);
    int32_t rows = terrain_nav_sample_count(t->depth - 1, step);
    void *mesh = rt_mesh3d_new();
    if (!mesh)
        return NULL;

    for (int32_t rz = 0; rz < rows; ++rz) {
        int32_t iz = terrain_nav_sample_coord(rz, rows, t->depth - 1, step);
        for (int32_t rx = 0; rx < cols; ++rx) {
            int32_t ix = terrain_nav_sample_coord(rx, cols, t->width - 1, step);
            double wx, wy, wz, nx, ny, nz_n, u, v;
            terrain_vertex(t, ix, iz, &wx, &wy, &wz, &nx, &ny, &nz_n, &u, &v);
            rt_mesh3d_add_vertex(mesh, wx, wy, wz, nx, ny, nz_n, u, v);
        }
    }

    for (int32_t rz = 0; rz < rows - 1; ++rz) {
        for (int32_t rx = 0; rx < cols - 1; ++rx) {
            int64_t base = (int64_t)(rz * cols + rx);
            rt_mesh3d_add_triangle(mesh, base, base + cols, base + 1);
            rt_mesh3d_add_triangle(mesh, base + 1, base + cols, base + cols + 1);
        }
    }

    if (((rt_mesh3d *)mesh)->build_failed) {
        if (rt_obj_release_check0(mesh))
            rt_obj_free(mesh);
        return NULL;
    }
    return mesh;
}

/// @brief Render the terrain at the origin via the Canvas3D.
void rt_canvas3d_draw_terrain(void *canvas_obj, void *terrain_obj) {
    rt_canvas3d_draw_terrain_at(canvas_obj, terrain_obj, 0.0, 0.0, 0.0);
}

/// @brief Render the terrain via the Canvas3D at a world-space translation.
/// @details Lazily builds chunk meshes on first draw, bakes the splat texture if needed, then
///   for each chunk: frustum-cull the translated AABB, pick LOD by translated chunk distance
///   to camera (XZ), and enqueue the chosen mesh with a translated model matrix.
void rt_canvas3d_draw_terrain_at(
    void *canvas_obj, void *terrain_obj, double tx, double ty, double tz) {
    rt_canvas3d *c = rt_canvas3d_checked_or_stack(canvas_obj);
    rt_terrain3d *t =
        (rt_terrain3d *)rt_g3d_checked_or_null(terrain_obj, RT_G3D_TERRAIN3D_CLASS_ID);
    int32_t chunk_count;
    void *splat_map;
    void *splat_layers[TERRAIN_MAX_SPLAT_LAYERS] = {NULL, NULL, NULL, NULL};
    float splat_layer_scales[TERRAIN_MAX_SPLAT_LAYERS] = {1.0f, 1.0f, 1.0f, 1.0f};
    int complete_splat_layers = 0;
    if (!c || !t)
        return;
    if (!c->in_frame || !c->backend || !t->material ||
        !rt_g3d_has_class(t->material, RT_G3D_MATERIAL3D_CLASS_ID) ||
        !terrain_has_valid_grid(t) || !terrain_has_valid_chunk_grid(t))
        return;
    if (c->frame_is_2d) {
        rt_trap("Canvas3D.DrawTerrain: cannot draw terrain during Begin2D/End");
        return;
    }

    /* Bake splat-blended texture if splat map is set and chunks are invalid */
    chunk_count = terrain_safe_chunk_count(t);
    splat_map = terrain_valid_splat_pixels_or_null(t->splat_map);
    if (t->splat_map && !splat_map)
        terrain_clear_invalid_splat_state(t);
    if (splat_map) {
        int any_invalid = 0;
        complete_splat_layers = 1;
        for (int si = 0; si < TERRAIN_MAX_SPLAT_LAYERS; si++) {
            splat_layers[si] = terrain_valid_pixels_or_null(t->layer_textures[si]);
            splat_layer_scales[si] = (float)terrain_layer_scale_or(t, si);
            if (!splat_layers[si])
                complete_splat_layers = 0;
        }
        for (int32_t i = 0; i < chunk_count && !any_invalid; i++)
            if (!t->chunk_meshes[i])
                any_invalid = 1;
        if (t->splat_dirty || any_invalid)
            bake_splat_texture(t);
    }

    /* Extract frustum from cached VP matrix for culling */
    vgfx3d_frustum_t frustum;
    vgfx3d_frustum_extract(&frustum, c->cached_vp);

    tx = terrain_clamp_abs_or(tx, 0.0);
    ty = terrain_clamp_abs_or(ty, 0.0);
    tz = terrain_clamp_abs_or(tz, 0.0);

    float lod_dist1;
    float lod_dist2;
    terrain_lod_distances_or(t, &lod_dist1, &lod_dist2);

    double model[16] = {
        1.0,
        0.0,
        0.0,
        tx,
        0.0,
        1.0,
        0.0,
        ty,
        0.0,
        0.0,
        1.0,
        tz,
        0.0,
        0.0,
        0.0,
        1.0,
    };

    for (int32_t cz = 0; cz < t->chunks_z; cz++) {
        for (int32_t cx = 0; cx < t->chunks_x; cx++) {
            int32_t idx = cz * t->chunks_x + cx;

            /* Ensure LOD 0 mesh + AABB are built (AABB computed from LOD 0) */
            if (!t->chunk_meshes[idx])
                t->chunk_meshes[idx] = build_chunk(t, cx, cz, 1, &t->chunk_aabbs[idx * 6]);
            if (!t->chunk_meshes[idx])
                continue;

            /* Phase A: Frustum culling */
            float *aabb = &t->chunk_aabbs[idx * 6];
            float world_min[3] = {
                aabb[0] + (float)tx,
                aabb[1] + (float)ty,
                aabb[2] + (float)tz,
            };
            float world_max[3] = {
                aabb[3] + (float)tx,
                aabb[4] + (float)ty,
                aabb[5] + (float)tz,
            };
            if (vgfx3d_frustum_test_aabb(&frustum, world_min, world_max) == 0)
                continue;

            /* Phase B: LOD selection based on distance to camera */
            float chunk_cx = (world_min[0] + world_max[0]) * 0.5f;
            float chunk_cz = (world_min[2] + world_max[2]) * 0.5f;
            double dx = (double)chunk_cx - (double)c->cached_cam_pos[0];
            double dz = (double)chunk_cz - (double)c->cached_cam_pos[2];
            double dist = sqrt(dx * dx + dz * dz);
            if (!isfinite(dist))
                dist = (double)lod_dist2;

            void *draw_mesh = NULL;
            if (dist >= lod_dist2) {
                /* LOD 2: quarter resolution */
                if (!t->chunk_meshes_lod2[idx])
                    t->chunk_meshes_lod2[idx] = build_chunk(t, cx, cz, 4, NULL);
                draw_mesh = t->chunk_meshes_lod2[idx];
            } else if (dist >= lod_dist1) {
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
                if (splat_map && complete_splat_layers) {
                    c->pending_has_splat = 1;
                    c->pending_splat_map = splat_map;
                    for (int si = 0; si < 4; si++) {
                        c->pending_splat_layers[si] = splat_layers[si];
                        c->pending_splat_layer_scales[si] = splat_layer_scales[si];
                    }
                }
                rt_canvas3d_draw_mesh_matrix(canvas_obj, draw_mesh, model, t->material);
            }
        }
    }
}

#else
typedef int rt_graphics_disabled_tu_guard;
#endif /* VIPER_ENABLE_GRAPHICS */
