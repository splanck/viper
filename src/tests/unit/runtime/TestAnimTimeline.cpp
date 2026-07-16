//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/unit/runtime/TestAnimTimeline.cpp
// Purpose: Tests for AnimTimeline tween-track interpolation (payload C).
//
//===----------------------------------------------------------------------===//

#include "tests/TestHarness.hpp"

#include <cstdint>

extern "C" {
#include "rt_string.h"
void *rt_animtimeline_new(int64_t total_duration_frames);
int64_t rt_animtimeline_add_tween_track(
    void *tl, rt_string name, int64_t start, int64_t dur, int64_t from, int64_t to);
int64_t rt_animtimeline_add_marker(void *tl, int64_t frame, int64_t marker_id);
void rt_animtimeline_play(void *tl);
void rt_animtimeline_advance(void *tl, int64_t delta);
int64_t rt_animtimeline_events_fired_count(void *tl);
int64_t rt_animtimeline_event_fired_id(void *tl, int64_t index);
int64_t rt_animtimeline_track_payload_a(void *tl, int64_t idx);
int64_t rt_animtimeline_track_payload_b(void *tl, int64_t idx);
int64_t rt_animtimeline_track_payload_c(void *tl, int64_t idx);
double rt_animtimeline_track_progress(void *tl, int64_t idx);
void *rt_animation_event_batch_from_ids(const int64_t *ids, int64_t count);
int64_t rt_animation_event_batch_count(void *batch);
rt_string rt_const_cstr(const char *s);
}

// VDOC-277: a tween track's payload C is the CURRENT interpolated value between
// its from (A) and to (B) endpoints, computed on read from the track's progress.
// It used to be a stored literal 0, so "tween track" did no interpolation.
TEST(AnimTimeline, TweenPayloadCInterpolates) {
    void *tl = rt_animtimeline_new(10);
    int64_t t = rt_animtimeline_add_tween_track(tl, rt_const_cstr("move"), 0, 10, 10, 20);
    EXPECT_EQ(t, 0);

    // Raw endpoints stay in A/B.
    EXPECT_EQ(rt_animtimeline_track_payload_a(tl, t), 10);
    EXPECT_EQ(rt_animtimeline_track_payload_b(tl, t), 20);

    // Before playback the value sits at the start endpoint.
    EXPECT_EQ(rt_animtimeline_track_payload_c(tl, t), 10);

    rt_animtimeline_play(tl);
    rt_animtimeline_advance(tl, 5); // halfway through a 10-frame tween
    EXPECT_NEAR(rt_animtimeline_track_progress(tl, t), 0.5, 1e-9);
    EXPECT_EQ(rt_animtimeline_track_payload_c(tl, t), 15); // lerp(10, 20, 0.5)

    rt_animtimeline_advance(tl, 5); // to the end
    EXPECT_EQ(rt_animtimeline_track_payload_c(tl, t), 20); // exact end value
}

// Exact endpoints hold for wide ranges (long double interpolation, VDOC-277/273).
TEST(AnimTimeline, TweenPayloadCPreservesWideEndpoints) {
    const int64_t big = 9007199254740993LL; // 2^53 + 1
    void *tl = rt_animtimeline_new(4);
    int64_t t = rt_animtimeline_add_tween_track(tl, rt_const_cstr("id"), 0, 4, 0, big);

    EXPECT_EQ(rt_animtimeline_track_payload_c(tl, t), 0); // start endpoint exact
    rt_animtimeline_play(tl);
    rt_animtimeline_advance(tl, 4);
    EXPECT_EQ(rt_animtimeline_track_payload_c(tl, t), big); // end endpoint exact, no 2^53 drift
}

// VDOC-278: a marker on the start frame (0) must fire on the first advance, not
// only as a side effect of a later loop wrap. The half-open (before, after]
// crossing used to exclude it because playback starts at frame 0.
TEST(AnimTimeline, FrameZeroMarkerFiresOnFirstAdvance) {
    void *tl = rt_animtimeline_new(5);
    EXPECT_EQ(rt_animtimeline_add_marker(tl, 0, 7), 0);
    rt_animtimeline_play(tl);
    rt_animtimeline_advance(tl, 3);
    EXPECT_EQ(rt_animtimeline_events_fired_count(tl), 1);
    EXPECT_EQ(rt_animtimeline_event_fired_id(tl, 0), 7);
    // It fires once, not again on the next advance within the same cycle.
    rt_animtimeline_advance(tl, 1);
    EXPECT_EQ(rt_animtimeline_events_fired_count(tl), 0);
}

// VDOC-278: markers beyond the total duration can never be crossed, so they are
// rejected at registration instead of registering silently and never firing.
TEST(AnimTimeline, MarkersBeyondDurationAreRejected) {
    void *tl = rt_animtimeline_new(5);
    EXPECT_EQ(rt_animtimeline_add_marker(tl, 8, 88), -1); // beyond duration → rejected
    EXPECT_EQ(rt_animtimeline_add_marker(tl, 5, 55), 0);  // at the end → accepted
    EXPECT_EQ(rt_animtimeline_add_marker(tl, 3, 33), 1);  // interior → accepted

    rt_animtimeline_play(tl);
    rt_animtimeline_advance(tl, 5); // runs to the end, crossing 3 and 5
    // Both accepted markers fire; the rejected one never existed.
    EXPECT_EQ(rt_animtimeline_events_fired_count(tl), 2);
}

// VDOC-279: a fired-ID snapshot fails transactionally — an allocation failure
// returns NULL (distinguishable from a genuine empty frame) instead of a
// misleading empty batch that silently drops the events. The count-overflow guard
// exercises the same failure path that a malloc failure takes.
TEST(AnimationEventBatch, AllocationFailureReturnsNullNotEmptyBatch) {
    int64_t ids[2] = {11, 22};

    // A legitimately empty snapshot is still a valid batch with count 0.
    void *empty = rt_animation_event_batch_from_ids(nullptr, 0);
    ASSERT_TRUE(empty != nullptr);
    EXPECT_EQ(rt_animation_event_batch_count(empty), 0);

    // A populated snapshot round-trips.
    void *full = rt_animation_event_batch_from_ids(ids, 2);
    ASSERT_TRUE(full != nullptr);
    EXPECT_EQ(rt_animation_event_batch_count(full), 2);

    // An impossible allocation (count overflow) fails to NULL, never an empty batch.
    void *overflow = rt_animation_event_batch_from_ids(ids, INT64_MAX);
    EXPECT_TRUE(overflow == nullptr);
}

int main() {
    return viper_test::run_all_tests();
}
