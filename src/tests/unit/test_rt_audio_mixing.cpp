//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
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
#include "rt_internal.h"
#include "rt_mixgroup.h"
#include "rt_playlist.h"
#include <cassert>
#include <cstdio>

// Trap handler
extern "C" void vm_trap(const char *msg)
{
    rt_abort(msg);
}

static int tests_passed = 0;
static int tests_total = 0;

#define TEST(name)                                                                                 \
    do                                                                                             \
    {                                                                                              \
        tests_total++;                                                                             \
        printf("  [%d] %s... ", tests_total, name);                                                \
    } while (0)

#define PASS()                                                                                     \
    do                                                                                             \
    {                                                                                              \
        tests_passed++;                                                                            \
        printf("ok\n");                                                                            \
    } while (0)

//=============================================================================
// Mix Group Volume Tests
//=============================================================================

static void test_group_volume_defaults(void)
{
    TEST("Group volumes default to 100");
    // Reset to defaults for clean test state
    rt_audio_set_group_volume(RT_MIXGROUP_MUSIC, 100);
    rt_audio_set_group_volume(RT_MIXGROUP_SFX, 100);

    assert(rt_audio_get_group_volume(RT_MIXGROUP_MUSIC) == 100);
    assert(rt_audio_get_group_volume(RT_MIXGROUP_SFX) == 100);
    PASS();
}

static void test_group_volume_set_get(void)
{
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

static void test_group_volume_clamp_high(void)
{
    TEST("Group volume clamps above 100");
    rt_audio_set_group_volume(RT_MIXGROUP_MUSIC, 150);
    assert(rt_audio_get_group_volume(RT_MIXGROUP_MUSIC) == 100);

    // Restore
    rt_audio_set_group_volume(RT_MIXGROUP_MUSIC, 100);
    PASS();
}

static void test_group_volume_clamp_low(void)
{
    TEST("Group volume clamps below 0");
    rt_audio_set_group_volume(RT_MIXGROUP_SFX, -10);
    assert(rt_audio_get_group_volume(RT_MIXGROUP_SFX) == 0);

    // Restore
    rt_audio_set_group_volume(RT_MIXGROUP_SFX, 100);
    PASS();
}

static void test_group_volume_zero(void)
{
    TEST("Group volume can be set to 0");
    rt_audio_set_group_volume(RT_MIXGROUP_MUSIC, 0);
    assert(rt_audio_get_group_volume(RT_MIXGROUP_MUSIC) == 0);

    // Restore
    rt_audio_set_group_volume(RT_MIXGROUP_MUSIC, 100);
    PASS();
}

static void test_group_volume_invalid_group(void)
{
    TEST("Invalid group ID returns default 100");
    assert(rt_audio_get_group_volume(99) == 100);
    assert(rt_audio_get_group_volume(-1) == 100);
    rt_audio_set_group_volume(99, 50); // No crash, no-op
    PASS();
}

static void test_group_independence(void)
{
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

//=============================================================================
// Crossfade State Tests
//=============================================================================

static void test_crossfade_initially_inactive(void)
{
    TEST("Crossfade not active initially");
    assert(rt_music_is_crossfading() == 0);
    PASS();
}

static void test_crossfade_null_safety(void)
{
    TEST("Crossfade with NULL music is safe");
    rt_music_crossfade_to(NULL, NULL, 500); // No crash
    PASS();
}

static void test_crossfade_update_when_inactive(void)
{
    TEST("Crossfade update when inactive is no-op");
    rt_music_crossfade_update(16); // No crash, no state change
    assert(rt_music_is_crossfading() == 0);
    PASS();
}

//=============================================================================
// Playlist Crossfade Tests
//=============================================================================

static void test_playlist_crossfade_default(void)
{
    TEST("Playlist crossfade disabled by default");
    void *pl = rt_playlist_new();
    assert(pl != NULL);
    assert(rt_playlist_get_crossfade(pl) == 0);
    PASS();
}

static void test_playlist_crossfade_set_get(void)
{
    TEST("Playlist crossfade set/get round-trip");
    void *pl = rt_playlist_new();
    rt_playlist_set_crossfade(pl, 500);
    assert(rt_playlist_get_crossfade(pl) == 500);

    rt_playlist_set_crossfade(pl, 0); // Disable
    assert(rt_playlist_get_crossfade(pl) == 0);
    PASS();
}

static void test_playlist_crossfade_negative_clamped(void)
{
    TEST("Playlist crossfade negative clamped to 0");
    void *pl = rt_playlist_new();
    rt_playlist_set_crossfade(pl, -100);
    assert(rt_playlist_get_crossfade(pl) == 0);
    PASS();
}

static void test_playlist_crossfade_null_safety(void)
{
    TEST("Playlist crossfade NULL safety");
    rt_playlist_set_crossfade(NULL, 500); // No crash
    assert(rt_playlist_get_crossfade(NULL) == 0);
    PASS();
}

//=============================================================================
// Sound Group Playback (NULL safety only — no audio hardware)
//=============================================================================

static void test_sound_play_group_null(void)
{
    TEST("Sound.PlayGroup with NULL sound returns -1");
    assert(rt_sound_play_in_group(NULL, RT_MIXGROUP_SFX) == -1);
    assert(rt_sound_play_ex_in_group(NULL, 100, 0, RT_MIXGROUP_SFX) == -1);
    assert(rt_sound_play_loop_in_group(NULL, 100, 0, RT_MIXGROUP_SFX) == -1);
    PASS();
}

//=============================================================================
// Main
//=============================================================================

int main()
{
    printf("test_rt_audio_mixing:\n");

    // Mix group volumes
    test_group_volume_defaults();
    test_group_volume_set_get();
    test_group_volume_clamp_high();
    test_group_volume_clamp_low();
    test_group_volume_zero();
    test_group_volume_invalid_group();
    test_group_independence();

    // Crossfade state
    test_crossfade_initially_inactive();
    test_crossfade_null_safety();
    test_crossfade_update_when_inactive();

    // Playlist crossfade
    test_playlist_crossfade_default();
    test_playlist_crossfade_set_get();
    test_playlist_crossfade_negative_clamped();
    test_playlist_crossfade_null_safety();

    // Sound group playback
    test_sound_play_group_null();

    printf("\n  %d/%d tests passed\n", tests_passed, tests_total);
    assert(tests_passed == tests_total);
    return 0;
}
