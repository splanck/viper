//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file rt_particle.h
/// @brief Simple particle system for visual effects.
///
/// Provides a particle emitter for creating effects like explosions, sparks,
/// smoke, and other visual elements in games.
///
//===----------------------------------------------------------------------===//

#ifndef VIPER_RT_PARTICLE_H
#define VIPER_RT_PARTICLE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// Maximum particles per emitter.
#define RT_PARTICLE_MAX 1024

/// Opaque handle to a ParticleEmitter instance.
typedef struct rt_particle_emitter_impl *rt_particle_emitter;

/// Creates a new ParticleEmitter.
/// @param max_particles Maximum number of particles (up to RT_PARTICLE_MAX).
/// @return A new ParticleEmitter instance.
rt_particle_emitter rt_particle_emitter_new(int64_t max_particles);

/// Destroys a ParticleEmitter and frees its memory.
/// @param emitter The emitter to destroy.
void rt_particle_emitter_destroy(rt_particle_emitter emitter);

/// Sets the emitter position.
/// @param emitter The emitter.
/// @param x X coordinate.
/// @param y Y coordinate.
void rt_particle_emitter_set_position(rt_particle_emitter emitter, double x, double y);

/// Gets the emitter X position.
/// @param emitter The emitter.
/// @return X coordinate.
double rt_particle_emitter_x(rt_particle_emitter emitter);

/// Gets the emitter Y position.
/// @param emitter The emitter.
/// @return Y coordinate.
double rt_particle_emitter_y(rt_particle_emitter emitter);

/// Sets the emission rate (particles per frame).
/// @param emitter The emitter.
/// @param rate Particles to emit per frame (can be fractional).
void rt_particle_emitter_set_rate(rt_particle_emitter emitter, double rate);

/// Gets the emission rate.
/// @param emitter The emitter.
/// @return Particles per frame.
double rt_particle_emitter_rate(rt_particle_emitter emitter);

/// Sets particle lifetime range.
/// @param emitter The emitter.
/// @param min_frames Minimum lifetime in frames.
/// @param max_frames Maximum lifetime in frames.
void rt_particle_emitter_set_lifetime(rt_particle_emitter emitter, int64_t min_frames, int64_t max_frames);

/// Sets particle initial velocity range.
/// @param emitter The emitter.
/// @param min_speed Minimum speed.
/// @param max_speed Maximum speed.
/// @param min_angle Minimum angle in degrees (0 = right, 90 = up).
/// @param max_angle Maximum angle in degrees.
void rt_particle_emitter_set_velocity(rt_particle_emitter emitter, double min_speed, double max_speed,
                                      double min_angle, double max_angle);

/// Sets gravity affecting particles.
/// @param emitter The emitter.
/// @param gx Gravity X component (per frame squared).
/// @param gy Gravity Y component (per frame squared).
void rt_particle_emitter_set_gravity(rt_particle_emitter emitter, double gx, double gy);

/// Sets particle color (ARGB format).
/// @param emitter The emitter.
/// @param color Color in 0xAARRGGBB format.
void rt_particle_emitter_set_color(rt_particle_emitter emitter, int64_t color);

/// Sets particle size range.
/// @param emitter The emitter.
/// @param min_size Minimum size in pixels.
/// @param max_size Maximum size in pixels.
void rt_particle_emitter_set_size(rt_particle_emitter emitter, double min_size, double max_size);

/// Sets whether particles fade out over lifetime.
/// @param emitter The emitter.
/// @param fade_out 1 to enable fade out, 0 to disable.
void rt_particle_emitter_set_fade_out(rt_particle_emitter emitter, int8_t fade_out);

/// Sets whether particles shrink over lifetime.
/// @param emitter The emitter.
/// @param shrink 1 to enable shrinking, 0 to disable.
void rt_particle_emitter_set_shrink(rt_particle_emitter emitter, int8_t shrink);

/// Starts or resumes emission.
/// @param emitter The emitter.
void rt_particle_emitter_start(rt_particle_emitter emitter);

/// Stops emission (existing particles continue).
/// @param emitter The emitter.
void rt_particle_emitter_stop(rt_particle_emitter emitter);

/// Checks if emitter is currently emitting.
/// @param emitter The emitter.
/// @return 1 if emitting, 0 if stopped.
int8_t rt_particle_emitter_is_emitting(rt_particle_emitter emitter);

/// Emits a burst of particles immediately.
/// @param emitter The emitter.
/// @param count Number of particles to emit.
void rt_particle_emitter_burst(rt_particle_emitter emitter, int64_t count);

/// Updates all particles by one frame.
/// @param emitter The emitter.
void rt_particle_emitter_update(rt_particle_emitter emitter);

/// Gets the number of active particles.
/// @param emitter The emitter.
/// @return Number of living particles.
int64_t rt_particle_emitter_count(rt_particle_emitter emitter);

/// Clears all particles.
/// @param emitter The emitter.
void rt_particle_emitter_clear(rt_particle_emitter emitter);

/// Gets particle data for rendering.
/// Index must be < count(). Returns 0 if invalid.
/// @param emitter The emitter.
/// @param index Particle index (0 to count-1).
/// @param out_x Output: X position.
/// @param out_y Output: Y position.
/// @param out_size Output: Current size.
/// @param out_color Output: Current color with alpha.
/// @return 1 if valid, 0 if index out of range.
int8_t rt_particle_emitter_get(rt_particle_emitter emitter, int64_t index, double *out_x, double *out_y,
                               double *out_size, int64_t *out_color);

#ifdef __cplusplus
}
#endif

#endif // VIPER_RT_PARTICLE_H
