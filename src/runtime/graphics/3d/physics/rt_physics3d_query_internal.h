//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/physics/rt_physics3d_query_internal.h
// Purpose: Shared spatial-query helpers for the 3D physics queries, split
//          between rt_physics3d_query.c (sweep/overlap) and
//          rt_physics3d_raycast.c (ray casting against shapes + mesh BVH).
//
// Key invariants:
//   - Engine-internal; included only by the physics/ query translation units.
//   - Coordinate/distance sanitizers clamp to PH3D_QUERY_COORD_ABS_MAX so a
//     hostile transform can't push the broadphase into non-finite territory.
//
// Ownership/Lifetime:
//   - Helpers borrow caller-owned world/hit buffers; no allocation here.
//
// Links: src/runtime/graphics/3d/physics/rt_physics3d_query.c (sweep/overlap),
//        src/runtime/graphics/3d/physics/rt_physics3d_raycast.c (raycast),
//        src/runtime/graphics/3d/physics/rt_physics3d_internal.h (shared types)
//
//===----------------------------------------------------------------------===//
#pragma once

#include "rt_physics3d.h"
#include "rt_physics3d_internal.h"

#include <stdint.h>

#define PH3D_QUERY_COORD_ABS_MAX 1000000000000.0

double query_saturate_coord(double value);
int query_read_vec3(void *obj, double out[3]);
double query_sanitize_distance(double value);
int query_normalize_direction(double dir[3]);
void query_normalize_normal(double normal[3], const double *fallback_dir);
int query_sanitize_hit(rt_query_hit3d *hit, double max_distance, const double *fallback_dir);
int query_hit_insert_sorted_bounded(
    rt_query_hit3d *hits, int32_t count, int32_t capacity, const rt_query_hit3d *hit);
int32_t world3d_build_query_broadphase(rt_world3d *w);

// Lazily allocated world-owned hit scratch buffer sized to w->max_query_hits
// (configurable via Physics3DWorld.SetMaxQueryHits). NULL on OOM.
rt_query_hit3d *world3d_query_hits_scratch(rt_world3d *w);
int query_entry_overlaps_bounds(
    const ph3d_broadphase_entry *entry, const double *query_min, const double *query_max);

// Allocation-free swept-CCD probe (defined in rt_physics3d_query.c, consumed
// by the step loop's time-of-impact pass in rt_physics3d_world.inc). Sweeps a
// bounding sphere from `center` along `delta` against static and kinematic
// bodies, and — when either side opted into CCD — against dynamic bodies using
// the RELATIVE displacement over `sub_dt` (so fast projectiles cannot tunnel
// through fast targets), skipping `ignore_body` (the moving body itself).
// On hit returns 1 and writes the earliest time-of-impact fraction (0..1 of
// the swept motion) and the surface normal.
int world3d_ccd_sweep_sphere_raw(rt_world3d *w,
                                 const double *center,
                                 double radius,
                                 const double *delta,
                                 const rt_body3d *ignore_body,
                                 double sub_dt,
                                 double *out_t,
                                 double *out_normal);

// Raw raycast primitives (defined in rt_physics3d_raycast.c, used by sweep too).
int raycast_sphere_raw(const double *origin, const double *dir, const double *center, double radius,
                       double max_distance, double *out_t, double *out_normal, int *out_started);
int raycast_box_pose_raw(void *box_collider, const rt_collider_pose *pose, const double *origin,
                         const double *dir, double max_distance, double expand_radius, double *out_t,
                         double *out_normal, int *out_started);
