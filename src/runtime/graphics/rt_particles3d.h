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

void *rt_particles3d_new(int64_t max_particles);
void rt_particles3d_set_position(void *obj, double x, double y, double z);
void rt_particles3d_set_direction(void *obj, double dx, double dy, double dz, double spread);
void rt_particles3d_set_speed(void *obj, double min_speed, double max_speed);
void rt_particles3d_set_lifetime(void *obj, double min_life, double max_life);
void rt_particles3d_set_size(void *obj, double start_size, double end_size);
void rt_particles3d_set_gravity(void *obj, double gx, double gy, double gz);
void rt_particles3d_set_color(void *obj, int64_t start_color, int64_t end_color);
void rt_particles3d_set_alpha(void *obj, double start_alpha, double end_alpha);
void rt_particles3d_set_rate(void *obj, double particles_per_second);
void rt_particles3d_set_additive(void *obj, int8_t additive);
void rt_particles3d_set_texture(void *obj, void *pixels);
void rt_particles3d_set_emitter_shape(void *obj, int64_t shape);
void rt_particles3d_set_emitter_size(void *obj, double sx, double sy, double sz);
void rt_particles3d_start(void *obj);
void rt_particles3d_stop(void *obj);
void rt_particles3d_burst(void *obj, int64_t count);
void rt_particles3d_clear(void *obj);
void rt_particles3d_update(void *obj, double delta_time);
void rt_particles3d_draw(void *obj, void *canvas3d, void *camera);
int64_t rt_particles3d_get_count(void *obj);
int8_t rt_particles3d_get_emitting(void *obj);

#ifdef __cplusplus
}
#endif
