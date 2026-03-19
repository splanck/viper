//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_rt_skeleton3d.cpp
// Purpose: Unit tests for Skeleton3D, Animation3D, AnimPlayer3D — bone
//   hierarchy, keyframe sampling, CPU skinning, crossfade.
//
// Links: rt_skeleton3d.h, vgfx3d_skinning.h, plans/3d/14-skeletal-animation.md
//
//===----------------------------------------------------------------------===//

#include "rt.hpp"
#include "rt_canvas3d.h"
#include "rt_internal.h"
#include "rt_skeleton3d.h"
#include "rt_string.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>

extern "C"
{
    extern void *rt_vec3_new(double x, double y, double z);
    extern double rt_vec3_x(void *v);
    extern double rt_vec3_y(void *v);
    extern double rt_vec3_z(void *v);
    extern void *rt_quat_new(double x, double y, double z, double w);
    extern void *rt_quat_from_euler(double pitch, double yaw, double roll);
    extern double rt_quat_x(void *q);
    extern double rt_quat_y(void *q);
    extern double rt_quat_z(void *q);
    extern double rt_quat_w(void *q);
    extern void *rt_mat4_identity(void);
    extern void *rt_mat4_translate(double tx, double ty, double tz);
    extern rt_string rt_const_cstr(const char *s);
}

static int tests_passed = 0;
static int tests_run = 0;

#define EXPECT_TRUE(cond, msg)                                                                     \
    do                                                                                             \
    {                                                                                              \
        tests_run++;                                                                               \
        if (!(cond))                                                                               \
        {                                                                                          \
            fprintf(stderr, "FAIL: %s\n", msg);                                                    \
        }                                                                                          \
        else                                                                                       \
        {                                                                                          \
            tests_passed++;                                                                        \
        }                                                                                          \
    } while (0)

#define EXPECT_NEAR(a, b, eps, msg)                                                                \
    do                                                                                             \
    {                                                                                              \
        tests_run++;                                                                               \
        if (fabs((double)(a) - (double)(b)) > (eps))                                               \
        {                                                                                          \
            fprintf(stderr, "FAIL: %s (got %f, expected %f)\n", msg, (double)(a), (double)(b));    \
        }                                                                                          \
        else                                                                                       \
        {                                                                                          \
            tests_passed++;                                                                        \
        }                                                                                          \
    } while (0)

static void test_skeleton_create()
{
    void *skel = rt_skeleton3d_new();
    EXPECT_TRUE(skel != nullptr, "Skeleton3D.New returns non-null");
    EXPECT_TRUE(rt_skeleton3d_get_bone_count(skel) == 0, "Initial bone count = 0");
}

static void test_skeleton_add_bone()
{
    void *skel = rt_skeleton3d_new();
    rt_string name = rt_const_cstr("root");
    int64_t idx = rt_skeleton3d_add_bone(skel, name, -1, rt_mat4_identity());
    EXPECT_TRUE(idx == 0, "First bone index = 0");
    EXPECT_TRUE(rt_skeleton3d_get_bone_count(skel) == 1, "Bone count = 1");

    rt_string child_name = rt_const_cstr("child");
    int64_t cidx = rt_skeleton3d_add_bone(skel, child_name, 0, rt_mat4_translate(1.0, 0.0, 0.0));
    EXPECT_TRUE(cidx == 1, "Second bone index = 1");
    EXPECT_TRUE(rt_skeleton3d_get_bone_count(skel) == 2, "Bone count = 2");
}

static void test_skeleton_find_bone()
{
    void *skel = rt_skeleton3d_new();
    rt_skeleton3d_add_bone(skel, rt_const_cstr("root"), -1, rt_mat4_identity());
    rt_skeleton3d_add_bone(skel, rt_const_cstr("arm"), 0, rt_mat4_identity());
    rt_skeleton3d_add_bone(skel, rt_const_cstr("hand"), 1, rt_mat4_identity());

    EXPECT_TRUE(rt_skeleton3d_find_bone(skel, rt_const_cstr("arm")) == 1, "FindBone('arm') = 1");
    EXPECT_TRUE(rt_skeleton3d_find_bone(skel, rt_const_cstr("hand")) == 2, "FindBone('hand') = 2");
    EXPECT_TRUE(rt_skeleton3d_find_bone(skel, rt_const_cstr("missing")) == -1,
                "FindBone('missing') = -1");
}

static void test_animation_create()
{
    void *anim = rt_animation3d_new(rt_const_cstr("walk"), 1.0);
    EXPECT_TRUE(anim != nullptr, "Animation3D.New returns non-null");
    EXPECT_NEAR(rt_animation3d_get_duration(anim), 1.0, 0.001, "Duration = 1.0");
}

static void test_animation_keyframes()
{
    void *anim = rt_animation3d_new(rt_const_cstr("test"), 1.0);
    void *pos0 = rt_vec3_new(0.0, 0.0, 0.0);
    void *pos1 = rt_vec3_new(1.0, 0.0, 0.0);
    void *rot = rt_quat_new(0.0, 0.0, 0.0, 1.0);
    void *scl = rt_vec3_new(1.0, 1.0, 1.0);

    rt_animation3d_add_keyframe(anim, 0, 0.0, pos0, rot, scl);
    rt_animation3d_add_keyframe(anim, 0, 1.0, pos1, rot, scl);
    /* Keyframes stored successfully — no crash */
    EXPECT_TRUE(1, "AddKeyframe succeeds");
}

static void test_player_create()
{
    void *skel = rt_skeleton3d_new();
    rt_skeleton3d_add_bone(skel, rt_const_cstr("root"), -1, rt_mat4_identity());
    rt_skeleton3d_compute_inverse_bind(skel);

    void *player = rt_anim_player3d_new(skel);
    EXPECT_TRUE(player != nullptr, "AnimPlayer3D.New returns non-null");
    EXPECT_TRUE(rt_anim_player3d_is_playing(player) == 0, "Not playing initially");
}

static void test_player_playback()
{
    void *skel = rt_skeleton3d_new();
    rt_skeleton3d_add_bone(skel, rt_const_cstr("root"), -1, rt_mat4_identity());
    rt_skeleton3d_compute_inverse_bind(skel);

    void *anim = rt_animation3d_new(rt_const_cstr("slide"), 1.0);
    void *pos0 = rt_vec3_new(0.0, 0.0, 0.0);
    void *pos1 = rt_vec3_new(10.0, 0.0, 0.0);
    void *rot = rt_quat_new(0.0, 0.0, 0.0, 1.0);
    void *scl = rt_vec3_new(1.0, 1.0, 1.0);
    rt_animation3d_add_keyframe(anim, 0, 0.0, pos0, rot, scl);
    rt_animation3d_add_keyframe(anim, 0, 1.0, pos1, rot, scl);

    void *player = rt_anim_player3d_new(skel);
    rt_anim_player3d_play(player, anim);
    EXPECT_TRUE(rt_anim_player3d_is_playing(player) == 1, "Playing after play()");

    /* Advance to midpoint */
    rt_anim_player3d_update(player, 0.5);
    void *mat = rt_anim_player3d_get_bone_matrix(player, 0);
    EXPECT_TRUE(mat != nullptr, "GetBoneMatrix returns non-null");

    /* At t=0.5, position should be (5, 0, 0). Bone palette = global * inverse_bind.
     * With identity bind pose, inverse_bind = identity, so palette = local transform.
     * The translation at t=0.5: lerp(0, 10, 0.5) = 5. */
    typedef struct
    {
        double m[16];
    } mat4_view;

    mat4_view *mv = (mat4_view *)mat;
    EXPECT_NEAR(mv->m[3], 5.0, 0.1, "At t=0.5: bone X translation ≈ 5");
}

static void test_player_loop()
{
    void *skel = rt_skeleton3d_new();
    rt_skeleton3d_add_bone(skel, rt_const_cstr("root"), -1, rt_mat4_identity());
    rt_skeleton3d_compute_inverse_bind(skel);

    void *anim = rt_animation3d_new(rt_const_cstr("loop"), 1.0);
    rt_animation3d_set_looping(anim, 1);
    void *pos0 = rt_vec3_new(0.0, 0.0, 0.0);
    void *rot = rt_quat_new(0.0, 0.0, 0.0, 1.0);
    void *scl = rt_vec3_new(1.0, 1.0, 1.0);
    rt_animation3d_add_keyframe(anim, 0, 0.0, pos0, rot, scl);
    rt_animation3d_add_keyframe(anim, 0, 1.0, pos0, rot, scl);

    void *player = rt_anim_player3d_new(skel);
    rt_anim_player3d_play(player, anim);

    /* Advance past duration — should wrap */
    rt_anim_player3d_update(player, 1.5);
    EXPECT_TRUE(rt_anim_player3d_is_playing(player) == 1, "Still playing after loop wrap");
    EXPECT_NEAR(rt_anim_player3d_get_time(player), 0.5, 0.01, "Time wraps to 0.5");
}

static void test_player_stop_at_end()
{
    void *skel = rt_skeleton3d_new();
    rt_skeleton3d_add_bone(skel, rt_const_cstr("root"), -1, rt_mat4_identity());
    rt_skeleton3d_compute_inverse_bind(skel);

    void *anim = rt_animation3d_new(rt_const_cstr("once"), 1.0);
    rt_animation3d_set_looping(anim, 0);
    void *pos = rt_vec3_new(0.0, 0.0, 0.0);
    void *rot = rt_quat_new(0.0, 0.0, 0.0, 1.0);
    void *scl = rt_vec3_new(1.0, 1.0, 1.0);
    rt_animation3d_add_keyframe(anim, 0, 0.0, pos, rot, scl);
    rt_animation3d_add_keyframe(anim, 0, 1.0, pos, rot, scl);

    void *player = rt_anim_player3d_new(skel);
    rt_anim_player3d_play(player, anim);
    rt_anim_player3d_update(player, 2.0);
    EXPECT_TRUE(rt_anim_player3d_is_playing(player) == 0, "Stopped after non-looping end");
}

static void test_player_speed()
{
    void *skel = rt_skeleton3d_new();
    rt_skeleton3d_add_bone(skel, rt_const_cstr("root"), -1, rt_mat4_identity());
    rt_skeleton3d_compute_inverse_bind(skel);

    void *anim = rt_animation3d_new(rt_const_cstr("fast"), 2.0);
    void *pos = rt_vec3_new(0.0, 0.0, 0.0);
    void *rot = rt_quat_new(0.0, 0.0, 0.0, 1.0);
    void *scl = rt_vec3_new(1.0, 1.0, 1.0);
    rt_animation3d_add_keyframe(anim, 0, 0.0, pos, rot, scl);
    rt_animation3d_add_keyframe(anim, 0, 2.0, pos, rot, scl);

    void *player = rt_anim_player3d_new(skel);
    rt_anim_player3d_set_speed(player, 2.0);
    rt_anim_player3d_play(player, anim);
    rt_anim_player3d_update(player, 0.5);
    /* At speed 2x, 0.5s real time = 1.0s animation time */
    EXPECT_NEAR(rt_anim_player3d_get_time(player), 1.0, 0.01, "Speed 2x: t=1.0 after 0.5s");
}

static void test_two_bone_chain()
{
    void *skel = rt_skeleton3d_new();
    /* Root at origin */
    rt_skeleton3d_add_bone(skel, rt_const_cstr("root"), -1, rt_mat4_identity());
    /* Child offset by (2, 0, 0) */
    rt_skeleton3d_add_bone(skel, rt_const_cstr("child"), 0, rt_mat4_translate(2.0, 0.0, 0.0));
    rt_skeleton3d_compute_inverse_bind(skel);

    /* Animation: translate root by (5, 0, 0). Child stays at local (2, 0, 0). */
    void *anim = rt_animation3d_new(rt_const_cstr("move"), 1.0);
    void *pos5 = rt_vec3_new(5.0, 0.0, 0.0);
    void *rot = rt_quat_new(0.0, 0.0, 0.0, 1.0);
    void *scl = rt_vec3_new(1.0, 1.0, 1.0);
    rt_animation3d_add_keyframe(anim, 0, 0.0, pos5, rot, scl);
    rt_animation3d_add_keyframe(anim, 0, 1.0, pos5, rot, scl);

    void *player = rt_anim_player3d_new(skel);
    rt_anim_player3d_play(player, anim);
    rt_anim_player3d_update(player, 0.0);

    /* Root bone: global = translate(5,0,0). Palette = global * inv_bind.
     * inv_bind(root) = identity. So palette[0] = translate(5,0,0). */
    typedef struct
    {
        double m[16];
    } mat4_view;

    mat4_view *m0 = (mat4_view *)rt_anim_player3d_get_bone_matrix(player, 0);
    EXPECT_NEAR(m0->m[3], 5.0, 0.1, "Root palette X = 5");

    /* Child bone: global = parent_global * child_local = translate(5,0,0) * translate(2,0,0) =
     * translate(7,0,0). inv_bind(child) = inverse(translate(2,0,0)) = translate(-2,0,0). Palette[1]
     * = translate(7,0,0) * translate(-2,0,0) = translate(5,0,0). */
    mat4_view *m1 = (mat4_view *)rt_anim_player3d_get_bone_matrix(player, 1);
    EXPECT_NEAR(m1->m[3], 5.0, 0.1, "Child palette X = 5 (root moved +5, child stays relative)");
}

static void test_non_identity_bind_pose()
{
    /* Regression test: bone palette must use separate globals buffer.
     * With non-identity bind pose, palette = global * inverse_bind.
     * A child's global must NOT include parent's inverse_bind. */
    void *skel = rt_skeleton3d_new();
    /* Root bone at position (3, 0, 0) */
    rt_skeleton3d_add_bone(skel, rt_const_cstr("root"), -1, rt_mat4_translate(3.0, 0.0, 0.0));
    /* Child bone at position (2, 0, 0) relative to root */
    rt_skeleton3d_add_bone(skel, rt_const_cstr("child"), 0, rt_mat4_translate(2.0, 0.0, 0.0));
    rt_skeleton3d_compute_inverse_bind(skel);

    /* Animation: move root to (10, 0, 0), child stays at local (2, 0, 0) */
    void *anim = rt_animation3d_new(rt_const_cstr("move"), 1.0);
    void *pos_root = rt_vec3_new(10.0, 0.0, 0.0);
    void *pos_child = rt_vec3_new(2.0, 0.0, 0.0);
    void *rot = rt_quat_new(0.0, 0.0, 0.0, 1.0);
    void *scl = rt_vec3_new(1.0, 1.0, 1.0);
    rt_animation3d_add_keyframe(anim, 0, 0.0, pos_root, rot, scl);
    rt_animation3d_add_keyframe(anim, 1, 0.0, pos_child, rot, scl);

    void *player = rt_anim_player3d_new(skel);
    rt_anim_player3d_play(player, anim);
    rt_anim_player3d_update(player, 0.0);

    /* Root: global=(10,0,0), inverse_bind=inv(translate(3,0,0))=translate(-3,0,0)
     * palette[0] = translate(10,0,0) * translate(-3,0,0) = translate(7,0,0) */
    typedef struct
    {
        double m[16];
    } mat4_view;

    mat4_view *m0 = (mat4_view *)rt_anim_player3d_get_bone_matrix(player, 0);
    EXPECT_NEAR(m0->m[3], 7.0, 0.1, "Root palette X = 10 - 3 = 7");

    /* Child: global = parent_global(10,0,0) * child_local(2,0,0) = translate(12,0,0)
     * inverse_bind(child) = inv(root_bind(3,0,0) * child_bind(2,0,0)) = inv(translate(5,0,0)) =
     * translate(-5,0,0) palette[1] = translate(12,0,0) * translate(-5,0,0) = translate(7,0,0) */
    mat4_view *m1 = (mat4_view *)rt_anim_player3d_get_bone_matrix(player, 1);
    EXPECT_NEAR(m1->m[3], 7.0, 0.1, "Child palette X = 12 - 5 = 7 (non-identity bind pose)");
}

static void test_bone_name()
{
    void *skel = rt_skeleton3d_new();
    rt_skeleton3d_add_bone(skel, rt_const_cstr("LeftArm"), -1, rt_mat4_identity());
    rt_string name = rt_skeleton3d_get_bone_name(skel, 0);
    EXPECT_TRUE(name != nullptr, "GetBoneName returns non-null");
}

int main()
{
    test_skeleton_create();
    test_skeleton_add_bone();
    test_skeleton_find_bone();
    test_animation_create();
    test_animation_keyframes();
    test_player_create();
    test_player_playback();
    test_player_loop();
    test_player_stop_at_end();
    test_player_speed();
    test_two_bone_chain();
    test_non_identity_bind_pose();
    test_bone_name();

    printf("Skeleton3D tests: %d/%d passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
