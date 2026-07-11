//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/physics/rt_physics3d_solver.c
// Purpose: Warm-started sequential-impulse contact solver and sweep-and-prune
//   broad phase for the Physics3D runtime — union-find contact islands, warm
//   starting from previous-frame impulses, Gauss-Seidel velocity solve, split
//   positional (Baumgarte) correction, sleep management, and per-step contact
//   detection. Split out of rt_physics3d.c; shares types via the internal hdr.
//
// Key invariants:
//   - Contacts are detected once per substep, then solved iteratively.
//   - Per-manifold-point impulses are accumulated and clamped non-negative;
//     warm starts seed them from the matching previous-frame contact.
//   - Broad phase sorts entries by the widest axis with deterministic reusable-buffer sorting so
//     the inner loop can break early once spans no longer overlap.
//
// Links: rt_physics3d_internal.h, rt_physics3d.c
//
//===----------------------------------------------------------------------===//

#ifdef VIPER_ENABLE_GRAPHICS

#include "rt_collider3d.h"
#include "rt_game3d_diagnostics.h"
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

#define PH3D_BROADPHASE_STACK_FALLBACK 128

/* ====================================================================== *
 * Warm-started sequential-impulse contact solver (Phase 8).
 *
 * Replaces the prior per-iteration re-detect-and-resolve loop. The step now
 * detects contacts once per substep, seeds each manifold point's accumulated
 * normal/friction impulse from the matching previous-frame contact (warm
 * starting), iterates a Gauss-Seidel velocity solve with non-negative
 * accumulated-impulse clamping, then applies a split positional correction so
 * recovery never injects energy into the velocity state. This is what lets
 * box stacks and piles rest stably instead of jittering and sinking.
 * ====================================================================== */

#define PH3D_PENETRATION_SLOP 0.005
#define PH3D_MAX_POSITION_CORRECTION 0.2 /* clamp on per-step positional push (m) */
/* Carry <100% of the prior impulse so an uneven manifold (Gauss-Seidel tends to
 * load the first-solved point) cannot compound its torque frame-over-frame into
 * a slow tip-over. The solver rebuilds the remainder each step. */
#define PH3D_WARMSTART_FACTOR 0.8
#define PH3D_CONTACT_WAKE_SPEED 0.1 /* approach speed (m/s) that wakes a sleeper */

/// @brief Build two orthonormal tangents spanning the plane perpendicular to
///   unit normal @p n (deterministic basis so warm-started friction impulses
///   stay aligned frame to frame while the normal is stable).
static void ph3d_contact_tangents(const double *n, double *t1, double *t2) {
    if (fabs(n[0]) >= 0.57735) {
        vec3_set(t1, n[1], -n[0], 0.0);
    } else {
        vec3_set(t1, 0.0, n[2], -n[1]);
    }
    if (vec3_normalize_in_place(t1) < 1e-12)
        vec3_set(t1, 1.0, 0.0, 0.0);
    vec3_cross(n, t1, t2);
    if (vec3_normalize_in_place(t2) < 1e-12) {
        /* n and t1 nearly parallel (only reachable with a degenerate, non-unit
         * normal): pick any axis orthogonal to t1 so friction never operates on a
         * zero-length tangent. No-op for the valid unit-normal case above. */
        vec3_set(t2, -t1[1], t1[0], 0.0);
        if (vec3_normalize_in_place(t2) < 1e-12)
            vec3_set(t2, 0.0, 0.0, 1.0);
    }
}

/// @brief Effective mass (1 / scalar inverse-mass) for a unit constraint @p axis
///   at the given per-body contact arms, including the angular term.
static double ph3d_contact_effective_mass(const rt_body3d *a,
                                          const double *r_a,
                                          const rt_body3d *b,
                                          const double *r_b,
                                          const double *axis) {
    double denom = body3d_contact_impulse_denominator(a, r_a, axis) +
                   body3d_contact_impulse_denominator(b, r_b, axis);
    return denom > 1e-12 ? 1.0 / denom : 0.0;
}

/// @brief Relative velocity at a contact arm pair projected onto @p axis.
static double ph3d_contact_relative_velocity(const rt_body3d *a,
                                             const double *r_a,
                                             const rt_body3d *b,
                                             const double *r_b,
                                             const double *axis) {
    double vel_a[3];
    double vel_b[3];
    double rel[3];
    body3d_contact_velocity(a, r_a, vel_a);
    body3d_contact_velocity(b, r_b, vel_b);
    vec3_sub(vel_b, vel_a, rel);
    return vec3_dot(rel, axis);
}

/// @brief Apply impulse @p magnitude along @p dir at the contact arms (b gets +,
///   a gets -, matching the a→b normal convention).
static void ph3d_apply_contact_axis_impulse(rt_body3d *a,
                                            const double *r_a,
                                            rt_body3d *b,
                                            const double *r_b,
                                            const double *dir,
                                            double magnitude) {
    double p[3];
    double neg[3];
    if (magnitude == 0.0)
        return;
    p[0] = magnitude * dir[0];
    p[1] = magnitude * dir[1];
    p[2] = magnitude * dir[2];
    vec3_negate(p, neg);
    body3d_apply_contact_impulse(a, neg, r_a);
    body3d_apply_contact_impulse(b, p, r_b);
}

/// @brief A body actively participates in the solve when it is dynamic and awake.
static int ph3d_body_is_active(const rt_body3d *b) {
    return b && b->motion_mode == PH3D_MODE_DYNAMIC && !b->is_sleeping;
}

/// @brief Whether a contact needs solving this step; also propagates wake-up.
/// @details A contact is solved when at least one body is an active (awake,
///   dynamic) body. If one side is active and the other is a *sleeping* dynamic
///   body, the sleeper is woken so the pair solves correctly (wake spreads one
///   contact layer per step). A contact between two non-active bodies (both
///   sleeping, or sleeping-vs-static) is skipped — this is what freezes a fully
///   settled island so a resting stack stays put instead of being re-solved
///   (and slowly drifting) every frame.
static int ph3d_contact_should_solve(rt_body3d *a, rt_body3d *b) {
    int active_a = ph3d_body_is_active(a);
    int active_b = ph3d_body_is_active(b);
    if (!active_a && !active_b)
        return 0;
    if (active_a && !active_b && b && b->motion_mode == PH3D_MODE_DYNAMIC)
        body3d_wake_if_dynamic(b);
    else if (active_b && !active_a && a && a->motion_mode == PH3D_MODE_DYNAMIC)
        body3d_wake_if_dynamic(a);
    return 1;
}

/// @brief Free every scratch array owned by a solver-island batch and zero it. Safe on NULL.
void ph3d_solver_island_batch_free(ph3d_solver_island_batch *batch) {
    if (!batch)
        return;
    free(batch->parent);
    free(batch->active_body);
    free(batch->root_to_island);
    free(batch->body_island);
    free(batch->island_contact_counts);
    free(batch->island_write_offsets);
    free(batch->island_offsets);
    free(batch->contact_indices);
    memset(batch, 0, sizeof(*batch));
}

/// @brief Allocate an int32 array of @p count elements — zero-filled when @p zeroed, else
///   uninitialized — with overflow checks, writing the buffer to @p out.
/// @return 1 on success (including count==0, which yields a NULL buffer), 0 on overflow or OOM.
static int ph3d_alloc_i32_array(int32_t count, int zeroed, int32_t **out) {
    size_t bytes;
    if (!out)
        return 0;
    *out = NULL;
    if (count < 0)
        return 0;
    if ((size_t)count > SIZE_MAX / sizeof(int32_t))
        return 0;
    bytes = (size_t)count * sizeof(int32_t);
    *out = zeroed ? (int32_t *)calloc((size_t)count, sizeof(int32_t)) : (int32_t *)malloc(bytes);
    return *out || count == 0;
}

/// @brief Push a non-negative @p value onto a growable int32 stack (initial capacity 128,
///   doubling thereafter), with overflow checks.
/// @return 1 on success, 0 on invalid args, a negative value, or allocation failure.
int ph3d_i32_stack_push(int32_t **items, int32_t *count, int32_t *capacity, int32_t value) {
    int32_t new_capacity;
    int32_t *grown;
    if (!items || !count || !capacity || value < 0)
        return 0;
    if (*count >= *capacity) {
        new_capacity = *capacity < 128 ? 128 : *capacity;
        if (new_capacity > INT32_MAX / 2)
            return 0;
        if (*capacity >= 128)
            new_capacity *= 2;
        if ((size_t)new_capacity > SIZE_MAX / sizeof(**items))
            return 0;
        grown = (int32_t *)realloc(*items, (size_t)new_capacity * sizeof(**items));
        if (!grown)
            return 0;
        *items = grown;
        *capacity = new_capacity;
    }
    (*items)[(*count)++] = value;
    return 1;
}

/// @brief Allocate the per-body and per-contact scratch arrays for one island-solver batch:
///   union-find parent, active-body flags, root/body→island maps, per-island contact counts
///   and write offsets, the island offset table, and the contact-index buffer.
/// @return 1 if every allocation succeeds, 0 on bad world state or any allocation failure.
static int ph3d_solver_island_batch_alloc(rt_world3d *w, ph3d_solver_island_batch *batch) {
    int32_t island_offset_count;
    if (!w || !batch || w->body_count < 0 || w->contact_count < 0)
        return 0;
    if (w->body_count == INT32_MAX)
        return 0;
    island_offset_count = w->body_count + 1;
    return ph3d_alloc_i32_array(w->body_count, 0, &batch->parent) &&
           ph3d_alloc_i32_array(w->body_count, 1, &batch->active_body) &&
           ph3d_alloc_i32_array(w->body_count, 0, &batch->root_to_island) &&
           ph3d_alloc_i32_array(w->body_count, 0, &batch->body_island) &&
           ph3d_alloc_i32_array(w->body_count, 1, &batch->island_contact_counts) &&
           ph3d_alloc_i32_array(w->body_count, 1, &batch->island_write_offsets) &&
           ph3d_alloc_i32_array(island_offset_count, 1, &batch->island_offsets) &&
           ph3d_alloc_i32_array(w->contact_count, 0, &batch->contact_indices);
}

/// @brief Return the world's index for @p body, using cached owner metadata first.
/// @return Index of @p body in @p w, or -1 if absent or either argument is NULL.
static int32_t world3d_body_index_of(const rt_world3d *w, const rt_body3d *body) {
    if (!w || !body)
        return -1;
    if (body->owner_world == w && body->owner_index >= 0 && body->owner_index < w->body_count &&
        w->bodies[body->owner_index] == body)
        return body->owner_index;
    for (int32_t i = 0; i < w->body_count; ++i) {
        if (w->bodies[i] == body)
            return i;
    }
    return -1;
}

/// @brief Union-find FIND with path compression: return the island root of @p index.
/// @details Walks parent links to the root, then re-points every node on the path directly at
///          the root so subsequent queries are near-constant time.
static int32_t ph3d_island_find(int32_t *parent, int32_t index) {
    int32_t root = index;
    while (parent[root] != root)
        root = parent[root];
    while (parent[index] != index) {
        int32_t next = parent[index];
        parent[index] = root;
        index = next;
    }
    return root;
}

/// @brief Union-find UNION: merge the islands containing bodies @p a and @p b.
/// @details Roots are joined under the numerically smaller index, giving a deterministic island
///          layout independent of contact ordering. No-op when either index is negative.
static void ph3d_island_union(int32_t *parent, int32_t a, int32_t b) {
    int32_t root_a;
    int32_t root_b;
    if (!parent || a < 0 || b < 0)
        return;
    root_a = ph3d_island_find(parent, a);
    root_b = ph3d_island_find(parent, b);
    if (root_a == root_b)
        return;
    if (root_b < root_a) {
        int32_t tmp = root_a;
        root_a = root_b;
        root_b = tmp;
    }
    parent[root_b] = root_a;
}

/// @brief Resolve the world-array indices of a contact's two bodies for solving.
/// @details Writes -1 to both outputs first, then skips trigger contacts and pairs where neither
///          body is active (both asleep/static) so the island builder only links solvable pairs.
/// @return 1 if the contact should be solved (indices written); 0 if it should be skipped.
static int ph3d_contact_solver_body_indices(const rt_world3d *w,
                                            const rt_contact3d *contact,
                                            int32_t *index_a,
                                            int32_t *index_b) {
    if (index_a)
        *index_a = -1;
    if (index_b)
        *index_b = -1;
    if (!w || !contact || contact->is_trigger)
        return 0;
    if (!ph3d_body_is_active(contact->body_a) && !ph3d_body_is_active(contact->body_b))
        return 0;
    if (index_a)
        *index_a = world3d_body_index_of(w, contact->body_a);
    if (index_b)
        *index_b = world3d_body_index_of(w, contact->body_b);
    return 1;
}

/// @brief Partition active bodies into solver islands and bucket each contact under its island.
/// @details Uses union-find over solvable contacts to group connected active bodies into islands,
///          assigns each island a dense id, then builds a CSR-style layout: `island_offsets` holds
///          the per-island prefix sums and `contact_indices` the contacts flattened in island
///          order. Solving islands independently decouples unrelated groups and is the prerequisite
///          for parallelizing the velocity/position solve. The batch must be released with
///          ph3d_solver_island_batch_free.
/// @return 1 on success (including the empty-world no-op); 0 after trapping on allocation failure.
int world3d_build_solver_island_batch(rt_world3d *w, ph3d_solver_island_batch *batch) {
    if (!batch)
        return 0;
    memset(batch, 0, sizeof(*batch));
    if (!w || w->body_count <= 0 || w->contact_count <= 0)
        return 1;

    if (!ph3d_solver_island_batch_alloc(w, batch)) {
        ph3d_solver_island_batch_free(batch);
        rt_trap("Physics3D.World.Step: solver island allocation failed");
        return 0;
    }

    for (int32_t i = 0; i < w->body_count; ++i) {
        batch->parent[i] = i;
        batch->root_to_island[i] = -1;
        batch->body_island[i] = -1;
    }

    for (int32_t i = 0; i < w->contact_count; ++i) {
        rt_contact3d *contact = &w->contacts[i];
        int32_t index_a;
        int32_t index_b;
        int active_a;
        int active_b;
        if (contact->is_trigger || !ph3d_contact_should_solve(contact->body_a, contact->body_b))
            continue;
        index_a = world3d_body_index_of(w, contact->body_a);
        index_b = world3d_body_index_of(w, contact->body_b);
        active_a = index_a >= 0 && ph3d_body_is_active(contact->body_a);
        active_b = index_b >= 0 && ph3d_body_is_active(contact->body_b);
        if (active_a)
            batch->active_body[index_a] = 1;
        if (active_b)
            batch->active_body[index_b] = 1;
        if (active_a && active_b)
            ph3d_island_union(batch->parent, index_a, index_b);
    }

    for (int32_t i = 0; i < w->body_count; ++i) {
        int32_t root;
        int32_t island;
        if (!batch->active_body[i])
            continue;
        root = ph3d_island_find(batch->parent, i);
        island = batch->root_to_island[root];
        if (island < 0) {
            island = batch->island_count++;
            batch->root_to_island[root] = island;
        }
        batch->body_island[i] = island;
        batch->active_body_count++;
    }

    for (int32_t i = 0; i < w->contact_count; ++i) {
        int32_t index_a;
        int32_t index_b;
        int32_t island = -1;
        rt_contact3d *contact = &w->contacts[i];
        if (!ph3d_contact_solver_body_indices(w, contact, &index_a, &index_b))
            continue;
        if (index_a >= 0 && batch->body_island[index_a] >= 0)
            island = batch->body_island[index_a];
        else if (index_b >= 0 && batch->body_island[index_b] >= 0)
            island = batch->body_island[index_b];
        if (island < 0)
            continue;
        batch->island_contact_counts[island]++;
        batch->solver_contact_count++;
    }

    for (int32_t island = 0; island < batch->island_count; ++island)
        batch->island_offsets[island + 1] =
            batch->island_offsets[island] + batch->island_contact_counts[island];
    memcpy(batch->island_write_offsets,
           batch->island_offsets,
           sizeof(int32_t) * (size_t)batch->island_count);

    for (int32_t i = 0; i < w->contact_count; ++i) {
        int32_t index_a;
        int32_t index_b;
        int32_t island = -1;
        int32_t write_index;
        rt_contact3d *contact = &w->contacts[i];
        if (!ph3d_contact_solver_body_indices(w, contact, &index_a, &index_b))
            continue;
        if (index_a >= 0 && batch->body_island[index_a] >= 0)
            island = batch->body_island[index_a];
        else if (index_b >= 0 && batch->body_island[index_b] >= 0)
            island = batch->body_island[index_b];
        if (island < 0)
            continue;
        write_index = batch->island_write_offsets[island]++;
        batch->contact_indices[write_index] = i;
    }

    return 1;
}

/// @brief Match this frame's contacts to the previous frame, seed accumulated
///   impulses (warm starting), capture per-point restitution bias, and re-apply
///   the seeded impulse so the velocity solver starts near the converged answer.
static void world3d_warm_start_contact(rt_world3d *w, rt_contact3d *c) {
    rt_body3d *a;
    rt_body3d *b;
    const rt_contact3d *prev = NULL;
    double e;
    if (!w || !c)
        return;
    a = c->body_a;
    b = c->body_b;
    if (!a || !b || c->is_trigger || !ph3d_contact_should_solve(a, b))
        return;
    {
        double ra = rt_collider3d_effective_restitution_raw(c->collider_a, a->restitution);
        double rb = rt_collider3d_effective_restitution_raw(c->collider_b, b->restitution);
        e = (ra < rb) ? ra : rb;
    }

    /* Identical body/collider ordering keeps stored impulse directions valid. */
    for (int32_t p = 0; p < w->previous_contact_count; ++p) {
        const rt_contact3d *q = &w->previous_contacts[p];
        if (q->body_a == c->body_a && q->body_b == c->body_b && q->collider_a == c->collider_a &&
            q->collider_b == c->collider_b) {
            prev = q;
            break;
        }
    }

    for (int32_t k = 0; k < c->contact_count; ++k) {
        double n[3];
        double t1[3];
        double t2[3];
        double r_a[3];
        double r_b[3];
        double seed_n = 0.0;
        double seed_t0 = 0.0;
        double seed_t1 = 0.0;
        double vn0;
        vec3_copy(n, c->normals[k]);
        ph3d_contact_tangents(n, t1, t2);

        /* Index-based match: the manifold builder emits points in a stable
         * order for a persistent pair, so current point k maps to previous
         * point k. This survives fast motion (unlike a position-proximity
         * match), which is what keeps multi-box stacks converged while they
         * settle. The normal-agreement guard prevents carrying impulse
         * across a normal flip (e.g. a deep-penetration axis change). */
        if (prev && k < prev->contact_count && vec3_dot(n, prev->normals[k]) > 0.9) {
            seed_n = prev->normal_impulse_acc[k] * PH3D_WARMSTART_FACTOR;
            seed_t0 = prev->tangent_impulse_acc[k][0] * PH3D_WARMSTART_FACTOR;
            seed_t1 = prev->tangent_impulse_acc[k][1] * PH3D_WARMSTART_FACTOR;
        }
        c->normal_impulse_acc[k] = seed_n;
        c->tangent_impulse_acc[k][0] = seed_t0;
        c->tangent_impulse_acc[k][1] = seed_t1;

        vec3_sub(c->points[k], a->position, r_a);
        vec3_sub(c->points[k], b->position, r_b);
        vn0 = ph3d_contact_relative_velocity(a, r_a, b, r_b, n);
        /* Restitution is an impact phenomenon: apply it only on the first
         * frame of a contact (no warm-start seed). A persistent/resting
         * contact gets zero restitution bias, so a stack cannot bounce
         * itself apart if a body momentarily speeds up. */
        {
            double threshold = ph3d_clamp_nonnegative_finite(w->restitution_threshold,
                                                             PH3D_DEFAULT_RESTITUTION_THRESHOLD);
            c->restitution_bias[k] = (seed_n == 0.0 && vn0 < -threshold) ? -e * vn0 : 0.0;
        }

        ph3d_apply_contact_axis_impulse(a, r_a, b, r_b, n, seed_n);
        ph3d_apply_contact_axis_impulse(a, r_a, b, r_b, t1, seed_t0);
        ph3d_apply_contact_axis_impulse(a, r_a, b, r_b, t2, seed_t1);
    }
}

/// @brief Sequential-impulse velocity solve for one contact manifold.
/// @details For each manifold point, solves the two friction (tangent) axes first — clamping the
///          accumulated tangent impulse to the friction cone `|t| <= mu * normal_impulse` with a
///          combined `mu = sqrt(friction_a * friction_b)` — then the normal axis, clamping the
///          accumulated normal impulse to be non-negative and steering toward the stored
///          restitution bias. Impulse deltas (new minus accumulated) are applied immediately so
///          later points in the same iteration see updated velocities.
static void world3d_solve_velocity_contact(rt_contact3d *c) {
    rt_body3d *a;
    rt_body3d *b;
    double mu;
    if (!c)
        return;
    a = c->body_a;
    b = c->body_b;
    if (!a || !b || c->is_trigger || (!ph3d_body_is_active(a) && !ph3d_body_is_active(b)))
        return;
    {
        /* Per-collider material overrides win over body values (plan 20);
         * the geometric-mean combine is unchanged. */
        double fa = ph3d_clamp_nonnegative_finite(
            rt_collider3d_effective_friction_raw(c->collider_a, a->friction), 0.0);
        double fb = ph3d_clamp_nonnegative_finite(
            rt_collider3d_effective_friction_raw(c->collider_b, b->friction), 0.0);
        mu = sqrt(fa) * sqrt(fb);
        if (!isfinite(mu))
            mu = 0.0;
    }
    for (int32_t k = 0; k < c->contact_count; ++k) {
        double n[3];
        double t1[3];
        double t2[3];
        double r_a[3];
        double r_b[3];
        const double *tan_axes[2];
        int axis;
        vec3_copy(n, c->normals[k]);
        ph3d_contact_tangents(n, t1, t2);
        vec3_sub(c->points[k], a->position, r_a);
        vec3_sub(c->points[k], b->position, r_b);
        tan_axes[0] = t1;
        tan_axes[1] = t2;

        for (axis = 0; axis < 2; ++axis) {
            double eff = ph3d_contact_effective_mass(a, r_a, b, r_b, tan_axes[axis]);
            double vt = ph3d_contact_relative_velocity(a, r_a, b, r_b, tan_axes[axis]);
            double max_f = mu * c->normal_impulse_acc[k];
            double old = c->tangent_impulse_acc[k][axis];
            double next = old - eff * vt;
            if (next > max_f)
                next = max_f;
            else if (next < -max_f)
                next = -max_f;
            c->tangent_impulse_acc[k][axis] = next;
            ph3d_apply_contact_axis_impulse(a, r_a, b, r_b, tan_axes[axis], next - old);
        }

        {
            double eff = ph3d_contact_effective_mass(a, r_a, b, r_b, n);
            double vn = ph3d_contact_relative_velocity(a, r_a, b, r_b, n);
            double old = c->normal_impulse_acc[k];
            double next = old - eff * (vn - c->restitution_bias[k]);
            if (next < 0.0)
                next = 0.0;
            c->normal_impulse_acc[k] = next;
            ph3d_apply_contact_axis_impulse(a, r_a, b, r_b, n, next - old);
        }
    }
}

/// @brief Split positional (Baumgarte) correction using each manifold's deepest
///   point, weighted by inverse mass. Runs after the velocity solve so it never
///   injects energy into the velocity state.
static void world3d_solve_position_contact(rt_contact3d *c, double beta) {
    rt_body3d *a;
    rt_body3d *b;
    const double *n;
    double inv_sum;
    double pen;
    double corr;
    double next_a[3];
    double next_b[3];
    int32_t deepest = 0;
    if (!c)
        return;
    a = c->body_a;
    b = c->body_b;
    if (!a || !b || c->is_trigger || c->contact_count <= 0 ||
        (!ph3d_body_is_active(a) && !ph3d_body_is_active(b)))
        return;
    for (int32_t k = 1; k < c->contact_count; ++k) {
        if (c->separations[k] < c->separations[deepest])
            deepest = k;
    }
    pen = -c->separations[deepest] - PH3D_PENETRATION_SLOP;
    if (pen <= 0.0)
        return;
    inv_sum = a->inv_mass + b->inv_mass;
    if (inv_sum <= 1e-12)
        return;
    /* Clamp the positional projection so a deep penetration cannot teleport
     * a body (which would otherwise inject huge separation and explode a
     * stack). Mirrors Box2D's b2_maxLinearCorrection. */
    pen = fmin(pen, PH3D_MAX_POSITION_CORRECTION);
    corr = beta * pen / inv_sum;
    n = c->normals[deepest];
    if (!ph3d_vec3_all_finite(n) || !isfinite(corr))
        return;
    for (int axis = 0; axis < 3; axis++) {
        next_a[axis] = a->position[axis] - corr * a->inv_mass * n[axis];
        next_b[axis] = b->position[axis] + corr * b->inv_mass * n[axis];
    }
    if (!ph3d_vec3_all_finite(next_a) || !ph3d_vec3_all_finite(next_b))
        return;
    vec3_copy(a->position, next_a);
    vec3_copy(b->position, next_b);
    if (a->inv_mass > 0.0)
        body3d_touch_broadphase(a);
    if (b->inv_mass > 0.0)
        body3d_touch_broadphase(b);
}

/// @brief Warm-start every solvable contact, walking the batch island by island.
void world3d_warm_start_solver_islands(rt_world3d *w, const ph3d_solver_island_batch *batch) {
    if (!w || !batch)
        return;
    for (int32_t island = 0; island < batch->island_count; ++island) {
        int32_t begin = batch->island_offsets[island];
        int32_t end = batch->island_offsets[island + 1];
        for (int32_t i = begin; i < end; ++i)
            world3d_warm_start_contact(w, &w->contacts[batch->contact_indices[i]]);
    }
}

/// @brief Run one velocity-solve pass over every solvable contact, island by island.
void world3d_solve_velocity_solver_islands(const ph3d_solver_island_batch *batch, rt_world3d *w) {
    if (!batch || !w)
        return;
    for (int32_t island = 0; island < batch->island_count; ++island) {
        int32_t begin = batch->island_offsets[island];
        int32_t end = batch->island_offsets[island + 1];
        for (int32_t i = begin; i < end; ++i)
            world3d_solve_velocity_contact(&w->contacts[batch->contact_indices[i]]);
    }
}

/// @brief Run one position-correction pass over every solvable contact, island by island.
void world3d_solve_position_solver_islands(const ph3d_solver_island_batch *batch, rt_world3d *w) {
    double beta;
    if (!batch || !w)
        return;
    beta = clampd(ph3d_finite_or(w->contact_beta, PH3D_DEFAULT_CONTACT_BETA), 0.0, 1.0);
    for (int32_t island = 0; island < batch->island_count; ++island) {
        int32_t begin = batch->island_offsets[island];
        int32_t end = batch->island_offsets[island + 1];
        for (int32_t i = begin; i < end; ++i)
            world3d_solve_position_contact(&w->contacts[batch->contact_indices[i]], beta);
    }
}

/// @brief Publish the per-contact scalar normal impulse (summed over manifold
///   points) for collision-event payloads and wake bodies that took a load.
void world3d_finalize_contacts(rt_world3d *w) {
    if (!w)
        return;
    for (int32_t i = 0; i < w->contact_count; ++i) {
        rt_contact3d *c = &w->contacts[i];
        double total = 0.0;
        if (c->is_trigger) {
            c->normal_impulse = 0.0;
            continue;
        }
        for (int32_t k = 0; k < c->contact_count; ++k)
            total += c->normal_impulse_acc[k];
        c->normal_impulse = total;
        /* Wake on genuine impact only (approach speed), never on the sustained
         * support impulse of a resting contact — otherwise a settled stack can
         * never sleep, and a perpetually-re-solved stack slowly drifts. */
        if (c->relative_speed > PH3D_CONTACT_WAKE_SPEED && c->body_a && c->body_b) {
            body3d_wake_if_dynamic(c->body_a);
            body3d_wake_if_dynamic(c->body_b);
        }
    }
}

/// @brief Post-solve sleep update: a dynamic body whose *resolved* velocity stays
///   below the sleep thresholds for PH3D_SLEEP_DELAY goes to sleep (and is then
///   frozen by the contact gate). Run after the contact solve so a body resting
///   under gravity — momentarily fast mid-integration, ~0 once supported —
///   actually settles. Wake propagation across contacts keeps a body awake while
///   any neighbour it touches is still moving.
void world3d_update_sleep(rt_world3d *w, double sub_dt) {
    if (!w)
        return;
    for (int32_t i = 0; i < w->body_count; i++) {
        rt_body3d *b = w->bodies[i];
        double linear_sq;
        double angular_sq;
        double linear_thresh = PH3D_SLEEP_LINEAR_THRESHOLD * PH3D_SLEEP_LINEAR_THRESHOLD;
        double angular_thresh = PH3D_SLEEP_ANGULAR_THRESHOLD * PH3D_SLEEP_ANGULAR_THRESHOLD;
        if (!b || b->motion_mode != PH3D_MODE_DYNAMIC || b->is_sleeping || !b->can_sleep)
            continue;
        linear_sq = vec3_len_sq(b->velocity);
        angular_sq = vec3_len_sq(b->angular_velocity);
        if (linear_sq <= linear_thresh && angular_sq <= angular_thresh) {
            b->sleep_time += sub_dt;
            if (b->sleep_time >= PH3D_SLEEP_DELAY) {
                b->is_sleeping = 1;
                vec3_set(b->velocity, 0.0, 0.0, 0.0);
                vec3_set(b->angular_velocity, 0.0, 0.0, 0.0);
            }
        } else {
            b->sleep_time = 0.0;
        }
    }
}

/// @brief Order two broad-phase entries along @p primary (0=X, 1=Y, 2=Z), then the
///   other two axes, then the body's stable @c owner_index.
/// @details Tie-breaking on @c owner_index — the body's slot index within the world,
///   which is identical in the VM and native backends and stable run-to-run — rather
///   than the body's heap pointer keeps sweep-and-prune order deterministic. Pointer
///   values vary with allocation order / ASLR and would otherwise desynchronise the
///   order-sensitive warm-started sequential-impulse solve between runs and backends.
/// @param a,b     Entries to compare.
/// @param primary Axis (0/1/2) used as the primary sort key.
/// @return -1, 0, or +1 for sort-order comparisons.
int ph3d_broadphase_compare_entries_axis(const ph3d_broadphase_entry *a,
                                         const ph3d_broadphase_entry *b,
                                         int primary) {
    const int a1 = (primary + 1) % 3;
    const int a2 = (primary + 2) % 3;
    if (a->min[primary] < b->min[primary])
        return -1;
    if (a->min[primary] > b->min[primary])
        return 1;
    if (a->min[a1] < b->min[a1])
        return -1;
    if (a->min[a1] > b->min[a1])
        return 1;
    if (a->min[a2] < b->min[a2])
        return -1;
    if (a->min[a2] > b->min[a2])
        return 1;
    {
        int32_t ia = a->body ? a->body->owner_index : -1;
        int32_t ib = b->body ? b->body->owner_index : -1;
        if (ia < ib)
            return -1;
        if (ia > ib)
            return 1;
    }
    return 0;
}

/// @brief Legacy comparator adapter for sweep-and-prune broad-phase entries by min-X.
/// @details After sorting, the inner collision loop can break early as soon as
///   entry[j].min[0] > entry[i].max[0], reducing the O(n²) pair count in practice.
///   Non-static for tests and internal code that still wants a C comparator adapter.
int ph3d_broadphase_compare_min_x(const void *lhs, const void *rhs) {
    return ph3d_broadphase_compare_entries_axis(
        (const ph3d_broadphase_entry *)lhs, (const ph3d_broadphase_entry *)rhs, 0);
}

/// @brief Sort a small or nearly sorted broadphase entry array with insertion sort.
/// @details Motion between physics steps is usually coherent, so insertion sort is faster than a
///   general-purpose indirect-comparator sort when the previous order is mostly preserved.
static void ph3d_broadphase_insertion_sort(ph3d_broadphase_entry *entries,
                                           int32_t count,
                                           int axis) {
    if (!entries || count <= 1)
        return;
    for (int32_t i = 1; i < count; i++) {
        ph3d_broadphase_entry key = entries[i];
        int32_t j = i - 1;
        while (j >= 0 && ph3d_broadphase_compare_entries_axis(&entries[j], &key, axis) > 0) {
            entries[j + 1] = entries[j];
            j--;
        }
        entries[j + 1] = key;
    }
}

/// @brief Count adjacent inversions up to @p limit to decide whether insertion sort is cheap.
/// @details The scan short-circuits once the data is clearly unordered; it is a small O(n) probe
///   that avoids running insertion sort on fully scrambled broadphase arrays.
static int32_t ph3d_broadphase_adjacent_inversions(const ph3d_broadphase_entry *entries,
                                                   int32_t count,
                                                   int axis,
                                                   int32_t limit) {
    int32_t inversions = 0;
    if (!entries || count <= 1 || limit <= 0)
        return 0;
    for (int32_t i = 1; i < count; i++) {
        if (ph3d_broadphase_compare_entries_axis(&entries[i - 1], &entries[i], axis) > 0 &&
            ++inversions >= limit)
            break;
    }
    return inversions;
}

/// @brief Merge two sorted broadphase runs from @p src into @p dst.
/// @details Used by the bottom-up stable merge sort fallback. Stable ordering preserves the
///   owner-index tie break and keeps solver warm-start behavior deterministic.
static void ph3d_broadphase_merge_run(const ph3d_broadphase_entry *src,
                                      ph3d_broadphase_entry *dst,
                                      int32_t left,
                                      int32_t mid,
                                      int32_t right,
                                      int axis) {
    int32_t i = left;
    int32_t j = mid;
    int32_t out = left;
    while (i < mid && j < right) {
        if (ph3d_broadphase_compare_entries_axis(&src[i], &src[j], axis) <= 0)
            dst[out++] = src[i++];
        else
            dst[out++] = src[j++];
    }
    while (i < mid)
        dst[out++] = src[i++];
    while (j < right)
        dst[out++] = src[j++];
}

/// @brief Deterministically sort broadphase entries along @p axis without calling libc qsort.
/// @details Uses insertion sort for small/nearly-sorted arrays and a bottom-up stable merge sort
///   for larger unordered arrays. @p scratch must hold @p count entries for the merge path; if it
///   is unavailable the function falls back to insertion sort to preserve correctness.
void ph3d_broadphase_sort_entries(ph3d_broadphase_entry *entries,
                                  ph3d_broadphase_entry *scratch,
                                  int32_t count,
                                  int axis) {
    const int32_t insertion_limit = 64;
    const int32_t inversion_limit = 8;
    if (!entries || count <= 1)
        return;
    if (count <= insertion_limit ||
        ph3d_broadphase_adjacent_inversions(entries, count, axis, inversion_limit) <
            inversion_limit ||
        !scratch) {
        ph3d_broadphase_insertion_sort(entries, count, axis);
        return;
    }

    ph3d_broadphase_entry *src = entries;
    ph3d_broadphase_entry *dst = scratch;
    for (int32_t width = 1; width < count; width *= 2) {
        for (int32_t left = 0; left < count; left += width * 2) {
            int32_t mid = left + width;
            int32_t right = left + width * 2;
            if (mid > count)
                mid = count;
            if (right > count)
                right = count;
            ph3d_broadphase_merge_run(src, dst, left, mid, right, axis);
        }
        ph3d_broadphase_entry *tmp = src;
        src = dst;
        dst = tmp;
    }
    if (src != entries)
        memcpy(entries, src, (size_t)count * sizeof(*entries));
}

/// @brief Return 1 if two 3D AABBs overlap, 0 if separated on any axis.
/// @details Separating-axis test on all three axes simultaneously.  Used in the
///   broad phase after the X-axis early-out to confirm overlap on Y and Z.
static int ph3d_bounds_overlap(const double *a_min,
                               const double *a_max,
                               const double *b_min,
                               const double *b_max) {
    return a_min[0] <= b_max[0] && a_max[0] >= b_min[0] && a_min[1] <= b_max[1] &&
           a_max[1] >= b_min[1] && a_min[2] <= b_max[2] && a_max[2] >= b_min[2];
}

/// @brief Run narrow-phase collision detection for one body pair (no resolution).
/// @details Skips static-static pairs or pairs that fail the bidirectional
///   layer/mask filter.  On a detected overlap, appends a new rt_contact3d to the
///   world's contact array (reallocating if needed) with its full manifold and a
///   zeroed warm-start state; the warm-started sequential-impulse solver resolves
///   the assembled contact list afterwards. Records the pre-solve approach speed
///   and updates is_grounded on whichever body has a contact normal pointing more
///   than ~45° upward (|normal.y| > 0.7).
/// @param w  World containing the bodies.
/// @param a  First body of the candidate pair.
/// @param b  Second body of the candidate pair.
/// @return   1 on success (including skipped pairs), 0 if the contact array
///           could not be reallocated.
static int world3d_process_collision_pair(rt_world3d *w, rt_body3d *a, rt_body3d *b) {
    double normal[3], depth, point[3];
    double relative_speed = 0.0;
    void *leaf_a = NULL;
    void *leaf_b = NULL;
    rt_collider_pose leaf_a_pose;
    rt_collider_pose leaf_b_pose;

    if (!w || !a || !b || a == b)
        return 1;
    if (a->motion_mode == PH3D_MODE_STATIC && b->motion_mode == PH3D_MODE_STATIC)
        return 1;
    if (!(a->collision_layer & b->collision_mask))
        return 1;
    if (!(b->collision_layer & a->collision_mask))
        return 1;
    if (!test_collision(a, b, normal, &depth, point, &leaf_a, &leaf_b, &leaf_a_pose, &leaf_b_pose))
        return 1;

    int32_t next_contact_count;
    if (!world3d_checked_increment(w->contact_count, &next_contact_count) ||
        !world3d_reserve_contacts(w, next_contact_count)) {
        rt_trap("Physics3D.World.Step: contact allocation failed");
        return 0;
    }

    rt_contact3d *c = &w->contacts[w->contact_count++];
    {
        rt_contact3d zero = {0};
        *c = zero;
    }
    c->body_a = a;
    c->body_b = b;
    c->collider_a = leaf_a ? leaf_a : a->collider;
    c->collider_b = leaf_b ? leaf_b : b->collider;
    c->point[0] = point[0];
    c->point[1] = point[1];
    c->point[2] = point[2];
    c->normal[0] = normal[0];
    c->normal[1] = normal[1];
    c->normal[2] = normal[2];
    c->separation = -depth;
    contact3d_init_single_point(c, point, normal, c->separation);
    contact3d_expand_aabb_manifold(c, a, b);
    if (c->contact_count <= 1)
        contact3d_expand_obb_manifold(c, leaf_a, &leaf_a_pose, leaf_b, &leaf_b_pose);
    c->is_trigger = (a->is_trigger || b->is_trigger) ? 1 : 0;
    /* Detection only: the warm-started sequential-impulse solver runs after the
     * whole manifold is built. Record the
     * pre-solve approach speed for collision-event payloads and reset the
     * per-point accumulated impulses; warm starting seeds them next step. */
    relative_speed = fabs((b->velocity[0] - a->velocity[0]) * normal[0] +
                          (b->velocity[1] - a->velocity[1]) * normal[1] +
                          (b->velocity[2] - a->velocity[2]) * normal[2]);
    c->relative_speed = relative_speed;
    c->normal_impulse = 0.0;
    memset(c->normal_impulse_acc, 0, sizeof(c->normal_impulse_acc));
    memset(c->tangent_impulse_acc, 0, sizeof(c->tangent_impulse_acc));
    memset(c->restitution_bias, 0, sizeof(c->restitution_bias));

    if (normal[1] > 0.7) {
        b->is_grounded = 1;
        b->ground_normal[0] = normal[0];
        b->ground_normal[1] = normal[1];
        b->ground_normal[2] = normal[2];
    } else if (normal[1] < -0.7) {
        a->is_grounded = 1;
        a->ground_normal[0] = -normal[0];
        a->ground_normal[1] = -normal[1];
        a->ground_normal[2] = -normal[2];
    }
    return 1;
}

/// @brief Cheap broad-phase rejection test for a body pair before narrow-phase: rejects
///   self-pairs, static-vs-static pairs, and pairs failing the bidirectional layer/mask
///   filter. Returns 1 only if the pair could plausibly collide.
static int world3d_pair_can_collide_cheap(const rt_body3d *a, const rt_body3d *b) {
    if (!a || !b || a == b)
        return 0;
    if (a->motion_mode == PH3D_MODE_STATIC && b->motion_mode == PH3D_MODE_STATIC)
        return 0;
    if (!(a->collision_layer & b->collision_mask))
        return 0;
    if (!(b->collision_layer & a->collision_mask))
        return 0;
    return 1;
}

/// @brief Count bodies that actually contribute a broadphase AABB this step.
/// @details Capacity reservations use this count instead of the raw body array size so worlds with
///   many placeholder bodies do not over-reserve broadphase scratch.
static int32_t world3d_count_broadphase_bodies(const rt_world3d *w) {
    int32_t count = 0;
    if (!w)
        return 0;
    for (int32_t i = 0; i < w->body_count; i++) {
        if (body3d_has_collision_geometry(w->bodies[i]))
            count++;
    }
    return count;
}

/// @brief Fill @p entries with current body AABBs for every collision-capable body in @p w.
/// @details The caller guarantees enough capacity. Separating this from allocation lets the step
///   path use persistent world scratch or a bounded stack fallback without duplicating AABB logic.
static int32_t world3d_fill_broadphase_entries(rt_world3d *w, ph3d_broadphase_entry *entries) {
    int32_t entry_count = 0;
    if (!w || !entries)
        return 0;
    for (int32_t i = 0; i < w->body_count; i++) {
        rt_body3d *body = w->bodies[i];
        if (!body3d_has_collision_geometry(body))
            continue;
        entries[entry_count].body = body;
        body_aabb(body, entries[entry_count].min, entries[entry_count].max);
        entry_count++;
    }
    return entry_count;
}

/// @brief Run the full broad-phase + narrow-phase collision pass for one world step.
/// @details Clears the contact list from the previous step, then:
///   1. Attempts to allocate/reuse broadphase_entries and sort scratch for sweep-and-prune.
///   2. If allocation fails for a small world, uses bounded stack scratch; large worlds fail
///      cleanly instead of silently entering an O(n²) collision crawl.
///   3. On success: computes each body's AABB, sorts entries by the widest axis (SAP), and
///      only tests pairs whose sweep-axis intervals overlap, short-circuiting the inner loop
///      when the selected-axis gap guarantees no overlap.
///   Each surviving pair is processed by world3d_process_collision_pair, which
///   records the contact and its manifold. Resolution happens afterwards in the
///   warm-started sequential-impulse solve, not here.
/// @param w  World to process.
/// @return   1 on success, 0 if any contact could not be allocated.
int world3d_detect_contacts(rt_world3d *w) {
    if (!w)
        return 0;
    w->contact_count = 0;
    if (w->body_count <= 1)
        return 1;

    ph3d_broadphase_entry stack_entries[PH3D_BROADPHASE_STACK_FALLBACK];
    ph3d_broadphase_entry stack_scratch[PH3D_BROADPHASE_STACK_FALLBACK];
    int32_t geometry_count = world3d_count_broadphase_bodies(w);
    ph3d_broadphase_entry *entries = NULL;
    ph3d_broadphase_entry *sort_scratch = NULL;
    if (geometry_count <= 1)
        return 1;
    if (!world3d_reserve_broadphase_capacity(w, geometry_count) ||
        !world3d_reserve_broadphase_sort_scratch(w, geometry_count)) {
        w->broadphase_fallback_count++;
        rt_game3d_diag_record_broadphase_fallback();
        if (geometry_count > PH3D_BROADPHASE_STACK_FALLBACK) {
            rt_trap("Physics3D.World.Step: broadphase allocation failed");
            return 0;
        }
        entries = stack_entries;
        sort_scratch = stack_scratch;
    } else {
        entries = w->broadphase_entries;
        sort_scratch = w->broadphase_sort_scratch;
    }

    int32_t entry_count = world3d_fill_broadphase_entries(w, entries);
    /* Sweep on the axis with the greatest spread of entry centres so the interval
     * early-out (the inner `break`) prunes the most pairs. A fixed X sweep degrades
     * toward O(n²) when many bodies share an X-span (e.g. a wide flat floor of stacked
     * boxes); picking the widest axis keeps the common 1D-dominant layouts near-linear.
     * The choice is derived from positions only, so it stays deterministic across the
     * VM and native backends. (A uniform spatial hash would also help 2D-dense grids —
     * a worthwhile future extension that is intentionally out of scope here.) */
    int sweep_axis = 0;
    if (entry_count > 1) {
        double lo[3];
        double hi[3];
        for (int k = 0; k < 3; k++)
            lo[k] = hi[k] = 0.5 * (entries[0].min[k] + entries[0].max[k]);
        for (int32_t i = 1; i < entry_count; i++) {
            for (int k = 0; k < 3; k++) {
                double c = 0.5 * (entries[i].min[k] + entries[i].max[k]);
                if (c < lo[k])
                    lo[k] = c;
                if (c > hi[k])
                    hi[k] = c;
            }
        }
        double best_spread = hi[0] - lo[0];
        if (hi[1] - lo[1] > best_spread) {
            best_spread = hi[1] - lo[1];
            sweep_axis = 1;
        }
        if (hi[2] - lo[2] > best_spread) {
            best_spread = hi[2] - lo[2];
            sweep_axis = 2;
        }
    }
    ph3d_broadphase_sort_entries(entries, sort_scratch, entry_count, sweep_axis);

    for (int32_t i = 0; i < entry_count; i++) {
        for (int32_t j = i + 1; j < entry_count; j++) {
            if (entries[j].min[sweep_axis] > entries[i].max[sweep_axis])
                break;
            if (!ph3d_bounds_overlap(
                    entries[i].min, entries[i].max, entries[j].min, entries[j].max))
                continue;
            if (!world3d_pair_can_collide_cheap(entries[i].body, entries[j].body))
                continue;
            if (!world3d_process_collision_pair(w, entries[i].body, entries[j].body)) {
                return 0;
            }
        }
    }
    return 1;
}

#else
typedef int rt_physics3d_solver_disabled_tu_guard;
#endif /* VIPER_ENABLE_GRAPHICS */
