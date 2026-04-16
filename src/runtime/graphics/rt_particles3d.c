//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_particles3d.c
// Purpose: 3D particle system — emitter spawning, Euler integration physics,
//   lifetime/size/color interpolation, camera-facing billboard quads,
//   batched single-draw-call rendering.
//
// Key invariants:
//   - Dead particles are swapped to end (unstable removal, O(1) per kill).
//   - Billboard quads use camera right/up vectors from the view matrix.
//   - All particles batched into one vertex+index buffer per Draw().
//   - xorshift32 PRNG for deterministic randomization (no stdlib rand).
//
// Links: rt_particles3d.h, plans/3d/17-particle-system.md
//
//===----------------------------------------------------------------------===//

#ifdef VIPER_ENABLE_GRAPHICS

#include "rt_particles3d.h"
#include "rt_canvas3d.h"
#include "rt_canvas3d_internal.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

extern void *rt_obj_new_i64(int64_t class_id, int64_t byte_size);
extern void rt_obj_set_finalizer(void *obj, void (*fn)(void *));
extern void rt_obj_retain_maybe(void *obj);
extern int rt_obj_release_check0(void *obj);
extern void rt_obj_free(void *obj);
#include "rt_trap.h"
extern void rt_canvas3d_add_temp_buffer(void *canvas, void *buffer);

/*==========================================================================
 * Internal types
 *=========================================================================*/

typedef struct {
    float pos[3];
    float vel[3];
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
    void *texture;
    int32_t emitter_shape; /* 0=point, 1=sphere, 2=box */
    double emitter_size[3];
    uint32_t prng_state;   /* per-instance PRNG seed */
    void *cached_material; /* reused across frames (GFX-052) */
} rt_particles3d;

/*==========================================================================
 * xorshift32 PRNG (per-instance, deterministic)
 *=========================================================================*/

static uint32_t xorshift32(rt_particles3d *ps) {
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
    return (float)(lo + randf(ps) * (hi - lo));
}

/*==========================================================================
 * Helpers
 *=========================================================================*/

static void unpack_color(int64_t packed, float *rgb) {
    rgb[0] = (float)((packed >> 16) & 0xFF) / 255.0f;
    rgb[1] = (float)((packed >> 8) & 0xFF) / 255.0f;
    rgb[2] = (float)(packed & 0xFF) / 255.0f;
}

/// @brief Generate a random direction within a cone of half-angle `spread`
///        around the given direction vector.
static void random_cone_dir(rt_particles3d *ps, const double *dir, double spread, float *out) {
    if (spread <= 0.0) {
        out[0] = (float)dir[0];
        out[1] = (float)dir[1];
        out[2] = (float)dir[2];
        return;
    }

    /* Random angle within cone */
    float theta = randf(ps) * (float)spread;
    float phi = randf(ps) * (float)(2.0 * M_PI);

    /* Build a coordinate frame around dir */
    float d[3] = {(float)dir[0], (float)dir[1], (float)dir[2]};
    float dlen = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (dlen > 1e-6f) {
        d[0] /= dlen;
        d[1] /= dlen;
        d[2] /= dlen;
    } else {
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
    float rlen = sqrtf(rx * rx + ry * ry + rz * rz);
    if (rlen > 1e-6f) {
        rx /= rlen;
        ry /= rlen;
        rz /= rlen;
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

    /* Normalize */
    float olen = sqrtf(out[0] * out[0] + out[1] * out[1] + out[2] * out[2]);
    if (olen > 1e-6f) {
        out[0] /= olen;
        out[1] /= olen;
        out[2] /= olen;
    }
}

/*==========================================================================
 * Lifecycle
 *=========================================================================*/

static void rt_particles3d_finalize(void *obj) {
    rt_particles3d *ps = (rt_particles3d *)obj;
    free(ps->particles);
    ps->particles = NULL;
    if (ps->texture && rt_obj_release_check0(ps->texture))
        rt_obj_free(ps->texture);
    ps->texture = NULL;
    if (ps->cached_material && rt_obj_release_check0(ps->cached_material))
        rt_obj_free(ps->cached_material);
    ps->cached_material = NULL;
}

void *rt_particles3d_new(int64_t max_particles) {
    if (max_particles <= 0 || max_particles > 100000) {
        rt_trap("Particles3D.New: max_particles must be 1-100000");
        return NULL;
    }
    rt_particles3d *ps = (rt_particles3d *)rt_obj_new_i64(0, (int64_t)sizeof(rt_particles3d));
    if (!ps) {
        rt_trap("Particles3D.New: memory allocation failed");
        return NULL;
    }

    ps->vptr = NULL;
    ps->particles = (vgfx3d_particle_t *)calloc((size_t)max_particles, sizeof(vgfx3d_particle_t));
    if (!ps->particles) {
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
    ps->texture = NULL;
    ps->emitter_shape = 0;
    ps->emitter_size[0] = ps->emitter_size[1] = ps->emitter_size[2] = 1.0;
    ps->prng_state = (uint32_t)(uintptr_t)ps ^ 0x12345678; /* unique per instance */
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
    if (!o)
        return;
    rt_particles3d *p = (rt_particles3d *)o;
    p->position[0] = x;
    p->position[1] = y;
    p->position[2] = z;
}

/// @brief Set the average emit direction (normalized internally) and the cone half-angle
/// `spread` in radians. spread=0 means perfectly aligned, spread=π means full sphere.
void rt_particles3d_set_direction(void *o, double dx, double dy, double dz, double spread) {
    if (!o)
        return;
    rt_particles3d *p = (rt_particles3d *)o;
    double len = sqrt(dx * dx + dy * dy + dz * dz);
    if (len > 1e-8) {
        p->emit_dir[0] = dx / len;
        p->emit_dir[1] = dy / len;
        p->emit_dir[2] = dz / len;
    }
    p->emit_spread = spread;
}

/// @brief Set the per-particle initial speed range [mn, mx] in world-units/sec (uniform random).
void rt_particles3d_set_speed(void *o, double mn, double mx) {
    if (!o)
        return;
    rt_particles3d *p = (rt_particles3d *)o;
    p->speed_min = mn;
    p->speed_max = mx;
}

/// @brief Set the per-particle lifetime range [mn, mx] in seconds (uniform random per spawn).
void rt_particles3d_set_lifetime(void *o, double mn, double mx) {
    if (!o)
        return;
    rt_particles3d *p = (rt_particles3d *)o;
    p->life_min = mn;
    p->life_max = mx;
}

/// @brief Set the start and end size (interpolated by age) for each particle.
void rt_particles3d_set_size(void *o, double s, double e) {
    if (!o)
        return;
    rt_particles3d *p = (rt_particles3d *)o;
    p->size_start = s;
    p->size_end = e;
}

/// @brief Set the constant acceleration applied to every particle each frame (typical: (0,-9.8,0)).
void rt_particles3d_set_gravity(void *o, double gx, double gy, double gz) {
    if (!o)
        return;
    rt_particles3d *p = (rt_particles3d *)o;
    p->gravity[0] = gx;
    p->gravity[1] = gy;
    p->gravity[2] = gz;
}

/// @brief Set start (`sc`) and end (`ec`) colors as packed 0xRRGGBBAA. Each particle linearly
/// interpolates between them based on age ratio. Alpha component is set separately via `_set_alpha`.
void rt_particles3d_set_color(void *o, int64_t sc, int64_t ec) {
    if (!o)
        return;
    rt_particles3d *p = (rt_particles3d *)o;
    unpack_color(sc, p->color_start);
    unpack_color(ec, p->color_end);
}

/// @brief Set start (`sa`) and end (`ea`) alpha values [0, 1]. Common pattern: 1.0→0.0 for fade-out.
void rt_particles3d_set_alpha(void *o, double sa, double ea) {
    if (!o)
        return;
    rt_particles3d *p = (rt_particles3d *)o;
    p->alpha_start = sa;
    p->alpha_end = ea;
}

/// @brief Set the spawn rate in particles per second. The accumulator pattern emits whole
/// particles when ≥1 worth has accumulated, preserving fractional rates across frames.
void rt_particles3d_set_rate(void *o, double r) {
    if (!o)
        return;
    ((rt_particles3d *)o)->rate = r > 0.0 ? r : 0.0;
}

/// @brief Toggle additive blend mode (1 = additive for fire/glow, 0 = alpha blend for smoke).
/// Additive skips the back-to-front sort since order doesn't affect the result.
void rt_particles3d_set_additive(void *o, int8_t a) {
    if (!o)
        return;
    ((rt_particles3d *)o)->additive_blend = a;
}

/// @brief Set the per-particle billboard texture. NULL produces solid color quads.
void rt_particles3d_set_texture(void *o, void *tex) {
    if (!o)
        return;
    rt_particles3d *ps = (rt_particles3d *)o;
    if (ps->texture == tex)
        return;
    rt_obj_retain_maybe(tex);
    if (ps->texture && rt_obj_release_check0(ps->texture))
        rt_obj_free(ps->texture);
    ps->texture = tex;
}

/// @brief Select the emitter volume: 0 = point (default), 1 = sphere (uniform interior),
/// 2 = box. Combined with `_set_emitter_size` to control the spawn region.
void rt_particles3d_set_emitter_shape(void *o, int64_t s) {
    if (!o)
        return;
    ((rt_particles3d *)o)->emitter_shape = (int32_t)s;
}

/// @brief Set the emitter shape's extent. For sphere: only sx is used (radius); for box: full
/// half-extents per axis. Ignored for point emitter.
void rt_particles3d_set_emitter_size(void *o, double sx, double sy, double sz) {
    if (!o)
        return;
    rt_particles3d *p = (rt_particles3d *)o;
    p->emitter_size[0] = sx;
    p->emitter_size[1] = sy;
    p->emitter_size[2] = sz;
}

/*==========================================================================
 * Playback
 *=========================================================================*/

/// @brief Begin emitting (continuous spawn at the configured rate). Existing live particles
/// continue to update regardless of the emit flag.
void rt_particles3d_start(void *o) {
    if (o)
        ((rt_particles3d *)o)->emitting = 1;
}

/// @brief Stop continuous emission. Existing particles run to natural lifetime; for instant
/// removal use `_clear`.
void rt_particles3d_stop(void *o) {
    if (o)
        ((rt_particles3d *)o)->emitting = 0;
}

/// @brief Kill every live particle and reset the spawn accumulator. Doesn't change emit state.
void rt_particles3d_clear(void *o) {
    if (!o)
        return;
    ((rt_particles3d *)o)->count = 0;
    ((rt_particles3d *)o)->accumulator = 0.0;
}

/// @brief Number of particles currently alive.
int64_t rt_particles3d_get_count(void *o) {
    return o ? ((rt_particles3d *)o)->count : 0;
}

/// @brief Returns 1 if continuous emission is enabled (`_start` called, no subsequent `_stop`).
int8_t rt_particles3d_get_emitting(void *o) {
    return o ? ((rt_particles3d *)o)->emitting : 0;
}

/*==========================================================================
 * Spawning
 *=========================================================================*/

static void spawn_particle(rt_particles3d *ps) {
    if (ps->count >= ps->max_particles)
        return;
    vgfx3d_particle_t *p = &ps->particles[ps->count++];

    /* Position: emitter origin + shape offset */
    p->pos[0] = (float)ps->position[0];
    p->pos[1] = (float)ps->position[1];
    p->pos[2] = (float)ps->position[2];

    if (ps->emitter_shape == 1) /* sphere */
    {
        float r = randf(ps) * (float)ps->emitter_size[0];
        float theta = randf(ps) * (float)(2.0 * M_PI);
        float phi = acosf(1.0f - 2.0f * randf(ps));
        p->pos[0] += r * sinf(phi) * cosf(theta);
        p->pos[1] += r * cosf(phi);
        p->pos[2] += r * sinf(phi) * sinf(theta);
    } else if (ps->emitter_shape == 2) /* box */
    {
        p->pos[0] += (randf(ps) - 0.5f) * 2.0f * (float)ps->emitter_size[0];
        p->pos[1] += (randf(ps) - 0.5f) * 2.0f * (float)ps->emitter_size[1];
        p->pos[2] += (randf(ps) - 0.5f) * 2.0f * (float)ps->emitter_size[2];
    }

    /* Velocity */
    float dir[3];
    random_cone_dir(ps, ps->emit_dir, ps->emit_spread, dir);
    float speed = rand_range(ps, ps->speed_min, ps->speed_max);
    p->vel[0] = dir[0] * speed;
    p->vel[1] = dir[1] * speed;
    p->vel[2] = dir[2] * speed;

    /* Life */
    p->max_life = rand_range(ps, ps->life_min, ps->life_max);
    if (p->max_life < 0.01f)
        p->max_life = 0.01f;
    p->life = p->max_life;

    /* Initial visuals */
    p->size = (float)ps->size_start;
    p->color[0] = ps->color_start[0];
    p->color[1] = ps->color_start[1];
    p->color[2] = ps->color_start[2];
    p->color[3] = (float)ps->alpha_start;
}

/// @brief Spawn `count` particles immediately (in addition to any continuous emission). Useful
/// for explosions, sparks, one-shot effects.
void rt_particles3d_burst(void *o, int64_t count) {
    if (!o || count <= 0)
        return;
    rt_particles3d *ps = (rt_particles3d *)o;
    for (int64_t i = 0; i < count; i++)
        spawn_particle(ps);
}

/*==========================================================================
 * Update (Euler integration + lifetime + interpolation)
 *=========================================================================*/

/// @brief Per-frame tick. Walks every live particle to apply Euler integration (pos += vel*dt,
/// vel += gravity*dt), age-interpolate size/color/alpha, and reap dead ones via O(1) swap-with-
/// last. Then accumulates and emits new particles from the rate while emission is enabled.
void rt_particles3d_update(void *o, double delta_time) {
    if (!o || delta_time <= 0.0)
        return;
    rt_particles3d *ps = (rt_particles3d *)o;
    float dt = (float)delta_time;

    /* Update alive particles */
    for (int32_t i = 0; i < ps->count;) {
        vgfx3d_particle_t *p = &ps->particles[i];
        p->life -= dt;
        if (p->life <= 0.0f) {
            /* Kill: swap with last alive particle (O(1) unstable removal) */
            ps->particles[i] = ps->particles[--ps->count];
            continue;
        }

        /* Physics: pos += vel * dt, vel += gravity * dt */
        p->pos[0] += p->vel[0] * dt;
        p->pos[1] += p->vel[1] * dt;
        p->pos[2] += p->vel[2] * dt;
        p->vel[0] += (float)ps->gravity[0] * dt;
        p->vel[1] += (float)ps->gravity[1] * dt;
        p->vel[2] += (float)ps->gravity[2] * dt;

        /* Interpolate size, color, alpha based on age ratio */
        float age = 1.0f - p->life / p->max_life; /* 0 = birth, 1 = death */
        p->size = (float)ps->size_start + age * (float)(ps->size_end - ps->size_start);
        p->color[0] = ps->color_start[0] + age * (ps->color_end[0] - ps->color_start[0]);
        p->color[1] = ps->color_start[1] + age * (ps->color_end[1] - ps->color_start[1]);
        p->color[2] = ps->color_start[2] + age * (ps->color_end[2] - ps->color_start[2]);
        p->color[3] = (float)ps->alpha_start + age * (float)(ps->alpha_end - ps->alpha_start);

        i++;
    }

    /* Spawn new particles */
    if (ps->emitting && ps->rate > 0.0) {
        double max_budget = (double)(ps->max_particles - ps->count) + 0.999999;
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
 * Billboard rendering (batched single draw call)
 *=========================================================================*/

extern void rt_canvas3d_draw_mesh(void *obj, void *mesh, void *transform, void *material);
extern void *rt_material3d_new(void);
extern void rt_material3d_set_color(void *m, double r, double g, double b);
extern void rt_material3d_set_unlit(void *m, int8_t u);
extern void rt_material3d_set_alpha(void *m, double a);
extern void rt_material3d_set_texture(void *m, void *tex);
extern void *rt_mat4_identity(void);

/// @brief Render every live particle as a camera-facing billboard quad. Extracts right/up from
/// the camera view matrix to build the quads. Sorts back-to-front for alpha blending; skips the
/// sort when in additive mode (order-independent). Submits as a single batched mesh draw.
void rt_particles3d_draw(void *o, void *canvas3d, void *camera) {
    if (!o || !canvas3d || !camera)
        return;
    rt_particles3d *ps = (rt_particles3d *)o;
    if (ps->count == 0)
        return;

    (void)canvas3d; /* used via extern draw calls below */
    rt_camera3d *cam = (rt_camera3d *)camera;

    /* Extract camera right and up vectors from view matrix (row-major).
     * Row 0 = right, Row 1 = up (before translation). */
    float right[3] = {(float)cam->view[0], (float)cam->view[1], (float)cam->view[2]};
    float up[3] = {(float)cam->view[4], (float)cam->view[5], (float)cam->view[6]};

    /* Sort particles back-to-front for alpha blend (skip for additive) */
    if (!ps->additive_blend) {
        float cam_pos[3] = {(float)cam->eye[0], (float)cam->eye[1], (float)cam->eye[2]};
        /* Simple insertion sort (stable, good for nearly-sorted data) */
        for (int32_t i = 1; i < ps->count; i++) {
            vgfx3d_particle_t key = ps->particles[i];
            float ki_dist = (key.pos[0] - cam_pos[0]) * (key.pos[0] - cam_pos[0]) +
                            (key.pos[1] - cam_pos[1]) * (key.pos[1] - cam_pos[1]) +
                            (key.pos[2] - cam_pos[2]) * (key.pos[2] - cam_pos[2]);
            int32_t j = i - 1;
            while (j >= 0) {
                vgfx3d_particle_t *pj = &ps->particles[j];
                float dj = (pj->pos[0] - cam_pos[0]) * (pj->pos[0] - cam_pos[0]) +
                           (pj->pos[1] - cam_pos[1]) * (pj->pos[1] - cam_pos[1]) +
                           (pj->pos[2] - cam_pos[2]) * (pj->pos[2] - cam_pos[2]);
                if (dj >= ki_dist)
                    break;
                ps->particles[j + 1] = ps->particles[j];
                j--;
            }
            ps->particles[j + 1] = key;
        }
    }

    /* Build batched vertex + index buffers */
    uint32_t vert_count = (uint32_t)ps->count * 4;
    uint32_t idx_count = (uint32_t)ps->count * 6;
    vgfx3d_vertex_t *verts = (vgfx3d_vertex_t *)calloc(vert_count, sizeof(vgfx3d_vertex_t));
    uint32_t *indices = (uint32_t *)malloc(idx_count * sizeof(uint32_t));
    if (!verts || !indices) {
        free(verts);
        free(indices);
        return;
    }

    for (int32_t i = 0; i < ps->count; i++) {
        vgfx3d_particle_t *p = &ps->particles[i];
        float hs = p->size * 0.5f;
        uint32_t base = (uint32_t)i * 4;

        /* 4 billboard vertices: center ± right*hs ± up*hs */
        float cx = p->pos[0], cy = p->pos[1], cz = p->pos[2];

        /* v0 = bottom-left, v1 = bottom-right, v2 = top-right, v3 = top-left */
        for (int vi = 0; vi < 4; vi++) {
            float rs = (vi == 1 || vi == 2) ? hs : -hs;
            float us = (vi == 2 || vi == 3) ? hs : -hs;
            vgfx3d_vertex_t *v = &verts[base + vi];
            v->pos[0] = cx + right[0] * rs + up[0] * us;
            v->pos[1] = cy + right[1] * rs + up[1] * us;
            v->pos[2] = cz + right[2] * rs + up[2] * us;
            /* Normal faces camera (forward = cross(right, up)) */
            v->normal[0] = right[1] * up[2] - right[2] * up[1];
            v->normal[1] = right[2] * up[0] - right[0] * up[2];
            v->normal[2] = right[0] * up[1] - right[1] * up[0];
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

        /* 2 triangles per quad (CCW) */
        indices[i * 6 + 0] = base + 0;
        indices[i * 6 + 1] = base + 1;
        indices[i * 6 + 2] = base + 2;
        indices[i * 6 + 3] = base + 0;
        indices[i * 6 + 4] = base + 2;
        indices[i * 6 + 5] = base + 3;
    }

    /* Create a temporary mesh and submit via the normal draw pipeline.
     * Use unlit material with particle alpha for the draw command. */
    rt_mesh3d tmp_mesh;
    memset(&tmp_mesh, 0, sizeof(tmp_mesh));
    tmp_mesh.vertices = verts;
    tmp_mesh.vertex_count = vert_count;
    tmp_mesh.indices = indices;
    tmp_mesh.index_count = idx_count;

    /* Build a lightweight material-like draw command.
     * Particles use the deferred draw queue with alpha < 1.0
     * for proper transparency sorting with scene geometry. */

    if (!ps->cached_material) {
        ps->cached_material = rt_material3d_new();
        rt_material3d_set_color(ps->cached_material, 1.0, 1.0, 1.0);
        rt_material3d_set_unlit(ps->cached_material, 1);
    }
    void *mat = ps->cached_material;
    /* Use average particle alpha for draw sorting (approximate) */
    float avg_alpha = 0.0f;
    for (int32_t i = 0; i < ps->count; i++)
        avg_alpha += ps->particles[i].color[3];
    avg_alpha /= (float)ps->count;
    rt_material3d_set_alpha(mat, (double)avg_alpha);
    rt_material3d_set_texture(mat, ps->texture);

    /* Register buffers for end-of-frame cleanup */
    rt_canvas3d_add_temp_buffer(canvas3d, verts);
    rt_canvas3d_add_temp_buffer(canvas3d, indices);

    rt_canvas3d_draw_mesh(canvas3d, &tmp_mesh, rt_mat4_identity(), mat);
}

#else
typedef int rt_graphics_disabled_tu_guard;
#endif /* VIPER_ENABLE_GRAPHICS */
