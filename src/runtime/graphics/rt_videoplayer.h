//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_videoplayer.h
// Purpose: Video playback runtime class — loads AVI (MJPEG) files, decodes
//   video frames to Pixels, and presents them for software rendering.
//
// Key invariants:
//   - Frame pool: 2 Pixels objects (display + decode), swapped on advance.
//   - AVI sync: frame-rate-based (no timestamps in AVI).
//   - OGG/Theora is not exposed as a working runtime surface yet.
//   - Update(dt) must be called each game frame to advance decode.
//   - Frame property returns current display Pixels (may be NULL before first decode).
//
// Links: rt_avi.h, rt_pixels.h, rt_canvas3d.h
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void *rt_videoplayer_open(void *path);
void rt_videoplayer_play(void *vp);
void rt_videoplayer_pause(void *vp);
void rt_videoplayer_stop(void *vp);
void rt_videoplayer_seek(void *vp, double seconds);
void rt_videoplayer_update(void *vp, double dt);
void rt_videoplayer_set_volume(void *vp, double vol);
int64_t rt_videoplayer_get_width(void *vp);
int64_t rt_videoplayer_get_height(void *vp);
double rt_videoplayer_get_duration(void *vp);
double rt_videoplayer_get_position(void *vp);
int64_t rt_videoplayer_get_is_playing(void *vp);
void *rt_videoplayer_get_frame(void *vp);

#ifdef __cplusplus
}
#endif
