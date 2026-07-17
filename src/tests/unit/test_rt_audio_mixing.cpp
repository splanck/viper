//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_rt_audio_mixing.cpp
// Purpose: Unit tests for audio mix groups (MUSIC/SFX volume), crossfade state,
//   and playlist crossfade settings. Tests API state management without requiring
//   audio hardware (mix group volumes are pure state, no playback needed).
//
// Key invariants:
//   - Group volumes default to 100 and clamp to [0, 100].
//   - Invalid group IDs return default (100) or are no-ops.
//   - Playlist crossfade defaults to 0 (disabled).
//   - Crossfade state query works without active audio context.
//
// Ownership/Lifetime:
//   - Uses runtime library. Playlist objects are GC-managed.
//
// Links: src/runtime/audio/rt_mixgroup.h, src/runtime/audio/rt_playlist.h
//
//===----------------------------------------------------------------------===//

#include "rt.hpp"
#include "rt_audio.h"
#include "rt_internal.h"
#include "rt_mixgroup.h"
#include "rt_playlist.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <thread>

// Trap handler
extern "C" void vm_trap(const char *msg) {
    rt_abort(msg);
}

static int tests_passed = 0;
static int tests_total = 0;

#define TEST(name)                                                                                 \
    do {                                                                                           \
        tests_total++;                                                                             \
        printf("  [%d] %s... ", tests_total, name);                                                \
    } while (0)

#define PASS()                                                                                     \
    do {                                                                                           \
        tests_passed++;                                                                            \
        printf("ok\n");                                                                            \
    } while (0)

//=============================================================================
// Mix Group Volume Tests
//=============================================================================

static void test_group_volume_defaults(void) {
    TEST("Group volumes default to 100");
    // Reset to defaults for clean test state
    rt_audio_set_group_volume(RT_MIXGROUP_MUSIC, 100);
    rt_audio_set_group_volume(RT_MIXGROUP_SFX, 100);

    assert(rt_audio_get_group_volume(RT_MIXGROUP_MUSIC) == 100);
    assert(rt_audio_get_group_volume(RT_MIXGROUP_SFX) == 100);
    PASS();
}

static void test_group_volume_set_get(void) {
    TEST("Group volume set/get round-trip");
    rt_audio_set_group_volume(RT_MIXGROUP_SFX, 50);
    assert(rt_audio_get_group_volume(RT_MIXGROUP_SFX) == 50);

    rt_audio_set_group_volume(RT_MIXGROUP_MUSIC, 75);
    assert(rt_audio_get_group_volume(RT_MIXGROUP_MUSIC) == 75);

    // Restore
    rt_audio_set_group_volume(RT_MIXGROUP_SFX, 100);
    rt_audio_set_group_volume(RT_MIXGROUP_MUSIC, 100);
    PASS();
}

static void test_group_volume_clamp_high(void) {
    TEST("Group volume clamps above 100");
    rt_audio_set_group_volume(RT_MIXGROUP_MUSIC, 150);
    assert(rt_audio_get_group_volume(RT_MIXGROUP_MUSIC) == 100);

    // Restore
    rt_audio_set_group_volume(RT_MIXGROUP_MUSIC, 100);
    PASS();
}

static void test_group_volume_clamp_low(void) {
    TEST("Group volume clamps below 0");
    rt_audio_set_group_volume(RT_MIXGROUP_SFX, -10);
    assert(rt_audio_get_group_volume(RT_MIXGROUP_SFX) == 0);

    // Restore
    rt_audio_set_group_volume(RT_MIXGROUP_SFX, 100);
    PASS();
}

static void test_group_volume_zero(void) {
    TEST("Group volume can be set to 0");
    rt_audio_set_group_volume(RT_MIXGROUP_MUSIC, 0);
    assert(rt_audio_get_group_volume(RT_MIXGROUP_MUSIC) == 0);

    // Restore
    rt_audio_set_group_volume(RT_MIXGROUP_MUSIC, 100);
    PASS();
}

static void test_group_volume_invalid_group(void) {
    TEST("Invalid group ID returns default 100");
    assert(rt_audio_get_group_volume(99) == 100);
    assert(rt_audio_get_group_volume(-1) == 100);
    rt_audio_set_group_volume(99, 50); // No crash, no-op
    PASS();
}

static void test_named_group_embedded_nul_does_not_alias_prefix(void) {
    TEST("Named group embedded NUL does not alias prefix");
    const char raw_name[] = {'c', 'o', 'd', 'e', 'x', '_', 'a', 'u', 'd', '\0', 'x'};
    rt_string full = rt_string_from_bytes(raw_name, sizeof(raw_name));
    rt_string prefix = rt_const_cstr("codex_aud");

    int64_t full_id = rt_audio_register_group(full);
    int64_t prefix_id = rt_audio_register_group(prefix);
    assert(full_id >= 0);
    assert(prefix_id >= 0);
    assert(full_id != prefix_id);
    assert(rt_audio_find_group(full) == full_id);
    assert(rt_audio_find_group(prefix) == prefix_id);

    rt_string stored = rt_audio_group_name(full_id);
    assert(strcmp(rt_string_cstr(stored), "codex_aud_x") == 0);
    rt_string_unref(full);
    PASS();
}

static void test_group_independence(void) {
    TEST("Music and SFX volumes are independent");
    rt_audio_set_group_volume(RT_MIXGROUP_MUSIC, 30);
    rt_audio_set_group_volume(RT_MIXGROUP_SFX, 80);
    assert(rt_audio_get_group_volume(RT_MIXGROUP_MUSIC) == 30);
    assert(rt_audio_get_group_volume(RT_MIXGROUP_SFX) == 80);

    // Restore
    rt_audio_set_group_volume(RT_MIXGROUP_MUSIC, 100);
    rt_audio_set_group_volume(RT_MIXGROUP_SFX, 100);
    PASS();
}

static void test_group_volume_threaded_stress(void) {
    TEST("Group volume set/get is safe under concurrent access");

    auto writer = [](int64_t group) {
        for (int i = 0; i < 1000; i++)
            rt_audio_set_group_volume(group, i % 125 - 10);
    };
    auto reader = []() {
        for (int i = 0; i < 1000; i++) {
            int64_t music = rt_audio_get_group_volume(RT_MIXGROUP_MUSIC);
            int64_t sfx = rt_audio_get_group_volume(RT_MIXGROUP_SFX);
            assert(music >= 0 && music <= 100);
            assert(sfx >= 0 && sfx <= 100);
        }
    };

    std::thread t1(writer, RT_MIXGROUP_MUSIC);
    std::thread t2(writer, RT_MIXGROUP_SFX);
    std::thread t3(reader);
    t1.join();
    t2.join();
    t3.join();

    rt_audio_set_group_volume(RT_MIXGROUP_MUSIC, 100);
    rt_audio_set_group_volume(RT_MIXGROUP_SFX, 100);
    PASS();
}

static void test_master_volume_without_backend_roundtrip(void) {
    TEST("Master volume round-trips without requiring backend init");
    rt_audio_shutdown();

    rt_audio_set_master_volume(33);
    assert(rt_audio_get_master_volume() == 33);
    rt_audio_set_master_volume(-5);
    assert(rt_audio_get_master_volume() == 0);
    rt_audio_set_master_volume(105);
    assert(rt_audio_get_master_volume() == 100);

    rt_audio_set_master_volume(100);
    PASS();
}

static void test_master_volume_threaded_stress(void) {
    TEST("Master volume set/get is safe under concurrent access");

    auto writer = []() {
        for (int i = 0; i < 1000; i++)
            rt_audio_set_master_volume(i % 125 - 10);
    };
    auto reader = []() {
        for (int i = 0; i < 1000; i++) {
            int64_t volume = rt_audio_get_master_volume();
            assert(volume >= 0 && volume <= 100);
        }
    };

    std::thread t1(writer);
    std::thread t2(writer);
    std::thread t3(reader);
    t1.join();
    t2.join();
    t3.join();

    rt_audio_set_master_volume(100);
    PASS();
}

//=============================================================================
// Crossfade State Tests
//=============================================================================

static void test_crossfade_initially_inactive(void) {
    TEST("Crossfade not active initially");
    assert(rt_music_is_crossfading() == 0);
    PASS();
}

static void test_crossfade_null_safety(void) {
    TEST("Crossfade with NULL music is safe");
    rt_music_crossfade_to(NULL, NULL, 500); // No crash
    PASS();
}

static void test_crossfade_update_when_inactive(void) {
    TEST("Crossfade update when inactive is no-op");
    rt_music_crossfade_update(16); // No crash, no state change
    assert(rt_music_is_crossfading() == 0);
    PASS();
}

static void test_audio_update_when_inactive(void) {
    TEST("Audio.Update when inactive is no-op");
    rt_audio_update();
    assert(rt_music_is_crossfading() == 0);
    PASS();
}

//=============================================================================
// Playlist Crossfade Tests
//=============================================================================

static void test_playlist_crossfade_default(void) {
    TEST("Playlist crossfade disabled by default");
    void *pl = rt_playlist_new();
    assert(pl != NULL);
    assert(rt_playlist_get_crossfade(pl) == 0);
    PASS();
}

static void test_playlist_crossfade_set_get(void) {
    TEST("Playlist crossfade set/get round-trip");
    void *pl = rt_playlist_new();
    rt_playlist_set_crossfade(pl, 500);
    assert(rt_playlist_get_crossfade(pl) == 500);

    rt_playlist_set_crossfade(pl, 0); // Disable
    assert(rt_playlist_get_crossfade(pl) == 0);
    PASS();
}

static void test_playlist_crossfade_negative_clamped(void) {
    TEST("Playlist crossfade negative clamped to 0");
    void *pl = rt_playlist_new();
    rt_playlist_set_crossfade(pl, -100);
    assert(rt_playlist_get_crossfade(pl) == 0);
    PASS();
}

static void test_playlist_crossfade_null_safety(void) {
    TEST("Playlist crossfade NULL safety");
    rt_playlist_set_crossfade(NULL, 500); // No crash
    assert(rt_playlist_get_crossfade(NULL) == 0);
    PASS();
}

static void test_playlist_null_paths_are_empty_strings(void) {
    TEST("Playlist add/insert NULL paths store empty strings");
    void *pl = rt_playlist_new();
    rt_playlist_add(pl, NULL);
    rt_playlist_insert(pl, 0, NULL);
    assert(rt_playlist_len(pl) == 2);

    rt_string first = rt_playlist_get(pl, 0);
    rt_string second = rt_playlist_get(pl, 1);
    assert(first != NULL && rt_str_len(first) == 0);
    assert(second != NULL && rt_str_len(second) == 0);
    rt_str_release_maybe(first);
    rt_str_release_maybe(second);
    PASS();
}

//=============================================================================
// Sound Group Playback (NULL safety only — no audio hardware)
//=============================================================================

static void test_sound_play_group_null(void) {
    TEST("Sound.PlayGroup with NULL sound returns -1");
    assert(rt_sound_play_in_group(NULL, RT_MIXGROUP_SFX) == -1);
    assert(rt_sound_play_ex_in_group(NULL, 100, 0, RT_MIXGROUP_SFX) == -1);
    assert(rt_sound_play_loop_in_group(NULL, 100, 0, RT_MIXGROUP_SFX) == -1);
    PASS();
}

//=============================================================================
// Main
//=============================================================================

int main() {
    printf("test_rt_audio_mixing:\n");

    // Mix group volumes
    test_group_volume_defaults();
    test_group_volume_set_get();
    test_group_volume_clamp_high();
    test_group_volume_clamp_low();
    test_group_volume_zero();
    test_group_volume_invalid_group();
    test_named_group_embedded_nul_does_not_alias_prefix();
    test_group_independence();
    test_group_volume_threaded_stress();
    test_master_volume_without_backend_roundtrip();
    test_master_volume_threaded_stress();

    // Crossfade state
    test_crossfade_initially_inactive();
    test_crossfade_null_safety();
    test_crossfade_update_when_inactive();
    test_audio_update_when_inactive();

    // Playlist crossfade
    test_playlist_crossfade_default();
    test_playlist_crossfade_set_get();
    test_playlist_crossfade_negative_clamped();
    test_playlist_crossfade_null_safety();
    test_playlist_null_paths_are_empty_strings();

    // Sound group playback
    test_sound_play_group_null();

    printf("\n  %d/%d tests passed\n", tests_passed, tests_total);
    assert(tests_passed == tests_total);
    return 0;
}
