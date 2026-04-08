//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/unit/test_rt_animcontroller3d.cpp
// Purpose: Unit tests for AnimController3D state playback, events, root motion,
//   and masked overlay layers.
//
//===----------------------------------------------------------------------===//

#include "rt_animcontroller3d.h"
#include "rt_skeleton3d.h"
#include "rt_string.h"
#include <cmath>
#include <cstdio>
#include <cstring>

extern "C" {
extern void *rt_vec3_new(double x, double y, double z);
extern double rt_vec3_x(void *v);
extern double rt_vec3_y(void *v);
extern double rt_vec3_z(void *v);
extern void *rt_quat_new(double x, double y, double z, double w);
extern void *rt_mat4_identity(void);
extern double rt_mat4_get(void *m, int64_t row, int64_t col);
extern rt_string rt_const_cstr(const char *s);
extern const char *rt_string_cstr(rt_string s);
}

static int tests_passed = 0;
static int tests_run = 0;

#define EXPECT_TRUE(cond, msg)                                                                     \
    do {                                                                                           \
        tests_run++;                                                                               \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "FAIL: %s\n", msg);                                                    \
        } else {                                                                                   \
            tests_passed++;                                                                        \
        }                                                                                          \
    } while (0)

#define EXPECT_NEAR(a, b, eps, msg)                                                                \
    do {                                                                                           \
        tests_run++;                                                                               \
        if (fabs((double)(a) - (double)(b)) > (eps)) {                                             \
            fprintf(stderr, "FAIL: %s (got %f, expected %f)\n", msg, (double)(a), (double)(b));    \
        } else {                                                                                   \
            tests_passed++;                                                                        \
        }                                                                                          \
    } while (0)

static void *make_anim(const char *name,
                       int64_t bone_index,
                       double x0,
                       double y0,
                       double z0,
                       double x1,
                       double y1,
                       double z1) {
    void *anim = rt_animation3d_new(rt_const_cstr(name), 1.0);
    void *pos0 = rt_vec3_new(x0, y0, z0);
    void *pos1 = rt_vec3_new(x1, y1, z1);
    void *rot = rt_quat_new(0.0, 0.0, 0.0, 1.0);
    void *scl = rt_vec3_new(1.0, 1.0, 1.0);
    rt_animation3d_set_looping(anim, 1);
    rt_animation3d_add_keyframe(anim, bone_index, 0.0, pos0, rot, scl);
    rt_animation3d_add_keyframe(anim, bone_index, 1.0, pos1, rot, scl);
    return anim;
}

static void test_controller_state_flow() {
    void *skel = rt_skeleton3d_new();
    void *controller;
    void *idle;
    void *walk;
    void *delta;
    void *root_mat;
    const char *event_name;

    rt_skeleton3d_add_bone(skel, rt_const_cstr("root"), -1, rt_mat4_identity());
    rt_skeleton3d_add_bone(skel, rt_const_cstr("arm"), 0, rt_mat4_identity());
    rt_skeleton3d_compute_inverse_bind(skel);

    idle = make_anim("idle", 0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0);
    walk = make_anim("walk", 0, 0.0, 0.0, 0.0, 10.0, 0.0, 0.0);

    controller = rt_anim_controller3d_new(skel);
    EXPECT_TRUE(controller != nullptr, "AnimController3D.New returns non-null");
    EXPECT_TRUE(rt_anim_controller3d_add_state(controller, rt_const_cstr("idle"), idle) == 0,
                "AddState idle returns index 0");
    EXPECT_TRUE(rt_anim_controller3d_add_state(controller, rt_const_cstr("walk"), walk) == 1,
                "AddState walk returns index 1");
    EXPECT_TRUE(rt_anim_controller3d_add_transition(
                    controller, rt_const_cstr("idle"), rt_const_cstr("walk"), 0.25) == 1,
                "AddTransition idle->walk succeeds");
    rt_anim_controller3d_add_event(controller, rt_const_cstr("walk"), 0.5, rt_const_cstr("step"));

    EXPECT_TRUE(rt_anim_controller3d_play(controller, rt_const_cstr("idle")) == 1,
                "Play idle succeeds");
    EXPECT_TRUE(strcmp(rt_string_cstr(rt_anim_controller3d_get_current_state(controller)), "idle") ==
                    0,
                "CurrentState = idle");

    EXPECT_TRUE(rt_anim_controller3d_play(controller, rt_const_cstr("walk")) == 1,
                "Play walk succeeds");
    EXPECT_TRUE(rt_anim_controller3d_get_is_transitioning(controller) == 1,
                "Play walk uses default transition");
    EXPECT_TRUE(strcmp(rt_string_cstr(rt_anim_controller3d_get_previous_state(controller)), "idle") ==
                    0,
                "PreviousState = idle");

    rt_anim_controller3d_update(controller, 0.5);
    root_mat = rt_anim_controller3d_get_bone_matrix(controller, 0);
    EXPECT_TRUE(root_mat != nullptr, "GetBoneMatrix(root) returns matrix");
    EXPECT_NEAR(rt_mat4_get(root_mat, 0, 3), 5.0, 0.1, "Root motion sampled at x=5");

    delta = rt_anim_controller3d_consume_root_motion(controller);
    EXPECT_NEAR(rt_vec3_x(delta), 5.0, 0.1, "ConsumeRootMotion returns root x delta");
    EXPECT_NEAR(rt_vec3_y(delta), 0.0, 0.01, "ConsumeRootMotion returns root y delta");

    delta = rt_anim_controller3d_get_root_motion_delta(controller);
    EXPECT_NEAR(rt_vec3_x(delta), 0.0, 0.01, "RootMotionDelta clears after consume");

    event_name = rt_string_cstr(rt_anim_controller3d_poll_event(controller));
    EXPECT_TRUE(event_name && strcmp(event_name, "step") == 0, "PollEvent returns queued step");
    EXPECT_TRUE(strcmp(rt_string_cstr(rt_anim_controller3d_poll_event(controller)), "") == 0,
                "PollEvent returns empty string when queue drained");
}

static void test_controller_masked_layer() {
    void *skel = rt_skeleton3d_new();
    void *controller;
    void *walk;
    void *wave;
    void *root_mat;
    void *arm_mat;
    int64_t arm_bone;

    rt_skeleton3d_add_bone(skel, rt_const_cstr("root"), -1, rt_mat4_identity());
    arm_bone = rt_skeleton3d_add_bone(skel, rt_const_cstr("arm"), 0, rt_mat4_identity());
    rt_skeleton3d_compute_inverse_bind(skel);

    walk = make_anim("walk", 0, 0.0, 0.0, 0.0, 10.0, 0.0, 0.0);
    wave = make_anim("wave", arm_bone, 0.0, 0.0, 0.0, 0.0, 2.0, 0.0);

    controller = rt_anim_controller3d_new(skel);
    rt_anim_controller3d_add_state(controller, rt_const_cstr("walk"), walk);
    rt_anim_controller3d_add_state(controller, rt_const_cstr("wave"), wave);
    rt_anim_controller3d_play(controller, rt_const_cstr("walk"));
    rt_anim_controller3d_set_layer_mask(controller, 1, arm_bone);
    rt_anim_controller3d_set_layer_weight(controller, 1, 1.0);
    EXPECT_TRUE(rt_anim_controller3d_play_layer(controller, 1, rt_const_cstr("wave")) == 1,
                "PlayLayer(layer=1,wave) succeeds");

    rt_anim_controller3d_update(controller, 0.5);
    root_mat = rt_anim_controller3d_get_bone_matrix(controller, 0);
    arm_mat = rt_anim_controller3d_get_bone_matrix(controller, arm_bone);

    EXPECT_NEAR(rt_mat4_get(root_mat, 0, 3), 5.0, 0.1, "Masked layer preserves base root x");
    EXPECT_NEAR(rt_mat4_get(root_mat, 1, 3), 0.0, 0.01, "Masked layer does not affect root y");
    EXPECT_NEAR(rt_mat4_get(arm_mat, 1, 3), 1.0, 0.1, "Masked layer drives arm y");

    rt_anim_controller3d_stop_layer(controller, 1);
    EXPECT_TRUE(rt_anim_controller3d_crossfade_layer(controller, 1, rt_const_cstr("wave"), 0.2) ==
                    1,
                "CrossfadeLayer succeeds");
}

int main() {
    test_controller_state_flow();
    test_controller_masked_layer();

    printf("AnimController3D tests: %d/%d passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
