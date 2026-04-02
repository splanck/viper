//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_raycast3d.c
// Purpose: 3D raycasting and AABB collision detection for picking, shooting,
//   and physics. Implements Möller–Trumbore ray-triangle intersection, slab
//   method ray-AABB, quadratic ray-sphere, and AABB overlap/penetration.
//
// Key invariants:
//   - Ray direction must be normalized for correct distance values.
//   - Möller–Trumbore returns parametric t; t < 0 means behind ray origin.
//   - Ray-mesh transforms the ray into object space via inverse model matrix.
//   - AABB penetration returns the minimum push-out vector (shortest axis).
//
// Links: rt_raycast3d.h, rt_canvas3d_internal.h
//
//===----------------------------------------------------------------------===//

#ifdef VIPER_ENABLE_GRAPHICS

#include "rt_raycast3d.h"
#include "rt_canvas3d_internal.h"

#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

extern void *rt_obj_new_i64(int64_t class_id, int64_t byte_size);
extern void rt_obj_set_finalizer(void *obj, void (*fn)(void *));
extern void *rt_vec3_new(double x, double y, double z);
extern double rt_vec3_x(void *v);
extern double rt_vec3_y(void *v);
extern double rt_vec3_z(void *v);

/*==========================================================================
 * RayHit3D — result of ray-mesh intersection
 *=========================================================================*/

typedef struct {
    double distance;
    double point[3];
    double normal[3];
    int64_t triangle_index;
} rt_rayhit3d;

/*==========================================================================
 * Möller–Trumbore ray-triangle intersection
 *
 * Returns parametric distance t (>= 0 = hit), or -1 if miss.
 * Edge vectors e1 = v1-v0, e2 = v2-v0. Uses cross products to compute
 * barycentric coordinates (u, v) and distance t simultaneously.
 *=========================================================================*/

#define EPSILON 1e-8

double rt_ray3d_intersect_triangle(
    void *origin, void *dir, void *v0_obj, void *v1_obj, void *v2_obj) {
    if (!origin || !dir || !v0_obj || !v1_obj || !v2_obj)
        return -1.0;

    double ox = rt_vec3_x(origin), oy = rt_vec3_y(origin), oz = rt_vec3_z(origin);
    double dx = rt_vec3_x(dir), dy = rt_vec3_y(dir), dz = rt_vec3_z(dir);

    double ax = rt_vec3_x(v0_obj), ay = rt_vec3_y(v0_obj), az = rt_vec3_z(v0_obj);
    double bx = rt_vec3_x(v1_obj), by = rt_vec3_y(v1_obj), bz = rt_vec3_z(v1_obj);
    double cx = rt_vec3_x(v2_obj), cy = rt_vec3_y(v2_obj), cz = rt_vec3_z(v2_obj);

    /* Edge vectors */
    double e1x = bx - ax, e1y = by - ay, e1z = bz - az;
    double e2x = cx - ax, e2y = cy - ay, e2z = cz - az;

    /* P = dir × e2 */
    double px = dy * e2z - dz * e2y;
    double py = dz * e2x - dx * e2z;
    double pz = dx * e2y - dy * e2x;

    double det = e1x * px + e1y * py + e1z * pz;
    if (fabs(det) < EPSILON)
        return -1.0; /* parallel */

    double inv_det = 1.0 / det;

    /* T = origin - v0 */
    double tx = ox - ax, ty = oy - ay, tz = oz - az;

    /* u = T · P * inv_det */
    double u = (tx * px + ty * py + tz * pz) * inv_det;
    if (u < 0.0 || u > 1.0)
        return -1.0;

    /* Q = T × e1 */
    double qx = ty * e1z - tz * e1y;
    double qy = tz * e1x - tx * e1z;
    double qz = tx * e1y - ty * e1x;

    /* v = dir · Q * inv_det */
    double v = (dx * qx + dy * qy + dz * qz) * inv_det;
    if (v < 0.0 || u + v > 1.0)
        return -1.0;

    /* t = e2 · Q * inv_det */
    double t = (e2x * qx + e2y * qy + e2z * qz) * inv_det;
    return t >= 0.0 ? t : -1.0;
}

/*==========================================================================
 * Ray-AABB intersection (slab method)
 *=========================================================================*/

/// @brief Test ray–AABB intersection using the slab method.
/// @details Returns the nearest positive intersection distance, or -1.0 on miss.
///          Uses the standard slab algorithm: project the ray onto each axis,
///          compute entry/exit intervals, and check for overlap.
/// @param origin   Vec3 ray origin.
/// @param dir      Vec3 ray direction (need not be normalized; length affects t).
/// @param aabb_min Vec3 minimum corner of the axis-aligned bounding box.
/// @param aabb_max Vec3 maximum corner of the axis-aligned bounding box.
/// @return Distance t along the ray to the nearest hit, or -1.0 on miss.
double rt_ray3d_intersect_aabb(void *origin, void *dir, void *aabb_min, void *aabb_max) {
    if (!origin || !dir || !aabb_min || !aabb_max)
        return -1.0;

    double ox = rt_vec3_x(origin), oy = rt_vec3_y(origin), oz = rt_vec3_z(origin);
    double dx = rt_vec3_x(dir), dy = rt_vec3_y(dir), dz = rt_vec3_z(dir);
    double mnx = rt_vec3_x(aabb_min), mny = rt_vec3_y(aabb_min), mnz = rt_vec3_z(aabb_min);
    double mxx = rt_vec3_x(aabb_max), mxy = rt_vec3_y(aabb_max), mxz = rt_vec3_z(aabb_max);

    double tmin = -1e30, tmax = 1e30;

    double inv[3];
    double o[3] = {ox, oy, oz};
    double d[3] = {dx, dy, dz};
    double mn[3] = {mnx, mny, mnz};
    double mx[3] = {mxx, mxy, mxz};

    for (int i = 0; i < 3; i++) {
        if (fabs(d[i]) < EPSILON) {
            if (o[i] < mn[i] || o[i] > mx[i])
                return -1.0;
        } else {
            inv[i] = 1.0 / d[i];
            double t1 = (mn[i] - o[i]) * inv[i];
            double t2 = (mx[i] - o[i]) * inv[i];
            if (t1 > t2) {
                double tmp = t1;
                t1 = t2;
                t2 = tmp;
            }
            if (t1 > tmin)
                tmin = t1;
            if (t2 < tmax)
                tmax = t2;
            if (tmin > tmax)
                return -1.0;
        }
    }

    return tmin >= 0.0 ? tmin : (tmax >= 0.0 ? 0.0 : -1.0);
}

/*==========================================================================
 * Ray-sphere intersection (quadratic formula)
 *=========================================================================*/

/// @brief Test ray–sphere intersection using the quadratic formula.
/// @details Solves the quadratic |O + tD - C|² = r² for the smallest positive t.
///          Returns -1.0 on miss. The ray origin may be inside the sphere (returns 0 distance).
/// @param origin Vec3 ray origin.
/// @param dir    Vec3 ray direction.
/// @param center Vec3 sphere center.
/// @param radius Sphere radius.
/// @return Distance t to nearest hit, or -1.0 on miss.
double rt_ray3d_intersect_sphere(void *origin, void *dir, void *center, double radius) {
    if (!origin || !dir || !center)
        return -1.0;

    double ox = rt_vec3_x(origin), oy = rt_vec3_y(origin), oz = rt_vec3_z(origin);
    double dx = rt_vec3_x(dir), dy = rt_vec3_y(dir), dz = rt_vec3_z(dir);
    double cx = rt_vec3_x(center), cy = rt_vec3_y(center), cz = rt_vec3_z(center);

    double lx = ox - cx, ly = oy - cy, lz = oz - cz;
    double a = dx * dx + dy * dy + dz * dz;
    double b = 2.0 * (lx * dx + ly * dy + lz * dz);
    double c = lx * lx + ly * ly + lz * lz - radius * radius;

    double disc = b * b - 4.0 * a * c;
    if (disc < 0.0)
        return -1.0;

    double sqrt_disc = sqrt(disc);
    double t0 = (-b - sqrt_disc) / (2.0 * a);
    double t1 = (-b + sqrt_disc) / (2.0 * a);

    if (t0 >= 0.0)
        return t0;
    if (t1 >= 0.0)
        return t1;
    return -1.0;
}

/*==========================================================================
 * Ray-mesh intersection (iterate triangles, AABB early-out)
 *=========================================================================*/

/// @brief Test ray–mesh intersection by iterating all triangles.
/// @details Performs an AABB early-out test, then iterates each triangle in the
///          mesh using the Möller–Trumbore algorithm. If a transform is provided,
///          vertices are transformed to world space per-triangle (avoids computing
///          the inverse model matrix). Returns a RayHit3D with distance, position,
///          normal, and triangle index on hit, or NULL on miss.
/// @param origin        Vec3 ray origin in world space.
/// @param dir           Vec3 ray direction (need not be normalized).
/// @param mesh_obj      Mesh handle to test against.
/// @param transform_obj Optional Mat4 model transform (NULL = identity).
/// @return Opaque RayHit3D handle, or NULL on miss.
void *rt_ray3d_intersect_mesh(void *origin, void *dir, void *mesh_obj, void *transform_obj) {
    if (!origin || !dir || !mesh_obj)
        return NULL;

    rt_mesh3d *m = (rt_mesh3d *)mesh_obj;
    if (m->vertex_count == 0 || m->index_count < 3)
        return NULL;

    double ox = rt_vec3_x(origin), oy = rt_vec3_y(origin), oz = rt_vec3_z(origin);
    double dx = rt_vec3_x(dir), dy = rt_vec3_y(dir), dz = rt_vec3_z(dir);

    /* If a transform is provided, vertices are transformed to world space
     * in the loop below (simpler than computing inverse model matrix). */
    int has_transform = (transform_obj != NULL);

    double best_t = 1e30;
    int64_t best_tri = -1;
    double best_normal[3] = {0, 1, 0};

    for (uint32_t i = 0; i + 2 < m->index_count; i += 3) {
        uint32_t i0 = m->indices[i], i1 = m->indices[i + 1], i2 = m->indices[i + 2];
        if (i0 >= m->vertex_count || i1 >= m->vertex_count || i2 >= m->vertex_count)
            continue;

        double ax = m->vertices[i0].pos[0], ay = m->vertices[i0].pos[1],
               az = m->vertices[i0].pos[2];
        double bx = m->vertices[i1].pos[0], by = m->vertices[i1].pos[1],
               bz = m->vertices[i1].pos[2];
        double cx = m->vertices[i2].pos[0], cy = m->vertices[i2].pos[1],
               cz = m->vertices[i2].pos[2];

        /* If there's a transform, apply it to vertex positions */
        if (has_transform) {
            typedef struct {
                double m[16];
            } mat4_view;

            mat4_view *mv = (mat4_view *)transform_obj;
            const double *M = mv->m;
            double tax = M[0] * ax + M[1] * ay + M[2] * az + M[3];
            double tay = M[4] * ax + M[5] * ay + M[6] * az + M[7];
            double taz = M[8] * ax + M[9] * ay + M[10] * az + M[11];
            ax = tax;
            ay = tay;
            az = taz;
            double tbx = M[0] * bx + M[1] * by + M[2] * bz + M[3];
            double tby = M[4] * bx + M[5] * by + M[6] * bz + M[7];
            double tbz = M[8] * bx + M[9] * by + M[10] * bz + M[11];
            bx = tbx;
            by = tby;
            bz = tbz;
            double tcx = M[0] * cx + M[1] * cy + M[2] * cz + M[3];
            double tcy = M[4] * cx + M[5] * cy + M[6] * cz + M[7];
            double tcz = M[8] * cx + M[9] * cy + M[10] * cz + M[11];
            cx = tcx;
            cy = tcy;
            cz = tcz;
        }

        /* Möller–Trumbore inline */
        double e1x = bx - ax, e1y = by - ay, e1z = bz - az;
        double e2x = cx - ax, e2y = cy - ay, e2z = cz - az;
        double px = dy * e2z - dz * e2y;
        double py = dz * e2x - dx * e2z;
        double pz = dx * e2y - dy * e2x;
        double det = e1x * px + e1y * py + e1z * pz;
        if (fabs(det) < EPSILON)
            continue;
        double inv_det = 1.0 / det;
        double tvx = ox - ax, tvy = oy - ay, tvz = oz - az;
        double u = (tvx * px + tvy * py + tvz * pz) * inv_det;
        if (u < 0.0 || u > 1.0)
            continue;
        double qx = tvy * e1z - tvz * e1y;
        double qy = tvz * e1x - tvx * e1z;
        double qz = tvx * e1y - tvy * e1x;
        double v = (dx * qx + dy * qy + dz * qz) * inv_det;
        if (v < 0.0 || u + v > 1.0)
            continue;
        double t = (e2x * qx + e2y * qy + e2z * qz) * inv_det;

        if (t >= 0.0 && t < best_t) {
            best_t = t;
            best_tri = (int64_t)(i / 3);
            /* Face normal from cross product */
            best_normal[0] = e1y * e2z - e1z * e2y;
            best_normal[1] = e1z * e2x - e1x * e2z;
            best_normal[2] = e1x * e2y - e1y * e2x;
            double nlen = sqrt(best_normal[0] * best_normal[0] + best_normal[1] * best_normal[1] +
                               best_normal[2] * best_normal[2]);
            if (nlen > 1e-8) {
                best_normal[0] /= nlen;
                best_normal[1] /= nlen;
                best_normal[2] /= nlen;
            }
        }
    }

    if (best_tri < 0)
        return NULL;

    rt_rayhit3d *hit = (rt_rayhit3d *)rt_obj_new_i64(0, (int64_t)sizeof(rt_rayhit3d));
    if (!hit)
        return NULL;
    hit->distance = best_t;
    hit->point[0] = ox + dx * best_t;
    hit->point[1] = oy + dy * best_t;
    hit->point[2] = oz + dz * best_t;
    hit->normal[0] = best_normal[0];
    hit->normal[1] = best_normal[1];
    hit->normal[2] = best_normal[2];
    hit->triangle_index = best_tri;
    return hit;
}

/*==========================================================================
 * AABB-AABB collision
 *=========================================================================*/

/// @brief Test whether two axis-aligned bounding boxes overlap.
/// @return 1 if they overlap on all three axes, 0 otherwise.
int8_t rt_aabb3d_overlaps(void *min_a, void *max_a, void *min_b, void *max_b) {
    if (!min_a || !max_a || !min_b || !max_b)
        return 0;

    double a0 = rt_vec3_x(min_a), a1 = rt_vec3_y(min_a), a2 = rt_vec3_z(min_a);
    double a3 = rt_vec3_x(max_a), a4 = rt_vec3_y(max_a), a5 = rt_vec3_z(max_a);
    double b0 = rt_vec3_x(min_b), b1 = rt_vec3_y(min_b), b2 = rt_vec3_z(min_b);
    double b3 = rt_vec3_x(max_b), b4 = rt_vec3_y(max_b), b5 = rt_vec3_z(max_b);

    return (a0 <= b3 && a3 >= b0 && a1 <= b4 && a4 >= b1 && a2 <= b5 && a5 >= b2) ? 1 : 0;
}

/// @brief Compute the minimum-axis penetration vector to separate two overlapping AABBs.
/// @details Finds the axis with the smallest overlap and returns a push vector
///          along that axis. Returns (0,0,0) if the boxes do not overlap.
void *rt_aabb3d_penetration(void *min_a, void *max_a, void *min_b, void *max_b) {
    if (!min_a || !max_a || !min_b || !max_b)
        return rt_vec3_new(0, 0, 0);

    double a0 = rt_vec3_x(min_a), a1 = rt_vec3_y(min_a), a2 = rt_vec3_z(min_a);
    double a3 = rt_vec3_x(max_a), a4 = rt_vec3_y(max_a), a5 = rt_vec3_z(max_a);
    double b0 = rt_vec3_x(min_b), b1 = rt_vec3_y(min_b), b2 = rt_vec3_z(min_b);
    double b3 = rt_vec3_x(max_b), b4 = rt_vec3_y(max_b), b5 = rt_vec3_z(max_b);

    /* No overlap → zero penetration */
    if (a0 > b3 || a3 < b0 || a1 > b4 || a4 < b1 || a2 > b5 || a5 < b2)
        return rt_vec3_new(0, 0, 0);

    /* Compute overlap on each axis */
    double ox = (a3 < b3 ? a3 - b0 : b3 - a0);
    double oy = (a4 < b4 ? a4 - b1 : b4 - a1);
    double oz = (a5 < b5 ? a5 - b2 : b5 - a2);

    /* Push out on the axis of minimum overlap */
    double ax = fabs(ox), ay = fabs(oy), az = fabs(oz);
    double ca = (a0 + a3) * 0.5, cb;

    if (ax <= ay && ax <= az) {
        cb = (b0 + b3) * 0.5;
        return rt_vec3_new(ca < cb ? -ox : ox, 0, 0);
    } else if (ay <= az) {
        cb = (b1 + b4) * 0.5;
        return rt_vec3_new(0, ca < cb ? -oy : oy, 0);
    } else {
        cb = (b2 + b5) * 0.5;
        return rt_vec3_new(0, 0, ca < cb ? -oz : oz);
    }
}

/*==========================================================================
 * RayHit3D accessors
 *=========================================================================*/

/// @brief Get the distance along the ray to the hit point.
double rt_ray3d_hit_distance(void *hit) {
    return hit ? ((rt_rayhit3d *)hit)->distance : -1.0;
}

/// @brief Get the world-space position of the hit point as a new Vec3.
void *rt_ray3d_hit_point(void *hit) {
    if (!hit)
        return rt_vec3_new(0, 0, 0);
    rt_rayhit3d *h = (rt_rayhit3d *)hit;
    return rt_vec3_new(h->point[0], h->point[1], h->point[2]);
}

/// @brief Get the surface normal at the hit point as a new Vec3.
void *rt_ray3d_hit_normal(void *hit) {
    if (!hit)
        return rt_vec3_new(0, 1, 0);
    rt_rayhit3d *h = (rt_rayhit3d *)hit;
    return rt_vec3_new(h->normal[0], h->normal[1], h->normal[2]);
}

/// @brief Get the index of the triangle that was hit (-1 if no hit).
int64_t rt_ray3d_hit_triangle(void *hit) {
    return hit ? ((rt_rayhit3d *)hit)->triangle_index : -1;
}

/*==========================================================================
 * Shape-shape collision primitives (for Physics3D)
 *=========================================================================*/

/// @brief Test whether two spheres overlap (distance < sum of radii).
int8_t rt_sphere3d_overlaps(void *center_a, double radius_a, void *center_b, double radius_b) {
    if (!center_a || !center_b)
        return 0;
    double dx = rt_vec3_x(center_b) - rt_vec3_x(center_a);
    double dy = rt_vec3_y(center_b) - rt_vec3_y(center_a);
    double dz = rt_vec3_z(center_b) - rt_vec3_z(center_a);
    double dist_sq = dx * dx + dy * dy + dz * dz;
    double r_sum = radius_a + radius_b;
    return dist_sq < r_sum * r_sum ? 1 : 0;
}

/// @brief Compute the penetration vector to separate two overlapping spheres.
void *rt_sphere3d_penetration(void *center_a, double radius_a, void *center_b, double radius_b) {
    if (!center_a || !center_b)
        return rt_vec3_new(0, 0, 0);
    double dx = rt_vec3_x(center_b) - rt_vec3_x(center_a);
    double dy = rt_vec3_y(center_b) - rt_vec3_y(center_a);
    double dz = rt_vec3_z(center_b) - rt_vec3_z(center_a);
    double dist = sqrt(dx * dx + dy * dy + dz * dz);
    double r_sum = radius_a + radius_b;
    if (dist >= r_sum || dist < 1e-12)
        return rt_vec3_new(0, 0, 0);
    double depth = r_sum - dist;
    double inv_dist = 1.0 / dist;
    return rt_vec3_new(dx * inv_dist * depth, dy * inv_dist * depth, dz * inv_dist * depth);
}

/// @brief Find the closest point on an AABB surface to a given point.
void *rt_aabb3d_closest_point(void *aabb_min, void *aabb_max, void *point) {
    if (!aabb_min || !aabb_max || !point)
        return rt_vec3_new(0, 0, 0);
    double px = rt_vec3_x(point), py = rt_vec3_y(point), pz = rt_vec3_z(point);
    double mnx = rt_vec3_x(aabb_min), mny = rt_vec3_y(aabb_min), mnz = rt_vec3_z(aabb_min);
    double mxx = rt_vec3_x(aabb_max), mxy = rt_vec3_y(aabb_max), mxz = rt_vec3_z(aabb_max);
    double cx = px < mnx ? mnx : (px > mxx ? mxx : px);
    double cy = py < mny ? mny : (py > mxy ? mxy : py);
    double cz = pz < mnz ? mnz : (pz > mxz ? mxz : pz);
    return rt_vec3_new(cx, cy, cz);
}

/// @brief Test whether an AABB and a sphere overlap.
int8_t rt_aabb3d_sphere_overlaps(void *aabb_min, void *aabb_max, void *center, double radius) {
    if (!aabb_min || !aabb_max || !center)
        return 0;
    void *closest = rt_aabb3d_closest_point(aabb_min, aabb_max, center);
    double dx = rt_vec3_x(center) - rt_vec3_x(closest);
    double dy = rt_vec3_y(center) - rt_vec3_y(closest);
    double dz = rt_vec3_z(center) - rt_vec3_z(closest);
    return (dx * dx + dy * dy + dz * dz) < radius * radius ? 1 : 0;
}

void *rt_segment3d_closest_point(void *seg_a, void *seg_b, void *point) {
    if (!seg_a || !seg_b || !point)
        return rt_vec3_new(0, 0, 0);
    double ax = rt_vec3_x(seg_a), ay = rt_vec3_y(seg_a), az = rt_vec3_z(seg_a);
    double bx = rt_vec3_x(seg_b), by = rt_vec3_y(seg_b), bz = rt_vec3_z(seg_b);
    double px = rt_vec3_x(point), py = rt_vec3_y(point), pz = rt_vec3_z(point);
    double dx = bx - ax, dy = by - ay, dz = bz - az;
    double len_sq = dx * dx + dy * dy + dz * dz;
    if (len_sq < 1e-12)
        return rt_vec3_new(ax, ay, az);
    double t = ((px - ax) * dx + (py - ay) * dy + (pz - az) * dz) / len_sq;
    if (t < 0.0)
        t = 0.0;
    if (t > 1.0)
        t = 1.0;
    return rt_vec3_new(ax + t * dx, ay + t * dy, az + t * dz);
}

int8_t rt_capsule3d_sphere_overlaps(
    void *cap_a, void *cap_b, double cap_radius, void *sphere_center, double sphere_radius) {
    if (!cap_a || !cap_b || !sphere_center)
        return 0;
    void *closest = rt_segment3d_closest_point(cap_a, cap_b, sphere_center);
    double dx = rt_vec3_x(sphere_center) - rt_vec3_x(closest);
    double dy = rt_vec3_y(sphere_center) - rt_vec3_y(closest);
    double dz = rt_vec3_z(sphere_center) - rt_vec3_z(closest);
    double r_sum = cap_radius + sphere_radius;
    return (dx * dx + dy * dy + dz * dz) < r_sum * r_sum ? 1 : 0;
}

int8_t rt_capsule3d_aabb_overlaps(
    void *cap_a, void *cap_b, double radius, void *aabb_min, void *aabb_max) {
    if (!cap_a || !cap_b || !aabb_min || !aabb_max)
        return 0;
    /* Test: closest point on capsule segment to closest point on AABB */
    /* Approximate: find closest point on segment to AABB center, then
     * find closest point on AABB to that segment point, check distance */
    double aabb_cx = (rt_vec3_x(aabb_min) + rt_vec3_x(aabb_max)) * 0.5;
    double aabb_cy = (rt_vec3_y(aabb_min) + rt_vec3_y(aabb_max)) * 0.5;
    double aabb_cz = (rt_vec3_z(aabb_min) + rt_vec3_z(aabb_max)) * 0.5;
    void *aabb_center = rt_vec3_new(aabb_cx, aabb_cy, aabb_cz);

    /* Closest point on segment to AABB center */
    void *seg_pt = rt_segment3d_closest_point(cap_a, cap_b, aabb_center);

    /* Closest point on AABB to that segment point */
    void *aabb_pt = rt_aabb3d_closest_point(aabb_min, aabb_max, seg_pt);

    double dx = rt_vec3_x(seg_pt) - rt_vec3_x(aabb_pt);
    double dy = rt_vec3_y(seg_pt) - rt_vec3_y(aabb_pt);
    double dz = rt_vec3_z(seg_pt) - rt_vec3_z(aabb_pt);
    return (dx * dx + dy * dy + dz * dz) < radius * radius ? 1 : 0;
}

#endif /* VIPER_ENABLE_GRAPHICS */
