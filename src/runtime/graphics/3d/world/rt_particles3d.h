//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_particles3d.h
// Purpose: 3D particle system with emitter-based spawning, physics (gravity,
//   velocity), lifetime management, size/color/alpha interpolation, and
//   camera-facing billboard rendering with batched draw calls.
//
// Key invariants:
//   - Particles are pooled (pre-allocated, no per-frame allocation).
//   - All particles batched into a single draw call per Draw().
//   - Billboards face the camera using view matrix right/up vectors.
//   - Alpha blend mode sorts particles back-to-front; additive is unordered.
//
// Links: plans/3d/17-particle-system.md, rt_canvas3d.h
//
//===----------------------------------------------------------------------===//
#pragma once

#include "rt_string.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Create a particle emitter pre-allocated for at most @p max_particles concurrent particles.
void *rt_particles3d_new(int64_t max_particles);
/// @brief Set the emitter origin in world space.
void rt_particles3d_set_position(void *obj, double x, double y, double z);
/// @brief Set the base emission direction and a cone spread angle in radians.
void rt_particles3d_set_direction(void *obj, double dx, double dy, double dz, double spread);
/// @brief Set the per-particle speed range (m/s) — random uniform sample at spawn.
void rt_particles3d_set_speed(void *obj, double min_speed, double max_speed);
/// @brief Set the per-particle lifetime range in seconds.
void rt_particles3d_set_lifetime(void *obj, double min_life, double max_life);
/// @brief Set particle size at spawn vs death (lerped over lifetime).
void rt_particles3d_set_size(void *obj, double start_size, double end_size);
/// @brief Set the world-space gravity acceleration applied to every particle.
void rt_particles3d_set_gravity(void *obj, double gx, double gy, double gz);
/// @brief Set particle color at spawn vs death (lerped over lifetime).
void rt_particles3d_set_color(void *obj, int64_t start_color, int64_t end_color);
/// @brief Set particle alpha at spawn vs death.
void rt_particles3d_set_alpha(void *obj, double start_alpha, double end_alpha);
/// @brief Set the steady-state spawn rate while emitting (particles per second).
void rt_particles3d_set_rate(void *obj, double particles_per_second);
/// @brief Toggle additive blending (1 = additive, 0 = alpha blend with back-to-front sort).
void rt_particles3d_set_additive(void *obj, int8_t additive);
/// @brief Bind a Pixels texture for billboard rendering (NULL = solid color quads).
void rt_particles3d_set_texture(void *obj, void *pixels);
/// @brief Set the emitter volume shape (point, box, sphere, cone — see RT_EMITTER3D_SHAPE_*).
void rt_particles3d_set_emitter_shape(void *obj, int64_t shape);
/// @brief Set the emitter shape's per-axis dimensions (interpretation depends on shape).
void rt_particles3d_set_emitter_size(void *obj, double sx, double sy, double sz);
/// @brief Start continuous emission at the configured rate.
void rt_particles3d_start(void *obj);
/// @brief Stop continuous emission (existing particles continue until their lifetime expires).
void rt_particles3d_stop(void *obj);
/// @brief Spawn @p count particles immediately as a one-shot burst.
void rt_particles3d_burst(void *obj, int64_t count);
/// @brief Despawn every active particle and reset the emitter timer.
void rt_particles3d_clear(void *obj);
/// @brief Advance every active particle by @p delta_time and spawn new ones if emitting.
void rt_particles3d_update(void *obj, double delta_time);
/// @brief Render all active particles as camera-facing billboards in a single batched draw.
void rt_particles3d_draw(void *obj, void *canvas3d, void *camera);
/// @brief Number of currently-alive particles.
int64_t rt_particles3d_get_count(void *obj);
/// @brief True if the emitter is in continuous-emission mode (between Start and Stop).
int8_t rt_particles3d_get_emitting(void *obj);

#ifdef __cplusplus
}
#endif
