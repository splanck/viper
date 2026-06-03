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

#define VEGETATION3D_BLADE_DIM_MAX 1000000.0
#define VEGETATION3D_TERRAIN_EXTENT_MAX 1000000.0
#define VEGETATION3D_WIND_PARAM_MAX 1000000.0
#define VEGETATION3D_TIME_MAX 1000000.0
#define VEGETATION3D_DT_MAX 1.0
#define VEGETATION3D_MAX_BLADES 2000000
#define VEGETATION3D_TWO_PI 6.28318530717958647692

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

/// @brief Clamp a positive vegetation scalar to a bounded finite range.
static double vegetation_positive_or(double value, double fallback, double max_value) {
    return rt_world3d_clamp_positive_or(value, fallback, max_value);
}

/// @brief Clamp a non-negative vegetation scalar to a bounded finite range.
static double vegetation_nonnegative_or(double value, double max_value) {
    return rt_world3d_clamp_nonnegative(value, max_value);
}

/// @brief Clamp a signed vegetation scalar to a bounded finite range.
static double vegetation_abs_or(double value, double fallback, double max_abs) {
    return rt_world3d_clamp_abs_or(value, fallback, max_abs);
}

/// @brief Drop one GC reference held in `*slot` and clear the slot. NULL-safe.
static void vegetation3d_release_ref(void **slot) {
    if (!slot || !*slot)
        return;
    if (rt_obj_release_check0(*slot))
        rt_obj_free(*slot);
    *slot = NULL;
}

/// @brief Return true when @p pixels is a live Pixels object.
static int vegetation3d_is_pixels_handle(void *pixels) {
    return rt_pixels_checked_impl_or_null(pixels) != NULL;
}

/// @brief Release a retained Pixels slot only if it still points at Pixels.
static void vegetation3d_release_pixels_slot(void **slot) {
    if (!slot || !*slot)
        return;
    if (!vegetation3d_is_pixels_handle(*slot)) {
        *slot = NULL;
        return;
    }
    vegetation3d_release_ref(slot);
}

/// @brief Release a retained Mesh3D slot only if it still points at Mesh3D.
static void vegetation3d_release_mesh_slot(void **slot) {
    if (!slot || !*slot)
        return;
    if (!rt_g3d_has_class(*slot, RT_G3D_MESH3D_CLASS_ID)) {
        *slot = NULL;
        return;
    }
    vegetation3d_release_ref(slot);
}

/// @brief Release a retained Material3D slot only if it still points at Material3D.
static void vegetation3d_release_material_slot(void **slot) {
    if (!slot || !*slot)
        return;
    if (!rt_g3d_has_class(*slot, RT_G3D_MATERIAL3D_CLASS_ID)) {
        *slot = NULL;
        return;
    }
    vegetation3d_release_ref(slot);
}

/// @brief Retain-then-release swap into a Pixels slot. Safe on self-assign.
static void vegetation3d_assign_pixels_ref(void **slot, void *value) {
    if (!slot)
        return;
    if (value && !vegetation3d_is_pixels_handle(value))
        value = NULL;
    if (*slot == value)
        return;
    if (value)
        rt_obj_retain_maybe(value);
    vegetation3d_release_pixels_slot(slot);
    *slot = value;
}

/// @brief Return a Mesh3D slot only when its private handle is still valid.
static rt_mesh3d *vegetation3d_mesh_ref(void *ref) {
    return rt_g3d_has_class(ref, RT_G3D_MESH3D_CLASS_ID) ? (rt_mesh3d *)ref : NULL;
}

/// @brief Return a Material3D slot only when its private handle is still valid.
static rt_material3d *vegetation3d_material_ref(void *ref) {
    return rt_g3d_has_class(ref, RT_G3D_MATERIAL3D_CLASS_ID) ? (rt_material3d *)ref : NULL;
}

static void vegetation3d_enable_double_sided_material(rt_material3d *mat) {
    if (!mat)
        return;
#if defined(RT_G3D_ALLOW_STACK_FIXTURES)
    (void)mat;
#else
    mat->double_sided = 1;
#endif
}

/// @brief Clear corrupted retained resource slots without releasing unrelated objects.
static void vegetation3d_repair_resource_handles(rt_vegetation3d *v) {
    if (!v)
        return;
    if (v->density_map && !vegetation3d_is_pixels_handle(v->density_map))
        vegetation3d_release_pixels_slot(&v->density_map);
    if (v->blade_mesh && !vegetation3d_mesh_ref(v->blade_mesh))
        vegetation3d_release_mesh_slot(&v->blade_mesh);
    if (v->blade_material && !vegetation3d_material_ref(v->blade_material))
        vegetation3d_release_material_slot(&v->blade_material);
}

/// @brief Free all population buffers and reset the population/visible counters.
static void vegetation3d_clear_population_buffers(rt_vegetation3d *v) {
    if (!v)
        return;
    free(v->base_transforms);
    free(v->positions);
    free(v->visible_transforms);
    v->base_transforms = NULL;
    v->positions = NULL;
    v->visible_transforms = NULL;
    v->total_count = 0;
    v->capacity = 0;
    v->visible_count = 0;
    v->visible_capacity = 0;
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
    if (!v)
        return;
    vegetation3d_clear_population_buffers(v);
    vegetation3d_release_pixels_slot(&v->density_map);
    vegetation3d_release_mesh_slot(&v->blade_mesh);
    vegetation3d_release_material_slot(&v->blade_material);
}

/// @brief Build the cross-billboard blade mesh (2 perpendicular quads).
static void build_blade_mesh(void *mesh, double w, double h) {
    if (!mesh)
        return;
    w = vegetation_positive_or(w, 0.4, VEGETATION3D_BLADE_DIM_MAX);
    h = vegetation_positive_or(h, 1.2, VEGETATION3D_BLADE_DIM_MAX);
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
    if (!state)
        return 0u;
    *state = *state * 1103515245u + 12345u;
    return *state;
}

/// @brief Repair count/buffer invariants before update/draw work touches flat arrays.
static int vegetation3d_repair_state(rt_vegetation3d *v) {
    if (!v)
        return 0;
    vegetation3d_repair_resource_handles(v);
    if (v->capacity < 0)
        v->capacity = 0;
    if (!v->base_transforms || !v->positions || v->capacity == 0) {
        free(v->base_transforms);
        free(v->positions);
        v->base_transforms = NULL;
        v->positions = NULL;
        v->total_count = 0;
        v->capacity = 0;
    } else {
        if (v->total_count < 0)
            v->total_count = 0;
        if (v->total_count > v->capacity)
            v->total_count = v->capacity;
    }
    if (v->visible_capacity < 0)
        v->visible_capacity = 0;
    if (!v->visible_transforms || v->visible_capacity == 0) {
        free(v->visible_transforms);
        v->visible_transforms = NULL;
        v->visible_count = 0;
        v->visible_capacity = 0;
    } else {
        if (v->visible_count < 0)
            v->visible_count = 0;
        if (v->visible_count > v->visible_capacity)
            v->visible_count = v->visible_capacity;
    }
    return 1;
}

/// @brief True if a transform matrix is finite and inside the vegetation world range.
static int vegetation3d_matrix_is_drawable(const float *m) {
    if (!m)
        return 0;
    for (int i = 0; i < 16; i++) {
        if (!isfinite(m[i]) || fabsf(m[i]) > (float)VEGETATION3D_TERRAIN_EXTENT_MAX)
            return 0;
    }
    return 1;
}

/// @brief Remove invalid visible matrices before handing the batch to Canvas3D.
static void vegetation3d_compact_visible(rt_vegetation3d *v) {
    if (!v || !v->visible_transforms || v->visible_count <= 0) {
        if (v)
            v->visible_count = 0;
        return;
    }
    int32_t out = 0;
    for (int32_t i = 0; i < v->visible_count; i++) {
        float *src = &v->visible_transforms[(size_t)i * 16u];
        if (!vegetation3d_matrix_is_drawable(src))
            continue;
        if (out != i)
            memcpy(&v->visible_transforms[(size_t)out * 16u], src, 16u * sizeof(float));
        out++;
    }
    v->visible_count = out;
}

/// @brief Construct a Vegetation3D system. Allocates the shared cross-billboard blade mesh and
/// an unlit textured material (if `blade_texture` is non-NULL — opacity comes from the texture).
/// Defaults: 0.4×1.2 blades with 30% size variation, wind speed 2.0 / strength 0.15 / turbulence
/// 0.5, LOD near=40 / far=100 world units. Traps on allocation failure.
void *rt_vegetation3d_new(void *blade_texture) {
    if (blade_texture && !vegetation3d_is_pixels_handle(blade_texture)) {
        rt_trap("Vegetation3D.New: blade_texture must be Pixels");
        return NULL;
    }
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
        vegetation3d_release_mesh_slot(&v->blade_mesh);
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
    vegetation3d_assign_pixels_ref(&v->density_map, pixels);
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
    v->wind_speed = vegetation_abs_or(speed, 0.0, VEGETATION3D_WIND_PARAM_MAX);
    v->wind_strength = vegetation_nonnegative_or(strength, VEGETATION3D_WIND_PARAM_MAX);
    v->wind_turbulence = vegetation_nonnegative_or(turbulence, VEGETATION3D_WIND_PARAM_MAX);
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
    near_dist = vegetation_nonnegative_or(near_dist, VEGETATION3D_TERRAIN_EXTENT_MAX);
    far_dist = vegetation_nonnegative_or(far_dist, VEGETATION3D_TERRAIN_EXTENT_MAX);
    if (near_dist <= 0.0)
        near_dist = 40.0;
    if (far_dist <= 0.0)
        far_dist = 100.0;
    if (far_dist <= near_dist + 1e-6)
        far_dist = near_dist + 1.0;
    if (far_dist > VEGETATION3D_TERRAIN_EXTENT_MAX)
        far_dist = VEGETATION3D_TERRAIN_EXTENT_MAX;
    if (near_dist >= far_dist)
        near_dist = far_dist > 1.0 ? far_dist - 1.0 : 0.0;
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
    width = vegetation_positive_or(width, 0.4, VEGETATION3D_BLADE_DIM_MAX);
    height = vegetation_positive_or(height, 1.2, VEGETATION3D_BLADE_DIM_MAX);
    variation = vegetation_finite_or(variation, 0.0);
    if (variation < 0.0)
        variation = 0.0;
    if (variation > 1.0)
        variation = 1.0;
    v->blade_width = width;
    v->blade_height = height;
    v->size_variation = variation;
    /* Rebuild blade mesh with new size */
    if (v->blade_mesh && !vegetation3d_mesh_ref(v->blade_mesh))
        vegetation3d_release_mesh_slot(&v->blade_mesh);
    if (!v->blade_mesh) {
        v->blade_mesh = rt_mesh3d_new();
        if (!v->blade_mesh) {
            rt_trap("Vegetation3D.SetBladeSize: blade mesh allocation failed");
            return;
        }
    } else {
        rt_mesh3d_clear(v->blade_mesh);
    }
    build_blade_mesh(v->blade_mesh, width, height);
}

/// @brief Build a row-major 4x4 transform: translate(x,y,z) * rotateY(angle) * scale(s).
static void build_transform(float *out, double x, double y, double z, double angle, double s) {
    if (!out)
        return;
    x = vegetation_abs_or(x, 0.0, VEGETATION3D_TERRAIN_EXTENT_MAX);
    y = vegetation_abs_or(y, 0.0, VEGETATION3D_TERRAIN_EXTENT_MAX);
    z = vegetation_abs_or(z, 0.0, VEGETATION3D_TERRAIN_EXTENT_MAX);
    angle = vegetation_abs_or(angle, 0.0, VEGETATION3D_WIND_PARAM_MAX);
    s = vegetation_positive_or(s, 1.0, VEGETATION3D_BLADE_DIM_MAX);
    angle = fmod(angle, VEGETATION3D_TWO_PI);
    if (!isfinite(angle))
        angle = 0.0;
    double ca = cos(angle), sa = sin(angle);
    if (!isfinite(ca))
        ca = 1.0;
    if (!isfinite(sa))
        sa = 0.0;
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
    vegetation3d_repair_resource_handles(v);
    if (count <= 0) {
        vegetation3d_clear_population_buffers(v);
        return;
    }
    if (count > VEGETATION3D_MAX_BLADES) {
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

    double sx = vegetation_positive_or(tv->scale[0], 1.0, VEGETATION3D_TERRAIN_EXTENT_MAX);
    double sz = vegetation_positive_or(tv->scale[2], 1.0, VEGETATION3D_TERRAIN_EXTENT_MAX);
    double tw = (double)tv->width * sx;
    double td = (double)tv->depth * sz;
    if (tw > VEGETATION3D_TERRAIN_EXTENT_MAX)
        tw = VEGETATION3D_TERRAIN_EXTENT_MAX;
    if (td > VEGETATION3D_TERRAIN_EXTENT_MAX)
        td = VEGETATION3D_TERRAIN_EXTENT_MAX;
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
    float *new_base_transforms = (float *)calloc(1, base_bytes);
    float *new_positions = (float *)calloc(1, pos_bytes);
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
            if (density <= 0)
                continue;
            if (density < 255 && (int32_t)roll >= density)
                continue; /* skip based on density */
        }

        double wy = rt_terrain3d_get_height_at(terrain, wx, wz);
        wy = vegetation_abs_or(wy, 0.0, VEGETATION3D_TERRAIN_EXTENT_MAX);

        /* Random Y rotation + scale variation */
        double angle = ((double)(lcg_next(&rng) & 0xFFFF) / 65535.0) * 6.283185307;
        double scale_var =
            1.0 + (((double)(lcg_next(&rng) & 0xFFFF) / 65535.0) - 0.5) * 2.0 * v->size_variation;
        scale_var = vegetation_positive_or(scale_var, 1.0, VEGETATION3D_BLADE_DIM_MAX);
        if (scale_var < 0.01)
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
    if (!vegetation3d_repair_state(v))
        return;
    if (dt > VEGETATION3D_DT_MAX)
        dt = VEGETATION3D_DT_MAX;
    camX = vegetation_abs_or(camX, 0.0, VEGETATION3D_TERRAIN_EXTENT_MAX);
    camZ = vegetation_abs_or(camZ, 0.0, VEGETATION3D_TERRAIN_EXTENT_MAX);
    v->time += dt;
    if (!isfinite(v->time))
        v->time = 0.0;
    if (v->time > VEGETATION3D_TIME_MAX)
        v->time = fmod(v->time, VEGETATION3D_TIME_MAX);

    if (v->total_count <= 0 || !v->base_transforms || !v->positions) {
        v->visible_count = 0;
        return;
    }

    float lod_near =
        (float)vegetation_nonnegative_or((double)v->lod_near, VEGETATION3D_TERRAIN_EXTENT_MAX);
    float lod_far =
        (float)vegetation_nonnegative_or((double)v->lod_far, VEGETATION3D_TERRAIN_EXTENT_MAX);
    if (lod_far <= lod_near)
        lod_far = lod_near + 1.0f;
    double wind_speed = vegetation_abs_or(v->wind_speed, 0.0, VEGETATION3D_WIND_PARAM_MAX);
    double wind_strength = vegetation_nonnegative_or(v->wind_strength, VEGETATION3D_WIND_PARAM_MAX);
    double wind_turbulence =
        vegetation_nonnegative_or(v->wind_turbulence, VEGETATION3D_WIND_PARAM_MAX);

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
        if (!isfinite(bx) || !isfinite(bz))
            continue;

        /* Distance to camera (XZ plane) */
        double dx = (double)bx - camX;
        double dz = (double)bz - camZ;
        double dist2 = dx * dx + dz * dz;
        if (!isfinite(dist2))
            continue;
        /* Hard cull beyond far distance using squared distance first, so blades
         * outside the LOD range never pay for the sqrt. The post-sqrt check
         * below still authoritatively handles a negative/degenerate lod_far. */
        if (lod_far >= 0.0f && dist2 > (double)lod_far * (double)lod_far)
            continue;
        float dist = (float)sqrt(dist2);
        if (!isfinite(dist))
            continue;
        if (dist > lod_far)
            continue;

        /* Progressive thinning between near and far */
        if (dist > lod_near) {
            float denom = lod_far - lod_near;
            float t = denom > 1e-6f ? (dist - lod_near) / denom : 1.0f;
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
        if (!vegetation3d_matrix_is_drawable(dst))
            continue;

        /* Apply wind shear to the transform's Y-column entries.
         * This bends the blade tops in the wind direction. */
        double phase = wind_turbulence * (bx * 0.1 + bz * 0.07) + v->time * wind_speed;
        if (!isfinite(phase))
            phase = 0.0;
        phase = fmod(phase, VEGETATION3D_TWO_PI);
        if (!isfinite(phase))
            phase = 0.0;
        float wind_x = (float)(sin(phase) * wind_strength);
        float wind_z = (float)(cos(phase * 0.7) * wind_strength * 0.5);
        if (!isfinite(wind_x))
            wind_x = 0.0f;
        if (!isfinite(wind_z))
            wind_z = 0.0f;
        /* Bend the blade's up (Y) axis toward the wind. In this row-major 4x4,
         * indices 1 and 9 are the X and Z components of the second (Y) column,
         * so adding to them leans the blade top along world X and Z. */
        dst[1] += wind_x;
        dst[9] += wind_z;
        if (!vegetation3d_matrix_is_drawable(dst))
            continue;

        v->visible_count++;
    }
}

/// @brief Submit the visible blade batch to the Canvas3D as one instanced draw.
/// @details Marks the blade material double-sided so grass renders from both sides without
///          mutating canvas-level culling state. No-op when called outside a frame, when the
///          backend is missing, or when nothing is visible.
void rt_canvas3d_draw_vegetation(void *canvas_obj, void *veg_obj) {
    if (!canvas_obj || !veg_obj)
        return;
    rt_canvas3d *c = rt_canvas3d_checked_or_stack(canvas_obj);
    rt_vegetation3d *v =
        (rt_vegetation3d *)rt_g3d_checked_or_null(veg_obj, RT_G3D_VEGETATION3D_CLASS_ID);
    if (!c || !v)
        return;
    if (!vegetation3d_repair_state(v))
        return;
    if (!c->in_frame || !c->backend || v->visible_count <= 0)
        return;
    if (c->frame_is_2d) {
        rt_trap("Canvas3D.DrawVegetation: cannot draw vegetation during Begin2D/End");
        return;
    }

    rt_mesh3d *mesh = vegetation3d_mesh_ref(v->blade_mesh);
    rt_material3d *mat = vegetation3d_material_ref(v->blade_material);
    vegetation3d_compact_visible(v);
    if (!mesh || mesh->vertex_count == 0 || !mat || !v->visible_transforms ||
        v->visible_count <= 0 || v->visible_count > v->visible_capacity)
        return;

    vegetation3d_enable_double_sided_material(mat);
    rt_canvas3d_queue_instanced_batch(
        c, mesh, mat, v->visible_transforms, v->visible_count, NULL, 0);
}

#else
typedef int rt_graphics_disabled_tu_guard;
#endif /* VIPER_ENABLE_GRAPHICS */
