//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/world/rt_particles3d.c
// Purpose: 3D particle system — emitter spawning, Euler integration physics,
//   lifetime/size/color interpolation, camera-facing billboard quads,
//   and blend-mode-aware billboard rendering.
//
// Key invariants:
//   - Dead particles are swapped to end (unstable removal, O(1) per kill).
//   - Billboard quads use camera right/up vectors from the view matrix.
//   - Each Draw fills a reusable per-frame vertex+index slot for all live particles.
//   - Additive mode submits one batched draw; alpha mode sorts the batched quads
//     back-to-front before upload so transparent draw order is deterministic.
//   - xorshift32 PRNG seeded from a local monotonic counter for deterministic
//     randomization without object-address-derived seeds (no stdlib rand).
//   - Draw materials are slotted per frame so queued commands are not mutated by later draws.
//
// Ownership/Lifetime:
//   - Particles3D is GC-managed; finalizer frees the particle pool and drops
//     refs on texture and cached material.
//   - Overflow draw slots fall back to canvas-owned temp buffers freed at end-of-frame.
//
// Links: rt_particles3d.h, plans/3d/17-particle-system.md
//
//===----------------------------------------------------------------------===//

#ifdef VIPER_ENABLE_GRAPHICS

#include "rt_particles3d.h"
#include "rt_canvas3d.h"
#include "rt_canvas3d_internal.h"
#include "rt_pixels.h"
#include "rt_pixels_internal.h"
#include "rt_platform.h"

#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#define PARTICLES3D_WORLD_ABS_MAX 1000000000000.0
#define PARTICLES3D_PARAM_MAX 1000000.0
#define PARTICLES3D_DRAW_SLOT_COUNT 4
#define PARTICLES3D_DT_MAX 1.0

extern void *rt_obj_new_i64(int64_t class_id, int64_t byte_size);
extern void rt_obj_set_finalizer(void *obj, void (*fn)(void *));
extern void rt_obj_retain_maybe(void *obj);
extern int rt_obj_release_check0(void *obj);
extern void rt_obj_free(void *obj);
#include "rt_trap.h"
extern int rt_canvas3d_add_temp_buffer(void *canvas, void *buffer);

/*==========================================================================
 * Internal types
 *=========================================================================*/

typedef struct {
    double pos[3];
    double vel[3];
    float color[4]; /* current RGBA */
    float size;
    float life;
    float max_life;
} vgfx3d_particle_t;

typedef struct {
    void *vptr;
    vgfx3d_particle_t *particles;
    int32_t count;
    int32_t max_particles;

    double position[3];
    double emit_dir[3];
    double emit_spread;
    double speed_min, speed_max;
    double life_min, life_max;
    double size_start, size_end;
    double gravity[3];
    float color_start[3], color_end[3]; /* RGB [0,1] */
    double alpha_start, alpha_end;
    double rate;
    double accumulator;

    int8_t emitting;
    int8_t additive_blend;
    double softness; /* Plan 10: soft-particle fade distance in world units (0 = off) */
    void *texture;
    int32_t emitter_shape; /* 0=point, 1=sphere, 2=box */
    double emitter_size[3];
    uint32_t prng_state;   /* per-instance PRNG seed */
    void *cached_material; /* reused across frames (GFX-052) */
    vgfx3d_vertex_t *draw_vertices[PARTICLES3D_DRAW_SLOT_COUNT];
    uint32_t *draw_indices[PARTICLES3D_DRAW_SLOT_COUNT];
    uint32_t draw_vertex_capacity[PARTICLES3D_DRAW_SLOT_COUNT];
    uint32_t draw_index_capacity[PARTICLES3D_DRAW_SLOT_COUNT];
    void *draw_materials[PARTICLES3D_DRAW_SLOT_COUNT];
    void *sort_keys;
    int32_t sort_key_capacity;
    uint64_t sort_key_grow_count;
    int64_t draw_frame_serial;
    int32_t draw_slots_used;
} rt_particles3d;

/// @brief Generate a non-zero per-instance seed for Particles3D.
/// @details Uses a process-local monotonic counter mixed with an odd constant
///          instead of deriving seeds from object addresses. The helper is
///          self-contained so graphics contract tests that compile this source
///          without the full runtime RNG still link cleanly.
/// @return Non-zero xorshift32 seed.
static uint32_t particles3d_next_seed(void) {
    static int64_t counter = INT64_C(0xA341316C);
    int64_t old = __atomic_fetch_add(&counter, INT64_C(0x9E3779B9), __ATOMIC_RELAXED);
    uint32_t seed = (uint32_t)old ^ 0x12345678u;
    return seed ? seed : 0xA341316Cu;
}

/// @brief Validate @p obj as a Particles3D handle and return its typed pointer (NULL on mismatch).
static rt_particles3d *particles3d_checked(void *obj) {
    return (rt_particles3d *)rt_g3d_checked_or_null(obj, RT_G3D_PARTICLES3D_CLASS_ID);
}

/// @brief Drop one retained object ref and clear the slot.
static void particles3d_release_ref(void **slot) {
    if (!slot || !*slot)
        return;
    if (rt_obj_release_check0(*slot))
        rt_obj_free(*slot);
    *slot = NULL;
}

/// @brief Return true when @p texture is a live Pixels object.
static int particles3d_texture_valid(void *texture) {
    return rt_pixels_checked_impl_or_null(texture) != NULL;
}

/// @brief Release a retained Pixels slot only if it still points at Pixels.
static void particles3d_release_texture_slot(void **slot) {
    if (!slot || !*slot)
        return;
    if (!particles3d_texture_valid(*slot)) {
        *slot = NULL;
        return;
    }
    particles3d_release_ref(slot);
}

/// @brief Release a retained Material3D slot only if it still points at Material3D.
static void particles3d_release_material_slot(void **slot) {
    if (!slot || !*slot)
        return;
    if (!rt_g3d_has_class(*slot, RT_G3D_MATERIAL3D_CLASS_ID)) {
        *slot = NULL;
        return;
    }
    particles3d_release_ref(slot);
}

/// @brief Retain-then-release assignment for the particle texture slot.
static void particles3d_assign_texture_ref(void **slot, void *value) {
    if (!slot || *slot == value)
        return;
    rt_obj_retain_maybe(value);
    particles3d_release_texture_slot(slot);
    *slot = value;
}

/// @brief Clear corrupted retained refs before update/draw/finalize paths use them.
static void particles3d_repair_refs(rt_particles3d *ps) {
    if (!ps)
        return;
    if (ps->texture && !particles3d_texture_valid(ps->texture))
        particles3d_release_texture_slot(&ps->texture);
    if (ps->cached_material && !rt_g3d_has_class(ps->cached_material, RT_G3D_MATERIAL3D_CLASS_ID))
        particles3d_release_material_slot(&ps->cached_material);
    for (int i = 0; i < PARTICLES3D_DRAW_SLOT_COUNT; ++i) {
        if (ps->draw_materials[i] &&
            !rt_g3d_has_class(ps->draw_materials[i], RT_G3D_MATERIAL3D_CLASS_ID))
            particles3d_release_material_slot(&ps->draw_materials[i]);
    }
}

/*==========================================================================
 * xorshift32 PRNG (per-instance, deterministic)
 *=========================================================================*/

/// @brief George Marsaglia's xorshift32 PRNG stepped in the emitter's per-instance
/// state. Deterministic for a given seed so two runs of the same emitter replay the
/// same spawn distribution — useful for reproducible recordings and unit tests.
/// Period is 2^32 - 1; fast enough to invoke many times per particle-spawn.
static uint32_t xorshift32(rt_particles3d *ps) {
    if (!ps)
        return 0xA341316Cu;
    if (ps->prng_state == 0)
        ps->prng_state = 0xA341316Cu;
    uint32_t x = ps->prng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    ps->prng_state = x;
    return x;
}

/// @brief Random float in [0, 1).
static float randf(rt_particles3d *ps) {
    return (float)(xorshift32(ps) & 0x00FFFFFF) / (float)0x01000000;
}

/// @brief Random float in [lo, hi].
static float rand_range(rt_particles3d *ps, double lo, double hi) {
    if (!isfinite(lo) || lo < 0.0)
        lo = 0.0;
    if (!isfinite(hi) || hi < 0.0)
        hi = 0.0;
    if (lo > PARTICLES3D_PARAM_MAX)
        lo = PARTICLES3D_PARAM_MAX;
    if (hi > PARTICLES3D_PARAM_MAX)
        hi = PARTICLES3D_PARAM_MAX;
    if (hi < lo) {
        double tmp = lo;
        lo = hi;
        hi = tmp;
    }
    return (float)(lo + randf(ps) * (hi - lo));
}

/*==========================================================================
 * Helpers
 *=========================================================================*/

/// @brief Split a 24-bit packed sRGB colour (0xRRGGBB layout) into three float channels
/// normalized to `[0, 1]`. Used to translate user-facing `int64` colour params into the
/// float vector format the mixer/renderer expects. Alpha is not part of the packed
/// format — callers manage alpha separately.
static void unpack_color(int64_t packed, float *rgb) {
    rgb[0] = (float)((packed >> 16) & 0xFF) / 255.0f;
    rgb[1] = (float)((packed >> 8) & 0xFF) / 255.0f;
    rgb[2] = (float)(packed & 0xFF) / 255.0f;
}

/// @brief Return `value` if it is finite, otherwise return `fallback`.
/// @details Used by all particle setters for position, gravity, and direction
///   components to silently absorb NaN/Inf from user code without trapping.
static double particles_finite_or(double value, double fallback) {
    return isfinite(value) ? value : fallback;
}

/// @brief Clamp `value` into `[-max_abs, max_abs]`, substituting `fallback` when not finite.
static double particles_clamp_abs_or(double value, double fallback, double max_abs) {
    value = particles_finite_or(value, fallback);
    if (value > max_abs)
        return max_abs;
    if (value < -max_abs)
        return -max_abs;
    return value;
}

/// @brief Clamp a parameter to [0, +inf), converting NaN/Inf and negatives to 0.
/// @details Applied to speeds, rates, sizes, and lifetimes that have no meaningful
///   negative value — negative inputs are treated as zero rather than as an error
///   so callers can pass unchecked user-supplied values.
static double particles_nonnegative_or_zero(double value) {
    if (!isfinite(value) || value < 0.0)
        return 0.0;
    if (value > PARTICLES3D_PARAM_MAX)
        return PARTICLES3D_PARAM_MAX;
    return value;
}

/// @brief Clamp `value` to [lo, hi], converting NaN/Inf to `lo`.
/// @details Used for bounded parameters like spread and alpha where the
///   valid range is bounded on both ends and NaN must be handled gracefully.
static double particles_clamp(double value, double lo, double hi) {
    if (!isfinite(value))
        return lo;
    if (value < lo)
        return lo;
    if (value > hi)
        return hi;
    return value;
}

/// @brief Normalize a mutable 3-vector in place. NULL-safe and epsilon-guarded — vectors
/// with length below 1e-8f are left untouched so accumulated "tiny but non-zero" drift
/// doesn't produce huge normalized outputs. Used by spawn-direction computation on cone,
/// sphere, and disc emitters.
static void normalize3(float *x, float *y, float *z) {
    float max_component;
    if (!x || !y || !z)
        return;
    *x = (float)particles_clamp_abs_or((double)*x, 0.0, PARTICLES3D_PARAM_MAX);
    *y = (float)particles_clamp_abs_or((double)*y, 0.0, PARTICLES3D_PARAM_MAX);
    *z = (float)particles_clamp_abs_or((double)*z, 0.0, PARTICLES3D_PARAM_MAX);
    max_component = fmaxf(fabsf(*x), fmaxf(fabsf(*y), fabsf(*z)));
    if (!isfinite(max_component) || max_component <= 1e-8f)
        return;
    float sx = *x / max_component;
    float sy = *y / max_component;
    float sz = *z / max_component;
    float len = sqrtf(sx * sx + sy * sy + sz * sz);
    if (!isfinite(len) || len <= 1e-8f)
        return;
    *x = sx / len;
    *y = sy / len;
    *z = sz / len;
}

/// @brief Generate a random direction within a cone of half-angle `spread`
///        around the given direction vector.
static void random_cone_dir(rt_particles3d *ps, const double *dir, double spread, float *out) {
    if (!ps || !dir || !out)
        return;
    spread = particles_clamp(spread, 0.0, M_PI);
    if (spread <= 0.0) {
        out[0] = (float)dir[0];
        out[1] = (float)dir[1];
        out[2] = (float)dir[2];
        normalize3(&out[0], &out[1], &out[2]);
        if (fabsf(out[0]) + fabsf(out[1]) + fabsf(out[2]) <= 1e-6f) {
            out[0] = 0.0f;
            out[1] = 1.0f;
            out[2] = 0.0f;
        }
        return;
    }

    /* Random angle within cone */
    float cos_theta = 1.0f - randf(ps) * (1.0f - cosf((float)spread));
    cos_theta = (float)particles_clamp((double)cos_theta, -1.0, 1.0);
    float theta = acosf(cos_theta);
    float phi = randf(ps) * (float)(2.0 * M_PI);

    /* Build a coordinate frame around dir */
    float d[3] = {(float)particles_clamp_abs_or(dir[0], 0.0, PARTICLES3D_PARAM_MAX),
                  (float)particles_clamp_abs_or(dir[1], 1.0, PARTICLES3D_PARAM_MAX),
                  (float)particles_clamp_abs_or(dir[2], 0.0, PARTICLES3D_PARAM_MAX)};
    normalize3(&d[0], &d[1], &d[2]);
    if (fabsf(d[0]) + fabsf(d[1]) + fabsf(d[2]) <= 1e-6f) {
        d[0] = 0;
        d[1] = 1;
        d[2] = 0;
    }

    /* Find a perpendicular vector */
    float perp[3];
    if (fabsf(d[1]) < 0.9f) {
        perp[0] = 0;
        perp[1] = 1;
        perp[2] = 0;
    } else {
        perp[0] = 1;
        perp[1] = 0;
        perp[2] = 0;
    }
    /* right = cross(d, perp), normalized */
    float rx = d[1] * perp[2] - d[2] * perp[1];
    float ry = d[2] * perp[0] - d[0] * perp[2];
    float rz = d[0] * perp[1] - d[1] * perp[0];
    normalize3(&rx, &ry, &rz);
    if (fabsf(rx) + fabsf(ry) + fabsf(rz) <= 1e-6f) {
        rx = 1.0f;
        ry = 0.0f;
        rz = 0.0f;
    }

    /* up = cross(right, d) */
    float ux = ry * d[2] - rz * d[1];
    float uy = rz * d[0] - rx * d[2];
    float uz = rx * d[1] - ry * d[0];

    float st = sinf(theta), ct = cosf(theta);
    float sp = sinf(phi), cp = cosf(phi);

    out[0] = d[0] * ct + rx * st * cp + ux * st * sp;
    out[1] = d[1] * ct + ry * st * cp + uy * st * sp;
    out[2] = d[2] * ct + rz * st * cp + uz * st * sp;

    normalize3(&out[0], &out[1], &out[2]);
    if (fabsf(out[0]) + fabsf(out[1]) + fabsf(out[2]) <= 1e-6f) {
        out[0] = d[0];
        out[1] = d[1];
        out[2] = d[2];
    }
}

/// @brief True if a particle's position, velocity, color, alpha and (non-negative)
///        size are all finite — used to drop corrupted particles before upload.
static int particle_state_is_finite(const vgfx3d_particle_t *p) {
    if (!p)
        return 0;
    for (int i = 0; i < 3; i++) {
        if (!isfinite(p->pos[i]) || !isfinite(p->vel[i]) || !isfinite(p->color[i]))
            return 0;
        if (fabs(p->pos[i]) > PARTICLES3D_WORLD_ABS_MAX || fabs(p->vel[i]) > PARTICLES3D_PARAM_MAX)
            return 0;
        if (p->color[i] < 0.0f || p->color[i] > 1.0f)
            return 0;
    }
    return isfinite(p->color[3]) && p->color[3] >= 0.0f && p->color[3] <= 1.0f &&
           isfinite(p->size) && p->size >= 0.0f && p->size <= (float)PARTICLES3D_PARAM_MAX &&
           isfinite(p->life) && isfinite(p->max_life) && p->max_life > 0.0f;
}

/*==========================================================================
 * Lifecycle
 *=========================================================================*/

/// @brief GC finalizer for Particles3D. Frees the particle pool, drops the texture
/// reference (if any), and drops the per-emitter cached material (built once and reused
/// across frames per GFX-052). Each release-check is independent so a missing texture
/// doesn't prevent material teardown or vice versa.
static void rt_particles3d_finalize(void *obj) {
    rt_particles3d *ps = (rt_particles3d *)obj;
    if (!ps)
        return;
    free(ps->particles);
    ps->particles = NULL;
    for (int i = 0; i < PARTICLES3D_DRAW_SLOT_COUNT; ++i) {
        free(ps->draw_vertices[i]);
        ps->draw_vertices[i] = NULL;
        free(ps->draw_indices[i]);
        ps->draw_indices[i] = NULL;
        particles3d_release_material_slot(&ps->draw_materials[i]);
    }
    free(ps->sort_keys);
    ps->sort_keys = NULL;
    ps->sort_key_capacity = 0;
    ps->sort_key_grow_count = 0;
    particles3d_release_texture_slot(&ps->texture);
    particles3d_release_material_slot(&ps->cached_material);
}

/// @brief Allocate a new particle emitter with an internally-sized pool of up to
/// `max_particles` concurrent particles. Rejects 0 / negative / >100000 to keep a predictable
/// memory ceiling — the pool is calloc'd up front so spawn/kill cost stays O(1) with no
/// re-allocation. Defaults are tuned for a generic sparkle: upward cone emit, ~17° spread,
/// 1-3 u/s speed, 0.5-1.5 s lifetime, 0.2 → 0.05 size taper, 9.8 u/s² gravity, white colour,
/// 1.0 → 0.0 alpha fade, rate 20 /s, alpha-blend mode, point-source emitter. The PRNG state is
/// mixed with the instance pointer so two emitters constructed in the same tick produce
/// different (but each individually deterministic) spawn distributions.
void *rt_particles3d_new(int64_t max_particles) {
    if (max_particles <= 0 || max_particles > 100000) {
        rt_trap("Particles3D.New: max_particles must be 1-100000");
        return NULL;
    }
    rt_particles3d *ps = (rt_particles3d *)rt_obj_new_i64(RT_G3D_PARTICLES3D_CLASS_ID,
                                                          (int64_t)sizeof(rt_particles3d));
    if (!ps) {
        rt_trap("Particles3D.New: memory allocation failed");
        return NULL;
    }

    ps->vptr = NULL;
    ps->particles = (vgfx3d_particle_t *)calloc((size_t)max_particles, sizeof(vgfx3d_particle_t));
    if (!ps->particles) {
        if (rt_obj_release_check0(ps))
            rt_obj_free(ps);
        rt_trap("Particles3D.New: out of memory");
        return NULL;
    }
    ps->count = 0;
    ps->max_particles = (int32_t)max_particles;

    ps->position[0] = ps->position[1] = ps->position[2] = 0.0;
    ps->emit_dir[0] = 0.0;
    ps->emit_dir[1] = 1.0;
    ps->emit_dir[2] = 0.0;
    ps->emit_spread = 0.3;
    ps->speed_min = 1.0;
    ps->speed_max = 3.0;
    ps->life_min = 0.5;
    ps->life_max = 1.5;
    ps->size_start = 0.2;
    ps->size_end = 0.05;
    ps->gravity[0] = 0;
    ps->gravity[1] = -9.8;
    ps->gravity[2] = 0;
    ps->color_start[0] = 1;
    ps->color_start[1] = 1;
    ps->color_start[2] = 1;
    ps->color_end[0] = 1;
    ps->color_end[1] = 1;
    ps->color_end[2] = 1;
    ps->alpha_start = 1.0;
    ps->alpha_end = 0.0;
    ps->rate = 20.0;
    ps->accumulator = 0.0;
    ps->emitting = 0;
    ps->additive_blend = 0;
    ps->softness = 0.0;
    ps->texture = NULL;
    ps->emitter_shape = 0;
    ps->emitter_size[0] = ps->emitter_size[1] = ps->emitter_size[2] = 1.0;
    ps->prng_state = particles3d_next_seed();
    ps->cached_material = NULL;

    rt_obj_set_finalizer(ps, rt_particles3d_finalize);
    return ps;
}

/*==========================================================================
 * Configuration
 *=========================================================================*/

/// @brief Set the emitter origin in world space. New particles spawn at this point (offset by
/// the emitter shape if non-point).
void rt_particles3d_set_position(void *o, double x, double y, double z) {
    rt_particles3d *p = particles3d_checked(o);
    if (!p)
        return;
    p->position[0] = particles_clamp_abs_or(x, 0.0, PARTICLES3D_WORLD_ABS_MAX);
    p->position[1] = particles_clamp_abs_or(y, 0.0, PARTICLES3D_WORLD_ABS_MAX);
    p->position[2] = particles_clamp_abs_or(z, 0.0, PARTICLES3D_WORLD_ABS_MAX);
}

/// @brief Set the average emit direction (normalized internally) and cone half-angle in radians.
/// spread=0 means perfectly aligned, spread=PI means full sphere.
void rt_particles3d_set_direction(void *o, double dx, double dy, double dz, double spread) {
    rt_particles3d *p = particles3d_checked(o);
    if (!p)
        return;
    double dir[3] = {
        particles_clamp_abs_or(dx, 0.0, PARTICLES3D_PARAM_MAX),
        particles_clamp_abs_or(dy, 1.0, PARTICLES3D_PARAM_MAX),
        particles_clamp_abs_or(dz, 0.0, PARTICLES3D_PARAM_MAX),
    };
    double max_component = fmax(fabs(dir[0]), fmax(fabs(dir[1]), fabs(dir[2])));
    int ok = 0;
    if (isfinite(max_component) && max_component > 1e-8) {
        double sx = dir[0] / max_component;
        double sy = dir[1] / max_component;
        double sz = dir[2] / max_component;
        double len = sqrt(sx * sx + sy * sy + sz * sz);
        if (isfinite(len) && len > 1e-8) {
            p->emit_dir[0] = sx / len;
            p->emit_dir[1] = sy / len;
            p->emit_dir[2] = sz / len;
            ok = 1;
        }
    }
    if (!ok) {
        p->emit_dir[0] = 0.0;
        p->emit_dir[1] = 1.0;
        p->emit_dir[2] = 0.0;
    }
    spread = particles_finite_or(spread, 0.0);
    if (spread > M_PI && spread <= 180.0)
        spread *= (M_PI / 180.0);
    p->emit_spread = particles_clamp(spread, 0.0, M_PI);
}

/// @brief Set the per-particle initial speed range [mn, mx] in world-units/sec (uniform random).
void rt_particles3d_set_speed(void *o, double mn, double mx) {
    rt_particles3d *p = particles3d_checked(o);
    if (!p)
        return;
    mn = particles_nonnegative_or_zero(mn);
    mx = particles_nonnegative_or_zero(mx);
    if (mx < mn) {
        double tmp = mn;
        mn = mx;
        mx = tmp;
    }
    if (mn > PARTICLES3D_PARAM_MAX)
        mn = PARTICLES3D_PARAM_MAX;
    if (mx > PARTICLES3D_PARAM_MAX)
        mx = PARTICLES3D_PARAM_MAX;
    p->speed_min = mn;
    p->speed_max = mx;
}

/// @brief Set the per-particle lifetime range [mn, mx] in seconds (uniform random per spawn).
void rt_particles3d_set_lifetime(void *o, double mn, double mx) {
    rt_particles3d *p = particles3d_checked(o);
    if (!p)
        return;
    mn = particles_finite_or(mn, 0.01);
    mx = particles_finite_or(mx, mn);
    if (mn < 0.01)
        mn = 0.01;
    if (mx < 0.01)
        mx = 0.01;
    if (mx < mn) {
        double tmp = mn;
        mn = mx;
        mx = tmp;
    }
    if (mn > PARTICLES3D_PARAM_MAX)
        mn = PARTICLES3D_PARAM_MAX;
    if (mx > PARTICLES3D_PARAM_MAX)
        mx = PARTICLES3D_PARAM_MAX;
    p->life_min = mn;
    p->life_max = mx;
}

/// @brief Set the start and end size (interpolated by age) for each particle.
void rt_particles3d_set_size(void *o, double s, double e) {
    rt_particles3d *p = particles3d_checked(o);
    if (!p)
        return;
    p->size_start = particles_nonnegative_or_zero(s);
    p->size_end = particles_nonnegative_or_zero(e);
}

/// @brief Set the constant acceleration applied to every particle each frame (typical: (0,-9.8,0)).
void rt_particles3d_set_gravity(void *o, double gx, double gy, double gz) {
    rt_particles3d *p = particles3d_checked(o);
    if (!p)
        return;
    p->gravity[0] = particles_clamp_abs_or(gx, 0.0, PARTICLES3D_PARAM_MAX);
    p->gravity[1] = particles_clamp_abs_or(gy, 0.0, PARTICLES3D_PARAM_MAX);
    p->gravity[2] = particles_clamp_abs_or(gz, 0.0, PARTICLES3D_PARAM_MAX);
}

/// @brief Set start (`sc`) and end (`ec`) colors as packed 0xRRGGBBAA. Each particle linearly
/// interpolates between them based on age ratio. Alpha component is set separately via
/// `_set_alpha`.
void rt_particles3d_set_color(void *o, int64_t sc, int64_t ec) {
    rt_particles3d *p = particles3d_checked(o);
    if (!p)
        return;
    unpack_color(sc, p->color_start);
    unpack_color(ec, p->color_end);
}

/// @brief Set start (`sa`) and end (`ea`) alpha values [0, 1]. Common pattern: 1.0→0.0 for
/// fade-out.
void rt_particles3d_set_alpha(void *o, double sa, double ea) {
    rt_particles3d *p = particles3d_checked(o);
    if (!p)
        return;
    p->alpha_start = particles_clamp(sa, 0.0, 1.0);
    p->alpha_end = particles_clamp(ea, 0.0, 1.0);
}

/// @brief Set the spawn rate in particles per second. The accumulator pattern emits whole
/// particles when ≥1 worth has accumulated, preserving fractional rates across frames.
void rt_particles3d_set_rate(void *o, double r) {
    rt_particles3d *p = particles3d_checked(o);
    if (!p)
        return;
    p->rate = particles_nonnegative_or_zero(r);
}

/// @brief Toggle additive blend mode (1 = additive for fire/glow, 0 = alpha blend for smoke).
/// Additive skips the back-to-front sort since order doesn't affect the result.
/// @brief Set the soft-particle fade distance in world units (0 disables).
/// @details Particles fade out as their quads approach opaque geometry, hiding
///          the hard intersection line. Requires a backend with an opaque depth
///          snapshot (BackendSupports("soft-particles")); ignored elsewhere.
void rt_particles3d_set_softness(void *o, double distance) {
    rt_particles3d *p = particles3d_checked(o);
    if (!p)
        return;
    if (!isfinite(distance) || distance < 0.0)
        distance = 0.0;
    if (distance > PARTICLES3D_PARAM_MAX)
        distance = PARTICLES3D_PARAM_MAX;
    p->softness = distance;
}

void rt_particles3d_set_additive(void *o, int8_t a) {
    rt_particles3d *p = particles3d_checked(o);
    if (!p)
        return;
    p->additive_blend = a ? 1 : 0;
}

/// @brief Set the per-particle billboard texture. NULL produces solid color quads.
void rt_particles3d_set_texture(void *o, void *tex) {
    rt_particles3d *ps = particles3d_checked(o);
    if (!ps)
        return;
    if (tex && !rt_pixels_checked_impl_or_null(tex))
        return;
    if (ps->texture == tex)
        return;
    particles3d_assign_texture_ref(&ps->texture, tex);
}

/// @brief Select the emitter volume: 0 = point (default), 1 = sphere (uniform interior),
/// 2 = box. Combined with `_set_emitter_size` to control the spawn region.
void rt_particles3d_set_emitter_shape(void *o, int64_t s) {
    rt_particles3d *p = particles3d_checked(o);
    if (!p)
        return;
    if (s < 0)
        s = 0;
    if (s > 2)
        s = 2;
    p->emitter_shape = (int32_t)s;
}

/// @brief Set the emitter shape's extent. For sphere: only sx is used (radius); for box: full
/// half-extents per axis. Ignored for point emitter.
void rt_particles3d_set_emitter_size(void *o, double sx, double sy, double sz) {
    rt_particles3d *p = particles3d_checked(o);
    if (!p)
        return;
    p->emitter_size[0] = fabs(particles_clamp_abs_or(sx, 0.0, PARTICLES3D_PARAM_MAX));
    p->emitter_size[1] = fabs(particles_clamp_abs_or(sy, 0.0, PARTICLES3D_PARAM_MAX));
    p->emitter_size[2] = fabs(particles_clamp_abs_or(sz, 0.0, PARTICLES3D_PARAM_MAX));
}

/*==========================================================================
 * Playback
 *=========================================================================*/

/// @brief Begin emitting (continuous spawn at the configured rate). Existing live particles
/// continue to update regardless of the emit flag.
void rt_particles3d_start(void *o) {
    rt_particles3d *p = particles3d_checked(o);
    if (p)
        p->emitting = 1;
}

/// @brief Stop continuous emission. Existing particles run to natural lifetime; for instant
/// removal use `_clear`.
void rt_particles3d_stop(void *o) {
    rt_particles3d *p = particles3d_checked(o);
    if (p)
        p->emitting = 0;
}

/// @brief Kill every live particle and reset the spawn accumulator. Doesn't change emit state.
void rt_particles3d_clear(void *o) {
    rt_particles3d *p = particles3d_checked(o);
    if (!p)
        return;
    p->count = 0;
    p->accumulator = 0.0;
}

/// @brief Number of particles currently alive.
int64_t rt_particles3d_get_count(void *o) {
    rt_particles3d *p = particles3d_checked(o);
    if (!p || !p->particles || p->max_particles <= 0 || p->count <= 0)
        return 0;
    return p->count > p->max_particles ? p->max_particles : p->count;
}

/// @brief Copy the emitter world position into @p out. Used by floating-origin rebase tests to
///   verify the emitter shifted; zeroed if the handle is invalid or @p out is NULL.
void rt_particles3d_get_position(void *o, double out[3]) {
    rt_particles3d *p = particles3d_checked(o);
    if (!out)
        return;
    out[0] = p ? particles_clamp_abs_or(p->position[0], 0.0, PARTICLES3D_WORLD_ABS_MAX) : 0.0;
    out[1] = p ? particles_clamp_abs_or(p->position[1], 0.0, PARTICLES3D_WORLD_ABS_MAX) : 0.0;
    out[2] = p ? particles_clamp_abs_or(p->position[2], 0.0, PARTICLES3D_WORLD_ABS_MAX) : 0.0;
}

/// @brief Returns 1 if continuous emission is enabled (`_start` called, no subsequent `_stop`).
int8_t rt_particles3d_get_emitting(void *o) {
    rt_particles3d *p = particles3d_checked(o);
    return p && p->emitting ? 1 : 0;
}

/*==========================================================================
 * Spawning
 *=========================================================================*/

/// @brief Initialize and append one new particle to the active pool. Silently no-ops
/// when the pool is full — caller (`update`) decides emission rate, this only handles
/// the per-particle slot. Reads emitter shape (point / sphere / box), velocity
/// distribution, lifetime range, and start-colour to populate the new entry. Position
/// is emitter-origin plus a per-shape offset. Velocity is randomised within the
/// configured spread, then scaled to the speed range; lifetime gets a random value in
/// the configured min..max window so the pool desynchronises naturally.
static void spawn_particle(rt_particles3d *ps) {
    if (!ps || !ps->particles || ps->max_particles <= 0)
        return;
    if (ps->count < 0)
        ps->count = 0;
    if (ps->count >= ps->max_particles)
        return;
    vgfx3d_particle_t *p = &ps->particles[ps->count++];

    /* Position: emitter origin + shape offset */
    p->pos[0] = particles_clamp_abs_or(ps->position[0], 0.0, PARTICLES3D_WORLD_ABS_MAX);
    p->pos[1] = particles_clamp_abs_or(ps->position[1], 0.0, PARTICLES3D_WORLD_ABS_MAX);
    p->pos[2] = particles_clamp_abs_or(ps->position[2], 0.0, PARTICLES3D_WORLD_ABS_MAX);
    double emitter_size[3] = {
        fabs(particles_clamp_abs_or(ps->emitter_size[0], 0.0, PARTICLES3D_PARAM_MAX)),
        fabs(particles_clamp_abs_or(ps->emitter_size[1], 0.0, PARTICLES3D_PARAM_MAX)),
        fabs(particles_clamp_abs_or(ps->emitter_size[2], 0.0, PARTICLES3D_PARAM_MAX)),
    };

    if (ps->emitter_shape == 1) /* sphere */
    {
        double r = cbrt((double)randf(ps)) * emitter_size[0];
        double theta = (double)randf(ps) * (2.0 * M_PI);
        double phi = acos(particles_clamp(1.0 - 2.0 * (double)randf(ps), -1.0, 1.0));
        p->pos[0] += r * sin(phi) * cos(theta);
        p->pos[1] += r * cos(phi);
        p->pos[2] += r * sin(phi) * sin(theta);
    } else if (ps->emitter_shape == 2) /* box */
    {
        p->pos[0] += ((double)randf(ps) - 0.5) * 2.0 * emitter_size[0];
        p->pos[1] += ((double)randf(ps) - 0.5) * 2.0 * emitter_size[1];
        p->pos[2] += ((double)randf(ps) - 0.5) * 2.0 * emitter_size[2];
    }
    for (int i = 0; i < 3; i++)
        p->pos[i] = particles_clamp_abs_or(p->pos[i], 0.0, PARTICLES3D_WORLD_ABS_MAX);

    /* Velocity */
    float dir[3];
    random_cone_dir(ps, ps->emit_dir, ps->emit_spread, dir);
    double speed = (double)rand_range(ps, ps->speed_min, ps->speed_max);
    p->vel[0] = (double)dir[0] * speed;
    p->vel[1] = (double)dir[1] * speed;
    p->vel[2] = (double)dir[2] * speed;

    /* Life */
    p->max_life = rand_range(ps, ps->life_min, ps->life_max);
    if (p->max_life < 0.01f)
        p->max_life = 0.01f;
    p->life = p->max_life;

    /* Initial visuals */
    p->size = (float)particles_nonnegative_or_zero(ps->size_start);
    p->color[0] = ps->color_start[0];
    p->color[1] = ps->color_start[1];
    p->color[2] = ps->color_start[2];
    p->color[3] = (float)ps->alpha_start;
    if (!particle_state_is_finite(p))
        ps->count--;
}

/// @brief Spawn `count` particles immediately (in addition to any continuous emission). Useful
/// for explosions, sparks, one-shot effects.
void rt_particles3d_burst(void *o, int64_t count) {
    rt_particles3d *ps = particles3d_checked(o);
    if (!ps || count <= 0)
        return;
    if (ps->count < 0)
        ps->count = 0;
    if (ps->count > ps->max_particles)
        ps->count = ps->max_particles;
    int64_t available = (int64_t)ps->max_particles - (int64_t)ps->count;
    if (available <= 0)
        return;
    if (count > available)
        count = available;
    for (int64_t i = 0; i < count; i++)
        spawn_particle(ps);
}

/// @brief Internal floating-origin hook: subtract the world rebase delta from
///   the emitter origin and every live particle, preserving velocities/lifetimes.
void rt_particles3d_rebase_origin(void *o, double dx, double dy, double dz) {
    rt_particles3d *ps = particles3d_checked(o);
    if (!ps)
        return;
    double delta[3] = {particles_clamp_abs_or(dx, 0.0, PARTICLES3D_WORLD_ABS_MAX),
                       particles_clamp_abs_or(dy, 0.0, PARTICLES3D_WORLD_ABS_MAX),
                       particles_clamp_abs_or(dz, 0.0, PARTICLES3D_WORLD_ABS_MAX)};
    if (delta[0] == 0.0 && delta[1] == 0.0 && delta[2] == 0.0)
        return;

    ps->position[0] =
        particles_clamp_abs_or(ps->position[0] - delta[0], 0.0, PARTICLES3D_WORLD_ABS_MAX);
    ps->position[1] =
        particles_clamp_abs_or(ps->position[1] - delta[1], 0.0, PARTICLES3D_WORLD_ABS_MAX);
    ps->position[2] =
        particles_clamp_abs_or(ps->position[2] - delta[2], 0.0, PARTICLES3D_WORLD_ABS_MAX);

    for (int32_t i = 0; i < ps->count;) {
        double x = ps->particles[i].pos[0] - delta[0];
        double y = ps->particles[i].pos[1] - delta[1];
        double z = ps->particles[i].pos[2] - delta[2];
        if (!isfinite(x) || !isfinite(y) || !isfinite(z) || fabs(x) > PARTICLES3D_WORLD_ABS_MAX ||
            fabs(y) > PARTICLES3D_WORLD_ABS_MAX || fabs(z) > PARTICLES3D_WORLD_ABS_MAX) {
            ps->particles[i] = ps->particles[--ps->count];
            continue;
        }
        ps->particles[i].pos[0] = x;
        ps->particles[i].pos[1] = y;
        ps->particles[i].pos[2] = z;
        i++;
    }
}

/*==========================================================================
 * Update (Euler integration + lifetime + interpolation)
 *=========================================================================*/

/// @brief Per-frame tick. Walks every live particle to apply Euler integration (pos += vel*dt,
/// vel += gravity*dt), age-interpolate size/color/alpha, and reap dead ones via O(1) swap-with-
/// last. Then accumulates and emits new particles from the rate while emission is enabled.
void rt_particles3d_update(void *o, double delta_time) {
    rt_particles3d *ps = particles3d_checked(o);
    if (!ps || !isfinite(delta_time) || delta_time <= 0.0)
        return;
    if (!ps->particles || ps->max_particles <= 0)
        return;
    if (ps->count < 0)
        ps->count = 0;
    if (ps->count > ps->max_particles)
        ps->count = ps->max_particles;
    if (delta_time > PARTICLES3D_DT_MAX)
        delta_time = PARTICLES3D_DT_MAX;
    double dt = delta_time;
    if (!isfinite(dt) || dt <= 0.0)
        return;
    float dtf = (float)dt;
    double gravity[3] = {
        particles_clamp_abs_or(ps->gravity[0], 0.0, PARTICLES3D_PARAM_MAX),
        particles_clamp_abs_or(ps->gravity[1], 0.0, PARTICLES3D_PARAM_MAX),
        particles_clamp_abs_or(ps->gravity[2], 0.0, PARTICLES3D_PARAM_MAX),
    };
    double size_start = particles_nonnegative_or_zero(ps->size_start);
    double size_end = particles_nonnegative_or_zero(ps->size_end);
    float color_start[3];
    float color_end[3];
    for (int c = 0; c < 3; c++) {
        color_start[c] = (float)particles_clamp((double)ps->color_start[c], 0.0, 1.0);
        color_end[c] = (float)particles_clamp((double)ps->color_end[c], 0.0, 1.0);
    }
    double alpha_start = particles_clamp(ps->alpha_start, 0.0, 1.0);
    double alpha_end = particles_clamp(ps->alpha_end, 0.0, 1.0);

    /* Update alive particles */
    for (int32_t i = 0; i < ps->count;) {
        vgfx3d_particle_t *p = &ps->particles[i];
        if (!particle_state_is_finite(p)) {
            ps->particles[i] = ps->particles[--ps->count];
            continue;
        }
        p->life -= dtf;
        if (p->life <= 0.0f) {
            /* Kill: swap with last alive particle (O(1) unstable removal) */
            ps->particles[i] = ps->particles[--ps->count];
            continue;
        }

        /* Physics: pos += vel * dt, vel += gravity * dt */
        p->pos[0] =
            particles_clamp_abs_or(p->pos[0] + p->vel[0] * dt, 0.0, PARTICLES3D_WORLD_ABS_MAX);
        p->pos[1] =
            particles_clamp_abs_or(p->pos[1] + p->vel[1] * dt, 0.0, PARTICLES3D_WORLD_ABS_MAX);
        p->pos[2] =
            particles_clamp_abs_or(p->pos[2] + p->vel[2] * dt, 0.0, PARTICLES3D_WORLD_ABS_MAX);
        p->vel[0] = particles_clamp_abs_or(p->vel[0] + gravity[0] * dt, 0.0, PARTICLES3D_PARAM_MAX);
        p->vel[1] = particles_clamp_abs_or(p->vel[1] + gravity[1] * dt, 0.0, PARTICLES3D_PARAM_MAX);
        p->vel[2] = particles_clamp_abs_or(p->vel[2] + gravity[2] * dt, 0.0, PARTICLES3D_PARAM_MAX);

        /* Interpolate size, color, alpha based on age ratio */
        float age = 1.0f - p->life / p->max_life; /* 0 = birth, 1 = death */
        if (!isfinite(age) || age < 0.0f)
            age = 0.0f;
        if (age > 1.0f)
            age = 1.0f;
        p->size = (float)size_start + age * (float)(size_end - size_start);
        p->color[0] = color_start[0] + age * (color_end[0] - color_start[0]);
        p->color[1] = color_start[1] + age * (color_end[1] - color_start[1]);
        p->color[2] = color_start[2] + age * (color_end[2] - color_start[2]);
        p->color[3] = (float)alpha_start + age * (float)(alpha_end - alpha_start);

        if (!particle_state_is_finite(p)) {
            ps->particles[i] = ps->particles[--ps->count];
            continue;
        }

        i++;
    }

    /* Spawn new particles */
    ps->rate = particles_nonnegative_or_zero(ps->rate);
    if (ps->emitting && ps->rate > 0.0) {
        double max_budget = (double)(ps->max_particles - ps->count) + 0.999999;
        if (!isfinite(ps->accumulator) || ps->accumulator < 0.0)
            ps->accumulator = 0.0;
        ps->accumulator += ps->rate * delta_time;
        if (ps->accumulator > max_budget)
            ps->accumulator = max_budget;
        while (ps->accumulator >= 1.0 && ps->count < ps->max_particles) {
            spawn_particle(ps);
            ps->accumulator -= 1.0;
        }
    }
}

/*==========================================================================
 * Billboard rendering
 *=========================================================================*/

extern void rt_canvas3d_draw_mesh(void *obj, void *mesh, void *transform, void *material);
extern void rt_canvas3d_draw_mesh_matrix_keyed(void *obj,
                                               void *mesh_obj,
                                               const double *model_matrix,
                                               void *material_obj,
                                               const void *motion_key,
                                               const float *prev_bone_palette,
                                               const float *prev_morph_weights);
extern int rt_canvas3d_get_camera_relative_origin(void *canvas, double out_origin[3]);
extern void *rt_material3d_new(void);
extern void rt_material3d_set_color(void *m, double r, double g, double b);
extern void rt_material3d_set_unlit(void *m, int8_t u);
extern void rt_material3d_set_alpha(void *m, double a);
extern void rt_material3d_set_alpha_mode(void *m, int64_t mode);
extern void rt_material3d_set_texture(void *m, void *tex);
extern int rt_canvas3d_remove_temp_buffer(void *canvas, void *buffer);

/// @brief Sort record pairing a particle's index with its camera-space depth.
/// @details Sorting by the camera forward axis is more stable for large billboard quads than
///   Euclidean distance, which can flip ordering near screen edges.
typedef struct particle3d_sort_key {
    int32_t index;
    double view_depth;
} particle3d_sort_key;

/// @brief qsort comparator ordering keys by descending camera-space depth (farthest first);
///   ties break on index so the sort is stable and deterministic.
static int particle3d_sort_key_desc(const void *a, const void *b) {
    const particle3d_sort_key *ka = (const particle3d_sort_key *)a;
    const particle3d_sort_key *kb = (const particle3d_sort_key *)b;
    if (ka->view_depth < kb->view_depth)
        return 1;
    if (ka->view_depth > kb->view_depth)
        return -1;
    if (ka->index < kb->index)
        return -1;
    if (ka->index > kb->index)
        return 1;
    return 0;
}

/// @brief Return true when sort keys are already in back-to-front draw order.
/// @details Particle systems often move coherently frame-to-frame, so the previous draw order
///          remains sorted for small camera/particle deltas. Detecting that case avoids a full
///          `qsort` call and keeps deterministic tie ordering by comparing index values too.
/// @param keys Sort-key array to inspect.
/// @param count Number of keys in @p keys.
/// @return 1 when the array is already descending by depth with ascending index ties.
static int particles3d_sort_keys_already_descending(const particle3d_sort_key *keys,
                                                    int32_t count) {
    if (!keys || count <= 1)
        return 1;
    for (int32_t i = 1; i < count; i++) {
        const particle3d_sort_key *prev = &keys[i - 1];
        const particle3d_sort_key *cur = &keys[i];
        if (prev->view_depth < cur->view_depth)
            return 0;
        if (prev->view_depth == cur->view_depth && prev->index > cur->index)
            return 0;
    }
    return 1;
}

/// @brief Sort a small particle key array with insertion sort.
/// @details For the common case of a few dozen transparent particles, insertion sort avoids the
///          indirect comparator overhead of `qsort` and performs well on nearly sorted input.
/// @param keys Sort-key array to reorder in place.
/// @param count Number of keys in @p keys.
static void particles3d_insertion_sort_keys_desc(particle3d_sort_key *keys, int32_t count) {
    if (!keys || count <= 1)
        return;
    for (int32_t i = 1; i < count; i++) {
        particle3d_sort_key key = keys[i];
        int32_t j = i - 1;
        while (j >= 0) {
            int after = keys[j].view_depth < key.view_depth ||
                        (keys[j].view_depth == key.view_depth && keys[j].index > key.index);
            if (!after)
                break;
            keys[j + 1] = keys[j];
            j--;
        }
        keys[j + 1] = key;
    }
}

/// @brief Sort particle keys back-to-front using the cheapest appropriate strategy.
/// @details Skips work when keys are already ordered, uses insertion sort for small batches, and
///          falls back to `qsort` for larger unordered alpha-blended emitters.
/// @param keys Sort-key array to reorder in place.
/// @param count Number of keys in @p keys.
static void particles3d_sort_keys_back_to_front(particle3d_sort_key *keys, int32_t count) {
    if (!keys || count <= 1 || particles3d_sort_keys_already_descending(keys, count))
        return;
    if (count <= 32) {
        particles3d_insertion_sort_keys_desc(keys, count);
        return;
    }
    qsort(keys, (size_t)count, sizeof(*keys), particle3d_sort_key_desc);
}

/// @brief Ensure the persistent particle-sort scratch buffer holds @p count keys.
/// @details Returning failure aborts alpha particle rendering for the frame instead of silently
///   falling back to unsorted transparent quads, because that fallback causes obvious flicker.
static int particles3d_ensure_sort_keys(rt_particles3d *ps, int32_t count) {
    void *grown;
    int32_t target_capacity;
    if (!ps || count <= 0)
        return 0;
    target_capacity = ps->max_particles > count ? ps->max_particles : count;
    if (target_capacity <= 0)
        return 0;
    if (ps->sort_key_capacity >= target_capacity && ps->sort_keys)
        return 1;
    if ((size_t)target_capacity > SIZE_MAX / sizeof(particle3d_sort_key)) {
        rt_trap("Particles3D.Draw: sort key allocation overflow");
        return 0;
    }
    grown = realloc(ps->sort_keys, (size_t)target_capacity * sizeof(particle3d_sort_key));
    if (!grown) {
        rt_trap("Particles3D.Draw: sort key allocation failed");
        return 0;
    }
    ps->sort_keys = grown;
    ps->sort_key_capacity = target_capacity;
    ps->sort_key_grow_count++;
    return 1;
}

/// @brief Test hook: current persistent alpha-sort key capacity.
int64_t rt_particles3d_test_sort_key_capacity(void *o) {
    rt_particles3d *ps = particles3d_checked(o);
    return ps ? ps->sort_key_capacity : 0;
}

/// @brief Test hook: number of persistent alpha-sort key buffer growth operations.
uint64_t rt_particles3d_test_sort_key_grow_count(void *o) {
    rt_particles3d *ps = particles3d_checked(o);
    return ps ? ps->sort_key_grow_count : 0;
}

/// @brief Lazily create the system's shared unlit white particle material in @p *slot.
/// @return 1 if the slot holds a material (existing or newly made), 0 on allocation failure.
static int particles3d_ensure_material(void **slot) {
    if (!slot)
        return 0;
    if (*slot && !rt_g3d_has_class(*slot, RT_G3D_MATERIAL3D_CLASS_ID))
        particles3d_release_material_slot(slot);
    if (!*slot) {
        *slot = rt_material3d_new();
        if (!*slot)
            return 0;
        rt_material3d_set_color(*slot, 1.0, 1.0, 1.0);
        rt_material3d_set_unlit(*slot, 1);
    }
    return 1;
}

/// @brief Acquire transient vertex/index/material storage for this frame's particle batch.
/// @details Prefers canvas-owned scratch buffers (flagged via @p out_canvas_owned) to avoid
///          per-frame allocation, falling back to the system's own storage. Also ensures the
///          shared particle material exists.
/// @return 1 with the out-params set, 0 on invalid args or allocation failure.
static int particles3d_acquire_draw_storage(rt_particles3d *ps,
                                            rt_canvas3d *canvas,
                                            uint32_t vert_count,
                                            uint32_t idx_count,
                                            vgfx3d_vertex_t **out_vertices,
                                            uint32_t **out_indices,
                                            void **out_material,
                                            int *out_canvas_owned) {
    int32_t slot;
    int64_t frame_serial;
    if (!ps || !canvas || !out_vertices || !out_indices || !out_material || !out_canvas_owned)
        return 0;
    *out_vertices = NULL;
    *out_indices = NULL;
    *out_material = NULL;
    *out_canvas_owned = 0;
    frame_serial = canvas->frame_serial;
    if (ps->draw_frame_serial != frame_serial) {
        ps->draw_frame_serial = frame_serial;
        ps->draw_slots_used = 0;
    }
    if (ps->draw_slots_used >= 0 && ps->draw_slots_used < PARTICLES3D_DRAW_SLOT_COUNT) {
        slot = ps->draw_slots_used++;
        if (vert_count > ps->draw_vertex_capacity[slot]) {
            vgfx3d_vertex_t *grown;
            if ((size_t)vert_count > SIZE_MAX / sizeof(*grown))
                return 0;
            grown = (vgfx3d_vertex_t *)realloc(ps->draw_vertices[slot],
                                               (size_t)vert_count * sizeof(*grown));
            if (!grown)
                return 0;
            ps->draw_vertices[slot] = grown;
            ps->draw_vertex_capacity[slot] = vert_count;
        }
        if (idx_count > ps->draw_index_capacity[slot]) {
            uint32_t *grown;
            if ((size_t)idx_count > SIZE_MAX / sizeof(*grown))
                return 0;
            grown = (uint32_t *)realloc(ps->draw_indices[slot], (size_t)idx_count * sizeof(*grown));
            if (!grown)
                return 0;
            ps->draw_indices[slot] = grown;
            ps->draw_index_capacity[slot] = idx_count;
        }
        if (!particles3d_ensure_material(&ps->draw_materials[slot]))
            return 0;
        memset(ps->draw_vertices[slot], 0, (size_t)vert_count * sizeof(*ps->draw_vertices[slot]));
        *out_vertices = ps->draw_vertices[slot];
        *out_indices = ps->draw_indices[slot];
        *out_material = ps->draw_materials[slot];
        return 1;
    }

    *out_vertices = (vgfx3d_vertex_t *)calloc((size_t)vert_count, sizeof(**out_vertices));
    *out_indices = (uint32_t *)malloc((size_t)idx_count * sizeof(**out_indices));
    *out_material = rt_material3d_new();
    if (!*out_vertices || !*out_indices || !*out_material) {
        free(*out_vertices);
        free(*out_indices);
        if (*out_material && rt_obj_release_check0(*out_material))
            rt_obj_free(*out_material);
        *out_vertices = NULL;
        *out_indices = NULL;
        *out_material = NULL;
        return 0;
    }
    rt_material3d_set_color(*out_material, 1.0, 1.0, 1.0);
    rt_material3d_set_unlit(*out_material, 1);
    *out_canvas_owned = 1;
    return 1;
}

/// @brief Build a row-major model matrix that translates by @p origin (identity rotation/scale).
static void particles3d_origin_model_matrix(const double origin[3], double out[16]) {
    static const double identity[16] = {
        1.0,
        0.0,
        0.0,
        0.0,
        0.0,
        1.0,
        0.0,
        0.0,
        0.0,
        0.0,
        1.0,
        0.0,
        0.0,
        0.0,
        0.0,
        1.0,
    };
    memcpy(out, identity, sizeof(identity));
    if (origin) {
        out[3] = particles_clamp_abs_or(origin[0], 0.0, PARTICLES3D_WORLD_ABS_MAX);
        out[7] = particles_clamp_abs_or(origin[1], 0.0, PARTICLES3D_WORLD_ABS_MAX);
        out[11] = particles_clamp_abs_or(origin[2], 0.0, PARTICLES3D_WORLD_ABS_MAX);
    }
}

/// @brief Normalize a 3-vector or replace it with a caller-provided fallback.
static void particles3d_normalize3_or(float v[3], float fx, float fy, float fz) {
    if (!v) {
        return;
    }
    v[0] = (float)particles_clamp_abs_or((double)v[0], (double)fx, PARTICLES3D_PARAM_MAX);
    v[1] = (float)particles_clamp_abs_or((double)v[1], (double)fy, PARTICLES3D_PARAM_MAX);
    v[2] = (float)particles_clamp_abs_or((double)v[2], (double)fz, PARTICLES3D_PARAM_MAX);
    float max_component = fmaxf(fabsf(v[0]), fmaxf(fabsf(v[1]), fabsf(v[2])));
    if (!isfinite(max_component) || max_component <= 1e-8f) {
        v[0] = fx;
        v[1] = fy;
        v[2] = fz;
        return;
    }
    float sx = v[0] / max_component;
    float sy = v[1] / max_component;
    float sz = v[2] / max_component;
    float len = sqrtf(sx * sx + sy * sy + sz * sz);
    if (!isfinite(len) || len <= 1e-8f) {
        v[0] = fx;
        v[1] = fy;
        v[2] = fz;
        return;
    }
    v[0] = sx / len;
    v[1] = sy / len;
    v[2] = sz / len;
}

/// @brief Render every live particle as a camera-facing billboard quad. Extracts right/up from
/// the camera view matrix to build the quads. Sorts back-to-front for alpha blending; skips the
/// sort when in additive mode (order-independent). Both additive and alpha modes stay batched;
/// alpha uses sorted indices so the backend draws quads back-to-front inside one mesh.
void rt_particles3d_draw(void *o, void *canvas3d, void *camera) {
    rt_particles3d *ps = particles3d_checked(o);
    rt_canvas3d *canvas = rt_canvas3d_checked_or_stack(canvas3d);
    rt_camera3d *cam = rt_camera3d_checked_or_stack(camera);
    if (!ps || !canvas || !cam)
        return;
    particles3d_repair_refs(ps);
    if (!ps->particles || ps->max_particles <= 0)
        return;
    if (ps->count < 0)
        ps->count = 0;
    if (ps->count > ps->max_particles)
        ps->count = ps->max_particles;
    for (int32_t i = 0; i < ps->count;) {
        if (particle_state_is_finite(&ps->particles[i])) {
            i++;
            continue;
        }
        ps->particles[i] = ps->particles[--ps->count];
    }
    if (ps->count == 0)
        return;

    double origin[3] = {0.0, 0.0, 0.0};
    (void)rt_canvas3d_get_camera_relative_origin(canvas3d, origin);

    /* Extract camera right and up vectors from view matrix (row-major).
     * Row 0 = right, Row 1 = up (before translation). */
    float right[3] = {(float)particles_clamp_abs_or(cam->view[0], 1.0, PARTICLES3D_PARAM_MAX),
                      (float)particles_clamp_abs_or(cam->view[1], 0.0, PARTICLES3D_PARAM_MAX),
                      (float)particles_clamp_abs_or(cam->view[2], 0.0, PARTICLES3D_PARAM_MAX)};
    float up[3] = {(float)particles_clamp_abs_or(cam->view[4], 0.0, PARTICLES3D_PARAM_MAX),
                   (float)particles_clamp_abs_or(cam->view[5], 1.0, PARTICLES3D_PARAM_MAX),
                   (float)particles_clamp_abs_or(cam->view[6], 0.0, PARTICLES3D_PARAM_MAX)};
    particles3d_normalize3_or(right, 1.0f, 0.0f, 0.0f);
    particles3d_normalize3_or(up, 0.0f, 1.0f, 0.0f);
    float forward[3] = {right[1] * up[2] - right[2] * up[1],
                        right[2] * up[0] - right[0] * up[2],
                        right[0] * up[1] - right[1] * up[0]};
    particles3d_normalize3_or(forward, 0.0f, 0.0f, 1.0f);

    particle3d_sort_key *sort_keys = NULL;

    /* Sort particles back-to-front for alpha blend (skip for additive) */
    if (!ps->additive_blend) {
        if (!particles3d_ensure_sort_keys(ps, ps->count))
            return;
        sort_keys = (particle3d_sort_key *)ps->sort_keys;
        if (sort_keys) {
            for (int32_t i = 0; i < ps->count; i++) {
                vgfx3d_particle_t *p = &ps->particles[i];
                double eye_x = particles_clamp_abs_or(cam->eye[0], 0.0, PARTICLES3D_WORLD_ABS_MAX);
                double eye_y = particles_clamp_abs_or(cam->eye[1], 0.0, PARTICLES3D_WORLD_ABS_MAX);
                double eye_z = particles_clamp_abs_or(cam->eye[2], 0.0, PARTICLES3D_WORLD_ABS_MAX);
                double dx =
                    particles_clamp_abs_or(p->pos[0] - eye_x, 0.0, PARTICLES3D_WORLD_ABS_MAX);
                double dy =
                    particles_clamp_abs_or(p->pos[1] - eye_y, 0.0, PARTICLES3D_WORLD_ABS_MAX);
                double dz =
                    particles_clamp_abs_or(p->pos[2] - eye_z, 0.0, PARTICLES3D_WORLD_ABS_MAX);
                sort_keys[i].index = i;
                sort_keys[i].view_depth =
                    dx * (double)forward[0] + dy * (double)forward[1] + dz * (double)forward[2];
                if (!isfinite(sort_keys[i].view_depth))
                    sort_keys[i].view_depth = 0.0;
            }
            particles3d_sort_keys_back_to_front(sort_keys, ps->count);
        }
    }

    /* Build batched vertex + index buffers */
    if ((uint64_t)ps->count > UINT32_MAX / 6u ||
        (size_t)ps->count > SIZE_MAX / (4u * sizeof(vgfx3d_vertex_t)) ||
        (size_t)ps->count > SIZE_MAX / (6u * sizeof(uint32_t))) {
        rt_trap("Particles3D.Draw: particle buffer allocation overflow");
        return;
    }
    uint32_t vert_count = (uint32_t)ps->count * 4;
    uint32_t idx_count = (uint32_t)ps->count * 6;
    vgfx3d_vertex_t *verts = NULL;
    uint32_t *indices = NULL;
    void *mat = NULL;
    int canvas_owned_storage = 0;
    if (!particles3d_acquire_draw_storage(
            ps, canvas, vert_count, idx_count, &verts, &indices, &mat, &canvas_owned_storage)) {
        return;
    }
    if (!mat) {
        if (canvas_owned_storage) {
            free(verts);
            free(indices);
        }
        return;
    }

    for (int32_t i = 0; i < ps->count; i++) {
        int32_t particle_index = sort_keys ? sort_keys[i].index : i;
        vgfx3d_particle_t *p = &ps->particles[particle_index];
        float hs = p->size * 0.5f;
        uint32_t base = (uint32_t)i * 4;

        /* v0 = bottom-left, v1 = bottom-right, v2 = top-right, v3 = top-left */
        for (int vi = 0; vi < 4; vi++) {
            float rs = (vi == 1 || vi == 2) ? hs : -hs;
            float us = (vi == 2 || vi == 3) ? hs : -hs;
            vgfx3d_vertex_t *v = &verts[base + vi];
            double vx = p->pos[0] - origin[0] + (double)right[0] * rs + (double)up[0] * us;
            double vy = p->pos[1] - origin[1] + (double)right[1] * rs + (double)up[1] * us;
            double vz = p->pos[2] - origin[2] + (double)right[2] * rs + (double)up[2] * us;
            vx = particles_clamp_abs_or(vx, 0.0, PARTICLES3D_WORLD_ABS_MAX);
            vy = particles_clamp_abs_or(vy, 0.0, PARTICLES3D_WORLD_ABS_MAX);
            vz = particles_clamp_abs_or(vz, 0.0, PARTICLES3D_WORLD_ABS_MAX);
            v->pos[0] = (float)vx;
            v->pos[1] = (float)vy;
            v->pos[2] = (float)vz;
            v->normal[0] = forward[0];
            v->normal[1] = forward[1];
            v->normal[2] = forward[2];
            v->color[0] = p->color[0];
            v->color[1] = p->color[1];
            v->color[2] = p->color[2];
            v->color[3] = p->color[3];
        }
        /* UVs */
        verts[base + 0].uv[0] = 0;
        verts[base + 0].uv[1] = 1;
        verts[base + 1].uv[0] = 1;
        verts[base + 1].uv[1] = 1;
        verts[base + 2].uv[0] = 1;
        verts[base + 2].uv[1] = 0;
        verts[base + 3].uv[0] = 0;
        verts[base + 3].uv[1] = 0;

        /* 2 triangles per quad (CCW). Alpha particles are sorted by the vertex
         * construction order above, so absolute indices preserve the sorted
         * back-to-front order while keeping the emitter in a single draw. */
        indices[i * 6 + 0] = base + 0;
        indices[i * 6 + 1] = base + 1;
        indices[i * 6 + 2] = base + 2;
        indices[i * 6 + 3] = base + 0;
        indices[i * 6 + 4] = base + 2;
        indices[i * 6 + 5] = base + 3;
    }
    sort_keys = NULL;

    /* Create a temporary mesh and submit via the normal draw pipeline.
     * Use unlit material with particle alpha for the draw command. */
    rt_mesh3d tmp_mesh;
    memset(&tmp_mesh, 0, sizeof(tmp_mesh));
    tmp_mesh.vertices = verts;
    tmp_mesh.positions64 = NULL;
    tmp_mesh.vertex_count = vert_count;
    tmp_mesh.vertex_capacity = vert_count;
    tmp_mesh.indices = indices;
    tmp_mesh.index_count = idx_count;
    tmp_mesh.index_capacity = idx_count;
    tmp_mesh.resident = 1;

    rt_material3d_set_texture(mat, ps->texture);
    ((rt_material3d *)mat)->additive_blend = 0;

    if (canvas_owned_storage) {
        int verts_tracked = rt_canvas3d_add_temp_buffer(canvas3d, verts);
        int indices_tracked = rt_canvas3d_add_temp_buffer(canvas3d, indices);
        if (!verts_tracked || !indices_tracked) {
            if (verts_tracked) {
                rt_canvas3d_remove_temp_buffer(canvas3d, verts);
                free(verts);
            }
            if (indices_tracked) {
                rt_canvas3d_remove_temp_buffer(canvas3d, indices);
                free(indices);
            }
            if (!verts_tracked)
                free(verts);
            if (!indices_tracked)
                free(indices);
            if (mat && rt_obj_release_check0(mat))
                rt_obj_free(mat);
            return;
        }
    }

    double model[16];
    particles3d_origin_model_matrix(origin, model);
    rt_material3d_set_alpha(mat, 1.0);
    rt_material3d_set_alpha_mode(mat, RT_MATERIAL3D_ALPHA_MODE_BLEND);
    ((rt_material3d *)mat)->additive_blend = ps->additive_blend ? 1 : 0;
    ((rt_material3d *)mat)->soft_fade = ps->softness;
    rt_canvas3d_draw_mesh_matrix(canvas3d, &tmp_mesh, model, mat);
    if (canvas_owned_storage && mat && rt_obj_release_check0(mat))
        rt_obj_free(mat);
}

#else
typedef int rt_graphics_disabled_tu_guard;
#endif /* VIPER_ENABLE_GRAPHICS */
