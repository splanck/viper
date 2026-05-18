//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_physics2d.c
// Purpose: Simple 2D rigid-body physics engine with AABB/circle collision detection
//   and impulse-based collision response. Designed for game use cases: enemies,
//   platforms, bullets, and other simple rectangular entities. Intentionally
//   not a general-purpose physics engine — correctness and simplicity are
//   favoured over feature completeness.
//
// Key invariants:
//   - Bodies are axis-aligned boxes (AABB) or circles. No rotational physics.
//   - Integration is symplectic Euler: forces → velocity, then velocity →
//     position, then collision resolution. Simple and stable for games.
//   - A body with mass == 0.0 is "static" (immovable). Its inv_mass is 0,
//     so impulse calculations produce zero delta-velocity for it.
//   - The body capacity per world is PH_MAX_BODIES (256). Exceeding this traps.
//   - Collision filtering uses 64-bit layer/mask bitmasks: bodies A and B
//     collide only when (A.layer & B.mask) && (B.layer & A.mask) are both
//     non-zero (bidirectional filter).
//   - Broad-phase uses a stack-local 8×8 uniform grid rebuilt each step.
//     The grid arrays live on the stack, making concurrent physics worlds safe.
//   - A 256×256 bit-matrix (pair_checked) ensures each candidate pair is
//     tested at most once per step, even when they share multiple grid cells.
//   - Positional correction uses the Baumgarte stabilisation technique with
//     a 1% slop and 40% correction factor to prevent sinking while avoiding
//     jitter.
//
// Ownership/Lifetime:
//   - World objects are GC-managed (rt_obj_new_i64). The world_finalizer
//     releases reference-counted bodies.
//   - Body objects are reference-counted: the world retains them on Add and
//     releases them on Remove or finalisation.
//   - Callers should call rt_physics2d_world_remove() before dropping a body
//     handle to avoid dangling references.
//
// Links: rt_physics2d.h (public API), docs/viperlib/game.md (usage guide)
//
//===----------------------------------------------------------------------===//

#include "rt_physics2d.h"
#include "rt_physics2d_internal.h"
#include "rt_physics2d_joint.h"

#include "rt_internal.h"
#include "rt_object.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

//=============================================================================
// Internal types
//=============================================================================

// Internal types are in rt_physics2d_internal.h

static int8_t aabb_overlap(rt_body_impl *a, rt_body_impl *b, double *nx, double *ny, double *pen);
static int8_t shape_overlap(rt_body_impl *a, rt_body_impl *b, double *nx, double *ny, double *pen);
static int8_t swept_bounds_pair(rt_body_impl *a, rt_body_impl *b, double *nx, double *ny, double *entry);
static void resolve_collision(rt_body_impl *a, rt_body_impl *b, double nx, double ny, double pen);

/// @brief Clear the world's per-step contact list (called at the start of
///        each physics step before broad/narrow-phase regenerates contacts).
static void world_clear_contacts(rt_world_impl *w) {
    if (!w)
        return;
    w->contact_count = 0;
    memset(w->contacts, 0, sizeof(w->contacts));
}

/// @brief Append a contact record to the world's per-step contact list.
/// @details Skips recording when the list is full (PH_MAX_CONTACTS) or when the
///   manifold values are non-finite, so downstream queries always see a clean
///   list even in degenerate numerical situations.  Penetration is clamped to
///   [0, +inf) because negative depth would indicate separation, not contact.
static void world_record_contact(
    rt_world_impl *w, rt_body_impl *a, rt_body_impl *b, double nx, double ny, double pen) {
    if (!w || !a || !b || w->contact_count >= PH_MAX_CONTACTS)
        return;
    if (!isfinite(nx) || !isfinite(ny) || !isfinite(pen))
        return;
    int32_t idx = w->contact_count++;
    w->contacts[idx].body_a = a;
    w->contacts[idx].body_b = b;
    w->contacts[idx].nx = nx;
    w->contacts[idx].ny = ny;
    w->contacts[idx].penetration = pen > 0.0 ? pen : 0.0;
}

/// @brief Return `value` if finite, otherwise `fallback`. Used for gravity and position setters.
static double finite_or(double value, double fallback) {
    return isfinite(value) ? value : fallback;
}

/// @brief Return `value` if finite and strictly positive, otherwise `fallback`.
/// @details Used for body dimensions (width, height) and mass to guarantee they
///   are always valid physics inputs — a zero or NaN dimension would make AABB
///   overlap tests degenerate.
static double positive_or(double value, double fallback) {
    return (isfinite(value) && value > 0.0) ? value : fallback;
}

/// @brief Clamp `value` to [0, 1], returning 0 for NaN/Inf. Used for restitution and friction.
static double clamp01(double value) {
    if (!isfinite(value))
        return 0.0;
    if (value < 0.0)
        return 0.0;
    if (value > 1.0)
        return 1.0;
    return value;
}

/// @brief Clamp `value` to [-limit, +limit], returning `fallback` for NaN/Inf.
/// @details Used by sanitize_body_state to cap position, velocity, and force magnitudes
///   to large but representable values, preventing IEEE infinity from propagating through
///   the integrator and corrupting the broad-phase grid bounds.
static double clamp_abs_finite(double value, double fallback, double limit) {
    if (!isfinite(value))
        return fallback;
    if (value > limit)
        return limit;
    if (value < -limit)
        return -limit;
    return value;
}

/// @brief Downcast a raw handle to rt_world_impl* after confirming its class ID.
/// @details Calls rt_physics2d_is_world_handle to verify the GC class-ID tag before
///   casting, trapping with `api` as the message on mismatch.  NULL input short-
///   circuits immediately without a trap so callers can chain checked_world checks
///   with early NULL guards.
static rt_world_impl *checked_world(void *obj, const char *api) {
    if (!obj)
        return NULL;
    if (!rt_physics2d_is_world_handle(obj)) {
        rt_trap(api);
        return NULL;
    }
    return (rt_world_impl *)obj;
}

/// @brief Downcast a raw handle to rt_body_impl* after confirming its class ID.
/// @details Mirror of checked_world — verifies the GC class-ID is the physics-body
///   sentinel before casting, trapping with `api` on mismatch.
static rt_body_impl *checked_body(void *obj, const char *api) {
    if (!obj)
        return NULL;
    if (!rt_physics2d_is_body_handle(obj)) {
        rt_trap(api);
        return NULL;
    }
    return (rt_body_impl *)obj;
}

/// @brief Clamp all body fields to safe, finite ranges and fix internal consistency.
/// @details Called after every integration step and after each pair resolution to
///   ensure NaN/Inf values and wildly out-of-range quantities from user code cannot
///   propagate.  Enforces: positions clamped to ±1e12, dimensions in (0, 1e9],
///   velocities/forces in ±1e9/±1e12, mass/inv_mass consistent (static bodies keep
///   both at 0), restitution/friction in [0,1].  Circle bodies with radius ≤ 0 get
///   a fallback radius of 1.0; box bodies have radius forced to 0.
static void sanitize_body_state(rt_body_impl *b) {
    if (!b)
        return;

    const double max_pos = 1.0e12;
    const double max_size = 1.0e9;
    const double max_vel = 1.0e9;
    const double max_force = 1.0e12;

    double fallback_x = isfinite(b->prev_x) ? b->prev_x : 0.0;
    double fallback_y = isfinite(b->prev_y) ? b->prev_y : 0.0;
    b->x = clamp_abs_finite(b->x, fallback_x, max_pos);
    b->y = clamp_abs_finite(b->y, fallback_y, max_pos);
    b->prev_x = clamp_abs_finite(b->prev_x, b->x, max_pos);
    b->prev_y = clamp_abs_finite(b->prev_y, b->y, max_pos);
    b->w = (isfinite(b->w) && b->w > 0.0) ? (b->w > max_size ? max_size : b->w) : 1.0;
    b->h = (isfinite(b->h) && b->h > 0.0) ? (b->h > max_size ? max_size : b->h) : 1.0;
    b->radius =
        (isfinite(b->radius) && b->radius > 0.0)
            ? (b->radius > max_size ? max_size : b->radius)
            : (b->is_circle ? 1.0 : 0.0);
    b->vx = clamp_abs_finite(b->vx, 0.0, max_vel);
    b->vy = clamp_abs_finite(b->vy, 0.0, max_vel);
    b->fx = clamp_abs_finite(b->fx, 0.0, max_force);
    b->fy = clamp_abs_finite(b->fy, 0.0, max_force);
    b->mass = (isfinite(b->mass) && b->mass > 0.0) ? b->mass : 0.0;
    b->inv_mass = (isfinite(b->inv_mass) && b->inv_mass > 0.0) ? b->inv_mass : 0.0;
    if (b->mass <= 0.0)
        b->inv_mass = 0.0;
    b->restitution = clamp01(b->restitution);
    b->friction = clamp01(b->friction);
    b->is_circle = b->is_circle ? 1 : 0;
    if (!b->is_circle)
        b->radius = 0.0;
}

/// @brief Left edge of this body's current AABB (works for both circle and box).
static double body_min_x(rt_body_impl *b) {
    return b->is_circle ? b->x - b->radius : b->x;
}

/// @brief Bottom edge of this body's current AABB.
static double body_min_y(rt_body_impl *b) {
    return b->is_circle ? b->y - b->radius : b->y;
}

/// @brief Right edge of this body's current AABB.
static double body_max_x(rt_body_impl *b) {
    return b->is_circle ? b->x + b->radius : b->x + b->w;
}

/// @brief Top edge of this body's current AABB.
static double body_max_y(rt_body_impl *b) {
    return b->is_circle ? b->y + b->radius : b->y + b->h;
}

/// @brief Left edge of this body's previous-frame AABB.
static double body_prev_min_x(rt_body_impl *b) {
    return b->is_circle ? b->prev_x - b->radius : b->prev_x;
}

/// @brief Bottom edge of this body's previous-frame AABB.
static double body_prev_min_y(rt_body_impl *b) {
    return b->is_circle ? b->prev_y - b->radius : b->prev_y;
}

/// @brief Right edge of this body's previous-frame AABB.
static double body_prev_max_x(rt_body_impl *b) {
    return b->is_circle ? b->prev_x + b->radius : b->prev_x + b->w;
}

/// @brief Top edge of this body's previous-frame AABB.
static double body_prev_max_y(rt_body_impl *b) {
    return b->is_circle ? b->prev_y + b->radius : b->prev_y + b->h;
}

/// @brief Minimum X of the union AABB spanning both previous and current positions.
/// @details The swept bound is used by the broad-phase grid to catch fast-moving bodies
///   that cross a grid cell boundary within a single time step.
static double body_swept_min_x(rt_body_impl *b) {
    double now = body_min_x(b);
    double prev = body_prev_min_x(b);
    return now < prev ? now : prev;
}

/// @brief Minimum Y of the swept union AABB.
static double body_swept_min_y(rt_body_impl *b) {
    double now = body_min_y(b);
    double prev = body_prev_min_y(b);
    return now < prev ? now : prev;
}

/// @brief Maximum X of the swept union AABB.
static double body_swept_max_x(rt_body_impl *b) {
    double now = body_max_x(b);
    double prev = body_prev_max_x(b);
    return now > prev ? now : prev;
}

/// @brief Maximum Y of the swept union AABB.
static double body_swept_max_y(rt_body_impl *b) {
    double now = body_max_y(b);
    double prev = body_prev_max_y(b);
    return now > prev ? now : prev;
}

/// @brief Remove and release the joint at `joint_index` in the world's joint array.
/// @details Marks the joint inactive before releasing so any in-flight solver callbacks
///   that still hold a pointer see it as dead.  Uses swap-with-tail compaction to keep
///   the array packed without shifting.
static void world_release_joint_at(rt_world_impl *w, int32_t joint_index) {
    if (!w || joint_index < 0 || joint_index >= w->joint_count)
        return;

    ph_joint *joint = w->joints[joint_index];
    if (joint)
        joint->active = 0;
    if (joint && rt_obj_release_check0(joint))
        rt_obj_free(joint);

    w->joint_count--;
    w->joints[joint_index] = w->joints[w->joint_count];
    w->joints[w->joint_count] = NULL;
}

/// @brief Remove and release every joint that references `body` as either endpoint.
/// @details Called just before a body is removed from the world to prevent dangling
///   body pointers inside live joint objects.  Iterates in place using an index
///   loop that does not advance when world_release_joint_at swaps the tail item
///   into the current slot.
static void world_remove_joints_for_body(rt_world_impl *w, rt_body_impl *body) {
    if (!w || !body)
        return;

    for (int32_t i = 0; i < w->joint_count;) {
        ph_joint *joint = w->joints[i];
        if (joint && (joint->body_a == body || joint->body_b == body)) {
            world_release_joint_at(w, i);
            continue;
        }
        i++;
    }
}

/// @brief Attempt narrow-phase collision detection and resolution for one body pair (ii, jj).
/// @details Applies the bidirectional layer/mask filter first — only continues when
///   (A.layer & B.mask) AND (B.layer & A.mask) are both nonzero.  Then tries exact
///   shape_overlap; if that misses but the swept AABB test fires, the bodies tunnelled
///   this step and the CCD path repositions them to the moment of first contact then
///   applies the resolution impulse.  sanitize_body_state is called on both bodies
///   afterward to clamp any divergent values produced by the impulse.
static void maybe_resolve_pair(rt_world_impl *w, int ii, int jj, double dt) {
    if (!w || ii < 0 || jj < 0 || ii >= w->body_count || jj >= w->body_count)
        return;

    rt_body_impl *bi = w->bodies[ii];
    rt_body_impl *bj = w->bodies[jj];
    if (!bi || !bj)
        return;

    if (!((bi->collision_layer & bj->collision_mask) && (bj->collision_layer & bi->collision_mask)))
        return;

    double nx, ny, pen, entry;
    if (shape_overlap(bi, bj, &nx, &ny, &pen)) {
        world_record_contact(w, bi, bj, nx, ny, pen);
        resolve_collision(bi, bj, nx, ny, pen);
    } else if (swept_bounds_pair(bi, bj, &nx, &ny, &entry)) {
        world_record_contact(w, bi, bj, nx, ny, 0.0);
        resolve_collision(bi, bj, nx, ny, 0.0);
        double remaining = dt * (1.0 - entry);
        if (remaining > 0.0 && isfinite(remaining)) {
            if (bi->inv_mass > 0.0) {
                bi->x += bi->vx * remaining;
                bi->y += bi->vy * remaining;
            }
            if (bj->inv_mass > 0.0) {
                bj->x += bj->vx * remaining;
                bj->y += bj->vy * remaining;
            }
        }
    }
    sanitize_body_state(bi);
    sanitize_body_state(bj);
}

//=============================================================================
// Collision detection and resolution
//=============================================================================

/// @brief Tests whether two AABB bodies overlap and computes the contact
///   manifold (normal direction and penetration depth).
///
/// Uses the Separating Axis Theorem (SAT) for AABBs. Computes the overlap on
/// each axis and selects the axis with the smallest overlap as the contact
/// normal. The normal always points from body A toward body B.
///
/// @param a       First body.
/// @param b       Second body.
/// @param nx      Output: contact normal X component (±1 or 0).
/// @param ny      Output: contact normal Y component (±1 or 0).
/// @param pen     Output: penetration depth along the contact normal.
/// @return 1 if the bodies overlap, 0 if they are separated.
static int8_t aabb_overlap(rt_body_impl *a, rt_body_impl *b, double *nx, double *ny, double *pen) {
    double ax1 = a->x, ay1 = a->y;
    double ax2 = a->x + a->w, ay2 = a->y + a->h;
    double bx1 = b->x, by1 = b->y;
    double bx2 = b->x + b->w, by2 = b->y + b->h;
    double ox, oy;

    if (ax2 <= bx1 || bx2 <= ax1 || ay2 <= by1 || by2 <= ay1)
        return 0;

    /* Calculate overlap on each axis */
    ox = (ax2 < bx2 ? ax2 - bx1 : bx2 - ax1);
    oy = (ay2 < by2 ? ay2 - by1 : by2 - ay1);

    /* Use minimum overlap axis as contact normal (minimum translation vector) */
    if (ox < oy) {
        *pen = ox;
        *ny = 0.0;
        *nx = ((a->x + a->w * 0.5) < (b->x + b->w * 0.5)) ? 1.0 : -1.0;
    } else {
        *pen = oy;
        *nx = 0.0;
        *ny = ((a->y + a->h * 0.5) < (b->y + b->h * 0.5)) ? 1.0 : -1.0;
    }
    return 1;
}

/// @brief Narrow-phase overlap test for two circle bodies.
/// @details Computes the distance between centres; if less than the sum of radii the
///   circles overlap.  Contact normal is the centre-to-centre direction; a degenerate
///   case (centres coincident, dist < 1e-12) emits a default +X normal to avoid a
///   division-by-zero.
static int8_t circle_circle_overlap(
    rt_body_impl *a, rt_body_impl *b, double *nx, double *ny, double *pen) {
    double dx = b->x - a->x;
    double dy = b->y - a->y;
    double radii = a->radius + b->radius;
    double dist_sq = dx * dx + dy * dy;

    if (dist_sq >= radii * radii)
        return 0;

    if (dist_sq < 1e-12) {
        *nx = 1.0;
        *ny = 0.0;
        *pen = radii;
        return 1;
    }

    double dist = sqrt(dist_sq);
    *nx = dx / dist;
    *ny = dy / dist;
    *pen = radii - dist;
    return 1;
}

/// @brief Narrow-phase overlap test for a circle body vs. an AABB body.
/// @details Finds the closest point on the rectangle to the circle's centre, then
///   checks whether the distance to that point is less than the circle's radius.
///   When the circle centre is inside the rectangle (dist_sq ≈ 0), selects the
///   nearest exit face using four edge distances and produces an inside-out normal
///   with a penetration depth that moves the circle fully outside.
static int8_t circle_aabb_overlap(
    rt_body_impl *circle, rt_body_impl *rect, double *nx, double *ny, double *pen) {
    double rx1 = rect->x;
    double ry1 = rect->y;
    double rx2 = rect->x + rect->w;
    double ry2 = rect->y + rect->h;
    double cx = circle->x;
    double cy = circle->y;
    double closest_x = cx < rx1 ? rx1 : (cx > rx2 ? rx2 : cx);
    double closest_y = cy < ry1 ? ry1 : (cy > ry2 ? ry2 : cy);
    double dx = closest_x - cx;
    double dy = closest_y - cy;
    double dist_sq = dx * dx + dy * dy;
    double radius = circle->radius;

    if (dist_sq >= radius * radius)
        return 0;

    if (dist_sq > 1e-12) {
        double dist = sqrt(dist_sq);
        *nx = dx / dist;
        *ny = dy / dist;
        *pen = radius - dist;
        return 1;
    }

    /* Circle center is inside the rectangle. Choose the nearest exit side and
     * use the minimum translation vector that moves the circle fully outside. */
    double left = cx - rx1;
    double right = rx2 - cx;
    double top = cy - ry1;
    double bottom = ry2 - cy;
    double best = left;
    *nx = 1.0;
    *ny = 0.0;

    if (right < best) {
        best = right;
        *nx = -1.0;
        *ny = 0.0;
    }
    if (top < best) {
        best = top;
        *nx = 0.0;
        *ny = 1.0;
    }
    if (bottom < best) {
        best = bottom;
        *nx = 0.0;
        *ny = -1.0;
    }
    *pen = radius + best;
    return 1;
}

/// @brief Dispatch the correct narrow-phase overlap test based on each body's shape type.
/// @details Handles the three possible pairings: circle/circle, circle/AABB, and AABB/AABB.
///   For circle-vs-AABB where the circle is body B, the normal is negated after the call
///   to restore the A-to-B direction convention.
static int8_t shape_overlap(rt_body_impl *a, rt_body_impl *b, double *nx, double *ny, double *pen) {
    if (!a || !b || !nx || !ny || !pen)
        return 0;

    if (a->is_circle && b->is_circle)
        return circle_circle_overlap(a, b, nx, ny, pen);

    if (a->is_circle && !b->is_circle)
        return circle_aabb_overlap(a, b, nx, ny, pen);

    if (!a->is_circle && b->is_circle) {
        int8_t hit = circle_aabb_overlap(b, a, nx, ny, pen);
        if (hit) {
            *nx = -*nx;
            *ny = -*ny;
        }
        return hit;
    }

    return aabb_overlap(a, b, nx, ny, pen);
}

/// @brief Compute the entry and exit times for two 1D intervals moving with relative velocity `rel`.
/// @details Part of the continuous collision detection (CCD) swept AABB test.  When `rel`
///   is zero, the intervals are either always overlapping (returns 1, entry=-inf, exit=+inf)
///   or always separated (returns 0).  Used by swept_bounds_pair to check each axis
///   independently before combining via the max(entry)/min(exit) convention.
static int8_t swept_axis(double a_min,
                         double a_max,
                         double b_min,
                         double b_max,
                         double rel,
                         double *entry,
                         double *exit) {
    if (rel > 0.0) {
        *entry = (b_min - a_max) / rel;
        *exit = (b_max - a_min) / rel;
        return 1;
    }
    if (rel < 0.0) {
        *entry = (b_max - a_min) / rel;
        *exit = (b_min - a_max) / rel;
        return 1;
    }

    if (a_max <= b_min || b_max <= a_min)
        return 0;
    *entry = -INFINITY;
    *exit = INFINITY;
    return 1;
}

/// @brief Swept AABB continuous collision test between two bodies.
/// @details Computes the relative velocity (A's displacement minus B's displacement this frame),
///   then calls swept_axis on both X and Y independently.  The overall entry time is
///   max(x_entry, y_entry) and exit time is min(x_exit, y_exit); a collision occurred
///   when entry < exit, 0 ≤ entry ≤ 1, and entry is finite.  On success, both bodies
///   are rolled back to their positions at the entry time so the subsequent impulse
///   resolution happens at the moment of contact rather than at full penetration depth.
///   The dominant entry axis determines the contact normal direction.
static int8_t swept_circle_circle_pair(
    rt_body_impl *a, rt_body_impl *b, double *nx, double *ny, double *entry_out) {
    double adx = a->x - a->prev_x;
    double ady = a->y - a->prev_y;
    double bdx = b->x - b->prev_x;
    double bdy = b->y - b->prev_y;
    double px = a->prev_x - b->prev_x;
    double py = a->prev_y - b->prev_y;
    double vx = adx - bdx;
    double vy = ady - bdy;
    double radii = a->radius + b->radius;
    double aa = vx * vx + vy * vy;
    double bb = 2.0 * (px * vx + py * vy);
    double cc = px * px + py * py - radii * radii;
    if (aa < 1e-18 || !isfinite(aa) || !isfinite(bb) || !isfinite(cc) || cc < 0.0)
        return 0;
    double disc = bb * bb - 4.0 * aa * cc;
    if (disc < 0.0 || !isfinite(disc))
        return 0;
    double t = (-bb - sqrt(disc)) / (2.0 * aa);
    if (t < 0.0 || t > 1.0 || !isfinite(t))
        return 0;

    double ahx = a->prev_x + adx * t;
    double ahy = a->prev_y + ady * t;
    double bhx = b->prev_x + bdx * t;
    double bhy = b->prev_y + bdy * t;
    double hx = bhx - ahx;
    double hy = bhy - ahy;
    double len = sqrt(hx * hx + hy * hy);
    if (len < 1e-12) {
        *nx = 1.0;
        *ny = 0.0;
    } else {
        *nx = hx / len;
        *ny = hy / len;
    }

    if (a->inv_mass > 0.0) {
        a->x = ahx;
        a->y = ahy;
    }
    if (b->inv_mass > 0.0) {
        b->x = bhx;
        b->y = bhy;
    }
    *entry_out = t;
    return 1;
}

/// @brief Swept (continuous) collision of a moving point against a static AABB.
/// @details Returns the entry time t in [0,1] and contact normal if the point's
///          motion this step crosses the box; used so fast bodies don't tunnel.
/// @return Non-zero on a hit (out params written), 0 otherwise.
static int8_t swept_point_aabb(double px,
                               double py,
                               double vx,
                               double vy,
                               double rx1,
                               double ry1,
                               double rx2,
                               double ry2,
                               double *nx,
                               double *ny,
                               double *entry_out) {
    double x_entry, x_exit, y_entry, y_exit;
    if (!swept_axis(px, px, rx1, rx2, vx, &x_entry, &x_exit) ||
        !swept_axis(py, py, ry1, ry2, vy, &y_entry, &y_exit))
        return 0;
    double entry = x_entry > y_entry ? x_entry : y_entry;
    double exit = x_exit < y_exit ? x_exit : y_exit;
    if (entry > exit || entry < 0.0 || entry > 1.0 || !isfinite(entry))
        return 0;
    if (x_entry > y_entry) {
        *nx = vx > 0.0 ? 1.0 : -1.0;
        *ny = 0.0;
    } else {
        *nx = 0.0;
        *ny = vy > 0.0 ? 1.0 : -1.0;
    }
    *entry_out = entry;
    return 1;
}

/// @brief Swept collision of a moving circle against a (possibly moving) AABB,
///        reduced to a point-vs-expanded-AABB test on the relative motion.
/// @return Non-zero on a hit (entry time/normal written), 0 otherwise.
static int8_t swept_circle_aabb_pair(
    rt_body_impl *circle, rt_body_impl *rect, double *nx, double *ny, double *entry_out) {
    double cdx = circle->x - circle->prev_x;
    double cdy = circle->y - circle->prev_y;
    double rdx = rect->x - rect->prev_x;
    double rdy = rect->y - rect->prev_y;
    double rvx = cdx - rdx;
    double rvy = cdy - rdy;
    if (fabs(rvx) < 1e-12 && fabs(rvy) < 1e-12)
        return 0;

    double best_entry = INFINITY;
    double best_nx = 0.0;
    double best_ny = 0.0;
    double entry = 0.0;
    double cand_nx = 0.0;
    double cand_ny = 0.0;
    double rx1 = rect->prev_x;
    double ry1 = rect->prev_y;
    double rx2 = rect->prev_x + rect->w;
    double ry2 = rect->prev_y + rect->h;

    if (swept_point_aabb(circle->prev_x,
                         circle->prev_y,
                         rvx,
                         rvy,
                         rx1 - circle->radius,
                         ry1 - circle->radius,
                         rx2 + circle->radius,
                         ry2 + circle->radius,
                         &cand_nx,
                         &cand_ny,
                         &entry)) {
        double hx = circle->prev_x + rvx * entry;
        double hy = circle->prev_y + rvy * entry;
        double closest_x = hx < rx1 ? rx1 : (hx > rx2 ? rx2 : hx);
        double closest_y = hy < ry1 ? ry1 : (hy > ry2 ? ry2 : hy);
        double ddx = closest_x - hx;
        double ddy = closest_y - hy;
        if (ddx * ddx + ddy * ddy <= circle->radius * circle->radius + 1e-9) {
            best_entry = entry;
            best_nx = cand_nx;
            best_ny = cand_ny;
        }
    }

    double corners[4][2] = {{rx1, ry1}, {rx2, ry1}, {rx1, ry2}, {rx2, ry2}};
    double aa = rvx * rvx + rvy * rvy;
    if (aa > 1e-18 && isfinite(aa)) {
        for (int i = 0; i < 4; i++) {
            double px = circle->prev_x - corners[i][0];
            double py = circle->prev_y - corners[i][1];
            double bb = 2.0 * (px * rvx + py * rvy);
            double cc = px * px + py * py - circle->radius * circle->radius;
            double disc = bb * bb - 4.0 * aa * cc;
            if (disc < 0.0 || !isfinite(disc))
                continue;
            double t = (-bb - sqrt(disc)) / (2.0 * aa);
            if (t < 0.0 || t > 1.0 || t >= best_entry || !isfinite(t))
                continue;
            double hx = circle->prev_x + rvx * t;
            double hy = circle->prev_y + rvy * t;
            double nnx = corners[i][0] - hx;
            double nny = corners[i][1] - hy;
            double len = sqrt(nnx * nnx + nny * nny);
            if (len < 1e-12)
                continue;
            best_entry = t;
            best_nx = nnx / len;
            best_ny = nny / len;
        }
    }

    if (!isfinite(best_entry))
        return 0;
    entry = best_entry;
    *nx = best_nx;
    *ny = best_ny;

    if (circle->inv_mass > 0.0) {
        circle->x = circle->prev_x + cdx * entry;
        circle->y = circle->prev_y + cdy * entry;
    }
    if (rect->inv_mass > 0.0) {
        rect->x = rect->prev_x + rdx * entry;
        rect->y = rect->prev_y + rdy * entry;
    }
    *entry_out = entry;
    return 1;
}

/// @brief Swept collision between two AABBs using their relative velocity
///        (Minkowski-expanded point sweep). @return non-zero on a hit.
static int8_t swept_aabb_pair(rt_body_impl *a, rt_body_impl *b, double *nx, double *ny, double *entry_out) {
    if (!a || !b || !nx || !ny || !entry_out)
        return 0;

    double adx = a->x - a->prev_x;
    double ady = a->y - a->prev_y;
    double bdx = b->x - b->prev_x;
    double bdy = b->y - b->prev_y;
    if (!isfinite(adx) || !isfinite(ady) || !isfinite(bdx) || !isfinite(bdy))
        return 0;

    double rvx = adx - bdx;
    double rvy = ady - bdy;
    if (fabs(rvx) < 1e-12 && fabs(rvy) < 1e-12)
        return 0;

    double x_entry, x_exit, y_entry, y_exit;
    if (!swept_axis(body_prev_min_x(a),
                    body_prev_max_x(a),
                    body_prev_min_x(b),
                    body_prev_max_x(b),
                    rvx,
                    &x_entry,
                    &x_exit) ||
        !swept_axis(body_prev_min_y(a),
                    body_prev_max_y(a),
                    body_prev_min_y(b),
                    body_prev_max_y(b),
                    rvy,
                    &y_entry,
                    &y_exit))
        return 0;

    double entry = x_entry > y_entry ? x_entry : y_entry;
    double exit = x_exit < y_exit ? x_exit : y_exit;
    if (entry > exit || entry < 0.0 || entry > 1.0 || !isfinite(entry))
        return 0;

    if (x_entry > y_entry) {
        *nx = rvx > 0.0 ? 1.0 : -1.0;
        *ny = 0.0;
    } else {
        *nx = 0.0;
        *ny = rvy > 0.0 ? 1.0 : -1.0;
    }

    if (a->inv_mass > 0.0) {
        a->x = a->prev_x + adx * entry;
        a->y = a->prev_y + ady * entry;
    }
    if (b->inv_mass > 0.0) {
        b->x = b->prev_x + bdx * entry;
        b->y = b->prev_y + bdy * entry;
    }
    *entry_out = entry;
    return 1;
}

/// @brief Swept test between two bodies, dispatching to the circle/AABB sweep
///        appropriate to their shapes; writes contact normal and entry time.
/// @return Non-zero if the bodies collide during this step, 0 otherwise.
static int8_t swept_bounds_pair(rt_body_impl *a, rt_body_impl *b, double *nx, double *ny, double *entry_out) {
    if (!a || !b || !nx || !ny || !entry_out)
        return 0;

    if (a->is_circle && b->is_circle)
        return swept_circle_circle_pair(a, b, nx, ny, entry_out);

    if (a->is_circle && !b->is_circle)
        return swept_circle_aabb_pair(a, b, nx, ny, entry_out);

    if (!a->is_circle && b->is_circle) {
        int8_t hit = swept_circle_aabb_pair(b, a, nx, ny, entry_out);
        if (hit) {
            *nx = -*nx;
            *ny = -*ny;
        }
        return hit;
    }

    return swept_aabb_pair(a, b, nx, ny, entry_out);
}

/// @brief Resolves a collision between two bodies using impulse-based dynamics.
///
/// Implements the standard game-physics collision response algorithm:
///
///   1. **Early-out**: If both bodies are static (inv_mass == 0), nothing moves.
///   2. **Relative velocity check**: Compute the relative velocity along the
///      contact normal. If it is positive (separating), skip resolution — the
///      bodies are already moving apart.
///   3. **Restitution (bounce) impulse**: Apply an impulse J along the contact
///      normal using the formula: J = -(1 + e) * vel_along_n / (1/mA + 1/mB),
///      where e = min(restitution_A, restitution_B). This is the standard
///      coefficient-of-restitution formula for instantaneous collision response.
///   4. **Friction impulse (Coulomb model)**: Compute the tangential relative
///      velocity and apply a friction impulse clamped to J * mu, where
///      mu = (friction_A + friction_B) / 2 (averaged coefficient).
///   5. **Positional correction (Baumgarte)**: Gently push overlapping bodies
///      apart by 40% of the excess penetration (with a 1% slop threshold) to
///      prevent slow sinking without causing jitter.
///
/// @param a   First body. Modified in-place (velocity and position).
/// @param b   Second body. Modified in-place (velocity and position).
/// @param nx  Contact normal X (from A toward B, magnitude 1).
/// @param ny  Contact normal Y (from A toward B, magnitude 1).
/// @param pen Penetration depth along the contact normal.
static void resolve_collision(rt_body_impl *a, rt_body_impl *b, double nx, double ny, double pen) {
    double rvx, rvy, vel_along_n, total_inv, correction;

    /* Both static — neither body can move, skip entirely */
    if (a->inv_mass == 0.0 && b->inv_mass == 0.0)
        return;
    total_inv = a->inv_mass + b->inv_mass;
    if (total_inv <= 0.0 || !isfinite(total_inv))
        return;

    /* Relative velocity of B w.r.t. A along all axes */
    rvx = b->vx - a->vx;
    rvy = b->vy - a->vy;

    /* Project relative velocity onto the contact normal */
    vel_along_n = rvx * nx + rvy * ny;

    if (vel_along_n <= 0.0 && isfinite(vel_along_n)) {
        double e, j;

        /* Use the less elastic material's coefficient so a rubber ball bouncing on
         * concrete uses the concrete's zero restitution, not the ball's high one */
        e = a->restitution < b->restitution ? a->restitution : b->restitution;

        /* Scalar impulse magnitude. Derivation: we want the post-collision relative
         * velocity along n to equal -e * vel_along_n (restitution). Solving for j
         * gives: j = -(1+e)*vel_along_n / (1/mA + 1/mB) */
        j = -(1.0 + e) * vel_along_n / total_inv;

        /* Apply the normal impulse to each body proportional to its inverse mass */
        a->vx -= j * a->inv_mass * nx;
        a->vy -= j * a->inv_mass * ny;
        b->vx += j * b->inv_mass * nx;
        b->vy += j * b->inv_mass * ny;

        /* Friction impulse: computed from the post-normal-impulse relative
         * velocity in the tangent direction. */
        rvx = b->vx - a->vx;
        rvy = b->vy - a->vy;
        {
            double vel_n_after = rvx * nx + rvy * ny;
            double tx = rvx - vel_n_after * nx;
            double ty = rvy - vel_n_after * ny;
            double t_len = sqrt(tx * tx + ty * ty);
            if (t_len > 1e-9) {
                double mu, jt, vel_along_t;
                tx /= t_len; /* Normalise tangent */
                ty /= t_len;
                vel_along_t = rvx * tx + rvy * ty;
                mu = (a->friction + b->friction) * 0.5; /* Average both surfaces */
                jt = -vel_along_t / total_inv;
                /* Clamp to Coulomb friction cone */
                if (jt > j * mu)
                    jt = j * mu;
                else if (jt < -j * mu)
                    jt = -j * mu;
                a->vx -= jt * a->inv_mass * tx;
                a->vy -= jt * a->inv_mass * ty;
                b->vx += jt * b->inv_mass * tx;
                b->vy += jt * b->inv_mass * ty;
            }
        }
    }

    /* Positional correction (Baumgarte stabilisation): directly move bodies
     * apart to counter numerical drift that causes objects to slowly sink into
     * each other. A small slop (0.01) is tolerated before correcting to avoid
     * jittering on resting contacts. The 40% factor spreads the correction
     * over several frames rather than snapping immediately (prevents bouncing). */
    {
        double slop = 0.01;
        double pct = 0.4;
        correction = (pen - slop > 0.0 ? pen - slop : 0.0) * pct / total_inv;
        a->x -= correction * a->inv_mass * nx;
        a->y -= correction * a->inv_mass * ny;
        b->x += correction * b->inv_mass * nx;
        b->y += correction * b->inv_mass * ny;
    }
}

//=============================================================================
// World finalisation
//=============================================================================

/// @brief GC finaliser for a physics world.
///
/// Called by the runtime's garbage collector when the world object is about to
/// be freed. Releases the reference-counted body handles so their own memory
/// can be reclaimed. After this call, all body pointers in the world are
/// invalid — the finaliser zeroes body_count to make this explicit.
///
/// @param obj Pointer to the rt_world_impl being finalised.
static void world_finalizer(void *obj) {
    rt_world_impl *w = (rt_world_impl *)obj;
    if (w) {
        while (w->joint_count > 0)
            world_release_joint_at(w, w->joint_count - 1);

        int64_t i;
        for (i = 0; i < w->body_count; i++) {
            if (w->bodies[i] && rt_obj_release_check0(w->bodies[i]))
                rt_obj_free(w->bodies[i]);
        }
        w->body_count = 0;
    }
}

//=============================================================================
// Public API — World
//=============================================================================

/// @brief Allocate a new physics world with the given constant gravity vector.
/// @details Initialises all body/joint/contact slots to zero and registers a GC
///   finalizer that will release retained body references when the world is collected.
/// @param gravity_x World-space X acceleration (e.g. 0 for horizontal, ±g for side-scrollers).
/// @param gravity_y World-space Y acceleration (positive = downward in screen coords).
/// @return Opaque world handle, or NULL on allocation failure (after trapping).
void *rt_physics2d_world_new(double gravity_x, double gravity_y) {
    rt_world_impl *w =
        (rt_world_impl *)rt_obj_new_i64(RT_PHYSICS2D_WORLD_CLASS_ID, (int64_t)sizeof(rt_world_impl));
    if (!w) {
        rt_trap("Physics2D.World: allocation failed");
        return NULL;
    }
    w->vptr = NULL;
    w->gravity_x = finite_or(gravity_x, 0.0);
    w->gravity_y = finite_or(gravity_y, 0.0);
    w->body_count = 0;
    w->joint_count = 0;
    w->contact_count = 0;
    memset(w->bodies, 0, sizeof(w->bodies));
    memset(w->joints, 0, sizeof(w->joints));
    memset(w->contacts, 0, sizeof(w->contacts));
    rt_obj_set_finalizer(w, world_finalizer);
    return w;
}

/// @brief Advance the physics world by `dt` seconds. Stages: (1) apply gravity + accumulated
/// forces to dynamic body velocities (symplectic Euler) and reset force accumulators; (2)
/// integrate velocity → position; (2.5) iteratively solve joint constraints; (3) broad-phase
/// 8×8 grid + narrow-phase shape overlap/CCD detection with bit-matrix de-dup, then resolve each
/// collision per the bodies' restitution/friction/collision filter. No-op for invalid dt or dt ≤ 0.
void rt_physics2d_world_step(void *obj, double dt) {
    rt_world_impl *w;
    int64_t i;
    if (!obj)
        return;
    w = checked_world(obj, "Physics2D.World.Step: expected Physics2D.World");
    if (!w)
        return;
    world_clear_contacts(w);
    if (dt <= 0.0 || !isfinite(dt))
        return;

    for (i = 0; i < w->body_count; i++) {
        rt_body_impl *b = w->bodies[i];
        if (!b)
            continue;
        sanitize_body_state(b);
        b->prev_x = b->x;
        b->prev_y = b->y;
    }

    /* Step 1: Apply accumulated forces and gravity to each dynamic body's
     * velocity (symplectic Euler, force→velocity half-step).
     * Forces are cleared here so Apply Force calls accumulate cleanly across
     * multiple Step() calls within the same frame if the caller uses sub-steps. */
    for (i = 0; i < w->body_count; i++) {
        rt_body_impl *b = w->bodies[i];
        if (!b)
            continue;
        if (b->inv_mass == 0.0) {
            b->fx = 0.0;
            b->fy = 0.0;
            continue; /* Skip static bodies */
        }
        b->vx += (b->fx * b->inv_mass + w->gravity_x) * dt;
        b->vy += (b->fy * b->inv_mass + w->gravity_y) * dt;
        b->fx = 0.0;
        b->fy = 0.0;
        sanitize_body_state(b);
    }

    if (w->joint_count > 0) {
        rt_physics2d_solve_spring_joints(obj, dt);
        for (i = 0; i < w->body_count; i++)
            sanitize_body_state(w->bodies[i]);
    }

    /* Step 2: Integrate velocity → position for each dynamic body.
     * Done in a separate pass from Step 1 so all velocity changes from forces
     * and springs are committed before any position updates occur. */
    for (i = 0; i < w->body_count; i++) {
        rt_body_impl *b = w->bodies[i];
        if (!b || b->inv_mass == 0.0)
            continue;
        b->x += b->vx * dt;
        b->y += b->vy * dt;
        sanitize_body_state(b);
    }

    /* Step 2.5: Solve joint constraints (iterative relaxation).
     * Joints are solved after velocity integration but before collision
     * detection so that constrained bodies are in valid positions before
     * the broad/narrow phase runs. */
    if (w->joint_count > 0) {
        rt_physics2d_solve_position_joints(obj, dt);
        for (i = 0; i < w->body_count; i++)
            sanitize_body_state(w->bodies[i]);
    }

    /* Step 3: Broad-phase + narrow-phase collision detection and resolution.
     *
     * Broad phase: uniform 8×8 grid. The grid is recomputed from scratch each
     * step. The world swept AABB is computed first, then divided into
     * BPG_DIM×BPG_DIM cells. Each body is registered in every cell its swept
     * bounds overlap.
     *
     * All grid arrays are stack-local, making this function safe to call on
     * concurrent worlds from separate threads with no data sharing.
     *
     * The grid intentionally stores uint8_t body indices (not pointers) to
     * keep each cell small. BPG_CELL_MAX caps the count per cell; if a cell
     * overflows, the step falls back to an exhaustive O(n²) pair pass so
     * collision correctness is preserved in dense scenes.
     *
     * Narrow phase: for each pair of bodies that share a grid cell, test with
     * shape_overlap() or swept AABB and call resolve_collision() if they collide.
     *
     * De-duplication: a 256×256 bit-matrix (pair_checked) ensures each pair
     * (i, j) is resolved at most once per step, even when the two bodies share
     * multiple grid cells (e.g., near a cell boundary). Bit (i,j) is stored at
     * byte [i*PH_MAX_BODIES+j >> 3], bit [(i*PH_MAX_BODIES+j) & 7]. The matrix
     * is stack-local: (256×256) / 8 = 8192 bytes ≈ 8 KB. */

#define BPG_DIM 8       /* Broad-phase grid cells per axis (8×8 = 64 total) */
#define BPG_CELL_MAX 32 /* Maximum body indices stored per grid cell */

    if (w->body_count >= 2) {
        /* --- Step 3a: Compute the world swept AABB that encloses all bodies --- */
        double wx0 = 1e18, wy0 = 1e18, wx1 = -1e18, wy1 = -1e18;
        for (i = 0; i < w->body_count; i++) {
            rt_body_impl *b = w->bodies[i];
            if (!b)
                continue;
            double bx0 = body_swept_min_x(b);
            double by0 = body_swept_min_y(b);
            double bx1 = body_swept_max_x(b);
            double by1 = body_swept_max_y(b);
            if (bx0 < wx0)
                wx0 = bx0;
            if (by0 < wy0)
                wy0 = by0;
            if (bx1 > wx1)
                wx1 = bx1;
            if (by1 > wy1)
                wy1 = by1;
        }
        /* Guard: ensure minimum cell size of 1 so division below never divides
         * by zero (can happen when all bodies occupy the exact same point). */
        if (wx1 <= wx0)
            wx1 = wx0 + 1.0;
        if (wy1 <= wy0)
            wy1 = wy0 + 1.0;
        double cell_w = (wx1 - wx0) / BPG_DIM;
        double cell_h = (wy1 - wy0) / BPG_DIM;

        /* --- Step 3b: Populate the broad-phase grid (stack-local) ---
         * Each body is inserted into every cell its swept bounds touch. A body
         * that straddles a cell boundary appears in both cells so it will be
         * paired with neighbours on either side. */
        uint8_t grid_bodies[BPG_DIM * BPG_DIM][BPG_CELL_MAX];
        int grid_count[BPG_DIM * BPG_DIM];
        int grid_overflow = 0;
        memset(grid_count, 0, sizeof(grid_count));

        for (i = 0; i < w->body_count; i++) {
            rt_body_impl *b = w->bodies[i];
            if (!b)
                continue;
            double bx0 = body_swept_min_x(b);
            double by0 = body_swept_min_y(b);
            double bx1 = body_swept_max_x(b);
            double by1 = body_swept_max_y(b);
            int cx0 = (int)((bx0 - wx0) / cell_w);
            if (cx0 < 0)
                cx0 = 0;
            if (cx0 >= BPG_DIM)
                cx0 = BPG_DIM - 1;
            int cy0 = (int)((by0 - wy0) / cell_h);
            if (cy0 < 0)
                cy0 = 0;
            if (cy0 >= BPG_DIM)
                cy0 = BPG_DIM - 1;
            int cx1 = (int)((bx1 - wx0) / cell_w);
            if (cx1 < 0)
                cx1 = 0;
            if (cx1 >= BPG_DIM)
                cx1 = BPG_DIM - 1;
            int cy1 = (int)((by1 - wy0) / cell_h);
            if (cy1 < 0)
                cy1 = 0;
            if (cy1 >= BPG_DIM)
                cy1 = BPG_DIM - 1;
            for (int cy = cy0; cy <= cy1; cy++) {
                for (int cx = cx0; cx <= cx1; cx++) {
                    int cell = cy * BPG_DIM + cx;
                    int cnt = grid_count[cell];
                    if (cnt < BPG_CELL_MAX) {
                        grid_bodies[cell][cnt] = (uint8_t)i;
                        grid_count[cell] = cnt + 1;
                    } else {
                        grid_overflow = 1;
                    }
                }
            }
        }

        if (grid_overflow) {
            for (int ii = 0; ii < w->body_count; ii++) {
                for (int jj = ii + 1; jj < w->body_count; jj++)
                    maybe_resolve_pair(w, ii, jj, dt);
            }
        } else {
            /* --- Step 3c: Narrow phase — test each cell's candidate pairs ---
             * pair_checked is a bit-matrix preventing duplicate pair resolution.
             * Pairs are always stored with the lower index first (ii < jj) so the
             * bit position is deterministic regardless of cell iteration order. */
            uint8_t pair_checked[PH_MAX_BODIES * PH_MAX_BODIES / 8 + 1];
            memset(pair_checked, 0, sizeof(pair_checked));

            for (int cell = 0; cell < BPG_DIM * BPG_DIM; cell++) {
                int cnt = grid_count[cell];
                for (int a = 0; a < cnt; a++) {
                    for (int b_idx = a + 1; b_idx < cnt; b_idx++) {
                        int ii = (int)grid_bodies[cell][a];
                        int jj = (int)grid_bodies[cell][b_idx];
                        if (ii > jj) {
                            int tmp = ii;
                            ii = jj;
                            jj = tmp;
                        }
                        int bit = ii * PH_MAX_BODIES + jj;
                        if (pair_checked[bit >> 3] & (uint8_t)(1u << (bit & 7)))
                            continue;
                        pair_checked[bit >> 3] |= (uint8_t)(1u << (bit & 7));
                        maybe_resolve_pair(w, ii, jj, dt);
                    }
                }
            }
        }
    }

#undef BPG_DIM
#undef BPG_CELL_MAX
}

/// @brief Insert a body into the world's simulation list. The world retains the body; remove
/// later via `_remove`. Traps if the world's body count cap (PH_MAX_BODIES) is hit.
void rt_physics2d_world_add(void *obj, void *body) {
    rt_world_impl *w;
    if (!obj || !body)
        return;
    w = checked_world(obj, "Physics2D.World.Add: expected Physics2D.World");
    if (!w)
        return;
    if (!rt_physics2d_is_body_handle(body)) {
        rt_trap("Physics2D.World.Add: expected Physics2D.Body");
        return;
    }
    sanitize_body_state((rt_body_impl *)body);
    for (int64_t i = 0; i < w->body_count; i++) {
        if (w->bodies[i] == (rt_body_impl *)body)
            return;
    }
    if (w->body_count >= PH_MAX_BODIES) {
        rt_trap("Physics2D.World.Add: body limit exceeded (max " RT_PH_MAX_BODIES_STR
                "); increase PH_MAX_BODIES and recompile");
        return;
    }
    rt_obj_retain_maybe(body);
    w->bodies[w->body_count++] = (rt_body_impl *)body;
}

/// @brief Remove a body from the world (linear scan, O(n)). The body is released; if its refcount
/// hits 0, it's freed. Order is not preserved (uses swap-with-tail compaction).
void rt_physics2d_world_remove(void *obj, void *body) {
    rt_world_impl *w;
    int64_t i;
    if (!obj || !body)
        return;
    w = checked_world(obj, "Physics2D.World.Remove: expected Physics2D.World");
    if (!w)
        return;
    if (!rt_physics2d_is_body_handle(body)) {
        rt_trap("Physics2D.World.Remove: expected Physics2D.Body");
        return;
    }
    for (i = 0; i < w->body_count; i++) {
        if (w->bodies[i] == (rt_body_impl *)body) {
            world_remove_joints_for_body(w, w->bodies[i]);
            world_clear_contacts(w);
            if (rt_obj_release_check0(w->bodies[i]))
                rt_obj_free(w->bodies[i]);
            /* Swap with tail to maintain a compact, order-independent array */
            w->bodies[i] = w->bodies[w->body_count - 1];
            w->bodies[w->body_count - 1] = NULL;
            w->body_count--;
            return;
        }
    }
}

/// @brief Number of bodies currently registered with the world.
int64_t rt_physics2d_world_body_count(void *obj) {
    if (!obj)
        return 0;
    rt_world_impl *w = checked_world(obj, "Physics2D.World.BodyCount: expected Physics2D.World");
    return w ? w->body_count : 0;
}

/// @brief Set world gravity in world-units per second² (typical: gx=0, gy=9.8 for downward grav).
void rt_physics2d_world_set_gravity(void *obj, double gx, double gy) {
    if (!obj)
        return;
    rt_world_impl *w = checked_world(obj, "Physics2D.World.SetGravity: expected Physics2D.World");
    if (!w)
        return;
    w->gravity_x = finite_or(gx, 0.0);
    w->gravity_y = finite_or(gy, 0.0);
}

/// @brief Number of contact pairs resolved during the most recent world step.
/// @details The list is rebuilt fresh on every call to rt_physics2d_world_step and
///   capped at PH_MAX_CONTACTS.  Query it between steps to drive game logic (e.g.
///   damage on collision, sound effects).
int64_t rt_physics2d_world_contact_count(void *obj) {
    if (!obj)
        return 0;
    rt_world_impl *w = checked_world(obj, "Physics2D.World.ContactCount: expected Physics2D.World");
    return w ? w->contact_count : 0;
}

/// @brief Guard for all contact-list accessors — returns 1 only when `index` is in range.
static int8_t checked_contact(rt_world_impl *w, int64_t index) {
    return w && index >= 0 && index < w->contact_count;
}

/// @brief Return the first body in a contact pair (the "A" side) at the given contact index.
void *rt_physics2d_world_contact_body_a(void *obj, int64_t index) {
    rt_world_impl *w = checked_world(obj, "Physics2D.World.ContactBodyA: expected Physics2D.World");
    return checked_contact(w, index) ? w->contacts[index].body_a : NULL;
}

/// @brief Return the second body in a contact pair (the "B" side) at the given contact index.
void *rt_physics2d_world_contact_body_b(void *obj, int64_t index) {
    rt_world_impl *w = checked_world(obj, "Physics2D.World.ContactBodyB: expected Physics2D.World");
    return checked_contact(w, index) ? w->contacts[index].body_b : NULL;
}

/// @brief Contact normal X component (points from body A toward body B).
double rt_physics2d_world_contact_nx(void *obj, int64_t index) {
    rt_world_impl *w = checked_world(obj, "Physics2D.World.ContactNX: expected Physics2D.World");
    return checked_contact(w, index) ? w->contacts[index].nx : 0.0;
}

/// @brief Contact normal Y component (points from body A toward body B).
double rt_physics2d_world_contact_ny(void *obj, int64_t index) {
    rt_world_impl *w = checked_world(obj, "Physics2D.World.ContactNY: expected Physics2D.World");
    return checked_contact(w, index) ? w->contacts[index].ny : 0.0;
}

/// @brief Penetration depth at the contact point (0 for tunnelling contacts caught by CCD).
double rt_physics2d_world_contact_depth(void *obj, int64_t index) {
    rt_world_impl *w = checked_world(obj, "Physics2D.World.ContactDepth: expected Physics2D.World");
    return checked_contact(w, index) ? w->contacts[index].penetration : 0.0;
}

//=============================================================================
// Public API — Body
//=============================================================================

/// @brief Construct a 2D rigid body with bottom-left position (x, y), size (w, h), and `mass`.
/// `mass <= 0` ⇒ static (immovable, infinite mass). Defaults: restitution 0.5 (moderately bouncy),
/// friction 0.3, collision_layer 1, collision_mask -1 (collides with all 64 layers).
void *rt_physics2d_body_new(double x, double y, double w, double h, double mass) {
    rt_body_impl *b =
        (rt_body_impl *)rt_obj_new_i64(RT_PHYSICS2D_BODY_CLASS_ID, (int64_t)sizeof(rt_body_impl));
    if (!b) {
        rt_trap("Physics2D.Body: allocation failed");
        return NULL;
    }
    b->vptr = NULL;
    x = finite_or(x, 0.0);
    y = finite_or(y, 0.0);
    w = positive_or(w, 1.0);
    h = positive_or(h, 1.0);
    mass = (isfinite(mass) && mass > 0.0) ? mass : 0.0;
    b->x = x;
    b->y = y;
    b->prev_x = x;
    b->prev_y = y;
    b->w = w;
    b->h = h;
    b->vx = 0.0;
    b->vy = 0.0;
    b->fx = 0.0;
    b->fy = 0.0;
    b->mass = mass;
    b->inv_mass = (mass > 0.0) ? (1.0 / mass) : 0.0;
    b->restitution = 0.5;           /* Moderately bouncy by default */
    b->friction = 0.3;              /* Moderate friction by default */
    b->collision_layer = 1;         /* Default: layer 0, bit 0 set */
    b->collision_mask = INT64_C(-1); /* Default: collide with all 64 layers */
    b->radius = 0.0;
    b->is_circle = 0;
    return b;
}

// The next six functions are simple accessors over the body's stored state
// (position, size, velocity). Each returns 0.0 for a NULL handle.

/// @brief Bottom-left X position in world units.
double rt_physics2d_body_x(void *obj) {
    rt_body_impl *b = checked_body(obj, "Physics2D.Body.X: expected Physics2D.Body");
    return b ? b->x : 0.0;
}

/// @brief Bottom-left Y position in world units.
double rt_physics2d_body_y(void *obj) {
    rt_body_impl *b = checked_body(obj, "Physics2D.Body.Y: expected Physics2D.Body");
    return b ? b->y : 0.0;
}

/// @brief X position at the start of the last step (before integration).
/// @details Used by the swept CCD path; useful in game logic for computing per-frame
///   displacement without storing a separate previous-position variable.
double rt_physics2d_body_prev_x(void *obj) {
    rt_body_impl *b = checked_body(obj, "Physics2D.Body.PrevX: expected Physics2D.Body");
    return b ? b->prev_x : 0.0;
}

/// @brief Y position at the start of the last step (before integration).
/// @details Mirror of rt_physics2d_body_prev_x for the vertical axis. Used by
///   the swept CCD path to construct the previous-frame AABB, and is useful in
///   game logic for computing per-frame vertical displacement without storing a
///   separate previous-position variable.
/// @param obj Physics2D.Body instance.
/// @return Y coordinate recorded at the beginning of the most recent simulation
///   step, or 0.0 if @p obj is not a valid body.
double rt_physics2d_body_prev_y(void *obj) {
    rt_body_impl *b = checked_body(obj, "Physics2D.Body.PrevY: expected Physics2D.Body");
    return b ? b->prev_y : 0.0;
}

/// @brief AABB width in world units.
double rt_physics2d_body_w(void *obj) {
    rt_body_impl *b = checked_body(obj, "Physics2D.Body.Width: expected Physics2D.Body");
    return b ? b->w : 0.0;
}

/// @brief AABB height in world units.
double rt_physics2d_body_h(void *obj) {
    rt_body_impl *b = checked_body(obj, "Physics2D.Body.Height: expected Physics2D.Body");
    return b ? b->h : 0.0;
}

/// @brief Linear X-velocity in world units per second.
double rt_physics2d_body_vx(void *obj) {
    rt_body_impl *b = checked_body(obj, "Physics2D.Body.VX: expected Physics2D.Body");
    return b ? b->vx : 0.0;
}

/// @brief Linear Y-velocity in world units per second.
double rt_physics2d_body_vy(void *obj) {
    rt_body_impl *b = checked_body(obj, "Physics2D.Body.VY: expected Physics2D.Body");
    return b ? b->vy : 0.0;
}

/// @brief Teleport the body to (x, y) world coordinates. Bypasses collision (the next `_step`
/// will resolve any resulting overlap). Use `_apply_impulse` for physically realistic motion.
void rt_physics2d_body_set_pos(void *obj, double x, double y) {
    if (!obj)
        return;
    if (!isfinite(x) || !isfinite(y))
        return;
    rt_body_impl *body = checked_body(obj, "Physics2D.Body.SetPos: expected Physics2D.Body");
    if (!body)
        return;
    body->x = x;
    body->y = y;
    body->prev_x = x;
    body->prev_y = y;
}

/// @brief Override the body's linear velocity directly. Useful for kinematic motion (e.g.,
/// platforms that move on a script). Static bodies (mass=0) ignore this.
void rt_physics2d_body_set_vel(void *obj, double vx, double vy) {
    if (!obj)
        return;
    if (!isfinite(vx) || !isfinite(vy))
        return;
    rt_body_impl *body = checked_body(obj, "Physics2D.Body.SetVel: expected Physics2D.Body");
    if (!body)
        return;
    if (body->inv_mass == 0.0) {
        body->vx = 0.0;
        body->vy = 0.0;
        return;
    }
    body->vx = vx;
    body->vy = vy;
    sanitize_body_state(body);
}

/// @brief Add (fx, fy) to the body's accumulated force vector. Forces are integrated and
/// cleared each `_step`; call repeatedly within a frame to combine multiple force contributors.
void rt_physics2d_body_apply_force(void *obj, double fx, double fy) {
    if (!obj)
        return;
    if (!isfinite(fx) || !isfinite(fy))
        return;
    rt_body_impl *body = checked_body(obj, "Physics2D.Body.ApplyForce: expected Physics2D.Body");
    if (!body)
        return;
    if (body->inv_mass == 0.0)
        return;
    /* Forces accumulate until the next Step(); they are additive so multiple
     * ApplyForce calls in the same frame combine correctly. */
    body->fx += fx;
    body->fy += fy;
    sanitize_body_state(body);
}

/// @brief Apply an instantaneous velocity change of (ix, iy) * inv_mass. Use for jumps,
/// explosions, kicks — anything that should change velocity *now* without requiring a force
/// applied for a duration.
void rt_physics2d_body_apply_impulse(void *obj, double ix, double iy) {
    rt_body_impl *b;
    if (!obj)
        return;
    if (!isfinite(ix) || !isfinite(iy))
        return;
    b = checked_body(obj, "Physics2D.Body.ApplyImpulse: expected Physics2D.Body");
    if (!b)
        return;
    if (b->inv_mass == 0.0)
        return; /* Static bodies cannot be moved by impulses */
    /* An impulse is an instantaneous velocity change: Δv = impulse / mass,
     * equivalently: Δv = impulse * inv_mass. */
    b->vx += ix * b->inv_mass;
    b->vy += iy * b->inv_mass;
    sanitize_body_state(b);
}

/// @brief Read the body's bounciness coefficient ([0, 1] typical).
double rt_physics2d_body_restitution(void *obj) {
    rt_body_impl *b = checked_body(obj, "Physics2D.Body.Restitution: expected Physics2D.Body");
    return b ? b->restitution : 0.0;
}

/// @brief Set bounciness: 0 = no bounce, 1 = perfectly elastic. Pair-wise restitution averages
/// both bodies' values during collision response.
void rt_physics2d_body_set_restitution(void *obj, double r) {
    rt_body_impl *b = checked_body(obj, "Physics2D.Body.Restitution.set: expected Physics2D.Body");
    if (b)
        b->restitution = clamp01(r);
}

/// @brief Read the body's friction coefficient ([0, 1] typical).
double rt_physics2d_body_friction(void *obj) {
    rt_body_impl *b = checked_body(obj, "Physics2D.Body.Friction: expected Physics2D.Body");
    return b ? b->friction : 0.0;
}

/// @brief Set friction: 0 = ice, 1 = sandpaper. Applied as a tangential damping during contact.
void rt_physics2d_body_set_friction(void *obj, double f) {
    rt_body_impl *b = checked_body(obj, "Physics2D.Body.Friction.set: expected Physics2D.Body");
    if (b)
        b->friction = clamp01(f);
}

/// @brief Returns 1 if the body is static (mass=0, immovable). Static bodies skip integration.
int8_t rt_physics2d_body_is_static(void *obj) {
    /* A body is static when its inverse-mass is zero (mass == 0 at creation) */
    rt_body_impl *b = checked_body(obj, "Physics2D.Body.IsStatic: expected Physics2D.Body");
    return (b && b->inv_mass == 0.0) ? 1 : 0;
}

/// @brief Read the body's mass (0 if static or NULL).
double rt_physics2d_body_mass(void *obj) {
    rt_body_impl *b = checked_body(obj, "Physics2D.Body.Mass: expected Physics2D.Body");
    return b ? b->mass : 0.0;
}

/// @brief Read the body's collision-layer bitmask (which layers it belongs to).
int64_t rt_physics2d_body_collision_layer(void *obj) {
    rt_body_impl *b = checked_body(obj, "Physics2D.Body.CollisionLayer: expected Physics2D.Body");
    return b ? b->collision_layer : 0;
}

/// @brief Set the collision-layer bitmask. Combined with the *other* body's collision_mask
/// during overlap tests — only pairs where each body's layer matches the other's mask collide.
void rt_physics2d_body_set_collision_layer(void *obj, int64_t layer) {
    rt_body_impl *b = checked_body(obj, "Physics2D.Body.CollisionLayer.set: expected Physics2D.Body");
    if (b)
        b->collision_layer = layer;
}

/// @brief Read the body's collision-mask bitmask (which layers it tests against).
int64_t rt_physics2d_body_collision_mask(void *obj) {
    rt_body_impl *b = checked_body(obj, "Physics2D.Body.CollisionMask: expected Physics2D.Body");
    return b ? b->collision_mask : 0;
}

/// @brief Set the collision-mask. Each bit corresponds to a layer this body collides with.
/// Default -1 = collides with all 64 layers. Use 0 to make the body collision-free.
void rt_physics2d_body_set_collision_mask(void *obj, int64_t mask) {
    rt_body_impl *b = checked_body(obj, "Physics2D.Body.CollisionMask.set: expected Physics2D.Body");
    if (b)
        b->collision_mask = mask;
}

//=============================================================================
// Projectile2D
//=============================================================================

typedef struct {
    void *vptr;
    double p0x, p0y;
    double v0x, v0y;
    double gx, gy;
    double drag;
    double total_time;
    int8_t landed;
    double ground_y;
} rt_projectile2d_impl;

/// @brief Safe-cast a handle to the Projectile2D impl, trapping @p api on a
///        class-id mismatch. @return The impl, or NULL if @p obj is NULL.
static rt_projectile2d_impl *checked_projectile(void *obj, const char *api) {
    if (!obj)
        return NULL;
    if (rt_obj_class_id(obj) != RT_PHYSICS2D_PROJECTILE_CLASS_ID) {
        rt_trap(api);
        return NULL;
    }
    return (rt_projectile2d_impl *)obj;
}

/// @brief Return @p value if finite, else @p fallback (sanitizes user-supplied
///        projectile parameters against NaN/Inf).
static double projectile_finite_or(double value, double fallback) {
    return isfinite(value) ? value : fallback;
}

void *rt_projectile2d_new(double p0x, double p0y, double v0x, double v0y, double gx, double gy) {
    rt_projectile2d_impl *p = (rt_projectile2d_impl *)rt_obj_new_i64(
        RT_PHYSICS2D_PROJECTILE_CLASS_ID, (int64_t)sizeof(rt_projectile2d_impl));
    if (!p)
        return NULL;
    memset(p, 0, sizeof(*p));
    p->p0x = projectile_finite_or(p0x, 0.0);
    p->p0y = projectile_finite_or(p0y, 0.0);
    p->v0x = projectile_finite_or(v0x, 0.0);
    p->v0y = projectile_finite_or(v0y, 0.0);
    p->gx = projectile_finite_or(gx, 0.0);
    p->gy = projectile_finite_or(gy, 0.0);
    p->ground_y = INFINITY;
    return p;
}

void rt_projectile2d_set_drag(void *obj, double drag) {
    rt_projectile2d_impl *p = checked_projectile(obj, "Projectile2D.SetDrag: expected Projectile2D");
    if (!p)
        return;
    p->drag = isfinite(drag) && drag > 0.0 ? drag : 0.0;
}

void rt_projectile2d_set_ground_y(void *obj, double y) {
    rt_projectile2d_impl *p =
        checked_projectile(obj, "Projectile2D.SetGroundY: expected Projectile2D");
    if (p)
        p->ground_y = isfinite(y) ? y : INFINITY;
}

void rt_projectile2d_reset(void *obj) {
    rt_projectile2d_impl *p = checked_projectile(obj, "Projectile2D.Reset: expected Projectile2D");
    if (!p)
        return;
    p->total_time = 0.0;
    p->landed = 0;
}

/// @brief Position of one axis at time @p t under constant gravity @p g and
///        linear @p drag (closed-form; reduces to p0+v0·t+½g·t² when drag==0).
static double projectile_pos_at(double p0, double v0, double g, double drag, double t) {
    if (!isfinite(t) || t <= 0.0)
        return p0;
    if (drag <= 0.0)
        return p0 + v0 * t + 0.5 * g * t * t;
    double e = exp(-drag * t);
    return p0 + (v0 / drag) * (1.0 - e) + (g / drag) * t -
           (g / (drag * drag)) * (1.0 - e);
}

/// @brief Velocity of one axis at time @p t under gravity @p g and linear
///        @p drag (closed-form; reduces to v0+g·t when drag==0).
static double projectile_vel_at(double v0, double g, double drag, double t) {
    if (!isfinite(t) || t <= 0.0)
        return v0;
    if (drag <= 0.0)
        return v0 + g * t;
    double e = exp(-drag * t);
    return v0 * e + (g / drag) * (1.0 - e);
}

void rt_projectile2d_advance(void *obj, double dt) {
    rt_projectile2d_impl *p =
        checked_projectile(obj, "Projectile2D.Advance: expected Projectile2D");
    if (!p || !isfinite(dt) || dt <= 0.0 || p->landed)
        return;
    p->total_time += dt;
    if (rt_projectile2d_y_at(obj, p->total_time) >= p->ground_y)
        p->landed = 1;
}

double rt_projectile2d_x_at(void *obj, double t) {
    rt_projectile2d_impl *p = checked_projectile(obj, "Projectile2D.XAt: expected Projectile2D");
    return p ? projectile_pos_at(p->p0x, p->v0x, p->gx, p->drag, t) : 0.0;
}

double rt_projectile2d_y_at(void *obj, double t) {
    rt_projectile2d_impl *p = checked_projectile(obj, "Projectile2D.YAt: expected Projectile2D");
    return p ? projectile_pos_at(p->p0y, p->v0y, p->gy, p->drag, t) : 0.0;
}

double rt_projectile2d_vx_at(void *obj, double t) {
    rt_projectile2d_impl *p = checked_projectile(obj, "Projectile2D.VXAt: expected Projectile2D");
    return p ? projectile_vel_at(p->v0x, p->gx, p->drag, t) : 0.0;
}

double rt_projectile2d_vy_at(void *obj, double t) {
    rt_projectile2d_impl *p = checked_projectile(obj, "Projectile2D.VYAt: expected Projectile2D");
    return p ? projectile_vel_at(p->v0y, p->gy, p->drag, t) : 0.0;
}

int8_t rt_projectile2d_has_landed(void *obj) {
    rt_projectile2d_impl *p =
        checked_projectile(obj, "Projectile2D.HasLanded: expected Projectile2D");
    return p ? p->landed : 0;
}

double rt_projectile2d_total_time(void *obj) {
    rt_projectile2d_impl *p =
        checked_projectile(obj, "Projectile2D.TotalTime: expected Projectile2D");
    return p ? p->total_time : 0.0;
}

double rt_projectile2d_time_to_ground(void *obj) {
    rt_projectile2d_impl *p =
        checked_projectile(obj, "Projectile2D.TimeToGround: expected Projectile2D");
    if (!p || !isfinite(p->ground_y))
        return INFINITY;
    if (p->drag > 0.0) {
        double lo = 0.0;
        double hi = 1.0;
        for (int i = 0; i < 64 && rt_projectile2d_y_at(obj, hi) < p->ground_y; i++)
            hi *= 2.0;
        if (!isfinite(hi) || rt_projectile2d_y_at(obj, hi) < p->ground_y)
            return INFINITY;
        for (int i = 0; i < 64; i++) {
            double mid = (lo + hi) * 0.5;
            if (rt_projectile2d_y_at(obj, mid) >= p->ground_y)
                hi = mid;
            else
                lo = mid;
        }
        return hi;
    }
    double a = 0.5 * p->gy;
    double b = p->v0y;
    double c = p->p0y - p->ground_y;
    if (fabs(a) < 1e-12) {
        if (fabs(b) < 1e-12)
            return c >= 0.0 ? 0.0 : INFINITY;
        double t = -c / b;
        return t >= 0.0 ? t : INFINITY;
    }
    double disc = b * b - 4.0 * a * c;
    if (disc < 0.0 || !isfinite(disc))
        return INFINITY;
    double root = sqrt(disc);
    double t1 = (-b - root) / (2.0 * a);
    double t2 = (-b + root) / (2.0 * a);
    double best = INFINITY;
    if (t1 >= 0.0)
        best = t1;
    if (t2 >= 0.0 && t2 < best)
        best = t2;
    return best;
}
