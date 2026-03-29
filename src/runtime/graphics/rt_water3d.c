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
extern void rt_trap(const char *msg);
extern void *rt_mesh3d_new(void);
extern void rt_mesh3d_add_vertex(
    void *m, double x, double y, double z, double nx, double ny, double nz, double u, double v);
extern void rt_mesh3d_add_triangle(void *m, int64_t v0, int64_t v1, int64_t v2);
extern void *rt_mat4_identity(void);
extern void rt_canvas3d_draw_mesh(void *canvas, void *mesh, void *transform, void *material);
extern void *rt_material3d_new_color(double r, double g, double b);
extern void rt_material3d_set_alpha(void *m, double a);
extern void rt_material3d_set_shininess(void *m, double s);

#define WATER_GRID 32

typedef struct {
    void *vptr;
    double width, depth;
    double height;
    double wave_speed, wave_amplitude, wave_frequency;
    double color[3];
    double alpha;
    double time;
    void *mesh;
    void *material;
} rt_water3d;

static void water3d_finalizer(void *obj) {
    (void)obj;
}

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
    rt_obj_set_finalizer(w, water3d_finalizer);
    return w;
}

/// @brief Set the base Y-level of the water plane in world space.
/// @details Individual vertex heights oscillate around this value based on the
///          sine wave displacement computed during update().
void rt_water3d_set_height(void *obj, double y) {
    if (obj)
        ((rt_water3d *)obj)->height = y;
}

/// @brief Set the wave params of the water3d.
void rt_water3d_set_wave_params(void *obj, double speed, double amplitude, double frequency) {
    if (!obj)
        return;
    rt_water3d *w = (rt_water3d *)obj;
    w->wave_speed = speed;
    w->wave_amplitude = amplitude;
    w->wave_frequency = frequency;
}

/// @brief Set the water surface color (RGBA, each component in [0, 1]).
/// @details Alpha controls transparency — translucent water (a < 1) is rendered
///          with blending enabled so the terrain beneath is partially visible.
void rt_water3d_set_color(void *obj, double r, double g, double b, double a) {
    if (!obj)
        return;
    rt_water3d *w = (rt_water3d *)obj;
    w->color[0] = r;
    w->color[1] = g;
    w->color[2] = b;
    w->alpha = a;
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
    else {
        rt_mesh3d *m = (rt_mesh3d *)w->mesh;
        m->vertex_count = 0;
        m->index_count = 0;
    }
    double hx = w->width * 0.5, hz = w->depth * 0.5;
    double step_x = w->width / WATER_GRID;
    double step_z = w->depth / WATER_GRID;

    /* Vertices */
    for (int gz = 0; gz <= WATER_GRID; gz++) {
        for (int gx = 0; gx <= WATER_GRID; gx++) {
            double x = -hx + gx * step_x;
            double z = -hz + gz * step_z;

            /* Sine wave displacement */
            double phase = w->wave_frequency * (x + z) + w->time * w->wave_speed;
            double y = w->height + w->wave_amplitude * sin(phase);

            /* Normal from wave derivative: dy/dx = amp * freq * cos(phase) */
            double dydx = w->wave_amplitude * w->wave_frequency * cos(phase);
            double nx = -dydx, ny = 1.0, nz = -dydx;
            double nlen = sqrt(nx * nx + ny * ny + nz * nz);
            if (nlen > 1e-8) {
                nx /= nlen;
                ny /= nlen;
                nz /= nlen;
            }

            double u = (double)gx / WATER_GRID;
            double v = (double)gz / WATER_GRID;
            rt_mesh3d_add_vertex(w->mesh, x, y, z, nx, ny, nz, u, v);
        }
    }

    /* Triangles */
    int row = WATER_GRID + 1;
    for (int gz = 0; gz < WATER_GRID; gz++) {
        for (int gx = 0; gx < WATER_GRID; gx++) {
            int base = gz * row + gx;
            rt_mesh3d_add_triangle(w->mesh, base, base + row, base + 1);
            rt_mesh3d_add_triangle(w->mesh, base + 1, base + row, base + row + 1);
        }
    }

    /* Update material — create on first use, update color every frame */
    if (!w->material) {
        w->material = rt_material3d_new_color(w->color[0], w->color[1], w->color[2]);
        rt_material3d_set_shininess(w->material, 128.0);
    } else {
        rt_material3d_set_color(w->material, w->color[0], w->color[1], w->color[2]);
    }
    rt_material3d_set_alpha(w->material, w->alpha);
}

/// @brief Render the water surface mesh into the 3D canvas.
/// @details Temporarily disables backface culling so the water plane is visible
///          from both above and below, draws the mesh with the water material,
///          then re-enables culling for subsequent draw calls.
void rt_canvas3d_draw_water(void *canvas, void *obj, void *camera) {
    if (!canvas || !obj)
        return;
    rt_water3d *w = (rt_water3d *)obj;
    (void)camera;
    if (!w->mesh || !w->material)
        return;

    /* Draw with backface culling disabled (water visible from both sides) */
    extern void rt_canvas3d_set_backface_cull(void *canvas, int8_t enabled);
    rt_canvas3d_set_backface_cull(canvas, 0);
    rt_canvas3d_draw_mesh(canvas, w->mesh, rt_mat4_identity(), w->material);
    rt_canvas3d_set_backface_cull(canvas, 1);
}

#endif /* VIPER_ENABLE_GRAPHICS */
