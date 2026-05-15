//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
#pragma once

#include "rt_string.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RT_ANIMTIMELINE_CLASS_ID INT64_C(-0x720101)

void *rt_animtimeline_new(int64_t total_duration_frames);
int64_t rt_animtimeline_add_anim_track(
    void *tl, rt_string name, int64_t start_frame, int64_t duration_frames, int64_t anim_state_id);
int64_t rt_animtimeline_add_tween_track(void *tl,
                                        rt_string name,
                                        int64_t start_frame,
                                        int64_t duration_frames,
                                        int64_t from,
                                        int64_t to);
int64_t rt_animtimeline_add_marker(void *tl, int64_t frame, int64_t marker_id);
void rt_animtimeline_play(void *tl);
void rt_animtimeline_pause(void *tl);
void rt_animtimeline_stop(void *tl);
int8_t rt_animtimeline_is_playing(void *tl);
int8_t rt_animtimeline_is_finished(void *tl);
int64_t rt_animtimeline_get_current_frame(void *tl);
void rt_animtimeline_set_looping(void *tl, int8_t loop);
void rt_animtimeline_advance(void *tl, int64_t delta_frames);
int64_t rt_animtimeline_events_fired_count(void *tl);
int64_t rt_animtimeline_event_fired_id(void *tl, int64_t index);
int8_t rt_animtimeline_track_is_active(void *tl, int64_t track_index);
double rt_animtimeline_track_progress(void *tl, int64_t track_index);
int64_t rt_animtimeline_track_payload_a(void *tl, int64_t track_index);
int64_t rt_animtimeline_track_payload_b(void *tl, int64_t track_index);
int64_t rt_animtimeline_track_payload_c(void *tl, int64_t track_index);

#ifdef __cplusplus
}
#endif
