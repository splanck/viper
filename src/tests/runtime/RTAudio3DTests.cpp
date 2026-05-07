//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//

#include "rt_audio3d.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>

namespace {

struct Vec3 {
    double x;
    double y;
    double z;
};

int64_t g_next_voice = 1;
int64_t g_next_play_result = 0;
int64_t g_last_play_volume = -1;
int64_t g_last_play_pan = 0;
int64_t g_last_update_voice = -1;
int64_t g_last_update_volume = -1;
int64_t g_last_update_pan = 0;

void reset_audio_stub_state() {
    g_next_play_result = 0;
    g_last_play_volume = -1;
    g_last_play_pan = 0;
    g_last_update_voice = -1;
    g_last_update_volume = -1;
    g_last_update_pan = 0;
}

} // namespace

extern "C" double rt_vec3_x(void *v) {
    return static_cast<Vec3 *>(v)->x;
}

extern "C" double rt_vec3_y(void *v) {
    return static_cast<Vec3 *>(v)->y;
}

extern "C" double rt_vec3_z(void *v) {
    return static_cast<Vec3 *>(v)->z;
}

extern "C" int64_t rt_sound_play_ex(void *, int64_t volume, int64_t pan) {
    g_last_play_volume = volume;
    g_last_play_pan = pan;
    if (g_next_play_result != 0)
        return g_next_play_result;
    return g_next_voice++;
}

extern "C" void rt_voice_set_volume(int64_t voice, int64_t volume) {
    g_last_update_voice = voice;
    g_last_update_volume = volume;
}

extern "C" void rt_voice_set_pan(int64_t voice, int64_t pan) {
    g_last_update_voice = voice;
    g_last_update_pan = pan;
}

static void test_origin_has_full_volume_and_zero_pan() {
    Vec3 listener{0.0, 0.0, 0.0};
    Vec3 forward{0.0, 0.0, -1.0};
    Vec3 source{0.0, 0.0, 0.0};
    reset_audio_stub_state();

    rt_audio3d_set_listener(&listener, &forward);
    int64_t voice = rt_audio3d_play_at(reinterpret_cast<void *>(1), &source, 10.0, 80);

    assert(voice > 0);
    assert(g_last_play_volume == 80);
    assert(g_last_play_pan == 0);
}

static void test_pan_and_attenuation_are_derived_from_position() {
    Vec3 listener{0.0, 0.0, 0.0};
    Vec3 forward{0.0, 0.0, -1.0};
    Vec3 source{5.0, 0.0, 0.0};
    reset_audio_stub_state();

    rt_audio3d_set_listener(&listener, &forward);
    int64_t voice = rt_audio3d_play_at(reinterpret_cast<void *>(1), &source, 10.0, 100);

    assert(voice > 0);
    assert(g_last_play_volume == 50);
    assert(g_last_play_pan == 100);
}

static void test_update_voice_reuses_original_base_volume_and_distance() {
    Vec3 listener{0.0, 0.0, 0.0};
    Vec3 forward{0.0, 0.0, -1.0};
    Vec3 source{5.0, 0.0, 0.0};
    reset_audio_stub_state();

    rt_audio3d_set_listener(&listener, &forward);
    int64_t voice = rt_audio3d_play_at(reinterpret_cast<void *>(1), &source, 10.0, 40);
    assert(voice > 0);
    assert(g_last_play_volume == 20);

    reset_audio_stub_state();
    rt_audio3d_update_voice(voice, &source, 0.0);

    assert(g_last_update_voice == voice);
    assert(g_last_update_volume == 20);
    assert(g_last_update_pan == 100);
}

static void test_invalid_inputs_are_ignored() {
    Vec3 listener{0.0, 0.0, 0.0};
    Vec3 forward{0.0, 0.0, -1.0};
    reset_audio_stub_state();

    rt_audio3d_set_listener(&listener, &forward);
    assert(rt_audio3d_play_at(nullptr, &listener, 10.0, 100) == 0);
    assert(rt_audio3d_play_at(reinterpret_cast<void *>(1), nullptr, 10.0, 100) == 0);
    rt_audio3d_update_voice(0, &listener, 5.0);
    assert(g_last_update_voice == -1);
}

static void test_play_at_normalizes_backend_failure_to_zero() {
    Vec3 source{0.0, 0.0, 0.0};
    reset_audio_stub_state();
    g_next_play_result = -1;

    int64_t voice = rt_audio3d_play_at(reinterpret_cast<void *>(1), &source, 10.0, 100);

    assert(voice == 0);
}

static void test_set_listener_accepts_partial_null_inputs() {
    Vec3 forward{1.0, 0.0, 0.0};
    Vec3 source{0.0, 0.0, 5.0};
    reset_audio_stub_state();

    rt_audio3d_set_listener(nullptr, &forward);
    int64_t voice = rt_audio3d_play_at(reinterpret_cast<void *>(1), &source, 10.0, 100);

    assert(voice > 0);
    assert(g_last_play_pan == 100);
}

static void test_nonfinite_positions_do_not_escape_clamps() {
    Vec3 listener{0.0, 0.0, 0.0};
    Vec3 forward{0.0, 0.0, -1.0};
    Vec3 source{NAN, INFINITY, -INFINITY};
    reset_audio_stub_state();

    rt_audio3d_set_listener(&listener, &forward);
    int64_t voice = rt_audio3d_play_at(reinterpret_cast<void *>(1), &source, NAN, 1000);

    assert(voice > 0);
    assert(g_last_play_volume >= 0 && g_last_play_volume <= 100);
    assert(g_last_play_pan >= -100 && g_last_play_pan <= 100);
}

static void test_voice_tracking_overwrites_as_ring() {
    Vec3 listener{0.0, 0.0, 0.0};
    Vec3 forward{0.0, 0.0, -1.0};
    Vec3 source{5.0, 0.0, 0.0};
    reset_audio_stub_state();
    rt_audio3d_set_listener(&listener, &forward);

    int64_t tracked_after_wrap = 0;
    for (int i = 0; i < 66; i++) {
        int64_t voice = rt_audio3d_play_at(reinterpret_cast<void *>(1), &source, 10.0, 40);
        assert(voice > 0);
        if (i == 64)
            tracked_after_wrap = voice;
    }

    reset_audio_stub_state();
    rt_audio3d_update_voice(tracked_after_wrap, &source, 0.0);

    assert(g_last_update_voice == tracked_after_wrap);
    assert(g_last_update_volume == 20);
    assert(g_last_update_pan == 100);
}

int main() {
    test_origin_has_full_volume_and_zero_pan();
    test_pan_and_attenuation_are_derived_from_position();
    test_update_voice_reuses_original_base_volume_and_distance();
    test_invalid_inputs_are_ignored();
    test_play_at_normalizes_backend_failure_to_zero();
    test_set_listener_accepts_partial_null_inputs();
    test_nonfinite_positions_do_not_escape_clamps();
    test_voice_tracking_overwrites_as_ring();
    std::printf("RTAudio3DTests passed.\n");
    return 0;
}
