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

#include "rt_path3d.h"
#include "rt_graphics3d_ids.h"

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
#define PATH3D_COORD_ABS_MAX 1000000000000.0
#define PATH3D_LENGTH_MAX 1000000000000000000.0

#define PATH3D_SPLINE_SUBSTEPS 64

typedef struct {
    void *vptr;
    double *xs, *ys, *zs;
    int32_t point_count;
    int32_t point_capacity;
    int8_t looping;
    double cached_length;
    int8_t length_dirty;
    /* Centripetal Catmull-Rom arclength cache (rt_path3d_eval_spline_raw):
     * cumulative length at every segment substep, rebuilt when dirty. */
    double *spline_cumulative;
    int32_t spline_sample_count;
    int8_t spline_dirty;
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
    if (!p)
        return;
    free(p->xs);
    free(p->ys);
    free(p->zs);
    p->xs = p->ys = p->zs = NULL;
    p->point_count = p->point_capacity = 0;
    free(p->spline_cumulative);
    p->spline_cumulative = NULL;
    p->spline_sample_count = 0;
}

/// @brief Sanitize one coordinate lane, capping finite extremes so interpolation stays finite.
static double path3d_coord_or(double value, double fallback) {
    if (!isfinite(fallback))
        fallback = 0.0;
    if (!isfinite(value))
        value = fallback;
    if (value > PATH3D_COORD_ABS_MAX)
        return PATH3D_COORD_ABS_MAX;
    if (value < -PATH3D_COORD_ABS_MAX)
        return -PATH3D_COORD_ABS_MAX;
    return value;
}

/// @brief Repair defensive invariants before public operations touch the parallel point arrays.
static void path3d_repair(rt_path3d *p) {
    if (!p)
        return;
    if (p->point_capacity < 0)
        p->point_capacity = 0;
    if (!p->xs || !p->ys || !p->zs || p->point_capacity == 0) {
        p->point_count = 0;
        p->point_capacity = 0;
        p->length_dirty = 1;
        p->spline_dirty = 1;
        p->cached_length = 0.0;
        return;
    }
    if (p->point_count < 0)
        p->point_count = 0;
    if (p->point_count > p->point_capacity)
        p->point_count = p->point_capacity;
    for (int32_t i = 0; i < p->point_count; ++i) {
        p->xs[i] = path3d_coord_or(p->xs[i], 0.0);
        p->ys[i] = path3d_coord_or(p->ys[i], 0.0);
        p->zs[i] = path3d_coord_or(p->zs[i], 0.0);
    }
    p->looping = p->looping ? 1 : 0;
    if (!isfinite(p->cached_length) || p->cached_length < 0.0) {
        p->cached_length = 0.0;
        p->length_dirty = 1;
        p->spline_dirty = 1;
    }
}

/// @brief Grow the parallel `xs` / `ys` / `zs` coordinate arrays to hold @p min_capacity entries.
/// @details Geometric growth (doubling from PATH3D_INIT_CAP) keeps amortised
///   `add_point` cost O(1). All three arrays are reallocated together and on
///   any failure the new buffers are freed before returning so the path stays
///   in its previous valid state. Returns 1 on success, 0 (after `rt_trap`) on
///   overflow or OOM.
static int path3d_reserve(rt_path3d *p, int32_t min_capacity) {
    if (!p || min_capacity < 0)
        return 0;
    path3d_repair(p);
    if (min_capacity <= p->point_capacity)
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
    p->spline_dirty = 1;
    p->spline_cumulative = NULL;
    p->spline_sample_count = 0;
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
    path3d_repair(p);

    if (p->point_count == INT32_MAX || !path3d_reserve(p, p->point_count + 1))
        return;

    double x = rt_vec3_x(pos);
    double y = rt_vec3_y(pos);
    double z = rt_vec3_z(pos);
    p->xs[p->point_count] = path3d_coord_or(x, 0.0);
    p->ys[p->point_count] = path3d_coord_or(y, 0.0);
    p->zs[p->point_count] = path3d_coord_or(z, 0.0);
    p->point_count++;
    p->length_dirty = 1;
    p->spline_dirty = 1;
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

static void path3d_eval_position(rt_path3d *p, double t, double *ox, double *oy, double *oz) {
    if (!ox || !oy || !oz)
        return;
    if (!p) {
        *ox = *oy = *oz = 0.0;
        return;
    }
    path3d_repair(p);
    if (!isfinite(t))
        t = 0.0;
    if (p->point_count < 2) {
        if (p->point_count == 1) {
            *ox = p->xs[0];
            *oy = p->ys[0];
            *oz = p->zs[0];
        } else {
            *ox = *oy = *oz = 0.0;
        }
        return;
    }

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
                   ox,
                   oy,
                   oz);
    *ox = path3d_coord_or(*ox, 0.0);
    *oy = path3d_coord_or(*oy, 0.0);
    *oz = path3d_coord_or(*oz, 0.0);
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
    double ox, oy, oz;
    path3d_eval_position(p, t, &ox, &oy, &oz);
    return rt_vec3_new(ox, oy, oz);
}

/// @brief Get the normalized tangent direction at parameter t.
/// @details Computes the tangent via finite differences (forward - backward at
///          a small epsilon). Returns (0,0,0) if the path has < 2 points.
void *rt_path3d_get_direction_at(void *obj, double t) {
    rt_path3d *p = (rt_path3d *)rt_g3d_checked_or_null(obj, RT_G3D_PATH3D_CLASS_ID);
    double eps = 0.001;
    double p0x, p0y, p0z;
    double p1x, p1y, p1z;
    if (!p) {
        return rt_vec3_new(0, 0, 0);
    }
    path3d_eval_position(p, t - eps, &p0x, &p0y, &p0z);
    path3d_eval_position(p, t + eps, &p1x, &p1y, &p1z);
    double dx = p1x - p0x;
    double dy = p1y - p0y;
    double dz = p1z - p0z;
    double max_abs = fmax(fabs(dx), fmax(fabs(dy), fabs(dz)));
    double len = 0.0;
    if (isfinite(max_abs) && max_abs > 0.0) {
        double sx = dx / max_abs;
        double sy = dy / max_abs;
        double sz = dz / max_abs;
        len = max_abs * sqrt(sx * sx + sy * sy + sz * sz);
    }
    if (isfinite(len) && len > 1e-8) {
        dx /= len;
        dy /= len;
        dz /= len;
    } else {
        dx = 0.0;
        dy = 0.0;
        dz = 0.0;
    }
    return rt_vec3_new(dx, dy, dz);
}

/// @brief Centripetal Catmull-Rom (Barry-Goldman, alpha = 0.5) for one segment.
/// @details Evaluates the curve between p1 and p2 at local u in [0,1] with
///   phantom neighbors p0/p3. Centripetal knots eliminate the loops/cusps and
///   along-line overshoot uniform parameterization produces on uneven spacing.
///   Degenerate (coincident) knots collapse to linear interpolation.
static void path3d_eval_centripetal_segment(const double p0[3],
                                            const double p1[3],
                                            const double p2[3],
                                            const double p3[3],
                                            double u,
                                            double out[3]) {
    double d01 = sqrt(sqrt((p1[0] - p0[0]) * (p1[0] - p0[0]) + (p1[1] - p0[1]) * (p1[1] - p0[1]) +
                           (p1[2] - p0[2]) * (p1[2] - p0[2])));
    double d12 = sqrt(sqrt((p2[0] - p1[0]) * (p2[0] - p1[0]) + (p2[1] - p1[1]) * (p2[1] - p1[1]) +
                           (p2[2] - p1[2]) * (p2[2] - p1[2])));
    double d23 = sqrt(sqrt((p3[0] - p2[0]) * (p3[0] - p2[0]) + (p3[1] - p2[1]) * (p3[1] - p2[1]) +
                           (p3[2] - p2[2]) * (p3[2] - p2[2])));
    double t0 = 0.0;
    double t1 = t0 + (d01 > 1e-9 ? d01 : 1e-9);
    double t2 = t1 + (d12 > 1e-9 ? d12 : 1e-9);
    double t3 = t2 + (d23 > 1e-9 ? d23 : 1e-9);
    double t = t1 + (t2 - t1) * (u < 0.0 ? 0.0 : (u > 1.0 ? 1.0 : u));
    for (int c = 0; c < 3; ++c) {
        double a1 = (t1 - t) / (t1 - t0) * p0[c] + (t - t0) / (t1 - t0) * p1[c];
        double a2 = (t2 - t) / (t2 - t1) * p1[c] + (t - t1) / (t2 - t1) * p2[c];
        double a3 = (t3 - t) / (t3 - t2) * p2[c] + (t - t2) / (t3 - t2) * p3[c];
        double b1 = (t2 - t) / (t2 - t0) * a1 + (t - t0) / (t2 - t0) * a2;
        double b2 = (t3 - t) / (t3 - t1) * a2 + (t - t1) / (t3 - t1) * a3;
        out[c] = (t2 - t) / (t2 - t1) * b1 + (t - t1) / (t2 - t1) * b2;
    }
}

/// @brief Evaluate the centripetal spline at uniform-segment parameter t
///   (same segment mapping GetPositionAt uses; shape differs by design).
static void path3d_eval_spline_position(rt_path3d *p, double t, double out[3]) {
    out[0] = out[1] = out[2] = 0.0;
    if (!p)
        return;
    path3d_repair(p);
    if (!isfinite(t))
        t = 0.0;
    if (p->point_count < 2) {
        if (p->point_count == 1) {
            out[0] = p->xs[0];
            out[1] = p->ys[0];
            out[2] = p->zs[0];
        }
        return;
    }
    if (p->looping) {
        t = fmod(t, 1.0);
        if (t < 0.0)
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
    double local_u = seg_f - (double)seg;
    if (p->looping) {
        if (seg >= segment_count) {
            seg = 0;
            local_u = 0.0;
        }
    } else if (seg >= segment_count) {
        seg = segment_count - 1;
        local_u = 1.0;
    }
    int32_t i0 = path_idx(p, seg - 1);
    int32_t i1 = path_idx(p, seg);
    int32_t i2 = path_idx(p, seg + 1);
    int32_t i3 = path_idx(p, seg + 2);
    double q0[3] = {p->xs[i0], p->ys[i0], p->zs[i0]};
    double q1[3] = {p->xs[i1], p->ys[i1], p->zs[i1]};
    double q2[3] = {p->xs[i2], p->ys[i2], p->zs[i2]};
    double q3[3] = {p->xs[i3], p->ys[i3], p->zs[i3]};
    path3d_eval_centripetal_segment(q0, q1, q2, q3, local_u, out);
    out[0] = path3d_coord_or(out[0], 0.0);
    out[1] = path3d_coord_or(out[1], 0.0);
    out[2] = path3d_coord_or(out[2], 0.0);
}

/// @brief Rebuild the arclength table: cumulative curve length sampled at
///   PATH3D_SPLINE_SUBSTEPS points per segment of the Catmull-Rom curve.
static void path3d_spline_refresh(rt_path3d *p) {
    if (!p || !p->spline_dirty)
        return;
    path3d_repair(p);
    int n = p->point_count;
    int segment_count = n >= 2 ? (p->looping ? n : n - 1) : 0;
    int32_t samples = segment_count > 0 ? segment_count * PATH3D_SPLINE_SUBSTEPS + 1 : 0;
    if (samples <= 0) {
        free(p->spline_cumulative);
        p->spline_cumulative = NULL;
        p->spline_sample_count = 0;
        p->spline_dirty = 0;
        return;
    }
    double *table = (double *)realloc(p->spline_cumulative, (size_t)samples * sizeof(double));
    if (!table)
        return; /* stay dirty; retry next call */
    p->spline_cumulative = table;
    p->spline_sample_count = samples;
    double prev_x = 0.0, prev_y = 0.0, prev_z = 0.0;
    double total = 0.0;
    for (int32_t i = 0; i < samples; ++i) {
        double t = (double)i / (double)(samples - 1);
        double sample[3];
        path3d_eval_spline_position(p, t, sample);
        double x = sample[0], y = sample[1], z = sample[2];
        if (i > 0) {
            double dx = x - prev_x;
            double dy = y - prev_y;
            double dz = z - prev_z;
            double d = sqrt(dx * dx + dy * dy + dz * dz);
            if (isfinite(d))
                total += d;
        }
        table[i] = total;
        prev_x = x;
        prev_y = y;
        prev_z = z;
    }
    p->spline_dirty = 0;
}

/// @brief Internal: arclength-normalized Catmull-Rom evaluation.
/// @details @p t in [0,1] maps to a constant-speed position along the SAME
///   centripetal Catmull-Rom curve through the control points (alpha 0.5 —
///   no loops/cusps on uneven spacing; GetPositionAt keeps its historical
///   uniform parameterization), using a cached cumulative-length table inverted piecewise-
///   linearly at PATH3D_SPLINE_SUBSTEPS resolution per segment. Writes the
///   position and (when @p tan_out is non-NULL) the unit tangent. Falls back
///   to the raw parameterization for degenerate paths (< 2 points or zero
///   length). Consumers: RailCamera3D, Timeline3D camera-move tracks.
void rt_path3d_eval_spline_raw(void *obj, double t, double *pos_out, double *tan_out) {
    rt_path3d *p = (rt_path3d *)rt_g3d_checked_or_null(obj, RT_G3D_PATH3D_CLASS_ID);
    if (pos_out)
        pos_out[0] = pos_out[1] = pos_out[2] = 0.0;
    if (tan_out)
        tan_out[0] = tan_out[1] = tan_out[2] = 0.0;
    if (!p || !pos_out)
        return;
    if (!isfinite(t))
        t = 0.0;
    if (p->looping) {
        t = fmod(t, 1.0);
        if (t < 0.0)
            t += 1.0;
    } else {
        if (t < 0.0)
            t = 0.0;
        if (t > 1.0)
            t = 1.0;
    }
    path3d_spline_refresh(p);
    double u = t;
    if (p->spline_cumulative && p->spline_sample_count >= 2) {
        double total = p->spline_cumulative[p->spline_sample_count - 1];
        if (isfinite(total) && total > 1e-12) {
            double target = t * total;
            int32_t lo = 0;
            int32_t hi = p->spline_sample_count - 1;
            while (lo + 1 < hi) {
                int32_t mid = (lo + hi) / 2;
                if (p->spline_cumulative[mid] <= target)
                    lo = mid;
                else
                    hi = mid;
            }
            double seg_len = p->spline_cumulative[hi] - p->spline_cumulative[lo];
            double frac = seg_len > 1e-12 ? (target - p->spline_cumulative[lo]) / seg_len : 0.0;
            u = ((double)lo + frac) / (double)(p->spline_sample_count - 1);
        }
    }
    path3d_eval_spline_position(p, u, pos_out);
    if (tan_out) {
        double eps = 0.0005;
        double before[3];
        double after[3];
        path3d_eval_spline_position(p, u - eps, before);
        path3d_eval_spline_position(p, u + eps, after);
        double ax = before[0], ay = before[1], az = before[2];
        double bx = after[0], by = after[1], bz = after[2];
        double dx = bx - ax;
        double dy = by - ay;
        double dz = bz - az;
        double len = sqrt(dx * dx + dy * dy + dz * dz);
        if (isfinite(len) && len > 1e-12) {
            tan_out[0] = dx / len;
            tan_out[1] = dy / len;
            tan_out[2] = dz / len;
        }
    }
}

/// @brief Compute the total arc length of the path (cached, recomputed when dirty).
/// @details Numerically integrates distance along the spline using up to 20 samples
///          per control point, capped to avoid integer overflow and runaway work.
///          The result is cached until points are added/removed.
double rt_path3d_get_length(void *obj) {
    rt_path3d *p = (rt_path3d *)rt_g3d_checked_or_null(obj, RT_G3D_PATH3D_CLASS_ID);
    if (!p)
        return 0.0;
    path3d_repair(p);
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
        double x, y, z;
        path3d_eval_position(p, t, &x, &y, &z);
        if (i > 0) {
            double dx = x - prev_x, dy = y - prev_y, dz = z - prev_z;
            double max_abs = fmax(fabs(dx), fmax(fabs(dy), fabs(dz)));
            double segment = 0.0;
            if (isfinite(max_abs) && max_abs > 0.0) {
                double sx = dx / max_abs;
                double sy = dy / max_abs;
                double sz = dz / max_abs;
                segment = max_abs * sqrt(sx * sx + sy * sy + sz * sz);
            }
            if (isfinite(segment))
                total += segment;
            if (total > PATH3D_LENGTH_MAX) {
                total = PATH3D_LENGTH_MAX;
                break;
            }
        }
        prev_x = x;
        prev_y = y;
        prev_z = z;
    }
    p->cached_length = total;
    p->length_dirty = 0;
    return total;
}

/// @brief Get the number of control points in the path.
int64_t rt_path3d_get_point_count(void *obj) {
    rt_path3d *p = (rt_path3d *)rt_g3d_checked_or_null(obj, RT_G3D_PATH3D_CLASS_ID);
    if (!p)
        return 0;
    path3d_repair(p);
    return p->point_count;
}

/// @brief Enable or disable looping (t wraps around instead of clamping).
void rt_path3d_set_looping(void *obj, int8_t loop) {
    rt_path3d *p = (rt_path3d *)rt_g3d_checked_or_null(obj, RT_G3D_PATH3D_CLASS_ID);
    if (!p)
        return;
    path3d_repair(p);
    p->looping = loop ? 1 : 0;
    p->length_dirty = 1;
    p->spline_dirty = 1;
}

/// @brief Remove all control points, resetting the path to empty.
void rt_path3d_clear(void *obj) {
    rt_path3d *p = (rt_path3d *)rt_g3d_checked_or_null(obj, RT_G3D_PATH3D_CLASS_ID);
    if (!p)
        return;
    path3d_repair(p);
    p->point_count = 0;
    p->length_dirty = 1;
    p->spline_dirty = 1;
}

#else
typedef int rt_graphics_disabled_tu_guard;
#endif /* VIPER_ENABLE_GRAPHICS */
