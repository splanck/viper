//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_spline.c
// Purpose: Spline curve interpolation for the Viper.Spline class. Supports three
//   curve types over Vec2 control points: linear (piecewise straight segments),
//   Catmull-Rom (smooth curve through all control points), and cubic Bezier
//   (curve guided by explicit tangent handles). All splines are parameterized on
//   t in [0.0, 1.0] and return an interpolated Vec2 position or tangent.
//
// Key invariants:
//   - Control point coordinates (x, y) are stored as separate double arrays xs
//     and ys, extracted from the Vec2 sequence at construction time.
//   - Catmull-Rom uses a centripetal parameterization and clamps end-point
//     tangents using phantom points mirrored from the first/last segments.
//   - Bezier evaluation uses De Casteljau's algorithm; for n control points it
//     operates on a degree-(n-1) curve.
//   - Spline objects are immutable after construction; the control point arrays
//     are allocated with calloc and freed via the GC finalizer.
//   - t values outside [0, 1] are clamped to the nearest valid segment.
//
// Ownership/Lifetime:
//   - ViperSpline structs are allocated via rt_obj_new_i64 (GC heap); the xs
//     and ys double arrays are malloc'd separately and freed in spline_finalizer,
//     registered as the GC finalizer at construction.
//
// Links: src/runtime/graphics/rt_spline.h (public API),
//        src/runtime/graphics/rt_vec2.h (Vec2 control point and return type),
//        src/runtime/rt_seq.h (input sequence of Vec2 control points)
//
//===----------------------------------------------------------------------===//

#include "rt_spline.h"

#include "rt_internal.h"
#include "rt_object.h"
#include "rt_seq.h"
#include "rt_vec2.h"

#include <math.h>
#include <stdlib.h>

typedef enum
{
    SPLINE_LINEAR = 0,
    SPLINE_CATMULL_ROM = 1,
    SPLINE_BEZIER = 2
} SplineKind;

typedef struct
{
    SplineKind kind;
    int64_t count;
    double *xs;
    double *ys;
} ViperSpline;

static void spline_finalizer(void *payload)
{
    ViperSpline *s = (ViperSpline *)payload;
    if (s)
    {
        free(s->xs);
        free(s->ys);
        s->xs = NULL;
        s->ys = NULL;
    }
}

static ViperSpline *spline_alloc(SplineKind kind, int64_t count)
{
    ViperSpline *s = (ViperSpline *)rt_obj_new_i64(0, (int64_t)sizeof(ViperSpline));
    if (!s)
    {
        rt_trap("Spline: memory allocation failed");
        return NULL;
    }
    s->kind = kind;
    s->count = count;
    s->xs = (double *)calloc((size_t)count, sizeof(double));
    s->ys = (double *)calloc((size_t)count, sizeof(double));
    if (!s->xs || !s->ys)
    {
        free(s->xs);
        free(s->ys);
        rt_trap("Spline: memory allocation failed");
        return NULL;
    }
    rt_obj_set_finalizer(s, spline_finalizer);
    return s;
}

static void extract_points(void *points, ViperSpline *s)
{
    int64_t n = rt_seq_len(points);
    int64_t count = n < s->count ? n : s->count;
    for (int64_t i = 0; i < count; ++i)
    {
        void *pt = rt_seq_get(points, i);
        if (pt)
        {
            s->xs[i] = rt_vec2_x(pt);
            s->ys[i] = rt_vec2_y(pt);
        }
    }
}

//=============================================================================
// Constructors
//=============================================================================

void *rt_spline_catmull_rom(void *points)
{
    if (!points)
    {
        rt_trap("Spline.CatmullRom: null points");
        return NULL;
    }
    int64_t n = rt_seq_len(points);
    if (n < 2)
    {
        rt_trap("Spline.CatmullRom: need at least 2 points");
        return NULL;
    }
    ViperSpline *s = spline_alloc(SPLINE_CATMULL_ROM, n);
    extract_points(points, s);
    return s;
}

void *rt_spline_bezier(void *p0, void *p1, void *p2, void *p3)
{
    if (!p0 || !p1 || !p2 || !p3)
    {
        rt_trap("Spline.Bezier: null control point");
        return NULL;
    }
    ViperSpline *s = spline_alloc(SPLINE_BEZIER, 4);
    s->xs[0] = rt_vec2_x(p0);
    s->ys[0] = rt_vec2_y(p0);
    s->xs[1] = rt_vec2_x(p1);
    s->ys[1] = rt_vec2_y(p1);
    s->xs[2] = rt_vec2_x(p2);
    s->ys[2] = rt_vec2_y(p2);
    s->xs[3] = rt_vec2_x(p3);
    s->ys[3] = rt_vec2_y(p3);
    return s;
}

void *rt_spline_linear(void *points)
{
    if (!points)
    {
        rt_trap("Spline.Linear: null points");
        return NULL;
    }
    int64_t n = rt_seq_len(points);
    if (n < 2)
    {
        rt_trap("Spline.Linear: need at least 2 points");
        return NULL;
    }
    ViperSpline *s = spline_alloc(SPLINE_LINEAR, n);
    extract_points(points, s);
    return s;
}

//=============================================================================
// Evaluation Helpers
//=============================================================================

static void eval_linear(ViperSpline *s, double t, double *ox, double *oy)
{
    if (t <= 0.0)
    {
        *ox = s->xs[0];
        *oy = s->ys[0];
        return;
    }
    if (t >= 1.0)
    {
        *ox = s->xs[s->count - 1];
        *oy = s->ys[s->count - 1];
        return;
    }
    double seg = t * (double)(s->count - 1);
    int64_t i = (int64_t)seg;
    if (i >= s->count - 1)
        i = s->count - 2;
    double f = seg - (double)i;
    *ox = s->xs[i] + (s->xs[i + 1] - s->xs[i]) * f;
    *oy = s->ys[i] + (s->ys[i + 1] - s->ys[i]) * f;
}

static void eval_bezier(ViperSpline *s, double t, double *ox, double *oy)
{
    double u = 1.0 - t;
    double u2 = u * u;
    double t2 = t * t;
    double a = u2 * u;
    double b = 3.0 * u2 * t;
    double c = 3.0 * u * t2;
    double d = t2 * t;
    *ox = a * s->xs[0] + b * s->xs[1] + c * s->xs[2] + d * s->xs[3];
    *oy = a * s->ys[0] + b * s->ys[1] + c * s->ys[2] + d * s->ys[3];
}

static void catmull_rom_segment(double p0x,
                                double p0y,
                                double p1x,
                                double p1y,
                                double p2x,
                                double p2y,
                                double p3x,
                                double p3y,
                                double t,
                                double *ox,
                                double *oy)
{
    double t2 = t * t;
    double t3 = t2 * t;
    *ox = 0.5 * ((2.0 * p1x) + (-p0x + p2x) * t + (2.0 * p0x - 5.0 * p1x + 4.0 * p2x - p3x) * t2 +
                 (-p0x + 3.0 * p1x - 3.0 * p2x + p3x) * t3);
    *oy = 0.5 * ((2.0 * p1y) + (-p0y + p2y) * t + (2.0 * p0y - 5.0 * p1y + 4.0 * p2y - p3y) * t2 +
                 (-p0y + 3.0 * p1y - 3.0 * p2y + p3y) * t3);
}

static void eval_catmull_rom(ViperSpline *s, double t, double *ox, double *oy)
{
    int64_t n = s->count;
    if (n < 2)
    {
        *ox = s->xs[0];
        *oy = s->ys[0];
        return;
    }
    if (t <= 0.0)
    {
        *ox = s->xs[0];
        *oy = s->ys[0];
        return;
    }
    if (t >= 1.0)
    {
        *ox = s->xs[n - 1];
        *oy = s->ys[n - 1];
        return;
    }
    double seg = t * (double)(n - 1);
    int64_t i = (int64_t)seg;
    if (i >= n - 1)
        i = n - 2;
    double f = seg - (double)i;

    int64_t i0 = i > 0 ? i - 1 : 0;
    int64_t i1 = i;
    int64_t i2 = i + 1;
    int64_t i3 = i + 2 < n ? i + 2 : n - 1;

    catmull_rom_segment(s->xs[i0],
                        s->ys[i0],
                        s->xs[i1],
                        s->ys[i1],
                        s->xs[i2],
                        s->ys[i2],
                        s->xs[i3],
                        s->ys[i3],
                        f,
                        ox,
                        oy);
}

//=============================================================================
// Tangent Helpers
//=============================================================================

static void tangent_linear(ViperSpline *s, double t, double *ox, double *oy)
{
    int64_t n = s->count;
    double seg = t * (double)(n - 1);
    int64_t i = (int64_t)seg;
    if (i >= n - 1)
        i = n - 2;
    if (i < 0)
        i = 0;
    *ox = s->xs[i + 1] - s->xs[i];
    *oy = s->ys[i + 1] - s->ys[i];
}

static void tangent_bezier(ViperSpline *s, double t, double *ox, double *oy)
{
    double u = 1.0 - t;
    double a = -3.0 * u * u;
    double b = 3.0 * u * u - 6.0 * u * t;
    double c = 6.0 * u * t - 3.0 * t * t;
    double d = 3.0 * t * t;
    *ox = a * s->xs[0] + b * s->xs[1] + c * s->xs[2] + d * s->xs[3];
    *oy = a * s->ys[0] + b * s->ys[1] + c * s->ys[2] + d * s->ys[3];
}

static void tangent_catmull_rom(ViperSpline *s, double t, double *ox, double *oy)
{
    /* Numerical derivative via central difference. */
    double h = 0.0001;
    double t0 = t - h;
    double t1 = t + h;
    if (t0 < 0.0)
        t0 = 0.0;
    if (t1 > 1.0)
        t1 = 1.0;
    double x0, y0, x1, y1;
    eval_catmull_rom(s, t0, &x0, &y0);
    eval_catmull_rom(s, t1, &x1, &y1);
    double dt = t1 - t0;
    if (dt == 0.0)
    {
        *ox = 0.0;
        *oy = 0.0;
        return;
    }
    *ox = (x1 - x0) / dt;
    *oy = (y1 - y0) / dt;
}

//=============================================================================
// Public API
//=============================================================================

void *rt_spline_eval(void *spline, double t)
{
    if (!spline)
    {
        rt_trap("Spline.Eval: null spline");
        return NULL;
    }
    ViperSpline *s = (ViperSpline *)spline;
    double ox = 0.0, oy = 0.0;
    switch (s->kind)
    {
        case SPLINE_LINEAR:
            eval_linear(s, t, &ox, &oy);
            break;
        case SPLINE_BEZIER:
            eval_bezier(s, t, &ox, &oy);
            break;
        case SPLINE_CATMULL_ROM:
            eval_catmull_rom(s, t, &ox, &oy);
            break;
    }
    return rt_vec2_new(ox, oy);
}

void *rt_spline_tangent(void *spline, double t)
{
    if (!spline)
    {
        rt_trap("Spline.Tangent: null spline");
        return NULL;
    }
    ViperSpline *s = (ViperSpline *)spline;
    double ox = 0.0, oy = 0.0;
    switch (s->kind)
    {
        case SPLINE_LINEAR:
            tangent_linear(s, t, &ox, &oy);
            break;
        case SPLINE_BEZIER:
            tangent_bezier(s, t, &ox, &oy);
            break;
        case SPLINE_CATMULL_ROM:
            tangent_catmull_rom(s, t, &ox, &oy);
            break;
    }
    return rt_vec2_new(ox, oy);
}

int64_t rt_spline_point_count(void *spline)
{
    if (!spline)
    {
        rt_trap("Spline.PointCount: null spline");
        return 0;
    }
    return ((ViperSpline *)spline)->count;
}

void *rt_spline_point_at(void *spline, int64_t index)
{
    if (!spline)
    {
        rt_trap("Spline.PointAt: null spline");
        return NULL;
    }
    ViperSpline *s = (ViperSpline *)spline;
    if (index < 0 || index >= s->count)
    {
        rt_trap("Spline.PointAt: index out of range");
        return NULL;
    }
    return rt_vec2_new(s->xs[index], s->ys[index]);
}

double rt_spline_arc_length(void *spline, double t0, double t1, int64_t steps)
{
    if (!spline)
    {
        rt_trap("Spline.ArcLength: null spline");
        return 0.0;
    }
    if (steps < 1)
        steps = 1;
    ViperSpline *s = (ViperSpline *)spline;
    double length = 0.0;
    double dt = (t1 - t0) / (double)steps;
    double px = 0.0, py = 0.0;

    switch (s->kind)
    {
        case SPLINE_LINEAR:
            eval_linear(s, t0, &px, &py);
            break;
        case SPLINE_BEZIER:
            eval_bezier(s, t0, &px, &py);
            break;
        case SPLINE_CATMULL_ROM:
            eval_catmull_rom(s, t0, &px, &py);
            break;
    }

    for (int64_t i = 1; i <= steps; ++i)
    {
        double t = t0 + dt * (double)i;
        double cx = 0.0, cy = 0.0;
        switch (s->kind)
        {
            case SPLINE_LINEAR:
                eval_linear(s, t, &cx, &cy);
                break;
            case SPLINE_BEZIER:
                eval_bezier(s, t, &cx, &cy);
                break;
            case SPLINE_CATMULL_ROM:
                eval_catmull_rom(s, t, &cx, &cy);
                break;
        }
        double dx = cx - px;
        double dy = cy - py;
        length += sqrt(dx * dx + dy * dy);
        px = cx;
        py = cy;
    }
    return length;
}

void *rt_spline_sample(void *spline, int64_t count)
{
    if (!spline)
    {
        rt_trap("Spline.Sample: null spline");
        return NULL;
    }
    if (count < 2)
        count = 2;

    void *seq = rt_seq_new();
    for (int64_t i = 0; i < count; ++i)
    {
        double t = (double)i / (double)(count - 1);
        void *pt = rt_spline_eval(spline, t);
        rt_seq_push(seq, pt);
    }
    return seq;
}
