//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_path3d.c
// Purpose: 3D Catmull-Rom spline path with position/direction evaluation.
//
// Key invariants:
//   - Separate x/y/z arrays for control points (matches rt_spline.c pattern).
//   - Catmull-Rom interpolation passes through all control points.
//   - Direction computed via finite difference of position evaluation.
//   - Arc length numerically integrated and cached with dirty flag.
//
// Links: rt_path3d.h, rt_spline.c
//
//===----------------------------------------------------------------------===//

#ifdef VIPER_ENABLE_GRAPHICS

#include "rt_path3d.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

extern void *rt_obj_new_i64(int64_t class_id, int64_t byte_size);
extern void rt_obj_set_finalizer(void *obj, void (*fn)(void *));
extern void rt_trap(const char *msg);
extern void *rt_vec3_new(double x, double y, double z);
extern double rt_vec3_x(void *v);
extern double rt_vec3_y(void *v);
extern double rt_vec3_z(void *v);

#define PATH3D_INIT_CAP 16

typedef struct {
    void *vptr;
    double *xs, *ys, *zs;
    int32_t point_count;
    int32_t point_capacity;
    int8_t looping;
    double cached_length;
    int8_t length_dirty;
} rt_path3d;

static void path3d_finalizer(void *obj) {
    rt_path3d *p = (rt_path3d *)obj;
    free(p->xs);
    free(p->ys);
    free(p->zs);
    p->xs = p->ys = p->zs = NULL;
    p->point_count = p->point_capacity = 0;
}

void *rt_path3d_new(void) {
    rt_path3d *p = (rt_path3d *)rt_obj_new_i64(0, (int64_t)sizeof(rt_path3d));
    if (!p) {
        rt_trap("Path3D.New: allocation failed");
        return NULL;
    }
    p->vptr = NULL;
    p->xs = (double *)calloc(PATH3D_INIT_CAP, sizeof(double));
    p->ys = (double *)calloc(PATH3D_INIT_CAP, sizeof(double));
    p->zs = (double *)calloc(PATH3D_INIT_CAP, sizeof(double));
    p->point_count = 0;
    p->point_capacity = PATH3D_INIT_CAP;
    p->looping = 0;
    p->cached_length = 0.0;
    p->length_dirty = 1;
    rt_obj_set_finalizer(p, path3d_finalizer);
    return p;
}

void rt_path3d_add_point(void *obj, void *pos) {
    if (!obj || !pos)
        return;
    rt_path3d *p = (rt_path3d *)obj;

    if (p->point_count >= p->point_capacity) {
        int32_t new_cap = p->point_capacity * 2;
        p->xs = (double *)realloc(p->xs, (size_t)new_cap * sizeof(double));
        p->ys = (double *)realloc(p->ys, (size_t)new_cap * sizeof(double));
        p->zs = (double *)realloc(p->zs, (size_t)new_cap * sizeof(double));
        p->point_capacity = new_cap;
    }

    p->xs[p->point_count] = rt_vec3_x(pos);
    p->ys[p->point_count] = rt_vec3_y(pos);
    p->zs[p->point_count] = rt_vec3_z(pos);
    p->point_count++;
    p->length_dirty = 1;
}

/// @brief 3D Catmull-Rom spline evaluation (mirrors rt_spline.c pattern).
static void catmull_rom_3d(double p0x,
                           double p0y,
                           double p0z,
                           double p1x,
                           double p1y,
                           double p1z,
                           double p2x,
                           double p2y,
                           double p2z,
                           double p3x,
                           double p3y,
                           double p3z,
                           double t,
                           double *ox,
                           double *oy,
                           double *oz) {
    double t2 = t * t, t3 = t2 * t;
    *ox = 0.5 * ((2.0 * p1x) + (-p0x + p2x) * t + (2.0 * p0x - 5.0 * p1x + 4.0 * p2x - p3x) * t2 +
                 (-p0x + 3.0 * p1x - 3.0 * p2x + p3x) * t3);
    *oy = 0.5 * ((2.0 * p1y) + (-p0y + p2y) * t + (2.0 * p0y - 5.0 * p1y + 4.0 * p2y - p3y) * t2 +
                 (-p0y + 3.0 * p1y - 3.0 * p2y + p3y) * t3);
    *oz = 0.5 * ((2.0 * p1z) + (-p0z + p2z) * t + (2.0 * p0z - 5.0 * p1z + 4.0 * p2z - p3z) * t2 +
                 (-p0z + 3.0 * p1z - 3.0 * p2z + p3z) * t3);
}

/// @brief Get index clamped or wrapped for Catmull-Rom neighbor lookup.
static int32_t path_idx(const rt_path3d *p, int32_t i) {
    if (p->looping)
        return ((i % p->point_count) + p->point_count) % p->point_count;
    if (i < 0)
        return 0;
    if (i >= p->point_count)
        return p->point_count - 1;
    return i;
}

void *rt_path3d_get_position_at(void *obj, double t) {
    if (!obj)
        return rt_vec3_new(0, 0, 0);
    rt_path3d *p = (rt_path3d *)obj;
    if (p->point_count < 2) {
        if (p->point_count == 1)
            return rt_vec3_new(p->xs[0], p->ys[0], p->zs[0]);
        return rt_vec3_new(0, 0, 0);
    }

    /* Clamp or wrap t */
    if (p->looping) {
        t = fmod(t, 1.0);
        if (t < 0)
            t += 1.0;
    } else {
        if (t < 0.0)
            t = 0.0;
        if (t > 1.0)
            t = 1.0;
    }

    int n = p->point_count;
    double seg_f = t * (double)(n - 1);
    int seg = (int)seg_f;
    double local_t = seg_f - (double)seg;
    if (seg >= n - 1) {
        seg = n - 2;
        local_t = 1.0;
    }

    int32_t i0 = path_idx(p, seg - 1);
    int32_t i1 = path_idx(p, seg);
    int32_t i2 = path_idx(p, seg + 1);
    int32_t i3 = path_idx(p, seg + 2);

    double ox, oy, oz;
    catmull_rom_3d(p->xs[i0],
                   p->ys[i0],
                   p->zs[i0],
                   p->xs[i1],
                   p->ys[i1],
                   p->zs[i1],
                   p->xs[i2],
                   p->ys[i2],
                   p->zs[i2],
                   p->xs[i3],
                   p->ys[i3],
                   p->zs[i3],
                   local_t,
                   &ox,
                   &oy,
                   &oz);
    return rt_vec3_new(ox, oy, oz);
}

void *rt_path3d_get_direction_at(void *obj, double t) {
    double eps = 0.001;
    void *p0 = rt_path3d_get_position_at(obj, t - eps);
    void *p1 = rt_path3d_get_position_at(obj, t + eps);
    double dx = rt_vec3_x(p1) - rt_vec3_x(p0);
    double dy = rt_vec3_y(p1) - rt_vec3_y(p0);
    double dz = rt_vec3_z(p1) - rt_vec3_z(p0);
    double len = sqrt(dx * dx + dy * dy + dz * dz);
    if (len > 1e-8) {
        dx /= len;
        dy /= len;
        dz /= len;
    }
    return rt_vec3_new(dx, dy, dz);
}

double rt_path3d_get_length(void *obj) {
    if (!obj)
        return 0.0;
    rt_path3d *p = (rt_path3d *)obj;
    if (p->point_count < 2)
        return 0.0;
    if (!p->length_dirty)
        return p->cached_length;

    int steps = p->point_count * 20;
    double total = 0.0, prev_x = 0, prev_y = 0, prev_z = 0;
    for (int i = 0; i <= steps; i++) {
        double t = (double)i / (double)steps;
        void *pt = rt_path3d_get_position_at(obj, t);
        double x = rt_vec3_x(pt), y = rt_vec3_y(pt), z = rt_vec3_z(pt);
        if (i > 0) {
            double dx = x - prev_x, dy = y - prev_y, dz = z - prev_z;
            total += sqrt(dx * dx + dy * dy + dz * dz);
        }
        prev_x = x;
        prev_y = y;
        prev_z = z;
    }
    p->cached_length = total;
    p->length_dirty = 0;
    return total;
}

int64_t rt_path3d_get_point_count(void *obj) {
    return obj ? ((rt_path3d *)obj)->point_count : 0;
}

void rt_path3d_set_looping(void *obj, int8_t loop) {
    if (!obj)
        return;
    rt_path3d *p = (rt_path3d *)obj;
    p->looping = loop;
    p->length_dirty = 1;
}

void rt_path3d_clear(void *obj) {
    if (!obj)
        return;
    rt_path3d *p = (rt_path3d *)obj;
    p->point_count = 0;
    p->length_dirty = 1;
}

#endif /* VIPER_ENABLE_GRAPHICS */
