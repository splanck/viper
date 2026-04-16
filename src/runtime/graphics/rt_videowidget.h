//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_videowidget.h
// Purpose: GUI VideoWidget — wraps VideoPlayer with transport controls,
//   timeline scrubber, and image display in the Viper.GUI widget tree.
//
// Key invariants:
//   - Owns a VideoPlayer internally for decode.
//   - Image widget updated each frame with decoded Pixels.
//   - Slider widget shows playback progress.
//   - Play/Pause/Stop buttons delegate to VideoPlayer.
//
// Links: rt_videoplayer.h, rt_gui.h
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Create a video-playback widget under @p parent that loads the file at @p path.
void *rt_videowidget_new(void *parent, void *path);
/// @brief Destroy the widget subtree and release the owned VideoPlayer immediately.
/// Safe to call multiple times; later calls are no-ops.
void rt_videowidget_destroy(void *vw);
/// @brief Start or resume playback.
void rt_videowidget_play(void *vw);
/// @brief Pause playback (current frame stays displayed).
void rt_videowidget_pause(void *vw);
/// @brief Stop playback and rewind to the start.
void rt_videowidget_stop(void *vw);
/// @brief Tick the underlying VideoPlayer by @p dt seconds and refresh the display image.
void rt_videowidget_update(void *vw, double dt);
/// @brief Show or hide the play/pause/stop transport controls and timeline slider.
void rt_videowidget_set_show_controls(void *vw, int8_t show);
/// @brief Toggle automatic restart when the video reaches its end.
void rt_videowidget_set_loop(void *vw, int8_t loop);
/// @brief Set audio playback volume (0.0 = mute, 1.0 = full).
void rt_videowidget_set_volume(void *vw, double vol);
/// @brief 1 if currently playing, 0 if paused/stopped.
int64_t rt_videowidget_get_is_playing(void *vw);
/// @brief Get the current playback position in seconds.
double rt_videowidget_get_position(void *vw);
/// @brief Get the total stream duration in seconds.
double rt_videowidget_get_duration(void *vw);

#ifdef __cplusplus
}
#endif
