//===----------------------------------------------------------------------===//
// Tests for SceneManager with transitions (Plan 08).
//===----------------------------------------------------------------------===//

#include "tests/TestHarness.hpp"

#include <cstring>

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

TEST(SceneManager, UnknownSceneNoOp) {
    void *mgr = rt_scenemanager_new();
    rt_scenemanager_add(mgr, (void *)rt_const_cstr("menu"));
    rt_scenemanager_switch(mgr, (void *)rt_const_cstr("nonexistent")); // no crash
    EXPECT_TRUE(rt_scenemanager_is_scene(mgr, (void *)rt_const_cstr("menu")));
}

int main() {
    return viper_test::run_all_tests();
}
