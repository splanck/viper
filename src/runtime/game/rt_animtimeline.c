//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//

#include "rt_animtimeline.h"

#include "rt_object.h"
#include "rt_trap.h"

#include <string.h>

#define RT_ANIMTIMELINE_MAX_TRACKS 16
#define RT_ANIMTIMELINE_MAX_EVENTS 32

typedef enum {
    TIMELINE_TRACK_ANIM = 0,
    TIMELINE_TRACK_TWEEN = 1,
    TIMELINE_TRACK_MARKER = 2,
} timeline_track_kind_t;

typedef struct {
    char name[32];
    timeline_track_kind_t kind;
    int64_t start_frame;
    int64_t duration_frames;
    int64_t payload_a;
    int64_t payload_b;
    int64_t payload_c;
} rt_animtimeline_track_t;

typedef struct {
    int64_t frame;
    int64_t event_id;
    uint8_t fired;
} rt_animtimeline_event_t;

typedef struct {
    void *vptr;
    int64_t total_duration_frames;
    int64_t current_frame;
    rt_animtimeline_track_t tracks[RT_ANIMTIMELINE_MAX_TRACKS];
    int64_t track_count;
    rt_animtimeline_event_t events[RT_ANIMTIMELINE_MAX_EVENTS];
    int64_t event_count;
    int64_t events_fired_ids[RT_ANIMTIMELINE_MAX_EVENTS];
    int64_t events_fired_count;
    int8_t playing;
    int8_t looping;
    int8_t finished;
} rt_animtimeline_impl;

/// @brief Safe-cast a handle to the AnimTimeline impl, trapping @p api on a
///        class-id mismatch. @return The impl, or NULL if @p ptr is NULL.
static rt_animtimeline_impl *checked_timeline(void *ptr, const char *api) {
    if (!ptr)
        return NULL;
    if (rt_obj_class_id(ptr) != RT_ANIMTIMELINE_CLASS_ID) {
        rt_trap(api);
        return NULL;
    }
    return (rt_animtimeline_impl *)ptr;
}

/// @brief Clamp a frame index to be non-negative.
static int64_t clamp_frame(int64_t frame) {
    return frame < 0 ? 0 : frame;
}

/// @brief Saturating int64 addition (clamps to INT64_MIN/MAX on overflow).
static int64_t timeline_add_sat_i64(int64_t a, int64_t b) {
    if (b > 0 && a > INT64_MAX - b)
        return INT64_MAX;
    if (b < 0 && a < INT64_MIN - b)
        return INT64_MIN;
    return a + b;
}

/// @brief End frame of a track (start + duration, saturating). 0 if NULL.
static int64_t timeline_track_end_frame(const rt_animtimeline_track_t *track) {
    if (!track)
        return 0;
    return timeline_add_sat_i64(track->start_frame, track->duration_frames);
}

/// @brief Copy a runtime string into a fixed @p cap char buffer, NUL-terminated
///        and truncated to fit. Empty buffer on NULL @p name.
static void timeline_copy_name(char *dst, size_t cap, rt_string name) {
    if (!dst || cap == 0)
        return;
    dst[0] = '\0';
    if (!name)
        return;
    const char *s = rt_string_cstr(name);
    int64_t len = rt_str_len(name);
    if (len < 0)
        len = 0;
    if ((size_t)len >= cap)
        len = (int64_t)cap - 1;
    memcpy(dst, s, (size_t)len);
    dst[len] = '\0';
}

/// @brief Append a track of @p kind with the given span and payloads.
/// @details Clamps start frame to >= 0 and duration to >= 1; copies @p name
///          into the track's inline buffer. @return new track index, or -1 if
///          @p tl is NULL or the per-timeline track cap is reached.
static int64_t timeline_add_track(rt_animtimeline_impl *tl,
                                  timeline_track_kind_t kind,
                                  rt_string name,
                                  int64_t start_frame,
                                  int64_t duration_frames,
                                  int64_t a,
                                  int64_t b,
                                  int64_t c) {
    if (!tl)
        return -1;
    if (tl->track_count >= RT_ANIMTIMELINE_MAX_TRACKS)
        return -1;
    int64_t idx = tl->track_count++;
    rt_animtimeline_track_t *track = &tl->tracks[idx];
    memset(track, 0, sizeof(*track));
    timeline_copy_name(track->name, sizeof(track->name), name);
    track->kind = kind;
    track->start_frame = clamp_frame(start_frame);
    track->duration_frames = duration_frames <= 0 ? 1 : duration_frames;
    track->payload_a = a;
    track->payload_b = b;
    track->payload_c = c;
    return idx;
}

void *rt_animtimeline_new(int64_t total_duration_frames) {
    rt_animtimeline_impl *tl = (rt_animtimeline_impl *)rt_obj_new_i64(
        RT_ANIMTIMELINE_CLASS_ID, (int64_t)sizeof(rt_animtimeline_impl));
    if (!tl)
        return NULL;
    memset(tl, 0, sizeof(*tl));
    tl->total_duration_frames = total_duration_frames <= 0 ? 1 : total_duration_frames;
    return tl;
}

int64_t rt_animtimeline_add_anim_track(
    void *ptr, rt_string name, int64_t start_frame, int64_t duration_frames, int64_t anim_state_id) {
    return timeline_add_track(checked_timeline(ptr, "AnimTimeline.AddAnimTrack: expected AnimTimeline"),
                              TIMELINE_TRACK_ANIM,
                              name,
                              start_frame,
                              duration_frames,
                              anim_state_id,
                              0,
                              0);
}

int64_t rt_animtimeline_add_tween_track(void *ptr,
                                        rt_string name,
                                        int64_t start_frame,
                                        int64_t duration_frames,
                                        int64_t from,
                                        int64_t to) {
    return timeline_add_track(checked_timeline(ptr, "AnimTimeline.AddTweenTrack: expected AnimTimeline"),
                              TIMELINE_TRACK_TWEEN,
                              name,
                              start_frame,
                              duration_frames,
                              from,
                              to,
                              0);
}

int64_t rt_animtimeline_add_marker(void *ptr, int64_t frame, int64_t marker_id) {
    rt_animtimeline_impl *tl =
        checked_timeline(ptr, "AnimTimeline.AddMarker: expected AnimTimeline");
    if (!tl)
        return -1;
    if (tl->event_count >= RT_ANIMTIMELINE_MAX_EVENTS)
        return -1;
    int64_t idx = tl->event_count++;
    tl->events[idx].frame = clamp_frame(frame);
    tl->events[idx].event_id = marker_id;
    tl->events[idx].fired = 0;
    return idx;
}

void rt_animtimeline_play(void *ptr) {
    rt_animtimeline_impl *tl = checked_timeline(ptr, "AnimTimeline.Play: expected AnimTimeline");
    if (!tl)
        return;
    tl->playing = 1;
    tl->finished = 0;
}

void rt_animtimeline_pause(void *ptr) {
    rt_animtimeline_impl *tl = checked_timeline(ptr, "AnimTimeline.Pause: expected AnimTimeline");
    if (tl)
        tl->playing = 0;
}

void rt_animtimeline_stop(void *ptr) {
    rt_animtimeline_impl *tl = checked_timeline(ptr, "AnimTimeline.Stop: expected AnimTimeline");
    if (!tl)
        return;
    tl->playing = 0;
    tl->finished = 0;
    tl->current_frame = 0;
    tl->events_fired_count = 0;
    for (int64_t i = 0; i < tl->event_count; i++)
        tl->events[i].fired = 0;
}

int8_t rt_animtimeline_is_playing(void *ptr) {
    rt_animtimeline_impl *tl =
        checked_timeline(ptr, "AnimTimeline.IsPlaying: expected AnimTimeline");
    return tl ? tl->playing : 0;
}

int8_t rt_animtimeline_is_finished(void *ptr) {
    rt_animtimeline_impl *tl =
        checked_timeline(ptr, "AnimTimeline.IsFinished: expected AnimTimeline");
    return tl ? tl->finished : 0;
}

int64_t rt_animtimeline_get_current_frame(void *ptr) {
    rt_animtimeline_impl *tl =
        checked_timeline(ptr, "AnimTimeline.CurrentFrame: expected AnimTimeline");
    return tl ? tl->current_frame : 0;
}

void rt_animtimeline_set_looping(void *ptr, int8_t loop) {
    rt_animtimeline_impl *tl =
        checked_timeline(ptr, "AnimTimeline.SetLooping: expected AnimTimeline");
    if (tl)
        tl->looping = loop ? 1 : 0;
}

/// @brief True if @p frame lies in the half-open span (before, after] — used
///        to detect markers crossed during an advance step.
static int8_t timeline_crossed(int64_t before, int64_t after, int64_t frame) {
    return frame > before && frame <= after;
}

void rt_animtimeline_advance(void *ptr, int64_t delta_frames) {
    rt_animtimeline_impl *tl = checked_timeline(ptr, "AnimTimeline.Advance: expected AnimTimeline");
    if (!tl || delta_frames <= 0)
        return;
    tl->events_fired_count = 0;
    if (!tl->playing || tl->finished)
        return;

    int64_t before = tl->current_frame;
    int64_t after_unwrapped = timeline_add_sat_i64(before, delta_frames);
    int64_t after = after_unwrapped;
    int8_t wrapped = 0;
    int8_t spanned_full_cycle = delta_frames >= tl->total_duration_frames;
    if (after_unwrapped >= tl->total_duration_frames) {
        if (tl->looping) {
            after = after_unwrapped % tl->total_duration_frames;
            wrapped = 1;
            for (int64_t i = 0; i < tl->event_count; i++)
                tl->events[i].fired = 0;
        } else {
            after = tl->total_duration_frames;
            tl->finished = 1;
            tl->playing = 0;
        }
    }

    for (int64_t i = 0; i < tl->event_count && tl->events_fired_count < RT_ANIMTIMELINE_MAX_EVENTS;
         i++) {
        int8_t fire = 0;
        if (!tl->events[i].fired && timeline_crossed(before, after, tl->events[i].frame))
            fire = 1;
        if (wrapped &&
            (spanned_full_cycle || tl->events[i].frame > before || tl->events[i].frame <= after))
            fire = 1;
        if (!fire)
            continue;
        tl->events[i].fired = 1;
        tl->events_fired_ids[tl->events_fired_count++] = tl->events[i].event_id;
    }
    tl->current_frame = after;
}

int64_t rt_animtimeline_events_fired_count(void *ptr) {
    rt_animtimeline_impl *tl =
        checked_timeline(ptr, "AnimTimeline.EventsFiredCount: expected AnimTimeline");
    return tl ? tl->events_fired_count : 0;
}

int64_t rt_animtimeline_event_fired_id(void *ptr, int64_t index) {
    rt_animtimeline_impl *tl =
        checked_timeline(ptr, "AnimTimeline.EventFiredId: expected AnimTimeline");
    if (!tl || index < 0 || index >= tl->events_fired_count)
        return 0;
    return tl->events_fired_ids[index];
}

/// @brief Bounds-checked track accessor. @return Track at @p index, or NULL.
static rt_animtimeline_track_t *timeline_track(rt_animtimeline_impl *tl, int64_t index) {
    if (!tl || index < 0 || index >= tl->track_count)
        return NULL;
    return &tl->tracks[index];
}

int8_t rt_animtimeline_track_is_active(void *ptr, int64_t track_index) {
    rt_animtimeline_impl *tl =
        checked_timeline(ptr, "AnimTimeline.TrackIsActive: expected AnimTimeline");
    rt_animtimeline_track_t *track = timeline_track(tl, track_index);
    if (!track)
        return 0;
    return tl->current_frame >= track->start_frame &&
           tl->current_frame < timeline_track_end_frame(track);
}

double rt_animtimeline_track_progress(void *ptr, int64_t track_index) {
    rt_animtimeline_impl *tl =
        checked_timeline(ptr, "AnimTimeline.TrackProgress: expected AnimTimeline");
    rt_animtimeline_track_t *track = timeline_track(tl, track_index);
    if (!track)
        return 0.0;
    if (tl->current_frame <= track->start_frame)
        return 0.0;
    int64_t end = timeline_track_end_frame(track);
    if (tl->current_frame >= end)
        return 1.0;
    return (double)(tl->current_frame - track->start_frame) / (double)track->duration_frames;
}

int64_t rt_animtimeline_track_payload_a(void *ptr, int64_t track_index) {
    rt_animtimeline_impl *tl =
        checked_timeline(ptr, "AnimTimeline.TrackPayloadA: expected AnimTimeline");
    rt_animtimeline_track_t *track = timeline_track(tl, track_index);
    return track ? track->payload_a : 0;
}

int64_t rt_animtimeline_track_payload_b(void *ptr, int64_t track_index) {
    rt_animtimeline_impl *tl =
        checked_timeline(ptr, "AnimTimeline.TrackPayloadB: expected AnimTimeline");
    rt_animtimeline_track_t *track = timeline_track(tl, track_index);
    return track ? track->payload_b : 0;
}

int64_t rt_animtimeline_track_payload_c(void *ptr, int64_t track_index) {
    rt_animtimeline_impl *tl =
        checked_timeline(ptr, "AnimTimeline.TrackPayloadC: expected AnimTimeline");
    rt_animtimeline_track_t *track = timeline_track(tl, track_index);
    return track ? track->payload_c : 0;
}
