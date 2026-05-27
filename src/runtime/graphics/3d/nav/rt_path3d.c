//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/nav/rt_path3d.c
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

#include "rt_graphics3d_ids.h"
#include "rt_path3d.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

extern void *rt_obj_new_i64(int64_t class_id, int64_t byte_size);
extern void rt_obj_set_finalizer(void *obj, void (*fn)(void *));
extern int32_t rt_obj_release_check0(void *obj);
extern void rt_obj_free(void *obj);
#include "rt_trap.h"
extern void *rt_vec3_new(double x, double y, double z);
extern double rt_vec3_x(void *v);
extern double rt_vec3_y(void *v);
extern double rt_vec3_z(void *v);

#define PATH3D_INIT_CAP 16
#define PATH3D_MAX_LENGTH_STEPS 1000000

typedef struct {
    void *vptr;
    double *xs, *ys, *zs;
    int32_t point_count;
    int32_t point_capacity;
    int8_t looping;
    double cached_length;
    int8_t length_dirty;
} rt_path3d;

/// @brief GC finalizer — release the three parallel coordinate arrays.
/// @details Path control points are stored in struct-of-arrays layout
///   (separate `xs`, `ys`, `zs` rather than a packed Vec3 array) so each
///   axis can be cache-linearly scanned during length integration. The
///   three arrays are always index-aligned and reallocated together; the
///   finalizer releases each independently and zeros the counts so a
///   stale post-finalize read sees an empty path rather than dangling
///   pointers.
static void path3d_finalizer(void *obj) {
    rt_path3d *p = (rt_path3d *)obj;
    free(p->xs);
    free(p->ys);
    free(p->zs);
    p->xs = p->ys = p->zs = NULL;
    p->point_count = p->point_capacity = 0;
}

/// @brief Drop one local refcount on @p obj and free if it hits zero.
/// @details Used by callers that take a transient Vec3 from a path-evaluation
///   helper and need to release it before returning their own result.
static void path3d_release_local(void *obj) {
    if (obj && rt_obj_release_check0(obj))
        rt_obj_free(obj);
}

/// @brief Grow the parallel `xs` / `ys` / `zs` coordinate arrays to hold @p min_capacity entries.
/// @details Geometric growth (doubling from PATH3D_INIT_CAP) keeps amortised
///   `add_point` cost O(1). All three arrays are reallocated together and on
///   any failure the new buffers are freed before returning so the path stays
///   in its previous valid state. Returns 1 on success, 0 (after `rt_trap`) on
///   overflow or OOM.
static int path3d_reserve(rt_path3d *p, int32_t min_capacity) {
    if (!p || min_capacity <= p->point_capacity)
        return 1;
    int32_t new_cap = p->point_capacity > 0 ? p->point_capacity : PATH3D_INIT_CAP;
    while (new_cap < min_capacity) {
        if (new_cap > INT32_MAX / 2) {
            rt_trap("Path3D.AddPoint: too many points");
            return 0;
        }
        new_cap *= 2;
    }
    if ((size_t)new_cap > SIZE_MAX / sizeof(double)) {
        rt_trap("Path3D.AddPoint: allocation size overflow");
        return 0;
    }
    double *new_xs = (double *)malloc((size_t)new_cap * sizeof(double));
    double *new_ys = (double *)malloc((size_t)new_cap * sizeof(double));
    double *new_zs = (double *)malloc((size_t)new_cap * sizeof(double));
    if (!new_xs || !new_ys || !new_zs) {
        free(new_xs);
        free(new_ys);
        free(new_zs);
        rt_trap("Path3D.AddPoint: allocation failed");
        return 0;
    }
    if (p->point_count > 0) {
        memcpy(new_xs, p->xs, (size_t)p->point_count * sizeof(double));
        memcpy(new_ys, p->ys, (size_t)p->point_count * sizeof(double));
        memcpy(new_zs, p->zs, (size_t)p->point_count * sizeof(double));
    }
    free(p->xs);
    free(p->ys);
    free(p->zs);
    p->xs = new_xs;
    p->ys = new_ys;
    p->zs = new_zs;
    p->point_capacity = new_cap;
    return 1;
}

/// @brief Create a new empty 3D Catmull-Rom spline path.
/// @details Paths are used for camera dollies, patrol routes, missile trajectories,
///          and similar smooth 3D curves. Points are added with add_point; the
///          curve passes through all control points (Catmull-Rom property). The
///          arc length is cached and recomputed lazily when points change.
/// @return Opaque path handle, or NULL on allocation failure.
void *rt_path3d_new(void) {
    rt_path3d *p = (rt_path3d *)rt_obj_new_i64(RT_G3D_PATH3D_CLASS_ID, (int64_t)sizeof(rt_path3d));
    if (!p) {
        rt_trap("Path3D.New: allocation failed");
        return NULL;
    }
    p->vptr = NULL;
    p->xs = (double *)calloc(PATH3D_INIT_CAP, sizeof(double));
    p->ys = (double *)calloc(PATH3D_INIT_CAP, sizeof(double));
    p->zs = (double *)calloc(PATH3D_INIT_CAP, sizeof(double));
    if (!p->xs || !p->ys || !p->zs) {
        path3d_finalizer(p);
        if (rt_obj_release_check0(p))
            rt_obj_free(p);
        rt_trap("Path3D.New: allocation failed");
        return NULL;
    }
    p->point_count = 0;
    p->point_capacity = PATH3D_INIT_CAP;
    p->looping = 0;
    p->cached_length = 0.0;
    p->length_dirty = 1;
    rt_obj_set_finalizer(p, path3d_finalizer);
    return p;
}

/// @brief Append a control point to the path (invalidates cached arc length).
void rt_path3d_add_point(void *obj, void *pos) {
    if (!obj || !rt_g3d_is_vec3(pos))
        return;
    rt_path3d *p = (rt_path3d *)rt_g3d_checked_or_null(obj, RT_G3D_PATH3D_CLASS_ID);
    if (!p)
        return;

    if (p->point_count == INT32_MAX || !path3d_reserve(p, p->point_count + 1))
        return;

    double x = rt_vec3_x(pos);
    double y = rt_vec3_y(pos);
    double z = rt_vec3_z(pos);
    p->xs[p->point_count] = isfinite(x) ? x : 0.0;
    p->ys[p->point_count] = isfinite(y) ? y : 0.0;
    p->zs[p->point_count] = isfinite(z) ? z : 0.0;
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

/// @brief Evaluate the path position at parameter t in [0, 1].
/// @details Uses Catmull-Rom interpolation between control points. The curve
///          passes through every control point. If looping is enabled, t wraps
///          around; otherwise it is clamped to [0, 1]. Requires at least 2 points.
/// @param obj Path handle.
/// @param t   Parameter along the path (0 = start, 1 = end).
/// @return New Vec3 at the interpolated position.
void *rt_path3d_get_position_at(void *obj, double t) {
    rt_path3d *p = (rt_path3d *)rt_g3d_checked_or_null(obj, RT_G3D_PATH3D_CLASS_ID);
    if (!p)
        return rt_vec3_new(0, 0, 0);
    if (!isfinite(t))
        t = 0.0;
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
    int segment_count = p->looping ? n : n - 1;
    double seg_f = t * (double)segment_count;
    int seg = (int)seg_f;
    double local_t = seg_f - (double)seg;
    if (p->looping) {
        if (seg >= segment_count) {
            seg = 0;
            local_t = 0.0;
        }
    } else if (seg >= segment_count) {
        seg = segment_count - 1;
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

/// @brief Get the normalized tangent direction at parameter t.
/// @details Computes the tangent via finite differences (forward - backward at
///          a small epsilon). Returns (0,0,0) if the path has < 2 points.
void *rt_path3d_get_direction_at(void *obj, double t) {
    double eps = 0.001;
    void *p0 = rt_path3d_get_position_at(obj, t - eps);
    void *p1 = rt_path3d_get_position_at(obj, t + eps);
    if (!p0 || !p1) {
        path3d_release_local(p0);
        path3d_release_local(p1);
        return rt_vec3_new(0, 0, 0);
    }
    double dx = rt_vec3_x(p1) - rt_vec3_x(p0);
    double dy = rt_vec3_y(p1) - rt_vec3_y(p0);
    double dz = rt_vec3_z(p1) - rt_vec3_z(p0);
    double len = sqrt(dx * dx + dy * dy + dz * dz);
    if (len > 1e-8) {
        dx /= len;
        dy /= len;
        dz /= len;
    }
    path3d_release_local(p0);
    path3d_release_local(p1);
    return rt_vec3_new(dx, dy, dz);
}

/// @brief Compute the total arc length of the path (cached, recomputed when dirty).
/// @details Numerically integrates distance along the spline using up to 20 samples
///          per control point, capped to avoid integer overflow and runaway work.
///          The result is cached until points are added/removed.
double rt_path3d_get_length(void *obj) {
    rt_path3d *p = (rt_path3d *)rt_g3d_checked_or_null(obj, RT_G3D_PATH3D_CLASS_ID);
    if (!p)
        return 0.0;
    if (p->point_count < 2)
        return 0.0;
    if (!p->length_dirty)
        return p->cached_length;

    int64_t steps = (int64_t)p->point_count * 20;
    if (steps > PATH3D_MAX_LENGTH_STEPS)
        steps = PATH3D_MAX_LENGTH_STEPS;
    double total = 0.0, prev_x = 0, prev_y = 0, prev_z = 0;
    for (int64_t i = 0; i <= steps; i++) {
        double t = (double)i / (double)steps;
        void *pt = rt_path3d_get_position_at(obj, t);
        if (!pt)
            continue;
        double x = rt_vec3_x(pt), y = rt_vec3_y(pt), z = rt_vec3_z(pt);
        if (i > 0) {
            double dx = x - prev_x, dy = y - prev_y, dz = z - prev_z;
            total += sqrt(dx * dx + dy * dy + dz * dz);
        }
        prev_x = x;
        prev_y = y;
        prev_z = z;
        path3d_release_local(pt);
    }
    p->cached_length = total;
    p->length_dirty = 0;
    return total;
}

/// @brief Get the number of control points in the path.
int64_t rt_path3d_get_point_count(void *obj) {
    rt_path3d *p = (rt_path3d *)rt_g3d_checked_or_null(obj, RT_G3D_PATH3D_CLASS_ID);
    return p ? p->point_count : 0;
}

/// @brief Enable or disable looping (t wraps around instead of clamping).
void rt_path3d_set_looping(void *obj, int8_t loop) {
    rt_path3d *p = (rt_path3d *)rt_g3d_checked_or_null(obj, RT_G3D_PATH3D_CLASS_ID);
    if (!p)
        return;
    p->looping = loop;
    p->length_dirty = 1;
}

/// @brief Remove all control points, resetting the path to empty.
void rt_path3d_clear(void *obj) {
    rt_path3d *p = (rt_path3d *)rt_g3d_checked_or_null(obj, RT_G3D_PATH3D_CLASS_ID);
    if (!p)
        return;
    p->point_count = 0;
    p->length_dirty = 1;
}

#else
typedef int rt_graphics_disabled_tu_guard;
#endif /* VIPER_ENABLE_GRAPHICS */
