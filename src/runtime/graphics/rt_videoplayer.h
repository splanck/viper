//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_videoplayer.h
// Purpose: Video playback runtime class — loads AVI (MJPEG) and OGG/Theora
//   containers, decodes video frames to Pixels, and presents them for software
//   rendering.
//
// Key invariants:
//   - Frame pool: 2 Pixels objects (display + decode), swapped on advance.
//   - AVI sync: frame-rate-based (no timestamps in AVI).
//   - OGG/Theora sync follows packet granule positions; when a Vorbis stream is
//     present in the same container, VideoPlayer coordinates audio playback via
//     the runtime music API.
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

/// @brief Open a video file (AVI/MJPEG or OGG/Theora) and return a player handle (NULL on failure).
void *rt_videoplayer_open(void *path);
/// @brief Begin or resume playback (decoding advances on each Update).
void rt_videoplayer_play(void *vp);
/// @brief Pause playback (decoding halts; current frame remains displayed).
void rt_videoplayer_pause(void *vp);
/// @brief Stop playback and rewind to the start.
void rt_videoplayer_stop(void *vp);
/// @brief Seek to @p seconds into the stream (clamped to [0, duration]).
void rt_videoplayer_seek(void *vp, double seconds);
/// @brief Advance decoding by @p dt seconds (call once per game frame).
void rt_videoplayer_update(void *vp, double dt);
/// @brief Set the audio track volume (0.0 = mute, 1.0 = full).
void rt_videoplayer_set_volume(void *vp, double vol);
/// @brief Get the video frame width in pixels.
int64_t rt_videoplayer_get_width(void *vp);
/// @brief Get the video frame height in pixels.
int64_t rt_videoplayer_get_height(void *vp);
/// @brief Get the total stream duration in seconds.
double rt_videoplayer_get_duration(void *vp);
/// @brief Get the current playback position in seconds.
double rt_videoplayer_get_position(void *vp);
/// @brief 1 if currently playing, 0 if paused/stopped/at-end.
int64_t rt_videoplayer_get_is_playing(void *vp);
/// @brief Borrow the current display Pixels frame (NULL before the first decode completes).
void *rt_videoplayer_get_frame(void *vp);

#ifdef __cplusplus
}
#endif
