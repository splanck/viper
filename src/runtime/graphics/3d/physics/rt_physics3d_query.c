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
#include "rt_physics3d_query_internal.h"
#include "rt_raycast3d.h"
#include "rt_trap.h"

#include <float.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>


#define PH3D_QUERY_DISTANCE_MAX 1000000000.0

/// @brief Clamp query coordinates to the same broad finite range used by body state.
double query_saturate_coord(double value) {
    if (!isfinite(value))
        return 0.0;
    if (value > PH3D_QUERY_COORD_ABS_MAX)
        return PH3D_QUERY_COORD_ABS_MAX;
    if (value < -PH3D_QUERY_COORD_ABS_MAX)
        return -PH3D_QUERY_COORD_ABS_MAX;
    return value;
}

/// @brief Read a public Vec3 argument, rejecting NaN/Inf and capping extreme finite values.
int query_read_vec3(void *obj, double out[3]) {
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
double query_sanitize_distance(double value) {
    if (!isfinite(value) || value <= 0.0)
        return 0.0;
    return value > PH3D_QUERY_DISTANCE_MAX ? PH3D_QUERY_DISTANCE_MAX : value;
}

/// @brief Normalize a direction vector in place, returning 0 for invalid/zero vectors.
int query_normalize_direction(double dir[3]) {
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
void query_normalize_normal(double normal[3], const double *fallback_dir) {
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
int query_sanitize_hit(rt_query_hit3d *hit, double max_distance, const double *fallback_dir) {
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
int query_hit_insert_sorted_bounded(rt_query_hit3d *hits,
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
int32_t world3d_build_query_broadphase(rt_world3d *w) {
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
int query_entry_overlaps_bounds(const ph3d_broadphase_entry *entry,
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
    rt_body3d zero = {0};
    *body = zero;
    quat_identity(body->orientation);
    vec3_set(body->scale, 1.0, 1.0, 1.0);
    body->motion_mode = PH3D_MODE_STATIC;
    body->collider = collider;
    if (position) {
        vec3_copy(body->position, position);
        ph3d_vec3_sanitize_state(body->position);
    }
}

/// @brief Borrow the world's reusable sphere collider, creating it on first use.
/// @details Query bodies only need a collider long enough for one query operation. Reusing one
///          world-owned sphere collider removes per-query heap churn while preserving the existing
///          narrow-phase API that expects a Collider3D handle.
/// @param w World that owns the scratch collider.
/// @param radius Sanitized sphere radius for this query.
/// @return Collider3D sphere handle, or NULL on allocation failure.
static void *world3d_query_sphere_collider(rt_world3d *w, double radius) {
    if (!w)
        return NULL;
    if (!w->query_sphere_collider) {
        w->query_sphere_collider = rt_collider3d_new_sphere(radius);
    } else {
        rt_collider3d_reset_sphere_raw(w->query_sphere_collider, radius);
    }
    return w->query_sphere_collider;
}

/// @brief Borrow the world's reusable box collider, creating it on first use.
/// @details The collider dimensions are reset in place before every AABB overlap query, avoiding
///          allocation/free pairs for the query shape.
/// @param w World that owns the scratch collider.
/// @param half Sanitized xyz half-extents.
/// @return Collider3D box handle, or NULL on allocation failure.
static void *world3d_query_box_collider(rt_world3d *w, const double half[3]) {
    if (!w || !half)
        return NULL;
    if (!w->query_box_collider) {
        w->query_box_collider = rt_collider3d_new_box(half[0], half[1], half[2]);
    } else {
        rt_collider3d_reset_box_raw(w->query_box_collider, half[0], half[1], half[2]);
    }
    return w->query_box_collider;
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
        rt_query_hit3d zero = {0};
        *out_hit = zero;
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
    {
        rt_query_hit3d zero = {0};
        *out_hit = zero;
    }
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
    out_hit->point[0] = query_saturate_coord(start_center[0] + dir[0] * out_hit->distance -
                                             out_hit->normal[0] * radius);
    out_hit->point[1] = query_saturate_coord(start_center[1] + dir[1] * out_hit->distance -
                                             out_hit->normal[1] * radius);
    out_hit->point[2] = query_saturate_coord(start_center[2] + dir[2] * out_hit->distance -
                                             out_hit->normal[2] * radius);
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
    if (!start_center || !delta || !body3d_has_collision_geometry(other) || max_distance <= 1e-12)
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
                                      void *sphere_collider,
                                      const double *delta,
                                      rt_body3d *other,
                                      double max_distance,
                                      rt_query_hit3d *out_hit) {
    double axis[3];
    double axis_len;
    int samples;
    int hit = 0;
    rt_query_hit3d best = {0};
    if (!a || !b || !sphere_collider || !delta || !body3d_has_collision_geometry(other))
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
    rt_query_hit3d *hits = world3d_query_hits_scratch(w);
    int32_t hit_capacity = w ? w->max_query_hits : 0;
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
    sphere_collider = world3d_query_sphere_collider(w, radius);
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
            if (hits && hit_count < hit_capacity)
                hits[hit_count++] = hit;
        }
    }
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
    rt_query_hit3d *hits = world3d_query_hits_scratch(w);
    int32_t hit_capacity = w ? w->max_query_hits : 0;
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
    box_collider = world3d_query_box_collider(w, half);
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
            if (hits && hit_count < hit_capacity)
                hits[hit_count++] = hit;
        }
    }
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
    sphere_collider = world3d_query_sphere_collider(w, radius);
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
    return found ? physics_hit3d_new(&best_hit) : NULL;
}

rt_query_hit3d *world3d_query_hits_scratch(rt_world3d *w) {
    if (!w)
        return NULL;
    if (w->max_query_hits < PH3D_QUERY_HITS_MIN)
        w->max_query_hits = PH3D_QUERY_HITS_MIN;
    if (w->max_query_hits > PH3D_QUERY_HITS_MAX)
        w->max_query_hits = PH3D_QUERY_HITS_MAX;
    if (!w->query_hits_scratch) {
        w->query_hits_scratch = malloc((size_t)w->max_query_hits * sizeof(rt_query_hit3d));
    }
    return (rt_query_hit3d *)w->query_hits_scratch;
}

int world3d_ccd_sweep_sphere_raw(rt_world3d *w,
                                 const double *center,
                                 double radius,
                                 const double *delta,
                                 const rt_body3d *ignore_body,
                                 double *out_t,
                                 double *out_normal) {
    rt_query_hit3d best_hit = {0};
    int found = 0;
    double max_distance;
    void *sphere_collider;
    if (!w || !center || !delta || !isfinite(radius) || radius <= 0.0)
        return 0;
    max_distance = vec3_len(delta);
    if (!isfinite(max_distance) || max_distance <= 1e-12)
        return 0;

    sphere_collider = world3d_query_sphere_collider(w, radius);
    if (!sphere_collider)
        return 0;

    /* Direct body iteration: CCD targets are the (few, large) static and
     * kinematic bodies whose poses are stable for the whole step, so the
     * cached query broadphase — which is invalidated as dynamic bodies
     * integrate — is deliberately not used here. */
    for (int32_t i = 0; i < w->body_count; ++i) {
        rt_body3d *body = w->bodies[i];
        rt_query_hit3d hit;
        if (!body || body == ignore_body)
            continue;
        if (body->motion_mode == PH3D_MODE_DYNAMIC)
            continue;
        if (!body3d_has_collision_geometry(body))
            continue;
        /* Honor the mutual layer/mask contract (same rule the solver applies). */
        if (ignore_body && (!(ignore_body->collision_layer & body->collision_mask) ||
                            !(body->collision_layer & ignore_body->collision_mask)))
            continue;
        /* Triggers report overlaps but never block motion. */
        if (body->is_trigger)
            continue;
        if (!sweep_sphere_against_body(
                sphere_collider, center, radius, delta, body, max_distance, &hit))
            continue;
        if (!query_sanitize_hit(&hit, max_distance, delta))
            continue;
        /* Persistent contacts belong to the impulse solver: a body resting on
         * (or rolling along) a surface starts every substep already touching
         * it, and clipping that sweep at t=0 would freeze tangential motion
         * entirely. CCD only guards surfaces the body is separated from at
         * substep start — which is exactly the anti-tunneling case. */
        if (hit.started_penetrating)
            continue;
        if (!found || hit.distance < best_hit.distance) {
            best_hit = hit;
            found = 1;
        }
    }

    if (!found)
        return 0;
    if (out_t)
        *out_t = best_hit.distance / max_distance;
    if (out_normal) {
        out_normal[0] = best_hit.normal[0];
        out_normal[1] = best_hit.normal[1];
        out_normal[2] = best_hit.normal[2];
    }
    return 1;
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
    void *sphere_collider;
    if (!w || !rt_g3d_is_vec3(a_obj) || !rt_g3d_is_vec3(b_obj) || !rt_g3d_is_vec3(delta_obj) ||
        !isfinite(radius) || radius < 0.0)
        return NULL;
    if (!query_read_vec3(a_obj, a) || !query_read_vec3(b_obj, b) ||
        !query_read_vec3(delta_obj, delta))
        return NULL;
    radius = query_sanitize_distance(radius);
    max_distance = query_cap_vector_length(delta);
    sphere_collider = world3d_query_sphere_collider(w, radius);
    if (!sphere_collider)
        return NULL;
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
        if (!sweep_capsule_against_body(
                a, b, radius, sphere_collider, delta, body, max_distance, &hit))
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

#else
typedef int rt_physics3d_query_core_disabled_tu_guard;
#endif /* VIPER_ENABLE_GRAPHICS */
