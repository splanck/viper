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

void *rt_audiolistener3d_new(void);

void *rt_audiolistener3d_get_position(void *listener);
void rt_audiolistener3d_set_position(void *listener, void *position);
void rt_audiolistener3d_set_position_vec(void *listener, double x, double y, double z);

void *rt_audiolistener3d_get_forward(void *listener);
void rt_audiolistener3d_set_forward(void *listener, void *forward);

void *rt_audiolistener3d_get_velocity(void *listener);
void rt_audiolistener3d_set_velocity(void *listener, void *velocity);

int8_t rt_audiolistener3d_get_is_active(void *listener);
void rt_audiolistener3d_set_is_active(void *listener, int8_t active);

void rt_audiolistener3d_bind_node(void *listener, void *node);
void rt_audiolistener3d_clear_node_binding(void *listener);
void rt_audiolistener3d_bind_camera(void *listener, void *camera);
void rt_audiolistener3d_clear_camera_binding(void *listener);

#ifdef __cplusplus
}
#endif
