//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/audio/rt_sound3d.h
// Purpose: Spatial audio — distance attenuation and stereo panning based on
//   3D listener and source positions. Wraps the existing 2D audio API.
//
// Links: rt_audio.h, rt_vec3.h
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Snapshot of a listener's spatial pose for voice-parameter math: world
///   position, orthonormal forward/right/up basis, velocity (Doppler), and a `valid` flag.
typedef struct rt_sound3d_listener_state {
    double position[3];
    double forward[3];
    double right[3];
    double up[3];
    double velocity[3];
    int8_t valid;
} rt_sound3d_listener_state;

/// @brief Reset @p state to the canonical identity (origin, -Z forward, +X right, +Y up).
void rt_sound3d_listener_state_identity(rt_sound3d_listener_state *state);
/// @brief Populate @p state from explicit position/forward/velocity arrays (any may be NULL).
void rt_sound3d_listener_state_set(rt_sound3d_listener_state *state,
                                   const double *position,
                                   const double *forward,
                                   const double *velocity);
/// @brief Populate @p state from explicit position/orientation/velocity arrays.
void rt_sound3d_listener_state_set_pose(rt_sound3d_listener_state *state,
                                        const double *position,
                                        const double *forward,
                                        const double *up,
                                        const double *velocity);
/// @brief Read the listener-state currently driving spatial audio (active or fallback).
void rt_sound3d_get_effective_listener_state(rt_sound3d_listener_state *out_state);
/// @brief Promote a listener-state snapshot to the active spatial-audio listener.
void rt_sound3d_set_active_listener_state(const rt_sound3d_listener_state *state);
/// @brief Detach the active SoundListener3D and revert to the fallback listener.
void rt_sound3d_clear_active_listener_state(void);
/// @brief Compute distance-attenuated volume + stereo pan for a 3D source given a listener state.
void rt_sound3d_compute_voice_params(const rt_sound3d_listener_state *listener,
                                     const double *source_position,
                                     double max_distance,
                                     int64_t base_volume,
                                     int64_t *out_volume,
                                     int64_t *out_pan);
/// @brief Extended spatial calculation with reference distance and Doppler factor output.
void rt_sound3d_compute_voice_params_ex(const rt_sound3d_listener_state *listener,
                                        const double *source_position,
                                        const double *source_velocity,
                                        double ref_distance,
                                        double max_distance,
                                        int64_t base_volume,
                                        int64_t *out_volume,
                                        int64_t *out_pan,
                                        double *out_doppler);
/// @brief Register an existing voice for later SpatialAudio3D.UpdateVoice fallback lookups.
void rt_sound3d_register_voice(int64_t voice, double max_distance, int64_t base_volume);
/// @brief Register an existing voice with explicit reference/max falloff distances.
void rt_sound3d_register_voice_ex(int64_t voice,
                                  double ref_distance,
                                  double max_distance,
                                  int64_t base_volume);
/// @brief Return the number of occupied entries in Sound3D's fixed voice metadata table.
int64_t rt_sound3d_tracked_voice_count(void);
/// @brief Return the maximum number of Sound3D voices whose metadata can be tracked.
int64_t rt_sound3d_tracked_voice_capacity(void);

/// @brief Set the fallback listener position and orientation (Vec3 handles).
void rt_sound3d_set_listener(void *position, void *forward);
/// @brief Play a sound at a 3D position with linear distance attenuation; returns voice ID.
int64_t rt_sound3d_play_at(void *sound, void *position, double max_distance, int64_t volume);
/// @brief Update a playing voice's volume and pan based on its current 3D position.
void rt_sound3d_update_voice(int64_t voice, void *position, double max_distance);
/// @brief Update a playing voice using explicit source velocity and falloff radii.
void rt_sound3d_update_voice_ex(int64_t voice,
                                void *position,
                                const double *source_velocity,
                                double ref_distance,
                                double max_distance);
/// @brief Push positions of any SoundSource3D objects bound to scene nodes (called per frame).
void rt_sound3d_sync_bindings(double dt);

#ifdef __cplusplus
}
#endif
