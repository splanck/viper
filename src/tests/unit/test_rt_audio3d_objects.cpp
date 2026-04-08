//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/unit/test_rt_audio3d_objects.cpp
// Purpose: Unit tests for AudioListener3D / AudioSource3D bindings and basic
//   playback lifecycle.
//
//===----------------------------------------------------------------------===//

#include "rt_audio.h"
#include "rt_audio3d.h"
#include "rt_audiolistener3d.h"
#include "rt_audiosource3d.h"
#include "rt_canvas3d.h"
#include "rt_scene3d.h"

#include <cmath>
#include <cstdio>

extern "C" {
extern void *rt_vec3_new(double x, double y, double z);
extern double rt_vec3_x(void *v);
extern double rt_vec3_y(void *v);
extern double rt_vec3_z(void *v);
extern void *rt_synth_tone(int64_t hz, int64_t duration_ms, int64_t volume);
}

static int tests_passed = 0;
static int tests_run = 0;

#define EXPECT_TRUE(cond, msg)                                                                     \
    do {                                                                                           \
        tests_run++;                                                                               \
        if (!(cond))                                                                               \
            std::fprintf(stderr, "FAIL: %s\n", msg);                                               \
        else                                                                                       \
            tests_passed++;                                                                        \
    } while (0)

#define EXPECT_NEAR(a, b, eps, msg)                                                                \
    do {                                                                                           \
        tests_run++;                                                                               \
        if (std::fabs((double)(a) - (double)(b)) > (eps))                                          \
            std::fprintf(stderr,                                                                   \
                         "FAIL: %s (got %f, expected %f)\n",                                       \
                         msg,                                                                      \
                         (double)(a),                                                              \
                         (double)(b));                                                             \
        else                                                                                       \
            tests_passed++;                                                                        \
    } while (0)

static void test_listener_follows_bound_camera() {
    void *camera = rt_camera3d_new(60.0, 1.0, 0.1, 100.0);
    void *listener = rt_audiolistener3d_new();
    void *pos;
    void *forward;
    void *vel;

    rt_camera3d_look_at(
        camera, rt_vec3_new(1.0, 2.0, 6.0), rt_vec3_new(1.0, 2.0, 5.0), rt_vec3_new(0.0, 1.0, 0.0));
    rt_audiolistener3d_bind_camera(listener, camera);
    rt_audiolistener3d_set_is_active(listener, 1);
    rt_audio3d_sync_bindings(0.25);

    pos = rt_audiolistener3d_get_position(listener);
    forward = rt_audiolistener3d_get_forward(listener);
    EXPECT_NEAR(rt_vec3_x(pos), 1.0, 0.001, "AudioListener3D follows camera X");
    EXPECT_NEAR(rt_vec3_y(pos), 2.0, 0.001, "AudioListener3D follows camera Y");
    EXPECT_NEAR(rt_vec3_z(pos), 6.0, 0.001, "AudioListener3D follows camera Z");
    EXPECT_NEAR(rt_vec3_x(forward), 0.0, 0.001, "AudioListener3D derives camera forward X");
    EXPECT_NEAR(rt_vec3_y(forward), 0.0, 0.001, "AudioListener3D derives camera forward Y");
    EXPECT_NEAR(rt_vec3_z(forward), -1.0, 0.001, "AudioListener3D derives camera forward Z");

    rt_camera3d_look_at(
        camera, rt_vec3_new(3.0, 2.0, 6.0), rt_vec3_new(3.0, 2.0, 5.0), rt_vec3_new(0.0, 1.0, 0.0));
    rt_audio3d_sync_bindings(0.5);
    vel = rt_audiolistener3d_get_velocity(listener);
    EXPECT_NEAR(rt_vec3_x(vel), 4.0, 0.05, "AudioListener3D computes velocity from bound camera motion");
    EXPECT_NEAR(rt_vec3_z(vel), 0.0, 0.05, "AudioListener3D velocity stays flat on unchanged Z");
}

static void test_source_follows_bound_node_in_world_space() {
    void *scene = rt_scene3d_new();
    void *parent = rt_scene_node3d_new();
    void *child = rt_scene_node3d_new();
    void *source = rt_audiosource3d_new(NULL);
    void *pos;
    void *vel;

    rt_scene_node3d_set_position(parent, 2.0, 0.0, 3.0);
    rt_scene_node3d_set_position(child, -1.0, 0.5, 4.0);
    rt_scene_node3d_add_child(parent, child);
    rt_scene3d_add(scene, parent);

    rt_audiosource3d_bind_node(source, child);
    rt_scene3d_sync_bindings(scene, 0.25);
    pos = rt_audiosource3d_get_position(source);
    EXPECT_NEAR(rt_vec3_x(pos), 1.0, 0.001, "AudioSource3D follows bound node world X");
    EXPECT_NEAR(rt_vec3_y(pos), 0.5, 0.001, "AudioSource3D follows bound node world Y");
    EXPECT_NEAR(rt_vec3_z(pos), 7.0, 0.001, "AudioSource3D follows bound node world Z");

    rt_scene_node3d_set_position(parent, 4.0, 0.0, 4.0);
    rt_scene3d_sync_bindings(scene, 0.5);
    pos = rt_audiosource3d_get_position(source);
    vel = rt_audiosource3d_get_velocity(source);
    EXPECT_NEAR(rt_vec3_x(pos), 3.0, 0.001, "AudioSource3D updates cached world X after parent motion");
    EXPECT_NEAR(rt_vec3_z(pos), 8.0, 0.001, "AudioSource3D updates cached world Z after parent motion");
    EXPECT_NEAR(rt_vec3_x(vel), 4.0, 0.05, "AudioSource3D computes bound-node X velocity");
    EXPECT_NEAR(rt_vec3_z(vel), 2.0, 0.05, "AudioSource3D computes bound-node Z velocity");
}

static void test_source_play_stop_when_audio_is_available() {
    if (!rt_audio_is_available()) {
        EXPECT_TRUE(true, "AudioSource3D playback skipped when audio backend is unavailable");
        return;
    }
    if (!rt_audio_init()) {
        EXPECT_TRUE(true, "AudioSource3D playback skipped when audio init fails");
        return;
    }

    void *listener = rt_audiolistener3d_new();
    void *sound = rt_synth_tone(440, 200, 80);
    void *source = rt_audiosource3d_new(sound);
    int64_t voice;

    rt_audiolistener3d_set_position(listener, rt_vec3_new(0.0, 0.0, 0.0));
    rt_audiolistener3d_set_forward(listener, rt_vec3_new(0.0, 0.0, -1.0));
    rt_audiolistener3d_set_is_active(listener, 1);
    rt_audiosource3d_set_position(source, rt_vec3_new(2.0, 0.0, 0.0));
    rt_audiosource3d_set_volume(source, 75);
    rt_audiosource3d_set_max_distance(source, 12.0);
    voice = rt_audiosource3d_play(source);

    EXPECT_TRUE(voice > 0, "AudioSource3D.Play returns a live voice when audio is available");
    EXPECT_TRUE(rt_audiosource3d_get_is_playing(source) != 0,
                "AudioSource3D reports IsPlaying after starting playback");
    EXPECT_TRUE(rt_audiosource3d_get_voice_id(source) == voice,
                "AudioSource3D exposes the playing voice id");

    rt_audiosource3d_stop(source);
    EXPECT_TRUE(rt_audiosource3d_get_is_playing(source) == 0,
                "AudioSource3D.Stop clears active playback state");
    EXPECT_TRUE(rt_audiosource3d_get_voice_id(source) == 0,
                "AudioSource3D.Stop clears VoiceId");
}

int main() {
    test_listener_follows_bound_camera();
    test_source_follows_bound_node_in_world_space();
    test_source_play_stop_when_audio_is_available();

    std::printf("Audio3D object tests: %d/%d passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
