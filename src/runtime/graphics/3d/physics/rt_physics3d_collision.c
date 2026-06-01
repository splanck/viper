//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/physics/rt_physics3d_collision.c
// Purpose: Narrow-phase collision detection and contact-manifold generation for
//   the Physics3D runtime — primitive tests (sphere/box/capsule/AABB), OBB
//   clipping, GJK/EPA convex-hull tests, mesh/heightfield/compound colliders,
//   the top-level test_collider_pair dispatch, and contact de-dup. Split out of
//   rt_physics3d.c; shares core types via rt_physics3d_internal.h.
//
// Key invariants:
//   - test_collision/test_collider_pair return a normal pointing from body A to
//     body B with a positive penetration depth, plus the contributing leaves.
//   - Manifolds carry up to PH3D_MAX_MANIFOLD_POINTS points; OBB pairs clip a
//     reference face against the incident face to recover a stable manifold.
//   - Mesh narrow-phase traverses the shared rt_physics_mesh_bvh_node BVH built
//     by mesh_physics_bvh_rebuild (rt_physics3d_query.c).
//
// Links: rt_physics3d_internal.h, rt_physics3d.c
//
//===----------------------------------------------------------------------===//

#ifdef VIPER_ENABLE_GRAPHICS

#include "rt_collider3d.h"
#include "rt_graphics3d_ids.h"
#include "rt_physics3d.h"
#include "rt_physics3d_internal.h"
#include "rt_trap.h"

#include <float.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// Forward declarations for collision-internal helpers (defined below, but
// referenced earlier by the OBB/manifold helpers).
/// @brief Compute scaled half-extents for a posed box collider.
static void box_scaled_half_extents(void *collider,
                                    const rt_collider_pose *pose,
                                    double *half_extents);
/// @brief Derive the world-space orthonormal axes for a pose rotation.
static void pose_rotation_axes(const rt_collider_pose *pose, double axes[3][3]);

static int test_simple_collision(const rt_body3d *a,
                                 const rt_body3d *b,
                                 double *normal,
                                 double *depth);

/// @brief Build a stack-allocated dummy sphere body for transient queries.
///
/// Used so that overlap / shapecast routines can reuse the body-vs-body
/// collision functions without needing to register a permanent body.
/// Only fields the collision routines read are populated.
static void make_temp_sphere(rt_body3d *out, const double *center, double radius) {
    memset(out, 0, sizeof(*out));
    out->shape = PH3D_SHAPE_SPHERE;
    out->position[0] = center[0];
    out->position[1] = center[1];
    out->position[2] = center[2];
    out->radius = radius;
}

/// @brief Return the two world-space endpoints of a capsule's oriented axis segment.
///
/// Capsules are authored along local Y with the body position at the center.
/// The body orientation rotates that local axis before collision tests use it.
void capsule_axis_endpoints(const rt_body3d *b, double *a, double *c) {
    double half_axis = fmax(b->height * 0.5 - b->radius, 0.0);
    double local_axis[3] = {0.0, half_axis, 0.0};
    double axis[3];
    quat_rotate_vec3(b->orientation, local_axis, axis);
    vec3_set(a, b->position[0] - axis[0], b->position[1] - axis[1], b->position[2] - axis[2]);
    vec3_set(c, b->position[0] + axis[0], b->position[1] + axis[1], b->position[2] + axis[2]);
}

/// @brief Find the point on segment [a, b] closest to @p point.
/// @details Projects @p point onto the line through a and b, clamps the
///   parameter t to [0, 1] to stay within the segment, then evaluates
///   the position.  The degenerate case (a == b, denom ≈ 0) returns a.
/// @param a        Segment start endpoint (world space).
/// @param b        Segment end endpoint (world space).
/// @param point    Query point (world space).
/// @param closest  Output: closest point on the segment to @p point.
static void closest_point_on_segment(const double *a,
                                     const double *b,
                                     const double *point,
                                     double *closest) {
    double ab[3], ap[3];
    vec3_sub(b, a, ab);
    vec3_sub(point, a, ap);
    double denom = vec3_dot(ab, ab);
    double t = denom > 1e-18 ? vec3_dot(ap, ab) / denom : 0.0;
    t = clampd(t, 0.0, 1.0);
    closest[0] = a[0] + ab[0] * t;
    closest[1] = a[1] + ab[1] * t;
    closest[2] = a[2] + ab[2] * t;
}

/// @brief Find the pair of closest points between two line segments in 3D.
/// @details Implements the GDC/Ericson Real-Time Collision Detection segment–segment
///   closest-point algorithm.  Handles degenerate cases where either or both segments
///   collapse to a point (length ≤ eps).  The result is the world-space pair (c1, c2)
///   such that |c1 - c2| is minimized over the two segments.  Used in capsule–capsule
///   and capsule–box narrow-phase collision to compute the axis-to-axis gap.
/// @param p1  Start of segment 1.
/// @param q1  End of segment 1.
/// @param p2  Start of segment 2.
/// @param q2  End of segment 2.
/// @param c1  Output: closest point on segment 1.
/// @param c2  Output: closest point on segment 2.
static void closest_points_on_segments(const double *p1,
                                       const double *q1,
                                       const double *p2,
                                       const double *q2,
                                       double *c1,
                                       double *c2) {
    const double eps = 1e-18;
    double d1[3], d2[3], r[3];
    vec3_sub(q1, p1, d1);
    vec3_sub(q2, p2, d2);
    vec3_sub(p1, p2, r);
    double a = vec3_dot(d1, d1);
    double e = vec3_dot(d2, d2);
    double f = vec3_dot(d2, r);
    double s, t;

    if (a <= eps && e <= eps) {
        vec3_copy(c1, p1);
        vec3_copy(c2, p2);
        return;
    }
    if (a <= eps) {
        s = 0.0;
        t = clampd(f / e, 0.0, 1.0);
    } else {
        double c = vec3_dot(d1, r);
        if (e <= eps) {
            t = 0.0;
            s = clampd(-c / a, 0.0, 1.0);
        } else {
            double b = vec3_dot(d1, d2);
            double denom = a * e - b * b;
            if (denom != 0.0)
                s = clampd((b * f - c * e) / denom, 0.0, 1.0);
            else
                s = 0.0;
            t = (b * s + f) / e;
            if (t < 0.0) {
                t = 0.0;
                s = clampd(-c / a, 0.0, 1.0);
            } else if (t > 1.0) {
                t = 1.0;
                s = clampd((b - c) / a, 0.0, 1.0);
            }
        }
    }
    c1[0] = p1[0] + d1[0] * s;
    c1[1] = p1[1] + d1[1] * s;
    c1[2] = p1[2] + d1[2] * s;
    c2[0] = p2[0] + d2[0] * t;
    c2[1] = p2[1] + d2[1] * t;
    c2[2] = p2[2] + d2[2] * t;
}

/// @brief Compute the squared distance from @p point to the AABB defined by [mn, mx].
/// @details Projects the point onto the closest face/edge/corner of the box using
///   per-axis clamping, then returns the squared length of the gap vector.  Returns
///   0 for points inside the AABB.  Used by the segment-to-AABB search to evaluate
///   candidate t parameters quickly without a sqrt.
/// @param point  Query point (world space, double[3]).
/// @param mn     AABB minimum corner (world space, double[3]).
/// @param mx     AABB maximum corner (world space, double[3]).
/// @return       Squared Euclidean distance from @p point to the nearest AABB surface.
static double point_aabb_distance_sq(const double *point, const double *mn, const double *mx) {
    double q[3] = {clampd(point[0], mn[0], mx[0]),
                   clampd(point[1], mn[1], mx[1]),
                   clampd(point[2], mn[2], mx[2])};
    double delta[3];
    vec3_sub(point, q, delta);
    return vec3_len_sq(delta);
}

/// @brief Find the point on segment [a, c] that is closest to the AABB [mn, mx].
/// @details Uses a multi-probe sampling strategy: evaluates point_aabb_distance_sq
///   at t = 0, 1, and at each of the six per-axis candidate t values derived from
///   the AABB face planes (where the segment crosses each slab boundary).  The
///   sample with the smallest squared distance wins.  This avoids a full analytic
///   segment–AABB solver while remaining robust for the capsule–box narrow phase.
/// @param a           Segment start endpoint (world space, double[3]).
/// @param c           Segment end endpoint (world space, double[3]).
/// @param mn          AABB minimum corner (world space, double[3]).
/// @param mx          AABB maximum corner (world space, double[3]).
/// @param closest_axis  Output: best-candidate point on the segment (world space, double[3]).
static void closest_point_segment_to_aabb(
    const double *a, const double *c, const double *mn, const double *mx, double *closest_axis) {
    double d[3];
    double best_t = 0.0;
    double best_dist = 1e300;
    vec3_sub(c, a, d);

#define PH3D_EVAL_SEG_AABB_T(t_expr)                                                               \
    do {                                                                                           \
        double eval_t = clampd((t_expr), 0.0, 1.0);                                                \
        double p_eval[3] = {a[0] + d[0] * eval_t, a[1] + d[1] * eval_t, a[2] + d[2] * eval_t};     \
        double dist_eval = point_aabb_distance_sq(p_eval, mn, mx);                                 \
        if (dist_eval < best_dist) {                                                               \
            best_dist = dist_eval;                                                                 \
            best_t = eval_t;                                                                       \
        }                                                                                          \
    } while (0)

    PH3D_EVAL_SEG_AABB_T(0.0);
    PH3D_EVAL_SEG_AABB_T(1.0);
    {
        double center[3] = {
            (mn[0] + mx[0]) * 0.5,
            (mn[1] + mx[1]) * 0.5,
            (mn[2] + mx[2]) * 0.5,
        };
        double ac[3];
        vec3_sub(center, a, ac);
        double len_sq = vec3_len_sq(d);
        if (len_sq > 1e-18)
            PH3D_EVAL_SEG_AABB_T(vec3_dot(ac, d) / len_sq);
    }
    for (int axis = 0; axis < 3; axis++) {
        if (fabs(d[axis]) > 1e-18) {
            PH3D_EVAL_SEG_AABB_T((mn[axis] - a[axis]) / d[axis]);
            PH3D_EVAL_SEG_AABB_T((mx[axis] - a[axis]) / d[axis]);
        }
    }
    {
        double lo = 0.0;
        double hi = 1.0;
        for (int iter = 0; iter < 24; iter++) {
            double m1 = lo + (hi - lo) / 3.0;
            double m2 = hi - (hi - lo) / 3.0;
            double p1[3] = {a[0] + d[0] * m1, a[1] + d[1] * m1, a[2] + d[2] * m1};
            double p2[3] = {a[0] + d[0] * m2, a[1] + d[1] * m2, a[2] + d[2] * m2};
            if (point_aabb_distance_sq(p1, mn, mx) < point_aabb_distance_sq(p2, mn, mx))
                hi = m2;
            else
                lo = m1;
        }
        PH3D_EVAL_SEG_AABB_T((lo + hi) * 0.5);
    }

#undef PH3D_EVAL_SEG_AABB_T

    closest_axis[0] = a[0] + d[0] * best_t;
    closest_axis[1] = a[1] + d[1] * best_t;
    closest_axis[2] = a[2] + d[2] * best_t;
}

/// @brief Project a point onto the capsule's axis segment.
///
/// Used as the first step in capsule-vs-anything distance computations.
static void closest_point_capsule_axis_to_point(const rt_body3d *cap,
                                                const double *point,
                                                double *closest) {
    double a[3], c[3];
    capsule_axis_endpoints(cap, a, c);
    closest_point_on_segment(a, c, point, closest);
}

/// @brief Find the closest point pair on two capsule axes.
///
/// Handles arbitrary capsule orientations by solving segment-vs-segment.
static void closest_points_capsule_axes(const rt_body3d *a,
                                        const rt_body3d *b,
                                        double *closest_a,
                                        double *closest_b) {
    double aa[3], ac[3], ba[3], bc[3];
    capsule_axis_endpoints(a, aa, ac);
    capsule_axis_endpoints(b, ba, bc);
    closest_points_on_segments(aa, ac, ba, bc, closest_a, closest_b);
}

/// @brief Find the point on a capsule's axis closest to a box.
///
/// Used as the first step of capsule-vs-AABB collision.
static void closest_point_capsule_axis_to_aabb(const rt_body3d *cap,
                                               const rt_body3d *box,
                                               double *closest_axis) {
    double mn[3], mx[3], a[3], c[3];
    body_aabb(box, mn, mx);
    capsule_axis_endpoints(cap, a, c);
    closest_point_segment_to_aabb(a, c, mn, mx, closest_axis);
}

/// @brief Layer/mask filter for one-sided queries (raycast, overlap).
///
/// Bidirectional layer/mask filtering applies for body-vs-body collision
/// (`bodies_can_collide`); queries are unidirectional, so we just check
/// the body's layer against the query's mask. A zero mask matches nothing,
/// which keeps `LayerMask.None()` consistent across high-level queries.
int query_mask_matches_body(const rt_body3d *body, int64_t mask) {
    if (!body)
        return 0;
    if (mask == 0)
        return 0;
    return (body->collision_layer & mask) != 0;
}

/// @brief Raw AABB-vs-AABB overlap test (no shape interpretation).
///
/// SAT on the axis-aligned axes. Returns true if neither axis fully
/// separates the two boxes. This is the broad-phase primitive — the
/// narrow-phase shape tests run only on pairs that pass this check.
int aabb_overlap_raw(const double *amn, const double *amx, const double *bmn, const double *bmx) {
    return !(amn[0] > bmx[0] || amx[0] < bmn[0] || amn[1] > bmx[1] || amx[1] < bmn[1] ||
             amn[2] > bmx[2] || amx[2] < bmn[2]);
}

/// @brief Compute the AABB swept by translating `[start_min, start_max]` by `delta`.
///
/// Used by CCD to make a single AABB that bounds an entire integration
/// step's motion. If this swept box doesn't overlap a candidate body's
/// AABB, the bodies cannot collide during the step.
void swept_aabb_from_points(const double *start_min,
                            const double *start_max,
                            const double *delta,
                            double *swept_min,
                            double *swept_max) {
    double end_min[3] = {start_min[0] + delta[0], start_min[1] + delta[1], start_min[2] + delta[2]};
    double end_max[3] = {start_max[0] + delta[0], start_max[1] + delta[1], start_max[2] + delta[2]};
    swept_min[0] = start_min[0] < end_min[0] ? start_min[0] : end_min[0];
    swept_min[1] = start_min[1] < end_min[1] ? start_min[1] : end_min[1];
    swept_min[2] = start_min[2] < end_min[2] ? start_min[2] : end_min[2];
    swept_max[0] = start_max[0] > end_max[0] ? start_max[0] : end_max[0];
    swept_max[1] = start_max[1] > end_max[1] ? start_max[1] : end_max[1];
    swept_max[2] = start_max[2] > end_max[2] ? start_max[2] : end_max[2];
}

/// @brief Bytewise contact copy. Used to snapshot frame-N contacts for
///        the frame-N+1 enter/stay/exit event diff.
void contact_snapshot_copy(rt_contact3d *dst, const rt_contact3d *src) {
    if (!dst || !src)
        return;
    memcpy(dst, src, sizeof(*dst));
}

/// @brief Whether the body's orientation quaternion is (within 1e-9) the identity.
/// @details Lets axis-aligned collision paths skip rotation math for un-rotated bodies.
static int body3d_orientation_is_identity(const rt_body3d *body) {
    if (!body)
        return 0;
    return fabs(body->orientation[0]) < 1e-9 && fabs(body->orientation[1]) < 1e-9 &&
           fabs(body->orientation[2]) < 1e-9 && fabs(body->orientation[3] - 1.0) < 1e-9;
}

/// @brief Write a manifold point (position, normal, separation) at @p index (bounds-checked).
static void contact3d_set_manifold_point(rt_contact3d *contact,
                                         int32_t index,
                                         const double *point,
                                         const double *normal,
                                         double separation) {
    if (!contact || !point || !normal || index < 0 || index >= PH3D_MAX_MANIFOLD_POINTS)
        return;
    vec3_copy(contact->points[index], point);
    vec3_copy(contact->normals[index], normal);
    contact->separations[index] = separation;
}

/// @brief Whether @p point already appears in the manifold (within 1e-6 distance) — a dedup guard.
static int contact3d_point_exists(const rt_contact3d *contact, const double *point) {
    if (!contact || !point)
        return 1;
    for (int32_t i = 0; i < contact->contact_count; i++) {
        double dx = contact->points[i][0] - point[0];
        double dy = contact->points[i][1] - point[1];
        double dz = contact->points[i][2] - point[2];
        if (dx * dx + dy * dy + dz * dz < 1e-12)
            return 1;
    }
    return 0;
}

/// @brief Reset the contact manifold to a single point (position, normal, separation).
void contact3d_init_single_point(rt_contact3d *contact,
                                 const double *point,
                                 const double *normal,
                                 double separation) {
    if (!contact || !point || !normal)
        return;
    contact->contact_count = 1;
    contact3d_set_manifold_point(contact, 0, point, normal, separation);
}

/// @brief Add a manifold point if it is finite, non-duplicate, and the manifold isn't full.
static void contact3d_try_add_manifold_point(rt_contact3d *contact,
                                             const double *point,
                                             const double *normal,
                                             double separation) {
    if (!contact || !point || !normal || !ph3d_vec3_all_finite(point) ||
        !ph3d_vec3_all_finite(normal))
        return;
    if (contact->contact_count >= PH3D_MAX_MANIFOLD_POINTS)
        return;
    if (contact3d_point_exists(contact, point))
        return;
    contact3d_set_manifold_point(contact, contact->contact_count++, point, normal, separation);
}

/// @brief Promote a single-point box/box contact to a multi-point manifold over the AABB overlap.
/// @details Samples the corners of the two bodies' overlapping AABB region as extra contact points,
///          giving the solver a stable contact patch (resting boxes don't wobble on one point).
void contact3d_expand_aabb_manifold(rt_contact3d *contact, const rt_body3d *a, const rt_body3d *b) {
    double a_min[3];
    double a_max[3];
    double b_min[3];
    double b_max[3];
    double overlap_min[3];
    double overlap_max[3];
    int axis = 0;
    int u;
    int v;
    if (!contact || !a || !b)
        return;
    if (a->shape != PH3D_SHAPE_AABB || b->shape != PH3D_SHAPE_AABB)
        return;
    if (!body3d_orientation_is_identity(a) || !body3d_orientation_is_identity(b))
        return;
    if (fabs(contact->normal[1]) > fabs(contact->normal[axis]))
        axis = 1;
    if (fabs(contact->normal[2]) > fabs(contact->normal[axis]))
        axis = 2;
    u = (axis + 1) % 3;
    v = (axis + 2) % 3;
    for (int i = 0; i < 3; i++) {
        a_min[i] = a->position[i] - a->half_extents[i];
        a_max[i] = a->position[i] + a->half_extents[i];
        b_min[i] = b->position[i] - b->half_extents[i];
        b_max[i] = b->position[i] + b->half_extents[i];
        overlap_min[i] = a_min[i] > b_min[i] ? a_min[i] : b_min[i];
        overlap_max[i] = a_max[i] < b_max[i] ? a_max[i] : b_max[i];
        if (!isfinite(overlap_min[i]) || !isfinite(overlap_max[i]) ||
            overlap_min[i] > overlap_max[i])
            return;
    }

    double p[3] = {contact->point[0], contact->point[1], contact->point[2]};
    p[axis] = contact->point[axis];
    p[u] = overlap_min[u];
    p[v] = overlap_min[v];
    contact3d_try_add_manifold_point(contact, p, contact->normal, contact->separation);
    p[u] = overlap_max[u];
    p[v] = overlap_min[v];
    contact3d_try_add_manifold_point(contact, p, contact->normal, contact->separation);
    p[u] = overlap_min[u];
    p[v] = overlap_max[v];
    contact3d_try_add_manifold_point(contact, p, contact->normal, contact->separation);
    p[u] = overlap_max[u];
    p[v] = overlap_max[v];
    contact3d_try_add_manifold_point(contact, p, contact->normal, contact->separation);
}

#define PH3D_OBB_CLIP_MAX_POINTS 16

typedef struct {
    double center[3];
    double axes[3][3];
    double half_extents[3];
} ph3d_obb_box;

typedef struct {
    double point[3];
    double separation;
} ph3d_obb_contact_candidate;

/// @brief Build an oriented-bounding-box description (center, world axes, scaled half-extents)
///        from a box collider and its world pose.
/// @return 1 if @p collider is a box and the resulting center/extents are finite; 0 otherwise.
static int ph3d_make_obb_box(void *collider, const rt_collider_pose *pose, ph3d_obb_box *out) {
    if (!collider || !pose || !out || rt_collider3d_get_type(collider) != RT_COLLIDER3D_TYPE_BOX)
        return 0;
    vec3_copy(out->center, pose->position);
    pose_rotation_axes(pose, out->axes);
    box_scaled_half_extents(collider, pose, out->half_extents);
    return ph3d_vec3_all_finite(out->center) && ph3d_vec3_all_finite(out->half_extents);
}

/// @brief Pick the box local axis whose face normal is most aligned with @p target_normal.
/// @details Scans the three local axes and selects the one with the largest |dot| against
///          @p target_normal; the sign chooses which of the two opposing faces points toward it.
/// @param axis_out Receives the chosen axis index (0..2).
/// @param sign_out Receives +1/-1 selecting the positive/negative face along that axis.
/// @param alignment_out Receives the alignment magnitude (|dot|, in [0,1]).
/// @return 1 if a positively-aligned face was found; 0 if @p box or @p target_normal is NULL.
static int ph3d_obb_best_face_axis(const ph3d_obb_box *box,
                                   const double *target_normal,
                                   int *axis_out,
                                   double *sign_out,
                                   double *alignment_out) {
    double best_alignment = -1.0;
    int best_axis = 0;
    double best_sign = 1.0;
    if (!box || !target_normal)
        return 0;
    for (int axis = 0; axis < 3; ++axis) {
        double d = vec3_dot(target_normal, box->axes[axis]);
        double a = fabs(d);
        if (a > best_alignment) {
            best_alignment = a;
            best_axis = axis;
            best_sign = d >= 0.0 ? 1.0 : -1.0;
        }
    }
    if (axis_out)
        *axis_out = best_axis;
    if (sign_out)
        *sign_out = best_sign;
    if (alignment_out)
        *alignment_out = best_alignment;
    return best_alignment > 0.0;
}

/// @brief Compute the four world-space corner vertices of one OBB face.
/// @param face_axis Axis (0..2) whose face is selected.
/// @param face_sign +1/-1 picking the positive/negative face along @p face_axis.
/// @param vertices Output filled with the face's four corners (wound around the face).
static void ph3d_obb_face_vertices(const ph3d_obb_box *box,
                                   int face_axis,
                                   double face_sign,
                                   double vertices[4][3]) {
    int u = (face_axis + 1) % 3;
    int v = (face_axis + 2) % 3;
    const double signs[4][2] = {{-1.0, -1.0}, {1.0, -1.0}, {1.0, 1.0}, {-1.0, 1.0}};
    double face_center[3];
    for (int i = 0; i < 3; ++i)
        face_center[i] =
            box->center[i] + box->axes[face_axis][i] * box->half_extents[face_axis] * face_sign;
    for (int corner = 0; corner < 4; ++corner) {
        for (int i = 0; i < 3; ++i) {
            vertices[corner][i] = face_center[i] +
                                  box->axes[u][i] * box->half_extents[u] * signs[corner][0] +
                                  box->axes[v][i] * box->half_extents[v] * signs[corner][1];
        }
    }
}

/// @brief Sutherland-Hodgman clip a convex polygon against one OBB side half-space.
/// @details Keeps the part of the polygon satisfying `dot(point - origin, axis) <= limit`,
///          inserting intersection points wherever an edge crosses the plane. Output is capped
///          at PH3D_OBB_CLIP_MAX_POINTS.
/// @return Number of vertices written to @p out_points.
static int ph3d_clip_poly_to_extent(const double in_points[PH3D_OBB_CLIP_MAX_POINTS][3],
                                    int in_count,
                                    const double *origin,
                                    const double *axis,
                                    double limit,
                                    double out_points[PH3D_OBB_CLIP_MAX_POINTS][3]) {
    int out_count = 0;
    const double eps = 1e-8;
    if (!origin || !axis || in_count <= 0)
        return 0;
    for (int i = 0; i < in_count; ++i) {
        const double *s = in_points[i];
        const double *e = in_points[(i + 1) % in_count];
        double so[3];
        double eo[3];
        double edge[3];
        double ds;
        double de;
        int s_inside;
        int e_inside;
        vec3_sub(s, origin, so);
        vec3_sub(e, origin, eo);
        ds = vec3_dot(so, axis) - limit;
        de = vec3_dot(eo, axis) - limit;
        s_inside = ds <= eps;
        e_inside = de <= eps;
        vec3_sub(e, s, edge);
        if (s_inside && e_inside) {
            if (out_count < PH3D_OBB_CLIP_MAX_POINTS)
                vec3_copy(out_points[out_count++], e);
        } else if (s_inside && !e_inside) {
            double denom = ds - de;
            double t = fabs(denom) > 1e-12 ? ds / denom : 0.0;
            double p[3] = {s[0] + edge[0] * t, s[1] + edge[1] * t, s[2] + edge[2] * t};
            if (out_count < PH3D_OBB_CLIP_MAX_POINTS)
                vec3_copy(out_points[out_count++], p);
        } else if (!s_inside && e_inside) {
            double denom = ds - de;
            double t = fabs(denom) > 1e-12 ? ds / denom : 0.0;
            double p[3] = {s[0] + edge[0] * t, s[1] + edge[1] * t, s[2] + edge[2] * t};
            if (out_count < PH3D_OBB_CLIP_MAX_POINTS)
                vec3_copy(out_points[out_count++], p);
            if (out_count < PH3D_OBB_CLIP_MAX_POINTS)
                vec3_copy(out_points[out_count++], e);
        }
    }
    return out_count;
}

/// @brief Return 1 if @p candidate already appears in the first @p count entries of @p indices.
static int ph3d_obb_candidate_exists(const int *indices, int count, int candidate) {
    for (int i = 0; i < count; ++i) {
        if (indices[i] == candidate)
            return 1;
    }
    return 0;
}

/// @brief Select the candidate contact point that extremizes one reference-face quadrant.
/// @details Projects each candidate onto the reference face's two tangent axes and scores it per
///          @p quadrant (each of the four quadrants favors a different (±u, ±v) corner), appending
///          the winning, not-yet-chosen index to @p indices. Used to reduce an over-full candidate
///          set to a well-spread PH3D_MAX_MANIFOLD_POINTS manifold.
static void ph3d_obb_select_candidate(const ph3d_obb_contact_candidate *candidates,
                                      int candidate_count,
                                      const ph3d_obb_box *ref_box,
                                      int ref_axis,
                                      const double *ref_face_center,
                                      int quadrant,
                                      int *indices,
                                      int *index_count) {
    int u = (ref_axis + 1) % 3;
    int v = (ref_axis + 2) % 3;
    double best_score = -DBL_MAX;
    int best_index = -1;
    for (int i = 0; i < candidate_count; ++i) {
        double rel[3];
        double coord_u;
        double coord_v;
        double score;
        vec3_sub(candidates[i].point, ref_face_center, rel);
        coord_u = vec3_dot(rel, ref_box->axes[u]);
        coord_v = vec3_dot(rel, ref_box->axes[v]);
        switch (quadrant) {
            case 0:
                score = -coord_u - coord_v;
                break;
            case 1:
                score = coord_u - coord_v;
                break;
            case 2:
                score = coord_u + coord_v;
                break;
            default:
                score = -coord_u + coord_v;
                break;
        }
        if (score > best_score) {
            best_score = score;
            best_index = i;
        }
    }
    if (best_index >= 0 && *index_count < PH3D_MAX_MANIFOLD_POINTS &&
        !ph3d_obb_candidate_exists(indices, *index_count, best_index)) {
        indices[(*index_count)++] = best_index;
    }
}

/// @brief Expand a single-point OBB/OBB contact into a stable multi-point manifold via face
/// clipping.
/// @details Reconstructs both boxes, picks the most-aligned face as the reference and the opposing
///          box's most anti-aligned face as the incident polygon, then clips the incident face
///          against the reference face's four side planes (ph3d_clip_poly_to_extent). Surviving
///          points within tolerance of the reference plane become contact candidates; when more
///          than PH3D_MAX_MANIFOLD_POINTS survive they are reduced to a spread set via
///          ph3d_obb_select_candidate. On too-poor face alignment or fewer than two surviving
///          points it restores the saved single-point @p contact and reports failure.
/// @return 1 if a multi-point manifold was produced; 0 to keep the original single-point contact.
int contact3d_expand_obb_manifold(rt_contact3d *contact,
                                  void *a_leaf,
                                  const rt_collider_pose *a_pose,
                                  void *b_leaf,
                                  const rt_collider_pose *b_pose) {
    rt_contact3d fallback;
    ph3d_obb_box box_a;
    ph3d_obb_box box_b;
    const ph3d_obb_box *ref_box;
    const ph3d_obb_box *inc_box;
    double toward_b[3];
    double toward_a[3];
    double ref_normal[3];
    double final_normal[3];
    double ref_face_center[3];
    double incident_vertices[4][3];
    double poly_a[PH3D_OBB_CLIP_MAX_POINTS][3];
    double poly_b[PH3D_OBB_CLIP_MAX_POINTS][3];
    ph3d_obb_contact_candidate candidates[PH3D_OBB_CLIP_MAX_POINTS];
    int ref_axis = 0;
    int inc_axis = 0;
    int clipped_count = 4;
    int candidate_count = 0;
    double ref_sign = 1.0;
    double inc_sign = 1.0;
    double align_a = 0.0;
    double align_b = 0.0;
    const double min_face_alignment = 0.95;

    if (!contact || !ph3d_make_obb_box(a_leaf, a_pose, &box_a) ||
        !ph3d_make_obb_box(b_leaf, b_pose, &box_b))
        return 0;

    contact_snapshot_copy(&fallback, contact);
    vec3_copy(toward_b, contact->normal);
    if (vec3_normalize_in_place(toward_b) <= 1e-12)
        return 0;
    vec3_negate(toward_b, toward_a);

    ph3d_obb_best_face_axis(&box_a, toward_b, &ref_axis, &ref_sign, &align_a);
    ph3d_obb_best_face_axis(&box_b, toward_a, &inc_axis, &inc_sign, &align_b);
    if (align_a < min_face_alignment && align_b < min_face_alignment)
        return 0;

    if (align_b > align_a) {
        ref_box = &box_b;
        inc_box = &box_a;
        ph3d_obb_best_face_axis(ref_box, toward_a, &ref_axis, &ref_sign, NULL);
        vec3_negate(toward_b, ref_normal);
    } else {
        ref_box = &box_a;
        inc_box = &box_b;
        ph3d_obb_best_face_axis(ref_box, toward_b, &ref_axis, &ref_sign, NULL);
        vec3_copy(ref_normal, toward_b);
    }
    vec3_copy(final_normal, toward_b);

    for (int i = 0; i < 3; ++i)
        ref_face_center[i] = ref_box->center[i] + ref_box->axes[ref_axis][i] *
                                                      ref_box->half_extents[ref_axis] * ref_sign;

    {
        double best_inc_alignment = -1.0;
        for (int axis = 0; axis < 3; ++axis) {
            double d = vec3_dot(ref_normal, inc_box->axes[axis]);
            double a = fabs(d);
            if (a > best_inc_alignment) {
                best_inc_alignment = a;
                inc_axis = axis;
                inc_sign = d >= 0.0 ? -1.0 : 1.0;
            }
        }
    }

    ph3d_obb_face_vertices(inc_box, inc_axis, inc_sign, incident_vertices);
    for (int i = 0; i < 4; ++i)
        vec3_copy(poly_a[i], incident_vertices[i]);

    {
        int u = (ref_axis + 1) % 3;
        int v = (ref_axis + 2) % 3;
        double neg_u[3];
        double neg_v[3];
        vec3_negate(ref_box->axes[u], neg_u);
        vec3_negate(ref_box->axes[v], neg_v);
        clipped_count = ph3d_clip_poly_to_extent(poly_a,
                                                 clipped_count,
                                                 ref_face_center,
                                                 ref_box->axes[u],
                                                 ref_box->half_extents[u],
                                                 poly_b);
        clipped_count = ph3d_clip_poly_to_extent(
            poly_b, clipped_count, ref_face_center, neg_u, ref_box->half_extents[u], poly_a);
        clipped_count = ph3d_clip_poly_to_extent(poly_a,
                                                 clipped_count,
                                                 ref_face_center,
                                                 ref_box->axes[v],
                                                 ref_box->half_extents[v],
                                                 poly_b);
        clipped_count = ph3d_clip_poly_to_extent(
            poly_b, clipped_count, ref_face_center, neg_v, ref_box->half_extents[v], poly_a);
    }

    if (clipped_count <= 0)
        return 0;

    for (int i = 0; i < clipped_count && i < PH3D_OBB_CLIP_MAX_POINTS; ++i) {
        double rel[3];
        double separation;
        vec3_sub(poly_a[i], ref_face_center, rel);
        separation = vec3_dot(rel, ref_normal);
        if (separation <= 1e-6 && candidate_count < PH3D_OBB_CLIP_MAX_POINTS) {
            candidates[candidate_count].point[0] = poly_a[i][0] - 0.5 * separation * ref_normal[0];
            candidates[candidate_count].point[1] = poly_a[i][1] - 0.5 * separation * ref_normal[1];
            candidates[candidate_count].point[2] = poly_a[i][2] - 0.5 * separation * ref_normal[2];
            candidates[candidate_count].separation = separation;
            candidate_count++;
        }
    }
    if (candidate_count < 2)
        return 0;

    contact->contact_count = 0;
    if (candidate_count <= PH3D_MAX_MANIFOLD_POINTS) {
        for (int i = 0; i < candidate_count; ++i)
            contact3d_try_add_manifold_point(
                contact, candidates[i].point, final_normal, candidates[i].separation);
    } else {
        int indices[PH3D_MAX_MANIFOLD_POINTS];
        int index_count = 0;
        for (int quadrant = 0; quadrant < PH3D_MAX_MANIFOLD_POINTS; ++quadrant) {
            ph3d_obb_select_candidate(candidates,
                                      candidate_count,
                                      ref_box,
                                      ref_axis,
                                      ref_face_center,
                                      quadrant,
                                      indices,
                                      &index_count);
        }
        for (int i = 0; i < candidate_count && index_count < PH3D_MAX_MANIFOLD_POINTS; ++i) {
            if (!ph3d_obb_candidate_exists(indices, index_count, i))
                indices[index_count++] = i;
        }
        for (int i = 0; i < index_count; ++i) {
            int candidate_index = indices[i];
            contact3d_try_add_manifold_point(contact,
                                             candidates[candidate_index].point,
                                             final_normal,
                                             candidates[candidate_index].separation);
        }
    }

    if (contact->contact_count < 2) {
        contact_snapshot_copy(contact, &fallback);
        return 0;
    }
    vec3_copy(contact->point, contact->points[0]);
    vec3_copy(contact->normal, final_normal);
    contact->separation = contact->separations[0];
    return 1;
}

/// @brief Identity test for contacts: same body pair AND same collider pair.
///
/// The collider check matters for compound bodies — same body pair but
/// different child colliders is a different contact.
int contact_pair_equals(const rt_contact3d *a, const rt_contact3d *b) {
    if (!a || !b)
        return 0;
    return (a->body_a == b->body_a && a->body_b == b->body_b && a->collider_a == b->collider_a &&
            a->collider_b == b->collider_b) ||
           (a->body_a == b->body_b && a->body_b == b->body_a && a->collider_a == b->collider_b &&
            a->collider_b == b->collider_a);
}

/// @brief Fold `value` into running hash `key` (golden-ratio mix, à la Boost hash_combine);
///   never returns 0 so the result can serve as a non-empty-slot marker.
static uintptr_t contact_pair_hash_mix(uintptr_t key, uintptr_t value) {
    key ^= value + (uintptr_t)0x9e3779b97f4a7c15ull + (key << 6) + (key >> 2);
    return key ? key : (uintptr_t)1u;
}

/// @brief Hash a contact's identity (both body and both collider pointers) into one key.
static uintptr_t contact_pair_hash_value(const rt_contact3d *contact) {
    uintptr_t body_a = (uintptr_t)contact->body_a;
    uintptr_t body_b = (uintptr_t)contact->body_b;
    uintptr_t collider_a = (uintptr_t)contact->collider_a;
    uintptr_t collider_b = (uintptr_t)contact->collider_b;
    uintptr_t key;
    if (body_b < body_a || (body_b == body_a && collider_b < collider_a)) {
        uintptr_t tmp = body_a;
        body_a = body_b;
        body_b = tmp;
        tmp = collider_a;
        collider_a = collider_b;
        collider_b = tmp;
    }
    key = body_a;
    key = contact_pair_hash_mix(key, body_b);
    key = contact_pair_hash_mix(key, collider_a);
    key = contact_pair_hash_mix(key, collider_b);
    return key ? key : (uintptr_t)1u;
}

/// @brief Append a contact to the per-frame aggregate, replacing an existing
///        same-pair record so the latest substep's geometry is exposed.
int world3d_append_frame_contact_unique(rt_world3d *w, const rt_contact3d *contact) {
    int32_t next_count;
    if (!w || !contact)
        return 1;
    for (int32_t i = 0; i < w->frame_contact_count; ++i) {
        if (contact_pair_equals(&w->frame_contacts[i], contact)) {
            contact_snapshot_copy(&w->frame_contacts[i], contact);
            return 1;
        }
    }
    if (!world3d_checked_increment(w->frame_contact_count, &next_count) ||
        !world3d_reserve_frame_contacts(w, next_count)) {
        rt_trap("Physics3D.World.Step: frame-contact allocation failed");
        return 0;
    }
    contact_snapshot_copy(&w->frame_contacts[w->frame_contact_count++], contact);
    return 1;
}

/// @brief Publish the frame-wide contact aggregate as the public contact list.
int world3d_publish_frame_contacts(rt_world3d *w) {
    if (!w)
        return 0;
    if (!world3d_reserve_contacts(w, w->frame_contact_count)) {
        rt_trap("Physics3D.World.Step: contact allocation failed");
        return 0;
    }
    w->contact_count = w->frame_contact_count;
    for (int32_t i = 0; i < w->frame_contact_count; ++i)
        contact_snapshot_copy(&w->contacts[i], &w->frame_contacts[i]);
    return 1;
}

/// @brief Choose a power-of-two table capacity above 2×`count` (kept sparse to bound
///   probe lengths); returns 0 on overflow or non-positive count.
static int contact_pair_table_capacity(int32_t count, int32_t *out_capacity) {
    int32_t cap = 16;
    if (!out_capacity || count <= 0)
        return 0;
    if (count > INT32_MAX / 2)
        return 0;
    while (cap < count * 2) {
        if (cap > INT32_MAX / 2)
            return 0;
        cap *= 2;
    }
    if ((size_t)cap > SIZE_MAX / sizeof(contact_pair_hash_entry))
        return 0;
    *out_capacity = cap;
    return 1;
}

/// @brief Build an open-addressing (linear-probe) hash set over `count` contacts so
///   membership can be tested in O(1); returns the table (caller frees) and its
///   capacity via @p out_capacity, or NULL on allocation failure.
contact_pair_hash_entry *contact_pair_table_build(const rt_contact3d *contacts,
                                                  int32_t count,
                                                  int32_t *out_capacity) {
    int32_t capacity = 0;
    contact_pair_hash_entry *table;
    if (!contacts || count <= 0 || !out_capacity)
        return NULL;
    if (!contact_pair_table_capacity(count, &capacity))
        return NULL;
    table = (contact_pair_hash_entry *)calloc((size_t)capacity, sizeof(*table));
    if (!table)
        return NULL;
    for (int32_t i = 0; i < count; ++i) {
        uintptr_t hash = contact_pair_hash_value(&contacts[i]);
        int32_t slot = (int32_t)(hash & (uintptr_t)(capacity - 1));
        for (int32_t probe = 0; probe < capacity; ++probe) {
            int32_t idx = (slot + probe) & (capacity - 1);
            if (!table[idx].contact) {
                table[idx].contact = &contacts[i];
                table[idx].hash = hash;
                break;
            }
        }
    }
    *out_capacity = capacity;
    return table;
}

/// @brief True if `needle`'s contact pair is present in the linear-probe table built by
///   contact_pair_table_build; stops at the first empty slot (open-addressing absence).
int contact_pair_table_contains(const contact_pair_hash_entry *table,
                                int32_t capacity,
                                const rt_contact3d *needle) {
    uintptr_t hash;
    int32_t slot;
    if (!table || capacity <= 0 || !needle)
        return 0;
    hash = contact_pair_hash_value(needle);
    slot = (int32_t)(hash & (uintptr_t)(capacity - 1));
    for (int32_t probe = 0; probe < capacity; ++probe) {
        int32_t idx = (slot + probe) & (capacity - 1);
        if (!table[idx].contact)
            return 0;
        if (table[idx].hash == hash && contact_pair_equals(table[idx].contact, needle))
            return 1;
    }
    return 0;
}

/// @brief Compute the support point for a collider in a given direction.
///
/// "Support" = farthest point on the shape along `direction`. Closed
/// form for spheres and capsules; recursive for compound colliders
/// (pick the child whose support has the largest dot with `direction`);
/// AABB-based fallback for unrecognized shapes. Used by contact-point
/// reconstruction after narrow-phase collision detection.
static void collider_support_point(void *collider,
                                   const rt_collider_pose *pose,
                                   const double *direction,
                                   double *out_point) {
    double dir[3];
    double mn[3];
    double mx[3];
    int64_t type;
    if (!out_point) {
        return;
    }
    vec3_set(out_point, 0.0, 0.0, 0.0);
    if (!collider || !pose || !direction)
        return;

    vec3_copy(dir, direction);
    if (vec3_normalize_in_place(dir) <= 1e-12)
        vec3_set(dir, 0.0, 1.0, 0.0);
    type = rt_collider3d_get_type(collider);

    switch (type) {
        case RT_COLLIDER3D_TYPE_BOX: {
            double half_extents[3];
            double inv_rotation[4];
            double local_dir[3];
            double local[3];
            rt_collider3d_get_box_half_extents_raw(collider, half_extents);
            quat_conjugate(pose->rotation, inv_rotation);
            quat_rotate_vec3(inv_rotation, dir, local_dir);
            local[0] = local_dir[0] * pose->scale[0] >= 0.0 ? half_extents[0] : -half_extents[0];
            local[1] = local_dir[1] * pose->scale[1] >= 0.0 ? half_extents[1] : -half_extents[1];
            local[2] = local_dir[2] * pose->scale[2] >= 0.0 ? half_extents[2] : -half_extents[2];
            transform_point_from_pose(pose, local, out_point);
            return;
        }
        case RT_COLLIDER3D_TYPE_SPHERE: {
            double radius = rt_collider3d_get_radius_raw(collider);
            double max_scale = fabs(pose->scale[0]);
            if (fabs(pose->scale[1]) > max_scale)
                max_scale = fabs(pose->scale[1]);
            if (fabs(pose->scale[2]) > max_scale)
                max_scale = fabs(pose->scale[2]);
            out_point[0] = pose->position[0] + dir[0] * radius * max_scale;
            out_point[1] = pose->position[1] + dir[1] * radius * max_scale;
            out_point[2] = pose->position[2] + dir[2] * radius * max_scale;
            return;
        }
        case RT_COLLIDER3D_TYPE_CAPSULE: {
            double radius = rt_collider3d_get_radius_raw(collider);
            double half_axis = fmax(rt_collider3d_get_height_raw(collider) * 0.5 - radius, 0.0);
            double sx = fabs(pose->scale[0]);
            double sy = fabs(pose->scale[1]);
            double sz = fabs(pose->scale[2]);
            double max_radial_scale = sx > sz ? sx : sz;
            double axis_dir[3];
            double local_y[3] = {0.0, 1.0, 0.0};
            quat_rotate_vec3(pose->rotation, local_y, axis_dir);
            if (vec3_normalize_in_place(axis_dir) <= 1e-12)
                vec3_set(axis_dir, 0.0, 1.0, 0.0);
            double side = vec3_dot(dir, axis_dir) >= 0.0 ? 1.0 : -1.0;
            out_point[0] = pose->position[0] + axis_dir[0] * half_axis * sy * side +
                           dir[0] * radius * max_radial_scale;
            out_point[1] = pose->position[1] + axis_dir[1] * half_axis * sy * side +
                           dir[1] * radius * max_radial_scale;
            out_point[2] = pose->position[2] + axis_dir[2] * half_axis * sy * side +
                           dir[2] * radius * max_radial_scale;
            return;
        }
        case RT_COLLIDER3D_TYPE_CONVEX_HULL: {
            rt_mesh3d *mesh = (rt_mesh3d *)rt_collider3d_get_mesh_raw(collider);
            double best_dot = -DBL_MAX;
            if (mesh && mesh->vertex_count > 0) {
                rt_mesh3d_refresh_bounds(mesh);
                for (uint32_t i = 0; i < mesh->vertex_count; ++i) {
                    double local[3] = {mesh->vertices[i].pos[0],
                                       mesh->vertices[i].pos[1],
                                       mesh->vertices[i].pos[2]};
                    double world[3];
                    double d;
                    transform_point_from_pose(pose, local, world);
                    d = vec3_dot(world, dir);
                    if (d > best_dot) {
                        best_dot = d;
                        vec3_copy(out_point, world);
                    }
                }
                if (best_dot > -DBL_MAX)
                    return;
            }
            break;
        }
        case RT_COLLIDER3D_TYPE_COMPOUND: {
            int64_t child_count = rt_collider3d_get_child_count_raw(collider);
            double best_dot = -1e300;
            for (int64_t i = 0; i < child_count; ++i) {
                void *child = rt_collider3d_get_child_raw(collider, i);
                double child_pos[3], child_rot[4], child_scale[3];
                rt_collider_pose child_pose;
                double candidate[3];
                rt_collider3d_get_child_transform_raw(
                    collider, i, child_pos, child_rot, child_scale);
                collider_pose_compose(pose, child_pos, child_rot, child_scale, &child_pose);
                collider_support_point(child, &child_pose, dir, candidate);
                {
                    double d = vec3_dot(candidate, dir);
                    if (d > best_dot) {
                        best_dot = d;
                        vec3_copy(out_point, candidate);
                    }
                }
            }
            return;
        }
        default:
            rt_collider3d_compute_world_aabb_raw(
                collider, pose->position, pose->rotation, pose->scale, mn, mx);
            out_point[0] = dir[0] >= 0.0 ? mx[0] : mn[0];
            out_point[1] = dir[1] >= 0.0 ? mx[1] : mn[1];
            out_point[2] = dir[2] >= 0.0 ? mx[2] : mn[2];
            return;
    }
}

/// @brief Estimate a representative contact point given two leaf colliders + a normal.
///
/// Uses the overlapping region of the two world AABBs to pick a point on
/// the active contact face. This is deliberately conservative: for large
/// static boxes (floors, walls) a support point along an axis-aligned normal
/// can land on a far corner when the other normal components are zero, which
/// creates bogus torque in the contact solver.
static void compute_contact_point_from_leafs(void *a_leaf,
                                             const rt_collider_pose *a_pose,
                                             void *b_leaf,
                                             const rt_collider_pose *b_pose,
                                             const double *normal,
                                             double *point_out) {
    double point_a[3];
    double point_b[3];
    double inv_normal[3];
    double amn[3];
    double amx[3];
    double bmn[3];
    double bmx[3];
    double abs_n[3];
    int axis = 0;
    if (!point_out) {
        return;
    }
    vec3_set(point_out, 0.0, 0.0, 0.0);
    if (!a_leaf || !a_pose || !b_leaf || !b_pose || !normal)
        return;

    rt_collider3d_compute_world_aabb_raw(
        a_leaf, a_pose->position, a_pose->rotation, a_pose->scale, amn, amx);
    rt_collider3d_compute_world_aabb_raw(
        b_leaf, b_pose->position, b_pose->rotation, b_pose->scale, bmn, bmx);

    abs_n[0] = fabs(normal[0]);
    abs_n[1] = fabs(normal[1]);
    abs_n[2] = fabs(normal[2]);
    if (abs_n[1] > abs_n[axis])
        axis = 1;
    if (abs_n[2] > abs_n[axis])
        axis = 2;

    if (isfinite(amn[0]) && isfinite(amn[1]) && isfinite(amn[2]) && isfinite(amx[0]) &&
        isfinite(amx[1]) && isfinite(amx[2]) && isfinite(bmn[0]) && isfinite(bmn[1]) &&
        isfinite(bmn[2]) && isfinite(bmx[0]) && isfinite(bmx[1]) && isfinite(bmx[2])) {
        for (int i = 0; i < 3; ++i) {
            double lo = fmax(amn[i], bmn[i]);
            double hi = fmin(amx[i], bmx[i]);
            if (i == axis && abs_n[i] > 1e-8) {
                double a_face = normal[i] >= 0.0 ? amx[i] : amn[i];
                double b_face = normal[i] >= 0.0 ? bmn[i] : bmx[i];
                point_out[i] = (a_face + b_face) * 0.5;
            } else if (lo <= hi) {
                point_out[i] = (lo + hi) * 0.5;
            } else {
                double ac = (amn[i] + amx[i]) * 0.5;
                double bc = (bmn[i] + bmx[i]) * 0.5;
                point_out[i] = (ac + bc) * 0.5;
            }
        }
        return;
    }

    vec3_negate(normal, inv_normal);
    collider_support_point(a_leaf, a_pose, normal, point_a);
    collider_support_point(b_leaf, b_pose, inv_normal, point_b);
    point_out[0] = (point_a[0] + point_b[0]) * 0.5;
    point_out[1] = (point_a[1] + point_b[1]) * 0.5;
    point_out[2] = (point_a[2] + point_b[2]) * 0.5;
}

/// @brief Bidirectional layer/mask filter for body-vs-body collision.
///
/// Both bodies must be on a layer the other body's mask accepts. This
/// preserves intuitive behavior: e.g., players (layer P, mask=P|E) and
/// enemies (layer E, mask=P|E) collide; bullets (layer B, mask=E) hit
/// enemies but not players.
int bodies_can_collide(const rt_body3d *a, const rt_body3d *b) {
    if (!a || !b)
        return 0;
    if (!(a->collision_layer & b->collision_mask))
        return 0;
    if (!(b->collision_layer & a->collision_mask))
        return 0;
    return 1;
}

/* --- Shape-specific narrow-phase collision tests ---
 * Normal always points A→B (matches impulse convention: a.vel -= j*n). */

/// @brief AABB-vs-AABB narrow phase: returns penetration depth and normal.
///
/// SAT picks the axis of minimum overlap as the separation direction.
/// Normal points from A toward B (impulse convention: `a.vel -= j*n`).
static int test_aabb_aabb(const rt_body3d *a, const rt_body3d *b, double *normal, double *depth) {
    double amn[3], amx[3], bmn[3], bmx[3];
    body_aabb(a, amn, amx);
    body_aabb(b, bmn, bmx);
    double ox = (amx[0] < bmx[0] ? amx[0] - bmn[0] : bmx[0] - amn[0]);
    double oy = (amx[1] < bmx[1] ? amx[1] - bmn[1] : bmx[1] - amn[1]);
    double oz = (amx[2] < bmx[2] ? amx[2] - bmn[2] : bmx[2] - amn[2]);
    if (ox <= 0 || oy <= 0 || oz <= 0)
        return 0;
    normal[0] = normal[1] = normal[2] = 0;
    if (ox <= oy && ox <= oz) {
        *depth = ox;
        normal[0] = (a->position[0] < b->position[0]) ? 1.0 : -1.0;
    } else if (oy <= oz) {
        *depth = oy;
        normal[1] = (a->position[1] < b->position[1]) ? 1.0 : -1.0;
    } else {
        *depth = oz;
        normal[2] = (a->position[2] < b->position[2]) ? 1.0 : -1.0;
    }
    return 1;
}

/// @brief Sphere-vs-sphere: simple distance vs sum-of-radii test.
///
/// Falls back to a +Y push when the centers are exactly coincident
/// (otherwise the normal would be ill-defined and the impulse step
/// would NaN out).
static int test_sphere_sphere(const rt_body3d *a,
                              const rt_body3d *b,
                              double *normal,
                              double *depth) {
    double dx = b->position[0] - a->position[0];
    double dy = b->position[1] - a->position[1];
    double dz = b->position[2] - a->position[2];
    double dist_sq = dx * dx + dy * dy + dz * dz;
    double sum_r = a->radius + b->radius;
    if (dist_sq >= sum_r * sum_r)
        return 0;
    double dist = sqrt(dist_sq);
    if (dist < 1e-12) {
        /* Coincident centers — push along Y */
        normal[0] = 0;
        normal[1] = 1;
        normal[2] = 0;
        *depth = sum_r;
    } else {
        double inv_dist = 1.0 / dist;
        normal[0] = dx * inv_dist;
        normal[1] = dy * inv_dist;
        normal[2] = dz * inv_dist;
        *depth = sum_r - dist;
    }
    return 1;
}

/// @brief AABB-vs-sphere: clamp sphere center into AABB, then sphere-distance test.
///
/// If the sphere's center lies inside the AABB, falls back to the
/// AABB-AABB pushout to avoid a degenerate zero-distance normal. The
/// returned normal points from AABB to sphere.
static int test_aabb_sphere(const rt_body3d *aabb,
                            const rt_body3d *sph,
                            double *normal,
                            double *depth) {
    /* Find closest point on AABB to sphere center */
    double closest[3];
    double amn[3], amx[3];
    body_aabb(aabb, amn, amx);
    for (int i = 0; i < 3; i++) {
        double v = sph->position[i];
        if (v < amn[i])
            v = amn[i];
        if (v > amx[i])
            v = amx[i];
        closest[i] = v;
    }
    double dx = sph->position[0] - closest[0];
    double dy = sph->position[1] - closest[1];
    double dz = sph->position[2] - closest[2];
    double dist_sq = dx * dx + dy * dy + dz * dz;
    if (dist_sq >= sph->radius * sph->radius)
        return 0;
    double dist = sqrt(dist_sq);
    if (dist < 1e-12) {
        /* Sphere center inside AABB — use AABB pushout */
        return test_aabb_aabb(aabb, sph, normal, depth);
    }
    double inv_dist = 1.0 / dist;
    /* Normal points from AABB toward sphere */
    normal[0] = dx * inv_dist;
    normal[1] = dy * inv_dist;
    normal[2] = dz * inv_dist;
    *depth = sph->radius - dist;
    return 1;
}

/// @brief Whether `type` is one of the primitive shapes the narrow phase handles directly.
static int collider_type_is_simple(int64_t type) {
    return type == RT_COLLIDER3D_TYPE_BOX || type == RT_COLLIDER3D_TYPE_SPHERE ||
           type == RT_COLLIDER3D_TYPE_CAPSULE;
}

/// @brief Build a transient `rt_body3d` whose shape mirrors a simple collider.
///
/// Used during compound-vs-compound collision: each leaf-pair produces
/// a transient body so we can reuse the body-level narrow-phase tests.
/// Applies the pose's scale to extents/radius/height. Returns 0 for
/// non-simple shapes (the caller falls back to AABB-vs-AABB).
int build_simple_proxy(const rt_collider_pose *pose, void *collider, rt_body3d *out) {
    double hx[3];
    double sx = fabs(pose->scale[0]);
    double sy = fabs(pose->scale[1]);
    double sz = fabs(pose->scale[2]);
    int64_t type;
    if (!pose || !collider || !out)
        return 0;
    memset(out, 0, sizeof(*out));
    out->position[0] = pose->position[0];
    out->position[1] = pose->position[1];
    out->position[2] = pose->position[2];
    out->orientation[0] = pose->rotation[0];
    out->orientation[1] = pose->rotation[1];
    out->orientation[2] = pose->rotation[2];
    out->orientation[3] = pose->rotation[3];
    type = rt_collider3d_get_type(collider);
    switch (type) {
        case RT_COLLIDER3D_TYPE_BOX:
            out->shape = PH3D_SHAPE_AABB;
            rt_collider3d_get_box_half_extents_raw(collider, hx);
            out->half_extents[0] = hx[0] * sx;
            out->half_extents[1] = hx[1] * sy;
            out->half_extents[2] = hx[2] * sz;
            return 1;
        case RT_COLLIDER3D_TYPE_SPHERE:
            out->shape = PH3D_SHAPE_SPHERE;
            out->radius = rt_collider3d_get_radius_raw(collider);
            if (sy > sx)
                sx = sy;
            if (sz > sx)
                sx = sz;
            out->radius *= sx;
            return 1;
        case RT_COLLIDER3D_TYPE_CAPSULE:
            out->shape = PH3D_SHAPE_CAPSULE;
            {
                double raw_radius = rt_collider3d_get_radius_raw(collider);
                double raw_height = rt_collider3d_get_height_raw(collider);
                double scaled_radius = raw_radius * (sx > sz ? sx : sz);
                double scaled_cylinder = fmax(raw_height - 2.0 * raw_radius, 0.0) * sy;
                out->radius = scaled_radius;
                out->height = scaled_cylinder + 2.0 * scaled_radius;
            }
            return 1;
        default:
            return 0;
    }
}

/// @brief Compute a box collider's world half-extents by scaling its raw local
///   half-extents by the pose's absolute per-axis scale.
static void box_scaled_half_extents(void *collider,
                                    const rt_collider_pose *pose,
                                    double *half_extents) {
    double raw[3];
    rt_collider3d_get_box_half_extents_raw(collider, raw);
    half_extents[0] = raw[0] * pose_abs_scale_or_zero(pose ? pose->scale[0] : 1.0);
    half_extents[1] = raw[1] * pose_abs_scale_or_zero(pose ? pose->scale[1] : 1.0);
    half_extents[2] = raw[2] * pose_abs_scale_or_zero(pose ? pose->scale[2] : 1.0);
}

/// @brief Derive the pose's three orthonormal world axes (rows of its rotation) by
///   rotating the local basis vectors; degenerate axes fall back to the identity basis.
static void pose_rotation_axes(const rt_collider_pose *pose, double axes[3][3]) {
    const double local_x[3] = {1.0, 0.0, 0.0};
    const double local_y[3] = {0.0, 1.0, 0.0};
    const double local_z[3] = {0.0, 0.0, 1.0};
    quat_rotate_vec3(pose->rotation, local_x, axes[0]);
    quat_rotate_vec3(pose->rotation, local_y, axes[1]);
    quat_rotate_vec3(pose->rotation, local_z, axes[2]);
    for (int i = 0; i < 3; i++) {
        if (vec3_normalize_in_place(axes[i]) <= 1e-12) {
            axes[i][0] = axes[i][1] = axes[i][2] = 0.0;
            axes[i][i] = 1.0;
        }
    }
}

/// @brief SAT helper: if @p penetration is the smallest seen so far, record it and the
///   signed separating axis as the current best (minimum-penetration) result.
static void obb_record_axis(const double *axis,
                            double sign,
                            double penetration,
                            double *best_penetration,
                            double *best_axis) {
    if (penetration >= *best_penetration)
        return;
    *best_penetration = penetration;
    best_axis[0] = axis[0] * sign;
    best_axis[1] = axis[1] * sign;
    best_axis[2] = axis[2] * sign;
}

/// @brief Exact SAT test for two oriented box colliders.
static int test_box_box_obb(void *a_collider,
                            const rt_collider_pose *a_pose,
                            void *b_collider,
                            const rt_collider_pose *b_pose,
                            double *normal,
                            double *depth) {
    double a_he[3], b_he[3];
    double A[3][3], B[3][3];
    double R[3][3], AbsR[3][3];
    double t_world[3], t[3];
    double best_depth = DBL_MAX;
    double best_axis[3] = {1.0, 0.0, 0.0};
    const double eps = 1e-9;

    if (!a_collider || !a_pose || !b_collider || !b_pose || !normal || !depth)
        return 0;

    box_scaled_half_extents(a_collider, a_pose, a_he);
    box_scaled_half_extents(b_collider, b_pose, b_he);
    pose_rotation_axes(a_pose, A);
    pose_rotation_axes(b_pose, B);

    vec3_sub(b_pose->position, a_pose->position, t_world);
    for (int i = 0; i < 3; i++) {
        t[i] = vec3_dot(t_world, A[i]);
        for (int j = 0; j < 3; j++) {
            R[i][j] = vec3_dot(A[i], B[j]);
            AbsR[i][j] = fabs(R[i][j]) + eps;
        }
    }

    for (int i = 0; i < 3; i++) {
        double ra = a_he[i];
        double rb = b_he[0] * AbsR[i][0] + b_he[1] * AbsR[i][1] + b_he[2] * AbsR[i][2];
        double dist = fabs(t[i]);
        double penetration = ra + rb - dist;
        if (penetration < 0.0)
            return 0;
        obb_record_axis(A[i], t[i] >= 0.0 ? 1.0 : -1.0, penetration, &best_depth, best_axis);
    }

    for (int j = 0; j < 3; j++) {
        double ra = a_he[0] * AbsR[0][j] + a_he[1] * AbsR[1][j] + a_he[2] * AbsR[2][j];
        double rb = b_he[j];
        double dist = fabs(t[0] * R[0][j] + t[1] * R[1][j] + t[2] * R[2][j]);
        double penetration = ra + rb - dist;
        if (penetration < 0.0)
            return 0;
        {
            double sign = vec3_dot(t_world, B[j]) >= 0.0 ? 1.0 : -1.0;
            obb_record_axis(B[j], sign, penetration, &best_depth, best_axis);
        }
    }

    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            double axis[3];
            double ra;
            double rb;
            double dist;
            double penetration;
            vec3_cross(A[i], B[j], axis);
            if (vec3_normalize_in_place(axis) <= 1e-8)
                continue;
            ra =
                a_he[(i + 1) % 3] * AbsR[(i + 2) % 3][j] + a_he[(i + 2) % 3] * AbsR[(i + 1) % 3][j];
            rb =
                b_he[(j + 1) % 3] * AbsR[i][(j + 2) % 3] + b_he[(j + 2) % 3] * AbsR[i][(j + 1) % 3];
            dist = fabs(t[(i + 2) % 3] * R[(i + 1) % 3][j] - t[(i + 1) % 3] * R[(i + 2) % 3][j]);
            penetration = ra + rb - dist;
            if (penetration < 0.0)
                return 0;
            obb_record_axis(axis,
                            vec3_dot(t_world, axis) >= 0.0 ? 1.0 : -1.0,
                            penetration,
                            &best_depth,
                            best_axis);
        }
    }

    vec3_copy(normal, best_axis);
    if (vec3_normalize_in_place(normal) <= 1e-12)
        vec3_set(normal, 0.0, 1.0, 0.0);
    *depth = best_depth == DBL_MAX ? 0.0 : best_depth;
    return *depth >= 0.0;
}

/// @brief Box-vs-sphere using the box's full oriented pose.
static int test_box_sphere_pose(void *box_collider,
                                const rt_collider_pose *box_pose,
                                const rt_body3d *sphere,
                                double *normal,
                                double *depth) {
    double raw_he[3];
    double local_center[3];
    double local_closest[3];
    double world_closest[3];
    double delta[3];
    double dist_sq;
    if (!box_collider || !box_pose || !sphere || !normal || !depth)
        return 0;
    rt_collider3d_get_box_half_extents_raw(box_collider, raw_he);
    transform_point_to_local(box_pose, sphere->position, local_center);
    for (int i = 0; i < 3; i++)
        local_closest[i] = clampd(local_center[i], -raw_he[i], raw_he[i]);
    transform_point_from_pose(box_pose, local_closest, world_closest);
    vec3_sub(sphere->position, world_closest, delta);
    dist_sq = vec3_len_sq(delta);
    if (dist_sq > sphere->radius * sphere->radius)
        return 0;
    if (dist_sq > 1e-18) {
        double dist = sqrt(dist_sq);
        normal[0] = delta[0] / dist;
        normal[1] = delta[1] / dist;
        normal[2] = delta[2] / dist;
        *depth = sphere->radius - dist;
        return 1;
    }

    {
        int axis = 0;
        double face_distance[3];
        double local_normal[3] = {0.0, 0.0, 0.0};
        for (int i = 0; i < 3; i++) {
            face_distance[i] =
                (raw_he[i] - fabs(local_center[i])) * pose_abs_scale_or_unit(box_pose->scale[i]);
            if (face_distance[i] < face_distance[axis])
                axis = i;
        }
        local_normal[axis] = local_center[axis] >= 0.0 ? 1.0 : -1.0;
        transform_normal_from_local(box_pose, local_normal, normal);
        *depth = sphere->radius + fmax(face_distance[axis], 0.0);
        return 1;
    }
}

/// @brief Box-vs-capsule via adaptive sphere samples along the capsule axis.
static int test_box_capsule_pose(void *box_collider,
                                 const rt_collider_pose *box_pose,
                                 const rt_body3d *capsule,
                                 double *normal,
                                 double *depth) {
    double a[3], b[3], axis[3];
    double best_depth = 0.0;
    double best_normal[3] = {0.0, 1.0, 0.0};
    int hit = 0;
    int samples;
    if (!box_collider || !box_pose || !capsule)
        return 0;
    capsule_axis_endpoints(capsule, a, b);
    vec3_sub(b, a, axis);
    samples = capsule_axis_sample_count(vec3_len(axis), capsule->radius);
    for (int i = 0; i < samples; i++) {
        double t = samples == 1 ? 0.5 : (double)i / (double)(samples - 1);
        rt_body3d sphere;
        double cur_normal[3];
        double cur_depth;
        make_temp_sphere(&sphere,
                         (double[3]){a[0] + axis[0] * t, a[1] + axis[1] * t, a[2] + axis[2] * t},
                         capsule->radius);
        if (test_box_sphere_pose(box_collider, box_pose, &sphere, cur_normal, &cur_depth) &&
            (!hit || cur_depth > best_depth)) {
            best_depth = cur_depth;
            vec3_copy(best_normal, cur_normal);
            hit = 1;
        }
    }
    if (!hit)
        return 0;
    vec3_copy(normal, best_normal);
    *depth = best_depth;
    return 1;
}

/// @brief Narrow-phase dispatch for two posed primitive colliders (box/sphere/capsule):
///   routes to the matching box-box/box-sphere/box-capsule (etc.) test, flipping the
///   returned normal so it always points from A toward B. Returns 1 on overlap with
///   @p normal/@p depth filled, else 0.
static int test_simple_collider_pose_collision(void *a_collider,
                                               const rt_collider_pose *a_pose,
                                               void *b_collider,
                                               const rt_collider_pose *b_pose,
                                               double *normal,
                                               double *depth) {
    int64_t a_type = rt_collider3d_get_type(a_collider);
    int64_t b_type = rt_collider3d_get_type(b_collider);
    rt_body3d proxy_a;
    rt_body3d proxy_b;

    if (a_type == RT_COLLIDER3D_TYPE_BOX && b_type == RT_COLLIDER3D_TYPE_BOX)
        return test_box_box_obb(a_collider, a_pose, b_collider, b_pose, normal, depth);

    if (a_type == RT_COLLIDER3D_TYPE_BOX) {
        if (!build_simple_proxy(b_pose, b_collider, &proxy_b))
            return 0;
        if (proxy_b.shape == PH3D_SHAPE_SPHERE)
            return test_box_sphere_pose(a_collider, a_pose, &proxy_b, normal, depth);
        if (proxy_b.shape == PH3D_SHAPE_CAPSULE)
            return test_box_capsule_pose(a_collider, a_pose, &proxy_b, normal, depth);
    }

    if (b_type == RT_COLLIDER3D_TYPE_BOX) {
        int hit;
        if (!build_simple_proxy(a_pose, a_collider, &proxy_a))
            return 0;
        if (proxy_a.shape == PH3D_SHAPE_SPHERE)
            hit = test_box_sphere_pose(b_collider, b_pose, &proxy_a, normal, depth);
        else if (proxy_a.shape == PH3D_SHAPE_CAPSULE)
            hit = test_box_capsule_pose(b_collider, b_pose, &proxy_a, normal, depth);
        else
            hit = 0;
        if (hit)
            vec3_negate(normal, normal);
        return hit;
    }

    if (!build_simple_proxy(a_pose, a_collider, &proxy_a) ||
        !build_simple_proxy(b_pose, b_collider, &proxy_b))
        return 0;
    return test_simple_collision(&proxy_a, &proxy_b, normal, depth);
}

/// @brief Narrow-phase dispatcher for primitive-shape body pairs.
///
/// Cheap broad-phase (AABB overlap) up front, then routes to:
///   - sphere/capsule × sphere/capsule via closest-axis sphere test
///   - AABB × sphere/capsule via clamp-then-distance
///   - AABB × AABB via SAT
/// The capsule shape is reduced to a transient sphere at the closest
/// axis point so the entire matrix collapses to ~3 specialized cases.
/// Always returns the normal pointing from A to B (so symmetric callers
/// flip it when needed).
static int test_simple_collision(const rt_body3d *a,
                                 const rt_body3d *b,
                                 double *normal,
                                 double *depth) {
    /* Broad phase: AABB overlap test */
    double amn[3], amx[3], bmn[3], bmx[3];
    body_aabb(a, amn, amx);
    body_aabb(b, bmn, bmx);
    if (amn[0] > bmx[0] || amx[0] < bmn[0] || amn[1] > bmx[1] || amx[1] < bmn[1] ||
        amn[2] > bmx[2] || amx[2] < bmn[2])
        return 0;

    /* Narrow phase: shape-specific dispatch */
    int sa = a->shape, sb = b->shape;

    /* Sphere/capsule pairs collapse to closest-axis sphere tests. */
    if ((sa == PH3D_SHAPE_SPHERE || sa == PH3D_SHAPE_CAPSULE) &&
        (sb == PH3D_SHAPE_SPHERE || sb == PH3D_SHAPE_CAPSULE)) {
        rt_body3d tmp_a, tmp_b;
        const rt_body3d *sphere_a = a;
        const rt_body3d *sphere_b = b;

        if (sa == PH3D_SHAPE_CAPSULE) {
            double center[3];
            if (sb == PH3D_SHAPE_CAPSULE) {
                double other_center[3];
                closest_points_capsule_axes(a, b, center, other_center);
                make_temp_sphere(&tmp_a, center, a->radius);
                make_temp_sphere(&tmp_b, other_center, b->radius);
                sphere_a = &tmp_a;
                sphere_b = &tmp_b;
                return test_sphere_sphere(sphere_a, sphere_b, normal, depth);
            }
            vec3_copy(center, b->position);
            closest_point_capsule_axis_to_point(a, center, center);
            make_temp_sphere(&tmp_a, center, a->radius);
            sphere_a = &tmp_a;
        }

        if (sb == PH3D_SHAPE_CAPSULE) {
            double center[3];
            vec3_copy(center, a->position);
            closest_point_capsule_axis_to_point(b, center, center);
            make_temp_sphere(&tmp_b, center, b->radius);
            sphere_b = &tmp_b;
        }
        return test_sphere_sphere(sphere_a, sphere_b, normal, depth);
    }

    /* AABB-sphere (order: A=AABB, B=sphere) */
    if (sa == PH3D_SHAPE_AABB && (sb == PH3D_SHAPE_SPHERE || sb == PH3D_SHAPE_CAPSULE)) {
        if (sb == PH3D_SHAPE_CAPSULE) {
            double center[3];
            rt_body3d tmp_sphere;
            closest_point_capsule_axis_to_aabb(b, a, center);
            make_temp_sphere(&tmp_sphere, center, b->radius);
            return test_aabb_sphere(a, &tmp_sphere, normal, depth);
        }
        return test_aabb_sphere(a, b, normal, depth);
    }

    /* Sphere-AABB (reversed — flip normal) */
    if ((sa == PH3D_SHAPE_SPHERE || sa == PH3D_SHAPE_CAPSULE) && sb == PH3D_SHAPE_AABB) {
        int hit;
        if (sa == PH3D_SHAPE_CAPSULE) {
            double center[3];
            rt_body3d tmp_sphere;
            closest_point_capsule_axis_to_aabb(a, b, center);
            make_temp_sphere(&tmp_sphere, center, a->radius);
            hit = test_aabb_sphere(b, &tmp_sphere, normal, depth);
        } else {
            hit = test_aabb_sphere(b, a, normal, depth);
        }
        if (hit) {
            normal[0] = -normal[0];
            normal[1] = -normal[1];
            normal[2] = -normal[2];
        }
        return hit;
    }

    /* AABB-AABB fallback */
    return test_aabb_aabb(a, b, normal, depth);
}

/// @brief Generic AABB-vs-AABB overlap with explicit centers (no body required).
///
/// Same SAT logic as `test_aabb_aabb` but takes raw extents and centers
/// so it can be used for compound child colliders that don't have a
/// backing body.
static int test_bounds_overlap(const double *amn,
                               const double *amx,
                               const double *a_center,
                               const double *bmn,
                               const double *bmx,
                               const double *b_center,
                               double *normal,
                               double *depth) {
    double ox = (amx[0] < bmx[0] ? amx[0] - bmn[0] : bmx[0] - amn[0]);
    double oy = (amx[1] < bmx[1] ? amx[1] - bmn[1] : bmx[1] - amn[1]);
    double oz = (amx[2] < bmx[2] ? amx[2] - bmn[2] : bmx[2] - amn[2]);
    if (ox <= 0.0 || oy <= 0.0 || oz <= 0.0)
        return 0;
    normal[0] = normal[1] = normal[2] = 0.0;
    if (ox <= oy && ox <= oz) {
        *depth = ox;
        normal[0] = (a_center[0] < b_center[0]) ? 1.0 : -1.0;
    } else if (oy <= oz) {
        *depth = oy;
        normal[1] = (a_center[1] < b_center[1]) ? 1.0 : -1.0;
    } else {
        *depth = oz;
        normal[2] = (a_center[2] < b_center[2]) ? 1.0 : -1.0;
    }
    return 1;
}

/// @brief Project point `p` onto triangle `(a, b, c)`, writing the closest point.
///
/// Implements the standard Voronoi-region algorithm from Real-Time
/// Collision Detection (Ericson §5.1.5): tests whether `p` falls into
/// the vertex, edge, or interior region and clamps accordingly.
static void closest_point_on_triangle(
    const double *p, const double *a, const double *b, const double *c, double *closest) {
    double ab[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
    double ac[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
    double ap[3] = {p[0] - a[0], p[1] - a[1], p[2] - a[2]};
    double d1 = vec3_dot(ab, ap);
    double d2 = vec3_dot(ac, ap);
    double d3, d4, d5, d6;
    if (d1 <= 0.0 && d2 <= 0.0) {
        vec3_copy(closest, a);
        return;
    }

    {
        double bp[3] = {p[0] - b[0], p[1] - b[1], p[2] - b[2]};
        d3 = vec3_dot(ab, bp);
        d4 = vec3_dot(ac, bp);
        if (d3 >= 0.0 && d4 <= d3) {
            vec3_copy(closest, b);
            return;
        }
        {
            double vc = d1 * d4 - d3 * d2;
            if (vc <= 0.0 && d1 >= 0.0 && d3 <= 0.0) {
                double v = d1 / (d1 - d3);
                closest[0] = a[0] + ab[0] * v;
                closest[1] = a[1] + ab[1] * v;
                closest[2] = a[2] + ab[2] * v;
                return;
            }
        }
    }

    {
        double cp[3] = {p[0] - c[0], p[1] - c[1], p[2] - c[2]};
        d5 = vec3_dot(ab, cp);
        d6 = vec3_dot(ac, cp);
        if (d6 >= 0.0 && d5 <= d6) {
            vec3_copy(closest, c);
            return;
        }
        {
            double vb = d5 * d2 - d1 * d6;
            if (vb <= 0.0 && d2 >= 0.0 && d6 <= 0.0) {
                double w = d2 / (d2 - d6);
                closest[0] = a[0] + ac[0] * w;
                closest[1] = a[1] + ac[1] * w;
                closest[2] = a[2] + ac[2] * w;
                return;
            }
        }
        {
            double va = d3 * d6 - d5 * d4;
            if (va <= 0.0 && (d4 - d3) >= 0.0 && (d5 - d6) >= 0.0) {
                double bc[3] = {c[0] - b[0], c[1] - b[1], c[2] - b[2]};
                double w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
                closest[0] = b[0] + bc[0] * w;
                closest[1] = b[1] + bc[1] * w;
                closest[2] = b[2] + bc[2] * w;
                return;
            }
        }
    }

    {
        double denom =
            1.0 / ((vec3_dot(ab, ab) * vec3_dot(ac, ac)) - vec3_dot(ab, ac) * vec3_dot(ab, ac));
        double dot_ap_ab = vec3_dot(ap, ab);
        double dot_ap_ac = vec3_dot(ap, ac);
        double dot_ab_ab = vec3_dot(ab, ab);
        double dot_ab_ac = vec3_dot(ab, ac);
        double dot_ac_ac = vec3_dot(ac, ac);
        double v = (dot_ac_ac * dot_ap_ab - dot_ab_ac * dot_ap_ac) * denom;
        double w = (dot_ab_ab * dot_ap_ac - dot_ab_ac * dot_ap_ab) * denom;
        closest[0] = a[0] + ab[0] * v + ac[0] * w;
        closest[1] = a[1] + ab[1] * v + ac[1] * w;
        closest[2] = a[2] + ab[2] * v + ac[2] * w;
    }
}

/// @brief Compute a unit normal from three triangle vertices via cross product.
///
/// Falls back to +Y when the triangle is degenerate (zero area). The
/// orientation depends on vertex order — caller may flip if needed.
void triangle_normal(const double *a, const double *b, const double *c, double *normal) {
    double ab[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
    double ac[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
    normal[0] = ab[1] * ac[2] - ab[2] * ac[1];
    normal[1] = ab[2] * ac[0] - ab[0] * ac[2];
    normal[2] = ab[0] * ac[1] - ab[1] * ac[0];
    {
        double len = vec3_len(normal);
        if (len > 1e-12) {
            normal[0] /= len;
            normal[1] /= len;
            normal[2] /= len;
        } else {
            normal[0] = 0.0;
            normal[1] = 1.0;
            normal[2] = 0.0;
        }
    }
}

int mesh_physics_bvh_rebuild(rt_mesh3d *mesh);
void transform_local_aabb_to_world(const rt_collider_pose *pose,
                                   const float *mn,
                                   const float *mx,
                                   double *out_min,
                                   double *out_max);

/// @brief Whether a sphere overlaps an axis-aligned box, via closest-point squared distance.
/// @details Sums squared distance only on axes where the center lies outside [mn, mx] and compares
///          to radius² — no sqrt, and an inside-the-box center trivially intersects.
static int aabb_intersects_sphere_raw(const double *mn,
                                      const double *mx,
                                      const double *center,
                                      double radius) {
    double dist_sq = 0.0;
    for (int axis = 0; axis < 3; ++axis) {
        double v = center[axis];
        if (v < mn[axis]) {
            double d = mn[axis] - v;
            dist_sq += d * d;
        } else if (v > mx[axis]) {
            double d = v - mx[axis];
            dist_sq += d * d;
        }
    }
    return dist_sq <= radius * radius;
}

/// @brief Sphere-vs-mesh narrow phase by per-triangle closest-point test.
///
/// Traverses the mesh's local-space BVH in world-space bounds and tests only
/// candidate leaf triangles, falling back to a full scan if BVH traversal cannot
/// complete.
static int test_meshlike_sphere(rt_mesh3d *mesh,
                                const rt_collider_pose *mesh_pose,
                                const rt_body3d *sphere,
                                double *normal,
                                double *depth) {
    double best_depth = 0.0;
    double best_normal[3] = {0.0, 1.0, 0.0};
    if (!mesh || !mesh_pose || !sphere || mesh->index_count < 3)
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
        if (!ph3d_i32_stack_push(&stack, &top, &stack_capacity, 0)) {
            overflow = 1;
        }
        while (top > 0) {
            int32_t node_index = stack[--top];
            const rt_physics_mesh_bvh_node *node;
            double node_min[3], node_max[3];
            if (node_index < 0 || node_index >= mesh->physics_bvh_node_count)
                continue;
            node = &nodes[node_index];
            transform_local_aabb_to_world(mesh_pose, node->min, node->max, node_min, node_max);
            if (!aabb_intersects_sphere_raw(node_min, node_max, sphere->position, sphere->radius))
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
            for (int32_t item = node->start; item < node->start + node->count; ++item) {
                uint32_t tri = tri_indices[item];
                uint32_t i0 = mesh->indices[tri * 3u + 0u];
                uint32_t i1 = mesh->indices[tri * 3u + 1u];
                uint32_t i2 = mesh->indices[tri * 3u + 2u];
                double a[3], b[3], c[3], closest[3];
                double dx, dy, dz, dist_sq;
                if (i0 >= mesh->vertex_count || i1 >= mesh->vertex_count ||
                    i2 >= mesh->vertex_count)
                    continue;
                {
                    double local[3];
                    local[0] = mesh->vertices[i0].pos[0];
                    local[1] = mesh->vertices[i0].pos[1];
                    local[2] = mesh->vertices[i0].pos[2];
                    transform_point_from_pose(mesh_pose, local, a);
                    local[0] = mesh->vertices[i1].pos[0];
                    local[1] = mesh->vertices[i1].pos[1];
                    local[2] = mesh->vertices[i1].pos[2];
                    transform_point_from_pose(mesh_pose, local, b);
                    local[0] = mesh->vertices[i2].pos[0];
                    local[1] = mesh->vertices[i2].pos[1];
                    local[2] = mesh->vertices[i2].pos[2];
                    transform_point_from_pose(mesh_pose, local, c);
                }
                closest_point_on_triangle(sphere->position, a, b, c, closest);
                dx = sphere->position[0] - closest[0];
                dy = sphere->position[1] - closest[1];
                dz = sphere->position[2] - closest[2];
                dist_sq = dx * dx + dy * dy + dz * dz;
                if (dist_sq >= sphere->radius * sphere->radius)
                    continue;
                {
                    double dist = sqrt(dist_sq);
                    double cur_depth = sphere->radius - dist;
                    double cur_normal[3];
                    if (dist > 1e-12) {
                        cur_normal[0] = dx / dist;
                        cur_normal[1] = dy / dist;
                        cur_normal[2] = dz / dist;
                    } else {
                        double centroid[3] = {(a[0] + b[0] + c[0]) / 3.0,
                                              (a[1] + b[1] + c[1]) / 3.0,
                                              (a[2] + b[2] + c[2]) / 3.0};
                        triangle_normal(a, b, c, cur_normal);
                        if ((sphere->position[0] - centroid[0]) * cur_normal[0] +
                                (sphere->position[1] - centroid[1]) * cur_normal[1] +
                                (sphere->position[2] - centroid[2]) * cur_normal[2] <
                            0.0) {
                            cur_normal[0] = -cur_normal[0];
                            cur_normal[1] = -cur_normal[1];
                            cur_normal[2] = -cur_normal[2];
                        }
                    }
                    if (cur_depth > best_depth) {
                        best_depth = cur_depth;
                        vec3_copy(best_normal, cur_normal);
                    }
                }
            }
        }
        free(stack);
        if (!overflow)
            goto mesh_sphere_done;
        best_depth = 0.0;
        vec3_set(best_normal, 0.0, 1.0, 0.0);
    }
    for (uint32_t i = 0; i + 2 < mesh->index_count; i += 3) {
        uint32_t i0 = mesh->indices[i];
        uint32_t i1 = mesh->indices[i + 1];
        uint32_t i2 = mesh->indices[i + 2];
        double a[3], b[3], c[3], closest[3];
        double dx, dy, dz, dist_sq;
        if (i0 >= mesh->vertex_count || i1 >= mesh->vertex_count || i2 >= mesh->vertex_count)
            continue;
        {
            double local[3];
            local[0] = mesh->vertices[i0].pos[0];
            local[1] = mesh->vertices[i0].pos[1];
            local[2] = mesh->vertices[i0].pos[2];
            transform_point_from_pose(mesh_pose, local, a);
            local[0] = mesh->vertices[i1].pos[0];
            local[1] = mesh->vertices[i1].pos[1];
            local[2] = mesh->vertices[i1].pos[2];
            transform_point_from_pose(mesh_pose, local, b);
            local[0] = mesh->vertices[i2].pos[0];
            local[1] = mesh->vertices[i2].pos[1];
            local[2] = mesh->vertices[i2].pos[2];
            transform_point_from_pose(mesh_pose, local, c);
        }
        closest_point_on_triangle(sphere->position, a, b, c, closest);
        dx = sphere->position[0] - closest[0];
        dy = sphere->position[1] - closest[1];
        dz = sphere->position[2] - closest[2];
        dist_sq = dx * dx + dy * dy + dz * dz;
        if (dist_sq >= sphere->radius * sphere->radius)
            continue;
        {
            double dist = sqrt(dist_sq);
            double cur_depth = sphere->radius - dist;
            double cur_normal[3];
            if (dist > 1e-12) {
                cur_normal[0] = dx / dist;
                cur_normal[1] = dy / dist;
                cur_normal[2] = dz / dist;
            } else {
                double centroid[3] = {(a[0] + b[0] + c[0]) / 3.0,
                                      (a[1] + b[1] + c[1]) / 3.0,
                                      (a[2] + b[2] + c[2]) / 3.0};
                triangle_normal(a, b, c, cur_normal);
                if ((sphere->position[0] - centroid[0]) * cur_normal[0] +
                        (sphere->position[1] - centroid[1]) * cur_normal[1] +
                        (sphere->position[2] - centroid[2]) * cur_normal[2] <
                    0.0) {
                    cur_normal[0] = -cur_normal[0];
                    cur_normal[1] = -cur_normal[1];
                    cur_normal[2] = -cur_normal[2];
                }
            }
            if (cur_depth > best_depth) {
                best_depth = cur_depth;
                vec3_copy(best_normal, cur_normal);
            }
        }
    }
mesh_sphere_done:
    if (best_depth <= 0.0)
        return 0;
    *depth = best_depth;
    vec3_copy(normal, best_normal);
    return 1;
}

/// @brief Capsule-vs-mesh narrow phase via adaptive sphere samples along the capsule axis.
///
/// Samples at radius-relative spacing instead of a fixed small count, which
/// avoids missing side contacts on long capsules.
static int test_meshlike_capsule(rt_mesh3d *mesh,
                                 const rt_collider_pose *mesh_pose,
                                 const rt_body3d *capsule,
                                 double *normal,
                                 double *depth) {
    double half_axis = fmax(capsule->height * 0.5 - capsule->radius, 0.0);
    double axis_a[3], axis_b[3];
    double best_depth = 0.0;
    double best_normal[3] = {0.0, 1.0, 0.0};
    int samples;
    capsule_axis_endpoints(capsule, axis_a, axis_b);
    {
        double axis_delta[3];
        vec3_sub(axis_b, axis_a, axis_delta);
        half_axis = vec3_len(axis_delta);
    }
    samples = capsule_axis_sample_count(half_axis, capsule->radius);
    for (int i = 0; i < samples; ++i) {
        double t = samples == 1 ? 0.5 : (double)i / (double)(samples - 1);
        rt_body3d sphere;
        memset(&sphere, 0, sizeof(sphere));
        sphere.shape = PH3D_SHAPE_SPHERE;
        sphere.position[0] = axis_a[0] + (axis_b[0] - axis_a[0]) * t;
        sphere.position[1] = axis_a[1] + (axis_b[1] - axis_a[1]) * t;
        sphere.position[2] = axis_a[2] + (axis_b[2] - axis_a[2]) * t;
        sphere.radius = capsule->radius;
        {
            double cur_normal[3];
            double cur_depth;
            if (test_meshlike_sphere(mesh, mesh_pose, &sphere, cur_normal, &cur_depth) &&
                cur_depth > best_depth) {
                best_depth = cur_depth;
                vec3_copy(best_normal, cur_normal);
            }
        }
    }
    if (best_depth <= 0.0)
        return 0;
    *depth = best_depth;
    vec3_copy(normal, best_normal);
    return 1;
}

/// @brief Initialize a collider pose (identity scale) from a body's position and
///   orientation — used to bring a body proxy into the SAT routines.
static void body_pose_from_proxy(const rt_body3d *body, rt_collider_pose *pose) {
    collider_pose_identity(pose);
    if (!body)
        return;
    vec3_copy(pose->position, body->position);
    pose->rotation[0] = body->orientation[0];
    pose->rotation[1] = body->orientation[1];
    pose->rotation[2] = body->orientation[2];
    pose->rotation[3] = body->orientation[3];
}

/// @brief One SAT axis test for a triangle vs a box (in the box's local frame): project
///   both onto the normalized @p axis_in; returns 0 if they are separated, else 1 and
///   updates the running minimum-overlap axis/sign. Zero-length axes are skipped (return 1).
static int test_triangle_box_local_axis(const double tri[3][3],
                                        const double he[3],
                                        const double axis_in[3],
                                        double *best_depth,
                                        double *best_axis,
                                        double *best_sign) {
    double axis[3] = {axis_in[0], axis_in[1], axis_in[2]};
    double len = vec3_normalize_in_place(axis);
    double min_p;
    double max_p;
    double radius;
    double overlap;
    if (len <= 1e-10)
        return 1;
    min_p = max_p = vec3_dot(tri[0], axis);
    for (int i = 1; i < 3; i++) {
        double p = vec3_dot(tri[i], axis);
        if (p < min_p)
            min_p = p;
        if (p > max_p)
            max_p = p;
    }
    radius = he[0] * fabs(axis[0]) + he[1] * fabs(axis[1]) + he[2] * fabs(axis[2]);
    if (max_p < -radius || min_p > radius)
        return 0;
    overlap = fmin(radius - min_p, max_p + radius);
    if (overlap < *best_depth) {
        double centroid[3] = {(tri[0][0] + tri[1][0] + tri[2][0]) / 3.0,
                              (tri[0][1] + tri[1][1] + tri[2][1]) / 3.0,
                              (tri[0][2] + tri[1][2] + tri[2][2]) / 3.0};
        *best_depth = overlap;
        vec3_copy(best_axis, axis);
        *best_sign = vec3_dot(centroid, axis) <= 0.0 ? 1.0 : -1.0;
    }
    return 1;
}

/// @brief Full triangle-vs-oriented-box overlap test via the separating-axis theorem:
///   transforms the triangle into the box's local space and tests the 3 box axes, the
///   triangle normal, and the 9 edge cross-product axes. Returns 1 on overlap with the
///   world-space @p normal (pointing out of the box) and penetration @p depth.
static int test_triangle_box_obb(const double *world_a,
                                 const double *world_b,
                                 const double *world_c,
                                 const rt_body3d *box,
                                 double *normal,
                                 double *depth) {
    rt_collider_pose box_pose;
    double tri[3][3];
    double edge0[3], edge1[3], edge2[3];
    double tri_normal[3];
    double best_depth = DBL_MAX;
    double best_axis[3] = {0.0, 1.0, 0.0};
    double best_sign = 1.0;
    const double axes[3][3] = {{1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}};

    if (!world_a || !world_b || !world_c || !box || !normal || !depth)
        return 0;
    body_pose_from_proxy(box, &box_pose);
    transform_point_to_local(&box_pose, world_a, tri[0]);
    transform_point_to_local(&box_pose, world_b, tri[1]);
    transform_point_to_local(&box_pose, world_c, tri[2]);

    vec3_sub(tri[1], tri[0], edge0);
    vec3_sub(tri[2], tri[1], edge1);
    vec3_sub(tri[0], tri[2], edge2);
    vec3_cross(edge0, edge1, tri_normal);

    for (int i = 0; i < 3; i++) {
        if (!test_triangle_box_local_axis(
                tri, box->half_extents, axes[i], &best_depth, best_axis, &best_sign))
            return 0;
    }
    if (!test_triangle_box_local_axis(
            tri, box->half_extents, tri_normal, &best_depth, best_axis, &best_sign))
        return 0;
    for (int e = 0; e < 3; e++) {
        const double *edge = e == 0 ? edge0 : (e == 1 ? edge1 : edge2);
        for (int a = 0; a < 3; a++) {
            double axis[3];
            vec3_cross(edge, axes[a], axis);
            if (!test_triangle_box_local_axis(
                    tri, box->half_extents, axis, &best_depth, best_axis, &best_sign))
                return 0;
        }
    }

    best_axis[0] *= best_sign;
    best_axis[1] *= best_sign;
    best_axis[2] *= best_sign;
    quat_rotate_vec3(box_pose.rotation, best_axis, normal);
    if (vec3_normalize_in_place(normal) <= 1e-12)
        vec3_set(normal, 0.0, 1.0, 0.0);
    *depth = best_depth == DBL_MAX ? 0.0 : best_depth;
    return 1;
}

/// @brief Test a posed triangle mesh against a box body by traversing mesh BVH
///   candidates before the triangle-vs-OBB SAT test. Falls back to a full scan if
///   BVH traversal cannot complete.
static int test_meshlike_box(rt_mesh3d *mesh,
                             const rt_collider_pose *mesh_pose,
                             const rt_body3d *box,
                             double *normal,
                             double *depth) {
    double best_depth = 0.0;
    double best_normal[3] = {0.0, 1.0, 0.0};
    int hit = 0;
    if (!mesh || !mesh_pose || !box)
        return 0;
    rt_mesh3d_refresh_bounds(mesh);
    if (mesh_physics_bvh_rebuild(mesh)) {
        const rt_physics_mesh_bvh_node *nodes =
            (const rt_physics_mesh_bvh_node *)mesh->physics_bvh_nodes;
        const uint32_t *tri_indices = mesh->physics_bvh_tri_indices;
        double box_min[3], box_max[3];
        int32_t stack[128];
        int32_t top = 0;
        int overflow = 0;
        body_aabb(box, box_min, box_max);
        stack[top++] = 0;
        while (top > 0) {
            int32_t node_index = stack[--top];
            const rt_physics_mesh_bvh_node *node;
            double node_min[3], node_max[3];
            if (node_index < 0 || node_index >= mesh->physics_bvh_node_count)
                continue;
            node = &nodes[node_index];
            transform_local_aabb_to_world(mesh_pose, node->min, node->max, node_min, node_max);
            if (!aabb_overlap_raw(node_min, node_max, box_min, box_max))
                continue;
            if (node->left >= 0 || node->right >= 0) {
                if (top + 2 > (int32_t)(sizeof(stack) / sizeof(stack[0]))) {
                    overflow = 1;
                    break;
                }
                if (node->right >= 0)
                    stack[top++] = node->right;
                if (node->left >= 0)
                    stack[top++] = node->left;
                continue;
            }
            for (int32_t item = node->start; item < node->start + node->count; ++item) {
                uint32_t tri = tri_indices[item];
                uint32_t i0 = mesh->indices[tri * 3u + 0u];
                uint32_t i1 = mesh->indices[tri * 3u + 1u];
                uint32_t i2 = mesh->indices[tri * 3u + 2u];
                double a[3], b[3], c[3], local[3], cur_normal[3], cur_depth;
                if (i0 >= mesh->vertex_count || i1 >= mesh->vertex_count ||
                    i2 >= mesh->vertex_count)
                    continue;
                local[0] = mesh->vertices[i0].pos[0];
                local[1] = mesh->vertices[i0].pos[1];
                local[2] = mesh->vertices[i0].pos[2];
                transform_point_from_pose(mesh_pose, local, a);
                local[0] = mesh->vertices[i1].pos[0];
                local[1] = mesh->vertices[i1].pos[1];
                local[2] = mesh->vertices[i1].pos[2];
                transform_point_from_pose(mesh_pose, local, b);
                local[0] = mesh->vertices[i2].pos[0];
                local[1] = mesh->vertices[i2].pos[1];
                local[2] = mesh->vertices[i2].pos[2];
                transform_point_from_pose(mesh_pose, local, c);
                if (test_triangle_box_obb(a, b, c, box, cur_normal, &cur_depth) &&
                    (!hit || cur_depth > best_depth)) {
                    best_depth = cur_depth;
                    vec3_copy(best_normal, cur_normal);
                    hit = 1;
                }
            }
        }
        if (!overflow)
            goto mesh_box_done;
        best_depth = 0.0;
        vec3_set(best_normal, 0.0, 1.0, 0.0);
        hit = 0;
    }
    for (uint32_t i = 0; i + 2 < mesh->index_count; i += 3) {
        uint32_t i0 = mesh->indices[i];
        uint32_t i1 = mesh->indices[i + 1];
        uint32_t i2 = mesh->indices[i + 2];
        double a[3], b[3], c[3], local[3], cur_normal[3], cur_depth;
        if (i0 >= mesh->vertex_count || i1 >= mesh->vertex_count || i2 >= mesh->vertex_count)
            continue;
        local[0] = mesh->vertices[i0].pos[0];
        local[1] = mesh->vertices[i0].pos[1];
        local[2] = mesh->vertices[i0].pos[2];
        transform_point_from_pose(mesh_pose, local, a);
        local[0] = mesh->vertices[i1].pos[0];
        local[1] = mesh->vertices[i1].pos[1];
        local[2] = mesh->vertices[i1].pos[2];
        transform_point_from_pose(mesh_pose, local, b);
        local[0] = mesh->vertices[i2].pos[0];
        local[1] = mesh->vertices[i2].pos[1];
        local[2] = mesh->vertices[i2].pos[2];
        transform_point_from_pose(mesh_pose, local, c);
        if (test_triangle_box_obb(a, b, c, box, cur_normal, &cur_depth) &&
            (!hit || cur_depth > best_depth)) {
            best_depth = cur_depth;
            vec3_copy(best_normal, cur_normal);
            hit = 1;
        }
    }
mesh_box_done:
    if (!hit)
        return 0;
    vec3_copy(normal, best_normal);
    *depth = best_depth;
    return 1;
}

typedef struct {
    double v[3];
} gjk_support_point;

typedef struct {
    gjk_support_point p[4];
    int32_t count;
} gjk_simplex;

typedef struct {
    int32_t a;
    int32_t b;
    int32_t c;
    double normal[3];
    double distance;
    int8_t active;
} epa_face;

typedef struct {
    int32_t a;
    int32_t b;
} epa_edge;

typedef void (*ph3d_gjk_support_fn)(void *ctx, const double *direction, gjk_support_point *out);

typedef struct {
    void *a_collider;
    const rt_collider_pose *a_pose;
    void *b_collider;
    const rt_collider_pose *b_pose;
} ph3d_collider_pair_support_ctx;

typedef struct {
    double tri[3][3];
    void *hull_collider;
    const rt_collider_pose *hull_pose;
} ph3d_triangle_hull_support_ctx;

/// @brief Triple cross product out = (a × b) × c, with a stable fallback when it degenerates.
/// @details GJK uses this to get a direction lying in an edge's plane and pointing toward the
/// origin;
///          when the inputs are collinear (near-zero result) it substitutes an arbitrary
///          perpendicular.
static void vec3_triple_cross(const double *a, const double *b, const double *c, double *out) {
    double tmp[3];
    vec3_cross(a, b, tmp);
    vec3_cross(tmp, c, out);
    if (vec3_len_sq(out) <= 1e-18) {
        double fallback[3] = {fabs(a[0]) < 0.9 ? 1.0 : 0.0, fabs(a[0]) < 0.9 ? 0.0 : 1.0, 0.0};
        vec3_cross(a, fallback, out);
        if (vec3_normalize_in_place(out) <= 1e-12)
            vec3_set(out, 0.0, 1.0, 0.0);
    }
}

/// @brief Minkowski-difference support point: support(A, dir) − support(B, −dir).
/// @details The farthest point of the configuration-space obstacle (A ⊖ B) along @p direction —
///          the single primitive GJK and EPA query to probe the shapes' combined geometry.
static void gjk_support(void *a_collider,
                        const rt_collider_pose *a_pose,
                        void *b_collider,
                        const rt_collider_pose *b_pose,
                        const double *direction,
                        gjk_support_point *out) {
    double pa[3];
    double pb[3];
    double inv_dir[3];
    vec3_negate(direction, inv_dir);
    collider_support_point(a_collider, a_pose, direction, pa);
    collider_support_point(b_collider, b_pose, inv_dir, pb);
    vec3_sub(pa, pb, out->v);
}

static void gjk_support_collider_pair(void *ctx, const double *direction, gjk_support_point *out) {
    ph3d_collider_pair_support_ctx *pair = (ph3d_collider_pair_support_ctx *)ctx;
    if (!pair || !out)
        return;
    gjk_support(pair->a_collider, pair->a_pose, pair->b_collider, pair->b_pose, direction, out);
}

static void triangle_support_point(const double tri[3][3], const double *direction, double *out) {
    double best_dot = -DBL_MAX;
    int best = 0;
    for (int i = 0; i < 3; ++i) {
        double d = vec3_dot(tri[i], direction);
        if (d > best_dot) {
            best_dot = d;
            best = i;
        }
    }
    vec3_copy(out, tri[best]);
}

static void gjk_support_triangle_hull(void *ctx, const double *direction, gjk_support_point *out) {
    ph3d_triangle_hull_support_ctx *pair = (ph3d_triangle_hull_support_ctx *)ctx;
    double tri_point[3];
    double hull_point[3];
    double inv_dir[3];
    if (!pair || !direction || !out)
        return;
    vec3_negate(direction, inv_dir);
    triangle_support_point(pair->tri, direction, tri_point);
    collider_support_point(pair->hull_collider, pair->hull_pose, inv_dir, hull_point);
    vec3_sub(tri_point, hull_point, out->v);
}

/// @brief Reduce the simplex to the single vertex @p a.
static void gjk_simplex_set1(gjk_simplex *simplex, const gjk_support_point *a) {
    simplex->p[0] = *a;
    simplex->count = 1;
}

/// @brief Set the simplex to the line segment (b, a) with @p a as the most-recent vertex.
static void gjk_simplex_set2(gjk_simplex *simplex,
                             const gjk_support_point *b,
                             const gjk_support_point *a) {
    simplex->p[0] = *b;
    simplex->p[1] = *a;
    simplex->count = 2;
}

/// @brief Set the simplex to the triangle (c, b, a) with @p a as the most-recent vertex.
static void gjk_simplex_set3(gjk_simplex *simplex,
                             const gjk_support_point *c,
                             const gjk_support_point *b,
                             const gjk_support_point *a) {
    simplex->p[0] = *c;
    simplex->p[1] = *b;
    simplex->p[2] = *a;
    simplex->count = 3;
}

/// @brief Evolve a 2-point (line) simplex toward the origin, updating the search @p direction.
/// @details Keeps the edge if the origin projects onto it (search perpendicular toward origin),
///          else drops back to the newest vertex. Returns 0 (origin not yet enclosed).
static int gjk_update_line(gjk_simplex *simplex, double *direction) {
    gjk_support_point a = simplex->p[simplex->count - 1];
    gjk_support_point b = simplex->p[simplex->count - 2];
    double ao[3];
    double ab[3];
    vec3_negate(a.v, ao);
    vec3_sub(b.v, a.v, ab);
    if (vec3_dot(ab, ao) > 0.0) {
        vec3_triple_cross(ab, ao, ab, direction);
    } else {
        gjk_simplex_set1(simplex, &a);
        vec3_copy(direction, ao);
    }
    return 0;
}

/// @brief Evolve a 3-point (triangle) simplex toward the origin, updating the search @p direction.
/// @details Tests the origin against the triangle's edge regions and the two face half-spaces,
///          reducing to the relevant edge or keeping the triangle (with the face normal toward the
///          origin) as the next search direction. Returns 0 (origin not yet enclosed).
static int gjk_update_triangle(gjk_simplex *simplex, double *direction) {
    gjk_support_point a = simplex->p[2];
    gjk_support_point b = simplex->p[1];
    gjk_support_point c = simplex->p[0];
    double ao[3];
    double ab[3];
    double ac[3];
    double abc[3];
    double edge_perp[3];

    vec3_negate(a.v, ao);
    vec3_sub(b.v, a.v, ab);
    vec3_sub(c.v, a.v, ac);
    vec3_cross(ab, ac, abc);

    vec3_cross(abc, ac, edge_perp);
    if (vec3_dot(edge_perp, ao) > 0.0) {
        if (vec3_dot(ac, ao) > 0.0) {
            gjk_simplex_set2(simplex, &c, &a);
            vec3_triple_cross(ac, ao, ac, direction);
        } else {
            gjk_simplex_set2(simplex, &b, &a);
            return gjk_update_line(simplex, direction);
        }
        return 0;
    }

    vec3_cross(ab, abc, edge_perp);
    if (vec3_dot(edge_perp, ao) > 0.0) {
        gjk_simplex_set2(simplex, &b, &a);
        return gjk_update_line(simplex, direction);
    }

    if (vec3_dot(abc, ao) > 0.0) {
        vec3_copy(direction, abc);
    } else {
        gjk_simplex_set3(simplex, &b, &c, &a);
        vec3_negate(abc, direction);
    }
    return 0;
}

/// @brief Compute the unit normal of face (a, b, c) oriented to point away from @p opposite.
/// @details Used per-face in the tetrahedron test so each face normal points outward; degenerate
///          faces fall back to +Y.
static void gjk_face_normal_away_from_point(const gjk_support_point *a,
                                            const gjk_support_point *b,
                                            const gjk_support_point *c,
                                            const gjk_support_point *opposite,
                                            double *normal) {
    double ab[3];
    double ac[3];
    double ao[3];
    vec3_sub(b->v, a->v, ab);
    vec3_sub(c->v, a->v, ac);
    vec3_cross(ab, ac, normal);
    if (vec3_normalize_in_place(normal) <= 1e-12) {
        vec3_set(normal, 0.0, 1.0, 0.0);
        return;
    }
    vec3_sub(opposite->v, a->v, ao);
    if (vec3_dot(normal, ao) > 0.0)
        vec3_negate(normal, normal);
}

/// @brief Evolve a 4-point (tetrahedron) simplex; report whether it encloses the origin.
/// @details Checks the origin against each of the three faces touching the newest vertex; if it
/// lies
///          outside one, drops to that face and continues. If inside all three, the origin is
///          enclosed.
/// @return 1 if the origin is inside the tetrahedron (collision), 0 to keep searching.
static int gjk_update_tetrahedron(gjk_simplex *simplex, double *direction) {
    gjk_support_point a = simplex->p[3];
    gjk_support_point b = simplex->p[2];
    gjk_support_point c = simplex->p[1];
    gjk_support_point d = simplex->p[0];
    double ao[3];
    double normal[3];
    vec3_negate(a.v, ao);

    gjk_face_normal_away_from_point(&a, &b, &c, &d, normal);
    if (vec3_dot(normal, ao) > 0.0) {
        gjk_simplex_set3(simplex, &c, &b, &a);
        vec3_copy(direction, normal);
        return 0;
    }

    gjk_face_normal_away_from_point(&a, &c, &d, &b, normal);
    if (vec3_dot(normal, ao) > 0.0) {
        gjk_simplex_set3(simplex, &d, &c, &a);
        vec3_copy(direction, normal);
        return 0;
    }

    gjk_face_normal_away_from_point(&a, &d, &b, &c, normal);
    if (vec3_dot(normal, ao) > 0.0) {
        gjk_simplex_set3(simplex, &b, &d, &a);
        vec3_copy(direction, normal);
        return 0;
    }

    return 1;
}

/// @brief Advance the GJK simplex one step by dispatching on its current vertex count.
/// @return 1 when the simplex (a tetrahedron) encloses the origin, else 0.
static int gjk_update_simplex(gjk_simplex *simplex, double *direction) {
    if (!simplex || !direction)
        return 0;
    if (simplex->count == 2)
        return gjk_update_line(simplex, direction);
    if (simplex->count == 3)
        return gjk_update_triangle(simplex, direction);
    if (simplex->count == 4)
        return gjk_update_tetrahedron(simplex, direction);
    if (simplex->count == 1)
        vec3_negate(simplex->p[0].v, direction);
    return 0;
}

/// @brief Build an EPA polytope face from three vertex indices, oriented outward from the origin.
/// @details Computes the unit normal and its signed distance from the origin; flips the winding so
///          the normal faces away from the origin (distance >= 0). Returns 0 for a degenerate face.
static int epa_make_face(
    const gjk_support_point *vertices, int32_t a, int32_t b, int32_t c, epa_face *face) {
    double ab[3];
    double ac[3];
    if (!vertices || !face)
        return 0;
    face->a = a;
    face->b = b;
    face->c = c;
    face->active = 1;
    vec3_sub(vertices[b].v, vertices[a].v, ab);
    vec3_sub(vertices[c].v, vertices[a].v, ac);
    vec3_cross(ab, ac, face->normal);
    if (vec3_normalize_in_place(face->normal) <= 1e-12)
        return 0;
    face->distance = vec3_dot(face->normal, vertices[a].v);
    if (face->distance < 0.0) {
        int32_t tmp = face->b;
        face->b = face->c;
        face->c = tmp;
        vec3_negate(face->normal, face->normal);
        face->distance = -face->distance;
    }
    return 1;
}

/// @brief Add edge (a, b) to the EPA horizon, cancelling it against an existing reverse edge.
/// @details A directed edge shared by two removed faces appears once as (a,b) and once as (b,a);
///          cancelling those pairs leaves exactly the silhouette/horizon edges that the new faces
///          must close over. Caps the edge list to avoid unbounded growth.
static int epa_add_edge(epa_edge *edges, int32_t *edge_count, int32_t a, int32_t b) {
    if (!edges || !edge_count)
        return 0;
    for (int32_t i = 0; i < *edge_count; ++i) {
        if (edges[i].a == b && edges[i].b == a) {
            edges[i] = edges[--(*edge_count)];
            return 1;
        }
    }
    if (*edge_count >= 192)
        return 0;
    edges[*edge_count].a = a;
    edges[*edge_count].b = b;
    (*edge_count)++;
    return 1;
}

/// @brief Append an EPA face (a, b, c) to the polytope, skipping degenerate faces.
/// @return 0 only when the face array is full; 1 otherwise (including a skipped degenerate face).
static int epa_add_face(const gjk_support_point *vertices,
                        epa_face *faces,
                        int32_t *face_count,
                        int32_t a,
                        int32_t b,
                        int32_t c) {
    if (!faces || !face_count || *face_count >= 128)
        return 0;
    if (!epa_make_face(vertices, a, b, c, &faces[*face_count]))
        return 1;
    (*face_count)++;
    return 1;
}

/// @brief Run EPA on GJK's enclosing tetrahedron to find the penetration normal and depth.
/// @details Iteratively expands the polytope toward the Minkowski surface: each step takes a
/// support
///          point past the closest face, removes all faces it can "see", and re-closes the
///          resulting horizon, until the closest face stops moving (within @p tolerance). Writes
///          the outward contact @p normal and penetration @p depth.
/// @return 1 on success, 0 if the input simplex is not a tetrahedron or the polytope degenerates.
static int epa_solve_with_support(ph3d_gjk_support_fn support_fn,
                                  void *support_ctx,
                                  const gjk_simplex *simplex,
                                  double *normal,
                                  double *depth) {
    gjk_support_point vertices[64];
    epa_face faces[128];
    int32_t vertex_count = 4;
    int32_t face_count = 0;
    const double tolerance = 1e-6;
    if (!support_fn || !simplex || simplex->count < 4 || !normal || !depth)
        return 0;
    for (int32_t i = 0; i < 4; ++i)
        vertices[i] = simplex->p[i];
    if (!epa_add_face(vertices, faces, &face_count, 0, 1, 2) ||
        !epa_add_face(vertices, faces, &face_count, 0, 3, 1) ||
        !epa_add_face(vertices, faces, &face_count, 0, 2, 3) ||
        !epa_add_face(vertices, faces, &face_count, 1, 3, 2))
        return 0;

    for (int32_t iter = 0; iter < 48; ++iter) {
        int32_t best = -1;
        double best_distance = DBL_MAX;
        for (int32_t i = 0; i < face_count; ++i) {
            if (faces[i].active && faces[i].distance < best_distance) {
                best_distance = faces[i].distance;
                best = i;
            }
        }
        if (best < 0)
            return 0;

        gjk_support_point support;
        support_fn(support_ctx, faces[best].normal, &support);
        double support_distance = vec3_dot(support.v, faces[best].normal);
        if (support_distance - faces[best].distance <= tolerance) {
            vec3_copy(normal, faces[best].normal);
            if (vec3_normalize_in_place(normal) <= 1e-12)
                vec3_set(normal, 0.0, 1.0, 0.0);
            *depth = faces[best].distance;
            if (*depth < 0.0)
                *depth = 0.0;
            return 1;
        }

        if (vertex_count >= 64)
            break;
        int32_t new_vertex = vertex_count++;
        vertices[new_vertex] = support;

        epa_edge edges[192];
        int32_t edge_count = 0;
        for (int32_t i = 0; i < face_count; ++i) {
            double to_support[3];
            if (!faces[i].active)
                continue;
            vec3_sub(support.v, vertices[faces[i].a].v, to_support);
            if (vec3_dot(faces[i].normal, to_support) > tolerance) {
                faces[i].active = 0;
                if (!epa_add_edge(edges, &edge_count, faces[i].a, faces[i].b) ||
                    !epa_add_edge(edges, &edge_count, faces[i].b, faces[i].c) ||
                    !epa_add_edge(edges, &edge_count, faces[i].c, faces[i].a))
                    return 0;
            }
        }
        for (int32_t i = 0; i < edge_count; ++i) {
            if (!epa_add_face(vertices, faces, &face_count, edges[i].a, edges[i].b, new_vertex))
                return 0;
        }
    }
    return 0;
}

static int epa_solve(void *a_collider,
                     const rt_collider_pose *a_pose,
                     void *b_collider,
                     const rt_collider_pose *b_pose,
                     const gjk_simplex *simplex,
                     double *normal,
                     double *depth) {
    ph3d_collider_pair_support_ctx ctx = {a_collider, a_pose, b_collider, b_pose};
    return epa_solve_with_support(gjk_support_collider_pair, &ctx, simplex, normal, depth);
}

static int gjk_epa_with_support(ph3d_gjk_support_fn support_fn,
                                void *support_ctx,
                                const double *seed_direction,
                                double *normal,
                                double *depth) {
    gjk_simplex simplex;
    double direction[3];
    gjk_support_point support;
    if (!support_fn || !normal || !depth)
        return 0;
    memset(&simplex, 0, sizeof(simplex));
    if (seed_direction)
        vec3_copy(direction, seed_direction);
    else
        vec3_set(direction, 1.0, 0.0, 0.0);
    if (vec3_normalize_in_place(direction) <= 1e-12)
        vec3_set(direction, 1.0, 0.0, 0.0);

    support_fn(support_ctx, direction, &support);
    gjk_simplex_set1(&simplex, &support);
    vec3_negate(support.v, direction);
    if (vec3_normalize_in_place(direction) <= 1e-12)
        vec3_set(direction, 0.0, 1.0, 0.0);

    for (int32_t iter = 0; iter < 32; ++iter) {
        support_fn(support_ctx, direction, &support);
        if (vec3_dot(support.v, direction) < 1e-9)
            return 0;
        if (simplex.count >= 4)
            return 0;
        simplex.p[simplex.count++] = support;
        if (gjk_update_simplex(&simplex, direction)) {
            if (epa_solve_with_support(support_fn, support_ctx, &simplex, normal, depth))
                return 1;
            if (seed_direction) {
                vec3_copy(normal, seed_direction);
                if (vec3_normalize_in_place(normal) <= 1e-12)
                    vec3_set(normal, 0.0, 1.0, 0.0);
            } else {
                vec3_set(normal, 0.0, 1.0, 0.0);
            }
            *depth = 0.0;
            return 1;
        }
        if (vec3_normalize_in_place(direction) <= 1e-12)
            vec3_set(direction, 1.0, 0.0, 0.0);
    }
    return 0;
}

/// @brief Test two convex colliders for overlap and, when overlapping, report the contact via EPA.
/// @details Runs GJK seeded from the center-to-center direction until it builds an origin-enclosing
///          tetrahedron (or an iteration cap proves separation), then hands that simplex to EPA for
///          the penetration @p normal and @p depth.
/// @return 1 if the shapes overlap (normal/depth written), 0 if separate.
static int test_convex_hull_gjk_epa(void *a_collider,
                                    const rt_collider_pose *a_pose,
                                    void *b_collider,
                                    const rt_collider_pose *b_pose,
                                    const double *a_center,
                                    const double *b_center,
                                    double *normal,
                                    double *depth) {
    gjk_simplex simplex;
    double direction[3];
    gjk_support_point support;
    if (!a_collider || !a_pose || !b_collider || !b_pose || !normal || !depth)
        return 0;
    memset(&simplex, 0, sizeof(simplex));
    if (a_center && b_center)
        vec3_sub(b_center, a_center, direction);
    else
        vec3_set(direction, 1.0, 0.0, 0.0);
    if (vec3_normalize_in_place(direction) <= 1e-12)
        vec3_set(direction, 1.0, 0.0, 0.0);

    gjk_support(a_collider, a_pose, b_collider, b_pose, direction, &support);
    gjk_simplex_set1(&simplex, &support);
    vec3_negate(support.v, direction);
    if (vec3_normalize_in_place(direction) <= 1e-12)
        vec3_set(direction, 0.0, 1.0, 0.0);

    for (int32_t iter = 0; iter < 32; ++iter) {
        gjk_support(a_collider, a_pose, b_collider, b_pose, direction, &support);
        if (vec3_dot(support.v, direction) < 1e-9)
            return 0;
        if (simplex.count >= 4)
            return 0;
        simplex.p[simplex.count++] = support;
        if (gjk_update_simplex(&simplex, direction)) {
            if (epa_solve(a_collider, a_pose, b_collider, b_pose, &simplex, normal, depth)) {
                if (a_center && b_center) {
                    double center_delta[3];
                    vec3_sub(b_center, a_center, center_delta);
                    if (vec3_dot(normal, center_delta) < 0.0)
                        vec3_negate(normal, normal);
                }
                return 1;
            }
            if (a_center && b_center) {
                vec3_sub(b_center, a_center, normal);
                if (vec3_normalize_in_place(normal) <= 1e-12)
                    vec3_set(normal, 0.0, 1.0, 0.0);
            } else {
                vec3_set(normal, 0.0, 1.0, 0.0);
            }
            *depth = 0.0;
            return 1;
        }
        if (vec3_normalize_in_place(direction) <= 1e-12)
            vec3_set(direction, 1.0, 0.0, 0.0);
    }
    return 0;
}

static int test_triangle_convex_hull_gjk_epa(const double tri[3][3],
                                             void *hull_collider,
                                             const rt_collider_pose *hull_pose,
                                             const double *hull_center,
                                             double *normal,
                                             double *depth) {
    ph3d_triangle_hull_support_ctx ctx;
    double centroid[3] = {(tri[0][0] + tri[1][0] + tri[2][0]) / 3.0,
                          (tri[0][1] + tri[1][1] + tri[2][1]) / 3.0,
                          (tri[0][2] + tri[1][2] + tri[2][2]) / 3.0};
    double seed[3];
    if (!tri || !hull_collider || !hull_pose || !normal || !depth)
        return 0;
    memcpy(ctx.tri, tri, sizeof(ctx.tri));
    ctx.hull_collider = hull_collider;
    ctx.hull_pose = hull_pose;
    if (hull_center)
        vec3_sub(hull_center, centroid, seed);
    else
        vec3_set(seed, 0.0, 1.0, 0.0);
    if (!gjk_epa_with_support(gjk_support_triangle_hull, &ctx, seed, normal, depth))
        return 0;
    if (hull_center) {
        double delta[3];
        vec3_sub(hull_center, centroid, delta);
        if (vec3_dot(normal, delta) < 0.0)
            vec3_negate(normal, normal);
    }
    return 1;
}

/// @brief Mesh-vs-convex-hull narrow phase: broad mesh nodes by the hull AABB,
///   then use GJK/EPA against each candidate triangle.
static int test_meshlike_convex_hull(rt_mesh3d *mesh,
                                     const rt_collider_pose *mesh_pose,
                                     void *hull_collider,
                                     const rt_collider_pose *hull_pose,
                                     double *normal,
                                     double *depth) {
    double hull_min[3], hull_max[3];
    double hull_center[3];
    double best_depth = 0.0;
    double best_normal[3] = {0.0, 1.0, 0.0};
    int hit = 0;
    if (!mesh || !mesh_pose || !hull_collider || !hull_pose)
        return 0;
    rt_collider3d_compute_world_aabb_raw(hull_collider,
                                         hull_pose->position,
                                         hull_pose->rotation,
                                         hull_pose->scale,
                                         hull_min,
                                         hull_max);
    hull_center[0] = (hull_min[0] + hull_max[0]) * 0.5;
    hull_center[1] = (hull_min[1] + hull_max[1]) * 0.5;
    hull_center[2] = (hull_min[2] + hull_max[2]) * 0.5;
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
            double node_min[3], node_max[3];
            if (node_index < 0 || node_index >= mesh->physics_bvh_node_count)
                continue;
            node = &nodes[node_index];
            transform_local_aabb_to_world(mesh_pose, node->min, node->max, node_min, node_max);
            if (!aabb_overlap_raw(node_min, node_max, hull_min, hull_max))
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
            for (int32_t item = node->start; item < node->start + node->count; ++item) {
                uint32_t tri_index = tri_indices[item];
                uint32_t i0 = mesh->indices[tri_index * 3u + 0u];
                uint32_t i1 = mesh->indices[tri_index * 3u + 1u];
                uint32_t i2 = mesh->indices[tri_index * 3u + 2u];
                double tri[3][3];
                double local[3];
                double cur_normal[3];
                double cur_depth;
                if (i0 >= mesh->vertex_count || i1 >= mesh->vertex_count ||
                    i2 >= mesh->vertex_count)
                    continue;
                local[0] = mesh->vertices[i0].pos[0];
                local[1] = mesh->vertices[i0].pos[1];
                local[2] = mesh->vertices[i0].pos[2];
                transform_point_from_pose(mesh_pose, local, tri[0]);
                local[0] = mesh->vertices[i1].pos[0];
                local[1] = mesh->vertices[i1].pos[1];
                local[2] = mesh->vertices[i1].pos[2];
                transform_point_from_pose(mesh_pose, local, tri[1]);
                local[0] = mesh->vertices[i2].pos[0];
                local[1] = mesh->vertices[i2].pos[1];
                local[2] = mesh->vertices[i2].pos[2];
                transform_point_from_pose(mesh_pose, local, tri[2]);
                if (test_triangle_convex_hull_gjk_epa(
                        tri, hull_collider, hull_pose, hull_center, cur_normal, &cur_depth) &&
                    (!hit || cur_depth > best_depth)) {
                    best_depth = cur_depth;
                    vec3_copy(best_normal, cur_normal);
                    hit = 1;
                }
            }
        }
        if (!overflow)
            goto mesh_hull_done;
        best_depth = 0.0;
        vec3_set(best_normal, 0.0, 1.0, 0.0);
        hit = 0;
    }

    for (uint32_t i = 0; i + 2 < mesh->index_count; i += 3) {
        uint32_t i0 = mesh->indices[i];
        uint32_t i1 = mesh->indices[i + 1u];
        uint32_t i2 = mesh->indices[i + 2u];
        double tri[3][3];
        double local[3];
        double cur_normal[3];
        double cur_depth;
        if (i0 >= mesh->vertex_count || i1 >= mesh->vertex_count || i2 >= mesh->vertex_count)
            continue;
        local[0] = mesh->vertices[i0].pos[0];
        local[1] = mesh->vertices[i0].pos[1];
        local[2] = mesh->vertices[i0].pos[2];
        transform_point_from_pose(mesh_pose, local, tri[0]);
        local[0] = mesh->vertices[i1].pos[0];
        local[1] = mesh->vertices[i1].pos[1];
        local[2] = mesh->vertices[i1].pos[2];
        transform_point_from_pose(mesh_pose, local, tri[1]);
        local[0] = mesh->vertices[i2].pos[0];
        local[1] = mesh->vertices[i2].pos[1];
        local[2] = mesh->vertices[i2].pos[2];
        transform_point_from_pose(mesh_pose, local, tri[2]);
        if (test_triangle_convex_hull_gjk_epa(
                tri, hull_collider, hull_pose, hull_center, cur_normal, &cur_depth) &&
            (!hit || cur_depth > best_depth)) {
            best_depth = cur_depth;
            vec3_copy(best_normal, cur_normal);
            hit = 1;
        }
    }

mesh_hull_done:
    if (!hit)
        return 0;
    vec3_copy(normal, best_normal);
    *depth = best_depth;
    return 1;
}

/// @brief Sphere-vs-heightfield narrow phase.
///
/// Transforms the sphere center into local heightfield space, samples
/// the field at (x, z) to get surface height + local normal, and tests
/// whether the sphere bottom is below the surface. Skips the cost of
/// per-cell triangle intersection by using the heightfield's analytical
/// normal directly.
static int test_heightfield_sphere(void *heightfield,
                                   const rt_collider_pose *field_pose,
                                   const rt_body3d *sphere,
                                   double *normal,
                                   double *depth) {
    double local_center[3];
    double surface_height = 0.0;
    double local_normal[3] = {0.0, 1.0, 0.0};
    double scale_y;
    double local_radius_y;
    double penetration;
    transform_point_to_local(field_pose, sphere->position, local_center);
    if (!rt_collider3d_sample_heightfield_raw(
            heightfield, local_center[0], local_center[2], &surface_height, local_normal))
        return 0;
    scale_y = pose_abs_scale_or_unit(field_pose->scale[1]);
    local_radius_y = sphere->radius / scale_y;
    penetration = surface_height - (local_center[1] - local_radius_y);
    if (penetration <= 0.0)
        return 0;
    transform_normal_from_local(field_pose, local_normal, normal);
    *depth = penetration * scale_y;
    return 1;
}

/// @brief Capsule-vs-heightfield narrow phase.
///
/// Samples along the oriented capsule axis and keeps the deepest hit.
static int test_heightfield_capsule(void *heightfield,
                                    const rt_collider_pose *field_pose,
                                    const rt_body3d *capsule,
                                    double *normal,
                                    double *depth) {
    double half_axis = fmax(capsule->height * 0.5 - capsule->radius, 0.0);
    double axis_a[3], axis_b[3];
    double best_depth = 0.0;
    double best_normal[3] = {0.0, 1.0, 0.0};
    int samples = half_axis > 1e-9 ? 5 : 1;
    capsule_axis_endpoints(capsule, axis_a, axis_b);
    for (int i = 0; i < samples; ++i) {
        double t = samples == 1 ? 0.5 : (double)i / (double)(samples - 1);
        rt_body3d sphere;
        double cur_normal[3];
        double cur_depth;
        memset(&sphere, 0, sizeof(sphere));
        sphere.shape = PH3D_SHAPE_SPHERE;
        sphere.position[0] = axis_a[0] + (axis_b[0] - axis_a[0]) * t;
        sphere.position[1] = axis_a[1] + (axis_b[1] - axis_a[1]) * t;
        sphere.position[2] = axis_a[2] + (axis_b[2] - axis_a[2]) * t;
        sphere.radius = capsule->radius;
        if (test_heightfield_sphere(heightfield, field_pose, &sphere, cur_normal, &cur_depth) &&
            cur_depth > best_depth) {
            best_depth = cur_depth;
            vec3_copy(best_normal, cur_normal);
        }
    }
    if (best_depth <= 0.0)
        return 0;
    *depth = best_depth;
    vec3_copy(normal, best_normal);
    return 1;
}

/// @brief Box-vs-heightfield narrow phase via 3x3 bottom probe.
///
/// Samples the heightfield at the oriented box's bottom corners, edge midpoints,
/// and center; the deepest penetration wins. The edge samples catch narrow ridges
/// that can pass between the old corner+center probe pattern.
static int test_heightfield_box(void *heightfield,
                                const rt_collider_pose *field_pose,
                                const rt_body3d *box,
                                double *normal,
                                double *depth) {
    rt_collider_pose box_pose;
    double samples[9][3];
    double best_depth = 0.0;
    double best_normal[3] = {0.0, 1.0, 0.0};
    double local_samples[9][3];
    int sample_count = 0;
    if (!box)
        return 0;
    body_pose_from_proxy(box, &box_pose);
    for (int ix = -1; ix <= 1; ++ix) {
        for (int iz = -1; iz <= 1; ++iz) {
            vec3_set(local_samples[sample_count++],
                     (double)ix * box->half_extents[0],
                     -box->half_extents[1],
                     (double)iz * box->half_extents[2]);
        }
    }
    for (int i = 0; i < sample_count; i++)
        transform_point_from_pose(&box_pose, local_samples[i], samples[i]);
    for (int i = 0; i < sample_count; ++i) {
        double local_point[3];
        double surface_height = 0.0;
        double local_normal[3] = {0.0, 1.0, 0.0};
        transform_point_to_local(field_pose, samples[i], local_point);
        if (!rt_collider3d_sample_heightfield_raw(
                heightfield, local_point[0], local_point[2], &surface_height, local_normal))
            continue;
        {
            double cur_depth = surface_height - local_point[1];
            if (cur_depth > best_depth) {
                best_depth = cur_depth;
                transform_normal_from_local(field_pose, local_normal, best_normal);
            }
        }
    }
    if (best_depth <= 0.0)
        return 0;
    {
        double len = vec3_len(best_normal);
        if (len > 1e-12) {
            best_normal[0] /= len;
            best_normal[1] /= len;
            best_normal[2] /= len;
        } else {
            best_normal[0] = 0.0;
            best_normal[1] = 1.0;
            best_normal[2] = 0.0;
        }
    }
    vec3_copy(normal, best_normal);
    *depth = best_depth * pose_abs_scale_or_unit(field_pose->scale[1]);
    return 1;
}

/// @brief Recursive collider-vs-collider dispatcher with leaf identification.
///
/// The "real" collision routine — handles every combination of:
///   - compound × anything (recurse into children)
///   - simple × simple (route via `test_simple_collision`)
///   - convex hull / mesh × simple and mesh × convex hull (route via BVH/GJK helpers)
///   - heightfield × simple (route via `test_heightfield_*`)
///   - everything else falls back to AABB-vs-AABB.
///
/// Outputs the leaf colliders that actually touched (so the higher-level
/// contact event can carry the precise child collider, not just the
/// outer compound). Reverses the normal when it has to swap A and B
/// for symmetric dispatch.
/// @brief Record the contributing leaf collider + pose for both bodies of a hit
///   (honoring NULL out-params) and report the pair as colliding. Centralizes the
///   identical bookkeeping every successful direct (non-swapped) dispatch case shares.
static int commit_pair_leaves(void **leaf_a_out,
                              rt_collider_pose *leaf_a_pose_out,
                              void *a_collider,
                              const rt_collider_pose *a_pose,
                              void **leaf_b_out,
                              rt_collider_pose *leaf_b_pose_out,
                              void *b_collider,
                              const rt_collider_pose *b_pose) {
    if (leaf_a_out)
        *leaf_a_out = a_collider;
    if (leaf_a_pose_out)
        *leaf_a_pose_out = *a_pose;
    if (leaf_b_out)
        *leaf_b_out = b_collider;
    if (leaf_b_pose_out)
        *leaf_b_pose_out = *b_pose;
    return 1;
}

static int test_collider_pair(const rt_body3d *a_body,
                              void *a_collider,
                              const rt_collider_pose *a_pose,
                              const rt_body3d *b_body,
                              void *b_collider,
                              const rt_collider_pose *b_pose,
                              double *normal,
                              double *depth,
                              void **leaf_a_out,
                              rt_collider_pose *leaf_a_pose_out,
                              void **leaf_b_out,
                              rt_collider_pose *leaf_b_pose_out);

/// @brief Collide a compound collider (the @p compound_is_a side) against the other collider by
///        testing each child shape and keeping the deepest penetration. Mirrors the two formerly
///        duplicated compound branches of test_collider_pair; recurses back into it per child.
static int test_compound_collider(const rt_body3d *a_body,
                                  void *a_collider,
                                  const rt_collider_pose *a_pose,
                                  const rt_body3d *b_body,
                                  void *b_collider,
                                  const rt_collider_pose *b_pose,
                                  int compound_is_a,
                                  double *normal,
                                  double *depth,
                                  void **leaf_a_out,
                                  rt_collider_pose *leaf_a_pose_out,
                                  void **leaf_b_out,
                                  rt_collider_pose *leaf_b_pose_out) {
    void *compound = compound_is_a ? a_collider : b_collider;
    const rt_collider_pose *compound_pose = compound_is_a ? a_pose : b_pose;
    double best_depth = 0.0;
    double best_normal[3] = {0.0, 1.0, 0.0};
    int hit = 0;
    int64_t child_count = rt_collider3d_get_child_count_raw(compound);
    for (int64_t i = 0; i < child_count; ++i) {
        void *child = rt_collider3d_get_child_raw(compound, i);
        double child_pos[3], child_rot[4], child_scale[3];
        rt_collider_pose child_pose;
        rt_collider3d_get_child_transform_raw(compound, i, child_pos, child_rot, child_scale);
        collider_pose_compose(compound_pose, child_pos, child_rot, child_scale, &child_pose);
        {
            double cur_normal[3];
            double cur_depth = 0.0;
            void *cur_leaf_a = NULL;
            void *cur_leaf_b = NULL;
            rt_collider_pose cur_leaf_a_pose;
            rt_collider_pose cur_leaf_b_pose;
            int sub = compound_is_a ? test_collider_pair(a_body,
                                                         child,
                                                         &child_pose,
                                                         b_body,
                                                         b_collider,
                                                         b_pose,
                                                         cur_normal,
                                                         &cur_depth,
                                                         &cur_leaf_a,
                                                         &cur_leaf_a_pose,
                                                         &cur_leaf_b,
                                                         &cur_leaf_b_pose)
                                    : test_collider_pair(a_body,
                                                         a_collider,
                                                         a_pose,
                                                         b_body,
                                                         child,
                                                         &child_pose,
                                                         cur_normal,
                                                         &cur_depth,
                                                         &cur_leaf_a,
                                                         &cur_leaf_a_pose,
                                                         &cur_leaf_b,
                                                         &cur_leaf_b_pose);
            if (sub && cur_depth > best_depth) {
                best_depth = cur_depth;
                vec3_copy(best_normal, cur_normal);
                if (leaf_a_out)
                    *leaf_a_out = cur_leaf_a;
                if (leaf_a_pose_out)
                    *leaf_a_pose_out = cur_leaf_a_pose;
                if (leaf_b_out)
                    *leaf_b_out = cur_leaf_b;
                if (leaf_b_pose_out)
                    *leaf_b_pose_out = cur_leaf_b_pose;
                hit = 1;
            }
        }
    }
    if (hit) {
        vec3_copy(normal, best_normal);
        *depth = best_depth;
    }
    return hit;
}

static int test_collider_pair(const rt_body3d *a_body,
                              void *a_collider,
                              const rt_collider_pose *a_pose,
                              const rt_body3d *b_body,
                              void *b_collider,
                              const rt_collider_pose *b_pose,
                              double *normal,
                              double *depth,
                              void **leaf_a_out,
                              rt_collider_pose *leaf_a_pose_out,
                              void **leaf_b_out,
                              rt_collider_pose *leaf_b_pose_out) {
    double amn[3], amx[3], bmn[3], bmx[3];
    double a_center[3], b_center[3];
    int64_t a_type;
    int64_t b_type;
    if (!a_collider || !b_collider)
        return 0;

    rt_collider3d_compute_world_aabb_raw(
        a_collider, a_pose->position, a_pose->rotation, a_pose->scale, amn, amx);
    rt_collider3d_compute_world_aabb_raw(
        b_collider, b_pose->position, b_pose->rotation, b_pose->scale, bmn, bmx);
    if (amn[0] > bmx[0] || amx[0] < bmn[0] || amn[1] > bmx[1] || amx[1] < bmn[1] ||
        amn[2] > bmx[2] || amx[2] < bmn[2])
        return 0;
    a_center[0] = (amn[0] + amx[0]) * 0.5;
    a_center[1] = (amn[1] + amx[1]) * 0.5;
    a_center[2] = (amn[2] + amx[2]) * 0.5;
    b_center[0] = (bmn[0] + bmx[0]) * 0.5;
    b_center[1] = (bmn[1] + bmx[1]) * 0.5;
    b_center[2] = (bmn[2] + bmx[2]) * 0.5;

    a_type = rt_collider3d_get_type(a_collider);
    b_type = rt_collider3d_get_type(b_collider);

    if (a_type == RT_COLLIDER3D_TYPE_COMPOUND)
        return test_compound_collider(a_body,
                                      a_collider,
                                      a_pose,
                                      b_body,
                                      b_collider,
                                      b_pose,
                                      1,
                                      normal,
                                      depth,
                                      leaf_a_out,
                                      leaf_a_pose_out,
                                      leaf_b_out,
                                      leaf_b_pose_out);

    if (b_type == RT_COLLIDER3D_TYPE_COMPOUND)
        return test_compound_collider(a_body,
                                      a_collider,
                                      a_pose,
                                      b_body,
                                      b_collider,
                                      b_pose,
                                      0,
                                      normal,
                                      depth,
                                      leaf_a_out,
                                      leaf_a_pose_out,
                                      leaf_b_out,
                                      leaf_b_pose_out);

    if (collider_type_is_simple(a_type) && collider_type_is_simple(b_type)) {
        if (!test_simple_collider_pose_collision(
                a_collider, a_pose, b_collider, b_pose, normal, depth))
            return 0;
        return commit_pair_leaves(leaf_a_out,
                                  leaf_a_pose_out,
                                  a_collider,
                                  a_pose,
                                  leaf_b_out,
                                  leaf_b_pose_out,
                                  b_collider,
                                  b_pose);
    }

    if (a_type == RT_COLLIDER3D_TYPE_CONVEX_HULL && collider_type_is_simple(b_type)) {
        if (!test_convex_hull_gjk_epa(
                a_collider, a_pose, b_collider, b_pose, a_center, b_center, normal, depth))
            return 0;
        return commit_pair_leaves(leaf_a_out,
                                  leaf_a_pose_out,
                                  a_collider,
                                  a_pose,
                                  leaf_b_out,
                                  leaf_b_pose_out,
                                  b_collider,
                                  b_pose);
    }

    if (a_type == RT_COLLIDER3D_TYPE_MESH && collider_type_is_simple(b_type)) {
        rt_body3d proxy_b;
        rt_mesh3d *mesh = (rt_mesh3d *)rt_collider3d_get_mesh_raw(a_collider);
        if (!build_simple_proxy(b_pose, b_collider, &proxy_b) || !mesh)
            return 0;
        if ((proxy_b.shape == PH3D_SHAPE_SPHERE &&
             !test_meshlike_sphere(mesh, a_pose, &proxy_b, normal, depth)) ||
            (proxy_b.shape == PH3D_SHAPE_CAPSULE &&
             !test_meshlike_capsule(mesh, a_pose, &proxy_b, normal, depth)) ||
            (proxy_b.shape == PH3D_SHAPE_AABB &&
             !test_meshlike_box(mesh, a_pose, &proxy_b, normal, depth)) ||
            ((proxy_b.shape != PH3D_SHAPE_SPHERE && proxy_b.shape != PH3D_SHAPE_CAPSULE &&
              proxy_b.shape != PH3D_SHAPE_AABB) &&
             !test_bounds_overlap(amn, amx, a_center, bmn, bmx, b_center, normal, depth))) {
            return 0;
        }
        return commit_pair_leaves(leaf_a_out,
                                  leaf_a_pose_out,
                                  a_collider,
                                  a_pose,
                                  leaf_b_out,
                                  leaf_b_pose_out,
                                  b_collider,
                                  b_pose);
    }

    if (a_type == RT_COLLIDER3D_TYPE_MESH && b_type == RT_COLLIDER3D_TYPE_CONVEX_HULL) {
        rt_mesh3d *mesh = (rt_mesh3d *)rt_collider3d_get_mesh_raw(a_collider);
        if (!mesh || !test_meshlike_convex_hull(mesh, a_pose, b_collider, b_pose, normal, depth))
            return 0;
        return commit_pair_leaves(leaf_a_out,
                                  leaf_a_pose_out,
                                  a_collider,
                                  a_pose,
                                  leaf_b_out,
                                  leaf_b_pose_out,
                                  b_collider,
                                  b_pose);
    }

    if (a_type == RT_COLLIDER3D_TYPE_CONVEX_HULL && b_type == RT_COLLIDER3D_TYPE_MESH) {
        int hit = test_collider_pair(b_body,
                                     b_collider,
                                     b_pose,
                                     a_body,
                                     a_collider,
                                     a_pose,
                                     normal,
                                     depth,
                                     leaf_b_out,
                                     leaf_b_pose_out,
                                     leaf_a_out,
                                     leaf_a_pose_out);
        if (hit) {
            normal[0] = -normal[0];
            normal[1] = -normal[1];
            normal[2] = -normal[2];
        }
        return hit;
    }

    if (collider_type_is_simple(a_type) &&
        (b_type == RT_COLLIDER3D_TYPE_CONVEX_HULL || b_type == RT_COLLIDER3D_TYPE_MESH)) {
        int hit;
        hit = test_collider_pair(b_body,
                                 b_collider,
                                 b_pose,
                                 a_body,
                                 a_collider,
                                 a_pose,
                                 normal,
                                 depth,
                                 leaf_b_out,
                                 leaf_b_pose_out,
                                 leaf_a_out,
                                 leaf_a_pose_out);
        if (hit) {
            normal[0] = -normal[0];
            normal[1] = -normal[1];
            normal[2] = -normal[2];
        }
        return hit;
    }

    if (a_type == RT_COLLIDER3D_TYPE_CONVEX_HULL && b_type == RT_COLLIDER3D_TYPE_CONVEX_HULL) {
        if (!test_convex_hull_gjk_epa(
                a_collider, a_pose, b_collider, b_pose, a_center, b_center, normal, depth))
            return 0;
        return commit_pair_leaves(leaf_a_out,
                                  leaf_a_pose_out,
                                  a_collider,
                                  a_pose,
                                  leaf_b_out,
                                  leaf_b_pose_out,
                                  b_collider,
                                  b_pose);
    }

    if (a_type == RT_COLLIDER3D_TYPE_HEIGHTFIELD && collider_type_is_simple(b_type)) {
        rt_body3d proxy_b;
        if (!build_simple_proxy(b_pose, b_collider, &proxy_b))
            return 0;
        if ((proxy_b.shape == PH3D_SHAPE_SPHERE &&
             !test_heightfield_sphere(a_collider, a_pose, &proxy_b, normal, depth)) ||
            (proxy_b.shape == PH3D_SHAPE_CAPSULE &&
             !test_heightfield_capsule(a_collider, a_pose, &proxy_b, normal, depth)) ||
            ((proxy_b.shape != PH3D_SHAPE_SPHERE && proxy_b.shape != PH3D_SHAPE_CAPSULE) &&
             !test_heightfield_box(a_collider, a_pose, &proxy_b, normal, depth))) {
            return 0;
        }
        return commit_pair_leaves(leaf_a_out,
                                  leaf_a_pose_out,
                                  a_collider,
                                  a_pose,
                                  leaf_b_out,
                                  leaf_b_pose_out,
                                  b_collider,
                                  b_pose);
    }

    if (collider_type_is_simple(a_type) && b_type == RT_COLLIDER3D_TYPE_HEIGHTFIELD) {
        int hit;
        hit = test_collider_pair(b_body,
                                 b_collider,
                                 b_pose,
                                 a_body,
                                 a_collider,
                                 a_pose,
                                 normal,
                                 depth,
                                 leaf_b_out,
                                 leaf_b_pose_out,
                                 leaf_a_out,
                                 leaf_a_pose_out);
        if (hit) {
            normal[0] = -normal[0];
            normal[1] = -normal[1];
            normal[2] = -normal[2];
        }
        return hit;
    }

    if (!test_bounds_overlap(amn, amx, a_center, bmn, bmx, b_center, normal, depth))
        return 0;
    return commit_pair_leaves(leaf_a_out,
                              leaf_a_pose_out,
                              a_collider,
                              a_pose,
                              leaf_b_out,
                              leaf_b_pose_out,
                              b_collider,
                              b_pose);
}

/// @brief Top-level body-vs-body collision test, including contact point.
///
/// Builds collider poses, delegates to `test_collider_pair` for the
/// actual recursion, then reconstructs an estimated contact point via
/// `compute_contact_point_from_leafs`. Outputs the leaf colliders that
/// touched, so contact events can carry sub-collider identity.
int test_collision(const rt_body3d *a,
                   const rt_body3d *b,
                   double *normal,
                   double *depth,
                   double *point,
                   void **leaf_a_out,
                   void **leaf_b_out,
                   rt_collider_pose *leaf_a_pose_out,
                   rt_collider_pose *leaf_b_pose_out) {
    rt_collider_pose a_pose;
    rt_collider_pose b_pose;
    rt_collider_pose leaf_a_pose;
    rt_collider_pose leaf_b_pose;
    void *leaf_a = NULL;
    void *leaf_b = NULL;
    if (!a || !b || !a->collider || !b->collider)
        return 0;
    collider_pose_from_body(a, &a_pose);
    collider_pose_from_body(b, &b_pose);
    if (!test_collider_pair(a,
                            a->collider,
                            &a_pose,
                            b,
                            b->collider,
                            &b_pose,
                            normal,
                            depth,
                            &leaf_a,
                            &leaf_a_pose,
                            &leaf_b,
                            &leaf_b_pose))
        return 0;
    if (leaf_a_out)
        *leaf_a_out = leaf_a;
    if (leaf_b_out)
        *leaf_b_out = leaf_b;
    if (leaf_a_pose_out)
        *leaf_a_pose_out = leaf_a_pose;
    if (leaf_b_pose_out)
        *leaf_b_pose_out = leaf_b_pose;
    if (point)
        compute_contact_point_from_leafs(leaf_a, &leaf_a_pose, leaf_b, &leaf_b_pose, normal, point);
    return 1;
}

#else
typedef int rt_physics3d_collision_disabled_tu_guard;
#endif /* VIPER_ENABLE_GRAPHICS */
