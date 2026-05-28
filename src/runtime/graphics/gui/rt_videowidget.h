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

#include "rt_string.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Create a video-playback widget under @p parent that loads the file at @p path.
void *rt_videowidget_new(void *parent, rt_string path);
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
/// @brief Return 1 when transport controls are visible.
int64_t rt_videowidget_get_show_controls(void *vw);
/// @brief Toggle automatic restart when the video reaches its end.
void rt_videowidget_set_loop(void *vw, int8_t loop);
/// @brief Return 1 when automatic looping is enabled.
int64_t rt_videowidget_get_loop(void *vw);
/// @brief Set audio playback volume (0.0 = mute, 1.0 = full).
void rt_videowidget_set_volume(void *vw, double vol);
/// @brief 1 if currently playing, 0 if paused/stopped.
int64_t rt_videowidget_get_is_playing(void *vw);
/// @brief Get the current playback position in seconds.
double rt_videowidget_get_position(void *vw);
/// @brief Get the total stream duration in seconds.
double rt_videowidget_get_duration(void *vw);
/// @brief Return the internal root widget so callers can compose or inspect layout.
void *rt_videowidget_get_root(void *vw);
// Proxy common Widget APIs through to the internal root widget so a VideoWidget
// can be laid out like any other widget.
/// @brief Show or hide the whole widget.
void rt_videowidget_set_visible(void *vw, int64_t visible);
/// @brief Enable or disable interaction with the widget.
void rt_videowidget_set_enabled(void *vw, int64_t enabled);
/// @brief Set the widget's fixed pixel size.
void rt_videowidget_set_size(void *vw, int64_t width, int64_t height);
/// @brief Set the preferred (natural) size used during flex layout.
void rt_videowidget_set_preferred_size(void *vw, double width, double height);
/// @brief Cap the widget's maximum size during layout.
void rt_videowidget_set_max_size(void *vw, double width, double height);
/// @brief Set the flex grow factor for distributing extra space in a flex container.
void rt_videowidget_set_flex(void *vw, double flex);
/// @brief Set the outer margin (in pixels) around the widget.
void rt_videowidget_set_margin(void *vw, int64_t margin);
/// @brief Set the widget's position relative to its parent.
void rt_videowidget_set_position(void *vw, int64_t x, int64_t y);
/// @brief Append a child widget to the internal root container.
void rt_videowidget_add_child(void *vw, void *child);

#ifdef __cplusplus
}
#endif
