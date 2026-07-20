//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
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
int query_hit_insert_sorted_bounded(rt_query_hit3d *hits,
                                    int32_t count,
                                    int32_t capacity,
                                    const rt_query_hit3d *hit);
int32_t world3d_build_query_broadphase(rt_world3d *w);

// Lazily allocated world-owned hit scratch buffer sized to w->max_query_hits
// (configurable via Physics3DWorld.SetMaxQueryHits). NULL on OOM.
rt_query_hit3d *world3d_query_hits_scratch(rt_world3d *w);
int query_entry_overlaps_bounds(const ph3d_broadphase_entry *entry,
                                const double *query_min,
                                const double *query_max);

/// @brief Ensure body-local CCD can sweep without allocating after integration.
/// @details Lazily creates the world's reusable sphere collider before the
///   Step transaction mutates any body. Subsequent radius changes reset that
///   collider in place and therefore cannot fail mid-transaction.
/// @param w Borrowed world that owns the reusable query collider.
/// @return `1` when the collider is ready, otherwise `0` after allocation failure.
int world3d_ccd_prepare_sweep_collider(rt_world3d *w);

/// @brief Sweep one conservative CCD proxy and return the earliest blocking hit.
/// @details Tests static/kinematic bodies plus eligible dynamic bodies using
///   relative displacement over @p sub_dt. Triggers and @p ignore_body are
///   skipped. The caller derives @p center and @p radius from the complete
///   moving collider bounds, so this primitive cannot omit a collider corner.
/// @param out_hit Optional raw target/collider/contact-point snapshot.
/// @return `1` for a separated time of impact, otherwise `0`.
int world3d_ccd_sweep_sphere_raw(rt_world3d *w,
                                 const double *center,
                                 double radius,
                                 const double *delta,
                                 const rt_body3d *ignore_body,
                                 double sub_dt,
                                 double *out_t,
                                 double *out_normal,
                                 rt_query_hit3d *out_hit);

/// @brief Record transient trigger crossings along one local CCD segment.
/// @details Sweeps the same conservative proxy used by blocking CCD, creates a
///   trigger contact for every crossed trigger, and appends it to the world's
///   frame-wide unique contact set. This preserves enter/exit behavior without
///   globally subdividing and re-solving the whole world.
/// @return `1` on success, or `0` if the frame-contact buffer cannot grow.
int world3d_ccd_record_trigger_crossings(rt_world3d *w,
                                         const double *center,
                                         double radius,
                                         const double *delta,
                                         rt_body3d *moving_body,
                                         double segment_dt);

// Raw raycast primitives (defined in rt_physics3d_raycast.c, used by sweep too).
int raycast_sphere_raw(const double *origin,
                       const double *dir,
                       const double *center,
                       double radius,
                       double max_distance,
                       double *out_t,
                       double *out_normal,
                       int *out_started);
int raycast_box_pose_raw(void *box_collider,
                         const rt_collider_pose *pose,
                         const double *origin,
                         const double *dir,
                         double max_distance,
                         double expand_radius,
                         double *out_t,
                         double *out_normal,
                         int *out_started);
