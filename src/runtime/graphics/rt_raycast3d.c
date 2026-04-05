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

#define EPSILON 1e-8

static double clampd(double v, double lo, double hi) {
    if (v < lo)
        return lo;
    if (v > hi)
        return hi;
    return v;
}

static void aabb3d_clamp_point_raw(const double *mn,
                                   const double *mx,
                                   const double *point,
                                   double *closest) {
    closest[0] = clampd(point[0], mn[0], mx[0]);
    closest[1] = clampd(point[1], mn[1], mx[1]);
    closest[2] = clampd(point[2], mn[2], mx[2]);
}

static double rt_ray3d_intersect_aabb_raw(const double *origin,
                                          const double *dir,
                                          const double *mn,
                                          const double *mx) {
    double tmin = -1e30, tmax = 1e30;
    for (int i = 0; i < 3; i++) {
        if (fabs(dir[i]) < EPSILON) {
            if (origin[i] < mn[i] || origin[i] > mx[i])
                return -1.0;
            continue;
        }
        {
            double inv = 1.0 / dir[i];
            double t0 = (mn[i] - origin[i]) * inv;
            double t1 = (mx[i] - origin[i]) * inv;
            if (t0 > t1) {
                double tmp = t0;
                t0 = t1;
                t1 = tmp;
            }
            if (t0 > tmin)
                tmin = t0;
            if (t1 < tmax)
                tmax = t1;
            if (tmin > tmax)
                return -1.0;
        }
    }
    return tmin >= 0.0 ? tmin : (tmax >= 0.0 ? 0.0 : -1.0);
}

static void mat4_transform_point_raw(const double *m, const double *point, double *out) {
    out[0] = m[0] * point[0] + m[1] * point[1] + m[2] * point[2] + m[3];
    out[1] = m[4] * point[0] + m[5] * point[1] + m[6] * point[2] + m[7];
    out[2] = m[8] * point[0] + m[9] * point[1] + m[10] * point[2] + m[11];
}

static int mat4d_invert(const double *m, double *out) {
    double inv[16];
    inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] +
             m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
    inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] -
             m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
    inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] +
             m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
    inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] -
              m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
    inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] -
             m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
    inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] +
             m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
    inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] -
             m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
    inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] +
              m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
    inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] +
             m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
    inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] -
             m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
    inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] +
              m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
    inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] -
              m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
    inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] -
             m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
    inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] +
             m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
    inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] -
              m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
    inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] +
              m[8] * m[1] * m[6] - m[8] * m[2] * m[5];

    {
        double det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
        if (fabs(det) < 1e-12)
            return -1;
        det = 1.0 / det;
        for (int i = 0; i < 16; i++)
            out[i] = inv[i] * det;
    }
    return 0;
}

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
    {
        double o[3] = {rt_vec3_x(origin), rt_vec3_y(origin), rt_vec3_z(origin)};
        double d[3] = {rt_vec3_x(dir), rt_vec3_y(dir), rt_vec3_z(dir)};
        double mn[3] = {rt_vec3_x(aabb_min), rt_vec3_y(aabb_min), rt_vec3_z(aabb_min)};
        double mx[3] = {rt_vec3_x(aabb_max), rt_vec3_y(aabb_max), rt_vec3_z(aabb_max)};
        return rt_ray3d_intersect_aabb_raw(o, d, mn, mx);
    }
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
    {
        double world_origin[3] = {rt_vec3_x(origin), rt_vec3_y(origin), rt_vec3_z(origin)};
        double world_dir[3] = {rt_vec3_x(dir), rt_vec3_y(dir), rt_vec3_z(dir)};
        double obj_origin[3];
        double obj_dir[3];
        double inv_model[16];
        int has_transform = (transform_obj != NULL);
        int use_object_space = 0;
        double best_t = 1e30;
        int64_t best_tri = -1;
        uint32_t best_i0 = 0, best_i1 = 0, best_i2 = 0;
        double best_obj_point[3] = {0, 0, 0};

        rt_mesh3d_refresh_bounds(m);

        if (has_transform) {
            const double *model = ((mat4_impl *)transform_obj)->m;
            if (mat4d_invert(model, inv_model) == 0) {
                double world_target[3] = {world_origin[0] + world_dir[0],
                                          world_origin[1] + world_dir[1],
                                          world_origin[2] + world_dir[2]};
                double obj_target[3];
                mat4_transform_point_raw(inv_model, world_origin, obj_origin);
                mat4_transform_point_raw(inv_model, world_target, obj_target);
                obj_dir[0] = obj_target[0] - obj_origin[0];
                obj_dir[1] = obj_target[1] - obj_origin[1];
                obj_dir[2] = obj_target[2] - obj_origin[2];
                use_object_space = 1;
            }
        }

        if (!use_object_space) {
            obj_origin[0] = world_origin[0];
            obj_origin[1] = world_origin[1];
            obj_origin[2] = world_origin[2];
            obj_dir[0] = world_dir[0];
            obj_dir[1] = world_dir[1];
            obj_dir[2] = world_dir[2];
        }

        {
            double bounds_min[3] = {m->aabb_min[0], m->aabb_min[1], m->aabb_min[2]};
            double bounds_max[3] = {m->aabb_max[0], m->aabb_max[1], m->aabb_max[2]};
            if (has_transform && !use_object_space) {
                const double *model = ((mat4_impl *)transform_obj)->m;
                double corners[8][3];
                double world_min[3] = {DBL_MAX, DBL_MAX, DBL_MAX};
                double world_max[3] = {-DBL_MAX, -DBL_MAX, -DBL_MAX};
                corners[0][0] = bounds_min[0];
                corners[0][1] = bounds_min[1];
                corners[0][2] = bounds_min[2];
                corners[1][0] = bounds_max[0];
                corners[1][1] = bounds_min[1];
                corners[1][2] = bounds_min[2];
                corners[2][0] = bounds_min[0];
                corners[2][1] = bounds_max[1];
                corners[2][2] = bounds_min[2];
                corners[3][0] = bounds_max[0];
                corners[3][1] = bounds_max[1];
                corners[3][2] = bounds_min[2];
                corners[4][0] = bounds_min[0];
                corners[4][1] = bounds_min[1];
                corners[4][2] = bounds_max[2];
                corners[5][0] = bounds_max[0];
                corners[5][1] = bounds_min[1];
                corners[5][2] = bounds_max[2];
                corners[6][0] = bounds_min[0];
                corners[6][1] = bounds_max[1];
                corners[6][2] = bounds_max[2];
                corners[7][0] = bounds_max[0];
                corners[7][1] = bounds_max[1];
                corners[7][2] = bounds_max[2];
                for (int i = 0; i < 8; i++) {
                    double p[3];
                    mat4_transform_point_raw(model, corners[i], p);
                    for (int axis = 0; axis < 3; axis++) {
                        if (p[axis] < world_min[axis])
                            world_min[axis] = p[axis];
                        if (p[axis] > world_max[axis])
                            world_max[axis] = p[axis];
                    }
                }
                if (rt_ray3d_intersect_aabb_raw(world_origin, world_dir, world_min, world_max) <
                    0.0)
                    return NULL;
            } else if (rt_ray3d_intersect_aabb_raw(obj_origin, obj_dir, bounds_min, bounds_max) <
                       0.0) {
                return NULL;
            }
        }

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

            if (has_transform && !use_object_space) {
                const double *model = ((mat4_impl *)transform_obj)->m;
                double a[3] = {ax, ay, az}, b[3] = {bx, by, bz}, c[3] = {cx, cy, cz};
                mat4_transform_point_raw(model, a, a);
                mat4_transform_point_raw(model, b, b);
                mat4_transform_point_raw(model, c, c);
                ax = a[0];
                ay = a[1];
                az = a[2];
                bx = b[0];
                by = b[1];
                bz = b[2];
                cx = c[0];
                cy = c[1];
                cz = c[2];
            }

            {
                double e1x = bx - ax, e1y = by - ay, e1z = bz - az;
                double e2x = cx - ax, e2y = cy - ay, e2z = cz - az;
                double px = obj_dir[1] * e2z - obj_dir[2] * e2y;
                double py = obj_dir[2] * e2x - obj_dir[0] * e2z;
                double pz = obj_dir[0] * e2y - obj_dir[1] * e2x;
                double det = e1x * px + e1y * py + e1z * pz;
                if (fabs(det) < EPSILON)
                    continue;
                {
                    double inv_det = 1.0 / det;
                    double tvx = obj_origin[0] - ax, tvy = obj_origin[1] - ay,
                           tvz = obj_origin[2] - az;
                    double u = (tvx * px + tvy * py + tvz * pz) * inv_det;
                    if (u < 0.0 || u > 1.0)
                        continue;
                    {
                        double qx = tvy * e1z - tvz * e1y;
                        double qy = tvz * e1x - tvx * e1z;
                        double qz = tvx * e1y - tvy * e1x;
                        double v = (obj_dir[0] * qx + obj_dir[1] * qy + obj_dir[2] * qz) * inv_det;
                        if (v < 0.0 || u + v > 1.0)
                            continue;
                        {
                            double t = (e2x * qx + e2y * qy + e2z * qz) * inv_det;
                            if (t >= 0.0 && t < best_t) {
                                best_t = t;
                                best_tri = (int64_t)(i / 3);
                                best_i0 = i0;
                                best_i1 = i1;
                                best_i2 = i2;
                                best_obj_point[0] = obj_origin[0] + obj_dir[0] * t;
                                best_obj_point[1] = obj_origin[1] + obj_dir[1] * t;
                                best_obj_point[2] = obj_origin[2] + obj_dir[2] * t;
                            }
                        }
                    }
                }
            }
        }

        if (best_tri < 0)
            return NULL;

        {
            rt_rayhit3d *hit = (rt_rayhit3d *)rt_obj_new_i64(0, (int64_t)sizeof(rt_rayhit3d));
            double best_normal[3];
            if (!hit)
                return NULL;

            {
                double a[3] = {m->vertices[best_i0].pos[0],
                               m->vertices[best_i0].pos[1],
                               m->vertices[best_i0].pos[2]};
                double b[3] = {m->vertices[best_i1].pos[0],
                               m->vertices[best_i1].pos[1],
                               m->vertices[best_i1].pos[2]};
                double c[3] = {m->vertices[best_i2].pos[0],
                               m->vertices[best_i2].pos[1],
                               m->vertices[best_i2].pos[2]};
                if (has_transform) {
                    const double *model = ((mat4_impl *)transform_obj)->m;
                    mat4_transform_point_raw(model, a, a);
                    mat4_transform_point_raw(model, b, b);
                    mat4_transform_point_raw(model, c, c);
                    if (use_object_space)
                        mat4_transform_point_raw(model, best_obj_point, hit->point);
                    else {
                        hit->point[0] = world_origin[0] + world_dir[0] * best_t;
                        hit->point[1] = world_origin[1] + world_dir[1] * best_t;
                        hit->point[2] = world_origin[2] + world_dir[2] * best_t;
                    }
                } else {
                    hit->point[0] = world_origin[0] + world_dir[0] * best_t;
                    hit->point[1] = world_origin[1] + world_dir[1] * best_t;
                    hit->point[2] = world_origin[2] + world_dir[2] * best_t;
                }

                {
                    double e1x = b[0] - a[0], e1y = b[1] - a[1], e1z = b[2] - a[2];
                    double e2x = c[0] - a[0], e2y = c[1] - a[1], e2z = c[2] - a[2];
                    best_normal[0] = e1y * e2z - e1z * e2y;
                    best_normal[1] = e1z * e2x - e1x * e2z;
                    best_normal[2] = e1x * e2y - e1y * e2x;
                }
            }

            {
                double nlen =
                    sqrt(best_normal[0] * best_normal[0] + best_normal[1] * best_normal[1] +
                         best_normal[2] * best_normal[2]);
                if (nlen > 1e-8) {
                    best_normal[0] /= nlen;
                    best_normal[1] /= nlen;
                    best_normal[2] /= nlen;
                } else {
                    best_normal[0] = 0.0;
                    best_normal[1] = 1.0;
                    best_normal[2] = 0.0;
                }
            }

            {
                double dir_len_sq = world_dir[0] * world_dir[0] + world_dir[1] * world_dir[1] +
                                    world_dir[2] * world_dir[2];
                hit->distance = dir_len_sq > 1e-12
                                    ? (((hit->point[0] - world_origin[0]) * world_dir[0] +
                                        (hit->point[1] - world_origin[1]) * world_dir[1] +
                                        (hit->point[2] - world_origin[2]) * world_dir[2]) /
                                       dir_len_sq)
                                    : 0.0;
            }
            hit->normal[0] = best_normal[0];
            hit->normal[1] = best_normal[1];
            hit->normal[2] = best_normal[2];
            hit->triangle_index = best_tri;
            return hit;
        }
    }
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
    {
        double p[3] = {rt_vec3_x(point), rt_vec3_y(point), rt_vec3_z(point)};
        double mn[3] = {rt_vec3_x(aabb_min), rt_vec3_y(aabb_min), rt_vec3_z(aabb_min)};
        double mx[3] = {rt_vec3_x(aabb_max), rt_vec3_y(aabb_max), rt_vec3_z(aabb_max)};
        double c[3];
        aabb3d_clamp_point_raw(mn, mx, p, c);
        if (p[0] >= mn[0] && p[0] <= mx[0] && p[1] >= mn[1] && p[1] <= mx[1] && p[2] >= mn[2] &&
            p[2] <= mx[2]) {
            double dx0 = fabs(p[0] - mn[0]), dx1 = fabs(mx[0] - p[0]);
            double dy0 = fabs(p[1] - mn[1]), dy1 = fabs(mx[1] - p[1]);
            double dz0 = fabs(p[2] - mn[2]), dz1 = fabs(mx[2] - p[2]);
            double best = dx0;
            c[0] = mn[0];
            c[1] = p[1];
            c[2] = p[2];
            if (dx1 < best) {
                best = dx1;
                c[0] = mx[0];
                c[1] = p[1];
                c[2] = p[2];
            }
            if (dy0 < best) {
                best = dy0;
                c[0] = p[0];
                c[1] = mn[1];
                c[2] = p[2];
            }
            if (dy1 < best) {
                best = dy1;
                c[0] = p[0];
                c[1] = mx[1];
                c[2] = p[2];
            }
            if (dz0 < best) {
                best = dz0;
                c[0] = p[0];
                c[1] = p[1];
                c[2] = mn[2];
            }
            if (dz1 < best) {
                c[0] = p[0];
                c[1] = p[1];
                c[2] = mx[2];
            }
        }
        return rt_vec3_new(c[0], c[1], c[2]);
    }
}

/// @brief Test whether an AABB and a sphere overlap.
int8_t rt_aabb3d_sphere_overlaps(void *aabb_min, void *aabb_max, void *center, double radius) {
    if (!aabb_min || !aabb_max || !center)
        return 0;
    {
        double p[3] = {rt_vec3_x(center), rt_vec3_y(center), rt_vec3_z(center)};
        double mn[3] = {rt_vec3_x(aabb_min), rt_vec3_y(aabb_min), rt_vec3_z(aabb_min)};
        double mx[3] = {rt_vec3_x(aabb_max), rt_vec3_y(aabb_max), rt_vec3_z(aabb_max)};
        double c[3];
        aabb3d_clamp_point_raw(mn, mx, p, c);
        {
            double dx = p[0] - c[0];
            double dy = p[1] - c[1];
            double dz = p[2] - c[2];
            return (dx * dx + dy * dy + dz * dz) < radius * radius ? 1 : 0;
        }
    }
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

    {
        double p[3] = {rt_vec3_x(seg_pt), rt_vec3_y(seg_pt), rt_vec3_z(seg_pt)};
        double mn[3] = {rt_vec3_x(aabb_min), rt_vec3_y(aabb_min), rt_vec3_z(aabb_min)};
        double mx[3] = {rt_vec3_x(aabb_max), rt_vec3_y(aabb_max), rt_vec3_z(aabb_max)};
        double c[3];
        aabb3d_clamp_point_raw(mn, mx, p, c);
        double dx = p[0] - c[0];
        double dy = p[1] - c[1];
        double dz = p[2] - c[2];
        return (dx * dx + dy * dy + dz * dz) < radius * radius ? 1 : 0;
    }
}

#else
typedef int rt_graphics_disabled_tu_guard;
#endif /* VIPER_ENABLE_GRAPHICS */
