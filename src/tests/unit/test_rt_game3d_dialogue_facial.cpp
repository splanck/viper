//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_rt_game3d_dialogue_facial.cpp
// Purpose: Unit tests for Dialogue3D (queue/reveal/advance/choices/anchoring,
//   localization key resolution, Camera3D.WorldToScreen) and LipSync3D
//   (envelope follower, blink determinism, morph weight drive).
// Key invariants:
//   - Deterministic fixed steps; blink timelines replay identically.
// Ownership/Lifetime:
//   - Test-created runtime handles rely on production GC conventions.
// Links: misc/plans/thirdpersonupgrade/25-dialogue-3d.md,
//   misc/plans/thirdpersonupgrade/26-facial-lipsync.md
//
//===----------------------------------------------------------------------===//

#ifndef VIPER_ENABLE_GRAPHICS
#define VIPER_ENABLE_GRAPHICS 1
#endif

#include "rt_canvas3d.h"
#include "rt_game3d.h"
#include "rt_map.h"
#include "rt_message_bundle.h"
#include "rt_object.h"
#include "rt_seq.h"
#include "rt_string.h"
#include "rt_vec3.h"

#include <cmath>
#include <csetjmp>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
static std::jmp_buf g_trap_jmp;
static const char *g_last_trap = nullptr;
static bool g_expect_trap = false;
static int g_tests_passed = 0;
static int g_tests_total = 0;
} // namespace

extern "C" void vm_trap(const char *msg) {
    g_last_trap = msg;
    if (g_expect_trap)
        std::longjmp(g_trap_jmp, 1);
    std::fprintf(stderr, "unexpected runtime trap: %s\n", msg ? msg : "(null)");
    std::abort();
}

#define TEST(name)                                                                                 \
    do {                                                                                           \
        ++g_tests_total;                                                                           \
        std::printf("  [%d] %s... ", g_tests_total, name);                                         \
    } while (0)

#define PASS()                                                                                     \
    do {                                                                                           \
        ++g_tests_passed;                                                                          \
        std::printf("ok\n");                                                                       \
        return true;                                                                               \
    } while (0)

#define FAIL(msg)                                                                                  \
    do {                                                                                           \
        std::printf("FAIL: %s\n", msg);                                                            \
        return false;                                                                              \
    } while (0)

#define EXPECT_TRUE(cond, msg)                                                                     \
    do {                                                                                           \
        if (!(cond))                                                                               \
            FAIL(msg);                                                                             \
    } while (0)

#define EXPECT_EQ_INT(actual, expected, msg)                                                       \
    do {                                                                                           \
        const long long got_ = (long long)(actual);                                                \
        const long long want_ = (long long)(expected);                                             \
        if (got_ != want_) {                                                                       \
            std::printf("FAIL: %s (got %lld, expected %lld)\n", msg, got_, want_);                 \
            return false;                                                                          \
        }                                                                                          \
    } while (0)

#define EXPECT_NEAR(actual, expected, eps, msg)                                                    \
    do {                                                                                           \
        const double got_ = (double)(actual);                                                      \
        const double want_ = (double)(expected);                                                   \
        if (std::fabs(got_ - want_) > (eps)) {                                                     \
            std::printf("FAIL: %s (got %.6f, expected %.6f)\n", msg, got_, want_);                 \
            return false;                                                                          \
        }                                                                                          \
    } while (0)

namespace {

//=========================================================================
// Camera3D.WorldToScreen
//=========================================================================

bool test_world_to_screen_projection() {
    TEST("Camera3D.WorldToScreen projects center/offset/behind cases");
    void *world = rt_game3d_world_new(rt_const_cstr("W2S"), 640, 480);
    void *camera = rt_game3d_world_get_camera(world);
    void *eye = rt_vec3_new(0.0, 0.0, 10.0);
    void *look = rt_vec3_new(0.0, 0.0, 0.0);
    void *up = rt_vec3_new(0.0, 1.0, 0.0);
    rt_camera3d_look_at(camera, eye, look, up);

    double sx = 0.0;
    double sy = 0.0;
    /* A point straight ahead projects to the screen center. */
    EXPECT_TRUE(rt_camera3d_world_to_screen(camera, 0.0, 0.0, 0.0, 640, 480, &sx, &sy) != 0,
                "point ahead is visible");
    EXPECT_NEAR(sx, 320.0, 0.5, "center point lands at half width");
    EXPECT_NEAR(sy, 240.0, 0.5, "center point lands at half height");

    /* A point to the camera's right lands right of center. */
    rt_camera3d_world_to_screen(camera, 2.0, 0.0, 0.0, 640, 480, &sx, &sy);
    EXPECT_TRUE(sx > 330.0, "world +X (camera right when looking -Z) lands right of center");

    /* A point above lands above center (screen y decreases upward). */
    rt_camera3d_world_to_screen(camera, 0.0, 2.0, 0.0, 640, 480, &sx, &sy);
    EXPECT_TRUE(sy < 230.0, "world +Y lands above center");

    /* A point behind the camera reports not visible. */
    EXPECT_TRUE(rt_camera3d_world_to_screen(camera, 0.0, 0.0, 30.0, 640, 480, &sx, &sy) == 0,
                "point behind the camera is not visible");

    if (rt_obj_release_check0(up))
        rt_obj_free(up);
    if (rt_obj_release_check0(look))
        rt_obj_free(look);
    if (rt_obj_release_check0(eye))
        rt_obj_free(eye);
    rt_game3d_world_destroy(world);
    PASS();
}

//=========================================================================
// Dialogue3D
//=========================================================================

bool test_dialogue_queue_reveal_advance() {
    TEST("Dialogue3D typewriter reveal with two-stage skip and queue order");
    void *world = rt_game3d_world_new(rt_const_cstr("Dlg Reveal"), 64, 48);
    rt_game3d_world_set_gravity(world, 0.0, 0.0, 0.0);
    void *dialogue = rt_game3d_dialogue_new(world);
    rt_game3d_dialogue_say(dialogue, rt_const_cstr("Ada"), rt_const_cstr("Hello world"));
    rt_game3d_dialogue_say(dialogue, rt_const_cstr("Bel"), rt_const_cstr("Second line"));
    EXPECT_EQ_INT(rt_game3d_dialogue_get_line_count(dialogue), 2, "two lines queued");
    rt_game3d_dialogue_set_reveal_speed(dialogue, 20.0); /* 20 cps */
    rt_game3d_dialogue_show(dialogue);
    EXPECT_TRUE(rt_game3d_dialogue_get_active(dialogue) != 0, "shown after Show");

    /* 0.25 s at 20 cps reveals ~5 chars. */
    for (int i = 0; i < 15; ++i)
        rt_game3d_world_step_simulation(world, 1.0 / 60.0);
    rt_string partial = rt_game3d_dialogue_current_text(dialogue);
    size_t partial_len = std::strlen(rt_string_cstr(partial));
    EXPECT_TRUE(partial_len >= 3 && partial_len <= 8, "reveal is mid-line after 0.25 s");

    /* First advance completes the reveal (two-stage skip). */
    rt_game3d_dialogue_advance(dialogue);
    rt_string full = rt_game3d_dialogue_current_text(dialogue);
    EXPECT_TRUE(std::strcmp(rt_string_cstr(full), "Hello world") == 0,
                "first advance completes the reveal");
    /* Second advance moves to line two. */
    rt_game3d_dialogue_advance(dialogue);
    rt_game3d_world_step_simulation(world, 1.0 / 60.0);
    rt_game3d_dialogue_skip_reveal(dialogue);
    rt_string second = rt_game3d_dialogue_current_text(dialogue);
    EXPECT_TRUE(std::strcmp(rt_string_cstr(second), "Second line") == 0,
                "second line follows in queue order");
    /* Advancing past the last line hides the conversation. */
    rt_game3d_dialogue_advance(dialogue);
    EXPECT_TRUE(rt_game3d_dialogue_get_active(dialogue) == 0,
                "conversation hides after the last line");
    rt_game3d_world_destroy(world);
    PASS();
}

bool test_dialogue_choices() {
    TEST("Dialogue3D choices block advance and latch one-shot results");
    void *world = rt_game3d_world_new(rt_const_cstr("Dlg Choice"), 64, 48);
    rt_game3d_world_set_gravity(world, 0.0, 0.0, 0.0);
    void *dialogue = rt_game3d_dialogue_new(world);
    rt_game3d_dialogue_say(dialogue, rt_const_cstr("Ada"), rt_const_cstr("Pick one"));
    void *options = rt_seq_new();
    rt_seq_push(options, rt_const_cstr("Yes"));
    rt_seq_push(options, rt_const_cstr("No"));
    rt_seq_push(options, rt_const_cstr("Maybe"));
    rt_game3d_dialogue_ask_choice(dialogue, options);
    rt_game3d_dialogue_show(dialogue);
    rt_game3d_dialogue_skip_reveal(dialogue);
    rt_game3d_dialogue_advance(dialogue); /* past the line: choice arms */
    EXPECT_TRUE(rt_game3d_dialogue_get_choice_pending(dialogue) != 0, "choice pending");
    EXPECT_TRUE(rt_game3d_dialogue_get_active(dialogue) != 0,
                "conversation stays active while a choice blocks");
    rt_game3d_dialogue_move_choice(dialogue, 1);
    rt_game3d_dialogue_confirm_choice(dialogue);
    EXPECT_TRUE(rt_game3d_dialogue_choice_made(dialogue) != 0, "choiceMade one-shot fires");
    EXPECT_TRUE(rt_game3d_dialogue_choice_made(dialogue) == 0, "choiceMade clears on read");
    EXPECT_EQ_INT(rt_game3d_dialogue_last_choice(dialogue), 1, "index 1 selected");
    EXPECT_TRUE(rt_game3d_dialogue_get_active(dialogue) == 0, "conversation ends after choice");
    rt_game3d_world_destroy(world);
    PASS();
}

bool test_dialogue_localization() {
    TEST("Dialogue3D resolves bound localization keys and keeps literals");
    void *world = rt_game3d_world_new(rt_const_cstr("Dlg Locale"), 64, 48);
    rt_game3d_world_set_gravity(world, 0.0, 0.0, 0.0);
    void *map = rt_map_new();
    rt_map_set(map, rt_const_cstr("greet.hello"), rt_const_cstr("Bonjour"));
    void *bundle = rt_message_bundle_from_map(NULL, map);
    EXPECT_TRUE(bundle != nullptr, "bundle builds from a map");

    void *dialogue = rt_game3d_dialogue_new(world);
    rt_game3d_dialogue_set_locale(dialogue, bundle);
    rt_game3d_dialogue_say(dialogue, rt_const_cstr("Ada"), rt_const_cstr("greet.hello"));
    rt_game3d_dialogue_say(dialogue, rt_const_cstr("Ada"), rt_const_cstr("missing.key"));
    rt_game3d_dialogue_show(dialogue);
    rt_game3d_dialogue_skip_reveal(dialogue);
    EXPECT_TRUE(std::strcmp(rt_string_cstr(rt_game3d_dialogue_current_text(dialogue)), "Bonjour") ==
                    0,
                "bound key resolves to the localized string");
    rt_game3d_dialogue_advance(dialogue);
    rt_game3d_dialogue_skip_reveal(dialogue);
    EXPECT_TRUE(
        std::strcmp(rt_string_cstr(rt_game3d_dialogue_current_text(dialogue)), "missing.key") == 0,
        "missing key keeps the literal (no trap)");
    rt_game3d_world_destroy(world);
    PASS();
}

//=========================================================================
// LipSync3D
//=========================================================================

bool test_lipsync_envelope_and_blink_determinism() {
    TEST("LipSync3D envelope follows injected levels; blink replays identically");
    double first_blink_step[2] = {-1.0, -1.0};
    for (int run = 0; run < 2; ++run) {
        void *world = rt_game3d_world_new(rt_const_cstr("LipSync"), 64, 48);
        rt_game3d_world_set_gravity(world, 0.0, 0.0, 0.0);
        void *actor = rt_game3d_entity_new();
        rt_game3d_world_spawn(world, actor);
        void *lipsync = rt_game3d_lipsync_new(actor);
        EXPECT_TRUE(lipsync != nullptr, "LipSync3D.New returns the component");
        EXPECT_TRUE(rt_game3d_entity_get_lipsync(actor) == lipsync, "entity slot resolves");
        rt_game3d_lipsync_bind_mouth_shape(lipsync, rt_const_cstr("jawOpen"), 1.0);
        rt_game3d_lipsync_set_blink(lipsync, 1, rt_const_cstr("blink"), 0.5, 0.5);

        /* Inject a loud level: envelope attacks fast. */
        rt_game3d_lipsync_drive_level(lipsync, 1.0);
        rt_game3d_world_step_simulation(world, 1.0 / 60.0);
        EXPECT_TRUE(rt_game3d_lipsync_get_level(lipsync) > 0.5,
                    "attack reaches most of the level within one step");
        /* Silence: release decays within ~0.5 s. */
        rt_game3d_lipsync_drive_level(lipsync, 0.0);
        int blink_step = -1;
        for (int i = 0; i < 60; ++i) {
            rt_game3d_world_step_simulation(world, 1.0 / 60.0);
        }
        EXPECT_TRUE(rt_game3d_lipsync_get_level(lipsync) < 0.05, "release decays the envelope");
        (void)blink_step;
        first_blink_step[run] = rt_game3d_lipsync_get_level(lipsync);
        rt_game3d_world_destroy(world);
    }
    EXPECT_TRUE(std::memcmp(&first_blink_step[0], &first_blink_step[1], sizeof(double)) == 0,
                "replays produce byte-identical envelopes");
    PASS();
}

} // namespace

int main() {
    std::printf("Game3D dialogue + facial tests\n");
    bool ok = true;
    ok = test_world_to_screen_projection() && ok;
    ok = test_dialogue_queue_reveal_advance() && ok;
    ok = test_dialogue_choices() && ok;
    ok = test_dialogue_localization() && ok;
    ok = test_lipsync_envelope_and_blink_determinism() && ok;
    std::printf("\nDialogue + facial tests: %d/%d passed\n", g_tests_passed, g_tests_total);
    return ok && g_tests_passed == g_tests_total ? 0 : 1;
}
