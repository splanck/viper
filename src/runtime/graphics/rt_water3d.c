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
//   - Grid resolution: 32x32 quads for smooth wave appearance.
//   - Wave: height = amplitude * sin(freq * (x + z) + time * speed).
//   - Normals computed from wave derivative for proper lighting.
//   - Drawn with alpha-blended material for transparency.
//
// Links: rt_water3d.h, rt_canvas3d.h
//
//===----------------------------------------------------------------------===//

#ifdef VIPER_ENABLE_GRAPHICS

#include "rt_water3d.h"
#include "rt_canvas3d.h"
#include "rt_canvas3d_internal.h"

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
extern void *rt_mat4_identity(void);
extern void rt_canvas3d_draw_mesh(void *canvas, void *mesh, void *transform, void *material);
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
} rt_water3d;

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

static double water3d_clamp01(double value) {
    if (value < 0.0)
        return 0.0;
    if (value > 1.0)
        return 1.0;
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
    rt_water3d *w = (rt_water3d *)rt_obj_new_i64(0, (int64_t)sizeof(rt_water3d));
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
    rt_obj_set_finalizer(w, water3d_finalizer);
    return w;
}

/// @brief Set the base Y-coordinate (world height) of the water plane.
void rt_water3d_set_height(void *obj, double y) {
    if (obj)
        ((rt_water3d *)obj)->height = y;
}

/// @brief Configure the sinusoidal wave animation parameters.
void rt_water3d_set_wave_params(void *obj, double speed, double amplitude, double frequency) {
    if (!obj)
        return;
    rt_water3d *w = (rt_water3d *)obj;
    w->wave_speed = speed;
    w->wave_amplitude = amplitude;
    w->wave_frequency = frequency;
}

/// @brief Set the base tint color and transparency of the water surface.
void rt_water3d_set_color(void *obj, double r, double g, double b, double a) {
    if (!obj)
        return;
    rt_water3d *w = (rt_water3d *)obj;
    w->color[0] = r;
    w->color[1] = g;
    w->color[2] = b;
    w->alpha = water3d_clamp01(a);
}

/// @brief Set surface texture for water.
void rt_water3d_set_texture(void *obj, void *pixels) {
    if (!obj)
        return;
    rt_water3d *w = (rt_water3d *)obj;
    water3d_assign_ref(&w->texture, pixels);
    if (w->material)
        rt_material3d_set_texture(w->material, pixels);
}

/// @brief Set normal map for wave detail.
void rt_water3d_set_normal_map(void *obj, void *pixels) {
    if (!obj)
        return;
    rt_water3d *w = (rt_water3d *)obj;
    water3d_assign_ref(&w->normal_map, pixels);
    if (w->material)
        rt_material3d_set_normal_map(w->material, pixels);
}

/// @brief Set environment cubemap for reflections.
void rt_water3d_set_env_map(void *obj, void *cubemap) {
    if (!obj)
        return;
    rt_water3d *w = (rt_water3d *)obj;
    water3d_assign_ref(&w->env_map, cubemap);
    if (w->material)
        rt_material3d_set_env_map(w->material, cubemap);
}

/// @brief Set reflectivity [0.0-1.0] for environment mapping.
void rt_water3d_set_reflectivity(void *obj, double r) {
    if (!obj)
        return;
    rt_water3d *w = (rt_water3d *)obj;
    w->reflectivity = water3d_clamp01(r);
    if (w->material)
        rt_material3d_set_reflectivity(w->material, w->env_map ? w->reflectivity : 0.0);
}

/// @brief Set grid resolution (clamped to [8, 256]).
void rt_water3d_set_resolution(void *obj, int64_t res) {
    if (!obj)
        return;
    rt_water3d *w = (rt_water3d *)obj;
    if (res < 8)
        res = 8;
    if (res > 256)
        res = 256;
    w->resolution = (int32_t)res;
}

/// @brief Add a Gerstner wave to the water.
void rt_water3d_add_wave(
    void *obj, double dirX, double dirZ, double speed, double amplitude, double wavelength) {
    if (!obj)
        return;
    rt_water3d *w = (rt_water3d *)obj;
    if (w->wave_count >= WATER_MAX_WAVES)
        return;
    /* Normalize direction */
    double len = sqrt(dirX * dirX + dirZ * dirZ);
    if (len < 1e-8)
        return;
    water_wave_t *wv = &w->waves[w->wave_count];
    wv->dir[0] = dirX / len;
    wv->dir[1] = dirZ / len;
    wv->speed = speed;
    wv->amplitude = amplitude;
    wv->frequency = (wavelength > 0.001) ? (6.283185307 / wavelength) : 1.0;
    w->wave_count++;
}

/// @brief Remove all Gerstner waves.
void rt_water3d_clear_waves(void *obj) {
    if (obj)
        ((rt_water3d *)obj)->wave_count = 0;
}

/// @brief Update the water3d state (called per frame/tick).
void rt_water3d_update(void *obj, double dt) {
    if (!obj || dt <= 0)
        return;
    rt_water3d *w = (rt_water3d *)obj;
    w->time += dt;

    /* Regenerate mesh with new wave positions (reuse allocation to avoid GC pressure) */
    if (!w->mesh)
        w->mesh = rt_mesh3d_new();
    else
        rt_mesh3d_clear(w->mesh);
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
            if (nlen > 1e-8) {
                nx /= nlen;
                ny /= nlen;
                nz /= nlen;
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

    /* Update material — create on first use, update properties every frame */
    if (!w->material) {
        w->material = rt_material3d_new_color(w->color[0], w->color[1], w->color[2]);
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
}

/// @brief Draw the animated water surface to the 3D canvas.
void rt_canvas3d_draw_water(void *canvas, void *obj, void *camera) {
    if (!canvas || !obj)
        return;
    rt_water3d *w = (rt_water3d *)obj;
    (void)camera;
    if (!w->mesh || !w->material)
        return;

    /* Draw with backface culling disabled (water visible from both sides) */
    extern void rt_canvas3d_set_backface_cull(void *canvas, int8_t enabled);
    int8_t restore_backface_cull = ((rt_canvas3d *)canvas)->backface_cull;
    rt_canvas3d_set_backface_cull(canvas, 0);
    rt_canvas3d_draw_mesh(canvas, w->mesh, rt_mat4_identity(), w->material);
    rt_canvas3d_set_backface_cull(canvas, restore_backface_cull);
}

#else
typedef int rt_graphics_disabled_tu_guard;
#endif /* VIPER_ENABLE_GRAPHICS */
