//===----------------------------------------------------------------------===//
//
// File: src/runtime/collections/rt_particle.h
// Purpose: Particle emitter for visual effects (explosions, sparks, smoke) managing a pool of particles with configurable lifetime, velocity, gravity, color, size, fade, and shrink.
//
// Key invariants:
//   - Maximum particle count is RT_PARTICLE_MAX (1024) per emitter.
//   - rt_particle_emitter_update must be called once per frame.
//   - Particle indices returned by rt_particle_emitter_get are valid only until the next update.
//   - Emission rate is fractional particles-per-frame; sub-integer amounts accumulate.
//
// Ownership/Lifetime:
//   - Caller owns the rt_particle_emitter handle; destroy with rt_particle_emitter_destroy.
//   - Output pointers written by rt_particle_emitter_get are not retained.
//
// Links: src/runtime/collections/rt_particle.c (implementation), src/runtime/collections/rt_screenfx.h
//
//===----------------------------------------------------------------------===//
#ifndef VIPER_RT_PARTICLE_H
#define VIPER_RT_PARTICLE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/// Maximum particles per emitter.
#define RT_PARTICLE_MAX 1024

    /// Opaque handle to a ParticleEmitter instance.
    typedef struct rt_particle_emitter_impl *rt_particle_emitter;

    /// @brief Allocates and initializes a new particle emitter with the given
    ///   pool capacity.
    /// @param max_particles Maximum number of particles this emitter can have
    ///   alive simultaneously. Clamped to [1, RT_PARTICLE_MAX].
    /// @return A new ParticleEmitter handle. The caller must free it with
    ///   rt_particle_emitter_destroy().
    rt_particle_emitter rt_particle_emitter_new(int64_t max_particles);

    /// @brief Destroys a ParticleEmitter and releases all associated memory.
    /// @param emitter The emitter to destroy. Passing NULL is a no-op.
    void rt_particle_emitter_destroy(rt_particle_emitter emitter);

    /// @brief Sets the world-space position from which new particles are spawned.
    /// @param emitter The particle emitter to modify.
    /// @param x X coordinate of the emission origin in world units.
    /// @param y Y coordinate of the emission origin in world units.
    void rt_particle_emitter_set_position(rt_particle_emitter emitter, double x, double y);

    /// @brief Retrieves the X coordinate of the emitter's current position.
    /// @param emitter The particle emitter to query.
    /// @return The X coordinate of the emission origin in world units.
    double rt_particle_emitter_x(rt_particle_emitter emitter);

    /// @brief Retrieves the Y coordinate of the emitter's current position.
    /// @param emitter The particle emitter to query.
    /// @return The Y coordinate of the emission origin in world units.
    double rt_particle_emitter_y(rt_particle_emitter emitter);

    /// @brief Sets the continuous emission rate.
    /// @param emitter The particle emitter to modify.
    /// @param rate Number of particles to spawn per frame. Fractional values
    ///   are accumulated across frames (e.g., 0.5 emits one particle every two
    ///   frames). Set to 0 to disable continuous emission.
    void rt_particle_emitter_set_rate(rt_particle_emitter emitter, double rate);

    /// @brief Retrieves the current continuous emission rate.
    /// @param emitter The particle emitter to query.
    /// @return Particles spawned per frame.
    double rt_particle_emitter_rate(rt_particle_emitter emitter);

    /// @brief Configures the lifetime range for newly spawned particles.
    /// @param emitter The particle emitter to modify.
    /// @param min_frames Minimum lifetime in frames. Must be >= 1.
    /// @param max_frames Maximum lifetime in frames. Must be >= min_frames.
    ///   Each particle's lifetime is chosen randomly within [min, max].
    void rt_particle_emitter_set_lifetime(rt_particle_emitter emitter,
                                          int64_t min_frames,
                                          int64_t max_frames);

    /// @brief Configures the initial velocity range for newly spawned particles.
    /// @param emitter The particle emitter to modify.
    /// @param min_speed Minimum launch speed in world units per frame.
    /// @param max_speed Maximum launch speed in world units per frame.
    /// @param min_angle Minimum launch angle in degrees (0 = rightward,
    ///   90 = upward). Values wrap modulo 360.
    /// @param max_angle Maximum launch angle in degrees. Each particle's angle
    ///   is chosen randomly within [min_angle, max_angle].
    void rt_particle_emitter_set_velocity(rt_particle_emitter emitter,
                                          double min_speed,
                                          double max_speed,
                                          double min_angle,
                                          double max_angle);

    /// @brief Sets the gravity vector applied to all particles each frame.
    /// @param emitter The particle emitter to modify.
    /// @param gx Horizontal gravity component in world units per frame squared.
    /// @param gy Vertical gravity component in world units per frame squared.
    ///   Positive values pull downward.
    void rt_particle_emitter_set_gravity(rt_particle_emitter emitter, double gx, double gy);

    /// @brief Sets the color used for newly spawned particles.
    /// @param emitter The particle emitter to modify.
    /// @param color Color in 0xAARRGGBB format. The alpha channel is the
    ///   starting alpha; it may decrease over time if fade-out is enabled.
    void rt_particle_emitter_set_color(rt_particle_emitter emitter, int64_t color);

    /// @brief Configures the size range for newly spawned particles.
    /// @param emitter The particle emitter to modify.
    /// @param min_size Minimum particle diameter in pixels. Must be > 0.
    /// @param max_size Maximum particle diameter in pixels. Must be >= min_size.
    ///   Each particle's initial size is chosen randomly within [min, max].
    void rt_particle_emitter_set_size(rt_particle_emitter emitter,
                                      double min_size,
                                      double max_size);

    /// @brief Enables or disables alpha fade-out over each particle's lifetime.
    /// @param emitter The particle emitter to modify.
    /// @param fade_out 1 to make particles linearly fade to transparent as they
    ///   age, 0 to keep alpha constant.
    void rt_particle_emitter_set_fade_out(rt_particle_emitter emitter, int8_t fade_out);

    /// @brief Enables or disables size shrinking over each particle's lifetime.
    /// @param emitter The particle emitter to modify.
    /// @param shrink 1 to make particles linearly shrink to zero size as they
    ///   age, 0 to keep size constant.
    void rt_particle_emitter_set_shrink(rt_particle_emitter emitter, int8_t shrink);

    /// @brief Starts or resumes continuous particle emission.
    ///
    /// Particles are spawned each frame at the configured rate until
    /// rt_particle_emitter_stop() is called.
    /// @param emitter The particle emitter to start.
    void rt_particle_emitter_start(rt_particle_emitter emitter);

    /// @brief Stops continuous emission without clearing existing particles.
    ///
    /// Particles already alive will continue to update and expire naturally.
    /// @param emitter The particle emitter to stop.
    void rt_particle_emitter_stop(rt_particle_emitter emitter);

    /// @brief Queries whether the emitter is currently in the emitting state.
    /// @param emitter The particle emitter to query.
    /// @return 1 if the emitter is actively spawning particles, 0 if stopped.
    int8_t rt_particle_emitter_is_emitting(rt_particle_emitter emitter);

    /// @brief Queries whether fade-out is enabled for this emitter.
    /// @param emitter The particle emitter to query.
    /// @return 1 if particles fade out over their lifetime, 0 otherwise.
    int8_t rt_particle_emitter_fade_out(rt_particle_emitter emitter);

    /// @brief Queries whether size shrinking is enabled for this emitter.
    /// @param emitter The particle emitter to query.
    /// @return 1 if particles shrink over their lifetime, 0 otherwise.
    int8_t rt_particle_emitter_shrink(rt_particle_emitter emitter);

    /// @brief Retrieves the configured particle color.
    /// @param emitter The particle emitter to query.
    /// @return The color in 0xAARRGGBB format.
    int64_t rt_particle_emitter_color(rt_particle_emitter emitter);

    /// @brief Immediately spawns a burst of particles at the current position.
    ///
    /// This is independent of the continuous emission rate. Useful for one-shot
    /// effects like explosions or impact sparks.
    /// @param emitter The particle emitter.
    /// @param count Number of particles to spawn. Clamped to the available pool
    ///   capacity.
    void rt_particle_emitter_burst(rt_particle_emitter emitter, int64_t count);

    /// @brief Advances all live particles by one simulation frame.
    ///
    /// Applies velocity, gravity, aging, fade-out, and shrink. Removes expired
    /// particles. If the emitter is active, spawns new particles at the
    /// configured rate. Must be called once per frame.
    /// @param emitter The particle emitter to update.
    void rt_particle_emitter_update(rt_particle_emitter emitter);

    /// @brief Returns the number of particles currently alive in the emitter.
    /// @param emitter The particle emitter to query.
    /// @return The count of living particles, in [0, max_particles].
    int64_t rt_particle_emitter_count(rt_particle_emitter emitter);

    /// @brief Immediately removes all living particles from the emitter.
    /// @param emitter The particle emitter to clear. Does not change emission
    ///   state (started/stopped) or any configuration.
    void rt_particle_emitter_clear(rt_particle_emitter emitter);

    /// @brief Retrieves rendering data for a single live particle by index.
    ///
    /// The caller should iterate indices from 0 to rt_particle_emitter_count()-1
    /// each frame to draw all particles. Indices are invalidated by the next
    /// call to rt_particle_emitter_update().
    /// @param emitter The particle emitter to query.
    /// @param index Particle index in [0, count - 1].
    /// @param out_x Pointer to receive the particle's X position. Must not be
    ///   NULL.
    /// @param out_y Pointer to receive the particle's Y position. Must not be
    ///   NULL.
    /// @param out_size Pointer to receive the particle's current diameter. Must
    ///   not be NULL.
    /// @param out_color Pointer to receive the particle's current ARGB color
    ///   (with alpha reflecting any fade-out). Must not be NULL.
    /// @return 1 if the index was valid and outputs were written, 0 if the index
    ///   was out of range (outputs are left unchanged).
    int8_t rt_particle_emitter_get(rt_particle_emitter emitter,
                                   int64_t index,
                                   double *out_x,
                                   double *out_y,
                                   double *out_size,
                                   int64_t *out_color);

    /// @brief Batch-render all live particles directly into a Pixels buffer.
    ///
    /// Iterates all live particles in a single pass and draws each as a disc
    /// via rt_pixels_draw_disc(). This eliminates the O(n²) overhead of calling
    /// rt_particle_emitter_get() n times per frame: the cost is now O(n) with a
    /// single tight loop and no per-particle VM dispatch overhead.
    ///
    /// @param emitter The particle emitter to render.
    /// @param pixels  A Pixels object to draw into (must not be NULL).
    /// @param offset_x X offset added to all particle world positions.
    /// @param offset_y Y offset added to all particle world positions.
    /// @return The number of particles drawn.
    int64_t rt_particle_emitter_draw_to_pixels(rt_particle_emitter emitter,
                                               void *pixels,
                                               int64_t offset_x,
                                               int64_t offset_y);

#ifdef __cplusplus
}
#endif

#endif // VIPER_RT_PARTICLE_H
