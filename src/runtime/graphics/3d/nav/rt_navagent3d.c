//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/nav/rt_navagent3d.c
// Purpose: Gameplay-facing single-agent navigation built on NavMesh3D path
//   queries, with optional Character3D and SceneNode3D bindings.
//
// Key invariants:
//   - Agent positions are snapped onto the bound NavMesh3D (closest valid point).
//   - Path corners are stored as a packed `[x0,y0,z0, x1,y1,z1, ...]` buffer.
//   - `path_index` advances past corners within `corner_tolerance` of the agent.
//   - When a CharacterController3D is bound it owns the authoritative position;
//     otherwise the agent integrates `desired_velocity * dt` directly.
//   - Auto-repath fires every `repath_interval` seconds while a target is active.
//
// Ownership/Lifetime:
//   - NavAgent3D is GC-managed; finalizer frees the path buffer and releases the
//     navmesh / bound character / bound node refs.
//   - All bindings (navmesh, character, node) are retained on assignment and
//     released on rebind / finalize.
//
// Links: rt_navagent3d.h, rt_navmesh3d.h, rt_physics3d.h, rt_scene3d.h
//
//===----------------------------------------------------------------------===//

#ifdef VIPER_ENABLE_GRAPHICS

#include "rt_navagent3d.h"

#include "rt_graphics3d_ids.h"
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

typedef struct rt_navagent3d {
    void *vptr;
    void *navmesh;
    void *bound_character;
    void *bound_node;
    double radius;
    double height;
    double avoidance_radius;
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
    int8_t avoidance_enabled;
    struct rt_navagent3d *registry_next;
    /* Uniform spatial-hash grid links for O(1)-ish neighbor queries during avoidance. The agent is
     * present in exactly one cell (grid_cx,grid_cz); the cell is refreshed whenever position syncs.
     */
    struct rt_navagent3d *grid_next;
    int32_t grid_cx;
    int32_t grid_cz;
    int8_t in_grid;
} rt_navagent3d;

static rt_navagent3d *g_navagent3d_registry = NULL;

/* Spatial hash over agent XZ positions: each registered agent lives in exactly one bucket so
 * avoidance can scan a small cell neighborhood instead of the whole registry (O(N^2) -> ~O(N)). */
#define NAVAGENT_GRID_CELL 4.0
#define NAVAGENT_GRID_BUCKETS 1024u /* power of two for mask-based modulo */
#define NAVAGENT_GRID_MAX_RING 16   /* beyond this, fall back to a full registry scan */
#define NAVAGENT_RVO_MIN_TIME_HORIZON 0.75
#define NAVAGENT_RVO_MAX_TIME_HORIZON 2.0
#define NAVAGENT_RVO_MAX_CANDIDATES 48
static rt_navagent3d *g_navagent3d_grid[NAVAGENT_GRID_BUCKETS];
/* Monotonic max of (effective avoidance radius + desired speed) across agents; bounds the cell
 * neighborhood a query must cover so the grid never misses a contributing peer. */
static double g_navagent3d_max_reach = 0.0;

static void navagent_grid_refresh(rt_navagent3d *agent);
static void navagent_grid_remove(rt_navagent3d *agent);

/// @brief Validate @p obj as a NavAgent3D handle and return its typed pointer (NULL on mismatch).
static rt_navagent3d *navagent3d_checked(void *obj) {
    return (rt_navagent3d *)rt_g3d_checked_or_null(obj, RT_G3D_NAVAGENT3D_CLASS_ID);
}

/// @brief Add an agent to the global registry and insert it into the spatial grid at its cell.
static void navagent_register(rt_navagent3d *agent) {
    if (!agent)
        return;
    agent->registry_next = g_navagent3d_registry;
    g_navagent3d_registry = agent;
    navagent_grid_refresh(agent); /* insert into the spatial grid at its current cell */
}

/// @brief Remove an agent from the spatial grid and unlink it from the global registry.
static void navagent_unregister(rt_navagent3d *agent) {
    rt_navagent3d **link = &g_navagent3d_registry;
    if (!agent)
        return;
    navagent_grid_remove(agent);
    while (*link) {
        if (*link == agent) {
            *link = agent->registry_next;
            agent->registry_next = NULL;
            return;
        }
        link = &(*link)->registry_next;
    }
}

/// @brief Squared length of a 3-vector; avoids a sqrt when only ordering/thresholding matters.
static double navagent_len_sq(const double v[3]) {
    return v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
}

/// @brief Euclidean length of a 3-vector (`sqrt` of the squared length).
static double navagent_len(const double v[3]) {
    return sqrt(navagent_len_sq(v));
}

/// @brief Clamp a value at zero from below, mapping non-finite values to 0.
static double navagent_clamp_nonnegative(double value) {
    return (isfinite(value) && value >= 0.0) ? value : 0.0;
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

/// @brief Release a locally-held GC reference, freeing the object if the refcount reaches zero.
/// @details Used by navagent cleanup to drop borrowed pointers (mesh, path, etc.)
///          that were retained at assignment time.
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

/// @brief The agent's avoidance radius if positive and finite, else 0 (avoidance disabled).
static double navagent_effective_avoidance_radius(const rt_navagent3d *agent) {
    if (!agent || !isfinite(agent->avoidance_radius) || agent->avoidance_radius <= 0.0)
        return 0.0;
    return agent->avoidance_radius;
}

/// @brief An agent's interaction "reach": radius plus a bounded RVO time horizon of travel.
///   Two agents can only influence each other within (reachA + reachB), which bounds the grid
///   query.
static double navagent_reach(const rt_navagent3d *agent) {
    double r = navagent_effective_avoidance_radius(agent);
    double s =
        (isfinite(agent->desired_speed) && agent->desired_speed > 0.0) ? agent->desired_speed : 0.0;
    return r + s * NAVAGENT_RVO_MAX_TIME_HORIZON;
}

/// @brief Quantize a world coordinate to its spatial-grid cell index (clamped to ±1e9; 0 if
/// non-finite).
static int32_t navagent_grid_coord(double v) {
    double c;
    if (!isfinite(v))
        return 0;
    c = floor(v / NAVAGENT_GRID_CELL);
    if (c < -1.0e9)
        c = -1.0e9;
    if (c > 1.0e9)
        c = 1.0e9;
    return (int32_t)c;
}

/// @brief Hash a 2D cell (cx, cz) to a grid bucket using the Teschner spatial-hash primes.
/// @details NAVAGENT_GRID_BUCKETS is a power of two, so the mask replaces a modulo.
static uint32_t navagent_grid_bucket(int32_t cx, int32_t cz) {
    uint32_t h = (uint32_t)cx * 73856093u ^ (uint32_t)cz * 19349663u;
    return h & (NAVAGENT_GRID_BUCKETS - 1u);
}

/// @brief Insert an agent into the spatial grid bucket for its current XZ cell (no-op if already
/// in).
static void navagent_grid_insert(rt_navagent3d *agent) {
    uint32_t b;
    if (!agent || agent->in_grid)
        return;
    agent->grid_cx = navagent_grid_coord(agent->position[0]);
    agent->grid_cz = navagent_grid_coord(agent->position[2]);
    b = navagent_grid_bucket(agent->grid_cx, agent->grid_cz);
    agent->grid_next = g_navagent3d_grid[b];
    g_navagent3d_grid[b] = agent;
    agent->in_grid = 1;
}

/// @brief Unlink an agent from its spatial-grid bucket (no-op if not currently in the grid).
static void navagent_grid_remove(rt_navagent3d *agent) {
    uint32_t b;
    rt_navagent3d **link;
    if (!agent || !agent->in_grid)
        return;
    b = navagent_grid_bucket(agent->grid_cx, agent->grid_cz);
    link = &g_navagent3d_grid[b];
    while (*link) {
        if (*link == agent) {
            *link = agent->grid_next;
            agent->grid_next = NULL;
            break;
        }
        link = &(*link)->grid_next;
    }
    agent->in_grid = 0;
}

/// @brief Keep @p agent's grid cell current and grow the global reach bound. Called after every
///   position sync and on registration; moves the agent between buckets only when its cell changes.
static void navagent_grid_refresh(rt_navagent3d *agent) {
    double reach;
    int32_t cx;
    int32_t cz;
    if (!agent)
        return;
    reach = navagent_reach(agent);
    if (reach > g_navagent3d_max_reach)
        g_navagent3d_max_reach = reach;
    if (!agent->in_grid) {
        navagent_grid_insert(agent);
        return;
    }
    cx = navagent_grid_coord(agent->position[0]);
    cz = navagent_grid_coord(agent->position[2]);
    if (cx != agent->grid_cx || cz != agent->grid_cz) {
        navagent_grid_remove(agent);
        navagent_grid_insert(agent);
    }
}

/// @brief Estimate an agent's current preferred XZ velocity for reciprocal avoidance.
/// @details During an update, `desired_velocity` already holds the freshly computed preferred
///   velocity for the current agent. Peers may not have updated yet this frame, so fall back to
///   their current path corner and desired speed before using their previous actual velocity.
static void navagent_preferred_velocity_xz(const rt_navagent3d *agent,
                                           double *out_vx,
                                           double *out_vz) {
    double speed;
    if (!out_vx || !out_vz)
        return;
    *out_vx = 0.0;
    *out_vz = 0.0;
    if (!agent)
        return;
    if (isfinite(agent->desired_velocity[0]) && isfinite(agent->desired_velocity[2]) &&
        agent->desired_velocity[0] * agent->desired_velocity[0] +
                agent->desired_velocity[2] * agent->desired_velocity[2] >
            1e-10) {
        *out_vx = agent->desired_velocity[0];
        *out_vz = agent->desired_velocity[2];
        return;
    }
    if (agent->has_path && agent->path_points_xyz && agent->path_point_count > 0) {
        int32_t idx = agent->path_index;
        const double *next;
        double dx;
        double dz;
        double dist;
        if (idx < 0)
            idx = 0;
        if (idx >= agent->path_point_count)
            idx = agent->path_point_count - 1;
        next = navagent_path_point(agent, idx);
        dx = next[0] - agent->position[0];
        dz = next[2] - agent->position[2];
        dist = sqrt(dx * dx + dz * dz);
        speed = (isfinite(agent->desired_speed) && agent->desired_speed > 0.0)
                    ? agent->desired_speed
                    : 0.0;
        if (dist > 1e-9 && speed > 0.0) {
            *out_vx = dx / dist * speed;
            *out_vz = dz / dist * speed;
            return;
        }
    }
    if (agent->has_target) {
        double dx = agent->target[0] - agent->position[0];
        double dz = agent->target[2] - agent->position[2];
        double dist = sqrt(dx * dx + dz * dz);
        speed = (isfinite(agent->desired_speed) && agent->desired_speed > 0.0)
                    ? agent->desired_speed
                    : 0.0;
        if (dist > 1e-9 && speed > 0.0) {
            *out_vx = dx / dist * speed;
            *out_vz = dz / dist * speed;
            return;
        }
    }
    if (isfinite(agent->velocity[0]) && isfinite(agent->velocity[2])) {
        *out_vx = agent->velocity[0];
        *out_vz = agent->velocity[2];
    }
}

/// @brief Add a velocity candidate after speed clamping and near-duplicate removal.
static int navagent_add_velocity_candidate(double candidates[NAVAGENT_RVO_MAX_CANDIDATES][2],
                                           int count,
                                           double vx,
                                           double vz,
                                           double max_speed) {
    double speed_sq;
    if (!isfinite(vx) || !isfinite(vz) || !isfinite(max_speed) || max_speed < 0.0)
        return count;
    speed_sq = vx * vx + vz * vz;
    if (speed_sq > max_speed * max_speed && speed_sq > 1e-12) {
        double scale = max_speed / sqrt(speed_sq);
        vx *= scale;
        vz *= scale;
    }
    for (int i = 0; i < count; ++i) {
        double dx = candidates[i][0] - vx;
        double dz = candidates[i][1] - vz;
        if (dx * dx + dz * dz <= 1e-10)
            return count;
    }
    if (count >= NAVAGENT_RVO_MAX_CANDIDATES)
        return count;
    candidates[count][0] = vx;
    candidates[count][1] = vz;
    return count + 1;
}

/// @brief Build a deterministic RVO candidate set around the preferred velocity.
static int navagent_build_velocity_candidates(double pref_x,
                                              double pref_z,
                                              double max_speed,
                                              double candidates[NAVAGENT_RVO_MAX_CANDIDATES][2]) {
    static const double speed_mults[] = {1.0, 0.75, 0.5};
    static const double angle_offsets[] = {0.0,
                                           0.3490658503988659,
                                           -0.3490658503988659,
                                           0.6981317007977318,
                                           -0.6981317007977318,
                                           1.0471975511965976,
                                           -1.0471975511965976,
                                           1.5707963267948966,
                                           -1.5707963267948966,
                                           2.356194490192345,
                                           -2.356194490192345,
                                           3.141592653589793};
    double pref_len = sqrt(pref_x * pref_x + pref_z * pref_z);
    double base_angle = pref_len > 1e-9 ? atan2(pref_z, pref_x) : 0.0;
    int count = 0;
    count = navagent_add_velocity_candidate(candidates, count, pref_x, pref_z, max_speed);
    count = navagent_add_velocity_candidate(candidates, count, 0.0, 0.0, max_speed);
    for (size_t s = 0; s < sizeof(speed_mults) / sizeof(speed_mults[0]); ++s) {
        double speed = max_speed * speed_mults[s];
        for (size_t a = 0; a < sizeof(angle_offsets) / sizeof(angle_offsets[0]); ++a) {
            double theta = base_angle + angle_offsets[a];
            count = navagent_add_velocity_candidate(
                candidates, count, cos(theta) * speed, sin(theta) * speed, max_speed);
        }
    }
    return count;
}

/// @brief Penalty for choosing candidate velocity (`cand_x`,`cand_z`) against one peer.
/// @details This is a reciprocal velocity-obstacle test: it predicts relative motion over the
///   bounded time horizon and penalizes candidates that enter the combined avoidance disk.
static double navagent_rvo_peer_penalty(rt_navagent3d *agent,
                                        rt_navagent3d *other,
                                        double agent_radius,
                                        double horizon,
                                        double cand_x,
                                        double cand_z) {
    double other_radius;
    double combined;
    double rel_px;
    double rel_pz;
    double other_vx = 0.0;
    double other_vz = 0.0;
    double rel_vx;
    double rel_vz;
    double dist_sq;
    double combined_sq;
    double rel_speed_sq;
    double dot;
    double t;
    double closest_x;
    double closest_z;
    double closest_sq;
    if (other == agent || !other->avoidance_enabled || other->navmesh != agent->navmesh)
        return 0.0;
    other_radius = navagent_effective_avoidance_radius(other);
    combined = agent_radius + other_radius;
    if (combined <= 0.0)
        return 0.0;
    rel_px = other->position[0] - agent->position[0];
    rel_pz = other->position[2] - agent->position[2];
    dist_sq = rel_px * rel_px + rel_pz * rel_pz;
    combined_sq = combined * combined;
    navagent_preferred_velocity_xz(other, &other_vx, &other_vz);
    rel_vx = other_vx - cand_x;
    rel_vz = other_vz - cand_z;
    rel_speed_sq = rel_vx * rel_vx + rel_vz * rel_vz;

    if (dist_sq < combined_sq) {
        double dist = dist_sq > 1e-12 ? sqrt(dist_sq) : 0.0;
        double penetration = (combined - dist) / combined;
        double sep_rate = 0.0;
        if (dist > 1e-9)
            sep_rate = (rel_px * rel_vx + rel_pz * rel_vz) / dist;
        else
            sep_rate = -sqrt(cand_x * cand_x + cand_z * cand_z);
        return 2500.0 * penetration * penetration +
               50.0 * fmax(0.0, (combined / horizon) - sep_rate);
    }
    if (rel_speed_sq <= 1e-10)
        return 0.0;
    dot = rel_px * rel_vx + rel_pz * rel_vz;
    t = -dot / rel_speed_sq;
    if (t < 0.0 || t > horizon)
        return 0.0;
    closest_x = rel_px + rel_vx * t;
    closest_z = rel_pz + rel_vz * t;
    closest_sq = closest_x * closest_x + closest_z * closest_z;
    if (closest_sq >= combined_sq)
        return 0.0;
    {
        double closest = closest_sq > 1e-12 ? sqrt(closest_sq) : 0.0;
        double risk = (combined - closest) / combined;
        double time_weight = 1.0 + (horizon - t) / horizon;
        return 1000.0 * risk * risk * time_weight;
    }
}

/// @brief Evaluate one candidate velocity against the grid or full registry.
static double navagent_rvo_candidate_penalty(rt_navagent3d *agent,
                                             double agent_radius,
                                             double horizon,
                                             int use_grid,
                                             double cand_x,
                                             double cand_z) {
    rt_navagent3d *other;
    double penalty = 0.0;
    if (use_grid && agent->in_grid) {
        double dmax = navagent_reach(agent) + g_navagent3d_max_reach;
        int ring = (int)ceil(dmax / NAVAGENT_GRID_CELL) + 1;
        if (ring >= 1 && ring <= NAVAGENT_GRID_MAX_RING) {
            int32_t cx = agent->grid_cx;
            int32_t cz = agent->grid_cz;
            int32_t dz;
            for (dz = -ring; dz <= ring; dz++) {
                int32_t dx;
                for (dx = -ring; dx <= ring; dx++) {
                    int32_t qx = cx + dx;
                    int32_t qz = cz + dz;
                    uint32_t b = navagent_grid_bucket(qx, qz);
                    for (other = g_navagent3d_grid[b]; other; other = other->grid_next) {
                        if (other->grid_cx == qx && other->grid_cz == qz) {
                            penalty += navagent_rvo_peer_penalty(
                                agent, other, agent_radius, horizon, cand_x, cand_z);
                        }
                    }
                }
            }
            return penalty;
        }
        /* reach too large for a tight neighborhood: fall through to the full scan */
    }
    for (other = g_navagent3d_registry; other; other = other->registry_next)
        penalty += navagent_rvo_peer_penalty(agent, other, agent_radius, horizon, cand_x, cand_z);
    return penalty;
}

/// @brief Compute a reciprocal velocity-obstacle steering adjustment for @p agent.
/// @details The solver samples a deterministic set of admissible velocities around the preferred
///   path-following velocity, scores each against predicted peer collisions over a bounded time
///   horizon, and returns the delta from preferred to the best candidate. With @p use_grid it scans
///   only the spatial-grid cell neighborhood sized to cover the RVO horizon.
///   @return 1 if avoidance applies (outputs written), 0 otherwise.
static int navagent_compute_avoidance_adjust(
    rt_navagent3d *agent, double dt, int use_grid, double *out_ax, double *out_az) {
    double agent_radius;
    double max_speed;
    double horizon;
    double pref_x;
    double pref_z;
    double pref_len;
    double right_x = 0.0;
    double right_z = 0.0;
    double candidates[NAVAGENT_RVO_MAX_CANDIDATES][2];
    int candidate_count;
    double best_x;
    double best_z;
    double best_score = 1.0e300;
    if (!agent || !agent->avoidance_enabled)
        return 0;
    agent_radius = navagent_effective_avoidance_radius(agent);
    max_speed =
        (isfinite(agent->desired_speed) && agent->desired_speed > 0.0) ? agent->desired_speed : 0.0;
    if (agent_radius <= 0.0 || max_speed <= 0.0)
        return 0;
    horizon = (isfinite(dt) && dt > 0.0) ? dt * 12.0 : NAVAGENT_RVO_MIN_TIME_HORIZON;
    if (horizon < NAVAGENT_RVO_MIN_TIME_HORIZON)
        horizon = NAVAGENT_RVO_MIN_TIME_HORIZON;
    if (horizon > NAVAGENT_RVO_MAX_TIME_HORIZON)
        horizon = NAVAGENT_RVO_MAX_TIME_HORIZON;
    pref_x = agent->desired_velocity[0];
    pref_z = agent->desired_velocity[2];
    if (!isfinite(pref_x) || !isfinite(pref_z))
        return 0;
    pref_len = sqrt(pref_x * pref_x + pref_z * pref_z);
    if (pref_len <= 1e-9)
        return 0;
    right_x = pref_z / pref_len;
    right_z = -pref_x / pref_len;
    candidate_count = navagent_build_velocity_candidates(pref_x, pref_z, max_speed, candidates);
    best_x = pref_x;
    best_z = pref_z;
    for (int i = 0; i < candidate_count; ++i) {
        double cand_x = candidates[i][0];
        double cand_z = candidates[i][1];
        double dx = cand_x - pref_x;
        double dz = cand_z - pref_z;
        double cand_speed = sqrt(cand_x * cand_x + cand_z * cand_z);
        double score = dx * dx + dz * dz;
        double forward = cand_x * pref_x + cand_z * pref_z;
        double side = cand_x * right_x + cand_z * right_z;
        double speed_loss = max_speed - cand_speed;
        score +=
            navagent_rvo_candidate_penalty(agent, agent_radius, horizon, use_grid, cand_x, cand_z);
        if (speed_loss > 0.0)
            score += speed_loss * speed_loss * 1.5;
        if (forward < 0.0)
            score += max_speed * max_speed * 2.0;
        /* Stable reciprocal passing side: prefer the candidate to the agent's right only as a
         * tie-breaker. Opposite headings therefore choose opposite world-space sides. */
        score -= side * max_speed * 1e-4;
        if (score < best_score) {
            best_score = score;
            best_x = cand_x;
            best_z = cand_z;
        }
    }
    *out_ax = best_x - pref_x;
    *out_az = best_z - pref_z;
    return 1;
}

/// @brief Steer the agent's desired velocity to avoid nearby peers.
/// @details Gathers peers via the spatial grid and applies a reciprocal velocity-obstacle candidate
///   solution. The selected candidate stays within DesiredSpeed, favors the preferred path
///   velocity, and predicts peer collisions across a bounded time horizon instead of only pushing
///   away after overlap.
static void navagent_apply_local_avoidance(rt_navagent3d *agent, double dt) {
    double max_speed;
    double adjust_x = 0.0;
    double adjust_z = 0.0;
    if (!navagent_compute_avoidance_adjust(agent, dt, 1, &adjust_x, &adjust_z))
        return;
    max_speed =
        (isfinite(agent->desired_speed) && agent->desired_speed > 0.0) ? agent->desired_speed : 0.0;
    if (fabs(adjust_x) + fabs(adjust_z) <= 1e-10)
        return;
    agent->desired_velocity[0] += adjust_x;
    agent->desired_velocity[2] += adjust_z;
    {
        double speed_sq = navagent_len_sq(agent->desired_velocity);
        double max_speed_sq = max_speed * max_speed;
        if (speed_sq > max_speed_sq && speed_sq > 1e-10) {
            double scale = max_speed / sqrt(speed_sq);
            agent->desired_velocity[0] *= scale;
            agent->desired_velocity[1] *= scale;
            agent->desired_velocity[2] *= scale;
        }
    }
}

/// @brief Test-only: verify the spatial-grid avoidance query produces the same steering adjustment
///   as a full registry scan for every registered agent (they must agree up to floating-point
///   summation order). @return 1 if all agents agree (or none registered), 0 on any mismatch. Not
///   part of the scripting surface.
int8_t rt_navagent3d_check_avoidance_grid_parity(void) {
    rt_navagent3d *a;
    for (a = g_navagent3d_registry; a; a = a->registry_next) {
        double gx = 0.0;
        double gz = 0.0;
        double fx = 0.0;
        double fz = 0.0;
        int g = navagent_compute_avoidance_adjust(a, 1.0 / 60.0, 1, &gx, &gz);
        int f = navagent_compute_avoidance_adjust(a, 1.0 / 60.0, 0, &fx, &fz);
        if (g != f)
            return 0;
        if (g && (fabs(gx - fx) > 1e-6 || fabs(gz - fz) > 1e-6))
            return 0;
    }
    return 1;
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
        void *local =
            (parent_inv && world_vec) ? rt_mat4_transform_point(parent_inv, world_vec) : NULL;
        if (local) {
            rt_scene_node3d_set_position(
                node, rt_vec3_x(local), rt_vec3_y(local), rt_vec3_z(local));
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
    } else if (agent->bound_node) {
        navagent_get_node_world_position(agent->bound_node, agent->position);
    }
    /* The synced position may have moved the agent to a new cell — keep the spatial grid aligned so
     * other agents' avoidance queries find it. (An agent only moves during its own update/sync.) */
    navagent_grid_refresh(agent);
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
           navagent_dist(agent->position, navagent_path_point(agent, agent->path_index)) <=
               tolerance) {
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
        remaining +=
            navagent_dist(navagent_path_point(agent, i), navagent_path_point(agent, i + 1));
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
    navagent_unregister(agent);
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
    if (navmesh && !rt_g3d_has_class(navmesh, RT_G3D_NAVMESH3D_CLASS_ID))
        return NULL;
    rt_navagent3d *agent =
        (rt_navagent3d *)rt_obj_new_i64(RT_G3D_NAVAGENT3D_CLASS_ID, (int64_t)sizeof(rt_navagent3d));
    if (!agent)
        return NULL;
    memset(agent, 0, sizeof(*agent));
    agent->vptr = NULL;
    agent->radius = (isfinite(radius) && radius > 0.0) ? radius : 0.4;
    agent->height = (isfinite(height) && height > 0.0) ? height : 1.8;
    agent->avoidance_radius = agent->radius;
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
    navagent_register(agent);
    return agent;
}

/// @brief Set the agent's destination. The position is snapped onto the navmesh, and a path is
/// rebuilt immediately. Subsequent `_update` calls steer the agent along the path until it
/// reaches `_get_stopping_distance` of the target.
void rt_navagent3d_set_target(void *obj, void *position) {
    rt_navagent3d *agent = navagent3d_checked(obj);
    if (!agent || !rt_g3d_is_vec3(position))
        return;
    navagent_sync_position_from_bindings(agent);
    navagent_sample_point(
        agent,
        (double[3]){rt_vec3_x(position), rt_vec3_y(position), rt_vec3_z(position)},
        agent->target);
    agent->has_target = 1;
    navagent_rebuild_path(agent);
}

/// @brief Cancel the active target and clear the path. Subsequent `_update` calls do nothing
/// until a new target is set. Velocity is zeroed so AI motion stops cleanly.
void rt_navagent3d_clear_target(void *obj) {
    rt_navagent3d *agent = navagent3d_checked(obj);
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
    rt_navagent3d *agent = navagent3d_checked(obj);
    double prev_pos[3];
    double target_dist;
    if (!agent || !isfinite(dt) || dt <= 0.0)
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
        if (!isfinite(speed) || speed < 0.0)
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

    navagent_apply_local_avoidance(agent, dt);

    if (agent->bound_character) {
        void *move_vec = rt_vec3_new(
            agent->desired_velocity[0], agent->desired_velocity[1], agent->desired_velocity[2]);
        rt_character3d_move(agent->bound_character, move_vec, dt);
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
    rt_navagent3d *agent = navagent3d_checked(obj);
    double world[3];
    if (!agent || !rt_g3d_is_vec3(position))
        return;
    world[0] = rt_vec3_x(position);
    world[1] = rt_vec3_y(position);
    world[2] = rt_vec3_z(position);
    navagent_sample_point(agent, world, agent->position);
    navagent_grid_refresh(agent); /* a warp can jump cells — realign the spatial grid immediately */
    navagent_push_position_to_bindings(agent);
    navagent_zero_motion(agent);
    navagent_clear_path(agent);
    if (agent->has_target)
        navagent_rebuild_path(agent);
}

/// @brief Read the agent's current world position. Re-syncs from any bound character/node first.
void *rt_navagent3d_get_position(void *obj) {
    rt_navagent3d *agent = navagent3d_checked(obj);
    if (!agent)
        return rt_vec3_new(0.0, 0.0, 0.0);
    navagent_sync_position_from_bindings(agent);
    return rt_vec3_new(agent->position[0], agent->position[1], agent->position[2]);
}

/// @brief Read the agent's actual velocity (position-delta over the last `_update`'s dt).
void *rt_navagent3d_get_velocity(void *obj) {
    rt_navagent3d *agent = navagent3d_checked(obj);
    if (!agent)
        return rt_vec3_new(0.0, 0.0, 0.0);
    return rt_vec3_new(agent->velocity[0], agent->velocity[1], agent->velocity[2]);
}

/// @brief Read the agent's *desired* velocity — the steering direction it tried to move at this
/// frame, before character-controller collisions. May differ from `_get_velocity` when blocked.
void *rt_navagent3d_get_desired_velocity(void *obj) {
    rt_navagent3d *agent = navagent3d_checked(obj);
    if (!agent)
        return rt_vec3_new(0.0, 0.0, 0.0);
    return rt_vec3_new(
        agent->desired_velocity[0], agent->desired_velocity[1], agent->desired_velocity[2]);
}

/// @brief Returns 1 if the agent currently has an active path being followed.
int8_t rt_navagent3d_get_has_path(void *obj) {
    rt_navagent3d *agent = navagent3d_checked(obj);
    return agent ? agent->has_path : 0;
}

/// @brief World-space distance from the agent's current position along the path to the goal.
/// Updated each `_update` tick. 0 when no path exists or stopping distance has been reached.
double rt_navagent3d_get_remaining_distance(void *obj) {
    rt_navagent3d *agent = navagent3d_checked(obj);
    return agent ? agent->remaining_distance : 0.0;
}

/// @brief Distance from the goal at which the agent stops moving (default = radius).
double rt_navagent3d_get_stopping_distance(void *obj) {
    rt_navagent3d *agent = navagent3d_checked(obj);
    return agent ? agent->stopping_distance : 0.0;
}

/// @brief Set the stopping distance (clamped to ≥ 0). Larger values cause the agent to halt
/// further from the goal — useful for combat/stand-back behaviors.
void rt_navagent3d_set_stopping_distance(void *obj, double distance) {
    rt_navagent3d *agent = navagent3d_checked(obj);
    if (!agent)
        return;
    agent->stopping_distance = (isfinite(distance) && distance >= 0.0) ? distance : 0.0;
}

/// @brief Maximum movement speed in world units per second (default 4).
double rt_navagent3d_get_desired_speed(void *obj) {
    rt_navagent3d *agent = navagent3d_checked(obj);
    return agent ? agent->desired_speed : 0.0;
}

/// @brief Set max movement speed (clamped to ≥ 0). Updates take effect on the next `_update`.
void rt_navagent3d_set_desired_speed(void *obj, double speed) {
    rt_navagent3d *agent = navagent3d_checked(obj);
    if (!agent)
        return;
    agent->desired_speed = (isfinite(speed) && speed >= 0.0) ? speed : 0.0;
}

/// @brief Returns 1 if the agent automatically rebuilds its path on the repath interval. When
/// disabled, the path is built once per `_set_target` and never refreshed.
int8_t rt_navagent3d_get_auto_repath(void *obj) {
    rt_navagent3d *agent = navagent3d_checked(obj);
    return agent ? agent->auto_repath : 0;
}

/// @brief Toggle automatic re-pathing every 0.25 s. Disable to manually control path freshness
/// (useful when target is static and rebuilds would waste CPU).
void rt_navagent3d_set_auto_repath(void *obj, int8_t enabled) {
    rt_navagent3d *agent = navagent3d_checked(obj);
    if (!agent)
        return;
    agent->auto_repath = enabled ? 1 : 0;
}

/// @brief Returns 1 when same-NavMesh local separation steering is enabled.
int8_t rt_navagent3d_get_avoidance_enabled(void *obj) {
    rt_navagent3d *agent = navagent3d_checked(obj);
    return agent ? agent->avoidance_enabled : 0;
}

/// @brief Toggle opt-in same-NavMesh local separation steering.
void rt_navagent3d_set_avoidance_enabled(void *obj, int8_t enabled) {
    rt_navagent3d *agent = navagent3d_checked(obj);
    if (!agent)
        return;
    agent->avoidance_enabled = enabled ? 1 : 0;
}

/// @brief Radius used by local avoidance neighbor separation (defaults to the agent radius).
double rt_navagent3d_get_avoidance_radius(void *obj) {
    rt_navagent3d *agent = navagent3d_checked(obj);
    return agent ? agent->avoidance_radius : 0.0;
}

/// @brief Set local avoidance radius (clamped to >= 0). A zero radius disables separation force.
void rt_navagent3d_set_avoidance_radius(void *obj, double radius) {
    rt_navagent3d *agent = navagent3d_checked(obj);
    if (!agent)
        return;
    agent->avoidance_radius = navagent_clamp_nonnegative(radius);
}

/// @brief Bind the agent to a CharacterController3D — `_update` will call `_move` on the
/// controller (respecting collisions) instead of moving the agent's position directly. The
/// agent's position is then read back from the controller post-move. Replaces any prior binding.
void rt_navagent3d_bind_character(void *obj, void *controller) {
    rt_navagent3d *agent = navagent3d_checked(obj);
    if (!agent)
        return;
    if (controller && !rt_g3d_has_class(controller, RT_G3D_CHARACTER3D_CLASS_ID))
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
    rt_navagent3d *agent = navagent3d_checked(obj);
    if (!agent)
        return;
    if (node && !rt_g3d_has_class(node, RT_G3D_SCENENODE3D_CLASS_ID))
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
