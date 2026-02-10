//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/RTSplineTests.cpp
// Purpose: Tests for Viper.Spline curve interpolation utilities.
//
//===----------------------------------------------------------------------===//

#include "rt_internal.h"
#include "rt_seq.h"
#include "rt_spline.h"
#include "rt_vec2.h"

#include <cassert>
#include <cmath>
#include <cstdio>

extern "C" void vm_trap(const char *msg)
{
    rt_abort(msg);
}

static const double EPSILON = 1e-6;

static bool approx_eq(double a, double b)
{
    return fabs(a - b) < EPSILON;
}

/* Helper: create a Seq of Vec2 from arrays. */
static void *make_points(const double *xs, const double *ys, int n)
{
    void *seq = rt_seq_new();
    for (int i = 0; i < n; ++i)
    {
        rt_seq_push(seq, rt_vec2_new(xs[i], ys[i]));
    }
    return seq;
}

// ============================================================================
// Linear spline
// ============================================================================

static void test_linear_endpoints()
{
    double xs[] = {0.0, 10.0};
    double ys[] = {0.0, 20.0};
    void *pts = make_points(xs, ys, 2);
    void *s = rt_spline_linear(pts);
    assert(s != nullptr);

    /* t=0 should be first point */
    void *p0 = rt_spline_eval(s, 0.0);
    assert(approx_eq(rt_vec2_x(p0), 0.0));
    assert(approx_eq(rt_vec2_y(p0), 0.0));

    /* t=1 should be last point */
    void *p1 = rt_spline_eval(s, 1.0);
    assert(approx_eq(rt_vec2_x(p1), 10.0));
    assert(approx_eq(rt_vec2_y(p1), 20.0));

    printf("test_linear_endpoints: PASSED\n");
}

static void test_linear_midpoint()
{
    double xs[] = {0.0, 10.0};
    double ys[] = {0.0, 20.0};
    void *pts = make_points(xs, ys, 2);
    void *s = rt_spline_linear(pts);

    void *mid = rt_spline_eval(s, 0.5);
    assert(approx_eq(rt_vec2_x(mid), 5.0));
    assert(approx_eq(rt_vec2_y(mid), 10.0));

    printf("test_linear_midpoint: PASSED\n");
}

static void test_linear_multi_segment()
{
    double xs[] = {0.0, 10.0, 20.0};
    double ys[] = {0.0, 10.0, 0.0};
    void *pts = make_points(xs, ys, 3);
    void *s = rt_spline_linear(pts);

    /* t=0.5 should be at the middle control point (10,10) */
    void *mid = rt_spline_eval(s, 0.5);
    assert(approx_eq(rt_vec2_x(mid), 10.0));
    assert(approx_eq(rt_vec2_y(mid), 10.0));

    /* t=0.25 should be midpoint of first segment (5,5) */
    void *q1 = rt_spline_eval(s, 0.25);
    assert(approx_eq(rt_vec2_x(q1), 5.0));
    assert(approx_eq(rt_vec2_y(q1), 5.0));

    printf("test_linear_multi_segment: PASSED\n");
}

static void test_linear_clamp()
{
    double xs[] = {0.0, 10.0};
    double ys[] = {0.0, 20.0};
    void *pts = make_points(xs, ys, 2);
    void *s = rt_spline_linear(pts);

    /* t < 0 should clamp to first point */
    void *before = rt_spline_eval(s, -1.0);
    assert(approx_eq(rt_vec2_x(before), 0.0));
    assert(approx_eq(rt_vec2_y(before), 0.0));

    /* t > 1 should clamp to last point */
    void *after = rt_spline_eval(s, 2.0);
    assert(approx_eq(rt_vec2_x(after), 10.0));
    assert(approx_eq(rt_vec2_y(after), 20.0));

    printf("test_linear_clamp: PASSED\n");
}

// ============================================================================
// Bezier spline
// ============================================================================

static void test_bezier_endpoints()
{
    void *p0 = rt_vec2_new(0.0, 0.0);
    void *p1 = rt_vec2_new(1.0, 2.0);
    void *p2 = rt_vec2_new(3.0, 2.0);
    void *p3 = rt_vec2_new(4.0, 0.0);
    void *s = rt_spline_bezier(p0, p1, p2, p3);
    assert(s != nullptr);

    /* t=0 should be p0 */
    void *r0 = rt_spline_eval(s, 0.0);
    assert(approx_eq(rt_vec2_x(r0), 0.0));
    assert(approx_eq(rt_vec2_y(r0), 0.0));

    /* t=1 should be p3 */
    void *r1 = rt_spline_eval(s, 1.0);
    assert(approx_eq(rt_vec2_x(r1), 4.0));
    assert(approx_eq(rt_vec2_y(r1), 0.0));

    printf("test_bezier_endpoints: PASSED\n");
}

static void test_bezier_midpoint()
{
    /* Symmetric bezier: (0,0), (0,2), (4,2), (4,0) */
    void *p0 = rt_vec2_new(0.0, 0.0);
    void *p1 = rt_vec2_new(0.0, 2.0);
    void *p2 = rt_vec2_new(4.0, 2.0);
    void *p3 = rt_vec2_new(4.0, 0.0);
    void *s = rt_spline_bezier(p0, p1, p2, p3);

    void *mid = rt_spline_eval(s, 0.5);
    /* At t=0.5 for cubic bezier: x = 0.125*0 + 0.375*0 + 0.375*4 + 0.125*4 = 2.0 */
    assert(approx_eq(rt_vec2_x(mid), 2.0));
    /* y = 0.125*0 + 0.375*2 + 0.375*2 + 0.125*0 = 1.5 */
    assert(approx_eq(rt_vec2_y(mid), 1.5));

    printf("test_bezier_midpoint: PASSED\n");
}

// ============================================================================
// Catmull-Rom spline
// ============================================================================

static void test_catmull_rom_endpoints()
{
    double xs[] = {0.0, 1.0, 2.0, 3.0};
    double ys[] = {0.0, 1.0, 1.0, 0.0};
    void *pts = make_points(xs, ys, 4);
    void *s = rt_spline_catmull_rom(pts);
    assert(s != nullptr);

    /* t=0 should be first point */
    void *r0 = rt_spline_eval(s, 0.0);
    assert(approx_eq(rt_vec2_x(r0), 0.0));
    assert(approx_eq(rt_vec2_y(r0), 0.0));

    /* t=1 should be last point */
    void *r1 = rt_spline_eval(s, 1.0);
    assert(approx_eq(rt_vec2_x(r1), 3.0));
    assert(approx_eq(rt_vec2_y(r1), 0.0));

    printf("test_catmull_rom_endpoints: PASSED\n");
}

static void test_catmull_rom_passes_through_controls()
{
    /* Catmull-Rom passes through all control points */
    double xs[] = {0.0, 1.0, 2.0, 3.0};
    double ys[] = {0.0, 1.0, 1.0, 0.0};
    void *pts = make_points(xs, ys, 4);
    void *s = rt_spline_catmull_rom(pts);

    /* t=1/3 should be at the second control point (1,1) */
    double t1 = 1.0 / 3.0;
    void *r1 = rt_spline_eval(s, t1);
    assert(approx_eq(rt_vec2_x(r1), 1.0));
    assert(approx_eq(rt_vec2_y(r1), 1.0));

    /* t=2/3 should be at the third control point (2,1) */
    double t2 = 2.0 / 3.0;
    void *r2 = rt_spline_eval(s, t2);
    assert(approx_eq(rt_vec2_x(r2), 2.0));
    assert(approx_eq(rt_vec2_y(r2), 1.0));

    printf("test_catmull_rom_passes_through_controls: PASSED\n");
}

// ============================================================================
// Point access
// ============================================================================

static void test_point_count()
{
    double xs[] = {0.0, 1.0, 2.0};
    double ys[] = {0.0, 1.0, 0.0};
    void *pts = make_points(xs, ys, 3);
    void *s = rt_spline_linear(pts);

    assert(rt_spline_point_count(s) == 3);
    printf("test_point_count: PASSED\n");
}

static void test_point_at()
{
    double xs[] = {10.0, 20.0, 30.0};
    double ys[] = {5.0, 15.0, 25.0};
    void *pts = make_points(xs, ys, 3);
    void *s = rt_spline_linear(pts);

    void *p0 = rt_spline_point_at(s, 0);
    assert(approx_eq(rt_vec2_x(p0), 10.0));
    assert(approx_eq(rt_vec2_y(p0), 5.0));

    void *p2 = rt_spline_point_at(s, 2);
    assert(approx_eq(rt_vec2_x(p2), 30.0));
    assert(approx_eq(rt_vec2_y(p2), 25.0));

    printf("test_point_at: PASSED\n");
}

static void test_bezier_point_count()
{
    void *s = rt_spline_bezier(
        rt_vec2_new(0.0, 0.0), rt_vec2_new(1.0, 1.0),
        rt_vec2_new(2.0, 1.0), rt_vec2_new(3.0, 0.0));
    assert(rt_spline_point_count(s) == 4);
    printf("test_bezier_point_count: PASSED\n");
}

// ============================================================================
// Tangent
// ============================================================================

static void test_linear_tangent()
{
    double xs[] = {0.0, 10.0};
    double ys[] = {0.0, 20.0};
    void *pts = make_points(xs, ys, 2);
    void *s = rt_spline_linear(pts);

    void *t = rt_spline_tangent(s, 0.5);
    /* Linear tangent should be the segment direction: (10, 20) */
    assert(approx_eq(rt_vec2_x(t), 10.0));
    assert(approx_eq(rt_vec2_y(t), 20.0));

    printf("test_linear_tangent: PASSED\n");
}

static void test_bezier_tangent()
{
    /* Straight-line bezier: tangent should be in the positive X direction */
    void *s = rt_spline_bezier(
        rt_vec2_new(0.0, 0.0), rt_vec2_new(1.0, 0.0),
        rt_vec2_new(2.0, 0.0), rt_vec2_new(3.0, 0.0));

    void *t = rt_spline_tangent(s, 0.5);
    /* For a straight line, tangent Y should be ~0 */
    assert(approx_eq(rt_vec2_y(t), 0.0));
    /* Tangent X should be positive */
    assert(rt_vec2_x(t) > 0.0);

    printf("test_bezier_tangent: PASSED\n");
}

// ============================================================================
// Arc length
// ============================================================================

static void test_linear_arc_length()
{
    /* Straight line from (0,0) to (3,4): length = 5 */
    double xs[] = {0.0, 3.0};
    double ys[] = {0.0, 4.0};
    void *pts = make_points(xs, ys, 2);
    void *s = rt_spline_linear(pts);

    double len = rt_spline_arc_length(s, 0.0, 1.0, 100);
    assert(approx_eq(len, 5.0));

    printf("test_linear_arc_length: PASSED\n");
}

static void test_arc_length_partial()
{
    /* Half of a straight line from (0,0) to (10,0): length = 5 */
    double xs[] = {0.0, 10.0};
    double ys[] = {0.0, 0.0};
    void *pts = make_points(xs, ys, 2);
    void *s = rt_spline_linear(pts);

    double len = rt_spline_arc_length(s, 0.0, 0.5, 100);
    assert(approx_eq(len, 5.0));

    printf("test_arc_length_partial: PASSED\n");
}

// ============================================================================
// Sample
// ============================================================================

static void test_sample()
{
    double xs[] = {0.0, 10.0};
    double ys[] = {0.0, 20.0};
    void *pts = make_points(xs, ys, 2);
    void *s = rt_spline_linear(pts);

    void *samples = rt_spline_sample(s, 3);
    assert(rt_seq_len(samples) == 3);

    /* First sample should be at t=0 */
    void *s0 = rt_seq_get(samples, 0);
    assert(approx_eq(rt_vec2_x(s0), 0.0));
    assert(approx_eq(rt_vec2_y(s0), 0.0));

    /* Middle sample should be at t=0.5 */
    void *s1 = rt_seq_get(samples, 1);
    assert(approx_eq(rt_vec2_x(s1), 5.0));
    assert(approx_eq(rt_vec2_y(s1), 10.0));

    /* Last sample should be at t=1 */
    void *s2 = rt_seq_get(samples, 2);
    assert(approx_eq(rt_vec2_x(s2), 10.0));
    assert(approx_eq(rt_vec2_y(s2), 20.0));

    printf("test_sample: PASSED\n");
}

// ============================================================================
// Main
// ============================================================================

int main()
{
    printf("=== Viper.Spline Tests ===\n\n");

    /* Linear */
    test_linear_endpoints();
    test_linear_midpoint();
    test_linear_multi_segment();
    test_linear_clamp();

    /* Bezier */
    test_bezier_endpoints();
    test_bezier_midpoint();

    /* Catmull-Rom */
    test_catmull_rom_endpoints();
    test_catmull_rom_passes_through_controls();

    /* Point access */
    test_point_count();
    test_point_at();
    test_bezier_point_count();

    /* Tangent */
    test_linear_tangent();
    test_bezier_tangent();

    /* Arc length */
    test_linear_arc_length();
    test_arc_length_partial();

    /* Sample */
    test_sample();

    printf("\nAll Spline tests passed!\n");
    return 0;
}
