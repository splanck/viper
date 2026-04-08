//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_audio3d.h
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

typedef struct rt_audio3d_listener_state {
    double position[3];
    double forward[3];
    double right[3];
    double velocity[3];
    int8_t valid;
} rt_audio3d_listener_state;

void rt_audio3d_listener_state_identity(rt_audio3d_listener_state *state);
void rt_audio3d_listener_state_set(rt_audio3d_listener_state *state,
                                   const double *position,
                                   const double *forward,
                                   const double *velocity);
void rt_audio3d_get_effective_listener_state(rt_audio3d_listener_state *out_state);
void rt_audio3d_set_active_listener_state(const rt_audio3d_listener_state *state);
void rt_audio3d_clear_active_listener_state(void);
void rt_audio3d_compute_voice_params(const rt_audio3d_listener_state *listener,
                                     const double *source_position,
                                     double max_distance,
                                     int64_t base_volume,
                                     int64_t *out_volume,
                                     int64_t *out_pan);

void rt_audio3d_set_listener(void *position, void *forward);
int64_t rt_audio3d_play_at(void *sound, void *position, double max_distance, int64_t volume);
void rt_audio3d_update_voice(int64_t voice, void *position, double max_distance);
void rt_audio3d_sync_bindings(double dt);

#ifdef __cplusplus
}
#endif
