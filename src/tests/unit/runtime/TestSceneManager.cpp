//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/unit/runtime/TestSceneManager.cpp
// Purpose: Verifies named scene registration, switching, and timed transitions.
// Key invariants:
//   - Switching changes the active scene only when the target is registered.
//   - Release-sized scene registries retain entries beyond the legacy 16-scene cap.
// Ownership/Lifetime:
//   - Each test owns its SceneManager for the duration of one test case.
//   - Runtime strings passed to Add and Switch are copied or immutable.
// Links: src/runtime/game/rt_scenemanager.c, docs/zannalib/game/scenemanager.md
//
//===----------------------------------------------------------------------===//

#include "tests/TestHarness.hpp"

#include <cstdio>
#include <cstring>
#include <string>

extern "C" {
#include "rt_string.h"
void *rt_scenemanager_new(void);
void rt_scenemanager_add(void *mgr, void *name);
void rt_scenemanager_switch(void *mgr, void *name);
void rt_scenemanager_switch_transition(void *mgr, void *name, int64_t dur);
void rt_scenemanager_update(void *mgr, int64_t dt);
void *rt_scenemanager_current(void *mgr);
int8_t rt_scenemanager_is_scene(void *mgr, void *name);
int8_t rt_scenemanager_just_entered(void *mgr);
int8_t rt_scenemanager_just_exited(void *mgr);
int8_t rt_scenemanager_is_transitioning(void *mgr);
double rt_scenemanager_transition_progress(void *mgr);
rt_string rt_const_cstr(const char *s);
const char *rt_string_cstr(rt_string s);
}

TEST(SceneManager, AddAndSwitch) {
    void *mgr = rt_scenemanager_new();
    rt_scenemanager_add(mgr, (void *)rt_const_cstr("menu"));
    rt_scenemanager_add(mgr, (void *)rt_const_cstr("game"));
    EXPECT_TRUE(rt_scenemanager_is_scene(mgr, (void *)rt_const_cstr("menu")));

    rt_scenemanager_switch(mgr, (void *)rt_const_cstr("game"));
    EXPECT_TRUE(rt_scenemanager_is_scene(mgr, (void *)rt_const_cstr("game")));
}

TEST(SceneManager, SupportsReleaseSizedSceneRegistries) {
    void *mgr = rt_scenemanager_new();
    char name[32];
    for (int i = 0; i < 24; ++i) {
        std::snprintf(name, sizeof(name), "scene-%d", i);
        rt_scenemanager_add(mgr, (void *)rt_const_cstr(name));
    }

    rt_scenemanager_switch(mgr, (void *)rt_const_cstr("scene-23"));

    EXPECT_TRUE(rt_scenemanager_is_scene(mgr, (void *)rt_const_cstr("scene-23")));
}

TEST(SceneManager, JustEnteredOnSwitch) {
    void *mgr = rt_scenemanager_new();
    rt_scenemanager_add(mgr, (void *)rt_const_cstr("a"));
    rt_scenemanager_add(mgr, (void *)rt_const_cstr("b"));
    rt_scenemanager_update(mgr, 16); // clear initial flags
    rt_scenemanager_switch(mgr, (void *)rt_const_cstr("b"));
    EXPECT_TRUE(rt_scenemanager_just_entered(mgr));
    EXPECT_TRUE(rt_scenemanager_just_exited(mgr));
}

TEST(SceneManager, TransitionTimerCountdown) {
    void *mgr = rt_scenemanager_new();
    rt_scenemanager_add(mgr, (void *)rt_const_cstr("a"));
    rt_scenemanager_add(mgr, (void *)rt_const_cstr("b"));
    rt_scenemanager_switch_transition(mgr, (void *)rt_const_cstr("b"), 500);
    EXPECT_TRUE(rt_scenemanager_is_transitioning(mgr));
    EXPECT_TRUE(rt_scenemanager_is_scene(mgr, (void *)rt_const_cstr("a"))); // still on "a"

    // After 500ms, should have switched
    for (int i = 0; i < 32; i++) // 32 * 16ms = 512ms
        rt_scenemanager_update(mgr, 16);
    EXPECT_FALSE(rt_scenemanager_is_transitioning(mgr));
    EXPECT_TRUE(rt_scenemanager_is_scene(mgr, (void *)rt_const_cstr("b")));
}

TEST(SceneManager, TransitionProgress) {
    void *mgr = rt_scenemanager_new();
    rt_scenemanager_add(mgr, (void *)rt_const_cstr("a"));
    rt_scenemanager_add(mgr, (void *)rt_const_cstr("b"));
    rt_scenemanager_switch_transition(mgr, (void *)rt_const_cstr("b"), 100);
    rt_scenemanager_update(mgr, 50); // half way
    double p = rt_scenemanager_transition_progress(mgr);
    EXPECT_TRUE(p > 0.4 && p < 0.6);
}

TEST(SceneManager, NegativeDeltaDoesNotAdvanceTransition) {
    void *mgr = rt_scenemanager_new();
    rt_scenemanager_add(mgr, (void *)rt_const_cstr("a"));
    rt_scenemanager_add(mgr, (void *)rt_const_cstr("b"));

    rt_scenemanager_switch_transition(mgr, (void *)rt_const_cstr("b"), 100);
    rt_scenemanager_update(mgr, -50);

    EXPECT_TRUE(rt_scenemanager_is_transitioning(mgr));
    EXPECT_EQ(rt_scenemanager_transition_progress(mgr), 0.0);

    rt_scenemanager_update(mgr, 50);
    EXPECT_TRUE(rt_scenemanager_is_transitioning(mgr));
    double p = rt_scenemanager_transition_progress(mgr);
    EXPECT_TRUE(p > 0.4 && p < 0.6);
}

TEST(SceneManager, TransitionProgressReachesOneOnCompletionTick) {
    void *mgr = rt_scenemanager_new();
    rt_scenemanager_add(mgr, (void *)rt_const_cstr("a"));
    rt_scenemanager_add(mgr, (void *)rt_const_cstr("b"));

    rt_scenemanager_switch_transition(mgr, (void *)rt_const_cstr("b"), 100);
    rt_scenemanager_update(mgr, 100);

    EXPECT_FALSE(rt_scenemanager_is_transitioning(mgr));
    EXPECT_EQ(rt_scenemanager_transition_progress(mgr), 1.0);
    EXPECT_TRUE(rt_scenemanager_is_scene(mgr, (void *)rt_const_cstr("b")));

    rt_scenemanager_update(mgr, 16);
    EXPECT_EQ(rt_scenemanager_transition_progress(mgr), 0.0);
}

TEST(SceneManager, UnknownSceneNoOp) {
    void *mgr = rt_scenemanager_new();
    rt_scenemanager_add(mgr, (void *)rt_const_cstr("menu"));
    rt_scenemanager_switch(mgr, (void *)rt_const_cstr("nonexistent")); // no crash
    EXPECT_TRUE(rt_scenemanager_is_scene(mgr, (void *)rt_const_cstr("menu")));
}

TEST(SceneManager, TransitionToCurrentSceneIsNoOp) {
    void *mgr = rt_scenemanager_new();
    rt_scenemanager_add(mgr, (void *)rt_const_cstr("menu"));
    rt_scenemanager_add(mgr, (void *)rt_const_cstr("game"));

    rt_scenemanager_update(mgr, 16); // clear initial just_entered
    rt_scenemanager_switch_transition(mgr, (void *)rt_const_cstr("menu"), 250);

    EXPECT_FALSE(rt_scenemanager_is_transitioning(mgr));
    EXPECT_TRUE(rt_scenemanager_is_scene(mgr, (void *)rt_const_cstr("menu")));
    EXPECT_FALSE(rt_scenemanager_just_entered(mgr));
    EXPECT_FALSE(rt_scenemanager_just_exited(mgr));
}

TEST(SceneManager, DuplicateSceneNamesAreIgnored) {
    void *mgr = rt_scenemanager_new();
    rt_scenemanager_add(mgr, (void *)rt_const_cstr("menu"));
    rt_scenemanager_add(mgr, (void *)rt_const_cstr("menu"));
    rt_scenemanager_add(mgr, (void *)rt_const_cstr("game"));

    rt_scenemanager_switch(mgr, (void *)rt_const_cstr("game"));
    EXPECT_TRUE(rt_scenemanager_is_scene(mgr, (void *)rt_const_cstr("game")));

    rt_scenemanager_switch(mgr, (void *)rt_const_cstr("menu"));
    EXPECT_TRUE(rt_scenemanager_is_scene(mgr, (void *)rt_const_cstr("menu")));
}

TEST(SceneManager, LongSceneNamesCanBeSwitchedByOriginalName) {
    const char *longName = "gameplay_checkpoint_after_tutorial_wave_three";
    void *mgr = rt_scenemanager_new();
    rt_scenemanager_add(mgr, (void *)rt_const_cstr("menu"));
    rt_scenemanager_add(mgr, (void *)rt_const_cstr(longName));

    rt_scenemanager_switch(mgr, (void *)rt_const_cstr(longName));

    EXPECT_TRUE(rt_scenemanager_is_scene(mgr, (void *)rt_const_cstr(longName)));
    EXPECT_EQ(std::strcmp(rt_string_cstr((rt_string)rt_scenemanager_current(mgr)), longName), 0);
}

// VDOC-243: names longer than the 127-byte buffer are rejected outright rather
// than truncated, so two distinct long names sharing a 127-byte prefix cannot
// alias to the same registered scene under the strcmp lookup.
TEST(SceneManager, OverlongSceneNamesAreRejectedNotAliased) {
    void *mgr = rt_scenemanager_new();

    // A 127-byte name fits exactly and is accepted.
    std::string fits(127, 'a');
    rt_scenemanager_add(mgr, (void *)rt_const_cstr(fits.c_str()));
    EXPECT_TRUE(rt_scenemanager_is_scene(mgr, (void *)rt_const_cstr(fits.c_str())));

    // Two 128-byte names sharing the first 127 bytes: previously both truncated to
    // the same key so the second was silently deduped. Now both are rejected, so
    // neither registers and they never alias each other.
    std::string prefix(127, 'b');
    std::string nameA = prefix + "1";
    std::string nameB = prefix + "2";
    rt_scenemanager_add(mgr, (void *)rt_const_cstr(nameA.c_str()));
    rt_scenemanager_add(mgr, (void *)rt_const_cstr(nameB.c_str()));
    EXPECT_FALSE(rt_scenemanager_is_scene(mgr, (void *)rt_const_cstr(nameA.c_str())));
    EXPECT_FALSE(rt_scenemanager_is_scene(mgr, (void *)rt_const_cstr(nameB.c_str())));

    // Switching to an overlong name is a no-op, so the current scene never becomes
    // a prefix-colliding alias.
    rt_scenemanager_switch(mgr, (void *)rt_const_cstr(nameA.c_str()));
    EXPECT_FALSE(rt_scenemanager_is_scene(mgr, (void *)rt_const_cstr(nameB.c_str())));
}

int main() {
    return zanna_test::run_all_tests();
}
