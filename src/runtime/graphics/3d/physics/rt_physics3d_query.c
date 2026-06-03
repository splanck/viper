//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/physics/rt_physics3d_query.c
// Purpose: Spatial queries for the Physics3D runtime — raycasts (sphere/box/
//   capsule/triangle/mesh-BVH/heightfield/collider), shape sweeps, and overlap
//   tests, plus the per-query broad-phase cache and sorted hit lists.
//   Split out of rt_physics3d.c; shares core types via rt_physics3d_internal.h.
//
// Key invariants:
//   - Result lists are bounded by PH3D_MAX_QUERY_HITS and kept distance-sorted.
//   - The query broad phase is rebuilt lazily when its signature changes.
//   - Mesh raycasts traverse the shared rt_physics_mesh_bvh_node BVH, rebuilt
//     on demand via mesh_physics_bvh_rebuild (also used by narrow-phase).
//
// Ownership/Lifetime:
//   - Boxed PhysicsHit3D / PhysicsHitList3D results retain referenced bodies
//     and colliders until released by the GC.
//
// Links: rt_physics3d_internal.h, rt_physics3d.c, rt_raycast3d.h
//
//===----------------------------------------------------------------------===//

#ifdef VIPER_ENABLE_GRAPHICS

#include "rt_collider3d.h"
#include "rt_graphics3d_ids.h"
#include "rt_physics3d.h"
#include "rt_physics3d_internal.h"
#include "rt_raycast3d.h"
#include "rt_trap.h"

#include <float.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// Forward declarations for query-internal raw raycasts (defined below, but
// referenced earlier by the sweep helpers).
static int raycast_sphere_raw(const double *origin,
                              const double *dir,
                              const double *center,
                              double radius,
                              double max_distance,
                              double *out_t,
                              double *out_normal,
                              int *out_started);
static int raycast_box_pose_raw(void *box_collider,
                                const rt_collider_pose *pose,
                                const double *origin,
                                const double *dir,
                                double max_distance,
                                double expand_radius,
                                double *out_t,
                                double *out_normal,
                                int *out_started);

#define PH3D_QUERY_COORD_ABS_MAX 1000000000000.0
#define PH3D_QUERY_DISTANCE_MAX 1000000000.0

/// @brief Clamp query coordinates to the same broad finite range used by body state.
static double query_saturate_coord(double value) {
    if (!isfinite(value))
        return 0.0;
    if (value > PH3D_QUERY_COORD_ABS_MAX)
        return PH3D_QUERY_COORD_ABS_MAX;
    if (value < -PH3D_QUERY_COORD_ABS_MAX)
        return -PH3D_QUERY_COORD_ABS_MAX;
    return value;
}

/// @brief Read a public Vec3 argument, rejecting NaN/Inf and capping extreme finite values.
static int query_read_vec3(void *obj, double out[3]) {
    double raw[3];
    if (!out || !rt_g3d_is_vec3(obj))
        return 0;
    raw[0] = rt_vec3_x(obj);
    raw[1] = rt_vec3_y(obj);
    raw[2] = rt_vec3_z(obj);
    if (!ph3d_vec3_all_finite(raw))
        return 0;
    out[0] = query_saturate_coord(raw[0]);
    out[1] = query_saturate_coord(raw[1]);
    out[2] = query_saturate_coord(raw[2]);
    return 1;
}

/// @brief Clamp a finite public query distance/radius to a runtime-safe range.
static double query_sanitize_distance(double value) {
    if (!isfinite(value) || value <= 0.0)
        return 0.0;
    return value > PH3D_QUERY_DISTANCE_MAX ? PH3D_QUERY_DISTANCE_MAX : value;
}

/// @brief Normalize a direction vector in place, returning 0 for invalid/zero vectors.
static int query_normalize_direction(double dir[3]) {
    if (!ph3d_vec3_all_finite(dir))
        return 0;
    ph3d_vec3_sanitize_state(dir);
    return vec3_normalize_in_place(dir) > 1e-12;
}

/// @brief Clamp a vector's length to the query distance cap, preserving direction.
static double query_cap_vector_length(double v[3]) {
    double len;
    if (!ph3d_vec3_all_finite(v))
        return 0.0;
    ph3d_vec3_sanitize_state(v);
    len = vec3_len(v);
    if (!isfinite(len) || len <= 0.0)
        return 0.0;
    if (len > PH3D_QUERY_DISTANCE_MAX) {
        double scale = PH3D_QUERY_DISTANCE_MAX / len;
        v[0] *= scale;
        v[1] *= scale;
        v[2] *= scale;
        return PH3D_QUERY_DISTANCE_MAX;
    }
    return len;
}

/// @brief Normalize a hit normal with a fallback opposite the ray/sweep direction.
static void query_normalize_normal(double normal[3], const double *fallback_dir) {
    ph3d_vec3_sanitize_state(normal);
    if (vec3_normalize_in_place(normal) > 1e-12)
        return;
    if (fallback_dir && ph3d_vec3_all_finite(fallback_dir)) {
        vec3_negate(fallback_dir, normal);
        ph3d_vec3_sanitize_state(normal);
        if (vec3_normalize_in_place(normal) > 1e-12)
            return;
    }
    vec3_set(normal, 0.0, 1.0, 0.0);
}

/// @brief Validate and sanitize a raw query hit before it is boxed or sorted.
static int query_sanitize_hit(rt_query_hit3d *hit, double max_distance, const double *fallback_dir) {
    if (!hit || !isfinite(hit->distance) || hit->distance < 0.0)
        return 0;
    hit->distance = query_sanitize_distance(hit->distance);
    if (max_distance > 1e-12 && hit->distance > max_distance)
        hit->distance = max_distance;
    if (!isfinite(hit->fraction)) {
        hit->fraction = max_distance > 1e-12 ? hit->distance / max_distance : 0.0;
    }
    if (hit->fraction < 0.0)
        hit->fraction = 0.0;
    if (hit->fraction > 1.0)
        hit->fraction = 1.0;
    ph3d_vec3_sanitize_state(hit->point);
    query_normalize_normal(hit->normal, fallback_dir);
    hit->started_penetrating = hit->started_penetrating ? 1 : 0;
    hit->is_trigger = hit->is_trigger ? 1 : 0;
    return 1;
}

/// @brief Insert `hit` into a distance-sorted hit array, keeping the order.
///
/// O(n) insertion (linear shift). Acceptable because `RaycastAll` /
/// `OverlapAll` queries are bounded by `PH3D_MAX_QUERY_HITS` (256).
static int query_hit_insert_sorted(rt_query_hit3d *hits, int32_t count, const rt_query_hit3d *hit) {
    int32_t pos = count;
    rt_query_hit3d clean;
    if (!hits || !hit)
        return count;
    clean = *hit;
    if (!query_sanitize_hit(&clean, PH3D_QUERY_DISTANCE_MAX, NULL))
        return count;
    while (pos > 0 && hits[pos - 1].distance > clean.distance) {
        hits[pos] = hits[pos - 1];
        pos--;
    }
    hits[pos] = clean;
    return count + 1;
}

/// @brief Insert a query hit into a distance-sorted array capped at @p capacity (keeps the
/// nearest).
/// @details Below capacity it inserts in order; when full, a hit nearer than the current farthest
///          displaces it (bounded insertion sort), so the array always holds the K closest results.
/// @return The new hit count.
static int query_hit_insert_sorted_bounded(rt_query_hit3d *hits,
                                           int32_t count,
                                           int32_t capacity,
                                           const rt_query_hit3d *hit) {
    int32_t pos;
    rt_query_hit3d clean;
    if (!hits || !hit || capacity <= 0)
        return count;
    clean = *hit;
    if (!query_sanitize_hit(&clean, PH3D_QUERY_DISTANCE_MAX, NULL))
        return count;
    if (count < capacity)
        return query_hit_insert_sorted(hits, count, &clean);
    if (clean.distance >= hits[capacity - 1].distance)
        return count;
    pos = capacity - 1;
    while (pos > 0 && hits[pos - 1].distance > clean.distance) {
        hits[pos] = hits[pos - 1];
        pos--;
    }
    hits[pos] = clean;
    return capacity;
}

/// @brief Rebuild the world's broadphase scratch entries for one query.
/// @return Entry count on success, -1 on allocation failure.
static uint64_t world3d_query_broadphase_signature(rt_world3d *w) {
    uint64_t hash = UINT64_C(1469598103934665603);
    if (!w)
        return 0;
    hash ^= (uint64_t)(uint32_t)w->body_count;
    hash *= UINT64_C(1099511628211);
    for (int32_t i = 0; i < w->body_count; ++i) {
        rt_body3d *body = w->bodies[i];
        uint64_t collider_revision =
            body && body->collider ? rt_collider3d_get_bounds_revision_raw(body->collider) : 0;
        hash ^= (uint64_t)(uintptr_t)body;
        hash *= UINT64_C(1099511628211);
        hash ^= body ? body->broadphase_revision : 0;
        hash *= UINT64_C(1099511628211);
        hash ^= collider_revision;
        hash *= UINT64_C(1099511628211);
    }
    return hash ? hash : 1;
}

/// @brief Build (or reuse) the cached broadphase entry list used to accelerate spatial queries.
/// @details Collects each body's world AABB into entries sorted by min-X for sweep-and-prune, and
///          stamps an FNV-1a signature over the body set/revisions so an unchanged world reuses the
///          cache instead of rebuilding.
/// @return The number of broadphase entries.
static int32_t world3d_build_query_broadphase(rt_world3d *w) {
    int32_t entry_count = 0;
    uint64_t signature;
    if (!w)
        return -1;
    signature = world3d_query_broadphase_signature(w);
    if (w->query_broadphase_valid && w->query_broadphase_signature == signature)
        return w->query_broadphase_count;
    if (!world3d_reserve_broadphase_capacity(w, w->body_count))
        return -1;
    for (int32_t i = 0; i < w->body_count; ++i) {
        rt_body3d *body = w->bodies[i];
        if (!body3d_has_collision_geometry(body))
            continue;
        ph3d_broadphase_entry *entry = &w->broadphase_entries[entry_count++];
        entry->body = body;
        body_aabb(body, entry->min, entry->max);
    }
    qsort(w->broadphase_entries,
          (size_t)entry_count,
          sizeof(*w->broadphase_entries),
          ph3d_broadphase_compare_min_x);
    w->query_broadphase_count = entry_count;
    w->query_broadphase_signature = signature;
    w->query_broadphase_valid = 1;
    return entry_count;
}

/// @brief Whether a broadphase entry's AABB overlaps the query's AABB on all three axes.
static int query_entry_overlaps_bounds(const ph3d_broadphase_entry *entry,
                                       const double *query_min,
                                       const double *query_max) {
    return entry && query_min && query_max && entry->max[0] >= query_min[0] &&
           entry->min[0] <= query_max[0] && entry->max[1] >= query_min[1] &&
           entry->min[1] <= query_max[1] && entry->max[2] >= query_min[2] &&
           entry->min[2] <= query_max[2];
}

/// @brief Stack-init a transient body with a collider for use by query helpers.
///
/// Static motion mode (so impulse code never touches it), identity
/// orientation, and the supplied position. `make_temp_sphere` is the
/// sphere-only variant used elsewhere; this is the general form.
static void init_temp_query_body(rt_body3d *body, void *collider, const double *position) {
    memset(body, 0, sizeof(*body));
    quat_identity(body->orientation);
    vec3_set(body->scale, 1.0, 1.0, 1.0);
    body->motion_mode = PH3D_MODE_STATIC;
    body->collider = collider;
    if (position) {
        vec3_copy(body->position, position);
        ph3d_vec3_sanitize_state(body->position);
    }
}

/// @brief Test a transient query body for overlap against a registered body.
///
/// Used by overlap queries — fills `out_hit` with `started_penetrating=1`
/// since the query body starts already touching the other body.
static int overlap_query_body_against_body(rt_body3d *query_body,
                                           rt_body3d *other,
                                           rt_query_hit3d *out_hit) {
    double normal[3], depth, point[3];
    void *leaf_other = NULL;
    if (!query_body || !body3d_has_collision_geometry(other))
        return 0;
    if (!test_collision(query_body, other, normal, &depth, point, NULL, &leaf_other, NULL, NULL))
        return 0;
    if (out_hit) {
        memset(out_hit, 0, sizeof(*out_hit));
        out_hit->body = other;
        out_hit->collider = leaf_other ? leaf_other : other->collider;
        vec3_copy(out_hit->point, point);
        vec3_copy(out_hit->normal, normal);
        out_hit->distance = 0.0;
        out_hit->fraction = 0.0;
        out_hit->started_penetrating = 1;
        out_hit->is_trigger = other->is_trigger;
        query_sanitize_hit(out_hit, 0.0, NULL);
    }
    return 1;
}

/// @brief Populate a query-hit record (body, point, normal, distance) for a sphere-sweep result.
static void sweep_sphere_fill_hit(rt_body3d *body,
                                  const double *start_center,
                                  const double *dir,
                                  double radius,
                                  double distance,
                                  double max_distance,
                                  const double *normal,
                                  rt_query_hit3d *out_hit) {
    if (!out_hit)
        return;
    memset(out_hit, 0, sizeof(*out_hit));
    out_hit->body = body;
    out_hit->collider = body ? body->collider : NULL;
    out_hit->distance = query_sanitize_distance(distance);
    out_hit->fraction = max_distance > 1e-12 ? out_hit->distance / max_distance : 0.0;
    out_hit->started_penetrating = 0;
    out_hit->is_trigger = body ? body->is_trigger : 0;
    if (normal)
        vec3_copy(out_hit->normal, normal);
    else
        vec3_negate(dir, out_hit->normal);
    query_normalize_normal(out_hit->normal, dir);
    out_hit->point[0] =
        query_saturate_coord(start_center[0] + dir[0] * out_hit->distance - out_hit->normal[0] * radius);
    out_hit->point[1] =
        query_saturate_coord(start_center[1] + dir[1] * out_hit->distance - out_hit->normal[1] * radius);
    out_hit->point[2] =
        query_saturate_coord(start_center[2] + dir[2] * out_hit->distance - out_hit->normal[2] * radius);
    query_sanitize_hit(out_hit, max_distance, dir);
}

/// @brief Exact fast paths for common sphere sweeps before the generic sampler runs.
static int sweep_sphere_against_simple_body(const double *start_center,
                                            double radius,
                                            const double *delta,
                                            rt_body3d *other,
                                            double max_distance,
                                            rt_query_hit3d *out_hit) {
    double dir[3];
    double t = 0.0;
    double normal[3] = {0.0, 1.0, 0.0};
    int started = 0;
    if (!start_center || !delta || !body3d_has_collision_geometry(other) ||
        max_distance <= 1e-12)
        return 0;
    vec3_copy(dir, delta);
    if (!query_normalize_direction(dir))
        return 0;
    if (other->shape == PH3D_SHAPE_SPHERE) {
        double combined_radius = radius + other->radius;
        if (!raycast_sphere_raw(start_center,
                                dir,
                                other->position,
                                combined_radius,
                                max_distance,
                                &t,
                                normal,
                                &started))
            return 0;
        if (started)
            return 0;
        sweep_sphere_fill_hit(other, start_center, dir, radius, t, max_distance, normal, out_hit);
        return 1;
    }
    if (other->shape == PH3D_SHAPE_AABB &&
        rt_collider3d_get_type(other->collider) == RT_COLLIDER3D_TYPE_BOX) {
        rt_collider_pose pose;
        collider_pose_from_body(other, &pose);
        if (!raycast_box_pose_raw(other->collider,
                                  &pose,
                                  start_center,
                                  dir,
                                  max_distance,
                                  radius,
                                  &t,
                                  normal,
                                  &started))
            return 0;
        if (started)
            return 0;
        sweep_sphere_fill_hit(other, start_center, dir, radius, t, max_distance, normal, out_hit);
        return 1;
    }
    return 0;
}

/// @brief Sweep a sphere along `delta` and find first contact with `other`.
///
/// First checks initial overlap (early-out for already-penetrating).
/// Then broad-phases against the swept AABB. Then steps along the sweep
/// in radius/body-feature-relative increments looking for the first overlap,
/// and refines the impact `t` via bisection. There is deliberately no fixed
/// world-unit minimum step: tiny spheres must still detect thin geometry.
static int sweep_sphere_against_body(void *sphere_collider,
                                     const double *start_center,
                                     double radius,
                                     const double *delta,
                                     rt_body3d *other,
                                     double max_distance,
                                     rt_query_hit3d *out_hit) {
    rt_body3d query_body;
    rt_query_hit3d cur_hit;
    double query_min[3], query_max[3], swept_min[3], swept_max[3];
    double other_min[3], other_max[3];
    double delta_len;
    double step_dist;
    int steps;
    if (!sphere_collider || !start_center || !delta || !body3d_has_collision_geometry(other))
        return 0;

    init_temp_query_body(&query_body, sphere_collider, start_center);
    body3d_update_shape_cache_from_collider(&query_body);
    if (overlap_query_body_against_body(&query_body, other, out_hit))
        return 1;

    if (sweep_sphere_against_simple_body(start_center, radius, delta, other, max_distance, out_hit))
        return 1;

    body_aabb(&query_body, query_min, query_max);
    body_aabb(other, other_min, other_max);
    swept_aabb_from_points(query_min, query_max, delta, swept_min, swept_max);
    if (!aabb_overlap_raw(swept_min, swept_max, other_min, other_max))
        return 0;

    delta_len = vec3_len(delta);
    if (delta_len <= 1e-12 || max_distance <= 0.0)
        return 0;

    {
        double other_extent_x = other_max[0] - other_min[0];
        double other_extent_y = other_max[1] - other_min[1];
        double other_extent_z = other_max[2] - other_min[2];
        double body_extent = other_extent_x;
        if (other_extent_y > body_extent)
            body_extent = other_extent_y;
        if (other_extent_z > body_extent)
            body_extent = other_extent_z;
        step_dist = radius > 1e-6 ? radius * 0.25 : body_extent * 0.05;
        if (!isfinite(step_dist) || step_dist <= 0.0)
            step_dist = 1e-4;
        if (step_dist < 1e-5)
            step_dist = 1e-5;
        if (step_dist > 0.25)
            step_dist = 0.25;
    }

    {
        double step_count = ceil(delta_len / step_dist);
        if (!isfinite(step_count) || step_count > (double)PH3D_MAX_SWEEP_STEPS)
            steps = PH3D_MAX_SWEEP_STEPS;
        else if (step_count < 1.0)
            steps = 1;
        else
            steps = (int)step_count;
    }
    if (steps < 1)
        steps = 1;

    {
        double prev_t = 0.0;
        for (int s = 1; s <= steps; ++s) {
            double t = (double)s / (double)steps;
            double center[3] = {
                query_saturate_coord(start_center[0] + delta[0] * t),
                query_saturate_coord(start_center[1] + delta[1] * t),
                query_saturate_coord(start_center[2] + delta[2] * t),
            };
            init_temp_query_body(&query_body, sphere_collider, center);
            body3d_update_shape_cache_from_collider(&query_body);
            if (!overlap_query_body_against_body(&query_body, other, &cur_hit)) {
                prev_t = t;
                continue;
            }
            {
                double lo = prev_t;
                double hi = t;
                rt_query_hit3d best = cur_hit;
                for (int iter = 0; iter < 14; ++iter) {
                    double mid = (lo + hi) * 0.5;
                    double mid_center[3] = {
                        query_saturate_coord(start_center[0] + delta[0] * mid),
                        query_saturate_coord(start_center[1] + delta[1] * mid),
                        query_saturate_coord(start_center[2] + delta[2] * mid),
                    };
                    init_temp_query_body(&query_body, sphere_collider, mid_center);
                    body3d_update_shape_cache_from_collider(&query_body);
                    if (overlap_query_body_against_body(&query_body, other, &cur_hit)) {
                        hi = mid;
                        best = cur_hit;
                    } else {
                        lo = mid;
                    }
                }
                best.distance = hi * max_distance;
                best.fraction = hi;
                best.started_penetrating = 0;
                query_sanitize_hit(&best, max_distance, delta);
                if (out_hit)
                    *out_hit = best;
                return 1;
            }
        }
    }
    return 0;
}

/// @brief Sweep a capsule (axis from `a` to `b`, radius `radius`) along `delta`.
///
/// Approximates the capsule sweep as adaptive sphere-sweeps sampled
/// along the axis. Picks the closest hit.
static int sweep_capsule_against_body(const double *a,
                                      const double *b,
                                      double radius,
                                      const double *delta,
                                      rt_body3d *other,
                                      double max_distance,
                                      rt_query_hit3d *out_hit) {
    double axis[3];
    double axis_len;
    int samples;
    int hit = 0;
    rt_query_hit3d best = {0};
    void *sphere_collider;
    if (!a || !b || !delta || !body3d_has_collision_geometry(other))
        return 0;
    sphere_collider = rt_collider3d_new_sphere(radius);
    if (!sphere_collider)
        return 0;
    vec3_sub(b, a, axis);
    axis_len = vec3_len(axis);
    if (!isfinite(axis_len))
        axis_len = 0.0;
    samples = capsule_axis_sample_count(axis_len, radius);
    for (int i = 0; i < samples; ++i) {
        double t = samples == 1 ? 0.5 : (double)i / (double)(samples - 1);
        double center[3] = {
            query_saturate_coord(a[0] + axis[0] * t),
            query_saturate_coord(a[1] + axis[1] * t),
            query_saturate_coord(a[2] + axis[2] * t),
        };
        rt_query_hit3d cur_hit;
        if (!sweep_sphere_against_body(
                sphere_collider, center, radius, delta, other, max_distance, &cur_hit))
            continue;
        if (!hit || cur_hit.distance < best.distance) {
            best = cur_hit;
            query_sanitize_hit(&best, max_distance, delta);
            hit = 1;
        }
    }
    if (rt_obj_release_check0(sphere_collider))
        rt_obj_free(sphere_collider);
    if (hit && out_hit)
        *out_hit = best;
    return hit;
}

/// @brief `World3D.OverlapSphere(center, radius, mask)` — list bodies overlapping a sphere.
///
/// Builds a transient sphere collider, then tests every world body
/// (after layer/mask filter) for overlap. Returns up to
/// `PH3D_MAX_QUERY_HITS` (256) hits as a `PhysicsHitList3D`. The
/// transient collider is released before returning.
void *rt_world3d_overlap_sphere(void *obj, void *center_obj, double radius, int64_t mask) {
    rt_world3d *w = world3d_checked(obj);
    rt_query_hit3d hits[PH3D_MAX_QUERY_HITS];
    int32_t hit_count = 0;
    int64_t total_count = 0;
    double center[3];
    double query_min[3], query_max[3];
    rt_body3d query_body;
    void *sphere_collider;
    if (!w || !rt_g3d_is_vec3(center_obj) || !isfinite(radius) || radius < 0.0)
        return NULL;
    if (!query_read_vec3(center_obj, center))
        return NULL;
    radius = query_sanitize_distance(radius);
    sphere_collider = rt_collider3d_new_sphere(radius);
    if (!sphere_collider)
        return NULL;
    init_temp_query_body(&query_body, sphere_collider, center);
    body3d_update_shape_cache_from_collider(&query_body);
    body_aabb(&query_body, query_min, query_max);
    int32_t entry_count = world3d_build_query_broadphase(w);
    for (int32_t i = 0; i < (entry_count >= 0 ? entry_count : w->body_count); ++i) {
        rt_body3d *body = entry_count >= 0 ? w->broadphase_entries[i].body : w->bodies[i];
        rt_query_hit3d hit;
        if (entry_count >= 0) {
            if (w->broadphase_entries[i].min[0] > query_max[0])
                break;
            if (!query_entry_overlaps_bounds(&w->broadphase_entries[i], query_min, query_max))
                continue;
        }
        if (!body3d_has_collision_geometry(body) || !query_mask_matches_body(body, mask))
            continue;
        if (overlap_query_body_against_body(&query_body, body, &hit)) {
            total_count++;
            if (hit_count < PH3D_MAX_QUERY_HITS)
                hits[hit_count++] = hit;
        }
    }
    if (rt_obj_release_check0(sphere_collider))
        rt_obj_free(sphere_collider);
    return physics_hit_list3d_new_ex(
        hits, hit_count, total_count, total_count > (int64_t)hit_count);
}

/// @brief `World3D.OverlapAabb(min, max, mask)` — list bodies overlapping a box.
///
/// Same pattern as `OverlapSphere` but uses a transient box collider
/// sized from the (min, max) corners. The half-extents are derived
/// from the corner spread; the center is the midpoint.
void *rt_world3d_overlap_aabb(void *obj, void *min_obj, void *max_obj, int64_t mask) {
    rt_world3d *w = world3d_checked(obj);
    rt_query_hit3d hits[PH3D_MAX_QUERY_HITS];
    int32_t hit_count = 0;
    int64_t total_count = 0;
    double mn[3], mx[3], center[3], half[3];
    double query_min[3], query_max[3];
    rt_body3d query_body;
    void *box_collider;
    if (!w || !rt_g3d_is_vec3(min_obj) || !rt_g3d_is_vec3(max_obj))
        return NULL;
    if (!query_read_vec3(min_obj, mn) || !query_read_vec3(max_obj, mx))
        return NULL;
    center[0] = query_saturate_coord((mn[0] + mx[0]) * 0.5);
    center[1] = query_saturate_coord((mn[1] + mx[1]) * 0.5);
    center[2] = query_saturate_coord((mn[2] + mx[2]) * 0.5);
    half[0] = query_sanitize_distance(fabs(mx[0] - mn[0]) * 0.5);
    half[1] = query_sanitize_distance(fabs(mx[1] - mn[1]) * 0.5);
    half[2] = query_sanitize_distance(fabs(mx[2] - mn[2]) * 0.5);
    if (!ph3d_vec3_all_finite(center) || !ph3d_vec3_all_finite(half))
        return NULL;
    box_collider = rt_collider3d_new_box(half[0], half[1], half[2]);
    if (!box_collider)
        return NULL;
    init_temp_query_body(&query_body, box_collider, center);
    body3d_update_shape_cache_from_collider(&query_body);
    body_aabb(&query_body, query_min, query_max);
    int32_t entry_count = world3d_build_query_broadphase(w);
    for (int32_t i = 0; i < (entry_count >= 0 ? entry_count : w->body_count); ++i) {
        rt_body3d *body = entry_count >= 0 ? w->broadphase_entries[i].body : w->bodies[i];
        rt_query_hit3d hit;
        if (entry_count >= 0) {
            if (w->broadphase_entries[i].min[0] > query_max[0])
                break;
            if (!query_entry_overlaps_bounds(&w->broadphase_entries[i], query_min, query_max))
                continue;
        }
        if (!body3d_has_collision_geometry(body) || !query_mask_matches_body(body, mask))
            continue;
        if (overlap_query_body_against_body(&query_body, body, &hit)) {
            total_count++;
            if (hit_count < PH3D_MAX_QUERY_HITS)
                hits[hit_count++] = hit;
        }
    }
    if (rt_obj_release_check0(box_collider))
        rt_obj_free(box_collider);
    return physics_hit_list3d_new_ex(
        hits, hit_count, total_count, total_count > (int64_t)hit_count);
}

/// @brief `World3D.SweepSphere(center, radius, delta, mask)` — first hit along a sphere sweep.
///
/// Returns the closest hit as a `PhysicsHit3D`, or NULL if the sweep
/// reaches `delta` without contact. Used for trajectory predictions
/// and projectile collision.
void *rt_world3d_sweep_sphere(
    void *obj, void *center_obj, double radius, void *delta_obj, int64_t mask) {
    rt_world3d *w = world3d_checked(obj);
    rt_query_hit3d best_hit = {0};
    int found = 0;
    double center[3], delta[3];
    double query_min[3], query_max[3], swept_min[3], swept_max[3];
    double max_distance;
    rt_body3d query_body;
    void *sphere_collider;
    if (!w || !rt_g3d_is_vec3(center_obj) || !rt_g3d_is_vec3(delta_obj) || !isfinite(radius) ||
        radius < 0.0)
        return NULL;
    if (!query_read_vec3(center_obj, center) || !query_read_vec3(delta_obj, delta))
        return NULL;
    radius = query_sanitize_distance(radius);
    max_distance = query_cap_vector_length(delta);
    sphere_collider = rt_collider3d_new_sphere(radius);
    if (!sphere_collider)
        return NULL;
    init_temp_query_body(&query_body, sphere_collider, center);
    body3d_update_shape_cache_from_collider(&query_body);
    body_aabb(&query_body, query_min, query_max);
    swept_aabb_from_points(query_min, query_max, delta, swept_min, swept_max);
    int32_t entry_count = world3d_build_query_broadphase(w);
    for (int32_t i = 0; i < (entry_count >= 0 ? entry_count : w->body_count); ++i) {
        rt_body3d *body = entry_count >= 0 ? w->broadphase_entries[i].body : w->bodies[i];
        rt_query_hit3d hit;
        if (entry_count >= 0) {
            if (w->broadphase_entries[i].min[0] > swept_max[0])
                break;
            if (!query_entry_overlaps_bounds(&w->broadphase_entries[i], swept_min, swept_max))
                continue;
        }
        if (!body3d_has_collision_geometry(body) || !query_mask_matches_body(body, mask))
            continue;
        if (!sweep_sphere_against_body(
                sphere_collider, center, radius, delta, body, max_distance, &hit))
            continue;
        if (!query_sanitize_hit(&hit, max_distance, delta))
            continue;
        if (!found || hit.distance < best_hit.distance) {
            best_hit = hit;
            found = 1;
        }
    }
    if (rt_obj_release_check0(sphere_collider))
        rt_obj_free(sphere_collider);
    return found ? physics_hit3d_new(&best_hit) : NULL;
}

/// @brief `World3D.SweepCapsule(a, b, radius, delta, mask)` — first hit along a capsule sweep.
///
/// Like `SweepSphere` but for capsule queries — primary use case is
/// character-controller motion against world geometry.
void *rt_world3d_sweep_capsule(
    void *obj, void *a_obj, void *b_obj, double radius, void *delta_obj, int64_t mask) {
    rt_world3d *w = world3d_checked(obj);
    rt_query_hit3d best_hit = {0};
    int found = 0;
    double a[3], b[3], delta[3];
    double query_min[3], query_max[3], swept_min[3], swept_max[3];
    double max_distance;
    if (!w || !rt_g3d_is_vec3(a_obj) || !rt_g3d_is_vec3(b_obj) || !rt_g3d_is_vec3(delta_obj) ||
        !isfinite(radius) || radius < 0.0)
        return NULL;
    if (!query_read_vec3(a_obj, a) || !query_read_vec3(b_obj, b) ||
        !query_read_vec3(delta_obj, delta))
        return NULL;
    radius = query_sanitize_distance(radius);
    max_distance = query_cap_vector_length(delta);
    for (int axis = 0; axis < 3; ++axis) {
        query_min[axis] = query_saturate_coord(fmin(a[axis], b[axis]) - radius);
        query_max[axis] = query_saturate_coord(fmax(a[axis], b[axis]) + radius);
    }
    swept_aabb_from_points(query_min, query_max, delta, swept_min, swept_max);
    int32_t entry_count = world3d_build_query_broadphase(w);
    for (int32_t i = 0; i < (entry_count >= 0 ? entry_count : w->body_count); ++i) {
        rt_body3d *body = entry_count >= 0 ? w->broadphase_entries[i].body : w->bodies[i];
        rt_query_hit3d hit;
        if (entry_count >= 0) {
            if (w->broadphase_entries[i].min[0] > swept_max[0])
                break;
            if (!query_entry_overlaps_bounds(&w->broadphase_entries[i], swept_min, swept_max))
                continue;
        }
        if (!body3d_has_collision_geometry(body) || !query_mask_matches_body(body, mask))
            continue;
        if (!sweep_capsule_against_body(a, b, radius, delta, body, max_distance, &hit))
            continue;
        if (!query_sanitize_hit(&hit, max_distance, delta))
            continue;
        if (!found || hit.distance < best_hit.distance) {
            best_hit = hit;
            found = 1;
        }
    }
    return found ? physics_hit3d_new(&best_hit) : NULL;
}

/// @brief Populate a ray-hit result record (body, distance, world point, and
///        normal) from a confirmed intersection at @p distance along the ray.
static void ray_fill_hit(rt_body3d *body,
                         double distance,
                         double max_distance,
                         const double *origin,
                         const double *dir,
                         const double *normal,
                         int started,
                         rt_query_hit3d *out_hit) {
    if (!out_hit)
        return;
    memset(out_hit, 0, sizeof(*out_hit));
    out_hit->body = body;
    out_hit->collider = body ? body->collider : NULL;
    out_hit->distance = query_sanitize_distance(distance);
    out_hit->fraction = max_distance > 1e-12 ? out_hit->distance / max_distance : 0.0;
    out_hit->started_penetrating = (int8_t)(started ? 1 : 0);
    out_hit->is_trigger = body ? body->is_trigger : 0;
    out_hit->point[0] = query_saturate_coord(origin[0] + dir[0] * out_hit->distance);
    out_hit->point[1] = query_saturate_coord(origin[1] + dir[1] * out_hit->distance);
    out_hit->point[2] = query_saturate_coord(origin[2] + dir[2] * out_hit->distance);
    if (normal)
        vec3_copy(out_hit->normal, normal);
    else
        vec3_negate(dir, out_hit->normal);
    query_sanitize_hit(out_hit, max_distance, dir);
}

/// @brief Ray vs sphere intersection (analytic quadratic solve).
/// @return Non-zero on a hit with the entry distance written, 0 otherwise.
static int raycast_sphere_raw(const double *origin,
                              const double *dir,
                              const double *center,
                              double radius,
                              double max_distance,
                              double *out_t,
                              double *out_normal,
                              int *out_started) {
    double m[3];
    if (!origin || !dir || !center || !isfinite(radius) || radius < 0.0 ||
        !isfinite(max_distance) || max_distance < 0.0)
        return 0;
    radius = query_sanitize_distance(radius);
    max_distance = query_sanitize_distance(max_distance);
    vec3_sub(origin, center, m);
    double c = vec3_dot(m, m) - radius * radius;
    if (c <= 0.0) {
        if (out_t)
            *out_t = 0.0;
        if (out_started)
            *out_started = 1;
        if (out_normal)
            vec3_negate(dir, out_normal);
        return 1;
    }
    double b = vec3_dot(m, dir);
    double disc = b * b - c;
    if (!isfinite(disc) || disc < 0.0)
        return 0;
    double t = -b - sqrt(disc);
    if (!isfinite(t) || t < 0.0 || t > max_distance)
        return 0;
    if (out_t)
        *out_t = t;
    if (out_started)
        *out_started = 0;
    if (out_normal) {
        out_normal[0] = origin[0] + dir[0] * t - center[0];
        out_normal[1] = origin[1] + dir[1] * t - center[1];
        out_normal[2] = origin[2] + dir[2] * t - center[2];
        query_normalize_normal(out_normal, dir);
    }
    return 1;
}

/// @brief Ray vs axis-aligned box intersection (slab method).
/// @return Non-zero on a hit with the entry distance written, 0 otherwise.
static int raycast_aabb_raw(const double *origin,
                            const double *dir,
                            const double *mn,
                            const double *mx,
                            double max_distance,
                            double *out_t,
                            double *out_normal,
                            int *out_started) {
    double tmin = 0.0;
    double tmax = max_distance;
    double n[3] = {0.0, 0.0, 0.0};
    int inside = 1;
    if (!origin || !dir || !mn || !mx || !isfinite(max_distance) || max_distance < 0.0)
        return 0;
    max_distance = query_sanitize_distance(max_distance);
    tmax = max_distance;
    for (int axis = 0; axis < 3; axis++) {
        if (origin[axis] < mn[axis] || origin[axis] > mx[axis])
            inside = 0;
        if (fabs(dir[axis]) < 1e-12) {
            if (origin[axis] < mn[axis] || origin[axis] > mx[axis])
                return 0;
            continue;
        }
        double inv = 1.0 / dir[axis];
        double t1 = (mn[axis] - origin[axis]) * inv;
        double t2 = (mx[axis] - origin[axis]) * inv;
        double sign = -1.0;
        if (!isfinite(t1) || !isfinite(t2))
            return 0;
        if (t1 > t2) {
            double tmp = t1;
            t1 = t2;
            t2 = tmp;
            sign = 1.0;
        }
        if (t1 > tmin) {
            tmin = t1;
            vec3_set(n, 0.0, 0.0, 0.0);
            n[axis] = sign;
        }
        if (t2 < tmax)
            tmax = t2;
        if (tmin > tmax)
            return 0;
    }
    if (inside) {
        if (out_t)
            *out_t = 0.0;
        if (out_started)
            *out_started = 1;
        if (out_normal) {
            vec3_negate(dir, out_normal);
            query_normalize_normal(out_normal, dir);
        }
        return 1;
    }
    if (tmin < 0.0 || tmin > max_distance)
        return 0;
    if (out_t)
        *out_t = tmin;
    if (out_started)
        *out_started = 0;
    if (out_normal) {
        vec3_copy(out_normal, n);
        query_normalize_normal(out_normal, dir);
    }
    return 1;
}

/// @brief Ray vs capsule intersection (cylinder body plus hemisphere caps).
/// @return Non-zero on a hit with the entry distance written, 0 otherwise.
static int raycast_capsule_raw(const double *origin,
                               const double *dir,
                               const double *a,
                               const double *b,
                               double radius,
                               double max_distance,
                               double *out_t,
                               double *out_normal,
                               int *out_started) {
    double best_t = max_distance + 1.0;
    double best_n[3] = {0.0, 0.0, 0.0};
    int best_started = 0;
    int found = 0;
    double t, n[3];
    int started;
    if (!origin || !dir || !a || !b || !isfinite(radius) || radius < 0.0 ||
        !isfinite(max_distance) || max_distance < 0.0)
        return 0;
    radius = query_sanitize_distance(radius);
    max_distance = query_sanitize_distance(max_distance);
    if (raycast_sphere_raw(origin, dir, a, radius, max_distance, &t, n, &started)) {
        best_t = t;
        vec3_copy(best_n, n);
        best_started = started;
        found = 1;
    }
    if (raycast_sphere_raw(origin, dir, b, radius, max_distance, &t, n, &started) &&
        (!found || t < best_t)) {
        best_t = t;
        vec3_copy(best_n, n);
        best_started = started;
        found = 1;
    }
    double axis[3];
    vec3_sub(b, a, axis);
    double h = vec3_normalize_in_place(axis);
    if (h > 1e-12) {
        double m[3];
        vec3_sub(origin, a, m);
        double md = vec3_dot(m, axis);
        double nd = vec3_dot(dir, axis);
        double mp[3] = {m[0] - axis[0] * md, m[1] - axis[1] * md, m[2] - axis[2] * md};
        double dp[3] = {dir[0] - axis[0] * nd, dir[1] - axis[1] * nd, dir[2] - axis[2] * nd};
        double qa = vec3_dot(dp, dp);
        double qb = 2.0 * vec3_dot(mp, dp);
        double qc = vec3_dot(mp, mp) - radius * radius;
        if (qc <= 0.0 && md >= 0.0 && md <= h) {
            best_t = 0.0;
            vec3_negate(dir, best_n);
            query_normalize_normal(best_n, dir);
            best_started = 1;
            found = 1;
        } else if (qa > 1e-12) {
            double disc = qb * qb - 4.0 * qa * qc;
            if (isfinite(disc) && disc >= 0.0) {
                double root = sqrt(disc);
                double roots[2] = {(-qb - root) / (2.0 * qa), (-qb + root) / (2.0 * qa)};
                for (int i = 0; i < 2; i++) {
                    double ct = roots[i];
                    double y = md + ct * nd;
                    if (!isfinite(ct) || !isfinite(y) || ct < 0.0 || ct > max_distance ||
                        y < 0.0 || y > h)
                        continue;
                    if (!found || ct < best_t) {
                        double p[3] = {origin[0] + dir[0] * ct,
                                       origin[1] + dir[1] * ct,
                                       origin[2] + dir[2] * ct};
                        double c[3] = {a[0] + axis[0] * y, a[1] + axis[1] * y, a[2] + axis[2] * y};
                        best_t = ct;
                        best_n[0] = p[0] - c[0];
                        best_n[1] = p[1] - c[1];
                        best_n[2] = p[2] - c[2];
                        query_normalize_normal(best_n, dir);
                        best_started = 0;
                        found = 1;
                    }
                }
            }
        }
    }
    if (!found)
        return 0;
    if (out_t)
        *out_t = best_t;
    if (out_normal) {
        vec3_copy(out_normal, best_n);
        query_normalize_normal(out_normal, dir);
    }
    if (out_started)
        *out_started = best_started;
    return 1;
}

/// @brief Raycast against a posed box (optionally Minkowski-expanded by @p expand_radius
///   for swept-sphere tests): transform the ray into box-local space and slab-test the
///   AABB. Returns 1 on hit with distance @p out_t, world normal @p out_normal, and
///   @p out_started set when the ray began already inside the box.
static int raycast_box_pose_raw(void *box_collider,
                                const rt_collider_pose *pose,
                                const double *origin,
                                const double *dir,
                                double max_distance,
                                double expand_radius,
                                double *out_t,
                                double *out_normal,
                                int *out_started) {
    double he[3];
    double local_origin[3];
    double local_dir[3];
    double mn[3];
    double mx[3];
    double local_normal[3] = {0.0, 0.0, 0.0};
    double t = 0.0;
    int started = 0;
    if (!box_collider || !pose || !origin || !dir || !isfinite(max_distance) ||
        max_distance < 0.0 || !isfinite(expand_radius) || expand_radius < 0.0)
        return 0;
    max_distance = query_sanitize_distance(max_distance);
    expand_radius = query_sanitize_distance(expand_radius);
    rt_collider3d_get_box_half_extents_raw(box_collider, he);
    for (int i = 0; i < 3; i++) {
        double expansion = expand_radius / pose_abs_scale_or_unit(pose->scale[i]);
        mn[i] = -he[i] - expansion;
        mx[i] = he[i] + expansion;
    }
    transform_point_to_local(pose, origin, local_origin);
    transform_vector_to_local(pose, dir, local_dir);
    ph3d_vec3_sanitize_state(local_origin);
    ph3d_vec3_sanitize_state(local_dir);
    if (!raycast_aabb_raw(
            local_origin, local_dir, mn, mx, max_distance, &t, local_normal, &started))
        return 0;
    if (out_t)
        *out_t = t;
    if (out_started)
        *out_started = started;
    if (out_normal) {
        if (started) {
            vec3_negate(dir, out_normal);
            query_normalize_normal(out_normal, dir);
        } else {
            transform_normal_from_local(pose, local_normal, out_normal);
            query_normalize_normal(out_normal, dir);
        }
    }
    return 1;
}

/// @brief Möller-Trumbore ray/triangle intersection in world space. Returns 1 on a hit
///   within @p max_distance with distance @p out_t and the geometric face normal
///   @p out_normal, else 0 (including parallel rays and barycentric misses).
static int raycast_triangle_world(const double *origin,
                                  const double *dir,
                                  const double *a,
                                  const double *b,
                                  const double *c,
                                  double max_distance,
                                  double *out_t,
                                  double *out_normal) {
    double e1[3], e2[3], pvec[3], tvec[3], qvec[3];
    double det, inv_det, u, v, t;
    if (!origin || !dir || !a || !b || !c || !isfinite(max_distance) || max_distance < 0.0)
        return 0;
    max_distance = query_sanitize_distance(max_distance);
    vec3_sub(b, a, e1);
    vec3_sub(c, a, e2);
    vec3_cross(dir, e2, pvec);
    det = vec3_dot(e1, pvec);
    if (!isfinite(det) || fabs(det) < 1e-12)
        return 0;
    inv_det = 1.0 / det;
    vec3_sub(origin, a, tvec);
    u = vec3_dot(tvec, pvec) * inv_det;
    if (!isfinite(u) || u < 0.0 || u > 1.0)
        return 0;
    vec3_cross(tvec, e1, qvec);
    v = vec3_dot(dir, qvec) * inv_det;
    if (!isfinite(v) || v < 0.0 || u + v > 1.0)
        return 0;
    t = vec3_dot(e2, qvec) * inv_det;
    if (!isfinite(t) || t < 0.0 || t > max_distance)
        return 0;
    if (out_t)
        *out_t = t;
    if (out_normal) {
        triangle_normal(a, b, c, out_normal);
        if (vec3_dot(out_normal, dir) > 0.0)
            vec3_negate(out_normal, out_normal);
        query_normalize_normal(out_normal, dir);
    }
    return 1;
}

/// @brief Grow the AABB [mn, mx] in place to include point @p p.
static void mesh_bvh_expand(float *mn, float *mx, const float *p) {
    for (int axis = 0; axis < 3; axis++) {
        if (p[axis] < mn[axis])
            mn[axis] = p[axis];
        if (p[axis] > mx[axis])
            mx[axis] = p[axis];
    }
}

/// @brief Sanitize a mesh vertex coordinate before storing it in the physics BVH.
static float mesh_bvh_sanitize_coord(float value) {
    if (!isfinite((double)value))
        return 0.0f;
    if (value > (float)PH3D_QUERY_COORD_ABS_MAX)
        return (float)PH3D_QUERY_COORD_ABS_MAX;
    if (value < (float)-PH3D_QUERY_COORD_ABS_MAX)
        return (float)-PH3D_QUERY_COORD_ABS_MAX;
    return value;
}

/// @brief Compute triangle @p tri's AABB (@p mn, @p mx) and centroid for BVH construction.
static void mesh_bvh_triangle_bounds(
    const rt_mesh3d *mesh, uint32_t tri, float *mn, float *mx, float *centroid) {
    uint32_t i0 = mesh->indices[tri * 3u + 0u];
    uint32_t i1 = mesh->indices[tri * 3u + 1u];
    uint32_t i2 = mesh->indices[tri * 3u + 2u];
    float a[3] = {mesh_bvh_sanitize_coord(mesh->vertices[i0].pos[0]),
                  mesh_bvh_sanitize_coord(mesh->vertices[i0].pos[1]),
                  mesh_bvh_sanitize_coord(mesh->vertices[i0].pos[2])};
    float b[3] = {mesh_bvh_sanitize_coord(mesh->vertices[i1].pos[0]),
                  mesh_bvh_sanitize_coord(mesh->vertices[i1].pos[1]),
                  mesh_bvh_sanitize_coord(mesh->vertices[i1].pos[2])};
    float c[3] = {mesh_bvh_sanitize_coord(mesh->vertices[i2].pos[0]),
                  mesh_bvh_sanitize_coord(mesh->vertices[i2].pos[1]),
                  mesh_bvh_sanitize_coord(mesh->vertices[i2].pos[2])};
    for (int axis = 0; axis < 3; axis++) {
        mn[axis] = fminf(a[axis], fminf(b[axis], c[axis]));
        mx[axis] = fmaxf(a[axis], fmaxf(b[axis], c[axis]));
        centroid[axis] = (a[axis] + b[axis] + c[axis]) / 3.0f;
    }
}

/// @brief Recursively build a physics BVH node over a range of mesh triangles.
/// @details Computes the node bounds, and if the range exceeds the leaf threshold, splits the
///          triangles by centroid along the widest axis and recurses into two children; otherwise
///          stores a leaf. Returns the node index, or a negative value on allocation failure.
static int mesh_bvh_build_node(rt_mesh3d *mesh,
                               rt_physics_mesh_bvh_node *nodes,
                               int32_t *node_count,
                               int32_t node_capacity,
                               uint32_t *tri_indices,
                               int32_t start,
                               int32_t count) {
    int32_t node_index;
    rt_physics_mesh_bvh_node *node;
    float centroid_min[3] = {FLT_MAX, FLT_MAX, FLT_MAX};
    float centroid_max[3] = {-FLT_MAX, -FLT_MAX, -FLT_MAX};
    int split_axis = 0;
    if (!mesh || !nodes || !node_count || !tri_indices || count <= 0 ||
        *node_count >= node_capacity)
        return -1;
    node_index = (*node_count)++;
    node = &nodes[node_index];
    node->left = -1;
    node->right = -1;
    node->start = start;
    node->count = count;
    node->min[0] = node->min[1] = node->min[2] = FLT_MAX;
    node->max[0] = node->max[1] = node->max[2] = -FLT_MAX;
    for (int32_t i = start; i < start + count; i++) {
        float tri_min[3], tri_max[3], centroid[3];
        mesh_bvh_triangle_bounds(mesh, tri_indices[i], tri_min, tri_max, centroid);
        mesh_bvh_expand(node->min, node->max, tri_min);
        mesh_bvh_expand(node->min, node->max, tri_max);
        mesh_bvh_expand(centroid_min, centroid_max, centroid);
    }
    if (count <= 8)
        return node_index;
    {
        float extent_x = centroid_max[0] - centroid_min[0];
        float extent_y = centroid_max[1] - centroid_min[1];
        float extent_z = centroid_max[2] - centroid_min[2];
        if (extent_y > extent_x && extent_y >= extent_z)
            split_axis = 1;
        else if (extent_z > extent_x && extent_z >= extent_y)
            split_axis = 2;
    }
    {
        float pivot = 0.5f * (centroid_min[split_axis] + centroid_max[split_axis]);
        int32_t lo = start;
        int32_t hi = start + count - 1;
        while (lo <= hi) {
            float tri_min[3], tri_max[3], centroid[3];
            mesh_bvh_triangle_bounds(mesh, tri_indices[lo], tri_min, tri_max, centroid);
            if (centroid[split_axis] < pivot) {
                lo++;
            } else {
                uint32_t tmp = tri_indices[lo];
                tri_indices[lo] = tri_indices[hi];
                tri_indices[hi] = tmp;
                hi--;
            }
        }
        if (lo == start || lo == start + count)
            lo = start + count / 2;
        node->left = mesh_bvh_build_node(
            mesh, nodes, node_count, node_capacity, tri_indices, start, lo - start);
        node->right = mesh_bvh_build_node(
            mesh, nodes, node_count, node_capacity, tri_indices, lo, start + count - lo);
        if (node->left < 0 || node->right < 0) {
            node->left = node->right = -1;
            node->start = start;
            node->count = count;
        } else {
            node->count = 0;
        }
    }
    return node_index;
}

/// @brief Rebuild the mesh's physics BVH acceleration structure for ray/shape queries.
/// @details Allocates the node/index arrays and builds the tree from the current geometry; cached
/// so
///          repeated queries against an unchanged mesh skip the rebuild.
/// @return 1 on success, 0 on allocation failure or empty geometry.
int mesh_physics_bvh_rebuild(rt_mesh3d *mesh) {
    uint32_t tri_total;
    uint32_t *tri_indices = NULL;
    rt_physics_mesh_bvh_node *nodes = NULL;
    int32_t tri_count = 0;
    int32_t node_capacity;
    int32_t node_count = 0;
    if (!mesh || mesh->index_count < 3 || mesh->vertex_count == 0)
        return 0;
    if (mesh->physics_bvh_nodes && mesh->physics_bvh_revision == mesh->geometry_revision)
        return mesh->physics_bvh_node_count > 0;
    free(mesh->physics_bvh_nodes);
    mesh->physics_bvh_nodes = NULL;
    free(mesh->physics_bvh_tri_indices);
    mesh->physics_bvh_tri_indices = NULL;
    mesh->physics_bvh_node_count = 0;
    mesh->physics_bvh_tri_count = 0;
    mesh->physics_bvh_revision = 0;
    tri_total = mesh->index_count / 3u;
    if (tri_total == 0 || tri_total > (uint32_t)(INT32_MAX / 2))
        return 0;
    tri_indices = (uint32_t *)malloc((size_t)tri_total * sizeof(*tri_indices));
    if (!tri_indices)
        return 0;
    for (uint32_t tri = 0; tri < tri_total; tri++) {
        uint32_t i0 = mesh->indices[tri * 3u + 0u];
        uint32_t i1 = mesh->indices[tri * 3u + 1u];
        uint32_t i2 = mesh->indices[tri * 3u + 2u];
        if (i0 >= mesh->vertex_count || i1 >= mesh->vertex_count || i2 >= mesh->vertex_count)
            continue;
        tri_indices[tri_count++] = tri;
    }
    if (tri_count <= 0) {
        free(tri_indices);
        return 0;
    }
    node_capacity = tri_count * 2;
    nodes = (rt_physics_mesh_bvh_node *)calloc((size_t)node_capacity, sizeof(*nodes));
    if (!nodes) {
        free(tri_indices);
        return 0;
    }
    if (mesh_bvh_build_node(mesh, nodes, &node_count, node_capacity, tri_indices, 0, tri_count) <
        0) {
        free(nodes);
        free(tri_indices);
        return 0;
    }
    mesh->physics_bvh_nodes = nodes;
    mesh->physics_bvh_tri_indices = tri_indices;
    mesh->physics_bvh_node_count = node_count;
    mesh->physics_bvh_tri_count = tri_count;
    mesh->physics_bvh_revision = mesh->geometry_revision;
    return 1;
}

/// @brief Transform a local-space AABB through a collider pose into a world-space AABB.
/// @details Rotates and translates all eight corners and takes their min/max, so the result tightly
///          bounds the oriented box (a plain min/max of the rotated extents would over-grow it).
void transform_local_aabb_to_world(const rt_collider_pose *pose,
                                   const float *mn,
                                   const float *mx,
                                   double *out_min,
                                   double *out_max) {
    for (int axis = 0; axis < 3; axis++) {
        out_min[axis] = DBL_MAX;
        out_max[axis] = -DBL_MAX;
    }
    for (int i = 0; i < 8; i++) {
        double local[3] = {
            (i & 1) ? mx[0] : mn[0], (i & 2) ? mx[1] : mn[1], (i & 4) ? mx[2] : mn[2]};
        double world[3];
        transform_point_from_pose(pose, local, world);
        for (int axis = 0; axis < 3; axis++) {
            if (world[axis] < out_min[axis])
                out_min[axis] = world[axis];
            if (world[axis] > out_max[axis])
                out_max[axis] = world[axis];
        }
    }
}

/// @brief Raycast a posed triangle mesh by transforming each triangle to world space and
///   keeping the nearest Möller-Trumbore hit within @p max_distance. Returns 1 on hit
///   with @p out_t/@p out_normal; meshes are surfaces so @p out_started is always 0.
static int raycast_meshlike_pose_raw(rt_mesh3d *mesh,
                                     const rt_collider_pose *pose,
                                     const double *origin,
                                     const double *dir,
                                     double max_distance,
                                     double *out_t,
                                     double *out_normal,
                                     int *out_started) {
    double best_t = max_distance + 1.0;
    double best_normal[3] = {0.0, 1.0, 0.0};
    int found = 0;
    if (!mesh || !pose || mesh->index_count < 3)
        return 0;
    rt_mesh3d_refresh_bounds(mesh);
    if (mesh_physics_bvh_rebuild(mesh)) {
        const rt_physics_mesh_bvh_node *nodes =
            (const rt_physics_mesh_bvh_node *)mesh->physics_bvh_nodes;
        const uint32_t *tri_indices = mesh->physics_bvh_tri_indices;
        int32_t *stack = NULL;
        int32_t top = 0;
        int32_t stack_capacity = 0;
        int overflow = 0;
        if (!ph3d_i32_stack_push(&stack, &top, &stack_capacity, 0))
            overflow = 1;
        while (top > 0) {
            int32_t node_index = stack[--top];
            const rt_physics_mesh_bvh_node *node;
            double node_min[3], node_max[3], box_t;
            if (node_index < 0 || node_index >= mesh->physics_bvh_node_count)
                continue;
            node = &nodes[node_index];
            transform_local_aabb_to_world(pose, node->min, node->max, node_min, node_max);
            if (!raycast_aabb_raw(origin, dir, node_min, node_max, best_t, &box_t, NULL, NULL))
                continue;
            if (node->left >= 0 || node->right >= 0) {
                if (node->right >= 0 &&
                    !ph3d_i32_stack_push(&stack, &top, &stack_capacity, node->right)) {
                    overflow = 1;
                    break;
                }
                if (node->left >= 0 &&
                    !ph3d_i32_stack_push(&stack, &top, &stack_capacity, node->left)) {
                    overflow = 1;
                    break;
                }
                continue;
            }
            for (int32_t item = node->start; item < node->start + node->count; item++) {
                uint32_t tri = tri_indices[item];
                uint32_t i0 = mesh->indices[tri * 3u + 0u];
                uint32_t i1 = mesh->indices[tri * 3u + 1u];
                uint32_t i2 = mesh->indices[tri * 3u + 2u];
                double a[3], b[3], c[3], local[3], t, n[3];
                if (i0 >= mesh->vertex_count || i1 >= mesh->vertex_count ||
                    i2 >= mesh->vertex_count)
                    continue;
                local[0] = mesh->vertices[i0].pos[0];
                local[1] = mesh->vertices[i0].pos[1];
                local[2] = mesh->vertices[i0].pos[2];
                transform_point_from_pose(pose, local, a);
                local[0] = mesh->vertices[i1].pos[0];
                local[1] = mesh->vertices[i1].pos[1];
                local[2] = mesh->vertices[i1].pos[2];
                transform_point_from_pose(pose, local, b);
                local[0] = mesh->vertices[i2].pos[0];
                local[1] = mesh->vertices[i2].pos[1];
                local[2] = mesh->vertices[i2].pos[2];
                transform_point_from_pose(pose, local, c);
                if (raycast_triangle_world(origin, dir, a, b, c, best_t, &t, n) && t < best_t) {
                    best_t = t;
                    vec3_copy(best_normal, n);
                    found = 1;
                }
            }
        }
        free(stack);
        if (!overflow)
            goto mesh_raycast_done;
        best_t = max_distance + 1.0;
        found = 0;
    }
    for (uint32_t i = 0; i + 2 < mesh->index_count; i += 3) {
        uint32_t i0 = mesh->indices[i];
        uint32_t i1 = mesh->indices[i + 1];
        uint32_t i2 = mesh->indices[i + 2];
        double a[3], b[3], c[3], local[3], t, n[3];
        if (i0 >= mesh->vertex_count || i1 >= mesh->vertex_count || i2 >= mesh->vertex_count)
            continue;
        local[0] = mesh->vertices[i0].pos[0];
        local[1] = mesh->vertices[i0].pos[1];
        local[2] = mesh->vertices[i0].pos[2];
        transform_point_from_pose(pose, local, a);
        local[0] = mesh->vertices[i1].pos[0];
        local[1] = mesh->vertices[i1].pos[1];
        local[2] = mesh->vertices[i1].pos[2];
        transform_point_from_pose(pose, local, b);
        local[0] = mesh->vertices[i2].pos[0];
        local[1] = mesh->vertices[i2].pos[1];
        local[2] = mesh->vertices[i2].pos[2];
        transform_point_from_pose(pose, local, c);
        if (raycast_triangle_world(origin, dir, a, b, c, max_distance, &t, n) && t < best_t) {
            best_t = t;
            vec3_copy(best_normal, n);
            found = 1;
        }
    }
mesh_raycast_done:
    if (!found)
        return 0;
    if (out_t)
        *out_t = best_t;
    if (out_normal)
        vec3_copy(out_normal, best_normal);
    if (out_started)
        *out_started = 0;
    return 1;
}

/// @brief Raycast a posed heightfield collider: clip the ray to the field AABB, then
///   march in local space sampling terrain height until the ray dips below the surface.
///   Returns 1 on hit with @p out_t/@p out_normal (sampled surface normal), else 0.
static int raycast_heightfield_pose_raw(void *heightfield,
                                        const rt_collider_pose *pose,
                                        const double *origin,
                                        const double *dir,
                                        double max_distance,
                                        double *out_t,
                                        double *out_normal,
                                        int *out_started) {
    double mn[3], mx[3], entry_t, aabb_normal[3];
    int started = 0;
    double local_origin[3], local_dir[3];
    double start_t;
    double step;
    double heightfield_scale[3] = {1.0, 1.0, 1.0};
    int32_t heightfield_width = 0;
    int32_t heightfield_depth = 0;
    double prev_t;
    double prev_clearance = DBL_MAX;
    int has_prev = 0;
    if (!heightfield || !pose || !origin || !dir || !isfinite(max_distance) || max_distance <= 0.0)
        return 0;
    max_distance = query_sanitize_distance(max_distance);
    rt_collider3d_compute_world_aabb_raw(
        heightfield, pose->position, pose->rotation, pose->scale, mn, mx);
    if (!raycast_aabb_raw(origin, dir, mn, mx, max_distance, &entry_t, aabb_normal, &started))
        return 0;
    transform_point_to_local(pose, origin, local_origin);
    transform_vector_to_local(pose, dir, local_dir);
    start_t = started ? 0.0 : entry_t;
    step = max_distance / 512.0;
    if (rt_collider3d_get_heightfield_info_raw(
            heightfield, &heightfield_width, &heightfield_depth, heightfield_scale)) {
        double sx = fabs(heightfield_scale[0]);
        double sz = fabs(heightfield_scale[2]);
        double cell = sx > 1e-12 && sz > 1e-12 ? fmin(sx, sz) : fmax(sx, sz);
        double horizontal_speed = sqrt(local_dir[0] * local_dir[0] + local_dir[2] * local_dir[2]);
        if (heightfield_width > 1 && heightfield_depth > 1 && cell > 1e-12 &&
            horizontal_speed > 1e-12) {
            step = (cell * 0.5) / horizontal_speed;
        }
    }
    if (!isfinite(step) || step <= 0.0)
        step = max_distance / 512.0;
    if (!isfinite(step) || step < 1e-5)
        step = 1e-5;
    if (step > max_distance)
        step = max_distance;
    if (!isfinite(step) || step <= 0.0)
        return 0;
    prev_t = start_t;
    for (double t = start_t, march_iter = 0.0; t <= max_distance + 1e-9 && march_iter < 2048.0;
         t += step, march_iter += 1.0) {
        double local_point[3] = {local_origin[0] + local_dir[0] * t,
                                 local_origin[1] + local_dir[1] * t,
                                 local_origin[2] + local_dir[2] * t};
        double surface = 0.0;
        double local_normal[3] = {0.0, 1.0, 0.0};
        double clearance;
        if (!rt_collider3d_sample_heightfield_raw(
                heightfield, local_point[0], local_point[2], &surface, local_normal)) {
            prev_t = t;
            has_prev = 0;
            continue;
        }
        clearance = local_point[1] - surface;
        if (clearance <= 0.0) {
            double hit_t = t;
            if (has_prev && prev_clearance > 0.0) {
                double lo = prev_t;
                double hi = t;
                for (int iter = 0; iter < 16; iter++) {
                    double mid = (lo + hi) * 0.5;
                    double mid_point[3] = {local_origin[0] + local_dir[0] * mid,
                                           local_origin[1] + local_dir[1] * mid,
                                           local_origin[2] + local_dir[2] * mid};
                    double mid_surface = 0.0;
                    double mid_normal[3] = {0.0, 1.0, 0.0};
                    if (rt_collider3d_sample_heightfield_raw(
                            heightfield, mid_point[0], mid_point[2], &mid_surface, mid_normal) &&
                        mid_point[1] - mid_surface <= 0.0) {
                        hi = mid;
                        vec3_copy(local_normal, mid_normal);
                    } else {
                        lo = mid;
                    }
                }
                hit_t = hi;
            }
            if (out_t)
                *out_t = hit_t;
            if (out_started)
                *out_started = (t == start_t && clearance <= 0.0) ? 1 : 0;
            if (out_normal) {
                transform_normal_from_local(pose, local_normal, out_normal);
                query_normalize_normal(out_normal, dir);
            }
            return 1;
        }
        prev_clearance = clearance;
        prev_t = t;
        has_prev = 1;
    }
    return 0;
}

/// @brief Top-level raycast dispatch for any posed collider: routes by collider type to
///   the box/sphere/capsule/mesh/heightfield/compound raycast (recursing into compound
///   children and returning the hit leaf via @p out_leaf). Returns 1 on the nearest hit
///   within @p max_distance with @p out_t/@p out_normal/@p out_started filled.
static int raycast_collider_pose(void *collider,
                                 const rt_collider_pose *pose,
                                 const double *origin,
                                 const double *dir,
                                 double max_distance,
                                 double *out_t,
                                 double *out_normal,
                                 int *out_started,
                                 void **out_leaf) {
    int64_t type;
    double t = 0.0;
    double normal[3] = {0.0, 1.0, 0.0};
    int started = 0;
    if (!collider || !pose)
        return 0;
    type = rt_collider3d_get_type(collider);
    if (type == RT_COLLIDER3D_TYPE_BOX) {
        if (!raycast_box_pose_raw(
                collider, pose, origin, dir, max_distance, 0.0, &t, normal, &started))
            return 0;
    } else if (type == RT_COLLIDER3D_TYPE_SPHERE) {
        double radius = rt_collider3d_get_radius_raw(collider);
        double sx = pose_abs_scale_or_unit(pose->scale[0]);
        double sy = pose_abs_scale_or_unit(pose->scale[1]);
        double sz = pose_abs_scale_or_unit(pose->scale[2]);
        double max_scale = sx > sy ? sx : sy;
        if (sz > max_scale)
            max_scale = sz;
        if (!raycast_sphere_raw(origin,
                                dir,
                                pose->position,
                                radius * max_scale,
                                max_distance,
                                &t,
                                normal,
                                &started))
            return 0;
    } else if (type == RT_COLLIDER3D_TYPE_CAPSULE) {
        rt_body3d proxy;
        double a[3], b[3];
        if (!build_simple_proxy(pose, collider, &proxy))
            return 0;
        capsule_axis_endpoints(&proxy, a, b);
        if (!raycast_capsule_raw(
                origin, dir, a, b, proxy.radius, max_distance, &t, normal, &started))
            return 0;
    } else if (type == RT_COLLIDER3D_TYPE_COMPOUND) {
        int64_t child_count = rt_collider3d_get_child_count_raw(collider);
        int found = 0;
        double best_t = max_distance + 1.0;
        double best_normal[3] = {0.0, 1.0, 0.0};
        int best_started = 0;
        void *best_leaf = NULL;
        for (int64_t i = 0; i < child_count; i++) {
            void *child = rt_collider3d_get_child_raw(collider, i);
            double child_pos[3], child_rot[4], child_scale[3];
            rt_collider_pose child_pose;
            double cur_t, cur_normal[3];
            int cur_started;
            void *cur_leaf = NULL;
            rt_collider3d_get_child_transform_raw(collider, i, child_pos, child_rot, child_scale);
            collider_pose_compose(pose, child_pos, child_rot, child_scale, &child_pose);
            if (raycast_collider_pose(child,
                                      &child_pose,
                                      origin,
                                      dir,
                                      max_distance,
                                      &cur_t,
                                      cur_normal,
                                      &cur_started,
                                      &cur_leaf) &&
                cur_t < best_t) {
                best_t = cur_t;
                vec3_copy(best_normal, cur_normal);
                best_started = cur_started;
                best_leaf = cur_leaf ? cur_leaf : child;
                found = 1;
            }
        }
        if (!found)
            return 0;
        t = best_t;
        vec3_copy(normal, best_normal);
        started = best_started;
        collider = best_leaf ? best_leaf : collider;
    } else if (type == RT_COLLIDER3D_TYPE_CONVEX_HULL || type == RT_COLLIDER3D_TYPE_MESH) {
        rt_mesh3d *mesh = (rt_mesh3d *)rt_collider3d_get_mesh_raw(collider);
        if (!raycast_meshlike_pose_raw(mesh, pose, origin, dir, max_distance, &t, normal, &started))
            return 0;
    } else if (type == RT_COLLIDER3D_TYPE_HEIGHTFIELD) {
        if (!raycast_heightfield_pose_raw(
                collider, pose, origin, dir, max_distance, &t, normal, &started))
            return 0;
    } else {
        double mn[3], mx[3];
        rt_collider3d_compute_world_aabb_raw(
            collider, pose->position, pose->rotation, pose->scale, mn, mx);
        if (!raycast_aabb_raw(origin, dir, mn, mx, max_distance, &t, normal, &started))
            return 0;
    }
    if (!isfinite(t) || t < 0.0 || t > max_distance)
        return 0;
    query_normalize_normal(normal, dir);
    if (out_t)
        *out_t = t;
    if (out_normal)
        vec3_copy(out_normal, normal);
    if (out_started)
        *out_started = started;
    if (out_leaf)
        *out_leaf = collider;
    return 1;
}

/// @brief Ray vs a physics body: dispatches to the actual attached collider
///        shape, recursing through compound children and testing mesh triangles.
/// @return Non-zero on a hit (nearest distance written), 0 otherwise.
static int raycast_body(rt_body3d *body,
                        const double *origin,
                        const double *dir,
                        double max_distance,
                        rt_query_hit3d *out_hit) {
    double t = 0.0;
    double normal[3] = {0.0, 0.0, 0.0};
    int started = 0;
    void *leaf = NULL;
    if (!body || !body->collider)
        return 0;
    {
        rt_collider_pose pose;
        collider_pose_from_body(body, &pose);
        if (!raycast_collider_pose(
                body->collider, &pose, origin, dir, max_distance, &t, normal, &started, &leaf))
            return 0;
    }
    ray_fill_hit(body, t, max_distance, origin, dir, normal, started, out_hit);
    if (out_hit && leaf)
        out_hit->collider = leaf;
    return 1;
}

/// @brief `World3D.Raycast(origin, direction, maxDistance, mask)` — first hit along a ray.
///
/// Uses collider-specific tests for boxes, spheres, capsules, meshes, hulls,
/// compounds, and heightfields. Returns NULL when the direction is zero or
/// `maxDistance <= 0`.
void *rt_world3d_raycast(
    void *obj, void *origin_obj, void *direction_obj, double max_distance, int64_t mask) {
    rt_world3d *w = world3d_checked(obj);
    rt_query_hit3d best_hit = {0};
    int found = 0;
    double origin[3];
    double dir[3];
    double query_min[3], query_max[3], end[3];
    if (!w || !rt_g3d_is_vec3(origin_obj) || !rt_g3d_is_vec3(direction_obj) ||
        !isfinite(max_distance) || max_distance <= 0.0)
        return NULL;
    if (!query_read_vec3(origin_obj, origin) || !query_read_vec3(direction_obj, dir))
        return NULL;
    max_distance = query_sanitize_distance(max_distance);
    if (!query_normalize_direction(dir))
        return NULL;
    end[0] = query_saturate_coord(origin[0] + dir[0] * max_distance);
    end[1] = query_saturate_coord(origin[1] + dir[1] * max_distance);
    end[2] = query_saturate_coord(origin[2] + dir[2] * max_distance);
    for (int axis = 0; axis < 3; ++axis) {
        query_min[axis] = fmin(origin[axis], end[axis]);
        query_max[axis] = fmax(origin[axis], end[axis]);
    }
    int32_t entry_count = world3d_build_query_broadphase(w);
    for (int32_t i = 0; i < (entry_count >= 0 ? entry_count : w->body_count); ++i) {
        rt_body3d *body = entry_count >= 0 ? w->broadphase_entries[i].body : w->bodies[i];
        rt_query_hit3d hit;
        if (entry_count >= 0) {
            if (w->broadphase_entries[i].min[0] > query_max[0])
                break;
            if (!query_entry_overlaps_bounds(&w->broadphase_entries[i], query_min, query_max))
                continue;
        }
        if (!body || !body->collider || !query_mask_matches_body(body, mask))
            continue;
        if (!raycast_body(body, origin, dir, max_distance, &hit))
            continue;
        if (!found || hit.distance < best_hit.distance) {
            best_hit = hit;
            found = 1;
        }
    }
    return found ? physics_hit3d_new(&best_hit) : NULL;
}

/// @brief `World3D.RaycastAll(origin, direction, maxDistance, mask)` — all hits along a ray,
/// sorted.
///
/// Like `Raycast` but doesn't stop at the first hit — every body
/// the ray pierces is recorded. Results come back sorted by distance.
/// Bounded by `PH3D_MAX_QUERY_HITS` (256).
void *rt_world3d_raycast_all(
    void *obj, void *origin_obj, void *direction_obj, double max_distance, int64_t mask) {
    rt_world3d *w = world3d_checked(obj);
    rt_query_hit3d hits[PH3D_MAX_QUERY_HITS];
    double origin[3], dir[3], query_min[3], query_max[3], end[3];
    int32_t hit_count = 0;
    int64_t total_count = 0;
    if (!w || !rt_g3d_is_vec3(origin_obj) || !rt_g3d_is_vec3(direction_obj) ||
        !isfinite(max_distance) || max_distance <= 0.0)
        return NULL;
    if (!query_read_vec3(origin_obj, origin) || !query_read_vec3(direction_obj, dir))
        return NULL;
    max_distance = query_sanitize_distance(max_distance);
    if (!query_normalize_direction(dir))
        return NULL;
    end[0] = query_saturate_coord(origin[0] + dir[0] * max_distance);
    end[1] = query_saturate_coord(origin[1] + dir[1] * max_distance);
    end[2] = query_saturate_coord(origin[2] + dir[2] * max_distance);
    for (int axis = 0; axis < 3; ++axis) {
        query_min[axis] = fmin(origin[axis], end[axis]);
        query_max[axis] = fmax(origin[axis], end[axis]);
    }
    int32_t entry_count = world3d_build_query_broadphase(w);
    for (int32_t i = 0; i < (entry_count >= 0 ? entry_count : w->body_count); ++i) {
        rt_body3d *body = entry_count >= 0 ? w->broadphase_entries[i].body : w->bodies[i];
        rt_query_hit3d hit;
        if (entry_count >= 0) {
            if (w->broadphase_entries[i].min[0] > query_max[0])
                break;
            if (!query_entry_overlaps_bounds(&w->broadphase_entries[i], query_min, query_max))
                continue;
        }
        if (!body || !body->collider || !query_mask_matches_body(body, mask))
            continue;
        if (raycast_body(body, origin, dir, max_distance, &hit)) {
            total_count++;
            hit_count = query_hit_insert_sorted_bounded(hits, hit_count, PH3D_MAX_QUERY_HITS, &hit);
        }
    }
    return physics_hit_list3d_new_ex(
        hits, hit_count, total_count, total_count > (int64_t)hit_count);
}

#else
typedef int rt_physics3d_query_disabled_tu_guard;
#endif /* VIPER_ENABLE_GRAPHICS */
