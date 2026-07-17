//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/audio/rt_soundsource3d.h
// Purpose: Gameplay-facing spatial source object for 3D audio.
//
//===----------------------------------------------------------------------===//

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Create a 3D audio source bound to a Sound asset.
void *rt_soundsource3d_new(void *sound);

/// @brief Get the source's world-space position as a Vec3.
void *rt_soundsource3d_get_position(void *source);
/// @brief Set the source's position from a Vec3 handle.
void rt_soundsource3d_set_position(void *source, void *position);
/// @brief Set the source's position from raw scalar coordinates.
void rt_soundsource3d_set_position_vec(void *source, double x, double y, double z);
/// @brief Shift an unbound source's position by a floating-origin rebase delta (subtracts it).
void rt_soundsource3d_rebase_origin(void *source, double dx, double dy, double dz);

/// @brief Get the source's velocity as a Vec3 (used for Doppler shift).
void *rt_soundsource3d_get_velocity(void *source);
/// @brief Set the source's velocity vector.
void rt_soundsource3d_set_velocity(void *source, void *velocity);
/// @brief Get the latest Doppler pitch factor computed from listener/source velocity.
double rt_soundsource3d_get_doppler_factor(void *source);

/// @brief Get the falloff radius beyond which the source is inaudible.
double rt_soundsource3d_get_max_distance(void *source);
/// @brief Set the falloff radius (linear attenuation between source and listener).
void rt_soundsource3d_set_max_distance(void *source, double max_distance);
/// @brief Get the full-volume reference distance used before linear falloff begins.
double rt_soundsource3d_get_ref_distance(void *source);
/// @brief Set the full-volume reference distance used before linear falloff begins.
void rt_soundsource3d_set_ref_distance(void *source, double ref_distance);

/// @brief Get the source's pre-attenuation volume (0–100).
int64_t rt_soundsource3d_get_volume(void *source);
/// @brief Set the source's pre-attenuation volume (clamped to 0–100).
void rt_soundsource3d_set_volume(void *source, int64_t volume);

/// @brief Get the user playback-rate multiplier (1.0 default; composes with Doppler).
double rt_soundsource3d_get_pitch(void *source);
/// @brief Set the user playback-rate multiplier (applies immediately to a live voice).
void rt_soundsource3d_set_pitch(void *source, double pitch);
/// @brief Get the occlusion amount (0 open .. 1 fully occluded).
double rt_soundsource3d_get_occlusion(void *source);
/// @brief Set the occlusion amount (game-driven; the mixer smooths ~80 ms).
void rt_soundsource3d_set_occlusion(void *source, double amount);
/// @brief Route future playback voices to a mix group (applies from next play).
void rt_soundsource3d_set_mix_group(void *source, int64_t group);
/// @brief Get the mix group future playback voices route to.
int64_t rt_soundsource3d_get_mix_group(void *source);

/// @brief True if the source loops automatically when its sound finishes.
int8_t rt_soundsource3d_get_looping(void *source);
/// @brief Toggle looping playback.
void rt_soundsource3d_set_looping(void *source, int8_t looping);

/// @brief True if the source is currently producing audio.
int8_t rt_soundsource3d_get_is_playing(void *source);
/// @brief Get the underlying voice ID for direct mixer control (0 if not playing).
int64_t rt_soundsource3d_get_voice_id(void *source);

/// @brief Start playback. Returns the assigned voice ID, or 0 on failure.
int64_t rt_soundsource3d_play(void *source);
/// @brief Stop playback (releases the voice slot).
void rt_soundsource3d_stop(void *source);

/// @brief Bind the source to a SceneNode3D so its position follows the node's transform each frame.
void rt_soundsource3d_bind_node(void *source, void *node);
/// @brief Detach the source from any bound node.
void rt_soundsource3d_clear_node_binding(void *source);

#ifdef __cplusplus
}
#endif
