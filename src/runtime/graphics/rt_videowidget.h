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

void *rt_videowidget_new(void *parent, void *path);
void rt_videowidget_play(void *vw);
void rt_videowidget_pause(void *vw);
void rt_videowidget_stop(void *vw);
void rt_videowidget_update(void *vw, double dt);
void rt_videowidget_set_show_controls(void *vw, int8_t show);
void rt_videowidget_set_loop(void *vw, int8_t loop);
void rt_videowidget_set_volume(void *vw, double vol);
int64_t rt_videowidget_get_is_playing(void *vw);
double rt_videowidget_get_position(void *vw);
double rt_videowidget_get_duration(void *vw);

#ifdef __cplusplus
}
#endif
