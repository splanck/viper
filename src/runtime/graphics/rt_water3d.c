//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_water3d.c
// Purpose: Animated water plane with sine-based waves.
//
// Key invariants:
//   - Grid resolution: configurable [8, 256], default 64x64 quads.
//   - Wave: height = amplitude * sin(freq * (x + z) - time * speed) (legacy).
//   - Gerstner multi-wave path sums up to WATER_MAX_WAVES superimposed waves.
//   - Normals computed analytically from wave derivative for smooth shading.
//   - Drawn with alpha-blended material; backface cull disabled around draw.
//
// Ownership/Lifetime:
//   - Water3D is GC-managed; finalizer releases the texture / normal-map /
//     env-map / mesh / material refs.
//
// Links: rt_water3d.h, rt_canvas3d.h
//
//===----------------------------------------------------------------------===//

#ifdef VIPER_ENABLE_GRAPHICS

#include "rt_water3d.h"
#include "rt_canvas3d.h"
#include "rt_canvas3d_internal.h"
#include "rt_heap.h"
#include "rt_pixels.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

extern void *rt_obj_new_i64(int64_t class_id, int64_t byte_size);
extern void rt_obj_set_finalizer(void *obj, void (*fn)(void *));
extern void rt_obj_retain_maybe(void *obj);
extern int32_t rt_obj_release_check0(void *p);
extern void rt_obj_free(void *p);
#include "rt_trap.h"
extern void *rt_mesh3d_new(void);
extern void rt_mesh3d_clear(void *m);
extern void rt_mesh3d_add_vertex(
    void *m, double x, double y, double z, double nx, double ny, double nz, double u, double v);
extern void rt_mesh3d_add_triangle(void *m, int64_t v0, int64_t v1, int64_t v2);
extern void rt_canvas3d_draw_mesh(void *canvas, void *mesh, void *transform, void *material);
extern void rt_canvas3d_draw_mesh_matrix(
    void *canvas, void *mesh, const double *transform, void *material);
extern void *rt_material3d_new_color(double r, double g, double b);
extern void rt_material3d_set_alpha(void *m, double a);
extern void rt_material3d_set_shininess(void *m, double s);
extern void rt_material3d_set_texture(void *m, void *tex);
extern void rt_material3d_set_normal_map(void *m, void *tex);
extern void rt_material3d_set_env_map(void *m, void *cubemap);
extern void rt_material3d_set_reflectivity(void *m, double r);
extern void rt_material3d_set_color(void *m, double r, double g, double b);

#define WATER_GRID 64
#define WATER_MAX_WAVES 8
#define WATER3D_SIZE_MAX 1000000.0
#define WATER3D_HEIGHT_ABS_MAX 1000000000000.0
#define WATER3D_PARAM_MAX 1000000.0

typedef struct {
    double dir[2]; /* normalized wave direction */
    double speed;
    double amplitude;
    double frequency; /* 2*PI / wavelength */
} water_wave_t;

typedef struct {
    void *vptr;
    double width, depth;
    double height;
    /* Legacy single-wave params (used when wave_count == 0) */
    double wave_speed, wave_amplitude, wave_frequency;
    double color[3];
    double alpha;
    double time;
    void *mesh;
    void *material;
    /* Phase A: texture/env map support */
    void *texture;       /* surface texture (Pixels) */
    void *normal_map;    /* wave normal map (Pixels) */
    void *env_map;       /* environment cubemap for reflections */
    double reflectivity; /* [0.0-1.0] */
    /* Phase B: Gerstner multi-wave */
    water_wave_t waves[WATER_MAX_WAVES];
    int32_t wave_count;
    int32_t resolution; /* grid resolution (default WATER_GRID) */
    int8_t mesh_dirty;
} rt_water3d;

/// @brief Validate @p obj as a Water3D handle and return its typed pointer (NULL on mismatch).
static rt_water3d *water3d_checked(void *obj) {
    return (rt_water3d *)rt_g3d_checked_or_null(obj, RT_G3D_WATER3D_CLASS_ID);
}

/// @brief Return non-zero when @p pixels is a live `Viper.Graphics.Pixels` handle.
static int water3d_is_pixels_handle(void *pixels) {
    return pixels && rt_obj_class_id(pixels) == RT_PIXELS_CLASS_ID;
}

/// @brief Drop one reference and zero the slot. Idempotent on null/empty slots.
static void water3d_release_ref(void **slot) {
    if (!slot || !*slot)
        return;
    if (rt_obj_release_check0(*slot))
        rt_obj_free(*slot);
    *slot = NULL;
}

/// @brief Replace `*slot` with `value`, retaining the new value first then releasing the old.
/// Order matters — without retain-before-release, assigning a slot's current value to itself
/// would briefly drop the refcount to 0 and free the object.
static void water3d_assign_ref(void **slot, void *value) {
    if (*slot == value)
        return;
    rt_obj_retain_maybe(value);
    water3d_release_ref(slot);
    *slot = value;
}

/// @brief Clamp a double to the `[0, 1]` range — used for water knobs like transparency,
///   reflectivity, and wave amplitude that are physical [0, 1] scalars.
static double water3d_clamp01(double value) {
    if (!isfinite(value))
        return 0.0;
    if (value < 0.0)
        return 0.0;
    if (value > 1.0)
        return 1.0;
    return value;
}

static double water3d_clamp_positive_or(double value, double fallback, double max_value) {
    if (!isfinite(value) || value <= 0.0)
        return fallback;
    if (value > max_value)
        return max_value;
    return value;
}

static double water3d_clamp_abs_or(double value, double fallback, double max_abs) {
    if (!isfinite(value))
        return fallback;
    if (value > max_abs)
        return max_abs;
    if (value < -max_abs)
        return -max_abs;
    return value;
}

static double water3d_clamp_nonnegative(double value, double max_value) {
    if (!isfinite(value) || value < 0.0)
        return 0.0;
    if (value > max_value)
        return max_value;
    return value;
}

/// @brief GC finalizer: release every retained graphics resource (textures, mesh, material).
static void water3d_finalizer(void *obj) {
    rt_water3d *w = (rt_water3d *)obj;
    water3d_release_ref(&w->texture);
    water3d_release_ref(&w->normal_map);
    water3d_release_ref(&w->env_map);
    water3d_release_ref(&w->mesh);
    water3d_release_ref(&w->material);
}

/// @brief Create a new water surface with animated wave simulation.
/// @details Creates a grid mesh that deforms each frame using sinusoidal or
///          Gerstner wave functions. The water supports configurable color,
///          transparency, surface textures, normal maps, and environment-map
///          reflections. The mesh is lazily built on first draw.
/// @param width World-space width of the water plane (X axis).
/// @param depth World-space depth of the water plane (Z axis).
/// @return Opaque water handle, or NULL on failure.
void *rt_water3d_new(double width, double depth) {
    width = water3d_clamp_positive_or(width, 1.0, WATER3D_SIZE_MAX);
    depth = water3d_clamp_positive_or(depth, 1.0, WATER3D_SIZE_MAX);
    rt_water3d *w =
        (rt_water3d *)rt_obj_new_i64(RT_G3D_WATER3D_CLASS_ID, (int64_t)sizeof(rt_water3d));
    if (!w) {
        rt_trap("Water3D.New: allocation failed");
        return NULL;
    }
    w->vptr = NULL;
    w->width = width;
    w->depth = depth;
    w->height = 0.0;
    w->wave_speed = 2.0;
    w->wave_amplitude = 0.15;
    w->wave_frequency = 1.5;
    w->color[0] = 0.1;
    w->color[1] = 0.3;
    w->color[2] = 0.6;
    w->alpha = 0.5;
    w->time = 0.0;
    w->mesh = NULL;
    w->material = NULL;
    w->texture = NULL;
    w->normal_map = NULL;
    w->env_map = NULL;
    w->reflectivity = 0.0;
    w->wave_count = 0;
    w->resolution = WATER_GRID;
    w->mesh_dirty = 1;
    rt_obj_set_finalizer(w, water3d_finalizer);
    return w;
}

/// @brief Set the base Y-coordinate (world height) of the water plane.
void rt_water3d_set_height(void *obj, double y) {
    rt_water3d *w = water3d_checked(obj);
    if (w) {
        w->height = water3d_clamp_abs_or(y, w->height, WATER3D_HEIGHT_ABS_MAX);
        w->mesh_dirty = 1;
    }
}

/// @brief Configure the sinusoidal wave animation parameters.
void rt_water3d_set_wave_params(void *obj, double speed, double amplitude, double frequency) {
    rt_water3d *w = water3d_checked(obj);
    if (!w)
        return;
    w->wave_speed = water3d_clamp_abs_or(speed, 0.0, WATER3D_PARAM_MAX);
    w->wave_amplitude = water3d_clamp_nonnegative(amplitude, WATER3D_PARAM_MAX);
    w->wave_frequency = water3d_clamp_abs_or(frequency, 0.0, WATER3D_PARAM_MAX);
    w->mesh_dirty = 1;
}

/// @brief Set the base tint color and transparency of the water surface.
void rt_water3d_set_color(void *obj, double r, double g, double b, double a) {
    rt_water3d *w = water3d_checked(obj);
    if (!w)
        return;
    w->color[0] = water3d_clamp01(r);
    w->color[1] = water3d_clamp01(g);
    w->color[2] = water3d_clamp01(b);
    w->alpha = water3d_clamp01(a);
}

/// @brief Set surface texture for water.
void rt_water3d_set_texture(void *obj, void *pixels) {
    rt_water3d *w = water3d_checked(obj);
    if (!w)
        return;
    if (pixels && !water3d_is_pixels_handle(pixels))
        return;
    water3d_assign_ref(&w->texture, pixels);
    if (w->material)
        rt_material3d_set_texture(w->material, pixels);
}

/// @brief Set normal map for wave detail.
void rt_water3d_set_normal_map(void *obj, void *pixels) {
    rt_water3d *w = water3d_checked(obj);
    if (!w)
        return;
    if (pixels && !water3d_is_pixels_handle(pixels))
        return;
    water3d_assign_ref(&w->normal_map, pixels);
    if (w->material)
        rt_material3d_set_normal_map(w->material, pixels);
}

/// @brief Set environment cubemap for reflections.
void rt_water3d_set_env_map(void *obj, void *cubemap) {
    rt_water3d *w = water3d_checked(obj);
    if (!w)
        return;
    if (cubemap && !rt_g3d_has_class(cubemap, RT_G3D_CUBEMAP3D_CLASS_ID))
        return;
    water3d_assign_ref(&w->env_map, cubemap);
    if (w->material)
        rt_material3d_set_env_map(w->material, cubemap);
}

/// @brief Set reflectivity [0.0-1.0] for environment mapping.
void rt_water3d_set_reflectivity(void *obj, double r) {
    rt_water3d *w = water3d_checked(obj);
    if (!w)
        return;
    w->reflectivity = water3d_clamp01(r);
    if (w->material)
        rt_material3d_set_reflectivity(w->material, w->env_map ? w->reflectivity : 0.0);
}

/// @brief Set grid resolution (clamped to [8, 256]).
void rt_water3d_set_resolution(void *obj, int64_t res) {
    rt_water3d *w = water3d_checked(obj);
    if (!w)
        return;
    if (res < 8)
        res = 8;
    if (res > 256)
        res = 256;
    if (w->resolution != (int32_t)res) {
        w->resolution = (int32_t)res;
        w->mesh_dirty = 1;
    }
}

/// @brief Add a Gerstner wave to the water.
void rt_water3d_add_wave(
    void *obj, double dirX, double dirZ, double speed, double amplitude, double wavelength) {
    rt_water3d *w = water3d_checked(obj);
    if (!w)
        return;
    if (w->wave_count >= WATER_MAX_WAVES)
        return;
    if (!isfinite(dirX) || !isfinite(dirZ) || !isfinite(speed) || !isfinite(amplitude) ||
        !isfinite(wavelength) || amplitude < 0.0 || wavelength <= 1e-6)
        return;
    /* Normalize direction */
    double len = sqrt(dirX * dirX + dirZ * dirZ);
    if (!isfinite(len) || len < 1e-8)
        return;
    water_wave_t *wv = &w->waves[w->wave_count];
    wv->dir[0] = dirX / len;
    wv->dir[1] = dirZ / len;
    wv->speed = water3d_clamp_abs_or(speed, 0.0, WATER3D_PARAM_MAX);
    wv->amplitude = water3d_clamp_nonnegative(amplitude, WATER3D_PARAM_MAX);
    wavelength = water3d_clamp_positive_or(wavelength, 1.0, WATER3D_PARAM_MAX);
    wv->frequency = 6.283185307 / wavelength;
    w->wave_count++;
    w->mesh_dirty = 1;
}

/// @brief Remove all Gerstner waves.
void rt_water3d_clear_waves(void *obj) {
    rt_water3d *w = water3d_checked(obj);
    if (w) {
        w->wave_count = 0;
        w->mesh_dirty = 1;
    }
}

/// @brief Advance the water simulation by `dt` seconds and regenerate the surface mesh.
/// @details Rebuilds the (grid+1)x(grid+1) vertex grid and the 2*grid*grid triangle list
///          each frame. Vertex heights come from either the Gerstner multi-wave sum (when
///          waves have been registered via `rt_water3d_add_wave`) or a single legacy sine
///          wave. Per-vertex normals are derived analytically from the wave derivative
///          (dy/dx, dy/dz) rather than numerically, keeping shading smooth across the
///          grid independent of resolution.
///
///          Mesh and material are created lazily on the first update and then reused:
///          `rt_mesh3d_clear` resets contents in place to avoid per-frame GC churn, and
///          the material's color/alpha/texture/env-map bindings are pushed through every
///          update so live property edits take effect on the next frame. Reflectivity is
///          forced to 0 when no environment cubemap is bound so the shader skips the
///          reflection path.
///
///          No work is done when `dt <= 0` — this lets callers pause the simulation by
///          passing dt=0 without redundant mesh rebuilds.
/// @param obj Opaque water handle from `rt_water3d_new` (no-op when NULL).
/// @param dt Elapsed seconds since the previous update (must be > 0 to apply).
void rt_water3d_update(void *obj, double dt) {
    rt_water3d *w = water3d_checked(obj);
    if (!w || !isfinite(dt) || dt < 0.0)
        return;
    if (dt == 0.0 && !w->mesh_dirty && w->mesh && w->material)
        return;
    w->time += dt;
    if (!isfinite(w->time))
        w->time = 0.0;

    /* Regenerate mesh with new wave positions (reuse allocation to avoid GC pressure) */
    if (!w->mesh)
        w->mesh = rt_mesh3d_new();
    else
        rt_mesh3d_clear(w->mesh);
    if (!w->mesh)
        return;
    int32_t grid = w->resolution;
    double hx = w->width * 0.5, hz = w->depth * 0.5;
    double step_x = w->width / grid;
    double step_z = w->depth / grid;

    /* Vertices */
    for (int gz = 0; gz <= grid; gz++) {
        for (int gx = 0; gx <= grid; gx++) {
            double x = -hx + gx * step_x;
            double z = -hz + gz * step_z;
            double y = w->height;
            double dydx = 0.0, dydz = 0.0;

            if (w->wave_count > 0) {
                /* Gerstner multi-wave sum. Standard form is A·sin(k·x − ω·t):
                 * the minus sign on the time term makes crests propagate in
                 * +direction of `dir`. Previously this used `+ time*speed`,
                 * so waves visually travelled opposite the user-specified
                 * direction. */
                for (int32_t wi = 0; wi < w->wave_count; wi++) {
                    water_wave_t *wv = &w->waves[wi];
                    double dot = wv->dir[0] * x + wv->dir[1] * z;
                    double phase = wv->frequency * dot - w->time * wv->speed;
                    y += wv->amplitude * sin(phase);
                    double dc = wv->amplitude * wv->frequency * cos(phase);
                    dydx += dc * wv->dir[0];
                    dydz += dc * wv->dir[1];
                }
            } else {
                /* Legacy single sine wave — keep same sign convention as above. */
                double phase = w->wave_frequency * (x + z) - w->time * w->wave_speed;
                y += w->wave_amplitude * sin(phase);
                double dc = w->wave_amplitude * w->wave_frequency * cos(phase);
                dydx = dc;
                dydz = dc;
            }

            /* Normal from wave derivatives */
            double nx = -dydx, ny = 1.0, nz = -dydz;
            double nlen = sqrt(nx * nx + ny * ny + nz * nz);
            if (isfinite(nlen) && nlen > 1e-8) {
                nx /= nlen;
                ny /= nlen;
                nz /= nlen;
            } else {
                nx = 0.0;
                ny = 1.0;
                nz = 0.0;
            }

            double u = (double)gx / grid;
            double v = (double)gz / grid;
            rt_mesh3d_add_vertex(w->mesh, x, y, z, nx, ny, nz, u, v);
        }
    }

    /* Triangles */
    int row = grid + 1;
    for (int gz = 0; gz < grid; gz++) {
        for (int gx = 0; gx < grid; gx++) {
            int base = gz * row + gx;
            rt_mesh3d_add_triangle(w->mesh, base, base + row, base + 1);
            rt_mesh3d_add_triangle(w->mesh, base + 1, base + row, base + row + 1);
        }
    }
    if (((rt_mesh3d *)w->mesh)->build_failed) {
        rt_mesh3d_clear(w->mesh);
        w->mesh_dirty = 1;
        return;
    }

    /* Update material — create on first use, update properties every frame */
    if (!w->material) {
        w->material = rt_material3d_new_color(w->color[0], w->color[1], w->color[2]);
        if (!w->material)
            return;
        rt_material3d_set_shininess(w->material, 128.0);
    } else {
        rt_material3d_set_color(w->material, w->color[0], w->color[1], w->color[2]);
    }
    rt_material3d_set_alpha(w->material, w->alpha);

    /* Phase A: wire texture/normalmap/envmap to material */
    rt_material3d_set_texture(w->material, w->texture);
    rt_material3d_set_normal_map(w->material, w->normal_map);
    rt_material3d_set_env_map(w->material, w->env_map);
    rt_material3d_set_reflectivity(w->material, w->env_map ? w->reflectivity : 0.0);
    w->mesh_dirty = 0;
}

/// @brief Draw the animated water surface through the 3D canvas.
/// @details Emits the current water mesh at the world origin (the grid is already built
///          in world space around the origin by `rt_water3d_update`, so an identity model
///          matrix is correct — reusing a static table avoids allocating a Transform3D).
///          Backface culling is temporarily disabled so the underside of waves and any
///          submerged camera angle still show geometry, then restored to the canvas's
///          prior setting so this call is transparent to other draws. Does nothing if
///          the mesh/material haven't been built yet (i.e. `update` was never called) or
///          if `canvas` / `obj` are NULL. The `camera` argument is accepted for API
///          symmetry with other draw functions but currently unused.
void rt_canvas3d_draw_water(void *canvas, void *obj, void *camera) {
    rt_canvas3d *c = rt_canvas3d_checked_or_stack(canvas);
    rt_water3d *w = water3d_checked(obj);
    if (!c || !w)
        return;
    (void)camera;
    if (!w->mesh || !w->material || w->mesh_dirty)
        rt_water3d_update(w, 0.0);
    if (!w->mesh || !w->material)
        return;

    /* Draw with backface culling disabled (water visible from both sides) */
    extern void rt_canvas3d_set_backface_cull(void *canvas, int8_t enabled);
    static const double identity[16] = {
        1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0,
        0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0,
    };
    int8_t restore_backface_cull = c->backface_cull;
    rt_canvas3d_set_backface_cull(c, 0);
    rt_canvas3d_draw_mesh_matrix(c, w->mesh, identity, w->material);
    rt_canvas3d_set_backface_cull(c, restore_backface_cull);
}

#else
typedef int rt_graphics_disabled_tu_guard;
#endif /* VIPER_ENABLE_GRAPHICS */
