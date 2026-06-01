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
#include "rt_blendtree3d.h"
#include "rt_box.h"
#include "rt_iksolver3d.h"
#include "rt_seq.h"
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
extern void *rt_mat4_translate(double tx, double ty, double tz);
extern void *rt_mat4_rotate_z(double angle);
extern void *rt_mat4_mul(void *a, void *b);
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
    EXPECT_TRUE(
        strcmp(rt_string_cstr(rt_anim_controller3d_get_current_state(controller)), "idle") == 0,
        "CurrentState = idle");

    EXPECT_TRUE(rt_anim_controller3d_play(controller, rt_const_cstr("walk")) == 1,
                "Play walk succeeds");
    EXPECT_TRUE(rt_anim_controller3d_get_is_transitioning(controller) == 1,
                "Play walk uses default transition");
    EXPECT_TRUE(
        strcmp(rt_string_cstr(rt_anim_controller3d_get_previous_state(controller)), "idle") == 0,
        "PreviousState = idle");

    rt_anim_controller3d_set_root_motion_bone(controller, 0);
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

static void test_controller_root_motion_disabled_and_loop_wrap() {
    void *skel = rt_skeleton3d_new();
    rt_skeleton3d_add_bone(skel, rt_const_cstr("root"), -1, rt_mat4_identity());
    rt_skeleton3d_compute_inverse_bind(skel);

    void *walk = make_anim("walk", 0, 0.0, 0.0, 0.0, 10.0, 0.0, 0.0);
    void *controller = rt_anim_controller3d_new(skel);
    rt_anim_controller3d_add_state(controller, rt_const_cstr("walk"), walk);

    rt_anim_controller3d_play(controller, rt_const_cstr("walk"));
    rt_anim_controller3d_update(controller, 0.5);
    void *delta = rt_anim_controller3d_consume_root_motion(controller);
    EXPECT_NEAR(rt_vec3_x(delta), 0.0, 0.01, "Root motion is disabled by default");

    rt_anim_controller3d_set_root_motion_bone(controller, 0);
    rt_anim_controller3d_update(controller, 0.4);
    delta = rt_anim_controller3d_consume_root_motion(controller);
    EXPECT_NEAR(rt_vec3_x(delta), 4.0, 0.1, "Root motion accumulates after enabling a bone");

    rt_anim_controller3d_update(controller, 0.2);
    delta = rt_anim_controller3d_consume_root_motion(controller);
    EXPECT_NEAR(rt_vec3_x(delta), 2.0, 0.1, "Looping root motion preserves forward wrap delta");

    rt_anim_controller3d_set_root_motion_bone(controller, -1);
    rt_anim_controller3d_update(controller, 0.25);
    delta = rt_anim_controller3d_consume_root_motion(controller);
    EXPECT_NEAR(rt_vec3_x(delta), 0.0, 0.01, "SetRootMotionBone(-1) disables root motion");
}

static void test_controller_crossfade_preserves_source_speed() {
    void *skel = rt_skeleton3d_new();
    rt_skeleton3d_add_bone(skel, rt_const_cstr("root"), -1, rt_mat4_identity());
    rt_skeleton3d_compute_inverse_bind(skel);

    void *run = make_anim("run", 0, 0.0, 0.0, 0.0, 10.0, 0.0, 0.0);
    void *idle = make_anim("idle", 0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0);
    void *controller = rt_anim_controller3d_new(skel);
    rt_anim_controller3d_add_state(controller, rt_const_cstr("run"), run);
    rt_anim_controller3d_add_state(controller, rt_const_cstr("idle"), idle);
    rt_anim_controller3d_set_state_speed(controller, rt_const_cstr("run"), 2.0);
    rt_anim_controller3d_set_state_speed(controller, rt_const_cstr("idle"), 0.0);
    rt_anim_controller3d_set_state_looping(controller, rt_const_cstr("run"), 0);

    rt_anim_controller3d_play(controller, rt_const_cstr("run"));
    rt_anim_controller3d_update(controller, 0.25);
    rt_anim_controller3d_crossfade(controller, rt_const_cstr("idle"), 1.0);
    rt_anim_controller3d_update(controller, 0.25);

    void *root_mat = rt_anim_controller3d_get_bone_matrix(controller, 0);
    EXPECT_NEAR(rt_mat4_get(root_mat, 0, 3),
                7.5,
                0.2,
                "Crossfade source clip continues with source state speed");
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
    EXPECT_NEAR(rt_mat4_get(arm_mat, 0, 3),
                5.0,
                0.1,
                "Masked layer keeps base parent motion on child world matrix");
    EXPECT_NEAR(rt_mat4_get(arm_mat, 1, 3), 1.0, 0.1, "Masked layer drives arm y");

    rt_anim_controller3d_set_layer_weight(controller, 1, NAN);
    rt_anim_controller3d_update(controller, 0.1);
    arm_mat = rt_anim_controller3d_get_bone_matrix(controller, arm_bone);
    EXPECT_TRUE(std::isfinite(rt_mat4_get(arm_mat, 1, 3)),
                "AnimController3D converts NaN layer weights to finite output");

    rt_anim_controller3d_stop_layer(controller, 1);
    EXPECT_TRUE(rt_anim_controller3d_crossfade_layer(controller, 1, rt_const_cstr("wave"), 0.2) ==
                    1,
                "CrossfadeLayer succeeds");
}

static void test_controller_true_additive_layer_uses_bind_pose_delta() {
    void *skel_replace = rt_skeleton3d_new();
    void *skel_additive = rt_skeleton3d_new();
    int64_t replace_arm;
    int64_t additive_arm;
    void *replace_controller;
    void *additive_controller;
    void *base_replace;
    void *raise_replace;
    void *base_additive;
    void *raise_additive;
    void *reach_additive;
    void *replace_arm_mat;
    void *additive_arm_mat;

    rt_skeleton3d_add_bone(skel_replace, rt_const_cstr("root"), -1, rt_mat4_identity());
    replace_arm = rt_skeleton3d_add_bone(
        skel_replace, rt_const_cstr("arm"), 0, rt_mat4_translate(0.0, 1.0, 0.0));
    rt_skeleton3d_compute_inverse_bind(skel_replace);

    rt_skeleton3d_add_bone(skel_additive, rt_const_cstr("root"), -1, rt_mat4_identity());
    additive_arm = rt_skeleton3d_add_bone(
        skel_additive, rt_const_cstr("arm"), 0, rt_mat4_translate(0.0, 1.0, 0.0));
    rt_skeleton3d_compute_inverse_bind(skel_additive);

    base_replace = make_anim("base", replace_arm, 0.0, 2.0, 0.0, 0.0, 2.0, 0.0);
    raise_replace = make_anim("raise", replace_arm, 0.0, 3.0, 0.0, 0.0, 3.0, 0.0);
    base_additive = make_anim("base", additive_arm, 0.0, 2.0, 0.0, 0.0, 2.0, 0.0);
    raise_additive = make_anim("raise", additive_arm, 0.0, 3.0, 0.0, 0.0, 3.0, 0.0);
    reach_additive = make_anim("reach", additive_arm, 0.0, 5.0, 0.0, 0.0, 5.0, 0.0);

    replace_controller = rt_anim_controller3d_new(skel_replace);
    rt_anim_controller3d_add_state(replace_controller, rt_const_cstr("base"), base_replace);
    rt_anim_controller3d_add_state(replace_controller, rt_const_cstr("raise"), raise_replace);
    rt_anim_controller3d_play(replace_controller, rt_const_cstr("base"));
    rt_anim_controller3d_set_layer_mask(replace_controller, 1, replace_arm);
    rt_anim_controller3d_set_layer_weight(replace_controller, 1, 1.0);
    EXPECT_TRUE(rt_anim_controller3d_play_layer(replace_controller, 1, rt_const_cstr("raise")) == 1,
                "PlayLayer remains a masked replace overlay");
    rt_anim_controller3d_update(replace_controller, 0.0);
    replace_arm_mat = rt_anim_controller3d_get_bone_matrix(replace_controller, replace_arm);

    additive_controller = rt_anim_controller3d_new(skel_additive);
    rt_anim_controller3d_add_state(additive_controller, rt_const_cstr("base"), base_additive);
    rt_anim_controller3d_add_state(additive_controller, rt_const_cstr("raise"), raise_additive);
    rt_anim_controller3d_add_state(additive_controller, rt_const_cstr("reach"), reach_additive);
    rt_anim_controller3d_play(additive_controller, rt_const_cstr("base"));
    rt_anim_controller3d_set_layer_mask(additive_controller, 1, additive_arm);
    rt_anim_controller3d_set_layer_weight(additive_controller, 1, 1.0);
    EXPECT_TRUE(rt_anim_controller3d_play_layer_additive(
                    additive_controller, 1, rt_const_cstr("raise")) == 1,
                "PlayLayerAdditive enables true bind-pose delta composition");
    EXPECT_TRUE(rt_anim_controller3d_play_layer_additive(
                    additive_controller, 0, rt_const_cstr("raise")) == 0,
                "PlayLayerAdditive rejects the base layer");
    rt_anim_controller3d_update(additive_controller, 0.0);
    additive_arm_mat = rt_anim_controller3d_get_bone_matrix(additive_controller, additive_arm);

    EXPECT_NEAR(
        rt_mat4_get(replace_arm_mat, 1, 3), 3.0, 0.1, "PlayLayer replaces the masked local pose");
    EXPECT_NEAR(rt_mat4_get(additive_arm_mat, 1, 3),
                4.0,
                0.1,
                "PlayLayerAdditive adds overlay minus bind pose onto the base pose");
    EXPECT_TRUE(rt_anim_controller3d_crossfade_layer_additive(
                    additive_controller, 0, rt_const_cstr("reach"), 1.0) == 0,
                "CrossfadeLayerAdditive rejects the base layer");
    EXPECT_TRUE(rt_anim_controller3d_crossfade_layer_additive(
                    additive_controller, 1, rt_const_cstr("reach"), 1.0) == 1,
                "CrossfadeLayerAdditive blends true additive overlay layers");
    rt_anim_controller3d_update(additive_controller, 0.5);
    additive_arm_mat = rt_anim_controller3d_get_bone_matrix(additive_controller, additive_arm);
    EXPECT_NEAR(rt_mat4_get(additive_arm_mat, 1, 3),
                5.0,
                0.15,
                "CrossfadeLayerAdditive blends halfway between additive deltas");
    rt_anim_controller3d_update(additive_controller, 0.5);
    additive_arm_mat = rt_anim_controller3d_get_bone_matrix(additive_controller, additive_arm);
    EXPECT_NEAR(rt_mat4_get(additive_arm_mat, 1, 3),
                6.0,
                0.15,
                "CrossfadeLayerAdditive reaches the target additive delta");
}

static void test_controller_blend_tree_drives_base_pose() {
    void *skel = rt_skeleton3d_new();
    rt_skeleton3d_add_bone(skel, rt_const_cstr("root"), -1, rt_mat4_identity());
    rt_skeleton3d_compute_inverse_bind(skel);

    void *idle = make_anim("idle", 0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0);
    void *lean = make_anim("lean", 0, 10.0, 0.0, 0.0, 10.0, 0.0, 0.0);
    void *tree = rt_blend_tree3d_new_1d(skel);
    rt_blend_tree3d_add_sample(tree, idle, 0.0, 0.0);
    rt_blend_tree3d_add_sample(tree, lean, 1.0, 0.0);
    rt_blend_tree3d_set_param(tree, 0.5, 0.0);

    void *controller = rt_anim_controller3d_new(skel);
    EXPECT_TRUE(rt_anim_controller3d_set_blend_tree(controller, tree) != 0,
                "AnimController3D.SetBlendTree accepts a compatible tree");
    rt_anim_controller3d_update(controller, 0.0);
    void *root_mat = rt_anim_controller3d_get_bone_matrix(controller, 0);
    EXPECT_NEAR(rt_mat4_get(root_mat, 0, 3),
                5.0,
                0.1,
                "AnimController3D.SetBlendTree drives the base pose from blend weights");

    EXPECT_TRUE(rt_anim_controller3d_set_blend_tree(controller, skel) == 0,
                "AnimController3D.SetBlendTree rejects non-tree handles");
    EXPECT_TRUE(rt_anim_controller3d_set_blend_tree(controller, nullptr) != 0,
                "AnimController3D.SetBlendTree clears on NULL");
    rt_anim_controller3d_update(controller, 0.0);
    root_mat = rt_anim_controller3d_get_bone_matrix(controller, 0);
    EXPECT_NEAR(rt_mat4_get(root_mat, 0, 3), 0.0, 0.1, "Clearing BlendTree restores bind pose");
}

static void test_two_bone_ik_pole_vector() {
    void *skel = rt_skeleton3d_new();
    int64_t root = rt_skeleton3d_add_bone(skel, rt_const_cstr("root"), -1, rt_mat4_identity());
    int64_t knee =
        rt_skeleton3d_add_bone(skel, rt_const_cstr("knee"), root, rt_mat4_translate(1.0, 0.0, 0.0));
    int64_t foot =
        rt_skeleton3d_add_bone(skel, rt_const_cstr("foot"), knee, rt_mat4_translate(1.0, 0.0, 0.0));
    rt_skeleton3d_compute_inverse_bind(skel);

    void *controller = rt_anim_controller3d_new(skel);
    void *solver = rt_ik_solver3d_two_bone(skel, root, knee, foot);
    rt_ik_solver3d_set_target(solver, rt_vec3_new(1.0, 1.0, 0.0));
    rt_ik_solver3d_set_weight(solver, 1.0);

    /* Pole toward +Z swings the bent knee toward +Z. */
    rt_ik_solver3d_set_pole(solver, rt_vec3_new(0.0, 0.0, 1.0));
    rt_ik_solver3d_solve(solver);
    rt_anim_controller3d_set_ik_solver(controller, solver);
    double knee_z_pos = rt_mat4_get(rt_anim_controller3d_get_bone_matrix(controller, knee), 2, 3);
    EXPECT_TRUE(knee_z_pos > 0.1, "TwoBone IK pole +Z bends the knee toward +Z");

    /* Flipping the pole to -Z mirrors the bend. */
    rt_ik_solver3d_set_pole(solver, rt_vec3_new(0.0, 0.0, -1.0));
    rt_ik_solver3d_solve(solver);
    rt_anim_controller3d_set_ik_solver(controller, solver);
    double knee_z_neg = rt_mat4_get(rt_anim_controller3d_get_bone_matrix(controller, knee), 2, 3);
    EXPECT_TRUE(knee_z_neg < -0.1, "TwoBone IK pole -Z bends the knee toward -Z");

    /* The pole only changes the bend plane — the foot still reaches the target. */
    void *foot_mat = rt_anim_controller3d_get_bone_matrix(controller, foot);
    EXPECT_NEAR(rt_mat4_get(foot_mat, 0, 3), 1.0, 0.05, "Pole keeps the foot on target x");
    EXPECT_NEAR(rt_mat4_get(foot_mat, 1, 3), 1.0, 0.05, "Pole keeps the foot on target y");
}

static void test_controller_ik_solver_drives_end_effector() {
    void *skel = rt_skeleton3d_new();
    int64_t root = rt_skeleton3d_add_bone(skel, rt_const_cstr("root"), -1, rt_mat4_identity());
    int64_t knee =
        rt_skeleton3d_add_bone(skel, rt_const_cstr("knee"), root, rt_mat4_translate(1.0, 0.0, 0.0));
    int64_t foot =
        rt_skeleton3d_add_bone(skel, rt_const_cstr("foot"), knee, rt_mat4_translate(1.0, 0.0, 0.0));
    rt_skeleton3d_compute_inverse_bind(skel);

    void *controller = rt_anim_controller3d_new(skel);
    void *solver = rt_ik_solver3d_two_bone(skel, root, knee, foot);
    EXPECT_TRUE(solver != nullptr, "IKSolver3D.TwoBone creates a parented solver");
    rt_ik_solver3d_set_target(solver, rt_vec3_new(1.0, 1.0, 0.0));
    rt_ik_solver3d_set_weight(solver, 1.0);
    rt_ik_solver3d_solve(solver);

    EXPECT_TRUE(rt_anim_controller3d_set_ik_solver(controller, solver) == 1,
                "AnimController3D.SetIKSolver accepts a compatible solver");
    void *foot_mat = rt_anim_controller3d_get_bone_matrix(controller, foot);
    EXPECT_NEAR(rt_mat4_get(foot_mat, 0, 3), 1.0, 0.03, "TwoBone IK reaches target x");
    EXPECT_NEAR(rt_mat4_get(foot_mat, 1, 3), 1.0, 0.03, "TwoBone IK reaches target y");
    EXPECT_NEAR(rt_mat4_get(foot_mat, 2, 3), 0.0, 0.03, "TwoBone IK preserves target z");

    rt_ik_solver3d_set_weight(solver, 0.5);
    EXPECT_TRUE(rt_anim_controller3d_set_ik_solver(controller, solver) == 1,
                "AnimController3D.SetIKSolver accepts weight changes on the same solver");
    foot_mat = rt_anim_controller3d_get_bone_matrix(controller, foot);
    EXPECT_NEAR(rt_mat4_get(foot_mat, 0, 3), 1.5, 0.05, "IK weight blends end-effector x");
    EXPECT_NEAR(rt_mat4_get(foot_mat, 1, 3), 0.5, 0.05, "IK weight blends end-effector y");

    EXPECT_TRUE(rt_anim_controller3d_set_ik_solver(controller, skel) == 0,
                "AnimController3D.SetIKSolver rejects non-solver handles");
    EXPECT_TRUE(rt_anim_controller3d_set_ik_solver(controller, nullptr) == 1,
                "AnimController3D.SetIKSolver clears with NULL");
    foot_mat = rt_anim_controller3d_get_bone_matrix(controller, foot);
    EXPECT_NEAR(rt_mat4_get(foot_mat, 0, 3), 2.0, 0.01, "Clearing IK restores bind-pose foot x");
    EXPECT_NEAR(rt_mat4_get(foot_mat, 1, 3), 0.0, 0.01, "Clearing IK restores bind-pose foot y");
}

static void test_two_bone_ik_foot_aligns_to_ground_normal() {
    /* The knee is given a 90-degree bind rotation so the foot's parent has a non-identity world
     * rotation. That exercises the foot-orientation's local/world conversion: a version that
     * forgot the parent inverse would only align correctly under an identity parent. */
    const double half_pi = 1.5707963267948966;
    void *skel = rt_skeleton3d_new();
    int64_t root = rt_skeleton3d_add_bone(skel, rt_const_cstr("root"), -1, rt_mat4_identity());
    void *knee_bind = rt_mat4_mul(rt_mat4_translate(1.0, 0.0, 0.0), rt_mat4_rotate_z(half_pi));
    int64_t knee = rt_skeleton3d_add_bone(skel, rt_const_cstr("knee"), root, knee_bind);
    int64_t foot =
        rt_skeleton3d_add_bone(skel, rt_const_cstr("foot"), knee, rt_mat4_translate(1.0, 0.0, 0.0));
    rt_skeleton3d_compute_inverse_bind(skel);

    void *controller = rt_anim_controller3d_new(skel);
    void *solver = rt_ik_solver3d_two_bone(skel, root, knee, foot);
    rt_ik_solver3d_set_target(solver, rt_vec3_new(1.5, 0.5, 0.0));
    rt_ik_solver3d_set_weight(solver, 1.0);
    /* A slope normal tilted away from world up. */
    double nx = 0.5, ny = 1.0, nz = 0.0;
    double nlen = std::sqrt(nx * nx + ny * ny + nz * nz);
    nx /= nlen;
    ny /= nlen;
    nz /= nlen;
    rt_ik_solver3d_set_ground_normal(solver, rt_vec3_new(nx, ny, nz));
    rt_ik_solver3d_solve(solver);
    EXPECT_TRUE(rt_anim_controller3d_set_ik_solver(controller, solver) == 1,
                "AnimController3D accepts the foot-orientation IK solver");

    /* The foot's world up axis (global matrix column 1) should align with the ground normal,
     * regardless of the rotated knee parent. */
    void *foot_mat = rt_anim_controller3d_get_bone_matrix(controller, foot);
    EXPECT_NEAR(rt_mat4_get(foot_mat, 0, 1), nx, 0.05, "Foot IK aligns foot up.x to ground normal");
    EXPECT_NEAR(rt_mat4_get(foot_mat, 1, 1), ny, 0.05, "Foot IK aligns foot up.y to ground normal");
    EXPECT_NEAR(rt_mat4_get(foot_mat, 2, 1), nz, 0.05, "Foot IK aligns foot up.z to ground normal");
}

static void test_ik_solver_look_at_and_fabrik_factories() {
    void *look_skel = rt_skeleton3d_new();
    int64_t look_bone =
        rt_skeleton3d_add_bone(look_skel, rt_const_cstr("head"), -1, rt_mat4_identity());
    rt_skeleton3d_compute_inverse_bind(look_skel);
    void *look_controller = rt_anim_controller3d_new(look_skel);
    void *look_solver = rt_ik_solver3d_look_at(look_skel, look_bone);
    EXPECT_TRUE(look_solver != nullptr, "IKSolver3D.LookAt creates a solver");
    rt_ik_solver3d_set_target(look_solver, rt_vec3_new(1.0, 0.0, 0.0));
    rt_ik_solver3d_set_weight(look_solver, 1.0);
    EXPECT_TRUE(rt_anim_controller3d_set_ik_solver(look_controller, look_solver) == 1,
                "AnimController3D.SetIKSolver accepts LookAt solvers");
    void *head_mat = rt_anim_controller3d_get_bone_matrix(look_controller, look_bone);
    EXPECT_NEAR(rt_mat4_get(head_mat, 0, 2), 1.0, 0.03, "LookAt rotates local +Z toward target x");
    EXPECT_NEAR(rt_mat4_get(head_mat, 1, 2), 0.0, 0.03, "LookAt keeps target y near zero");

    void *skel = rt_skeleton3d_new();
    int64_t b0 = rt_skeleton3d_add_bone(skel, rt_const_cstr("b0"), -1, rt_mat4_identity());
    int64_t b1 =
        rt_skeleton3d_add_bone(skel, rt_const_cstr("b1"), b0, rt_mat4_translate(1.0, 0.0, 0.0));
    int64_t b2 =
        rt_skeleton3d_add_bone(skel, rt_const_cstr("b2"), b1, rt_mat4_translate(1.0, 0.0, 0.0));
    int64_t b3 =
        rt_skeleton3d_add_bone(skel, rt_const_cstr("b3"), b2, rt_mat4_translate(1.0, 0.0, 0.0));
    rt_skeleton3d_compute_inverse_bind(skel);
    void *chain = rt_seq_new_owned();
    rt_seq_push(chain, rt_box_i64(b0));
    rt_seq_push(chain, rt_box_i64(b1));
    rt_seq_push(chain, rt_box_i64(b2));
    rt_seq_push(chain, rt_box_i64(b3));
    void *fabrik = rt_ik_solver3d_fabrik(skel, chain);
    EXPECT_TRUE(fabrik != nullptr, "IKSolver3D.FABRIK creates a chain solver");
    rt_ik_solver3d_set_target(fabrik, rt_vec3_new(0.0, 2.0, 0.0));
    void *controller = rt_anim_controller3d_new(skel);
    EXPECT_TRUE(rt_anim_controller3d_set_ik_solver(controller, fabrik) == 1,
                "AnimController3D.SetIKSolver accepts FABRIK solvers");
    void *end_mat = rt_anim_controller3d_get_bone_matrix(controller, b3);
    EXPECT_NEAR(rt_mat4_get(end_mat, 0, 3), 0.0, 0.08, "FABRIK reaches target x");
    EXPECT_NEAR(rt_mat4_get(end_mat, 1, 3), 2.0, 0.08, "FABRIK reaches target y");

    EXPECT_TRUE(rt_ik_solver3d_two_bone(skel, b0, b2, b1) == nullptr,
                "IKSolver3D.TwoBone rejects non-parented chains");
}

static void test_controller_animation_lod_throttles_updates_deterministically() {
    void *skel = rt_skeleton3d_new();
    rt_skeleton3d_add_bone(skel, rt_const_cstr("root"), -1, rt_mat4_identity());
    rt_skeleton3d_compute_inverse_bind(skel);

    void *walk = make_anim("walk", 0, 0.0, 0.0, 0.0, 10.0, 0.0, 0.0);
    void *controller = rt_anim_controller3d_new(skel);
    rt_anim_controller3d_add_state(controller, rt_const_cstr("walk"), walk);
    rt_anim_controller3d_play(controller, rt_const_cstr("walk"));
    rt_anim_controller3d_set_animation_lod(controller, 50.0, 2.0);

    for (int i = 0; i < 4; i++)
        rt_anim_controller3d_update(controller, 0.1);
    void *root_mat = rt_anim_controller3d_get_bone_matrix(controller, 0);
    EXPECT_NEAR(rt_mat4_get(root_mat, 0, 3),
                0.0,
                0.01,
                "SetAnimationLOD holds pose until the configured update interval elapses");
    EXPECT_NEAR(rt_anim_controller3d_get_state_time(controller),
                0.0,
                0.01,
                "SetAnimationLOD defers state time while accumulating sub-interval deltas");

    rt_anim_controller3d_update(controller, 0.1);
    root_mat = rt_anim_controller3d_get_bone_matrix(controller, 0);
    EXPECT_NEAR(rt_mat4_get(root_mat, 0, 3),
                5.0,
                0.1,
                "SetAnimationLOD applies accumulated time at the target update rate");
    EXPECT_NEAR(rt_anim_controller3d_get_state_time(controller),
                0.5,
                0.01,
                "SetAnimationLOD advances state time by the accumulated delta");

    rt_anim_controller3d_set_animation_lod(controller, 0.0, 0.0);
    rt_anim_controller3d_update(controller, 0.1);
    root_mat = rt_anim_controller3d_get_bone_matrix(controller, 0);
    EXPECT_NEAR(rt_mat4_get(root_mat, 0, 3),
                6.0,
                0.1,
                "SetAnimationLOD disables throttling for non-positive inputs");
}

static void test_controller_events_cover_full_loops_and_reverse() {
    void *skel = rt_skeleton3d_new();
    rt_skeleton3d_add_bone(skel, rt_const_cstr("root"), -1, rt_mat4_identity());
    rt_skeleton3d_compute_inverse_bind(skel);

    void *walk = make_anim("walk", 0, 0.0, 0.0, 0.0, 10.0, 0.0, 0.0);
    void *controller = rt_anim_controller3d_new(skel);
    rt_anim_controller3d_add_state(controller, rt_const_cstr("walk"), walk);
    rt_anim_controller3d_add_event(controller, rt_const_cstr("walk"), 0.5, rt_const_cstr("step"));

    rt_anim_controller3d_play(controller, rt_const_cstr("walk"));
    rt_anim_controller3d_update(controller, 1.0);
    EXPECT_TRUE(strcmp(rt_string_cstr(rt_anim_controller3d_poll_event(controller)), "step") == 0,
                "AnimController3D fires events crossed by an exact full loop");

    rt_anim_controller3d_update(controller, 2.25);
    EXPECT_TRUE(strcmp(rt_string_cstr(rt_anim_controller3d_poll_event(controller)), "step") == 0,
                "AnimController3D fires looping events after multi-loop updates");

    rt_anim_controller3d_set_state_speed(controller, rt_const_cstr("walk"), -1.0);
    rt_anim_controller3d_play(controller, rt_const_cstr("walk"));
    rt_anim_controller3d_update(controller, 0.75);
    EXPECT_TRUE(strcmp(rt_string_cstr(rt_anim_controller3d_poll_event(controller)), "step") == 0,
                "AnimController3D fires looping events during reverse wrap");

    rt_anim_controller3d_set_state_speed(controller, rt_const_cstr("walk"), NAN);
    rt_anim_controller3d_play(controller, rt_const_cstr("walk"));
    rt_anim_controller3d_update(controller, 0.5);
    EXPECT_TRUE(
        std::isfinite(rt_mat4_get(rt_anim_controller3d_get_bone_matrix(controller, 0), 0, 3)),
        "AnimController3D sanitizes non-finite state speeds");
}

static void test_controller_rejects_wrong_animation_handles() {
    void *skel = rt_skeleton3d_new();
    void *controller;

    rt_skeleton3d_add_bone(skel, rt_const_cstr("root"), -1, rt_mat4_identity());
    rt_skeleton3d_compute_inverse_bind(skel);
    controller = rt_anim_controller3d_new(skel);

    EXPECT_TRUE(rt_anim_controller3d_add_state(controller, rt_const_cstr("bad"), skel) == -1,
                "AnimController3D.AddState rejects non-Animation3D handles");
    EXPECT_TRUE(rt_anim_controller3d_get_state_count(controller) == 0,
                "Rejected AnimController3D state does not change StateCount");
    rt_animation3d_set_looping(skel, 1);
    EXPECT_TRUE(rt_animation3d_get_looping(skel) == 0,
                "Animation3D getters/setters reject non-Animation3D handles");
}

static void test_controller_bone_count_lod_freezes_distal_bones() {
    /* 4-bone chain, identity binds: root(0) - b1(1) - b2(2) - foot(3). */
    void *skel = rt_skeleton3d_new();
    rt_skeleton3d_add_bone(skel, rt_const_cstr("root"), -1, rt_mat4_identity());
    int64_t b1 = rt_skeleton3d_add_bone(skel, rt_const_cstr("b1"), 0, rt_mat4_identity());
    int64_t b2 = rt_skeleton3d_add_bone(skel, rt_const_cstr("b2"), (int64_t)b1, rt_mat4_identity());
    int64_t foot =
        rt_skeleton3d_add_bone(skel, rt_const_cstr("foot"), (int64_t)b2, rt_mat4_identity());
    rt_skeleton3d_compute_inverse_bind(skel);

    /* LOD off: animating only the foot moves it (local x -> 1 at the t=0.5 midpoint). */
    void *c1 = rt_anim_controller3d_new(skel);
    rt_anim_controller3d_add_state(
        c1, rt_const_cstr("kick"), make_anim("kick", foot, 0.0, 0.0, 0.0, 2.0, 0.0, 0.0));
    rt_anim_controller3d_play(c1, rt_const_cstr("kick"));
    rt_anim_controller3d_update(c1, 0.5);
    EXPECT_NEAR(rt_mat4_get(rt_anim_controller3d_get_bone_matrix(c1, foot), 0, 3),
                1.0,
                0.1,
                "Bone LOD off: the distal foot animates");

    /* LOD freezing bones >= foot: the foot stops adding local animation (returns to bind x=0). */
    void *c2 = rt_anim_controller3d_new(skel);
    rt_anim_controller3d_add_state(
        c2, rt_const_cstr("kick"), make_anim("kick", foot, 0.0, 0.0, 0.0, 2.0, 0.0, 0.0));
    rt_anim_controller3d_play(c2, rt_const_cstr("kick"));
    rt_anim_controller3d_set_bone_lod(c2, foot);
    rt_anim_controller3d_update(c2, 0.5);
    EXPECT_NEAR(rt_mat4_get(rt_anim_controller3d_get_bone_matrix(c2, foot), 0, 3),
                0.0,
                0.05,
                "Bone LOD freezes the distal foot to its bind-pose local");

    /* Frozen bones still follow animated ancestors: animate b1, freeze from b2 on, foot follows. */
    void *c3 = rt_anim_controller3d_new(skel);
    rt_anim_controller3d_add_state(
        c3, rt_const_cstr("shift"), make_anim("shift", b1, 0.0, 0.0, 0.0, 2.0, 0.0, 0.0));
    rt_anim_controller3d_play(c3, rt_const_cstr("shift"));
    rt_anim_controller3d_set_bone_lod(c3, b2);
    rt_anim_controller3d_update(c3, 0.5);
    EXPECT_NEAR(rt_mat4_get(rt_anim_controller3d_get_bone_matrix(c3, foot), 0, 3),
                1.0,
                0.1,
                "Bone LOD: frozen bones still follow their animated ancestors");
}

int main() {
    test_controller_state_flow();
    test_controller_root_motion_disabled_and_loop_wrap();
    test_controller_crossfade_preserves_source_speed();
    test_controller_masked_layer();
    test_controller_true_additive_layer_uses_bind_pose_delta();
    test_controller_blend_tree_drives_base_pose();
    test_two_bone_ik_pole_vector();
    test_controller_ik_solver_drives_end_effector();
    test_two_bone_ik_foot_aligns_to_ground_normal();
    test_ik_solver_look_at_and_fabrik_factories();
    test_controller_animation_lod_throttles_updates_deterministically();
    test_controller_bone_count_lod_freezes_distal_bones();
    test_controller_events_cover_full_loops_and_reverse();
    test_controller_rejects_wrong_animation_handles();

    printf("AnimController3D tests: %d/%d passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
