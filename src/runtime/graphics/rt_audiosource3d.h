//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_audiosource3d.h
// Purpose: Gameplay-facing spatial source object for 3D audio.
//
//===----------------------------------------------------------------------===//

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Create a 3D audio source bound to a Sound asset.
void *rt_audiosource3d_new(void *sound);

/// @brief Get the source's world-space position as a Vec3.
void *rt_audiosource3d_get_position(void *source);
/// @brief Set the source's position from a Vec3 handle.
void rt_audiosource3d_set_position(void *source, void *position);
/// @brief Set the source's position from raw scalar coordinates.
void rt_audiosource3d_set_position_vec(void *source, double x, double y, double z);

/// @brief Get the source's velocity as a Vec3 (used for Doppler shift).
void *rt_audiosource3d_get_velocity(void *source);
/// @brief Set the source's velocity vector.
void rt_audiosource3d_set_velocity(void *source, void *velocity);

/// @brief Get the falloff radius beyond which the source is inaudible.
double rt_audiosource3d_get_max_distance(void *source);
/// @brief Set the falloff radius (linear attenuation between source and listener).
void rt_audiosource3d_set_max_distance(void *source, double max_distance);

/// @brief Get the source's pre-attenuation volume (0–100).
int64_t rt_audiosource3d_get_volume(void *source);
/// @brief Set the source's pre-attenuation volume (clamped to 0–100).
void rt_audiosource3d_set_volume(void *source, int64_t volume);

/// @brief True if the source loops automatically when its sound finishes.
int8_t rt_audiosource3d_get_looping(void *source);
/// @brief Toggle looping playback.
void rt_audiosource3d_set_looping(void *source, int8_t looping);

/// @brief True if the source is currently producing audio.
int8_t rt_audiosource3d_get_is_playing(void *source);
/// @brief Get the underlying voice ID for direct mixer control (0 if not playing).
int64_t rt_audiosource3d_get_voice_id(void *source);

/// @brief Start playback. Returns the assigned voice ID, or 0 on failure.
int64_t rt_audiosource3d_play(void *source);
/// @brief Stop playback (releases the voice slot).
void rt_audiosource3d_stop(void *source);

/// @brief Bind the source to a SceneNode3D so its position follows the node's transform each frame.
void rt_audiosource3d_bind_node(void *source, void *node);
/// @brief Detach the source from any bound node.
void rt_audiosource3d_clear_node_binding(void *source);

#ifdef __cplusplus
}
#endif
