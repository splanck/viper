//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/physics/rt_physics3d.h
// Purpose: 3D physics world with AABB/sphere/capsule collision, impulse
//   resolution, rotational body state, collision layers, and character
//   controller with slide/step movement.
//
// Key invariants:
//   - Body/contact/joint arrays grow on demand from production-sized initial capacities.
//   - Symplectic Euler integration (force→velocity, velocity→position).
//   - Dynamic bodies integrate angular velocity into quaternion orientation.
//   - Kinematic bodies move from explicit linear/angular velocity only.
//   - Collision filtering: bidirectional layer/mask bitmask check.
//   - Character controller binds to a World3D and uses swept slide iterations.
//   - Trigger bodies overlap but don't apply physics impulse.
//   - Trigger3D: standalone AABB zone with frame-edge enter/exit detection.
//   - Max 64 tracked bodies per trigger (TRG3D_MAX_TRACKED).
//
// Links: rt_raycast3d.h, misc/plans/3d/20-phase-a-core-game-systems.md
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Shared read/write view of a Body3D's kinematic prefix.
/// @details The full `rt_body3d` struct is private to rt_physics3d.c, but the
///   joint solver (rt_joints3d.c) needs direct access to a body's pose and
///   velocity. Rather than hand-duplicating the field layout (which silently
///   breaks if the private struct is reordered), both sides share this prefix
///   type. rt_physics3d.c pins the contract with `_Static_assert`s that every
///   field offset here matches the real `rt_body3d`.
typedef struct {
    void *vptr;
    double position[3];
    double orientation[4];
    double scale[3];
    double velocity[3];
    double angular_velocity[3];
    double force[3];
    double torque[3];
    double mass;
    double inv_mass;
} rt_body3d_kinematics;

/* Physics3D World */
/// @brief Create a 3D physics world with the given gravity vector (m/s² along each axis).
void *rt_world3d_new(double gx, double gy, double gz);
/// @brief Integrate one simulation step of @p dt seconds (resolves contacts and updates events).
void rt_world3d_step(void *world, double dt);
/// @brief Add a Body3D to the world, growing storage as needed.
void rt_world3d_add(void *world, void *body);
/// @brief Add a Body3D to the world and return whether it is present afterward.
int8_t rt_world3d_try_add(void *world, void *body);
/// @brief Remove a Body3D from the world (silently ignores unknown bodies).
void rt_world3d_remove(void *world, void *body);
/// @brief Total number of bodies currently in the world.
int64_t rt_world3d_body_count(void *world);
/// @brief Whether @p body is currently registered in @p world.
int8_t rt_world3d_contains_body(void *world, void *body);
/// @brief Unclamped CCD substep demand from the most recent Step.
int64_t rt_world3d_get_last_ccd_requested_substeps(void *world);
/// @brief Actual CCD substeps used by the most recent Step.
int64_t rt_world3d_get_last_ccd_substeps(void *world);
/// @brief Number of Step calls whose CCD substep demand exceeded the cap.
int64_t rt_world3d_get_ccd_substep_clamped_count(void *world);
/// @brief Replace the world gravity vector at runtime.
void rt_world3d_set_gravity(void *world, double gx, double gy, double gz);
/// @brief Shift all body/contact/query state by the inverse of a floating-origin delta.
void rt_world3d_rebase_origin(void *world, double dx, double dy, double dz);

/* Joint management */
/// @brief Register a joint; @p joint_type matches the RT_JOINT_* constants.
void rt_world3d_add_joint(void *world, void *joint, int64_t joint_type);
/// @brief Unregister a joint.
void rt_world3d_remove_joint(void *world, void *joint);
/// @brief Number of joints currently in the world.
int64_t rt_world3d_joint_count(void *world);
/// @brief Number of iterative solver passes used for constraints.
int64_t rt_world3d_get_solver_iterations(void *world);
/// @brief Set iterative solver passes; clamped to the runtime-supported range.
void rt_world3d_set_solver_iterations(void *world, int64_t iterations);
/// @brief Max active contact islands scheduled by the most recent Step.
int64_t rt_world3d_get_last_solver_island_count(void *world);
/// @brief Max awake dynamic bodies included in contact islands by the most recent Step.
int64_t rt_world3d_get_last_solver_active_body_count(void *world);
/// @brief Max non-trigger contacts scheduled through contact islands by the most recent Step.
int64_t rt_world3d_get_last_solver_contact_count(void *world);

/* Collision event queries (populated after each Step) */
/// @brief Number of contact pairs from the most recent Step.
int64_t rt_world3d_get_collision_count(void *world);
/// @brief First body in the @p index-th contact pair from the most recent Step.
void *rt_world3d_get_collision_body_a(void *world, int64_t index);
/// @brief Second body in the @p index-th contact pair.
void *rt_world3d_get_collision_body_b(void *world, int64_t index);
/// @brief Contact normal as a Vec3 (points from body A to body B).
void *rt_world3d_get_collision_normal(void *world, int64_t index);
/// @brief Penetration depth in world units for the @p index-th contact.
double rt_world3d_get_collision_depth(void *world, int64_t index);
/// @brief Number of detailed collision events (with contact-point lists) from the most recent Step.
int64_t rt_world3d_get_collision_event_count(void *world);
/// @brief Get the @p index-th detailed collision event.
void *rt_world3d_get_collision_event(void *world, int64_t index);
/// @brief Number of "enter" events (pairs that started colliding this Step).
int64_t rt_world3d_get_enter_event_count(void *world);
/// @brief Get the @p index-th enter event.
void *rt_world3d_get_enter_event(void *world, int64_t index);
/// @brief Number of "stay" events (pairs still colliding from a previous Step).
int64_t rt_world3d_get_stay_event_count(void *world);
/// @brief Get the @p index-th stay event.
void *rt_world3d_get_stay_event(void *world, int64_t index);
/// @brief Number of "exit" events (pairs that stopped colliding this Step).
int64_t rt_world3d_get_exit_event_count(void *world);
/// @brief Get the @p index-th exit event.
void *rt_world3d_get_exit_event(void *world, int64_t index);
/// @brief Clear live and transition collision buffers without stepping the simulation.
void rt_world3d_clear_collision_events(void *world);

/* World queries */
/// @brief Cast a ray; returns the closest PhysicsHit3D within @p max_distance, or NULL if none.
void *rt_world3d_raycast(
    void *world, void *origin, void *direction, double max_distance, int64_t mask);
/// @brief Cast a ray and return all hits as a PhysicsHitList3D, sorted by distance.
void *rt_world3d_raycast_all(
    void *world, void *origin, void *direction, double max_distance, int64_t mask);
/// @brief Sweep a sphere along @p delta; returns the first PhysicsHit3D or NULL.
void *rt_world3d_sweep_sphere(void *world, void *center, double radius, void *delta, int64_t mask);
/// @brief Sweep a capsule (segment between @p a and @p b, radius @p radius) along @p delta.
void *rt_world3d_sweep_capsule(
    void *world, void *a, void *b, double radius, void *delta, int64_t mask);
/// @brief Find all bodies overlapping a sphere (PhysicsHitList3D).
void *rt_world3d_overlap_sphere(void *world, void *center, double radius, int64_t mask);
/// @brief Find all bodies overlapping an axis-aligned bounding box (PhysicsHitList3D).
void *rt_world3d_overlap_aabb(void *world, void *min_corner, void *max_corner, int64_t mask);

/* PhysicsHit3D */
/// @brief Get the body that was hit.
void *rt_physics_hit3d_get_body(void *hit);
/// @brief Get the specific collider on the body that was hit (compound bodies).
void *rt_physics_hit3d_get_collider(void *hit);
/// @brief Get the world-space hit point as a Vec3.
void *rt_physics_hit3d_get_point(void *hit);
/// @brief Get the surface normal at the hit point as a Vec3.
void *rt_physics_hit3d_get_normal(void *hit);
/// @brief Get the distance from the ray/sweep origin to the hit point.
double rt_physics_hit3d_get_distance(void *hit);
/// @brief Get the parametric fraction along the sweep where the hit occurred (0..1).
double rt_physics_hit3d_get_fraction(void *hit);
/// @brief True if the sweep started already penetrating the collider (initial overlap).
int8_t rt_physics_hit3d_get_started_penetrating(void *hit);
/// @brief True if the hit body is a trigger volume (not a solid collider).
int8_t rt_physics_hit3d_get_is_trigger(void *hit);

/* PhysicsHitList3D */
/// @brief Number of hits in the list.
int64_t rt_physics_hit_list3d_get_count(void *list);
/// @brief Number of hits matched before the bounded result list was truncated.
int64_t rt_physics_hit_list3d_get_total_count(void *list);
/// @brief True when Count is smaller than TotalCount.
int8_t rt_physics_hit_list3d_get_truncated(void *list);
/// @brief Get the @p index-th PhysicsHit3D in the list (NULL if out of range).
void *rt_physics_hit_list3d_get(void *list, int64_t index);

/* CollisionEvent3D */
/// @brief Get the first body in the collision pair.
void *rt_collision_event3d_get_body_a(void *event);
/// @brief Get the second body in the collision pair.
void *rt_collision_event3d_get_body_b(void *event);
/// @brief Get the specific collider on body A (for compound bodies).
void *rt_collision_event3d_get_collider_a(void *event);
/// @brief Get the specific collider on body B.
void *rt_collision_event3d_get_collider_b(void *event);
/// @brief True if either body in the pair is a trigger volume.
int8_t rt_collision_event3d_get_is_trigger(void *event);
/// @brief Number of contact points generated for this collision (typically 1–4).
int64_t rt_collision_event3d_get_contact_count(void *event);
/// @brief Relative speed of the two bodies along the contact normal at first contact.
double rt_collision_event3d_get_relative_speed(void *event);
/// @brief Total normal impulse applied to resolve the collision (proportional to "force").
double rt_collision_event3d_get_normal_impulse(void *event);
/// @brief Get the @p index-th ContactPoint3D for this event.
void *rt_collision_event3d_get_contact(void *event, int64_t index);
/// @brief Convenience: world-space position of the @p index-th contact point.
void *rt_collision_event3d_get_contact_point(void *event, int64_t index);
/// @brief Convenience: surface normal of the @p index-th contact point.
void *rt_collision_event3d_get_contact_normal(void *event, int64_t index);
/// @brief Convenience: penetration depth at the @p index-th contact (negative = separation).
double rt_collision_event3d_get_contact_separation(void *event, int64_t index);

/* ContactPoint3D */
/// @brief Get the world-space position of the contact.
void *rt_contact_point3d_get_point(void *contact);
/// @brief Get the surface normal at the contact (points from body A to body B).
void *rt_contact_point3d_get_normal(void *contact);
/// @brief Get the contact separation (>0 = penetration depth, <0 = gap).
double rt_contact_point3d_get_separation(void *contact);

/* Physics3D Body */
/// @brief Create a body with no collider yet (call `_set_collider` before adding to a world).
void *rt_body3d_new(double mass);
/// @brief Create a body with an AABB collider of half-extents (hx, hy, hz).
void *rt_body3d_new_aabb(double hx, double hy, double hz, double mass);
/// @brief Create a body with a sphere collider of the given radius.
void *rt_body3d_new_sphere(double radius, double mass);
/// @brief Create a body with a capsule collider (radius + total height including caps).
void *rt_body3d_new_capsule(double radius, double height, double mass);
/// @brief Replace the body's collider (Collider3D handle).
void rt_body3d_set_collider(void *body, void *collider);
/// @brief Get the currently bound collider handle.
void *rt_body3d_get_collider(void *body);
/// @brief Teleport the body to (x, y, z) (does not generate motion vectors).
void rt_body3d_set_position(void *body, double x, double y, double z);
/// @brief Get the body's world-space position as a Vec3.
void *rt_body3d_get_position(void *body);
/// @brief Set collider scale applied during physics queries and collision tests.
void rt_body3d_set_scale(void *body, double x, double y, double z);
/// @brief Get the body's collider scale as a Vec3.
void *rt_body3d_get_scale(void *body);
/// @brief Set the body's orientation from a Quaternion (overwrites accumulated rotation).
void rt_body3d_set_orientation(void *body, void *quat);
/// @brief Get the body's current orientation as a Quaternion.
void *rt_body3d_get_orientation(void *body);
/// @brief Copy raw position, orientation, and scale arrays without allocating wrapper objects.
void rt_body3d_get_pose_raw(void *body, double *position_out, double *rotation_out, double *scale_out);
/// @brief Set the linear velocity in m/s.
void rt_body3d_set_velocity(void *body, double vx, double vy, double vz);
/// @brief Get the linear velocity as a Vec3.
void *rt_body3d_get_velocity(void *body);
/// @brief Set the angular velocity in rad/s about each axis.
void rt_body3d_set_angular_velocity(void *body, double wx, double wy, double wz);
/// @brief Get the angular velocity as a Vec3.
void *rt_body3d_get_angular_velocity(void *body);
/// @brief Apply a continuous force in Newtons (integrated over the next Step).
void rt_body3d_apply_force(void *body, double fx, double fy, double fz);
/// @brief Apply a continuous force at a world point — adds force and torque
///   (r x force), e.g. a thruster. Cleared each step like ApplyForce.
void rt_body3d_apply_force_at_point(void *body, double fx, double fy, double fz,
                                    double px, double py, double pz);
/// @brief Apply an instantaneous impulse (kg·m/s) — changes velocity immediately.
void rt_body3d_apply_impulse(void *body, double ix, double iy, double iz);
/// @brief Apply a linear impulse at a world point — adds linear and (via the lever
///   arm) angular velocity, so off-center hits spin the body.
void rt_body3d_apply_impulse_at_point(void *body, double ix, double iy, double iz,
                                      double px, double py, double pz);
/// @brief Apply a continuous torque (N·m) about each axis.
void rt_body3d_apply_torque(void *body, double tx, double ty, double tz);
/// @brief Apply an instantaneous angular impulse — changes angular velocity immediately.
void rt_body3d_apply_angular_impulse(void *body, double ix, double iy, double iz);
/// @brief Set the bounciness coefficient (0 = perfectly inelastic, 1 = perfectly elastic).
void rt_body3d_set_restitution(void *body, double r);
/// @brief Get the restitution coefficient.
double rt_body3d_get_restitution(void *body);
/// @brief Set the friction coefficient (typical range 0.0–1.0).
void rt_body3d_set_friction(void *body, double f);
/// @brief Get the friction coefficient.
double rt_body3d_get_friction(void *body);
/// @brief Set linear damping (per-second velocity decay multiplier).
void rt_body3d_set_linear_damping(void *body, double d);
/// @brief Get linear damping.
double rt_body3d_get_linear_damping(void *body);
/// @brief Set angular damping (per-second angular-velocity decay multiplier).
void rt_body3d_set_angular_damping(void *body, double d);
/// @brief Get angular damping.
double rt_body3d_get_angular_damping(void *body);
/// @brief Set the body's collision layer (1–63 reserved bits identify "what I am").
void rt_body3d_set_collision_layer(void *body, int64_t layer);
/// @brief Get the body's collision layer.
int64_t rt_body3d_get_collision_layer(void *body);
/// @brief Set the body's collision mask (bitmask of layers it interacts with).
void rt_body3d_set_collision_mask(void *body, int64_t mask);
/// @brief Get the body's collision mask.
int64_t rt_body3d_get_collision_mask(void *body);
/// @brief Mark the body as static (immovable, infinite mass).
void rt_body3d_set_static(void *body, int8_t s);
/// @brief True if the body is static.
int8_t rt_body3d_is_static(void *body);
/// @brief Mark the body as kinematic (moves only via explicit velocity, not forces).
void rt_body3d_set_kinematic(void *body, int8_t k);
/// @brief True if the body is kinematic.
int8_t rt_body3d_is_kinematic(void *body);
/// @brief Mark the body as a trigger (generates events but doesn't apply impulse).
void rt_body3d_set_trigger(void *body, int8_t t);
/// @brief True if the body is a trigger.
int8_t rt_body3d_is_trigger(void *body);
/// @brief Allow or forbid the body from entering sleep state when at rest.
void rt_body3d_set_can_sleep(void *body, int8_t can_sleep);
/// @brief True if the body is allowed to sleep.
int8_t rt_body3d_can_sleep(void *body);
/// @brief True if the body is currently sleeping (not being simulated).
int8_t rt_body3d_is_sleeping(void *body);
/// @brief Force the body awake (it will be re-simulated next Step).
void rt_body3d_wake(void *body);
/// @brief Force the body to sleep immediately (zero velocity).
void rt_body3d_sleep(void *body);
/// @brief Toggle continuous collision detection for fast-moving bodies (prevents tunneling).
void rt_body3d_set_use_ccd(void *body, int8_t use_ccd);
/// @brief Get the CCD enable flag.
int8_t rt_body3d_get_use_ccd(void *body);
/// @brief True if the body is currently in contact with a surface below it.
int8_t rt_body3d_is_grounded(void *body);
/// @brief Get the ground normal (Vec3) for a grounded body, or zero vector if airborne.
void *rt_body3d_get_ground_normal(void *body);
/// @brief Get the body's mass in kg (0 for static/kinematic bodies).
double rt_body3d_get_mass(void *body);

/* Trigger3D — standalone AABB zone with enter/exit detection */
/// @brief Create a standalone trigger zone (axis-aligned box from min to max corner).
void *rt_trigger3d_new(double x0, double y0, double z0, double x1, double y1, double z1);
/// @brief True if @p point (Vec3) is inside the trigger AABB.
int8_t rt_trigger3d_contains(void *trigger, void *point);
/// @brief Scan @p world's bodies and emit enter/exit events for this trigger this frame.
void rt_trigger3d_update(void *trigger, void *world);
/// @brief Number of bodies that entered the trigger during the most recent Update.
int64_t rt_trigger3d_get_enter_count(void *trigger);
/// @brief Number of bodies that exited the trigger during the most recent Update.
int64_t rt_trigger3d_get_exit_count(void *trigger);
/// @brief Replace the trigger's AABB at runtime.
void rt_trigger3d_set_bounds(
    void *trigger, double x0, double y0, double z0, double x1, double y1, double z1);

/* Character Controller */
/// @brief Create a capsule-based character controller (radius, total height, mass).
void *rt_character3d_new(double radius, double height, double mass);
/// @brief Move the controller by velocity * dt with swept-slide collision response.
void rt_character3d_move(void *ctrl, void *velocity, double dt);
/// @brief Set the maximum step-up height for stairs (controller auto-climbs steps below this).
void rt_character3d_set_step_height(void *ctrl, double h);
/// @brief Get the current step-up height limit.
double rt_character3d_get_step_height(void *ctrl);
/// @brief Set the maximum walkable slope in degrees (steeper slopes = sliding instead of walking).
void rt_character3d_set_slope_limit(void *ctrl, double degrees);
/// @brief Bind the controller to a physics world (required before Move).
void rt_character3d_set_world(void *ctrl, void *world);
/// @brief Get the bound physics world.
void *rt_character3d_get_world(void *ctrl);
/// @brief True if the controller is currently standing on a walkable surface.
int8_t rt_character3d_is_grounded(void *ctrl);
/// @brief True for one frame after the controller transitions from airborne to grounded.
int8_t rt_character3d_just_landed(void *ctrl);
/// @brief Get the controller's world-space position as a Vec3.
void *rt_character3d_get_position(void *ctrl);
/// @brief Teleport the controller (no swept collision; use Move for normal motion).
void rt_character3d_set_position(void *ctrl, double x, double y, double z);

#ifdef __cplusplus
}
#endif
