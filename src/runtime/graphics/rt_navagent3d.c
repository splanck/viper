//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_navagent3d.c
// Purpose: Gameplay-facing single-agent navigation built on NavMesh3D path
//   queries, with optional Character3D and SceneNode3D bindings.
//
//===----------------------------------------------------------------------===//

#ifdef VIPER_ENABLE_GRAPHICS

#include "rt_navagent3d.h"

#include "rt_mat4.h"
#include "rt_navmesh3d.h"
#include "rt_physics3d.h"
#include "rt_scene3d.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

extern void *rt_obj_new_i64(int64_t class_id, int64_t byte_size);
extern void rt_obj_set_finalizer(void *obj, void (*fn)(void *));
extern void rt_obj_retain_maybe(void *obj);
extern int rt_obj_release_check0(void *obj);
extern void rt_obj_free(void *obj);
extern void *rt_vec3_new(double x, double y, double z);
extern double rt_vec3_x(void *v);
extern double rt_vec3_y(void *v);
extern double rt_vec3_z(void *v);

typedef struct {
    void *vptr;
    void *navmesh;
    void *bound_character;
    void *bound_node;
    double radius;
    double height;
    double position[3];
    double velocity[3];
    double desired_velocity[3];
    double target[3];
    double stopping_distance;
    double desired_speed;
    double remaining_distance;
    double repath_interval;
    double repath_accum;
    double *path_points_xyz;
    int32_t path_point_count;
    int32_t path_index;
    int8_t has_target;
    int8_t has_path;
    int8_t auto_repath;
} rt_navagent3d;

/// @brief Squared length of a 3-vector; avoids a sqrt when only ordering/thresholding matters.
static double navagent_len_sq(const double v[3]) {
    return v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
}

/// @brief Euclidean length of a 3-vector (`sqrt` of the squared length).
static double navagent_len(const double v[3]) {
    return sqrt(navagent_len_sq(v));
}

/// @brief Squared Euclidean distance between two points — cheaper when only compared to another
///   squared value (corner-tolerance checks use this to skip the sqrt).
static double navagent_dist_sq(const double a[3], const double b[3]) {
    double dx = a[0] - b[0];
    double dy = a[1] - b[1];
    double dz = a[2] - b[2];
    return dx * dx + dy * dy + dz * dz;
}

/// @brief Euclidean distance between two points.
static double navagent_dist(const double a[3], const double b[3]) {
    return sqrt(navagent_dist_sq(a, b));
}

/// @brief Assign `(x,y,z)` to `dst[0..2]`. Reads declaratively as "set this vector to …".
static void navagent_vec_set(double dst[3], double x, double y, double z) {
    dst[0] = x;
    dst[1] = y;
    dst[2] = z;
}

/// @brief Copy `src[0..2]` into `dst[0..2]` — trivial 3-double `memcpy`-equivalent.
static void navagent_vec_copy(double dst[3], const double src[3]) {
    dst[0] = src[0];
    dst[1] = src[1];
    dst[2] = src[2];
}

/// @brief Decrement-and-free a GC reference slot, clearing the slot to NULL.
/// @details Used when rebinding a navmesh / character / scene-node. The slot is zeroed
///   so subsequent calls are idempotent, and the finalizer can call this on every slot
///   without needing per-field null checks.
static void navagent_release_ref(void **slot) {
    if (!slot || !*slot)
        return;
    if (rt_obj_release_check0(*slot))
        rt_obj_free(*slot);
    *slot = NULL;
}

static void navagent_release_local(void *obj) {
    if (obj && rt_obj_release_check0(obj))
        rt_obj_free(obj);
}

/// @brief Address the xyz triple for corner `index` in the flat path buffer.
/// @details Path corners are stored as a packed `[x0,y0,z0, x1,y1,z1, ...]`
///   array — returning a stride-adjusted pointer avoids constructing a
///   temporary `double[3]` for every read and lets callers memcpy or index
///   directly. The caller is responsible for bounds-checking `index`
///   against `path_point_count` — this is a raw indexing primitive.
static const double *navagent_path_point(const rt_navagent3d *agent, int32_t index) {
    return agent->path_points_xyz + (size_t)index * 3u;
}

/// @brief Tear down the current path — free the corner buffer, zero counters, and mark
/// `has_path` false so the next update knows there's nothing to follow. Safe to call
/// when no path is active. Does NOT zero the velocity; the caller handles that if a
/// motion reset is also desired.
static void navagent_clear_path(rt_navagent3d *agent) {
    if (!agent)
        return;
    free(agent->path_points_xyz);
    agent->path_points_xyz = NULL;
    agent->path_point_count = 0;
    agent->path_index = 0;
    agent->has_path = 0;
    agent->remaining_distance = 0.0;
}

/// @brief Bring both the current and desired velocity vectors to rest.
/// @details Called when the agent reaches its destination, loses its path,
///   or is forcibly stopped. Clearing *both* velocity vectors matters because
///   the integrator uses `desired_velocity` as the steering target — if only
///   `velocity` were reset, the next step would immediately re-accelerate
///   toward the stale desired velocity and produce a visible jitter.
static void navagent_zero_motion(rt_navagent3d *agent) {
    if (!agent)
        return;
    navagent_vec_set(agent->velocity, 0.0, 0.0, 0.0);
    navagent_vec_set(agent->desired_velocity, 0.0, 0.0, 0.0);
}

/// @brief Derive "close enough to this corner" distance from the agent's radius. Half
/// the radius works well in practice, with a 0.15 unit floor so tiny agents (e.g. radius
/// 0.2) don't wind up chasing corners forever because their tolerance converged to zero.
static double navagent_corner_tolerance(const rt_navagent3d *agent) {
    double tol = agent->radius > 0.0 ? agent->radius * 0.5 : 0.2;
    if (tol < 0.15)
        tol = 0.15;
    return tol;
}

/// @brief Snap a world-space query point onto the bound navmesh (closest valid point on
/// any polygon). Falls through with an identity copy — or zero — when no navmesh is bound
/// or when the mesh's closest-point query fails. Used to keep the agent's position, start
/// of the path, and goal all on the navigable surface regardless of where the caller's
/// coordinates originated.
static void navagent_sample_point(rt_navagent3d *agent, const double src[3], double dst[3]) {
    if (!agent || !dst) {
        return;
    }
    if (!agent->navmesh) {
        if (src)
            navagent_vec_copy(dst, src);
        else
            navagent_vec_set(dst, 0.0, 0.0, 0.0);
        return;
    }
    void *query = rt_vec3_new(src ? src[0] : 0.0, src ? src[1] : 0.0, src ? src[2] : 0.0);
    void *sample = query ? rt_navmesh3d_sample_position(agent->navmesh, query) : NULL;
    navagent_release_local(query);
    if (!sample) {
        if (src)
            navagent_vec_copy(dst, src);
        else
            navagent_vec_set(dst, 0.0, 0.0, 0.0);
        return;
    }
    dst[0] = rt_vec3_x(sample);
    dst[1] = rt_vec3_y(sample);
    dst[2] = rt_vec3_z(sample);
    navagent_release_local(sample);
}

/// @brief Resolve a SceneNode3D's world-space position by transforming the origin through
/// its cached world matrix. Falls back to the local-space position when no world matrix
/// is available (which is only approximately correct for parented nodes, but never
/// crashes). Results land in `out_pos[0..2]` — always written even on the failure paths
/// so callers don't read uninitialised memory.
static void navagent_get_node_world_position(void *node, double out_pos[3]) {
    void *world_matrix;
    void *world_pos;
    if (!node) {
        navagent_vec_set(out_pos, 0.0, 0.0, 0.0);
        return;
    }
    world_matrix = rt_scene_node3d_get_world_matrix(node);
    if (!world_matrix) {
        void *local = rt_scene_node3d_get_position(node);
        out_pos[0] = local ? rt_vec3_x(local) : 0.0;
        out_pos[1] = local ? rt_vec3_y(local) : 0.0;
        out_pos[2] = local ? rt_vec3_z(local) : 0.0;
        navagent_release_local(local);
        return;
    }
    void *origin = rt_vec3_new(0.0, 0.0, 0.0);
    world_pos = origin ? rt_mat4_transform_point(world_matrix, origin) : NULL;
    out_pos[0] = world_pos ? rt_vec3_x(world_pos) : 0.0;
    out_pos[1] = world_pos ? rt_vec3_y(world_pos) : 0.0;
    out_pos[2] = world_pos ? rt_vec3_z(world_pos) : 0.0;
    navagent_release_local(origin);
    navagent_release_local(world_pos);
    navagent_release_local(world_matrix);
}

/// @brief Write a world-space position into a SceneNode3D, converting through the
/// inverse of the parent's world matrix so the node-local coordinate comes out right
/// after the parent transform is applied. When the node is a root (no parent), the
/// world position is written directly as the local position. Also falls back to direct
/// write when the parent's world matrix can't be inverted (degenerate scale, etc.).
static void navagent_set_node_world_position(void *node, const double world_pos[3]) {
    void *parent;
    if (!node || !world_pos)
        return;
    parent = rt_scene_node3d_get_parent(node);
    if (!parent) {
        rt_scene_node3d_set_position(node, world_pos[0], world_pos[1], world_pos[2]);
        return;
    }

    {
        void *parent_world = rt_scene_node3d_get_world_matrix(parent);
        void *parent_inv = parent_world ? rt_mat4_inverse(parent_world) : NULL;
        void *world_vec = rt_vec3_new(world_pos[0], world_pos[1], world_pos[2]);
        void *local = (parent_inv && world_vec) ? rt_mat4_transform_point(parent_inv, world_vec) : NULL;
        if (local) {
            rt_scene_node3d_set_position(node, rt_vec3_x(local), rt_vec3_y(local), rt_vec3_z(local));
        } else {
            rt_scene_node3d_set_position(node, world_pos[0], world_pos[1], world_pos[2]);
        }
        navagent_release_local(local);
        navagent_release_local(world_vec);
        navagent_release_local(parent_inv);
        navagent_release_local(parent_world);
    }
}

/// @brief Pull the agent's authoritative world position from whichever binding is
/// primary. A bound CharacterController3D wins (the controller owns the physics position);
/// otherwise a bound SceneNode3D provides the world transform. With neither binding the
/// agent's cached position is already authoritative and this call is a no-op. Called at
/// the top of every `_update` so external movement of the character or node flows into
/// pathing decisions.
static void navagent_sync_position_from_bindings(rt_navagent3d *agent) {
    if (!agent)
        return;
    if (agent->bound_character) {
        void *pos = rt_character3d_get_position(agent->bound_character);
        agent->position[0] = pos ? rt_vec3_x(pos) : 0.0;
        agent->position[1] = pos ? rt_vec3_y(pos) : 0.0;
        agent->position[2] = pos ? rt_vec3_z(pos) : 0.0;
        navagent_release_local(pos);
        return;
    }
    if (agent->bound_node) {
        navagent_get_node_world_position(agent->bound_node, agent->position);
    }
}

/// @brief Propagate the agent's current position *out* to every live binding — the
/// controller via `rt_character3d_set_position`, the scene node via
/// `navagent_set_node_world_position`. Used when the agent is warped, or when `_update`
/// finishes and needs to keep a passive visual rig in sync with the AI mover.
static void navagent_push_position_to_bindings(rt_navagent3d *agent) {
    if (!agent)
        return;
    if (agent->bound_character) {
        rt_character3d_set_position(
            agent->bound_character, agent->position[0], agent->position[1], agent->position[2]);
    }
    if (agent->bound_node) {
        navagent_set_node_world_position(agent->bound_node, agent->position);
    }
}

/// @brief Advance `path_index` past any corners the agent has already reached (within
/// the corner tolerance derived from its radius). Skips zero or more consecutive corners
/// in one call so a character moving fast enough to overshoot several waypoints in a
/// single tick still progresses cleanly. Always leaves `path_index` at the final corner
/// at the latest (so the goal is never popped off the chase list).
static void navagent_refresh_path_index(rt_navagent3d *agent) {
    double tolerance;
    if (!agent || !agent->has_path || agent->path_point_count <= 0)
        return;
    tolerance = navagent_corner_tolerance(agent);
    while (agent->path_index < agent->path_point_count - 1 &&
           navagent_dist(agent->position, navagent_path_point(agent, agent->path_index)) <= tolerance) {
        agent->path_index++;
    }
}

/// @brief Sum the remaining world-space path length from the agent's current position
/// to the goal — that is, the straight-line hop to the current `path_index` waypoint
/// plus every subsequent corner-to-corner segment. Clamped `path_index` into valid
/// range defensively. Callers expose this via `get_remaining_distance` so AI scripts
/// can decide "am I almost there?" without repeating the walk themselves.
static double navagent_compute_remaining_distance(rt_navagent3d *agent) {
    double remaining = 0.0;
    int32_t idx;
    if (!agent || !agent->has_path || agent->path_point_count <= 0)
        return 0.0;
    idx = agent->path_index;
    if (idx < 0)
        idx = 0;
    if (idx >= agent->path_point_count)
        idx = agent->path_point_count - 1;
    remaining += navagent_dist(agent->position, navagent_path_point(agent, idx));
    for (int32_t i = idx; i < agent->path_point_count - 1; i++) {
        remaining += navagent_dist(navagent_path_point(agent, i), navagent_path_point(agent, i + 1));
    }
    return remaining;
}

/// @brief Clear any previous path and query the navmesh for a fresh corner list between
/// the agent's (navmesh-snapped) current position and its (navmesh-snapped) target.
/// Resets the repath timer. On empty or failed path result, zeroes velocity so the agent
/// stops cleanly instead of following a stale path. The first corner (`path_index = 1`)
/// is the next waypoint, because corner 0 is the starting position.
static void navagent_rebuild_path(rt_navagent3d *agent) {
    double start[3];
    double goal[3];
    double *points = NULL;
    int64_t point_count;
    if (!agent)
        return;
    navagent_clear_path(agent);
    agent->repath_accum = 0.0;
    if (!agent->navmesh || !agent->has_target) {
        navagent_zero_motion(agent);
        return;
    }

    navagent_sync_position_from_bindings(agent);
    navagent_sample_point(agent, agent->position, start);
    navagent_sample_point(agent, agent->target, goal);
    navagent_vec_copy(agent->target, goal);

    void *start_vec = rt_vec3_new(start[0], start[1], start[2]);
    void *goal_vec = rt_vec3_new(goal[0], goal[1], goal[2]);
    point_count = (start_vec && goal_vec)
                      ? rt_navmesh3d_copy_path_points(agent->navmesh, start_vec, goal_vec, &points)
                      : 0;
    navagent_release_local(start_vec);
    navagent_release_local(goal_vec);
    if (point_count <= 0 || !points) {
        navagent_zero_motion(agent);
        return;
    }

    agent->path_points_xyz = points;
    agent->path_point_count = (int32_t)point_count;
    agent->path_index = point_count > 1 ? 1 : 0;
    agent->has_path = point_count > 0 ? 1 : 0;
    navagent_refresh_path_index(agent);
    agent->remaining_distance = navagent_compute_remaining_distance(agent);
}

/// @brief GC finalizer for NavAgent3D. Frees the heap-allocated path-points buffer and
/// drops references to the navmesh and any bound character / scene-node. Binding
/// references may or may not actually free on release depending on external retains.
static void navagent_finalize(void *obj) {
    rt_navagent3d *agent = (rt_navagent3d *)obj;
    if (!agent)
        return;
    free(agent->path_points_xyz);
    agent->path_points_xyz = NULL;
    navagent_release_ref(&agent->navmesh);
    navagent_release_ref(&agent->bound_character);
    navagent_release_ref(&agent->bound_node);
}

/// @brief Create a navigation agent that pathfinds across `navmesh`. `radius` and `height`
/// describe the agent's collision capsule (defaults 0.4 / 1.8 if 0). Defaults: stopping_distance
/// = radius, desired_speed = 4 u/s, auto-repath every 0.25 s, position at origin (snapped to
/// the navmesh on construction).
void *rt_navagent3d_new(void *navmesh, double radius, double height) {
    rt_navagent3d *agent =
        (rt_navagent3d *)rt_obj_new_i64(0, (int64_t)sizeof(rt_navagent3d));
    if (!agent)
        return NULL;
    memset(agent, 0, sizeof(*agent));
    agent->vptr = NULL;
    agent->radius = radius > 0.0 ? radius : 0.4;
    agent->height = height > 0.0 ? height : 1.8;
    agent->stopping_distance = agent->radius > 0.0 ? agent->radius : 0.25;
    agent->desired_speed = 4.0;
    agent->repath_interval = 0.25;
    agent->auto_repath = 1;
    if (navmesh)
        rt_obj_retain_maybe(navmesh);
    agent->navmesh = navmesh;
    navagent_vec_set(agent->position, 0.0, 0.0, 0.0);
    navagent_zero_motion(agent);
    if (agent->navmesh)
        navagent_sample_point(agent, agent->position, agent->position);
    rt_obj_set_finalizer(agent, navagent_finalize);
    return agent;
}

/// @brief Set the agent's destination. The position is snapped onto the navmesh, and a path is
/// rebuilt immediately. Subsequent `_update` calls steer the agent along the path until it
/// reaches `_get_stopping_distance` of the target.
void rt_navagent3d_set_target(void *obj, void *position) {
    rt_navagent3d *agent = (rt_navagent3d *)obj;
    if (!agent || !position)
        return;
    navagent_sync_position_from_bindings(agent);
    navagent_sample_point(agent,
                          (double[3]){rt_vec3_x(position), rt_vec3_y(position), rt_vec3_z(position)},
                          agent->target);
    agent->has_target = 1;
    navagent_rebuild_path(agent);
}

/// @brief Cancel the active target and clear the path. Subsequent `_update` calls do nothing
/// until a new target is set. Velocity is zeroed so AI motion stops cleanly.
void rt_navagent3d_clear_target(void *obj) {
    rt_navagent3d *agent = (rt_navagent3d *)obj;
    if (!agent)
        return;
    agent->has_target = 0;
    navagent_clear_path(agent);
    navagent_zero_motion(agent);
}

/// @brief Per-frame steering tick. Optionally re-paths if the auto-repath interval has elapsed,
/// computes a desired velocity toward the next path waypoint, then either moves a bound
/// CharacterController3D (if any) or integrates position directly. Falls back to snapping to the
/// navmesh after each step. Stops cleanly when within `stopping_distance` of the target.
void rt_navagent3d_update(void *obj, double dt) {
    rt_navagent3d *agent = (rt_navagent3d *)obj;
    double prev_pos[3];
    double target_dist;
    if (!agent || dt <= 0.0)
        return;

    navagent_sync_position_from_bindings(agent);
    navagent_vec_copy(prev_pos, agent->position);
    target_dist = agent->has_target ? navagent_dist(agent->position, agent->target) : 0.0;

    if (agent->auto_repath && agent->has_target && target_dist > agent->stopping_distance) {
        agent->repath_accum += dt;
        if (agent->repath_accum >= agent->repath_interval) {
            navagent_rebuild_path(agent);
        }
    }

    navagent_refresh_path_index(agent);
    agent->remaining_distance = navagent_compute_remaining_distance(agent);
    if (!agent->has_path || target_dist <= agent->stopping_distance ||
        agent->remaining_distance <= agent->stopping_distance) {
        agent->has_path = 0;
        agent->remaining_distance = 0.0;
        navagent_zero_motion(agent);
        if (agent->bound_character && agent->bound_node) {
            navagent_push_position_to_bindings(agent);
        }
        return;
    }

    {
        const double *next_point = navagent_path_point(agent, agent->path_index);
        double delta[3] = {next_point[0] - agent->position[0],
                           next_point[1] - agent->position[1],
                           next_point[2] - agent->position[2]};
        double dist = navagent_len(delta);
        double speed = agent->desired_speed;
        if (dist <= 1e-9) {
            navagent_refresh_path_index(agent);
            navagent_zero_motion(agent);
            agent->remaining_distance = navagent_compute_remaining_distance(agent);
            return;
        }
        if (speed < 0.0)
            speed = 0.0;
        if (dt > 0.0) {
            double clamp_speed = dist / dt;
            if (clamp_speed < speed)
                speed = clamp_speed;
        }

        agent->desired_velocity[0] = delta[0] / dist * speed;
        agent->desired_velocity[1] = delta[1] / dist * speed;
        agent->desired_velocity[2] = delta[2] / dist * speed;
    }

    if (agent->bound_character) {
        void *move_vec = rt_vec3_new(agent->desired_velocity[0],
                                     agent->desired_velocity[1],
                                     agent->desired_velocity[2]);
        rt_character3d_move(agent->bound_character,
                            move_vec,
                            dt);
        navagent_release_local(move_vec);
        navagent_sync_position_from_bindings(agent);
        if (agent->bound_node)
            navagent_set_node_world_position(agent->bound_node, agent->position);
    } else {
        agent->position[0] += agent->desired_velocity[0] * dt;
        agent->position[1] += agent->desired_velocity[1] * dt;
        agent->position[2] += agent->desired_velocity[2] * dt;
        if (agent->navmesh)
            navagent_sample_point(agent, agent->position, agent->position);
        if (agent->bound_node)
            navagent_set_node_world_position(agent->bound_node, agent->position);
    }

    agent->velocity[0] = (agent->position[0] - prev_pos[0]) / dt;
    agent->velocity[1] = (agent->position[1] - prev_pos[1]) / dt;
    agent->velocity[2] = (agent->position[2] - prev_pos[2]) / dt;
    navagent_refresh_path_index(agent);
    agent->remaining_distance = navagent_compute_remaining_distance(agent);
    if (agent->has_target &&
        navagent_dist(agent->position, agent->target) <= agent->stopping_distance) {
        agent->has_path = 0;
        agent->remaining_distance = 0.0;
        navagent_zero_motion(agent);
    }
}

/// @brief Teleport the agent to `position` (snapped onto the navmesh). Pushes the new position
/// to any bound character/node, zeros velocity, clears the cached path, and rebuilds the path
/// if a target is still active.
void rt_navagent3d_warp(void *obj, void *position) {
    rt_navagent3d *agent = (rt_navagent3d *)obj;
    double world[3];
    if (!agent || !position)
        return;
    world[0] = rt_vec3_x(position);
    world[1] = rt_vec3_y(position);
    world[2] = rt_vec3_z(position);
    navagent_sample_point(agent, world, agent->position);
    navagent_push_position_to_bindings(agent);
    navagent_zero_motion(agent);
    navagent_clear_path(agent);
    if (agent->has_target)
        navagent_rebuild_path(agent);
}

/// @brief Read the agent's current world position. Re-syncs from any bound character/node first.
void *rt_navagent3d_get_position(void *obj) {
    rt_navagent3d *agent = (rt_navagent3d *)obj;
    if (!agent)
        return rt_vec3_new(0.0, 0.0, 0.0);
    navagent_sync_position_from_bindings(agent);
    return rt_vec3_new(agent->position[0], agent->position[1], agent->position[2]);
}

/// @brief Read the agent's actual velocity (position-delta over the last `_update`'s dt).
void *rt_navagent3d_get_velocity(void *obj) {
    rt_navagent3d *agent = (rt_navagent3d *)obj;
    if (!agent)
        return rt_vec3_new(0.0, 0.0, 0.0);
    return rt_vec3_new(agent->velocity[0], agent->velocity[1], agent->velocity[2]);
}

/// @brief Read the agent's *desired* velocity — the steering direction it tried to move at this
/// frame, before character-controller collisions. May differ from `_get_velocity` when blocked.
void *rt_navagent3d_get_desired_velocity(void *obj) {
    rt_navagent3d *agent = (rt_navagent3d *)obj;
    if (!agent)
        return rt_vec3_new(0.0, 0.0, 0.0);
    return rt_vec3_new(
        agent->desired_velocity[0], agent->desired_velocity[1], agent->desired_velocity[2]);
}

/// @brief Returns 1 if the agent currently has an active path being followed.
int8_t rt_navagent3d_get_has_path(void *obj) {
    return obj ? ((rt_navagent3d *)obj)->has_path : 0;
}

/// @brief World-space distance from the agent's current position along the path to the goal.
/// Updated each `_update` tick. 0 when no path exists or stopping distance has been reached.
double rt_navagent3d_get_remaining_distance(void *obj) {
    return obj ? ((rt_navagent3d *)obj)->remaining_distance : 0.0;
}

/// @brief Distance from the goal at which the agent stops moving (default = radius).
double rt_navagent3d_get_stopping_distance(void *obj) {
    return obj ? ((rt_navagent3d *)obj)->stopping_distance : 0.0;
}

/// @brief Set the stopping distance (clamped to ≥ 0). Larger values cause the agent to halt
/// further from the goal — useful for combat/stand-back behaviors.
void rt_navagent3d_set_stopping_distance(void *obj, double distance) {
    if (!obj)
        return;
    ((rt_navagent3d *)obj)->stopping_distance = distance >= 0.0 ? distance : 0.0;
}

/// @brief Maximum movement speed in world units per second (default 4).
double rt_navagent3d_get_desired_speed(void *obj) {
    return obj ? ((rt_navagent3d *)obj)->desired_speed : 0.0;
}

/// @brief Set max movement speed (clamped to ≥ 0). Updates take effect on the next `_update`.
void rt_navagent3d_set_desired_speed(void *obj, double speed) {
    if (!obj)
        return;
    ((rt_navagent3d *)obj)->desired_speed = speed >= 0.0 ? speed : 0.0;
}

/// @brief Returns 1 if the agent automatically rebuilds its path on the repath interval. When
/// disabled, the path is built once per `_set_target` and never refreshed.
int8_t rt_navagent3d_get_auto_repath(void *obj) {
    return obj ? ((rt_navagent3d *)obj)->auto_repath : 0;
}

/// @brief Toggle automatic re-pathing every 0.25 s. Disable to manually control path freshness
/// (useful when target is static and rebuilds would waste CPU).
void rt_navagent3d_set_auto_repath(void *obj, int8_t enabled) {
    if (!obj)
        return;
    ((rt_navagent3d *)obj)->auto_repath = enabled ? 1 : 0;
}

/// @brief Bind the agent to a CharacterController3D — `_update` will call `_move` on the
/// controller (respecting collisions) instead of moving the agent's position directly. The
/// agent's position is then read back from the controller post-move. Replaces any prior binding.
void rt_navagent3d_bind_character(void *obj, void *controller) {
    rt_navagent3d *agent = (rt_navagent3d *)obj;
    if (!agent)
        return;
    if (agent->bound_character == controller)
        return;
    if (controller)
        rt_obj_retain_maybe(controller);
    navagent_release_ref(&agent->bound_character);
    agent->bound_character = controller;
    navagent_sync_position_from_bindings(agent);
    if (agent->bound_character && agent->bound_node)
        navagent_set_node_world_position(agent->bound_node, agent->position);
}

/// @brief Bind the agent to a SceneNode3D — the node's world position will be updated to
/// match the agent each `_update`. Useful for keeping a visual rig in sync with the AI mover.
void rt_navagent3d_bind_node(void *obj, void *node) {
    rt_navagent3d *agent = (rt_navagent3d *)obj;
    if (!agent)
        return;
    if (agent->bound_node == node)
        return;
    if (node)
        rt_obj_retain_maybe(node);
    navagent_release_ref(&agent->bound_node);
    agent->bound_node = node;
    if (agent->bound_character) {
        navagent_sync_position_from_bindings(agent);
        if (agent->bound_node)
            navagent_set_node_world_position(agent->bound_node, agent->position);
    } else {
        navagent_sync_position_from_bindings(agent);
    }
}

#else
typedef int rt_graphics_disabled_tu_guard;
#endif /* VIPER_ENABLE_GRAPHICS */
