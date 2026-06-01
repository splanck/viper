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
#include "rt_trap.h"

#include <float.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/// @brief Character3D controller payload: the kinematic body it drives, its
///   owning world, step-up height, walkable-slope cosine, and grounded state.
typedef struct {
    void *vptr;
    rt_body3d *body;
    rt_world3d *world;
    double step_height;
    double slope_limit_cos;
    int8_t is_grounded;
    int8_t was_grounded;
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

// Character controller — built on top of Body3D with custom motion
// resolution: kinematic-style sweeps + slide along surfaces, optional
// step-up over small obstacles, ground probing for "is grounded" state.

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
    if (grounded && normal) {
        ctrl->body->ground_normal[0] = -normal[0];
        ctrl->body->ground_normal[1] = -normal[1];
        ctrl->body->ground_normal[2] = -normal[2];
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
    return ctrl && normal && (-normal[1] >= ctrl->slope_limit_cos);
}

/// @brief Filter for which world bodies the controller should slide against.
///
/// Excludes self, triggers, and dynamic bodies (the controller is
/// kinematic so we don't want it pushing dynamics around as solid
/// blockers). Honors the standard layer/mask filter.
static int character3d_candidate_body(const rt_character3d *ctrl, const rt_body3d *other) {
    if (!ctrl || !ctrl->body || !ctrl->world || !other)
        return 0;
    if (other == ctrl->body)
        return 0;
    if (other->is_trigger || other->motion_mode == PH3D_MODE_DYNAMIC)
        return 0;
    return bodies_can_collide(ctrl->body, other);
}

/// @brief Probe what the controller would collide with at a given position.
///
/// Temporarily moves the body to `pos`, runs the standard narrow-phase
/// against every candidate body, restores the original position, and
/// returns the deepest contact (if any). Used for both penetration
/// resolution and binary-searched sweeps.
static int character3d_test_position(rt_character3d *ctrl,
                                     const double *pos,
                                     rt_character_hit3d *out_hit) {
    if (!ctrl || !ctrl->body || !ctrl->world)
        return 0;

    rt_body3d *body = ctrl->body;
    double saved[3] = {body->position[0], body->position[1], body->position[2]};
    body->position[0] = pos[0];
    body->position[1] = pos[1];
    body->position[2] = pos[2];

    rt_character_hit3d best;
    memset(&best, 0, sizeof(best));
    for (int32_t i = 0; i < ctrl->world->body_count; i++) {
        rt_body3d *other = ctrl->world->bodies[i];
        double normal[3], depth;
        if (!character3d_candidate_body(ctrl, other))
            continue;
        if (!test_collision(body, other, normal, &depth, NULL, NULL, NULL, NULL, NULL))
            continue;
        if (!best.hit || depth > best.depth) {
            best.hit = 1;
            best.body = other;
            best.depth = depth;
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
        if (!character3d_test_position(ctrl, pos, &hit))
            return;
        ctrl->body->position[0] -= hit.normal[0] * (hit.depth + 1e-4);
        ctrl->body->position[1] -= hit.normal[1] * (hit.depth + 1e-4);
        ctrl->body->position[2] -= hit.normal[2] * (hit.depth + 1e-4);
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
    if (!ctrl || !ctrl->body || vec3_len_sq(delta) < 1e-12)
        return 0;

    rt_body3d *body = ctrl->body;
    double start[3] = {body->position[0], body->position[1], body->position[2]};
    double end[3] = {start[0] + delta[0], start[1] + delta[1], start[2] + delta[2]};
    double move_len = sqrt(vec3_len_sq(delta));
    double step_dist = body->radius > 1e-6 ? body->radius * 0.25 : 0.05;
    int steps = (int)ceil(move_len / (step_dist > 0.05 ? step_dist : 0.05));
    double prev_t = 0.0;
    rt_character_hit3d hit;

    if (steps < 1)
        steps = 1;
    if (steps > 128)
        steps = 128;

    for (int s = 1; s <= steps; s++) {
        double t = (double)s / (double)steps;
        double pos[3] = {start[0] + delta[0] * t, start[1] + delta[1] * t, start[2] + delta[2] * t};
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
                double mid_pos[3] = {start[0] + delta[0] * mid,
                                     start[1] + delta[1] * mid,
                                     start[2] + delta[2] * mid};
                if (character3d_test_position(ctrl, mid_pos, &hit)) {
                    hi = mid;
                    best_hit = hit;
                } else {
                    lo = mid;
                }
            }

            body->position[0] = start[0] + delta[0] * lo;
            body->position[1] = start[1] + delta[1] * lo;
            body->position[2] = start[2] + delta[2] * lo;
            best_hit.hit = 1;
            best_hit.fraction = lo;
            if (out_hit)
                *out_hit = best_hit;
            return 1;
        }
    }

    body->position[0] = end[0];
    body->position[1] = end[1];
    body->position[2] = end[2];
    if (out_hit)
        memset(out_hit, 0, sizeof(*out_hit));
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
    if (character3d_test_position(ctrl, probe_pos, &hit) &&
        character3d_normal_is_walkable(ctrl, hit.normal)) {
        character3d_set_ground_state(ctrl, 1, hit.normal);
        return 1;
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
    if (!ctrl || !ctrl->body || ctrl->step_height <= 1e-6 || vec3_len_sq(horizontal_delta) < 1e-12)
        return 0;

    double start[3] = {ctrl->body->position[0], ctrl->body->position[1], ctrl->body->position[2]};
    double up[3] = {0.0, ctrl->step_height, 0.0};
    rt_character_hit3d hit;

    if (character3d_sweep(ctrl, up, &hit)) {
        ctrl->body->position[0] = start[0];
        ctrl->body->position[1] = start[1];
        ctrl->body->position[2] = start[2];
        return 0;
    }
    character3d_resolve_penetration(ctrl);

    if (character3d_sweep(ctrl, horizontal_delta, &hit)) {
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
            return 1;
        }
    }

    ctrl->body->position[0] = start[0];
    ctrl->body->position[1] = start[1];
    ctrl->body->position[2] = start[2];
    return 0;
}

/// @brief Slide-and-iterate motion solver — the heart of the controller's `Move`.
///
/// Up to 4 iterations of: resolve penetration → sweep → if hit,
/// project leftover motion onto the contact plane (or try step-up
/// for non-walkable hits) → continue with the leftover motion. This
/// gives the "slide along walls" feel typical of FPS controllers.
/// Vertical hits onto walkable surfaces also set the grounded flag
/// so gravity stops compounding.
static void character3d_move_axis(rt_character3d *ctrl,
                                  const double *initial_delta,
                                  int allow_step) {
    double remaining[3] = {initial_delta[0], initial_delta[1], initial_delta[2]};
    for (int iter = 0; iter < 4; iter++) {
        rt_character_hit3d hit;
        double leftover[3];

        if (vec3_len_sq(remaining) < 1e-12)
            return;

        character3d_resolve_penetration(ctrl);
        if (!character3d_sweep(ctrl, remaining, &hit))
            return;

        leftover[0] = remaining[0] * (1.0 - hit.fraction);
        leftover[1] = remaining[1] * (1.0 - hit.fraction);
        leftover[2] = remaining[2] * (1.0 - hit.fraction);

        if (allow_step && !character3d_normal_is_walkable(ctrl, hit.normal) &&
            character3d_try_step(ctrl, leftover))
            return;

        if (remaining[1] < 0.0 && character3d_normal_is_walkable(ctrl, hit.normal)) {
            character3d_set_ground_state(ctrl, 1, hit.normal);
            return;
        }

        {
            double into = vec3_dot(leftover, hit.normal);
            if (into > 0.0) {
                leftover[0] -= hit.normal[0] * into;
                leftover[1] -= hit.normal[1] * into;
                leftover[2] -= hit.normal[2] * into;
            } else {
                leftover[0] = leftover[1] = leftover[2] = 0.0;
            }
        }

        remaining[0] = leftover[0];
        remaining[1] = leftover[1];
        remaining[2] = leftover[2];
    }
}

/// @brief GC finalizer for `Character3D` — release the body and the world ref.
static void character3d_finalizer(void *obj) {
    rt_character3d *c = (rt_character3d *)obj;
    if (!c)
        return;
    if (c->body && rt_obj_release_check0(c->body))
        rt_obj_free(c->body);
    c->body = NULL;
    if (c->world && rt_obj_release_check0(c->world))
        rt_obj_free(c->world);
    c->world = NULL;
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
    rt_body3d *body = ctrl->body;
    if (!body)
        return;

    double vx = ph3d_finite_or(rt_vec3_x(velocity_vec), 0.0);
    double vy = ph3d_finite_or(rt_vec3_y(velocity_vec), 0.0);
    double vz = ph3d_finite_or(rt_vec3_z(velocity_vec), 0.0);

    ctrl->was_grounded = ctrl->is_grounded;
    character3d_set_ground_state(ctrl, 0, NULL);

    {
        double start[3] = {body->position[0], body->position[1], body->position[2]};
        double horizontal[3] = {vx * dt, 0.0, vz * dt};
        double vertical[3] = {0.0, vy * dt, 0.0};

        character3d_resolve_penetration(ctrl);
        character3d_move_axis(ctrl, horizontal, 1);
        character3d_move_axis(ctrl, vertical, 0);
        character3d_resolve_penetration(ctrl);
        if (!ctrl->is_grounded)
            character3d_probe_ground(ctrl);

        body->velocity[0] = (body->position[0] - start[0]) / dt;
        body->velocity[1] = (body->position[1] - start[1]) / dt;
        body->velocity[2] = (body->position[2] - start[2]) / dt;
        ph3d_vec3_sanitize_state(body->velocity);
    }
}

/// @brief `Character3D.SetStepHeight(h)` — max obstacle height the controller can step over.
void rt_character3d_set_step_height(void *o, double h) {
    rt_character3d *c = character3d_checked(o);
    if (c)
        c->step_height = ph3d_clamp_nonnegative_finite(h, 0.0);
}

/// @brief `Character3D.GetStepHeight` — read the configured step height.
double rt_character3d_get_step_height(void *o) {
    rt_character3d *c = character3d_checked(o);
    return c ? c->step_height : 0.3;
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
    }
}

/// @brief `Character3D.SetWorld(world)` — bind the character to a physics world.
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
    memset(t, 0, sizeof(rt_trigger3d));
    x0 = ph3d_finite_or(x0, 0.0);
    y0 = ph3d_finite_or(y0, 0.0);
    z0 = ph3d_finite_or(z0, 0.0);
    x1 = ph3d_finite_or(x1, 0.0);
    y1 = ph3d_finite_or(y1, 0.0);
    z1 = ph3d_finite_or(z1, 0.0);
    t->bounds_min[0] = x0 < x1 ? x0 : x1;
    t->bounds_min[1] = y0 < y1 ? y0 : y1;
    t->bounds_min[2] = z0 < z1 ? z0 : z1;
    t->bounds_max[0] = x0 > x1 ? x0 : x1;
    t->bounds_max[1] = y0 > y1 ? y0 : y1;
    t->bounds_max[2] = z0 > z1 ? z0 : z1;
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
        int8_t inside = (b->position[0] >= t->bounds_min[0] && b->position[0] <= t->bounds_max[0] &&
                         b->position[1] >= t->bounds_min[1] && b->position[1] <= t->bounds_max[1] &&
                         b->position[2] >= t->bounds_min[2] && b->position[2] <= t->bounds_max[2])
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
    x0 = ph3d_finite_or(x0, 0.0);
    y0 = ph3d_finite_or(y0, 0.0);
    z0 = ph3d_finite_or(z0, 0.0);
    x1 = ph3d_finite_or(x1, 0.0);
    y1 = ph3d_finite_or(y1, 0.0);
    z1 = ph3d_finite_or(z1, 0.0);
    t->bounds_min[0] = x0 < x1 ? x0 : x1;
    t->bounds_min[1] = y0 < y1 ? y0 : y1;
    t->bounds_min[2] = z0 < z1 ? z0 : z1;
    t->bounds_max[0] = x0 > x1 ? x0 : x1;
    t->bounds_max[1] = y0 > y1 ? y0 : y1;
    t->bounds_max[2] = z0 > z1 ? z0 : z1;
}

#else
typedef int rt_physics3d_character_disabled_tu_guard;
#endif /* VIPER_ENABLE_GRAPHICS */
