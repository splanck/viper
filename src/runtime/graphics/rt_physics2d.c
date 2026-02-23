//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_physics2d.c
// Purpose: Simple 2D rigid-body physics engine with AABB collision detection
//   and impulse-based collision response. Designed for game use cases: enemies,
//   platforms, bullets, and other simple rectangular entities. Intentionally
//   not a general-purpose physics engine — correctness and simplicity are
//   favoured over feature completeness.
//
// Key invariants:
//   - All bodies are axis-aligned bounding boxes (AABB). No rotational physics.
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

#include "rt_internal.h"
#include "rt_object.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/// Maximum number of rigid bodies a single world can contain.
/// Exceeding this limit causes rt_trap() to fire with a descriptive message.
/// To increase the limit, edit this constant and recompile.
#define PH_MAX_BODIES 256

/// Stringified version of PH_MAX_BODIES for use in rt_trap() messages without
/// requiring runtime sprintf. Keep this in sync with PH_MAX_BODIES.
#define RT_PH_MAX_BODIES_STR "256"

//=============================================================================
// Internal types
//=============================================================================

/// @brief Internal representation of a single rigid body.
///
/// Bodies are axis-aligned bounding boxes (AABBs). Position (x, y) is the
/// top-left corner of the bounding box. Velocity (vx, vy) is in world-units
/// per second. Force (fx, fy) is accumulated each frame via apply_force() and
/// cleared after every integration step.
///
/// The inv_mass field stores the reciprocal of mass for efficiency: static
/// bodies have mass == 0 and inv_mass == 0, so multiplication by inv_mass
/// produces zero without any branching in the integrator or impulse solver.
///
/// The vptr field is reserved for Zia's virtual-dispatch table pointer
/// (similar to a C++ vtable pointer). It must be the first member so the
/// struct layout matches the Zia object model.
typedef struct
{
    void   *vptr;          ///< Zia virtual-dispatch pointer (must be first).
    double  x, y;          ///< Top-left position in world coordinates.
    double  w, h;          ///< Width and height of the AABB.
    double  vx, vy;        ///< Velocity in world-units per second.
    double  fx, fy;        ///< Accumulated force for the current frame (zeroed after integration).
    double  mass;          ///< Mass in arbitrary units. 0 = static (immovable).
    double  inv_mass;      ///< Reciprocal of mass (1/mass), or 0 for static bodies.
    double  restitution;   ///< Bounciness coefficient in [0, 1]. 0 = inelastic, 1 = perfectly elastic.
    double  friction;      ///< Kinetic friction coefficient in [0, 1]. Applied along contact tangent.
    int64_t collision_layer; ///< Bitmask: which physical layer(s) this body occupies (default: 1).
    int64_t collision_mask;  ///< Bitmask: which layers this body can collide with (default: 0xFFFFFFFF, all layers).
} rt_body_impl;

/// @brief Internal representation of a physics world.
///
/// The world owns a fixed-capacity array of body pointers. Each body is
/// reference-counted; the world retains a reference when a body is added and
/// releases it when the body is removed or the world is finalised.
///
/// Gravity is applied uniformly to all dynamic bodies every integration step.
/// Gravity is specified in world-units per second squared.
typedef struct
{
    void         *vptr;                      ///< Zia virtual-dispatch pointer (must be first).
    double        gravity_x;                 ///< Horizontal gravity (world-units/s²). Usually 0.
    double        gravity_y;                 ///< Vertical gravity (world-units/s²). Positive = downward in screen space.
    rt_body_impl *bodies[PH_MAX_BODIES];     ///< Flat array of retained body pointers.
    int64_t       body_count;                ///< Number of bodies currently in the world.
} rt_world_impl;

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
static int8_t aabb_overlap(rt_body_impl *a, rt_body_impl *b, double *nx, double *ny, double *pen)
{
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
    if (ox < oy)
    {
        *pen = ox;
        *ny = 0.0;
        *nx = ((a->x + a->w * 0.5) < (b->x + b->w * 0.5)) ? 1.0 : -1.0;
    }
    else
    {
        *pen = oy;
        *nx = 0.0;
        *ny = ((a->y + a->h * 0.5) < (b->y + b->h * 0.5)) ? 1.0 : -1.0;
    }
    return 1;
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
static void resolve_collision(rt_body_impl *a, rt_body_impl *b, double nx, double ny, double pen)
{
    double rvx, rvy, vel_along_n, e, j, total_inv, correction;

    /* Both static — neither body can move, skip entirely */
    if (a->inv_mass == 0.0 && b->inv_mass == 0.0)
        return;

    /* Relative velocity of B w.r.t. A along all axes */
    rvx = b->vx - a->vx;
    rvy = b->vy - a->vy;

    /* Project relative velocity onto the contact normal */
    vel_along_n = rvx * nx + rvy * ny;

    /* If bodies are separating (positive projection) do nothing — applying an
     * impulse to separating bodies would pull them back together */
    if (vel_along_n > 0.0)
        return;

    /* Use the less elastic material's coefficient so a rubber ball bouncing on
     * concrete uses the concrete's zero restitution, not the ball's high one */
    e = a->restitution < b->restitution ? a->restitution : b->restitution;

    /* Scalar impulse magnitude. Derivation: we want the post-collision relative
     * velocity along n to equal -e * vel_along_n (restitution). Solving for j
     * gives: j = -(1+e)*vel_along_n / (1/mA + 1/mB) */
    total_inv = a->inv_mass + b->inv_mass;
    j = -(1.0 + e) * vel_along_n / total_inv;

    /* Apply the normal impulse to each body proportional to its inverse mass */
    a->vx -= j * a->inv_mass * nx;
    a->vy -= j * a->inv_mass * ny;
    b->vx += j * b->inv_mass * nx;
    b->vy += j * b->inv_mass * ny;

    /* Friction impulse: computed in the tangent direction (perpendicular to n).
     * Clamped to Coulomb's law (|jt| <= mu * |j|) to prevent friction from
     * exceeding the normal force. */
    {
        double tx = rvx - vel_along_n * nx;
        double ty = rvy - vel_along_n * ny;
        double t_len = sqrt(tx * tx + ty * ty);
        if (t_len > 1e-9)
        {
            double mu, jt, vel_along_t;
            tx /= t_len;  /* Normalise tangent */
            ty /= t_len;
            vel_along_t = rvx * tx + rvy * ty;
            mu = (a->friction + b->friction) * 0.5;  /* Average both surfaces */
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
static void world_finalizer(void *obj)
{
    rt_world_impl *w = (rt_world_impl *)obj;
    if (w)
    {
        int64_t i;
        for (i = 0; i < w->body_count; i++)
        {
            if (w->bodies[i])
                rt_obj_release_check0(w->bodies[i]);
        }
        w->body_count = 0;
    }
}

//=============================================================================
// Public API — World
//=============================================================================

void *rt_physics2d_world_new(double gravity_x, double gravity_y)
{
    rt_world_impl *w = (rt_world_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_world_impl));
    if (!w)
    {
        rt_trap("Physics2D.World: allocation failed");
        return NULL;
    }
    w->vptr = NULL;
    w->gravity_x = gravity_x;
    w->gravity_y = gravity_y;
    w->body_count = 0;
    memset(w->bodies, 0, sizeof(w->bodies));
    rt_obj_set_finalizer(w, world_finalizer);
    return w;
}

void rt_physics2d_world_step(void *obj, double dt)
{
    rt_world_impl *w;
    int64_t i;
    if (!obj || dt <= 0.0)
        return;
    w = (rt_world_impl *)obj;

    /* Step 1: Apply accumulated forces and gravity to each dynamic body's
     * velocity (symplectic Euler, force→velocity half-step).
     * Forces are cleared here so Apply Force calls accumulate cleanly across
     * multiple Step() calls within the same frame if the caller uses sub-steps. */
    for (i = 0; i < w->body_count; i++)
    {
        rt_body_impl *b = w->bodies[i];
        if (!b || b->inv_mass == 0.0)
            continue;   /* Skip static bodies */
        b->vx += (b->fx * b->inv_mass + w->gravity_x) * dt;
        b->vy += (b->fy * b->inv_mass + w->gravity_y) * dt;
        b->fx = 0.0;
        b->fy = 0.0;
    }

    /* Step 2: Integrate velocity → position for each dynamic body.
     * Done in a separate pass from Step 1 so all velocity changes from forces
     * are committed before any position updates occur. */
    for (i = 0; i < w->body_count; i++)
    {
        rt_body_impl *b = w->bodies[i];
        if (!b || b->inv_mass == 0.0)
            continue;
        b->x += b->vx * dt;
        b->y += b->vy * dt;
    }

    /* Step 3: Broad-phase + narrow-phase collision detection and resolution.
     *
     * Broad phase: uniform 8×8 grid. The grid is recomputed from scratch each
     * step. The world AABB is computed first, then divided into BPG_DIM×BPG_DIM
     * cells. Each body is registered in every cell its AABB overlaps.
     *
     * All grid arrays are stack-local, making this function safe to call on
     * concurrent worlds from separate threads with no data sharing.
     *
     * The grid intentionally stores uint8_t body indices (not pointers) to
     * keep each cell small. BPG_CELL_MAX caps the count per cell; a body is
     * silently dropped from a cell if it overflows — which only affects
     * broad-phase pairing efficiency, not correctness in normal scenes.
     *
     * Narrow phase: for each pair of bodies that share a grid cell, test with
     * aabb_overlap() and call resolve_collision() if they overlap.
     *
     * De-duplication: a 256×256 bit-matrix (pair_checked) ensures each pair
     * (i, j) is resolved at most once per step, even when the two bodies share
     * multiple grid cells (e.g., near a cell boundary). Bit (i,j) is stored at
     * byte [i*PH_MAX_BODIES+j >> 3], bit [(i*PH_MAX_BODIES+j) & 7]. The matrix
     * is stack-local: (256×256) / 8 = 8192 bytes ≈ 8 KB. */

#define BPG_DIM      8   /* Broad-phase grid cells per axis (8×8 = 64 total) */
#define BPG_CELL_MAX 32  /* Maximum body indices stored per grid cell */

    if (w->body_count >= 2)
    {
        /* --- Step 3a: Compute the world AABB that tightly encloses all bodies --- */
        double wx0 = 1e18, wy0 = 1e18, wx1 = -1e18, wy1 = -1e18;
        for (i = 0; i < w->body_count; i++)
        {
            rt_body_impl *b = w->bodies[i];
            if (!b) continue;
            if (b->x        < wx0) wx0 = b->x;
            if (b->y        < wy0) wy0 = b->y;
            if (b->x + b->w > wx1) wx1 = b->x + b->w;
            if (b->y + b->h > wy1) wy1 = b->y + b->h;
        }
        /* Guard: ensure minimum cell size of 1 so division below never divides
         * by zero (can happen when all bodies occupy the exact same point). */
        if (wx1 <= wx0) wx1 = wx0 + 1.0;
        if (wy1 <= wy0) wy1 = wy0 + 1.0;
        double cell_w = (wx1 - wx0) / BPG_DIM;
        double cell_h = (wy1 - wy0) / BPG_DIM;

        /* --- Step 3b: Populate the broad-phase grid (stack-local) ---
         * Each body is inserted into every cell its AABB touches. A body that
         * straddles a cell boundary appears in both cells so it will be paired
         * with neighbours on either side. */
        uint8_t grid_bodies[BPG_DIM * BPG_DIM][BPG_CELL_MAX];
        int     grid_count [BPG_DIM * BPG_DIM];
        memset(grid_count, 0, sizeof(grid_count));

        for (i = 0; i < w->body_count; i++)
        {
            rt_body_impl *b = w->bodies[i];
            if (!b) continue;
            int cx0 = (int)((b->x        - wx0) / cell_w); if (cx0 < 0) cx0 = 0; if (cx0 >= BPG_DIM) cx0 = BPG_DIM - 1;
            int cy0 = (int)((b->y        - wy0) / cell_h); if (cy0 < 0) cy0 = 0; if (cy0 >= BPG_DIM) cy0 = BPG_DIM - 1;
            int cx1 = (int)((b->x + b->w - wx0) / cell_w); if (cx1 < 0) cx1 = 0; if (cx1 >= BPG_DIM) cx1 = BPG_DIM - 1;
            int cy1 = (int)((b->y + b->h - wy0) / cell_h); if (cy1 < 0) cy1 = 0; if (cy1 >= BPG_DIM) cy1 = BPG_DIM - 1;
            for (int cy = cy0; cy <= cy1; cy++)
            {
                for (int cx = cx0; cx <= cx1; cx++)
                {
                    int cell = cy * BPG_DIM + cx;
                    int cnt = grid_count[cell];
                    if (cnt < BPG_CELL_MAX)
                    {
                        grid_bodies[cell][cnt] = (uint8_t)i;
                        grid_count[cell] = cnt + 1;
                    }
                    /* If cnt >= BPG_CELL_MAX the body is silently dropped from
                     * this cell. It may still be paired via an adjacent cell. */
                }
            }
        }

        /* --- Step 3c: Narrow phase — test each cell's candidate pairs ---
         * pair_checked is a bit-matrix preventing duplicate pair resolution.
         * Pairs are always stored with the lower index first (ii < jj) so the
         * bit position is deterministic regardless of cell iteration order. */
        uint8_t pair_checked[PH_MAX_BODIES * PH_MAX_BODIES / 8 + 1];
        memset(pair_checked, 0, sizeof(pair_checked));

        for (int cell = 0; cell < BPG_DIM * BPG_DIM; cell++)
        {
            int cnt = grid_count[cell];
            for (int a = 0; a < cnt; a++)
            {
                for (int b_idx = a + 1; b_idx < cnt; b_idx++)
                {
                    int ii = (int)grid_bodies[cell][a];
                    int jj = (int)grid_bodies[cell][b_idx];
                    /* Normalise order so ii < jj for bit-matrix lookup */
                    if (ii > jj) { int tmp = ii; ii = jj; jj = tmp; }
                    /* Check the bit-matrix: skip this pair if already resolved */
                    int bit = ii * PH_MAX_BODIES + jj;
                    if (pair_checked[bit >> 3] & (uint8_t)(1u << (bit & 7)))
                        continue;
                    pair_checked[bit >> 3] |= (uint8_t)(1u << (bit & 7));

                    double nx, ny, pen;
                    rt_body_impl *bi = w->bodies[ii];
                    rt_body_impl *bj = w->bodies[jj];
                    if (!bi || !bj) continue;

                    /* Bidirectional collision filter: both bodies must be on
                     * layers the other can collide with. This allows one-sided
                     * triggers (A sees B, but B ignores A). */
                    if (!((bi->collision_layer & bj->collision_mask) &&
                          (bj->collision_layer & bi->collision_mask))) continue;

                    if (aabb_overlap(bi, bj, &nx, &ny, &pen))
                        resolve_collision(bi, bj, nx, ny, pen);
                }
            }
        }
    }

#undef BPG_DIM
#undef BPG_CELL_MAX
}

void rt_physics2d_world_add(void *obj, void *body)
{
    rt_world_impl *w;
    if (!obj || !body)
        return;
    w = (rt_world_impl *)obj;
    if (w->body_count >= PH_MAX_BODIES)
    {
        rt_trap("Physics2D.World.Add: body limit exceeded (max " RT_PH_MAX_BODIES_STR
                "); increase PH_MAX_BODIES and recompile");
        return;
    }
    rt_obj_retain_maybe(body);
    w->bodies[w->body_count++] = (rt_body_impl *)body;
}

void rt_physics2d_world_remove(void *obj, void *body)
{
    rt_world_impl *w;
    int64_t i;
    if (!obj || !body)
        return;
    w = (rt_world_impl *)obj;
    for (i = 0; i < w->body_count; i++)
    {
        if (w->bodies[i] == (rt_body_impl *)body)
        {
            rt_obj_release_check0(w->bodies[i]);
            /* Swap with tail to maintain a compact, order-independent array */
            w->bodies[i] = w->bodies[w->body_count - 1];
            w->bodies[w->body_count - 1] = NULL;
            w->body_count--;
            return;
        }
    }
}

int64_t rt_physics2d_world_body_count(void *obj)
{
    if (!obj)
        return 0;
    return ((rt_world_impl *)obj)->body_count;
}

void rt_physics2d_world_set_gravity(void *obj, double gx, double gy)
{
    if (!obj)
        return;
    ((rt_world_impl *)obj)->gravity_x = gx;
    ((rt_world_impl *)obj)->gravity_y = gy;
}

//=============================================================================
// Public API — Body
//=============================================================================

void *rt_physics2d_body_new(double x, double y, double w, double h, double mass)
{
    rt_body_impl *b = (rt_body_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_body_impl));
    if (!b)
    {
        rt_trap("Physics2D.Body: allocation failed");
        return NULL;
    }
    b->vptr = NULL;
    b->x = x;
    b->y = y;
    b->w = w;
    b->h = h;
    b->vx = 0.0;
    b->vy = 0.0;
    b->fx = 0.0;
    b->fy = 0.0;
    b->mass = mass;
    b->inv_mass = (mass > 0.0) ? (1.0 / mass) : 0.0;
    b->restitution = 0.5;        /* Moderately bouncy by default */
    b->friction    = 0.3;        /* Moderate friction by default */
    b->collision_layer = 1;          /* Default: layer 0, bit 0 set */
    b->collision_mask  = 0xFFFFFFFF; /* Default: collide with all 32 layers */
    return b;
}

double rt_physics2d_body_x(void *obj)
{
    return obj ? ((rt_body_impl *)obj)->x : 0.0;
}

double rt_physics2d_body_y(void *obj)
{
    return obj ? ((rt_body_impl *)obj)->y : 0.0;
}

double rt_physics2d_body_w(void *obj)
{
    return obj ? ((rt_body_impl *)obj)->w : 0.0;
}

double rt_physics2d_body_h(void *obj)
{
    return obj ? ((rt_body_impl *)obj)->h : 0.0;
}

double rt_physics2d_body_vx(void *obj)
{
    return obj ? ((rt_body_impl *)obj)->vx : 0.0;
}

double rt_physics2d_body_vy(void *obj)
{
    return obj ? ((rt_body_impl *)obj)->vy : 0.0;
}

void rt_physics2d_body_set_pos(void *obj, double x, double y)
{
    if (!obj)
        return;
    ((rt_body_impl *)obj)->x = x;
    ((rt_body_impl *)obj)->y = y;
}

void rt_physics2d_body_set_vel(void *obj, double vx, double vy)
{
    if (!obj)
        return;
    ((rt_body_impl *)obj)->vx = vx;
    ((rt_body_impl *)obj)->vy = vy;
}

void rt_physics2d_body_apply_force(void *obj, double fx, double fy)
{
    if (!obj)
        return;
    /* Forces accumulate until the next Step(); they are additive so multiple
     * ApplyForce calls in the same frame combine correctly. */
    ((rt_body_impl *)obj)->fx += fx;
    ((rt_body_impl *)obj)->fy += fy;
}

void rt_physics2d_body_apply_impulse(void *obj, double ix, double iy)
{
    rt_body_impl *b;
    if (!obj)
        return;
    b = (rt_body_impl *)obj;
    if (b->inv_mass == 0.0)
        return;   /* Static bodies cannot be moved by impulses */
    /* An impulse is an instantaneous velocity change: Δv = impulse / mass,
     * equivalently: Δv = impulse * inv_mass. */
    b->vx += ix * b->inv_mass;
    b->vy += iy * b->inv_mass;
}

double rt_physics2d_body_restitution(void *obj)
{
    return obj ? ((rt_body_impl *)obj)->restitution : 0.0;
}

void rt_physics2d_body_set_restitution(void *obj, double r)
{
    if (obj)
        ((rt_body_impl *)obj)->restitution = r;
}

double rt_physics2d_body_friction(void *obj)
{
    return obj ? ((rt_body_impl *)obj)->friction : 0.0;
}

void rt_physics2d_body_set_friction(void *obj, double f)
{
    if (obj)
        ((rt_body_impl *)obj)->friction = f;
}

int8_t rt_physics2d_body_is_static(void *obj)
{
    /* A body is static when its inverse-mass is zero (mass == 0 at creation) */
    return (obj && ((rt_body_impl *)obj)->inv_mass == 0.0) ? 1 : 0;
}

double rt_physics2d_body_mass(void *obj)
{
    return obj ? ((rt_body_impl *)obj)->mass : 0.0;
}

int64_t rt_physics2d_body_collision_layer(void *obj)
{
    return obj ? ((rt_body_impl *)obj)->collision_layer : 0;
}

void rt_physics2d_body_set_collision_layer(void *obj, int64_t layer)
{
    if (obj)
        ((rt_body_impl *)obj)->collision_layer = layer;
}

int64_t rt_physics2d_body_collision_mask(void *obj)
{
    return obj ? ((rt_body_impl *)obj)->collision_mask : 0;
}

void rt_physics2d_body_set_collision_mask(void *obj, int64_t mask)
{
    if (obj)
        ((rt_body_impl *)obj)->collision_mask = mask;
}
