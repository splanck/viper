//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/world/rt_vegetation3d.c
// Purpose: Instanced vegetation rendering with cross-billboard blades,
//   wind animation, density map population, and distance-based LOD.
//
// Key invariants:
//   - Blade mesh: 2 perpendicular quads (8 verts, 4 tris) — cross-billboard.
//   - Population: LCG random scatter on terrain, filtered by density map.
//   - Wind: per-blade Y-axis shear using sin(position + time).
//   - LOD: progressive thinning + hard cull at far distance.
//   - Rendering: queues instanced blade draws through Canvas3D so they share
//     the same shadow, sorting, and overlay pipeline as other 3D content.
//   - Backface culling temporarily disabled for grass (visible both sides).
//
// Ownership/Lifetime:
//   - Vegetation3D is GC-managed; finalizer frees per-instance buffers and
//     releases the blade mesh, blade material, and density map.
//
// Links: rt_vegetation3d.h, rt_terrain3d.h, rt_canvas3d.h
//
//===----------------------------------------------------------------------===//

#ifdef VIPER_ENABLE_GRAPHICS

#include "rt_vegetation3d.h"
#include "rt_canvas3d.h"
#include "rt_canvas3d_internal.h"
#include "rt_pixels_internal.h"
#include "rt_world3d_common.h"
#include "vgfx3d_backend.h"

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
extern void *rt_mesh3d_new(void);
extern void rt_mesh3d_add_vertex(
    void *m, double x, double y, double z, double nx, double ny, double nz, double u, double v);
extern void rt_mesh3d_add_triangle(void *m, int64_t v0, int64_t v1, int64_t v2);
extern void rt_mesh3d_clear(void *m);
extern void *rt_mat4_identity(void);
extern void rt_canvas3d_draw_mesh(void *canvas, void *mesh, void *transform, void *material);
extern void *rt_material3d_new(void);
extern void rt_material3d_set_texture(void *m, void *tex);
extern void rt_material3d_set_unlit(void *m, int8_t u);
extern double rt_terrain3d_get_height_at(void *terrain, double x, double z);

typedef struct {
    void *vptr;
    void *blade_mesh;     /* cross-billboard mesh (shared by all blades) */
    void *blade_material; /* textured, unlit */
    /* Blade geometry params */
    double blade_width;
    double blade_height;
    double size_variation; /* [0.0-1.0] random scale variation */
    /* Population */
    float *base_transforms; /* total_count * 16 floats */
    float *positions;       /* total_count * 3 floats (x,y,z for wind/LOD) */
    int32_t total_count;
    int32_t capacity;
    void *density_map;
    /* Wind */
    double wind_speed;
    double wind_strength;
    double wind_turbulence;
    double time;
    /* LOD */
    float lod_near;
    float lod_far;
    /* Per-frame visible set */
    float *visible_transforms; /* visible_count * 16 floats */
    int32_t visible_count;
    int32_t visible_capacity;
} rt_vegetation3d;

/// @brief Return @p value when finite, else @p fallback. Sanitizes scalar inputs.
static double vegetation_finite_or(double value, double fallback) {
    return rt_world3d_finite_or(value, fallback);
}

/// @brief Drop one GC reference held in `*slot` and clear the slot. NULL-safe.
static void vegetation3d_release_ref(void **slot) {
    if (!slot || !*slot)
        return;
    if (rt_obj_release_check0(*slot))
        rt_obj_free(*slot);
    *slot = NULL;
}

/// @brief Retain-then-release swap into @p slot. Safe on self-assign.
static void vegetation3d_assign_ref(void **slot, void *value) {
    if (!slot || *slot == value)
        return;
    rt_obj_retain_maybe(value);
    vegetation3d_release_ref(slot);
    *slot = value;
}

/// @brief GC finalizer — release owned blade resources and instance arrays.
/// @details The vegetation system stores per-instance data in three
///   independent flat buffers: `base_transforms` (original 4x4 matrices),
///   `positions` (vec3 source points kept for wind resimulation), and
///   `visible_transforms` (this frame's culled subset). All three are
///   plain float allocations — nothing inside them owns downstream refs,
///   so the finalize is just three `free`s plus pointer nulling.
static void vegetation3d_finalizer(void *obj) {
    rt_vegetation3d *v = (rt_vegetation3d *)obj;
    free(v->base_transforms);
    free(v->positions);
    free(v->visible_transforms);
    v->base_transforms = NULL;
    v->positions = NULL;
    v->visible_transforms = NULL;
    vegetation3d_release_ref(&v->density_map);
    vegetation3d_release_ref(&v->blade_mesh);
    vegetation3d_release_ref(&v->blade_material);
}

/// @brief Build the cross-billboard blade mesh (2 perpendicular quads).
static void build_blade_mesh(void *mesh, double w, double h) {
    if (!mesh)
        return;
    double hw = w * 0.5;
    /* Quad 1: X-aligned */
    rt_mesh3d_add_vertex(mesh, -hw, 0, 0, 0, 0, 1, 0, 1);
    rt_mesh3d_add_vertex(mesh, hw, 0, 0, 0, 0, 1, 1, 1);
    rt_mesh3d_add_vertex(mesh, hw, h, 0, 0, 0, 1, 1, 0);
    rt_mesh3d_add_vertex(mesh, -hw, h, 0, 0, 0, 1, 0, 0);
    rt_mesh3d_add_triangle(mesh, 0, 1, 2);
    rt_mesh3d_add_triangle(mesh, 0, 2, 3);
    /* Quad 2: Z-aligned (perpendicular) */
    rt_mesh3d_add_vertex(mesh, 0, 0, -hw, 1, 0, 0, 0, 1);
    rt_mesh3d_add_vertex(mesh, 0, 0, hw, 1, 0, 0, 1, 1);
    rt_mesh3d_add_vertex(mesh, 0, h, hw, 1, 0, 0, 1, 0);
    rt_mesh3d_add_vertex(mesh, 0, h, -hw, 1, 0, 0, 0, 0);
    rt_mesh3d_add_triangle(mesh, 4, 5, 6);
    rt_mesh3d_add_triangle(mesh, 4, 6, 7);
}

/// @brief Deterministic linear congruential RNG for scattering blade instances.
/// @details Uses the `glibc` constants (1103515245, 12345, mod 2^32). Chosen
///   over `rand()` so blade placement is repeatable across platforms and
///   runs for a given seed — important for authoring workflows where an
///   artist tunes a seed to get a specific look. Not suitable for anything
///   cryptographic, but statistically adequate for even-ish scattering.
static uint32_t lcg_next(uint32_t *state) {
    *state = *state * 1103515245u + 12345u;
    return *state;
}

/// @brief Construct a Vegetation3D system. Allocates the shared cross-billboard blade mesh and
/// an unlit textured material (if `blade_texture` is non-NULL — opacity comes from the texture).
/// Defaults: 0.4×1.2 blades with 30% size variation, wind speed 2.0 / strength 0.15 / turbulence
/// 0.5, LOD near=40 / far=100 world units. Traps on allocation failure.
void *rt_vegetation3d_new(void *blade_texture) {
    rt_vegetation3d *v = (rt_vegetation3d *)rt_obj_new_i64(RT_G3D_VEGETATION3D_CLASS_ID,
                                                           (int64_t)sizeof(rt_vegetation3d));
    if (!v) {
        rt_trap("Vegetation3D.New: allocation failed");
        return NULL;
    }
    v->vptr = NULL;
    v->blade_width = 0.4;
    v->blade_height = 1.2;
    v->size_variation = 0.3;
    v->base_transforms = NULL;
    v->positions = NULL;
    v->total_count = 0;
    v->capacity = 0;
    v->density_map = NULL;
    v->wind_speed = 2.0;
    v->wind_strength = 0.15;
    v->wind_turbulence = 0.5;
    v->time = 0.0;
    v->lod_near = 40.0f;
    v->lod_far = 100.0f;
    v->visible_transforms = NULL;
    v->visible_count = 0;
    v->visible_capacity = 0;

    /* Build blade mesh */
    v->blade_mesh = rt_mesh3d_new();
    if (!v->blade_mesh) {
        if (rt_obj_release_check0(v))
            rt_obj_free(v);
        rt_trap("Vegetation3D.New: blade mesh allocation failed");
        return NULL;
    }
    build_blade_mesh(v->blade_mesh, v->blade_width, v->blade_height);

    /* Build material */
    v->blade_material = rt_material3d_new();
    if (!v->blade_material) {
        vegetation3d_release_ref(&v->blade_mesh);
        if (rt_obj_release_check0(v))
            rt_obj_free(v);
        rt_trap("Vegetation3D.New: material allocation failed");
        return NULL;
    }
    if (blade_texture)
        rt_material3d_set_texture(v->blade_material, blade_texture);
    rt_material3d_set_unlit(v->blade_material, 1);

    rt_obj_set_finalizer(v, vegetation3d_finalizer);
    return v;
}

/// @brief Attach a Pixels density map. During `_populate`, each candidate blade rolls against the
/// red channel of the corresponding pixel — higher R = denser vegetation. NULL = uniform density.
void rt_vegetation3d_set_density_map(void *obj, void *pixels) {
    rt_vegetation3d *v =
        (rt_vegetation3d *)rt_g3d_checked_or_null(obj, RT_G3D_VEGETATION3D_CLASS_ID);
    if (!v)
        return;
    if (pixels) {
        rt_pixels_impl *p =
            rt_pixels_checked_impl(pixels, "Vegetation3D.SetDensityMap: expected Pixels");
        if (!p || p->width <= 0 || p->height <= 0 || !p->data) {
            rt_trap("Vegetation3D.SetDensityMap: density map must be non-empty Pixels");
            return;
        }
    }
    vegetation3d_assign_ref(&v->density_map, pixels);
}

/// @brief Configure wind animation. `speed` scales time, `strength` is the maximum top-of-blade
/// shear in world units, `turbulence` controls how much the wind phase varies across the field
/// (higher = more chaotic, less synchronized waving).
void rt_vegetation3d_set_wind_params(void *obj, double speed, double strength, double turbulence) {
    if (!obj)
        return;
    rt_vegetation3d *v =
        (rt_vegetation3d *)rt_g3d_checked_or_null(obj, RT_G3D_VEGETATION3D_CLASS_ID);
    if (!v)
        return;
    v->wind_speed = vegetation_finite_or(speed, 0.0);
    v->wind_strength = vegetation_finite_or(strength, 0.0);
    if (v->wind_strength < 0.0)
        v->wind_strength = 0.0;
    v->wind_turbulence = vegetation_finite_or(turbulence, 0.0);
    if (v->wind_turbulence < 0.0)
        v->wind_turbulence = 0.0;
}

/// @brief Set LOD thresholds. Within `near_dist` all blades render; between near and far they
/// progressively thin (skip every 1..5 instances based on distance ratio); beyond `far_dist`
/// blades are hard-culled. Larger near = denser foreground at higher GPU cost.
void rt_vegetation3d_set_lod_distances(void *obj, double near_dist, double far_dist) {
    if (!obj)
        return;
    rt_vegetation3d *v =
        (rt_vegetation3d *)rt_g3d_checked_or_null(obj, RT_G3D_VEGETATION3D_CLASS_ID);
    if (!v)
        return;
    near_dist = vegetation_finite_or(near_dist, 40.0);
    far_dist = vegetation_finite_or(far_dist, 100.0);
    if (near_dist < 0.0)
        near_dist = 0.0;
    if (far_dist <= near_dist + 1e-6)
        far_dist = near_dist + 1.0;
    v->lod_near = (float)near_dist;
    v->lod_far = (float)far_dist;
}

/// @brief Reset blade dimensions and rebuild the shared mesh. `variation` ∈ [0,1] randomizes per-
/// blade scale at populate time. Already-populated blades retain their previous random scale —
/// call `_populate` again to apply the new variation factor.
void rt_vegetation3d_set_blade_size(void *obj, double width, double height, double variation) {
    if (!obj)
        return;
    rt_vegetation3d *v =
        (rt_vegetation3d *)rt_g3d_checked_or_null(obj, RT_G3D_VEGETATION3D_CLASS_ID);
    if (!v)
        return;
    width = vegetation_finite_or(width, 0.4);
    height = vegetation_finite_or(height, 1.2);
    variation = vegetation_finite_or(variation, 0.0);
    if (width <= 0.0)
        width = 0.4;
    if (height <= 0.0)
        height = 1.2;
    if (variation < 0.0)
        variation = 0.0;
    if (variation > 1.0)
        variation = 1.0;
    v->blade_width = width;
    v->blade_height = height;
    v->size_variation = variation;
    /* Rebuild blade mesh with new size */
    if (v->blade_mesh)
        rt_mesh3d_clear(v->blade_mesh);
    build_blade_mesh(v->blade_mesh, width, height);
}

/// @brief Build a row-major 4x4 transform: translate(x,y,z) * rotateY(angle) * scale(s).
static void build_transform(float *out, double x, double y, double z, double angle, double s) {
    double ca = cos(angle), sa = sin(angle);
    /* Row 0 */ out[0] = (float)(ca * s);
    out[1] = 0;
    out[2] = (float)(sa * s);
    out[3] = (float)x;
    /* Row 1 */ out[4] = 0;
    out[5] = (float)s;
    out[6] = 0;
    out[7] = (float)y;
    /* Row 2 */ out[8] = (float)(-sa * s);
    out[9] = 0;
    out[10] = (float)(ca * s);
    out[11] = (float)z;
    /* Row 3 */ out[12] = 0;
    out[13] = 0;
    out[14] = 0;
    out[15] = 1.0f;
}

/// @brief Scatter up to `count` blade instances across `terrain`. Positions are LCG-randomized
/// within terrain bounds (2-unit margin), filtered by the density map's R channel if set, and
/// snapped to terrain height. Each blade gets a random Y rotation and per-blade scale variation
/// (±size_variation). Reallocates the transform/position buffers and resets `total_count`.
void rt_vegetation3d_populate(void *obj, void *terrain, int64_t count) {
    rt_vegetation3d *v =
        (rt_vegetation3d *)rt_g3d_checked_or_null(obj, RT_G3D_VEGETATION3D_CLASS_ID);
    if (!v)
        return;
    if (count <= 0) {
        v->total_count = 0;
        v->visible_count = 0;
        return;
    }
    if (count > INT32_MAX) {
        rt_trap("Vegetation3D.Populate: count exceeds supported range");
        return;
    }

    /* Get terrain dimensions for scatter bounds */
    typedef struct {
        void *vptr;
        float *heights;
        int32_t width, depth;
        double scale[3];
    } terrain_view;

    terrain_view *tv = (terrain_view *)rt_g3d_checked_or_null(terrain, RT_G3D_TERRAIN3D_CLASS_ID);
    if (!tv || tv->width <= 0 || tv->depth <= 0)
        return;

    double sx = vegetation_finite_or(tv->scale[0], 1.0);
    double sz = vegetation_finite_or(tv->scale[2], 1.0);
    if (sx <= 0.0)
        sx = 1.0;
    if (sz <= 0.0)
        sz = 1.0;
    double tw = (double)tv->width * sx;
    double td = (double)tv->depth * sz;
    if (!isfinite(tw) || !isfinite(td) || tw <= 0.0 || td <= 0.0) {
        rt_trap("Vegetation3D.Populate: invalid terrain extents");
        return;
    }

    /* Allocate storage */
    int32_t cap = (int32_t)count;
    size_t base_count;
    size_t pos_count;
    size_t base_bytes;
    size_t pos_bytes;
    if (!rt_world3d_checked_mul_size((size_t)cap, 16u, &base_count) ||
        !rt_world3d_checked_mul_size((size_t)cap, 3u, &pos_count) ||
        !rt_world3d_checked_mul_size(base_count, sizeof(float), &base_bytes) ||
        !rt_world3d_checked_mul_size(pos_count, sizeof(float), &pos_bytes)) {
        rt_trap("Vegetation3D.Populate: allocation size overflow");
        return;
    }
    float *new_base_transforms = (float *)malloc(base_bytes);
    float *new_positions = (float *)malloc(pos_bytes);
    if (!new_base_transforms || !new_positions) {
        free(new_base_transforms);
        free(new_positions);
        rt_trap("Vegetation3D.Populate: allocation failed");
        return;
    }

    rt_pixels_impl *density_map = NULL;
    if (v->density_map) {
        density_map = rt_pixels_checked_impl_or_null(v->density_map);
        if (!density_map || density_map->width <= 0 || density_map->height <= 0 ||
            !density_map->data) {
            free(new_base_transforms);
            free(new_positions);
            rt_trap("Vegetation3D.Populate: density map is invalid");
            return;
        }
    }

    free(v->base_transforms);
    free(v->positions);
    v->base_transforms = new_base_transforms;
    v->positions = new_positions;
    v->capacity = cap;
    v->total_count = 0;
    v->visible_count = 0;

    uint32_t rng = 42;
    double min_x = tw >= 4.0 ? 2.0 : 0.0;
    double max_x = tw >= 4.0 ? tw - 2.0 : tw;
    double min_z = td >= 4.0 ? 2.0 : 0.0;
    double max_z = td >= 4.0 ? td - 2.0 : td;

    for (int64_t i = 0; i < count; i++) {
        /* Random position within terrain bounds (margin of 2 units) */
        double fx = (double)(lcg_next(&rng) & 0xFFFF) / 65535.0;
        double fz = (double)(lcg_next(&rng) & 0xFFFF) / 65535.0;
        double wx = min_x + fx * (max_x - min_x);
        double wz = min_z + fz * (max_z - min_z);

        /* Density map check */
        if (density_map) {
            int64_t px = (int64_t)(fx * (double)(density_map->width - 1));
            int64_t pz = (int64_t)(fz * (double)(density_map->height - 1));
            if (px < 0)
                px = 0;
            if (pz < 0)
                pz = 0;
            if (px >= density_map->width)
                px = density_map->width - 1;
            if (pz >= density_map->height)
                pz = density_map->height - 1;
            uint32_t pixel = density_map->data[pz * density_map->width + px];
            int32_t density = (int32_t)((pixel >> 24) & 0xFF); /* R channel */
            uint32_t roll = lcg_next(&rng) & 0xFF;
            if ((int32_t)roll > density)
                continue; /* skip based on density */
        }

        double wy = rt_terrain3d_get_height_at(terrain, wx, wz);
        wy = vegetation_finite_or(wy, 0.0);

        /* Random Y rotation + scale variation */
        double angle = ((double)(lcg_next(&rng) & 0xFFFF) / 65535.0) * 6.283185307;
        double scale_var =
            1.0 + (((double)(lcg_next(&rng) & 0xFFFF) / 65535.0) - 0.5) * 2.0 * v->size_variation;
        if (!isfinite(scale_var) || scale_var < 0.01)
            scale_var = 0.01;

        int32_t idx = v->total_count;
        build_transform(&v->base_transforms[idx * 16], wx, wy, wz, angle, scale_var);
        v->positions[idx * 3 + 0] = (float)wx;
        v->positions[idx * 3 + 1] = (float)wy;
        v->positions[idx * 3 + 2] = (float)wz;
        v->total_count++;
    }
}

/// @brief Per-frame tick. Advances wind time by `dt`, then rebuilds the visible-instances buffer
/// by walking every populated blade: hard-cull beyond `lod_far`, progressively skip between
/// `lod_near` and `lod_far` (skip stride 1..5), and apply per-blade wind shear to columns 1 and 9
/// of the transform (bending blade tops). `camY` is unused — culling is XZ-only.
void rt_vegetation3d_update(void *obj, double dt, double camX, double camY, double camZ) {
    (void)camY;
    rt_vegetation3d *v =
        (rt_vegetation3d *)rt_g3d_checked_or_null(obj, RT_G3D_VEGETATION3D_CLASS_ID);
    if (!v || !isfinite(dt) || dt < 0.0)
        return;
    camX = vegetation_finite_or(camX, 0.0);
    camZ = vegetation_finite_or(camZ, 0.0);
    v->time += dt;
    if (!isfinite(v->time))
        v->time = 0.0;

    if (v->total_count <= 0 || !v->base_transforms || !v->positions) {
        v->visible_count = 0;
        return;
    }

    /* Ensure visible buffer is large enough */
    if (v->visible_capacity < v->total_count) {
        size_t visible_count;
        size_t visible_bytes;
        if (!rt_world3d_checked_mul_size((size_t)v->total_count, 16u, &visible_count) ||
            !rt_world3d_checked_mul_size(visible_count, sizeof(float), &visible_bytes)) {
            v->visible_count = 0;
            rt_trap("Vegetation3D.Update: visible buffer size overflow");
            return;
        }
        float *new_visible = (float *)realloc(v->visible_transforms, visible_bytes);
        if (!new_visible) {
            v->visible_count = 0;
            rt_trap("Vegetation3D.Update: visible buffer allocation failed");
            return;
        }
        v->visible_transforms = new_visible;
        v->visible_capacity = v->total_count;
    }
    v->visible_count = 0;

    for (int32_t i = 0; i < v->total_count; i++) {
        float bx = v->positions[i * 3 + 0];
        float bz = v->positions[i * 3 + 2];

        /* Distance to camera (XZ plane) */
        float dx = bx - (float)camX;
        float dz = bz - (float)camZ;
        float dist = sqrtf(dx * dx + dz * dz);

        /* Hard cull beyond far distance */
        if (dist > v->lod_far)
            continue;

        /* Progressive thinning between near and far */
        if (dist > v->lod_near) {
            float denom = v->lod_far - v->lod_near;
            float t = denom > 1e-6f ? (dist - v->lod_near) / denom : 1.0f;
            if (t < 0.0f)
                t = 0.0f;
            if (t > 1.0f)
                t = 1.0f;
            int skip = 1 + (int)(t * 4.0f);
            if (skip < 1)
                skip = 1;
            if ((i % skip) != 0)
                continue;
        }

        /* Copy base transform */
        float *dst = &v->visible_transforms[v->visible_count * 16];
        memcpy(dst, &v->base_transforms[i * 16], 16 * sizeof(float));

        /* Apply wind shear to the transform's Y-column entries.
         * This bends the blade tops in the wind direction. */
        double phase = v->wind_turbulence * (bx * 0.1 + bz * 0.07) + v->time * v->wind_speed;
        float wind_x = (float)(sin(phase) * v->wind_strength);
        float wind_z = (float)(cos(phase * 0.7) * v->wind_strength * 0.5);
        dst[1] += wind_x; /* shear X column by wind */
        dst[9] += wind_z; /* shear Z column by wind */

        v->visible_count++;
    }
}

/// @brief Submit the visible blade batch to the Canvas3D as one instanced draw. Temporarily
/// disables backface culling (grass renders both sides) and restores the previous state on exit.
/// No-op when called outside a frame, when the backend is missing, or when nothing is visible.
void rt_canvas3d_draw_vegetation(void *canvas_obj, void *veg_obj) {
    if (!canvas_obj || !veg_obj)
        return;
    rt_canvas3d *c = rt_canvas3d_checked_or_stack(canvas_obj);
    rt_vegetation3d *v =
        (rt_vegetation3d *)rt_g3d_checked_or_null(veg_obj, RT_G3D_VEGETATION3D_CLASS_ID);
    if (!c || !v)
        return;
    if (!c->in_frame || !c->backend || v->visible_count == 0)
        return;
    if (c->frame_is_2d) {
        rt_trap("Canvas3D.DrawVegetation: cannot draw vegetation during Begin2D/End");
        return;
    }

    rt_mesh3d *mesh = (rt_mesh3d *)v->blade_mesh;
    rt_material3d *mat = (rt_material3d *)v->blade_material;
    if (!mesh || mesh->vertex_count == 0 || !mat)
        return;

    /* Disable backface culling for grass (visible from both sides) */
    int8_t prev_cull = c->backface_cull;
    c->backface_cull = 0;

    rt_canvas3d_queue_instanced_batch(
        c, mesh, mat, v->visible_transforms, v->visible_count, NULL, 0);

    c->backface_cull = prev_cull;
}

#else
typedef int rt_graphics_disabled_tu_guard;
#endif /* VIPER_ENABLE_GRAPHICS */
