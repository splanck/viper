//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_audiolistener3d.h
// Purpose: Gameplay-facing active-listener object for 3D audio.
//
//===----------------------------------------------------------------------===//

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Create a 3D audio listener (positions audio relative to this listener while active).
void *rt_audiolistener3d_new(void);

/// @brief Get the listener's world position as a Vec3.
void *rt_audiolistener3d_get_position(void *listener);
/// @brief Set the listener's position from a Vec3 handle.
void rt_audiolistener3d_set_position(void *listener, void *position);
/// @brief Set the listener's position from raw scalar coordinates.
void rt_audiolistener3d_set_position_vec(void *listener, double x, double y, double z);

/// @brief Get the listener's forward direction as a Vec3.
void *rt_audiolistener3d_get_forward(void *listener);
/// @brief Set the listener's forward direction.
void rt_audiolistener3d_set_forward(void *listener, void *forward);

/// @brief Get the listener's velocity (used for Doppler shift).
void *rt_audiolistener3d_get_velocity(void *listener);
/// @brief Set the listener's velocity.
void rt_audiolistener3d_set_velocity(void *listener, void *velocity);

/// @brief True if this listener is the currently-active spatial-audio listener.
int8_t rt_audiolistener3d_get_is_active(void *listener);
/// @brief Promote/demote this listener to/from active. Only one active listener at a time.
void rt_audiolistener3d_set_is_active(void *listener, int8_t active);

/// @brief Bind to a SceneNode3D; listener follows the node's transform each frame.
void rt_audiolistener3d_bind_node(void *listener, void *node);
/// @brief Detach from any bound scene node.
void rt_audiolistener3d_clear_node_binding(void *listener);
/// @brief Bind to a Camera3D; listener follows the camera's view position/forward each frame.
void rt_audiolistener3d_bind_camera(void *listener, void *camera);
/// @brief Detach from any bound camera.
void rt_audiolistener3d_clear_camera_binding(void *listener);

#ifdef __cplusplus
}
#endif
