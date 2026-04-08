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

void *rt_audiosource3d_new(void *sound);

void *rt_audiosource3d_get_position(void *source);
void rt_audiosource3d_set_position(void *source, void *position);
void rt_audiosource3d_set_position_vec(void *source, double x, double y, double z);

void *rt_audiosource3d_get_velocity(void *source);
void rt_audiosource3d_set_velocity(void *source, void *velocity);

double rt_audiosource3d_get_max_distance(void *source);
void rt_audiosource3d_set_max_distance(void *source, double max_distance);

int64_t rt_audiosource3d_get_volume(void *source);
void rt_audiosource3d_set_volume(void *source, int64_t volume);

int8_t rt_audiosource3d_get_looping(void *source);
void rt_audiosource3d_set_looping(void *source, int8_t looping);

int8_t rt_audiosource3d_get_is_playing(void *source);
int64_t rt_audiosource3d_get_voice_id(void *source);

int64_t rt_audiosource3d_play(void *source);
void rt_audiosource3d_stop(void *source);

void rt_audiosource3d_bind_node(void *source, void *node);
void rt_audiosource3d_clear_node_binding(void *source);

#ifdef __cplusplus
}
#endif
