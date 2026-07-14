//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/physics/rt_physics3d_character.c
// Purpose: Character3D kinematic controller (sweep-and-slide motion, step-up,
//   ground probing) and Trigger3D overlap volumes for the Physics3D runtime.
//   Split out of rt_physics3d.c; shares core types via rt_physics3d_internal.h.
//
// Key invariants:
//   - Character3D moves via kinematic sweeps against the world's bodies, sliding
//     along contact normals; up to 3 slide iterations per move axis.
//   - Trigger3D tracks up to TRG3D_MAX_TRACKED overlapping bodies as weak refs;
//     stale entries are pruned on the next Update.
//
// Ownership/Lifetime:
//   - Character3D / Trigger3D are GC-managed; finalizers release retained refs.
//
// Links: rt_physics3d_internal.h, rt_physics3d.c
//
//===----------------------------------------------------------------------===//

#ifdef VIPER_ENABLE_GRAPHICS

#include "rt_collider3d.h"
#include "rt_graphics3d_ids.h"
#include "rt_physics3d.h"
#include "rt_physics3d_internal.h"
#include "rt_physics3d_query_internal.h"
#include "rt_trap.h"

#include <float.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/// @brief Character3D controller payload: the kinematic body it drives, its
///   owning world, step-up height, walkable-slope cosine, grounded/sliding state,
///   dynamic-body interaction tuning, and the retained ground body (platforms).
typedef struct {
    void *vptr;
    rt_body3d *body;
    rt_world3d *world;
    double step_height;
    double slope_limit_cos;
    int8_t is_grounded;
    int8_t was_grounded;
    int8_t is_sliding;      /* standing on a non-walkable surface this step */
    int8_t collide_dynamic; /* dynamic bodies block/push (default on) */
    int8_t ride_platforms;  /* pre-displace with kinematic ground motion */
    double push_strength;   /* dynamic push impulse scale (0 = block only) */
    rt_body3d *ground_body; /* retained body under our feet, NULL airborne */
    rt_body3d *pushed_body; /* impulse-once-per-step guard, weak in-step ref */
    /* Per-Move broadphase shortlist: bodies whose AABB overlaps the swept move
     * volume. While active, position probes narrow-phase only this list instead
     * of every world body. Weak in-step refs; the world cannot mutate its body
     * set during a Move. */
    rt_body3d **move_candidates;
    int32_t move_candidate_count;
    int32_t move_candidate_capacity;
    int8_t move_candidates_active;
} rt_character3d;

/// @brief Validate @p obj as a Character3D handle (NULL on mismatch).
static rt_character3d *character3d_checked(void *obj) {
    return (rt_character3d *)rt_g3d_checked_or_null(obj, RT_G3D_CHARACTER3D_CLASS_ID);
}

/*==========================================================================
 * Character Controller
 *=========================================================================*/

typedef struct {
    rt_body3d *body;
    double normal[3];
    double depth;
    double fraction;
    int8_t hit;
} rt_character_hit3d;

#define CHARACTER3D_COORD_ABS_MAX 1000000000000.0
#define CHARACTER3D_MOVE_ABS_MAX 1000.0
#define CHARACTER3D_STEP_HEIGHT_MAX 100.0
#define CHARACTER3D_DT_MAX 1.0

/// @brief Clamp a character/trigger coordinate to a finite physics state range.
static double character3d_saturate_coord(double value) {
    if (!isfinite(value))
        return 0.0;
    if (value > CHARACTER3D_COORD_ABS_MAX)
        return CHARACTER3D_COORD_ABS_MAX;
    if (value < -CHARACTER3D_COORD_ABS_MAX)
        return -CHARACTER3D_COORD_ABS_MAX;
    return value;
}

/// @brief Clamp a Vec3 in place for character controller math.
static void character3d_sanitize_vec3(double v[3]) {
    if (!v)
        return;
    v[0] = character3d_saturate_coord(v[0]);
    v[1] = character3d_saturate_coord(v[1]);
    v[2] = character3d_saturate_coord(v[2]);
}

/// @brief Normalize a collision normal, returning 0 when no reliable direction exists.
static int character3d_sanitize_contact_normal(double out[3], const double *normal) {
    if (!out || !normal)
        return 0;
    out[0] = character3d_saturate_coord(normal[0]);
    out[1] = character3d_saturate_coord(normal[1]);
    out[2] = character3d_saturate_coord(normal[2]);
    return vec3_normalize_in_place(out) > 1e-12;
}

/// @brief Copy and cap a movement vector, preserving direction for extreme finite velocities.
static double character3d_sanitize_delta(const double *src, double out[3]) {
    double len;
    if (!src || !out)
        return 0.0;
    out[0] = character3d_saturate_coord(src[0]);
    out[1] = character3d_saturate_coord(src[1]);
    out[2] = character3d_saturate_coord(src[2]);
    len = vec3_len(out);
    if (!isfinite(len) || len <= 1e-12) {
        vec3_set(out, 0.0, 0.0, 0.0);
        return 0.0;
    }
    if (len > CHARACTER3D_MOVE_ABS_MAX) {
        double scale = CHARACTER3D_MOVE_ABS_MAX / len;
        out[0] *= scale;
        out[1] *= scale;
        out[2] *= scale;
        len = CHARACTER3D_MOVE_ABS_MAX;
    }
    return len;
}

/// @brief Clamp controller step heights to a non-negative, physically usable range.
static double character3d_sanitize_step_height(double value) {
    if (!isfinite(value) || value <= 0.0)
        return 0.0;
    return value > CHARACTER3D_STEP_HEIGHT_MAX ? CHARACTER3D_STEP_HEIGHT_MAX : value;
}

// Character controller — built on top of Body3D with custom motion
// resolution: kinematic-style sweeps + slide along surfaces, optional
// step-up over small obstacles, ground probing for "is grounded" state.

/// @brief Retain @p body into the controller's ground slot (NULL clears).
static void character3d_retain_ground_body(rt_character3d *ctrl, rt_body3d *body) {
    if (!ctrl || ctrl->ground_body == body)
        return;
    if (body)
        rt_obj_retain_maybe(body);
    if (ctrl->ground_body && rt_obj_release_check0(ctrl->ground_body))
        rt_obj_free(ctrl->ground_body);
    ctrl->ground_body = body;
}

/// @brief Update the controller's grounded flag and store the latest ground normal.
///
/// Negates the contact normal because the contact normal points from
/// the body toward the ground; we want the surface normal pointing up.
static void character3d_set_ground_state(rt_character3d *ctrl,
                                         int8_t grounded,
                                         const double *normal) {
    if (!ctrl || !ctrl->body)
        return;
    ctrl->is_grounded = grounded;
    ctrl->body->is_grounded = grounded;
    if (!grounded)
        character3d_retain_ground_body(ctrl, NULL);
    if (grounded && normal) {
        double contact_normal[3];
        if (character3d_sanitize_contact_normal(contact_normal, normal)) {
            ctrl->body->ground_normal[0] = -contact_normal[0];
            ctrl->body->ground_normal[1] = -contact_normal[1];
            ctrl->body->ground_normal[2] = -contact_normal[2];
        } else {
            ctrl->body->ground_normal[0] = 0.0;
            ctrl->body->ground_normal[1] = 1.0;
            ctrl->body->ground_normal[2] = 0.0;
        }
    } else {
        ctrl->body->ground_normal[0] = 0.0;
        ctrl->body->ground_normal[1] = 1.0;
        ctrl->body->ground_normal[2] = 0.0;
    }
}

/// @brief True if the surface (negated contact normal) is below the slope limit.
///
/// `slope_limit_cos = cos(max_slope_angle)`; a "walkable" surface has
/// `normal_y >= cos(angle)`. Used to gate ground-snapping and step-up.
static int character3d_normal_is_walkable(const rt_character3d *ctrl, const double *normal) {
    double contact_normal[3];
    return ctrl && normal && character3d_sanitize_contact_normal(contact_normal, normal) &&
           (-contact_normal[1] >= ctrl->slope_limit_cos);
}

/// @brief Filter for which world bodies the controller should slide against.
///
/// Excludes self and triggers. Dynamic bodies block (and are pushed via
/// `push_strength`) by default; `rt_character3d_set_collide_dynamic(ctrl, 0)`
/// restores the legacy ghost-through behavior. Honors the standard
/// layer/mask filter.
static int character3d_candidate_body(const rt_character3d *ctrl, const rt_body3d *other) {
    if (!ctrl || !ctrl->body || !ctrl->world || !other)
        return 0;
    if (other == ctrl->body)
        return 0;
    if (other->is_trigger)
        return 0;
    if (other->motion_mode == PH3D_MODE_DYNAMIC && !ctrl->collide_dynamic)
        return 0;
    return bodies_can_collide(ctrl->body, other);
}

/// @brief Ensure the per-Move candidate shortlist can hold @p needed body pointers.
static int character3d_reserve_move_candidates(rt_character3d *ctrl, int32_t needed) {
    rt_body3d **grown;
    int32_t new_cap;
    if (!ctrl || needed < 0)
        return 0;
    if (ctrl->move_candidate_capacity >= needed)
        return 1;
    new_cap = ctrl->move_candidate_capacity > 0 ? ctrl->move_candidate_capacity : 16;
    while (new_cap < needed) {
        if (new_cap > INT32_MAX / 2)
            return 0;
        new_cap *= 2;
    }
    grown = (rt_body3d **)realloc(ctrl->move_candidates, (size_t)new_cap * sizeof(*grown));
    if (!grown)
        return 0;
    ctrl->move_candidates = grown;
    ctrl->move_candidate_capacity = new_cap;
    return 1;
}

/// @brief Build the per-Move broadphase shortlist for a swept move volume.
///
/// Collects every candidate body whose cached broadphase AABB overlaps the
/// conservative volume the controller can touch this Move: the start position
/// expanded on every axis by the total move length (sliding can redirect
/// motion onto any axis), the capsule extents, the step-up/probe reach, and a
/// safety margin. Falls back to full-world scans (shortlist inactive) if the
/// broadphase cannot be built.
static void character3d_begin_move_candidates(rt_character3d *ctrl,
                                              const double *start,
                                              double move_len) {
    ctrl->move_candidates_active = 0;
    ctrl->move_candidate_count = 0;
    if (!ctrl->world || !ctrl->body)
        return;

    int32_t entry_count = world3d_build_query_broadphase(ctrl->world);
    if (entry_count < 0)
        return;

    double half_height = isfinite(ctrl->body->height) ? fabs(ctrl->body->height) * 0.5 : 1.0;
    double radius = isfinite(ctrl->body->radius) ? fabs(ctrl->body->radius) : 0.5;
    double step = character3d_sanitize_step_height(ctrl->step_height);
    double reach = move_len + half_height + radius + step + 0.6;
    if (!isfinite(reach) || reach <= 0.0)
        return;

    double qmin[3];
    double qmax[3];
    for (int axis = 0; axis < 3; axis++) {
        qmin[axis] = start[axis] - reach;
        qmax[axis] = start[axis] + reach;
    }

    for (int32_t i = 0; i < entry_count; i++) {
        const ph3d_broadphase_entry *entry = &ctrl->world->broadphase_entries[i];
        if (!query_entry_overlaps_bounds(entry, qmin, qmax))
            continue;
        if (!character3d_candidate_body(ctrl, entry->body))
            continue;
        if (!character3d_reserve_move_candidates(ctrl, ctrl->move_candidate_count + 1))
            return; /* stay inactive: full scan remains correct */
        ctrl->move_candidates[ctrl->move_candidate_count++] = entry->body;
    }
    ctrl->move_candidates_active = 1;
}

/// @brief Deactivate the per-Move shortlist (buffer is kept for reuse).
static void character3d_end_move_candidates(rt_character3d *ctrl) {
    if (!ctrl)
        return;
    ctrl->move_candidates_active = 0;
    ctrl->move_candidate_count = 0;
}

/// @brief Probe what the controller would collide with at a given position.
///
/// Temporarily moves the body to `pos`, runs the standard narrow-phase
/// against every candidate body (the per-Move broadphase shortlist when one
/// is active, otherwise every world body), restores the original position,
/// and returns the deepest contact (if any). Used for both penetration
/// resolution and binary-searched sweeps.
static int character3d_test_position(rt_character3d *ctrl,
                                     const double *pos,
                                     rt_character_hit3d *out_hit) {
    if (!ctrl || !ctrl->body || !ctrl->world)
        return 0;

    rt_body3d *body = ctrl->body;
    double saved[3] = {body->position[0], body->position[1], body->position[2]};
    double test_pos[3] = {pos[0], pos[1], pos[2]};
    character3d_sanitize_vec3(test_pos);
    body->position[0] = test_pos[0];
    body->position[1] = test_pos[1];
    body->position[2] = test_pos[2];

    rt_body3d **candidates =
        ctrl->move_candidates_active ? ctrl->move_candidates : ctrl->world->bodies;
    int32_t candidate_count =
        ctrl->move_candidates_active ? ctrl->move_candidate_count : ctrl->world->body_count;

    rt_character_hit3d best = {0};
    for (int32_t i = 0; i < candidate_count; i++) {
        rt_body3d *other = candidates[i];
        double normal[3], depth;
        if (!character3d_candidate_body(ctrl, other))
            continue;
        if (!test_collision(body, other, normal, &depth, NULL, NULL, NULL, NULL, NULL))
            continue;
        if (!isfinite(depth) || depth <= 0.0 || !character3d_sanitize_contact_normal(normal, normal))
            continue;
        if (!best.hit || depth > best.depth) {
            best.hit = 1;
            best.body = other;
            best.depth = depth > CHARACTER3D_MOVE_ABS_MAX ? CHARACTER3D_MOVE_ABS_MAX : depth;
            vec3_copy(best.normal, normal);
        }
    }

    body->position[0] = saved[0];
    body->position[1] = saved[1];
    body->position[2] = saved[2];

    if (best.hit && out_hit)
        *out_hit = best;
    return best.hit;
}

/// @brief Push the controller out of any penetration it currently has.
///
/// Up to 6 iterations: probe at current position, push along the
/// deepest normal by `depth + 1e-4` epsilon, repeat. Bails early
/// when no penetration remains. Bounded so degenerate stuck cases
/// terminate quickly.
static void character3d_resolve_penetration(rt_character3d *ctrl) {
    if (!ctrl || !ctrl->body)
        return;
    for (int iter = 0; iter < 6; iter++) {
        rt_character_hit3d hit;
        double pos[3] = {ctrl->body->position[0], ctrl->body->position[1], ctrl->body->position[2]};
        character3d_sanitize_vec3(pos);
        if (!character3d_test_position(ctrl, pos, &hit))
            return;
        double push = hit.depth + 1e-4;
        if (!isfinite(push) || push <= 0.0)
            return;
        if (push > CHARACTER3D_MOVE_ABS_MAX)
            push = CHARACTER3D_MOVE_ABS_MAX;
        ctrl->body->position[0] = character3d_saturate_coord(ctrl->body->position[0] - hit.normal[0] * push);
        ctrl->body->position[1] = character3d_saturate_coord(ctrl->body->position[1] - hit.normal[1] * push);
        ctrl->body->position[2] = character3d_saturate_coord(ctrl->body->position[2] - hit.normal[2] * push);
    }
}

/// @brief Sweep the controller along `delta`, stopping at the first contact.
///
/// Steps in `radius/4`-sized substeps; on the first substep that hits,
/// 14 iterations of bisection refine the impact `t`. On no-hit, body
/// is moved to the end position and the function returns 0. Step count
/// is bounded to 128.
static int character3d_sweep(rt_character3d *ctrl,
                             const double *delta,
                             rt_character_hit3d *out_hit) {
    double move_delta[3];
    double move_len;
    if (!ctrl || !ctrl->body)
        return 0;
    move_len = character3d_sanitize_delta(delta, move_delta);
    if (move_len <= 1e-12)
        return 0;

    rt_body3d *body = ctrl->body;
    double start[3] = {body->position[0], body->position[1], body->position[2]};
    character3d_sanitize_vec3(start);
    double end[3] = {character3d_saturate_coord(start[0] + move_delta[0]),
                     character3d_saturate_coord(start[1] + move_delta[1]),
                     character3d_saturate_coord(start[2] + move_delta[2])};
    double step_dist = body->radius > 1e-6 ? body->radius * 0.25 : 0.05;
    double step_count;
    int steps;
    double prev_t = 0.0;
    rt_character_hit3d hit;

    if (!isfinite(step_dist) || step_dist < 0.05)
        step_dist = 0.05;
    step_count = ceil(move_len / step_dist);
    if (!isfinite(step_count) || step_count > 128.0)
        steps = 128;
    else if (step_count < 1.0)
        steps = 1;
    else
        steps = (int)step_count;
    if (steps < 1)
        steps = 1;

    for (int s = 1; s <= steps; s++) {
        double t = (double)s / (double)steps;
        double pos[3] = {character3d_saturate_coord(start[0] + move_delta[0] * t),
                         character3d_saturate_coord(start[1] + move_delta[1] * t),
                         character3d_saturate_coord(start[2] + move_delta[2] * t)};
        if (!character3d_test_position(ctrl, pos, &hit)) {
            prev_t = t;
            continue;
        }

        {
            double lo = prev_t;
            double hi = t;
            rt_character_hit3d best_hit = hit;
            for (int iter = 0; iter < 14; iter++) {
                double mid = (lo + hi) * 0.5;
                double mid_pos[3] = {character3d_saturate_coord(start[0] + move_delta[0] * mid),
                                     character3d_saturate_coord(start[1] + move_delta[1] * mid),
                                     character3d_saturate_coord(start[2] + move_delta[2] * mid)};
                if (character3d_test_position(ctrl, mid_pos, &hit)) {
                    hi = mid;
                    best_hit = hit;
                } else {
                    lo = mid;
                }
            }

            body->position[0] = character3d_saturate_coord(start[0] + move_delta[0] * lo);
            body->position[1] = character3d_saturate_coord(start[1] + move_delta[1] * lo);
            body->position[2] = character3d_saturate_coord(start[2] + move_delta[2] * lo);
            best_hit.hit = 1;
            best_hit.fraction = clampd(lo, 0.0, 1.0);
            if (out_hit)
                *out_hit = best_hit;
            return 1;
        }
    }

    body->position[0] = end[0];
    body->position[1] = end[1];
    body->position[2] = end[2];
    if (out_hit) {
        rt_character_hit3d zero = {0};
        *out_hit = zero;
    }
    return 0;
}

/// @brief Drop the controller 5cm and check for a walkable surface.
///
/// Used to detect grounded state when the controller is just barely
/// above the floor (after a small jump or when sliding down a slight
/// slope). Updates the body's grounded flag accordingly.
static int character3d_probe_ground(rt_character3d *ctrl) {
    if (!ctrl || !ctrl->body)
        return 0;
    double probe_pos[3] = {
        ctrl->body->position[0], ctrl->body->position[1] - 0.05, ctrl->body->position[2]};
    rt_character_hit3d hit;
    if (character3d_test_position(ctrl, probe_pos, &hit)) {
        if (character3d_normal_is_walkable(ctrl, hit.normal)) {
            character3d_set_ground_state(ctrl, 1, hit.normal);
            character3d_retain_ground_body(ctrl, hit.body);
            return 1;
        }
        /* Resting against a too-steep surface: not grounded, but sliding. */
        ctrl->is_sliding = 1;
    }
    character3d_set_ground_state(ctrl, 0, NULL);
    return 0;
}

/// @brief Attempt to step up over a small obstacle (FPS-style stair climb).
///
/// Three-phase test:
///   1. Sweep up by `step_height`. If blocked, abort.
///   2. Sweep horizontally by the leftover delta. If blocked, abort.
///   3. Sweep down (slightly past `step_height`) onto the new surface.
///      If the new surface is walkable, commit and mark grounded.
/// On any failure the controller is restored to its original position.
static int character3d_try_step(rt_character3d *ctrl, const double *horizontal_delta) {
    double step_delta[3];
    if (!ctrl || !ctrl->body || ctrl->step_height <= 1e-6 ||
        character3d_sanitize_delta(horizontal_delta, step_delta) <= 1e-12)
        return 0;

    double start[3] = {ctrl->body->position[0], ctrl->body->position[1], ctrl->body->position[2]};
    character3d_sanitize_vec3(start);
    double up[3] = {0.0, ctrl->step_height, 0.0};
    rt_character_hit3d hit;

    if (character3d_sweep(ctrl, up, &hit)) {
        ctrl->body->position[0] = start[0];
        ctrl->body->position[1] = start[1];
        ctrl->body->position[2] = start[2];
        return 0;
    }
    character3d_resolve_penetration(ctrl);

    if (character3d_sweep(ctrl, step_delta, &hit)) {
        ctrl->body->position[0] = start[0];
        ctrl->body->position[1] = start[1];
        ctrl->body->position[2] = start[2];
        return 0;
    }
    character3d_resolve_penetration(ctrl);

    {
        double down[3] = {0.0, -(ctrl->step_height + 0.05), 0.0};
        if (character3d_sweep(ctrl, down, &hit) &&
            character3d_normal_is_walkable(ctrl, hit.normal)) {
            character3d_set_ground_state(ctrl, 1, hit.normal);
            character3d_retain_ground_body(ctrl, hit.body);
            return 1;
        }
    }

    ctrl->body->position[0] = start[0];
    ctrl->body->position[1] = start[1];
    ctrl->body->position[2] = start[2];
    return 0;
}

/// @brief Push a blocking dynamic body once per step: impulse along the contact
///   normal proportional to the approach speed, mass-ratio scaled so light props
///   yield and heavy props wall the controller. Applied only on the resolved
///   contact (never inside sweep bisection) so bodies cannot gain energy.
static void character3d_push_dynamic(rt_character3d *ctrl,
                                     const rt_character_hit3d *hit,
                                     const double *attempted_delta,
                                     double dt) {
    if (!ctrl || !ctrl->body || !hit || !hit->hit || !hit->body)
        return;
    if (hit->body->motion_mode != PH3D_MODE_DYNAMIC)
        return;
    if (ctrl->push_strength <= 0.0 || !isfinite(dt) || dt <= 1e-9)
        return;
    if (ctrl->pushed_body == hit->body)
        return; /* once per contact per step */
    double v_into = vec3_dot(attempted_delta, hit->normal) / dt;
    if (!isfinite(v_into) || v_into <= 0.0)
        return;
    double other_mass = hit->body->mass > 1e-9 ? hit->body->mass : 1e-9;
    double ratio = ctrl->body->mass / other_mass;
    if (!isfinite(ratio) || ratio > 1.0)
        ratio = 1.0;
    double mag = ctrl->push_strength * ratio * v_into;
    if (!isfinite(mag) || mag <= 0.0)
        return;
    /* Approximate contact point: capsule surface along the contact normal. */
    double px = character3d_saturate_coord(ctrl->body->position[0] +
                                           hit->normal[0] * ctrl->body->radius);
    double py = character3d_saturate_coord(ctrl->body->position[1] +
                                           hit->normal[1] * ctrl->body->radius);
    double pz = character3d_saturate_coord(ctrl->body->position[2] +
                                           hit->normal[2] * ctrl->body->radius);
    rt_body3d_apply_impulse_at_point(hit->body,
                                     hit->normal[0] * mag,
                                     hit->normal[1] * mag,
                                     hit->normal[2] * mag,
                                     px,
                                     py,
                                     pz);
    rt_body3d_wake(hit->body);
    ctrl->pushed_body = hit->body;
}

/// @brief Slide-and-iterate motion solver — the heart of the controller's `Move`.
///
/// Up to 4 iterations of: resolve penetration → sweep → if hit,
/// project leftover motion onto the contact plane (or try the bounded
/// step-up path for a horizontal obstruction) → continue with the motion. This
/// gives the "slide along walls" feel typical of FPS controllers.
/// Vertical hits onto walkable surfaces also set the grounded flag
/// so gravity stops compounding.
static void character3d_move_axis(rt_character3d *ctrl,
                                  const double *initial_delta,
                                  int allow_step,
                                  double dt) {
    double remaining[3];
    if (character3d_sanitize_delta(initial_delta, remaining) <= 1e-12)
        return;
    for (int iter = 0; iter < 4; iter++) {
        rt_character_hit3d hit;
        double leftover[3];

        if (character3d_sanitize_delta(remaining, remaining) <= 1e-12)
            return;

        character3d_resolve_penetration(ctrl);
        if (!character3d_sweep(ctrl, remaining, &hit))
            return;

        character3d_push_dynamic(ctrl, &hit, remaining, dt);

        leftover[0] = remaining[0] * (1.0 - hit.fraction);
        leftover[1] = remaining[1] * (1.0 - hit.fraction);
        leftover[2] = remaining[2] * (1.0 - hit.fraction);
        character3d_sanitize_delta(leftover, leftover);

        /* A horizontal capsule sweep intersects the supporting heightfield as
         * soon as the ground rises, even when the sampled surface is almost
         * flat.  Treating that walkable contact only as a slide can repeatedly
         * consume the tiny horizontal remainder at cell boundaries, leaving a
         * controller unable to cross otherwise navigable rolling terrain.
         *
         * The existing three-phase step is also the correct bounded traversal
         * for this case: lift by the configured step height, cross the
         * remainder, then settle onto a walkable surface.  Applying it to every
         * horizontal obstruction (not just wall-like contacts) preserves the
         * slope limit and step-height contract while preventing ground contact
         * from becoming an invisible wall. */
        if (allow_step && character3d_try_step(ctrl, leftover))
            return;

        if (remaining[1] < 0.0 && character3d_normal_is_walkable(ctrl, hit.normal)) {
            character3d_set_ground_state(ctrl, 1, hit.normal);
            character3d_retain_ground_body(ctrl, hit.body);
            return;
        }
        if (remaining[1] < 0.0 && fabs(remaining[0]) + fabs(remaining[2]) < 1e-12)
            ctrl->is_sliding = 1; /* descending onto a too-steep surface */

        {
            double into = vec3_dot(leftover, hit.normal);
            if (isfinite(into) && into > 0.0) {
                leftover[0] = character3d_saturate_coord(leftover[0] - hit.normal[0] * into);
                leftover[1] = character3d_saturate_coord(leftover[1] - hit.normal[1] * into);
                leftover[2] = character3d_saturate_coord(leftover[2] - hit.normal[2] * into);
            } else {
                leftover[0] = leftover[1] = leftover[2] = 0.0;
            }
        }

        character3d_sanitize_delta(leftover, remaining);
    }
}

/// @brief GC finalizer for `Character3D` — release the body, world, and ground refs.
static void character3d_finalizer(void *obj) {
    rt_character3d *c = (rt_character3d *)obj;
    if (!c)
        return;
    free(c->move_candidates);
    c->move_candidates = NULL;
    c->move_candidate_count = 0;
    c->move_candidate_capacity = 0;
    c->move_candidates_active = 0;
    if (c->body && rt_obj_release_check0(c->body))
        rt_obj_free(c->body);
    c->body = NULL;
    if (c->world && rt_obj_release_check0(c->world))
        rt_obj_free(c->world);
    c->world = NULL;
    if (c->ground_body && rt_obj_release_check0(c->ground_body))
        rt_obj_free(c->ground_body);
    c->ground_body = NULL;
}

/// @brief `Physics3D.Character.New(radius, height, mass)` — make a capsule character.
///
/// Creates an internally-owned capsule body and wraps it. Defaults:
/// 30cm step height, 45° max walkable slope. The character is not
/// added to a world automatically — call `SetWorld` before using
/// `Move`.
void *rt_character3d_new(double radius, double height, double mass) {
    rt_character3d *c = (rt_character3d *)rt_obj_new_i64(RT_G3D_CHARACTER3D_CLASS_ID,
                                                         (int64_t)sizeof(rt_character3d));
    if (!c) {
        rt_trap("Physics3D.Character.New: allocation failed");
        return NULL;
    }
    c->vptr = NULL;
    c->body = (rt_body3d *)rt_body3d_new_capsule(radius, height, mass);
    if (!c->body) {
        if (rt_obj_release_check0(c))
            rt_obj_free(c);
        return NULL;
    }
    c->world = NULL;
    c->step_height = 0.3;
    c->slope_limit_cos = cos(45.0 * 3.14159265358979323846 / 180.0);
    c->is_grounded = 0;
    c->was_grounded = 0;
    c->is_sliding = 0;
    c->collide_dynamic = 1;
    c->ride_platforms = 1;
    c->push_strength = 1.0;
    c->ground_body = NULL;
    c->pushed_body = NULL;
    c->move_candidates = NULL;
    c->move_candidate_count = 0;
    c->move_candidate_capacity = 0;
    c->move_candidates_active = 0;
    rt_obj_set_finalizer(c, character3d_finalizer);
    return c;
}

/// @brief `Character3D.Move(velocity, dt)` — kinematic move with sliding.
///
/// Splits the velocity into horizontal (allows step-up) and vertical
/// (does not), runs `character3d_move_axis` for each, then probes the
/// ground if not already grounded. Updates the body's velocity to the
/// actual achieved displacement / dt — useful for animation systems
/// that read velocity off the controller.
void rt_character3d_move(void *obj, void *velocity_vec, double dt) {
    rt_character3d *ctrl = character3d_checked(obj);
    if (!ctrl || !rt_g3d_is_vec3(velocity_vec) || !isfinite(dt) || dt <= 0)
        return;
    if (dt > CHARACTER3D_DT_MAX)
        dt = CHARACTER3D_DT_MAX;
    rt_body3d *body = ctrl->body;
    if (!body)
        return;

    double velocity[3] = {ph3d_finite_or(rt_vec3_x(velocity_vec), 0.0),
                          ph3d_finite_or(rt_vec3_y(velocity_vec), 0.0),
                          ph3d_finite_or(rt_vec3_z(velocity_vec), 0.0)};
    character3d_sanitize_vec3(velocity);

    ctrl->was_grounded = ctrl->is_grounded;
    ctrl->pushed_body = NULL;
    ctrl->is_sliding = 0;

    /* Moving platforms: while grounded on a kinematic/static body that is
     * moving, pre-displace by the platform's step displacement (linear plus
     * yaw about the platform origin) BEFORE the swept move, so a wall on the
     * platform still blocks the ride. */
    if (ctrl->ride_platforms && ctrl->is_grounded && ctrl->ground_body &&
        ctrl->ground_body->motion_mode != PH3D_MODE_DYNAMIC) {
        rt_body3d *platform = ctrl->ground_body;
        double lin[3] = {ph3d_finite_or(platform->velocity[0], 0.0) * dt,
                         ph3d_finite_or(platform->velocity[1], 0.0) * dt,
                         ph3d_finite_or(platform->velocity[2], 0.0) * dt};
        double yaw = ph3d_finite_or(platform->angular_velocity[1], 0.0) * dt;
        double px = body->position[0];
        double pz = body->position[2];
        if (fabs(yaw) > 1e-12) {
            double ox = px - platform->position[0];
            double oz = pz - platform->position[2];
            double c = cos(yaw);
            double s = sin(yaw);
            px = platform->position[0] + ox * c - oz * s;
            pz = platform->position[2] + ox * s + oz * c;
        }
        body->position[0] = character3d_saturate_coord(px + lin[0]);
        body->position[1] = character3d_saturate_coord(body->position[1] + lin[1]);
        body->position[2] = character3d_saturate_coord(pz + lin[2]);
    }

    character3d_set_ground_state(ctrl, 0, NULL);

    {
        double start[3] = {body->position[0], body->position[1], body->position[2]};
        character3d_sanitize_vec3(start);
        double horizontal[3] = {velocity[0] * dt, 0.0, velocity[2] * dt};
        double vertical[3] = {0.0, velocity[1] * dt, 0.0};
        double move_len = vec3_len(horizontal) + vec3_len(vertical);

        character3d_begin_move_candidates(ctrl, start, move_len);
        character3d_resolve_penetration(ctrl);
        character3d_move_axis(ctrl, horizontal, 1, dt);
        character3d_move_axis(ctrl, vertical, 0, dt);
        character3d_resolve_penetration(ctrl);
        if (!ctrl->is_grounded)
            character3d_probe_ground(ctrl);
        character3d_end_move_candidates(ctrl);

        body->position[0] = character3d_saturate_coord(body->position[0]);
        body->position[1] = character3d_saturate_coord(body->position[1]);
        body->position[2] = character3d_saturate_coord(body->position[2]);
        body->velocity[0] = character3d_saturate_coord((body->position[0] - start[0]) / dt);
        body->velocity[1] = character3d_saturate_coord((body->position[1] - start[1]) / dt);
        body->velocity[2] = character3d_saturate_coord((body->position[2] - start[2]) / dt);
        ph3d_vec3_sanitize_state(body->velocity);
        /* The swept move mutated the body's position directly; stamp the
         * broadphase so later spatial queries (raycasts, overlaps, other
         * controllers' shortlists) see the new AABB instead of a stale cache. */
        body3d_touch_broadphase(body);
    }
}

/// @brief `Character3D.set_StepHeight(h)` — max obstacle height the controller can step over.
void rt_character3d_set_step_height(void *o, double h) {
    rt_character3d *c = character3d_checked(o);
    if (c)
        c->step_height = character3d_sanitize_step_height(h);
}

/// @brief `Character3D.GetStepHeight` — read the configured step height.
double rt_character3d_get_step_height(void *o) {
    rt_character3d *c = character3d_checked(o);
    return c ? character3d_sanitize_step_height(c->step_height) : 0.3;
}

/// @brief `Character3D.SetSlopeLimit(degrees)` — max walkable slope angle.
///
/// Stored as `cos(angle)` to make the per-step "is this surface walkable"
/// test a single comparison (no trig in the hot path).
void rt_character3d_set_slope_limit(void *o, double degrees) {
    rt_character3d *c = character3d_checked(o);
    if (c) {
        degrees = ph3d_finite_or(degrees, 45.0);
        degrees = clampd(degrees, 0.0, 89.9);
        c->slope_limit_cos = cos(degrees * 3.14159265358979323846 / 180.0);
        if (!isfinite(c->slope_limit_cos))
            c->slope_limit_cos = cos(45.0 * 3.14159265358979323846 / 180.0);
    }
}

/// @brief `Character3D.set_World(world)` — bind the character to a physics world.
///
/// Required before `Move` will collide against anything. Releases any
/// previous world reference and retains the new one. NULL detaches.
void rt_character3d_set_world(void *o, void *world) {
    rt_character3d *ctrl = character3d_checked(o);
    rt_world3d *w = world3d_checked(world);
    if (!ctrl)
        return;
    if (world && !w)
        return;
    if (ctrl->world == w)
        return;
    if (w)
        rt_obj_retain_maybe(w);
    if (ctrl->world && rt_obj_release_check0(ctrl->world))
        rt_obj_free(ctrl->world);
    ctrl->world = w;
}

/// @brief `Character3D.GetWorld` — borrowed reference to the bound world.
void *rt_character3d_get_world(void *o) {
    rt_character3d *c = character3d_checked(o);
    return c ? c->world : NULL;
}

/// @brief `Character3D.IsGrounded` — true when standing on a walkable surface.
int8_t rt_character3d_is_grounded(void *o) {
    rt_character3d *c = character3d_checked(o);
    return c ? c->is_grounded : 0;
}

/// @brief `Character3D.JustLanded` — edge-detect: true on the first frame after landing.
///
/// Compares this frame's grounded state to the previous frame's. Useful
/// for landing animations, fall-damage triggers, dust puffs, etc.
int8_t rt_character3d_just_landed(void *o) {
    rt_character3d *c = character3d_checked(o);
    if (!c)
        return 0;
    return c->is_grounded && !c->was_grounded;
}

/// @brief `Character3D.GetPosition` — fresh `Vec3` of the body's position.
void *rt_character3d_get_position(void *o) {
    rt_character3d *c = character3d_checked(o);
    if (!c)
        return rt_vec3_new(0, 0, 0);
    return rt_body3d_get_position(c->body);
}

/// @brief `Character3D.SetPosition(x, y, z)` — teleport the controller.
///
/// Direct delegation to the underlying body. Caller is responsible for
/// avoiding teleports into geometry.
void rt_character3d_set_position(void *o, double x, double y, double z) {
    rt_character3d *c = character3d_checked(o);
    if (c)
        rt_body3d_set_position(c->body, x, y, z);
}

/// @brief `Character3D.TrySetHeight(h)` — crouch/stand capsule resize.
///
/// Shrinking always succeeds and keeps the feet planted (the capsule center
/// drops by half the height delta). Growing first tests the stand pose with
/// the enlarged capsule and fails (returns 0) when blocked — `TryStand`
/// semantics come free. The capsule bounds revision is bumped so the
/// broadphase re-inserts the body.
int8_t rt_character3d_try_set_height(void *o, double height) {
    rt_character3d *c = character3d_checked(o);
    if (!c || !c->body || !c->body->collider)
        return 0;
    if (!isfinite(height) || height <= 0.0)
        return 0;
    double radius = c->body->radius > 0.0 ? c->body->radius
                                          : rt_collider3d_get_radius_raw(c->body->collider);
    if (radius <= 0.0)
        return 0;
    if (height < radius * 2.0)
        height = radius * 2.0;
    double old_height = rt_collider3d_get_height_raw(c->body->collider);
    if (old_height <= 0.0)
        old_height = c->body->height;
    if (!isfinite(old_height) || old_height <= 0.0)
        return 0;
    if (fabs(height - old_height) < 1e-12)
        return 1;
    /* Feet stay planted: center shifts by half the height delta. */
    double new_center_y =
        character3d_saturate_coord(c->body->position[1] + (height - old_height) * 0.5);
    if (height < old_height) {
        rt_collider3d_reset_capsule_raw(c->body->collider, radius, height);
        body3d_update_shape_cache_from_collider(c->body);
        c->body->position[1] = new_center_y;
        body3d_touch_broadphase(c->body);
        return 1;
    }
    /* Growing: probe the stand pose (lifted 1 cm so resting ground contact
     * does not read as a blocker) before committing. */
    rt_collider3d_reset_capsule_raw(c->body->collider, radius, height);
    body3d_update_shape_cache_from_collider(c->body);
    {
        rt_character_hit3d hit;
        double stand_pos[3] = {c->body->position[0], new_center_y + 0.01, c->body->position[2]};
        if (character3d_test_position(c, stand_pos, &hit)) {
            rt_collider3d_reset_capsule_raw(c->body->collider, radius, old_height);
            body3d_update_shape_cache_from_collider(c->body);
            return 0;
        }
    }
    c->body->position[1] = new_center_y;
    body3d_touch_broadphase(c->body);
    return 1;
}

/// @brief `Character3D.set_Height(h)` — property form of TrySetHeight (result ignored).
void rt_character3d_set_height(void *o, double height) {
    (void)rt_character3d_try_set_height(o, height);
}

/// @brief `Character3D.get_Height` — current capsule height including caps.
double rt_character3d_get_height(void *o) {
    rt_character3d *c = character3d_checked(o);
    if (!c || !c->body)
        return 0.0;
    double height = c->body->collider ? rt_collider3d_get_height_raw(c->body->collider) : 0.0;
    return height > 0.0 ? height : c->body->height;
}

/// @brief `Character3D.set_PushStrength(s)` — dynamic push impulse scale (0 = block only).
void rt_character3d_set_push_strength(void *o, double strength) {
    rt_character3d *c = character3d_checked(o);
    if (c)
        c->push_strength = (isfinite(strength) && strength > 0.0) ? clampd(strength, 0.0, 1000.0)
                                                                  : 0.0;
}

/// @brief `Character3D.get_PushStrength` — dynamic push impulse scale.
double rt_character3d_get_push_strength(void *o) {
    rt_character3d *c = character3d_checked(o);
    return c ? c->push_strength : 0.0;
}

/// @brief `Character3D.set_CollideDynamic(on)` — dynamic bodies block/push (default)
///   or ghost through (legacy compatibility).
void rt_character3d_set_collide_dynamic(void *o, int8_t enabled) {
    rt_character3d *c = character3d_checked(o);
    if (c)
        c->collide_dynamic = enabled ? 1 : 0;
}

/// @brief `Character3D.get_CollideDynamic` — whether dynamic bodies block the controller.
int8_t rt_character3d_get_collide_dynamic(void *o) {
    rt_character3d *c = character3d_checked(o);
    return c ? c->collide_dynamic : 0;
}

/// @brief `Character3D.set_RidePlatforms(on)` — track kinematic ground motion.
void rt_character3d_set_ride_platforms(void *o, int8_t enabled) {
    rt_character3d *c = character3d_checked(o);
    if (c)
        c->ride_platforms = enabled ? 1 : 0;
}

/// @brief `Character3D.get_RidePlatforms` — whether the controller rides platforms.
int8_t rt_character3d_get_ride_platforms(void *o) {
    rt_character3d *c = character3d_checked(o);
    return c ? c->ride_platforms : 0;
}

/// @brief `Character3D.IsSliding` — true while resting on a too-steep surface.
int8_t rt_character3d_is_sliding(void *o) {
    rt_character3d *c = character3d_checked(o);
    return c ? c->is_sliding : 0;
}

/// @brief `Character3D.GetGroundBody` — borrowed body under the controller's feet
///   (NULL while airborne). Gameplay uses it for conveyors and surface queries.
void *rt_character3d_get_ground_body(void *o) {
    rt_character3d *c = character3d_checked(o);
    return c ? c->ground_body : NULL;
}

/*==========================================================================
 * Trigger3D — standalone AABB zone with enter/exit edge detection
 *=========================================================================*/

#define TRG3D_MAX_TRACKED 64

typedef struct {
    void *vptr;
    double bounds_min[3];
    double bounds_max[3];
    void *tracked_bodies[TRG3D_MAX_TRACKED];
    int8_t was_inside[TRG3D_MAX_TRACKED];
    int8_t is_inside[TRG3D_MAX_TRACKED];
    int32_t tracked_count;
    int32_t enter_count;
    int32_t exit_count;
} rt_trigger3d;

/// @brief Validate @p obj as a Trigger3D handle and return its typed pointer (NULL on mismatch).
static rt_trigger3d *trigger3d_checked(void *obj) {
    return (rt_trigger3d *)rt_g3d_checked_or_null(obj, RT_G3D_TRIGGER3D_CLASS_ID);
}

/// @brief GC finalizer for `Trigger3D` — no-op (tracked-body refs are weak).
///
/// Tracked bodies are stored as raw pointers (we don't retain them
/// because the trigger is only an observer). Weak refs is fine here:
/// if a tracked body is destroyed the next `Update` will discover the
/// stale pointer and clean it up.
static void trigger3d_finalizer(void *obj) {
    (void)obj;
}

/// @brief Store ordered, finite trigger bounds.
static void trigger3d_set_bounds_raw(
    rt_trigger3d *t, double x0, double y0, double z0, double x1, double y1, double z1) {
    double a[3] = {character3d_saturate_coord(x0),
                   character3d_saturate_coord(y0),
                   character3d_saturate_coord(z0)};
    double b[3] = {character3d_saturate_coord(x1),
                   character3d_saturate_coord(y1),
                   character3d_saturate_coord(z1)};
    if (!t)
        return;
    t->bounds_min[0] = a[0] < b[0] ? a[0] : b[0];
    t->bounds_min[1] = a[1] < b[1] ? a[1] : b[1];
    t->bounds_min[2] = a[2] < b[2] ? a[2] : b[2];
    t->bounds_max[0] = a[0] > b[0] ? a[0] : b[0];
    t->bounds_max[1] = a[1] > b[1] ? a[1] : b[1];
    t->bounds_max[2] = a[2] > b[2] ? a[2] : b[2];
}

/// @brief `Trigger3D.New(x0, y0, z0, x1, y1, z1)` — make an axis-aligned trigger zone.
///
/// Auto-orders the corners so caller can pass them in any order. Up to
/// 64 bodies are tracked for enter/exit edge detection.
void *rt_trigger3d_new(double x0, double y0, double z0, double x1, double y1, double z1) {
    rt_trigger3d *t =
        (rt_trigger3d *)rt_obj_new_i64(RT_G3D_TRIGGER3D_CLASS_ID, (int64_t)sizeof(rt_trigger3d));
    if (!t) {
        rt_trap("Trigger3D.New: allocation failed");
        return NULL;
    }
    {
        rt_trigger3d zero = {0};
        *t = zero;
    }
    trigger3d_set_bounds_raw(t, x0, y0, z0, x1, y1, z1);
    rt_obj_set_finalizer(t, trigger3d_finalizer);
    return t;
}

/// @brief `Trigger3D.Contains(point)` — point-in-AABB test for a `Vec3`.
///
/// Synchronous query; doesn't update enter/exit state. Use this for
/// ad-hoc "is the player in the safe zone" checks; use `Update` +
/// `EnterCount`/`ExitCount` for transition-based logic.
int8_t rt_trigger3d_contains(void *obj, void *point) {
    rt_trigger3d *t = trigger3d_checked(obj);
    if (!t || !rt_g3d_is_vec3(point))
        return 0;
    double px = rt_vec3_x(point), py = rt_vec3_y(point), pz = rt_vec3_z(point);
    if (!isfinite(px) || !isfinite(py) || !isfinite(pz))
        return 0;
    px = character3d_saturate_coord(px);
    py = character3d_saturate_coord(py);
    pz = character3d_saturate_coord(pz);
    return (px >= t->bounds_min[0] && px <= t->bounds_max[0] && py >= t->bounds_min[1] &&
            py <= t->bounds_max[1] && pz >= t->bounds_min[2] && pz <= t->bounds_max[2])
               ? 1
               : 0;
}

/// @brief Find a tracked body's slot, or claim a new slot if the table has room.
///
/// Returns -1 when the 64-slot table is full; the caller skips the
/// body for this frame in that case.
static int32_t trigger3d_find_or_add(rt_trigger3d *t, void *body) {
    for (int32_t i = 0; i < t->tracked_count; i++)
        if (t->tracked_bodies[i] == body)
            return i;
    if (t->tracked_count >= TRG3D_MAX_TRACKED)
        return -1;
    int32_t idx = t->tracked_count++;
    t->tracked_bodies[idx] = body;
    t->was_inside[idx] = 0;
    t->is_inside[idx] = 0;
    return idx;
}

/// @brief `Trigger3D.Update(world)` — recompute occupancy and edge counts.
///
/// Tests every body in `world` for point-in-AABB inclusion (using body
/// center as the query point). Diffs current frame vs. previous to
/// produce `enter_count` and `exit_count` totals — no per-body events
/// are stored, so callers learn "how many entered" but not "which".
/// Run once per frame after `World3D.Step`.
void rt_trigger3d_update(void *obj, void *world_obj) {
    rt_trigger3d *t = trigger3d_checked(obj);
    rt_world3d *w = world3d_checked(world_obj);
    int8_t seen[TRG3D_MAX_TRACKED];
    if (!t || !w)
        return;
    memset(seen, 0, sizeof(seen));

    /* Swap current → previous */
    for (int32_t i = 0; i < t->tracked_count; i++) {
        t->was_inside[i] = t->is_inside[i];
        t->is_inside[i] = 0;
    }
    t->enter_count = 0;
    t->exit_count = 0;

    /* Test every body in the world */
    for (int32_t i = 0; i < w->body_count; i++) {
        rt_body3d *b = w->bodies[i];
        if (!b)
            continue;

        /* Point-in-AABB test using body center */
        double pos[3] = {b->position[0], b->position[1], b->position[2]};
        character3d_sanitize_vec3(pos);
        int8_t inside = (pos[0] >= t->bounds_min[0] && pos[0] <= t->bounds_max[0] &&
                         pos[1] >= t->bounds_min[1] && pos[1] <= t->bounds_max[1] &&
                         pos[2] >= t->bounds_min[2] && pos[2] <= t->bounds_max[2])
                            ? 1
                            : 0;

        int32_t idx = trigger3d_find_or_add(t, b);
        if (idx < 0)
            continue; /* tracking full */

        seen[idx] = 1;
        t->is_inside[idx] = inside;
        if (inside && !t->was_inside[idx])
            t->enter_count++;
        if (!inside && t->was_inside[idx])
            t->exit_count++;
    }

    /* Weakly tracked bodies absent from the world left the trigger/world.
     * Emit exit once for bodies that were previously inside, then forget them. */
    for (int32_t i = 0; i < t->tracked_count;) {
        if (seen[i]) {
            i++;
            continue;
        }
        if (t->was_inside[i])
            t->exit_count++;
        for (int32_t j = i; j < t->tracked_count - 1; j++) {
            t->tracked_bodies[j] = t->tracked_bodies[j + 1];
            t->was_inside[j] = t->was_inside[j + 1];
            t->is_inside[j] = t->is_inside[j + 1];
            seen[j] = seen[j + 1];
        }
        t->tracked_count--;
        t->tracked_bodies[t->tracked_count] = NULL;
        t->was_inside[t->tracked_count] = 0;
        t->is_inside[t->tracked_count] = 0;
        seen[t->tracked_count] = 0;
    }
}

/// @brief `Trigger3D.EnterCount` — bodies that entered this trigger this frame.
int64_t rt_trigger3d_get_enter_count(void *obj) {
    rt_trigger3d *t = trigger3d_checked(obj);
    return t ? t->enter_count : 0;
}

/// @brief `Trigger3D.ExitCount` — bodies that left this trigger this frame.
int64_t rt_trigger3d_get_exit_count(void *obj) {
    rt_trigger3d *t = trigger3d_checked(obj);
    return t ? t->exit_count : 0;
}

/// @brief `Trigger3D.SetBounds(x0..z1)` — replace the trigger's AABB.
///
/// Auto-orders the corners. Tracked-body state is preserved across the
/// resize, so a body that was inside the old box and is also inside
/// the new box remains "in" without firing an enter event.
void rt_trigger3d_set_bounds(
    void *obj, double x0, double y0, double z0, double x1, double y1, double z1) {
    rt_trigger3d *t = trigger3d_checked(obj);
    if (!t)
        return;
    trigger3d_set_bounds_raw(t, x0, y0, z0, x1, y1, z1);
}

#else
typedef int rt_physics3d_character_disabled_tu_guard;
#endif /* VIPER_ENABLE_GRAPHICS */
