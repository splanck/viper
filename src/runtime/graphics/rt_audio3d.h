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
extern "C"
{
#endif

    void    rt_audio3d_set_listener(void *position, void *forward);
    int64_t rt_audio3d_play_at(void *sound, void *position, double max_distance, int64_t volume);
    void    rt_audio3d_update_voice(int64_t voice, void *position);

#ifdef __cplusplus
}
#endif
