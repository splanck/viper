//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/unit/test_rt_raycast3d.cpp
// Purpose: Unit tests for Ray3D, AABB3D collision — ray-triangle, ray-AABB,
//   ray-sphere, AABB overlap, AABB penetration.
//
// Key invariants:
//   - Retained BVH hits match an independent exhaustive triangle reference.
//   - Geometry revisions rebuild once and preserve deterministic closest-hit ties.
// Ownership/Lifetime:
//   - Query and mesh objects are owned by the runtime for each test case.
//
// Links: rt_raycast3d.h
//
//===----------------------------------------------------------------------===//

#ifndef ZANNA_ENABLE_GRAPHICS
#define ZANNA_ENABLE_GRAPHICS 1
#endif

#include "rt.hpp"
#include "rt_canvas3d.h"
#include "rt_canvas3d_internal.h"
#include "rt_internal.h"
#include "rt_raycast3d.h"
#include "rt_string.h"
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>

extern "C" {
extern void *rt_vec3_new(double x, double y, double z);
extern double rt_vec3_x(void *v);
extern double rt_vec3_y(void *v);
extern double rt_vec3_z(void *v);
extern void *rt_mesh3d_new_box(double w, double h, double d);
extern void *rt_mat4_identity(void);
extern void *rt_mat4_translate(double tx, double ty, double tz);
}

static int tests_passed = 0;
static int tests_run = 0;

#define EXPECT_TRUE(cond, msg)                                                                     \
    do {                                                                                           \
        tests_run++;                                                                               \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "FAIL: %s\n", msg);                                                    \
        } else {                                                                                   \
            tests_passed++;                                                                        \
        }                                                                                          \
    } while (0)

#define EXPECT_NEAR(a, b, eps, msg)                                                                \
    do {                                                                                           \
        tests_run++;                                                                               \
        if (fabs((double)(a) - (double)(b)) > (eps)) {                                             \
            fprintf(stderr, "FAIL: %s (got %f, expected %f)\n", msg, (double)(a), (double)(b));    \
        } else {                                                                                   \
            tests_passed++;                                                                        \
        }                                                                                          \
    } while (0)

static void test_ray_triangle_hit() {
    void *origin = rt_vec3_new(0, 0, 5);
    void *dir = rt_vec3_new(0, 0, -1);
    void *v0 = rt_vec3_new(-1, -1, 0);
    void *v1 = rt_vec3_new(1, -1, 0);
    void *v2 = rt_vec3_new(0, 1, 0);
    double t = rt_ray3d_intersect_triangle(origin, dir, v0, v1, v2);
    EXPECT_NEAR(t, 5.0, 0.01, "Ray-triangle hit at distance 5");
}

static void test_ray_triangle_miss() {
    void *origin = rt_vec3_new(5, 5, 5);
    void *dir = rt_vec3_new(0, 0, -1);
    void *v0 = rt_vec3_new(-1, -1, 0);
    void *v1 = rt_vec3_new(1, -1, 0);
    void *v2 = rt_vec3_new(0, 1, 0);
    double t = rt_ray3d_intersect_triangle(origin, dir, v0, v1, v2);
    EXPECT_TRUE(t < 0, "Ray-triangle miss returns -1");
}

static void test_ray_triangle_parallel() {
    void *origin = rt_vec3_new(0, 0, 5);
    void *dir = rt_vec3_new(1, 0, 0); /* parallel to triangle */
    void *v0 = rt_vec3_new(-1, -1, 0);
    void *v1 = rt_vec3_new(1, -1, 0);
    void *v2 = rt_vec3_new(0, 1, 0);
    double t = rt_ray3d_intersect_triangle(origin, dir, v0, v1, v2);
    EXPECT_TRUE(t < 0, "Ray parallel to triangle returns -1");
}

static void test_ray_aabb_hit() {
    void *origin = rt_vec3_new(0, 0, 5);
    void *dir = rt_vec3_new(0, 0, -1);
    void *mn = rt_vec3_new(-1, -1, -1);
    void *mx = rt_vec3_new(1, 1, 1);
    double t = rt_ray3d_intersect_aabb(origin, dir, mn, mx);
    EXPECT_NEAR(t, 4.0, 0.01, "Ray-AABB hit at distance 4");
}

static void test_ray_aabb_miss() {
    void *origin = rt_vec3_new(5, 5, 5);
    void *dir = rt_vec3_new(0, 1, 0); /* pointing away */
    void *mn = rt_vec3_new(-1, -1, -1);
    void *mx = rt_vec3_new(1, 1, 1);
    double t = rt_ray3d_intersect_aabb(origin, dir, mn, mx);
    EXPECT_TRUE(t < 0, "Ray-AABB miss returns -1");
}

static void test_ray_aabb_inside() {
    void *origin = rt_vec3_new(0, 0, 0);
    void *dir = rt_vec3_new(0, 0, -1);
    void *mn = rt_vec3_new(-1, -1, -1);
    void *mx = rt_vec3_new(1, 1, 1);
    double t = rt_ray3d_intersect_aabb(origin, dir, mn, mx);
    EXPECT_NEAR(t, 0.0, 0.01, "Ray inside AABB returns 0");
}

static void test_ray_aabb_rejects_zero_direction_inside_box() {
    void *origin = rt_vec3_new(0, 0, 0);
    void *dir = rt_vec3_new(0, 0, 0);
    void *mn = rt_vec3_new(-1, -1, -1);
    void *mx = rt_vec3_new(1, 1, 1);
    double t = rt_ray3d_intersect_aabb(origin, dir, mn, mx);
    EXPECT_TRUE(t < 0.0, "Ray-AABB rejects zero-length direction even from inside the box");
}

static void test_ray_sphere_hit() {
    void *origin = rt_vec3_new(0, 0, 5);
    void *dir = rt_vec3_new(0, 0, -1);
    void *center = rt_vec3_new(0, 0, 0);
    double t = rt_ray3d_intersect_sphere(origin, dir, center, 1.0);
    EXPECT_NEAR(t, 4.0, 0.01, "Ray-sphere hit at distance 4");
}

static void test_ray_sphere_inside_returns_zero() {
    void *origin = rt_vec3_new(0, 0, 0);
    void *dir = rt_vec3_new(0, 0, -2);
    void *center = rt_vec3_new(0, 0, 0);
    double t = rt_ray3d_intersect_sphere(origin, dir, center, 1.0);
    EXPECT_NEAR(t, 0.0, 0.01, "Ray-sphere returns zero when origin is inside");
}

static void test_ray_sphere_miss() {
    void *origin = rt_vec3_new(5, 5, 5);
    void *dir = rt_vec3_new(0, 1, 0);
    void *center = rt_vec3_new(0, 0, 0);
    double t = rt_ray3d_intersect_sphere(origin, dir, center, 1.0);
    EXPECT_TRUE(t < 0, "Ray-sphere miss returns -1");
}

static void test_ray_sphere_rejects_invalid_inputs() {
    void *origin = rt_vec3_new(0, 0, 5);
    void *zero_dir = rt_vec3_new(0, 0, 0);
    void *bad_dir = rt_vec3_new(NAN, 0, -1);
    void *center = rt_vec3_new(0, 0, 0);
    EXPECT_TRUE(rt_ray3d_intersect_sphere(origin, zero_dir, center, 1.0) < 0,
                "Ray-sphere rejects zero-length direction");
    EXPECT_TRUE(rt_ray3d_intersect_sphere(origin, bad_dir, center, 1.0) < 0,
                "Ray-sphere rejects non-finite direction");
    EXPECT_TRUE(rt_ray3d_intersect_sphere(origin, rt_vec3_new(0, 0, -1), center, NAN) < 0,
                "Ray-sphere rejects non-finite radius");
}

static void test_aabb_overlaps() {
    void *a0 = rt_vec3_new(0, 0, 0), *a1 = rt_vec3_new(2, 2, 2);
    void *b0 = rt_vec3_new(1, 1, 1), *b1 = rt_vec3_new(3, 3, 3);
    EXPECT_TRUE(rt_aabb3d_overlaps(a0, a1, b0, b1) == 1, "Overlapping AABBs");
}

static void test_aabb_separate() {
    void *a0 = rt_vec3_new(0, 0, 0), *a1 = rt_vec3_new(1, 1, 1);
    void *b0 = rt_vec3_new(5, 5, 5), *b1 = rt_vec3_new(6, 6, 6);
    EXPECT_TRUE(rt_aabb3d_overlaps(a0, a1, b0, b1) == 0, "Separate AABBs");
}

static void test_aabb_penetration() {
    void *a0 = rt_vec3_new(0, 0, 0), *a1 = rt_vec3_new(2, 2, 2);
    void *b0 = rt_vec3_new(1.5, 0, 0), *b1 = rt_vec3_new(3.5, 2, 2);
    void *pen = rt_aabb3d_penetration(a0, a1, b0, b1);
    /* Overlap on X is 0.5, Y is 2.0, Z is 2.0. Min overlap = X. */
    double px = fabs(rt_vec3_x(pen));
    EXPECT_NEAR(px, 0.5, 0.01, "AABB penetration X = 0.5");
    EXPECT_NEAR(rt_vec3_y(pen), 0.0, 0.01, "AABB penetration Y = 0 (not min axis)");
}

static void test_aabb_penetration_uses_per_axis_centers() {
    void *a0 = rt_vec3_new(-100.0, 10.0, 0.0);
    void *a1 = rt_vec3_new(-98.0, 12.0, 2.0);
    void *b0 = rt_vec3_new(-99.0, 9.5, 0.0);
    void *b1 = rt_vec3_new(-97.0, 10.5, 2.0);
    void *pen = rt_aabb3d_penetration(a0, a1, b0, b1);
    EXPECT_NEAR(rt_vec3_x(pen), 0.0, 0.01, "AABB Y-min penetration has zero X");
    EXPECT_NEAR(rt_vec3_y(pen), 0.5, 0.01, "AABB Y-min penetration sign uses Y centers");
    EXPECT_NEAR(rt_vec3_z(pen), 0.0, 0.01, "AABB Y-min penetration has zero Z");
}

static void test_sphere_penetration_coincident_centers() {
    void *center = rt_vec3_new(0, 0, 0);
    void *pen = rt_sphere3d_penetration(center, 1.0, center, 1.0);
    EXPECT_NEAR(rt_vec3_x(pen), 0.0, 0.01, "Coincident sphere penetration X = 0");
    EXPECT_NEAR(rt_vec3_y(pen), 2.0, 0.01, "Coincident sphere penetration returns stable depth");
    EXPECT_NEAR(rt_vec3_z(pen), 0.0, 0.01, "Coincident sphere penetration Z = 0");
}

static void test_sphere_penetration_pushes_a_away_from_b() {
    void *a = rt_vec3_new(0, 0, 0);
    void *b = rt_vec3_new(0.5, 0, 0);
    void *pen = rt_sphere3d_penetration(a, 1.0, b, 1.0);
    EXPECT_NEAR(rt_vec3_x(pen), -1.5, 0.01, "Sphere penetration vector pushes A away from B");
    EXPECT_NEAR(rt_vec3_y(pen), 0.0, 0.01, "Sphere penetration side axis remains zero");
}

static void test_ray_mesh() {
    void *origin = rt_vec3_new(0, 0, 5);
    void *dir = rt_vec3_new(0, 0, -1);
    void *box = rt_mesh3d_new_box(2.0, 2.0, 2.0);
    void *hit = rt_ray3d_intersect_mesh(origin, dir, box, rt_mat4_identity());
    EXPECT_TRUE(hit != nullptr, "Ray-mesh hit returns non-null");
    if (hit) {
        EXPECT_NEAR(rt_ray3d_hit_distance(hit), 4.0, 0.01, "Ray-mesh distance = 4");
        void *pt = rt_ray3d_hit_point(hit);
        EXPECT_NEAR(rt_vec3_z(pt), 1.0, 0.01, "Hit point Z = 1 (front face of box)");
    }
}

static void test_ray_mesh_unnormalized_direction_reports_world_distance() {
    void *origin = rt_vec3_new(0, 0, 5);
    void *dir = rt_vec3_new(0, 0, -2);
    void *box = rt_mesh3d_new_box(2.0, 2.0, 2.0);
    void *hit = rt_ray3d_intersect_mesh(origin, dir, box, rt_mat4_identity());
    EXPECT_TRUE(hit != nullptr, "Ray-mesh accepts unnormalized non-zero directions");
    if (hit) {
        EXPECT_NEAR(rt_ray3d_hit_distance(hit),
                    4.0,
                    0.01,
                    "Ray-mesh unnormalized direction reports Euclidean distance");
        void *pt = rt_ray3d_hit_point(hit);
        EXPECT_NEAR(rt_vec3_z(pt),
                    1.0,
                    0.01,
                    "Ray-mesh unnormalized direction reports the correct hit point");
    }
}

static void test_ray_mesh_repairs_corrupt_geometry_counts() {
    void *origin = rt_vec3_new(0, 0, 5);
    void *dir = rt_vec3_new(0, 0, -1);
    void *box = rt_mesh3d_new_box(2.0, 2.0, 2.0);
    rt_mesh3d *mesh = (rt_mesh3d *)box;
    uint32_t vertex_count = mesh->vertex_count;
    uint32_t index_count = mesh->index_count;
    mesh->vertex_capacity = vertex_count;
    mesh->index_capacity = index_count;
    mesh->vertex_count = UINT32_MAX;
    mesh->index_count = UINT32_MAX;

    void *hit = rt_ray3d_intersect_mesh(origin, dir, box, rt_mat4_identity());

    EXPECT_TRUE(hit != nullptr, "Ray-mesh repairs corrupt counts and still hits valid geometry");
    EXPECT_TRUE(mesh->vertex_count == vertex_count && mesh->index_count == index_count,
                "Ray-mesh repairs corrupt geometry counts before traversal");
    if (hit)
        EXPECT_NEAR(rt_ray3d_hit_distance(hit), 4.0, 0.01, "Ray-mesh repaired-count distance");
}

static void test_ray_mesh_translated() {
    void *origin = rt_vec3_new(3, 0, 5);
    void *dir = rt_vec3_new(0, 0, -1);
    void *box = rt_mesh3d_new_box(2.0, 2.0, 2.0);
    void *xf = rt_mat4_translate(3.0, 0.0, 0.0);
    void *hit = rt_ray3d_intersect_mesh(origin, dir, box, xf);
    EXPECT_TRUE(hit != nullptr, "Ray-mesh translated hit returns non-null");
    if (hit) {
        EXPECT_NEAR(rt_ray3d_hit_distance(hit), 4.0, 0.01, "Translated ray-mesh distance = 4");
        void *pt = rt_ray3d_hit_point(hit);
        EXPECT_NEAR(rt_vec3_x(pt), 3.0, 0.01, "Translated hit point X = 3");
        EXPECT_NEAR(rt_vec3_z(pt), 1.0, 0.01, "Translated hit point Z = 1");
    }
}

/// @brief Compute the deterministic closest hit against a mesh by an exhaustive triangle sweep.
/// @details This independent Möller–Trumbore reference normalizes the input direction exactly as
///          the public query does, skips incomplete/out-of-range triangles, and resolves exact
///          distance ties to the lower triangle index. It is intentionally test-local so the BVH
///          implementation cannot accidentally validate itself.
/// @param mesh Mesh payload to scan in local space.
/// @param origin Three-component ray origin.
/// @param direction Three-component non-zero ray direction.
/// @param out_distance Receives the closest normalized-ray distance when a hit exists.
/// @param out_triangle Receives the source triangle index when a hit exists.
/// @return True when at least one triangle is hit; false otherwise.
static bool ray_mesh_linear_reference(const rt_mesh3d *mesh,
                                      const double origin[3],
                                      const double direction[3],
                                      double *out_distance,
                                      int64_t *out_triangle) {
    double dir[3] = {direction[0], direction[1], direction[2]};
    double length = std::sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
    double best = INFINITY;
    int64_t best_triangle = -1;
    if (!mesh || !std::isfinite(length) || length <= 1.0e-8)
        return false;
    dir[0] /= length;
    dir[1] /= length;
    dir[2] /= length;
    uint32_t vertex_count = rt_mesh3d_safe_vertex_count(mesh);
    uint32_t triangle_count = rt_mesh3d_safe_index_count(mesh) / 3u;
    for (uint32_t triangle = 0; triangle < triangle_count; ++triangle) {
        uint32_t i0 = mesh->indices[triangle * 3u + 0u];
        uint32_t i1 = mesh->indices[triangle * 3u + 1u];
        uint32_t i2 = mesh->indices[triangle * 3u + 2u];
        if (i0 >= vertex_count || i1 >= vertex_count || i2 >= vertex_count)
            continue;
        const float *a = mesh->vertices[i0].pos;
        const float *b = mesh->vertices[i1].pos;
        const float *c = mesh->vertices[i2].pos;
        double e1[3] = {(double)b[0] - a[0], (double)b[1] - a[1], (double)b[2] - a[2]};
        double e2[3] = {(double)c[0] - a[0], (double)c[1] - a[1], (double)c[2] - a[2]};
        double p[3] = {dir[1] * e2[2] - dir[2] * e2[1],
                       dir[2] * e2[0] - dir[0] * e2[2],
                       dir[0] * e2[1] - dir[1] * e2[0]};
        double det = e1[0] * p[0] + e1[1] * p[1] + e1[2] * p[2];
        if (!std::isfinite(det) || std::fabs(det) < 1.0e-8)
            continue;
        double inv_det = 1.0 / det;
        double tv[3] = {origin[0] - a[0], origin[1] - a[1], origin[2] - a[2]};
        double u = (tv[0] * p[0] + tv[1] * p[1] + tv[2] * p[2]) * inv_det;
        if (!std::isfinite(u) || u < 0.0 || u > 1.0)
            continue;
        double q[3] = {tv[1] * e1[2] - tv[2] * e1[1],
                       tv[2] * e1[0] - tv[0] * e1[2],
                       tv[0] * e1[1] - tv[1] * e1[0]};
        double v = (dir[0] * q[0] + dir[1] * q[1] + dir[2] * q[2]) * inv_det;
        if (!std::isfinite(v) || v < 0.0 || u + v > 1.0)
            continue;
        double distance = (e2[0] * q[0] + e2[1] * q[1] + e2[2] * q[2]) * inv_det;
        if (std::isfinite(distance) && distance >= 0.0 &&
            (distance < best || (distance == best && (int64_t)triangle < best_triangle))) {
            best = distance;
            best_triangle = triangle;
        }
    }
    if (best_triangle < 0)
        return false;
    if (out_distance)
        *out_distance = best;
    if (out_triangle)
        *out_triangle = best_triangle;
    return true;
}

/// @brief Verify retained mesh-ray BVH equivalence, sublinear probes, reuse, and invalidation.
/// @details Builds a 2,048-triangle plane, compares 128 deterministic randomized rays against the
///          independent exhaustive reference, and asserts that unchanged queries keep one BVH
///          build. Appending a distant valid triangle bumps the geometry revision and must produce
///          exactly one additional rebuild while preserving the original local hit.
static void test_ray_mesh_retained_bvh_matches_linear_reference() {
    constexpr int kCells = 32;
    rt_mesh3d *mesh = (rt_mesh3d *)rt_mesh3d_new();
    void *identity = rt_mat4_identity();
    uint32_t random_state = UINT32_C(0x6d2b79f5);
    uint64_t largest_probe_count = 0;
    for (int y = 0; y <= kCells; ++y) {
        for (int x = 0; x <= kCells; ++x) {
            rt_mesh3d_add_vertex(mesh,
                                 (double)x - kCells * 0.5,
                                 (double)y - kCells * 0.5,
                                 0.0,
                                 0.0,
                                 0.0,
                                 1.0,
                                 (double)x / kCells,
                                 (double)y / kCells);
        }
    }
    for (int y = 0; y < kCells; ++y) {
        for (int x = 0; x < kCells; ++x) {
            int64_t i0 = (int64_t)y * (kCells + 1) + x;
            int64_t i1 = i0 + 1;
            int64_t i2 = i0 + (kCells + 1);
            int64_t i3 = i2 + 1;
            rt_mesh3d_add_triangle(mesh, i0, i1, i3);
            rt_mesh3d_add_triangle(mesh, i0, i3, i2);
        }
    }
    for (int sample = 0; sample < 128; ++sample) {
        auto next_unit = [&random_state]() {
            random_state = random_state * UINT32_C(1664525) + UINT32_C(1013904223);
            return (double)(random_state >> 8u) / (double)UINT32_C(0x01000000);
        };
        double origin[3] = {
            -14.0 + next_unit() * 28.0, -14.0 + next_unit() * 28.0, 4.0 + next_unit() * 12.0};
        double direction[3] = {(next_unit() - 0.5) * 0.12, (next_unit() - 0.5) * 0.12, -1.0};
        double reference_distance = 0.0;
        int64_t reference_triangle = -1;
        bool reference_hit = ray_mesh_linear_reference(
            mesh, origin, direction, &reference_distance, &reference_triangle);
        void *hit = rt_ray3d_intersect_mesh(rt_vec3_new(origin[0], origin[1], origin[2]),
                                            rt_vec3_new(direction[0], direction[1], direction[2]),
                                            mesh,
                                            identity);
        EXPECT_TRUE((hit != nullptr) == reference_hit,
                    "Retained ray BVH agrees with exhaustive hit/miss result");
        if (hit && reference_hit) {
            EXPECT_NEAR(rt_ray3d_hit_distance(hit),
                        reference_distance,
                        1.0e-6,
                        "Retained ray BVH returns exhaustive closest distance");
            EXPECT_TRUE(rt_ray3d_hit_triangle(hit) == reference_triangle,
                        "Retained ray BVH returns exhaustive deterministic triangle");
        }
        if (mesh->raycast_last_triangle_probe_count > largest_probe_count)
            largest_probe_count = mesh->raycast_last_triangle_probe_count;
    }
    EXPECT_TRUE(mesh->raycast_bvh_rebuild_count == 1,
                "Unchanged randomized mesh rays reuse one retained BVH build");
    EXPECT_TRUE(largest_probe_count < (uint64_t)(kCells * kCells * 2) / 4u,
                "Localized retained BVH rays probe fewer than one quarter of mesh triangles");

    rt_mesh3d_add_vertex(mesh, 100.0, 100.0, -5.0, 0.0, 0.0, 1.0, 0.0, 0.0);
    rt_mesh3d_add_vertex(mesh, 101.0, 100.0, -5.0, 0.0, 0.0, 1.0, 1.0, 0.0);
    rt_mesh3d_add_vertex(mesh, 100.0, 101.0, -5.0, 0.0, 0.0, 1.0, 0.0, 1.0);
    int64_t far_base = rt_mesh3d_get_vertex_count(mesh) - 3;
    rt_mesh3d_add_triangle(mesh, far_base, far_base + 1, far_base + 2);
    void *post_mutation_hit = rt_ray3d_intersect_mesh(
        rt_vec3_new(0.25, 0.25, 8.0), rt_vec3_new(0.0, 0.0, -1.0), mesh, identity);
    EXPECT_TRUE(post_mutation_hit != nullptr,
                "Ray BVH rebuild after geometry mutation preserves existing local hits");
    EXPECT_TRUE(mesh->raycast_bvh_rebuild_count == 2,
                "Geometry revision mutation triggers exactly one retained BVH rebuild");
}

static void test_ray_mesh_and_hit_accessors_reject_wrong_handles() {
    void *origin = rt_vec3_new(0, 0, 5);
    void *dir = rt_vec3_new(0, 0, -1);
    void *box = rt_mesh3d_new_box(2.0, 2.0, 2.0);
    void *identity = rt_mat4_identity();
    void *pt;

    EXPECT_TRUE(rt_ray3d_intersect_mesh(origin, dir, box, box) == nullptr,
                "Ray-mesh rejects non-Mat4 transform handles");
    EXPECT_TRUE(rt_ray3d_intersect_mesh(origin, dir, identity, identity) == nullptr,
                "Ray-mesh rejects non-Mesh3D handles");
    EXPECT_NEAR(
        rt_ray3d_hit_distance(box), -1.0, 0.01, "RayHit3D.Distance rejects non-hit handles");
    EXPECT_TRUE(rt_ray3d_hit_triangle(box) == -1, "RayHit3D.Triangle rejects non-hit handles");
    pt = rt_ray3d_hit_point(box);
    EXPECT_NEAR(rt_vec3_x(pt), 0.0, 0.01, "RayHit3D.Point returns safe zero for bad handles");
}

static void test_aabb_closest_point_surface_inside() {
    void *mn = rt_vec3_new(-1, -1, -1);
    void *mx = rt_vec3_new(1, 1, 1);
    void *p = rt_vec3_new(0.9, 0.1, 0.0);
    void *closest = rt_aabb3d_closest_point(mn, mx, p);
    EXPECT_NEAR(rt_vec3_x(closest), 1.0, 0.01, "AABB closest point snaps to nearest surface");
    EXPECT_NEAR(rt_vec3_y(closest), 0.1, 0.01, "AABB closest point preserves tangent Y");
    EXPECT_NEAR(rt_vec3_z(closest), 0.0, 0.01, "AABB closest point preserves tangent Z");
}

static void test_aabb_sphere_overlap_inside_box() {
    void *mn = rt_vec3_new(-1, -1, -1);
    void *mx = rt_vec3_new(1, 1, 1);
    void *center = rt_vec3_new(0, 0, 0);
    EXPECT_TRUE(rt_aabb3d_sphere_overlaps(mn, mx, center, 0.1) == 1,
                "AABB-sphere overlap handles centers inside the box");
}

static void test_aabb_canonicalizes_inverted_bounds() {
    void *a0 = rt_vec3_new(2, 2, 2), *a1 = rt_vec3_new(0, 0, 0);
    void *b0 = rt_vec3_new(1, 1, 1), *b1 = rt_vec3_new(3, 3, 3);
    void *closest;
    void *pen;

    EXPECT_TRUE(rt_aabb3d_overlaps(a0, a1, b0, b1) == 1,
                "AABB overlap canonicalizes inverted min/max bounds");
    pen = rt_aabb3d_penetration(a0, a1, b0, b1);
    EXPECT_NEAR(fabs(rt_vec3_x(pen)) + fabs(rt_vec3_y(pen)) + fabs(rt_vec3_z(pen)),
                1.0,
                0.01,
                "AABB penetration canonicalizes inverted min/max bounds");
    closest = rt_aabb3d_closest_point(
        rt_vec3_new(1, 1, 1), rt_vec3_new(-1, -1, -1), rt_vec3_new(3, 0, 0));
    EXPECT_NEAR(
        rt_vec3_x(closest), 1.0, 0.01, "AABB closest-point canonicalizes inverted min/max bounds");
    EXPECT_TRUE(rt_aabb3d_sphere_overlaps(a0, a1, rt_vec3_new(1, 1, 1), -1.0) == 0,
                "AABB-sphere rejects negative radii");
}

static void test_capsule_aabb_uses_exact_segment_distance() {
    void *aabb_min = rt_vec3_new(0.0, 0.0, 0.0);
    void *aabb_max = rt_vec3_new(100.0, 1.0, 1.0);
    void *cap_a = rt_vec3_new(0.0, 1.1, 0.5);
    void *cap_b = rt_vec3_new(100.0, 10.0, 0.5);

    EXPECT_TRUE(rt_capsule3d_aabb_overlaps(cap_a, cap_b, 0.2, aabb_min, aabb_max) == 1,
                "Capsule-AABB tests distance to the box, not only the box center");
}

static void test_ray_queries_clamp_extreme_finite_inputs() {
    void *origin = rt_vec3_new(1.0e300, 0.0, 5.0);
    void *dir = rt_vec3_new(-1.0e300, 0.0, -5.0);
    void *mn = rt_vec3_new(-1.0, -1.0, -1.0);
    void *mx = rt_vec3_new(1.0, 1.0, 1.0);
    double t = rt_ray3d_intersect_aabb(origin, dir, mn, mx);
    EXPECT_TRUE(std::isfinite(t), "Ray-AABB clamps extreme finite coordinates to finite distance");
    EXPECT_TRUE(t <= 1000000000.0, "Ray-AABB caps extreme finite hit distances");

    t = rt_ray3d_intersect_sphere(origin, dir, rt_vec3_new(0.0, 0.0, 0.0), 1.0e300);
    EXPECT_TRUE(std::isfinite(t),
                "Ray-sphere clamps extreme finite coordinates and radius to finite distance");
    EXPECT_TRUE(t <= 1000000000.0, "Ray-sphere caps extreme finite hit distances");
}

static void test_translated_mesh_hit_transform_aliasing() {
    void *origin = rt_vec3_new(3, 0, 5);
    void *dir = rt_vec3_new(0, 0, -1);
    void *box = rt_mesh3d_new_box(2.0, 2.0, 2.0);
    void *xf = rt_mat4_translate(3.0, 0.0, 0.0);
    void *hit = rt_ray3d_intersect_mesh(origin, dir, box, xf);
    EXPECT_TRUE(hit != nullptr, "Ray-mesh translated hit still succeeds with aliased transforms");
    if (hit) {
        void *pt = rt_ray3d_hit_point(hit);
        void *normal = rt_ray3d_hit_normal(hit);
        EXPECT_NEAR(
            rt_vec3_x(pt), 3.0, 0.01, "Translated mesh hit keeps X after alias-safe transform");
        EXPECT_NEAR(
            rt_vec3_z(pt), 1.0, 0.01, "Translated mesh hit keeps Z after alias-safe transform");
        EXPECT_TRUE(std::isfinite(rt_ray3d_hit_distance(hit)),
                    "RayHit distance accessor is finite");
        EXPECT_TRUE(std::isfinite(rt_vec3_x(normal)) && std::isfinite(rt_vec3_y(normal)) &&
                        std::isfinite(rt_vec3_z(normal)),
                    "RayHit normal accessor returns finite components");
    }
}

static void test_shape_queries_clamp_extreme_finite_inputs() {
    void *huge_a = rt_vec3_new(-1.0e300, -1.0e300, -1.0e300);
    void *huge_b = rt_vec3_new(1.0e300, 1.0e300, 1.0e300);
    void *point = rt_vec3_new(5.0e299, 0.0, 0.0);
    void *closest = rt_aabb3d_closest_point(huge_a, huge_b, point);
    EXPECT_TRUE(std::isfinite(rt_vec3_x(closest)) && std::isfinite(rt_vec3_y(closest)) &&
                    std::isfinite(rt_vec3_z(closest)),
                "AABB closest point clamps extreme finite inputs");

    void *pen =
        rt_sphere3d_penetration(rt_vec3_new(0, 0, 0), 1.0e300, rt_vec3_new(0.5, 0, 0), 1.0e300);
    EXPECT_TRUE(std::isfinite(rt_vec3_x(pen)) && std::isfinite(rt_vec3_y(pen)) &&
                    std::isfinite(rt_vec3_z(pen)),
                "Sphere penetration clamps extreme finite radii");

    EXPECT_TRUE(rt_capsule3d_aabb_overlaps(huge_a, huge_b, 1.0e300, huge_a, huge_b) == 1,
                "Capsule-AABB clamps extreme finite radius and bounds");
}

int main() {
    test_ray_triangle_hit();
    test_ray_triangle_miss();
    test_ray_triangle_parallel();
    test_ray_aabb_hit();
    test_ray_aabb_miss();
    test_ray_aabb_inside();
    test_ray_aabb_rejects_zero_direction_inside_box();
    test_ray_sphere_hit();
    test_ray_sphere_inside_returns_zero();
    test_ray_sphere_miss();
    test_ray_sphere_rejects_invalid_inputs();
    test_aabb_overlaps();
    test_aabb_separate();
    test_aabb_penetration();
    test_aabb_penetration_uses_per_axis_centers();
    test_sphere_penetration_coincident_centers();
    test_sphere_penetration_pushes_a_away_from_b();
    test_ray_mesh();
    test_ray_mesh_unnormalized_direction_reports_world_distance();
    test_ray_mesh_repairs_corrupt_geometry_counts();
    test_ray_mesh_translated();
    test_ray_mesh_retained_bvh_matches_linear_reference();
    test_ray_mesh_and_hit_accessors_reject_wrong_handles();
    test_aabb_closest_point_surface_inside();
    test_aabb_sphere_overlap_inside_box();
    test_aabb_canonicalizes_inverted_bounds();
    test_capsule_aabb_uses_exact_segment_distance();
    test_ray_queries_clamp_extreme_finite_inputs();
    test_translated_mesh_hit_transform_aliasing();
    test_shape_queries_clamp_extreme_finite_inputs();

    printf("Raycast3D tests: %d/%d passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
