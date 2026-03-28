//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/RTAnimStateTests.cpp
// Purpose: Unit tests for AnimStateMachine (combined state + animation).
//
//===----------------------------------------------------------------------===//

#include "tests/TestHarness.hpp"

extern "C" {
void *rt_animstate_new(void);
void rt_animstate_add_state(void *asm_,
                            int64_t state_id,
                            int64_t start_frame,
                            int64_t end_frame,
                            int64_t frame_duration,
                            int8_t loop);
int8_t rt_animstate_set_initial(void *asm_, int64_t state_id);
int8_t rt_animstate_transition(void *asm_, int64_t state_id);
void rt_animstate_update(void *asm_);
void rt_animstate_clear_flags(void *asm_);
int64_t rt_animstate_current_state(void *asm_);
int64_t rt_animstate_previous_state(void *asm_);
int8_t rt_animstate_just_entered(void *asm_);
int8_t rt_animstate_just_exited(void *asm_);
int64_t rt_animstate_frames_in_state(void *asm_);
int64_t rt_animstate_current_frame(void *asm_);
int8_t rt_animstate_is_anim_finished(void *asm_);
int64_t rt_animstate_progress(void *asm_);
}

TEST(AnimState, CreateAndInitial) {
    void *a = rt_animstate_new();
    ASSERT_TRUE(a != nullptr);

    // No state set yet
    EXPECT_EQ(rt_animstate_current_state(a), -1);
    EXPECT_EQ(rt_animstate_previous_state(a), -1);

    // Add states: IDLE=0 (frames 0-3, dur 6, loop), WALK=1 (frames 4-7, dur 4, loop)
    rt_animstate_add_state(a, 0, 0, 3, 6, 1);
    rt_animstate_add_state(a, 1, 4, 7, 4, 1);

    // Set initial
    EXPECT_EQ(rt_animstate_set_initial(a, 0), 1);
    EXPECT_EQ(rt_animstate_current_state(a), 0);
    EXPECT_EQ(rt_animstate_current_frame(a), 0);
    EXPECT_EQ(rt_animstate_just_entered(a), 1);
}

TEST(AnimState, TransitionChangesClip) {
    void *a = rt_animstate_new();
    rt_animstate_add_state(a, 0, 0, 3, 6, 1); // IDLE
    rt_animstate_add_state(a, 1, 4, 7, 4, 1); // WALK
    rt_animstate_set_initial(a, 0);
    rt_animstate_clear_flags(a);

    // Transition to WALK
    EXPECT_EQ(rt_animstate_transition(a, 1), 1);
    EXPECT_EQ(rt_animstate_current_state(a), 1);
    EXPECT_EQ(rt_animstate_previous_state(a), 0);
    EXPECT_EQ(rt_animstate_current_frame(a), 4); // WALK starts at frame 4
    EXPECT_EQ(rt_animstate_just_entered(a), 1);
    EXPECT_EQ(rt_animstate_just_exited(a), 1);

    // Transition to same state is no-op
    EXPECT_EQ(rt_animstate_transition(a, 1), 0);
}

TEST(AnimState, UpdateAdvancesFrame) {
    void *a = rt_animstate_new();
    // Single frame duration = 2 (advance every 2 updates)
    rt_animstate_add_state(a, 0, 10, 12, 2, 1);
    rt_animstate_set_initial(a, 0);

    EXPECT_EQ(rt_animstate_current_frame(a), 10);

    rt_animstate_update(a);                       // frame_counter = 1
    EXPECT_EQ(rt_animstate_current_frame(a), 10); // not yet

    rt_animstate_update(a); // frame_counter = 2 -> advance
    EXPECT_EQ(rt_animstate_current_frame(a), 11);

    rt_animstate_update(a);
    rt_animstate_update(a); // advance again
    EXPECT_EQ(rt_animstate_current_frame(a), 12);

    // Loop back to start
    rt_animstate_update(a);
    rt_animstate_update(a);
    EXPECT_EQ(rt_animstate_current_frame(a), 10);
}

TEST(AnimState, OneShotFinishes) {
    void *a = rt_animstate_new();
    // One-shot clip: frames 0-2, duration 1, no loop
    rt_animstate_add_state(a, 0, 0, 2, 1, 0);
    rt_animstate_set_initial(a, 0);

    EXPECT_EQ(rt_animstate_is_anim_finished(a), 0);

    rt_animstate_update(a); // frame 0 -> 1
    EXPECT_EQ(rt_animstate_current_frame(a), 1);
    EXPECT_EQ(rt_animstate_is_anim_finished(a), 0);

    rt_animstate_update(a); // frame 1 -> 2
    EXPECT_EQ(rt_animstate_current_frame(a), 2);

    rt_animstate_update(a); // at end, finish
    EXPECT_EQ(rt_animstate_is_anim_finished(a), 1);
    EXPECT_EQ(rt_animstate_current_frame(a), 2); // stays at last frame
}

TEST(AnimState, FramesInStateAndProgress) {
    void *a = rt_animstate_new();
    rt_animstate_add_state(a, 0, 0, 3, 1, 1);
    rt_animstate_set_initial(a, 0);

    EXPECT_EQ(rt_animstate_frames_in_state(a), 0);
    rt_animstate_update(a);
    EXPECT_EQ(rt_animstate_frames_in_state(a), 1);
    rt_animstate_update(a);
    EXPECT_EQ(rt_animstate_frames_in_state(a), 2);

    // Progress: frame 2 of 0-3 = 66%
    EXPECT_EQ(rt_animstate_progress(a), 66);
}

TEST(AnimState, ClearFlags) {
    void *a = rt_animstate_new();
    rt_animstate_add_state(a, 0, 0, 3, 6, 1);
    rt_animstate_add_state(a, 1, 4, 7, 4, 1);
    rt_animstate_set_initial(a, 0);

    EXPECT_EQ(rt_animstate_just_entered(a), 1);
    rt_animstate_clear_flags(a);
    EXPECT_EQ(rt_animstate_just_entered(a), 0);
    EXPECT_EQ(rt_animstate_just_exited(a), 0);

    rt_animstate_transition(a, 1);
    EXPECT_EQ(rt_animstate_just_entered(a), 1);
    EXPECT_EQ(rt_animstate_just_exited(a), 1);
    rt_animstate_clear_flags(a);
    EXPECT_EQ(rt_animstate_just_entered(a), 0);
}

TEST(AnimState, TransitionResetsAnimation) {
    void *a = rt_animstate_new();
    rt_animstate_add_state(a, 0, 0, 3, 1, 1);
    rt_animstate_add_state(a, 1, 10, 15, 1, 1);
    rt_animstate_set_initial(a, 0);

    // Advance a few frames
    rt_animstate_update(a);
    rt_animstate_update(a);
    EXPECT_EQ(rt_animstate_current_frame(a), 2);

    // Transition resets to new clip's start
    rt_animstate_transition(a, 1);
    EXPECT_EQ(rt_animstate_current_frame(a), 10);
    EXPECT_EQ(rt_animstate_is_anim_finished(a), 0);
    EXPECT_EQ(rt_animstate_frames_in_state(a), 0);
}

int main(int argc, char **argv) {
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
