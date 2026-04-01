//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/unit/runtime/TestAnimStateNamed.cpp
// Purpose: Tests for AnimStateMachine named state API and frame events.
//
//===----------------------------------------------------------------------===//

#include "tests/TestHarness.hpp"

#include <cstring>

extern "C" {
#include "rt_string.h"
void *rt_animstate_new(void);
void rt_animstate_add_named(void *asm_, void *name, int64_t start, int64_t end,
                            int64_t dur, int8_t loop);
void rt_animstate_play(void *asm_, void *name);
void rt_animstate_update(void *asm_);
void rt_animstate_clear_flags(void *asm_);
void *rt_animstate_current_name(void *asm_);
int64_t rt_animstate_current_frame(void *asm_);
int8_t rt_animstate_just_entered(void *asm_);
void rt_animstate_set_event_frame(void *asm_, int64_t frame);
int8_t rt_animstate_event_fired(void *asm_);
int8_t rt_animstate_set_initial(void *asm_, int64_t id);
rt_string rt_const_cstr(const char *s);
const char *rt_string_cstr(rt_string s);
}

TEST(AnimStateNamed, AddAndPlayByName) {
    void *sm = rt_animstate_new();
    rt_animstate_add_named(sm, (void *)rt_const_cstr("walk"), 0, 3, 8, 1);
    rt_animstate_set_initial(sm, 0); // ID 0 = "walk"
    rt_animstate_play(sm, (void *)rt_const_cstr("walk"));
    EXPECT_EQ(rt_animstate_current_frame(sm), 0);
}

TEST(AnimStateNamed, TransitionByName) {
    void *sm = rt_animstate_new();
    rt_animstate_add_named(sm, (void *)rt_const_cstr("idle"), 0, 3, 8, 1);
    rt_animstate_add_named(sm, (void *)rt_const_cstr("run"), 4, 7, 6, 1);
    rt_animstate_set_initial(sm, 0);
    rt_animstate_play(sm, (void *)rt_const_cstr("run"));
    EXPECT_TRUE(rt_animstate_just_entered(sm));
    EXPECT_EQ(rt_animstate_current_frame(sm), 4); // run starts at frame 4
}

TEST(AnimStateNamed, GetStateName) {
    void *sm = rt_animstate_new();
    rt_animstate_add_named(sm, (void *)rt_const_cstr("jump"), 8, 10, 4, 0);
    rt_animstate_set_initial(sm, 0);
    void *name = rt_animstate_current_name(sm);
    ASSERT_TRUE(name != nullptr);
    const char *cname = rt_string_cstr((rt_string)name);
    EXPECT_EQ(strcmp(cname, "jump"), 0);
}

TEST(AnimStateNamed, UnknownNameNoOp) {
    void *sm = rt_animstate_new();
    rt_animstate_add_named(sm, (void *)rt_const_cstr("idle"), 0, 3, 8, 1);
    rt_animstate_set_initial(sm, 0);
    rt_animstate_clear_flags(sm);
    rt_animstate_play(sm, (void *)rt_const_cstr("nonexistent"));
    // Should not crash, state unchanged
    EXPECT_EQ(rt_animstate_current_frame(sm), 0);
}

TEST(AnimStateNamed, EventFrameFires) {
    void *sm = rt_animstate_new();
    rt_animstate_add_named(sm, (void *)rt_const_cstr("attack"), 0, 5, 1, 0);
    rt_animstate_set_initial(sm, 0);
    rt_animstate_set_event_frame(sm, 3);

    // Advance to frame 3
    for (int i = 0; i < 3; i++)
        rt_animstate_update(sm);

    EXPECT_TRUE(rt_animstate_event_fired(sm));
}

TEST(AnimStateNamed, EventAutoClears) {
    void *sm = rt_animstate_new();
    rt_animstate_add_named(sm, (void *)rt_const_cstr("attack"), 0, 5, 1, 0);
    rt_animstate_set_initial(sm, 0);
    rt_animstate_set_event_frame(sm, 1);

    rt_animstate_update(sm); // frame 0 → 1
    EXPECT_TRUE(rt_animstate_event_fired(sm));   // first check: true
    EXPECT_FALSE(rt_animstate_event_fired(sm));  // second check: auto-cleared
}

int main() {
    return viper_test::run_all_tests();
}
