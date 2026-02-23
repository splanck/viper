//===----------------------------------------------------------------------===//
// RTAudioIntegrationTests.cpp - Integration tests for audio + playlist APIs
//
// Tests the audio system and playlist management APIs in a headless
// environment. Audio hardware is optional — the runtime gracefully
// degrades when no device is available. These tests exercise the API
// surface, verify null-safety, and confirm playlist manipulation semantics.
//===----------------------------------------------------------------------===//

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C"
{
#include "rt_audio.h"
#include "rt_internal.h"
#include "rt_object.h"
#include "rt_playlist.h"
#include "rt_string.h"

    void vm_trap(const char *msg)
    {
        fprintf(stderr, "TRAP: %s\n", msg);
        rt_abort(msg);
    }
}

static int tests_run = 0;
static int tests_passed = 0;

#define ASSERT(cond, msg)                                                                          \
    do                                                                                             \
    {                                                                                              \
        tests_run++;                                                                               \
        if (!(cond))                                                                               \
        {                                                                                          \
            fprintf(stderr, "FAIL [%s:%d]: %s\n", __FILE__, __LINE__, msg);                        \
        }                                                                                          \
        else                                                                                       \
        {                                                                                          \
            tests_passed++;                                                                        \
        }                                                                                          \
    } while (0)

static rt_string make_str(const char *s)
{
    return rt_string_from_bytes(s, strlen(s));
}

//=============================================================================
// Audio system tests (headless-safe)
//=============================================================================

static void test_audio_init()
{
    // Init may succeed or fail depending on hardware — both are valid
    int64_t result = rt_audio_init();
    ASSERT(result == 0 || result == 1, "audio init returns 0 or 1");
}

static void test_audio_volume()
{
    // Volume functions are no-ops without hardware but shouldn't crash
    rt_audio_set_master_volume(75);
    int64_t vol = rt_audio_get_master_volume();
    // If audio initialized, vol should be 75; if not, default 100 or 0
    ASSERT(vol >= 0 && vol <= 100, "master volume in valid range");
}

static void test_audio_pause_resume()
{
    // These are safe no-ops without hardware
    rt_audio_pause_all();
    rt_audio_resume_all();
    rt_audio_stop_all_sounds();
    ASSERT(1, "pause/resume/stop don't crash");
}

static void test_sound_null_safety()
{
    // NULL sound handle operations
    rt_sound_free(NULL);
    int64_t voice = rt_sound_play(NULL);
    ASSERT(voice == -1, "play null sound returns -1");

    int64_t voice2 = rt_sound_play_ex(NULL, 50, 0);
    ASSERT(voice2 == -1, "play_ex null sound returns -1");

    int64_t voice3 = rt_sound_play_loop(NULL, 50, 0);
    ASSERT(voice3 == -1, "play_loop null sound returns -1");
}

static void test_voice_null_safety()
{
    // Voice operations on invalid IDs
    rt_voice_stop(0);
    rt_voice_stop(999999);
    rt_voice_set_volume(0, 50);
    rt_voice_set_pan(0, 0);
    ASSERT(rt_voice_is_playing(0) == 0, "invalid voice not playing");
    ASSERT(rt_voice_is_playing(999999) == 0, "invalid voice not playing");
}

static void test_music_null_safety()
{
    rt_music_free(NULL);
    rt_music_play(NULL, 0);
    rt_music_stop(NULL);
    rt_music_pause(NULL);
    rt_music_resume(NULL);
    rt_music_set_volume(NULL, 50);
    ASSERT(rt_music_get_volume(NULL) == 0, "null music volume = 0");
    ASSERT(rt_music_is_playing(NULL) == 0, "null music not playing");
    ASSERT(rt_music_get_position(NULL) == 0, "null music position = 0");
    ASSERT(rt_music_get_duration(NULL) == 0, "null music duration = 0");
    rt_music_seek(NULL, 0);
    ASSERT(1, "music null operations don't crash");
}

//=============================================================================
// Playlist management tests (pure data structure, no audio needed)
//=============================================================================

static void test_playlist_new()
{
    void *pl = rt_playlist_new();
    ASSERT(pl != NULL, "playlist created");
    ASSERT(rt_playlist_len(pl) == 0, "new playlist is empty");
    ASSERT(rt_playlist_get_current(pl) == -1, "no current track");
    ASSERT(rt_playlist_is_playing(pl) == 0, "not playing");
    ASSERT(rt_playlist_is_paused(pl) == 0, "not paused");
}

static void test_playlist_add_remove()
{
    void *pl = rt_playlist_new();
    rt_string track1 = make_str("track1.wav");
    rt_string track2 = make_str("track2.wav");
    rt_string track3 = make_str("track3.wav");

    rt_playlist_add(pl, track1);
    ASSERT(rt_playlist_len(pl) == 1, "added 1 track");

    rt_playlist_add(pl, track2);
    rt_playlist_add(pl, track3);
    ASSERT(rt_playlist_len(pl) == 3, "added 3 tracks");

    // Verify track at index
    rt_string got = rt_playlist_get(pl, 0);
    if (got)
    {
        ASSERT(strcmp(rt_string_cstr(got), "track1.wav") == 0, "track 0 = track1.wav");
    }

    got = rt_playlist_get(pl, 2);
    if (got)
    {
        ASSERT(strcmp(rt_string_cstr(got), "track3.wav") == 0, "track 2 = track3.wav");
    }

    // Remove middle track
    rt_playlist_remove(pl, 1);
    ASSERT(rt_playlist_len(pl) == 2, "removed 1 track");

    got = rt_playlist_get(pl, 1);
    if (got)
    {
        ASSERT(strcmp(rt_string_cstr(got), "track3.wav") == 0,
               "after remove: track 1 = track3.wav");
    }
}

static void test_playlist_insert()
{
    void *pl = rt_playlist_new();
    rt_string a = make_str("a.wav");
    rt_string b = make_str("b.wav");
    rt_string c = make_str("c.wav");

    rt_playlist_add(pl, a);
    rt_playlist_add(pl, c);
    ASSERT(rt_playlist_len(pl) == 2, "2 tracks");

    // Insert b at position 1 (between a and c)
    rt_playlist_insert(pl, 1, b);
    ASSERT(rt_playlist_len(pl) == 3, "3 tracks after insert");

    rt_string got = rt_playlist_get(pl, 1);
    if (got)
    {
        ASSERT(strcmp(rt_string_cstr(got), "b.wav") == 0, "inserted at position 1");
    }
}

static void test_playlist_clear()
{
    void *pl = rt_playlist_new();
    rt_playlist_add(pl, make_str("x.wav"));
    rt_playlist_add(pl, make_str("y.wav"));
    ASSERT(rt_playlist_len(pl) == 2, "2 tracks before clear");

    rt_playlist_clear(pl);
    ASSERT(rt_playlist_len(pl) == 0, "empty after clear");
    ASSERT(rt_playlist_get_current(pl) == -1, "no current after clear");
}

static void test_playlist_volume()
{
    void *pl = rt_playlist_new();

    rt_playlist_set_volume(pl, 80);
    ASSERT(rt_playlist_get_volume(pl) == 80, "volume = 80");

    rt_playlist_set_volume(pl, 0);
    ASSERT(rt_playlist_get_volume(pl) == 0, "volume = 0");

    rt_playlist_set_volume(pl, 100);
    ASSERT(rt_playlist_get_volume(pl) == 100, "volume = 100");
}

static void test_playlist_shuffle_repeat()
{
    void *pl = rt_playlist_new();

    // Shuffle
    ASSERT(rt_playlist_get_shuffle(pl) == 0, "shuffle off by default");
    rt_playlist_set_shuffle(pl, 1);
    ASSERT(rt_playlist_get_shuffle(pl) == 1, "shuffle on");
    rt_playlist_set_shuffle(pl, 0);
    ASSERT(rt_playlist_get_shuffle(pl) == 0, "shuffle off");

    // Repeat modes
    ASSERT(rt_playlist_get_repeat(pl) == 0, "no repeat by default");
    rt_playlist_set_repeat(pl, 1);
    ASSERT(rt_playlist_get_repeat(pl) == 1, "repeat all");
    rt_playlist_set_repeat(pl, 2);
    ASSERT(rt_playlist_get_repeat(pl) == 2, "repeat one");
    rt_playlist_set_repeat(pl, 0);
    ASSERT(rt_playlist_get_repeat(pl) == 0, "no repeat");
}

static void test_playlist_navigation()
{
    void *pl = rt_playlist_new();
    rt_playlist_add(pl, make_str("one.wav"));
    rt_playlist_add(pl, make_str("two.wav"));
    rt_playlist_add(pl, make_str("three.wav"));

    // Jump to track
    rt_playlist_jump(pl, 1);
    ASSERT(rt_playlist_get_current(pl) == 1, "jumped to track 1");

    // Next
    rt_playlist_next(pl);
    ASSERT(rt_playlist_get_current(pl) == 2, "next -> track 2");

    // Prev
    rt_playlist_prev(pl);
    ASSERT(rt_playlist_get_current(pl) == 1, "prev -> track 1");

    // Jump to beginning
    rt_playlist_jump(pl, 0);
    ASSERT(rt_playlist_get_current(pl) == 0, "jumped to track 0");
}

static void test_playlist_update_no_crash()
{
    void *pl = rt_playlist_new();
    rt_playlist_add(pl, make_str("song.wav"));

    // Update should not crash even without audio
    rt_playlist_update(pl);
    rt_playlist_update(pl);
    ASSERT(1, "playlist update doesn't crash");
}

static void test_playlist_null_safety()
{
    // All operations on NULL should be safe
    rt_playlist_add(NULL, make_str("x.wav"));
    rt_playlist_insert(NULL, 0, make_str("x.wav"));
    rt_playlist_remove(NULL, 0);
    rt_playlist_clear(NULL);
    ASSERT(rt_playlist_len(NULL) == 0, "null len = 0");
    // rt_playlist_get returns empty string for null/invalid, not NULL
    {
        rt_string got = rt_playlist_get(NULL, 0);
        ASSERT(got != NULL, "null playlist get returns non-null (empty string)");
    }
    rt_playlist_play(NULL);
    rt_playlist_pause(NULL);
    rt_playlist_stop(NULL);
    rt_playlist_next(NULL);
    rt_playlist_prev(NULL);
    rt_playlist_jump(NULL, 0);
    ASSERT(rt_playlist_get_current(NULL) == -1, "null current = -1");
    ASSERT(rt_playlist_is_playing(NULL) == 0, "null not playing");
    ASSERT(rt_playlist_is_paused(NULL) == 0, "null not paused");
    ASSERT(rt_playlist_get_volume(NULL) == 0, "null volume = 0");
    rt_playlist_set_volume(NULL, 50);
    rt_playlist_set_shuffle(NULL, 1);
    ASSERT(rt_playlist_get_shuffle(NULL) == 0, "null shuffle = 0");
    rt_playlist_set_repeat(NULL, 1);
    ASSERT(rt_playlist_get_repeat(NULL) == 0, "null repeat = 0");
    rt_playlist_update(NULL);
    ASSERT(1, "all null operations safe");
}

static void test_playlist_bounds()
{
    void *pl = rt_playlist_new();
    rt_playlist_add(pl, make_str("a.wav"));

    // Out-of-bounds operations return empty string
    {
        rt_string got1 = rt_playlist_get(pl, -1);
        ASSERT(got1 != NULL && rt_str_len(got1) == 0, "negative index = empty string");
        rt_string got2 = rt_playlist_get(pl, 100);
        ASSERT(got2 != NULL && rt_str_len(got2) == 0, "out of bounds = empty string");
    }

    rt_playlist_remove(pl, 99); // should not crash
    rt_playlist_jump(pl, 99);   // should not crash
    ASSERT(1, "out-of-bounds operations safe");
}

//=============================================================================
// Bug-fix regression tests
//=============================================================================

// C-1 / C-2 / C-3: Playlist shuffle_order memory lifecycle.
//
// Before the fix:
//   C-2: generate_shuffle_order() abandoned the old shuffle_order seq on every
//        reshuffle without releasing it.
//   C-3: rt_playlist_clear() set shuffle_order = NULL without releasing it.
//   C-1: rt_playlist_new() never registered a finalizer, so all of the above
//        also leaked if the playlist was GC'd without an explicit call to clear.
//
// After the fix, all three release paths correctly call rt_obj_release_check0
// + rt_obj_free. These tests verify no crash occurs through all lifecycle
// transitions that previously triggered the leaks.
static void test_playlist_shuffle_lifecycle()
{
    void *pl = rt_playlist_new();
    ASSERT(pl != NULL, "playlist created");

    // Enable shuffle before adding tracks (empty → no shuffle_order generated yet)
    rt_playlist_set_shuffle(pl, 1);
    ASSERT(rt_playlist_get_shuffle(pl) == 1, "shuffle enabled");

    // Adding the first track generates shuffle_order for the first time (nothing to release)
    rt_playlist_add(pl, make_str("a.wav"));
    ASSERT(rt_playlist_len(pl) == 1, "1 track");

    // Each subsequent add reshuffles — C-2: old shuffle_order must be released, not leaked
    rt_playlist_add(pl, make_str("b.wav"));
    rt_playlist_add(pl, make_str("c.wav"));
    ASSERT(rt_playlist_len(pl) == 3, "3 tracks after adds");

    // Toggling shuffle off then on re-generates shuffle_order (C-2 path again)
    rt_playlist_set_shuffle(pl, 0);
    rt_playlist_set_shuffle(pl, 1);
    ASSERT(rt_playlist_get_shuffle(pl) == 1, "shuffle re-enabled");

    // Clear — C-3: shuffle_order must be released, not leaked
    rt_playlist_clear(pl);
    ASSERT(rt_playlist_len(pl) == 0, "empty after clear");
    ASSERT(rt_playlist_get_current(pl) == -1, "no current after clear");

    // Post-clear add — shuffle_order must regenerate cleanly from NULL
    rt_playlist_add(pl, make_str("d.wav"));
    ASSERT(rt_playlist_len(pl) == 1, "can add after clear+shuffle");

    ASSERT(1, "shuffle lifecycle: no crash");
}

// C-2: Stress the reshuffle path with many cycles.
// If the old shuffle_order is leaked each time, ASAN / valgrind will catch it.
static void test_playlist_shuffle_many_reshuffles()
{
    void *pl = rt_playlist_new();
    rt_playlist_set_shuffle(pl, 1);

    // 20 adds with shuffle on = 20 shuffle_order generations; each must release the previous
    for (int i = 0; i < 20; i++)
        rt_playlist_add(pl, make_str("track.wav"));
    ASSERT(rt_playlist_len(pl) == 20, "20 tracks");

    // Toggle shuffle 10 times = 10 more release/reallocate cycles
    for (int i = 0; i < 10; i++)
    {
        rt_playlist_set_shuffle(pl, 0);
        rt_playlist_set_shuffle(pl, 1);
    }
    ASSERT(rt_playlist_get_shuffle(pl) == 1, "shuffle on after toggles");

    rt_playlist_clear(pl); // C-3: final release
    ASSERT(rt_playlist_len(pl) == 0, "cleared");

    ASSERT(1, "many reshuffles: no crash");
}

// C-3: Clear with shuffle enabled — shuffle_order must be released.
static void test_playlist_clear_releases_shuffle_order()
{
    void *pl = rt_playlist_new();
    rt_playlist_set_shuffle(pl, 1);
    rt_playlist_add(pl, make_str("x.wav"));
    rt_playlist_add(pl, make_str("y.wav"));

    // Multiple clears must each handle NULL / non-NULL shuffle_order safely
    rt_playlist_clear(pl);
    ASSERT(rt_playlist_len(pl) == 0, "cleared once");

    rt_playlist_clear(pl); // Second clear: shuffle_order is NULL — must not double-free
    ASSERT(rt_playlist_len(pl) == 0, "cleared twice (idempotent)");

    ASSERT(1, "clear releases shuffle_order safely");
}

//=============================================================================
// H-7: WAV sample_rate validation
//
// A WAV file with sample_rate=0 in its header previously caused a division-
// by-zero crash inside the resampler. After the fix, parse_wav_header rejects
// invalid sample rates and vaud_load_sound returns NULL gracefully.
//=============================================================================

// Write a minimal PCM WAV file with the given sample_rate to `path`.
// Returns 1 on success, 0 if the temp file could not be written.
static int write_test_wav(const char *path, uint32_t sample_rate)
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return 0;

    // RIFF/WAVE header (12 bytes)
    fwrite("RIFF", 1, 4, f);
    uint32_t riff_sz = 38; // = 4 (WAVE) + 24 (fmt chunk) + 10 (data chunk)
    fwrite(&riff_sz, 4, 1, f);
    fwrite("WAVE", 1, 4, f);

    // fmt chunk (24 bytes)
    fwrite("fmt ", 1, 4, f);
    uint32_t fmt_sz = 16;
    fwrite(&fmt_sz, 4, 1, f);
    uint16_t audio_fmt = 1; // PCM
    fwrite(&audio_fmt, 2, 1, f);
    uint16_t channels = 1;
    fwrite(&channels, 2, 1, f);
    fwrite(&sample_rate, 4, 1, f);
    uint32_t byte_rate = sample_rate * 2; // sr * ch * bytes_per_sample
    fwrite(&byte_rate, 4, 1, f);
    uint16_t block_align = 2; // ch * bytes_per_sample
    fwrite(&block_align, 2, 1, f);
    uint16_t bits = 16;
    fwrite(&bits, 2, 1, f);

    // data chunk (10 bytes)
    fwrite("data", 1, 4, f);
    uint32_t data_sz = 2; // one 16-bit mono sample
    fwrite(&data_sz, 4, 1, f);
    uint16_t sample = 0;
    fwrite(&sample, 2, 1, f);

    fclose(f);
    return 1;
}

static void test_wav_zero_sample_rate()
{
    const char *path = "/tmp/viper_test_wav_zero_sr.wav";
    if (!write_test_wav(path, 0))
    {
        ASSERT(1, "could not write temp WAV file (skip H-7 test)");
        return;
    }

    void *snd = rt_sound_load(make_str(path));
    ASSERT(snd == NULL, "H-7: WAV with sample_rate=0 returns NULL (no crash)");

    remove(path);
}

static void test_wav_extreme_sample_rate()
{
    // sample_rate > 384000 is also rejected (H-7 upper bound)
    const char *path = "/tmp/viper_test_wav_extreme_sr.wav";
    if (!write_test_wav(path, 999999999u))
    {
        ASSERT(1, "could not write temp WAV file (skip)");
        return;
    }

    void *snd = rt_sound_load(make_str(path));
    ASSERT(snd == NULL, "H-7: WAV with sample_rate=999999999 returns NULL");

    remove(path);
}

static void test_wav_valid_sample_rate()
{
    // Positive control: a well-formed single-sample WAV at 44100 Hz
    const char *path = "/tmp/viper_test_wav_valid_sr.wav";
    if (!write_test_wav(path, 44100))
    {
        ASSERT(1, "could not write temp WAV file (skip)");
        return;
    }

    // The load may succeed (returns non-NULL) or fail due to headless environment
    // (no audio context). What must NOT happen is a crash.
    rt_sound_load(make_str(path)); // return value intentionally ignored
    ASSERT(1, "H-7: valid WAV at 44100 Hz does not crash");

    remove(path);
}

int main()
{
    // Audio system (headless-safe)
    test_audio_init();
    test_audio_volume();
    test_audio_pause_resume();
    test_sound_null_safety();
    test_voice_null_safety();
    test_music_null_safety();

    // Playlist management (pure data structure)
    test_playlist_new();
    test_playlist_add_remove();
    test_playlist_insert();
    test_playlist_clear();
    test_playlist_volume();
    test_playlist_shuffle_repeat();
    test_playlist_navigation();
    test_playlist_update_no_crash();
    test_playlist_null_safety();
    test_playlist_bounds();

    // Bug-fix regressions (C-1/C-2/C-3: playlist shuffle_order lifecycle)
    test_playlist_shuffle_lifecycle();
    test_playlist_shuffle_many_reshuffles();
    test_playlist_clear_releases_shuffle_order();

    // Bug-fix regressions (H-7: WAV sample_rate validation)
    test_wav_zero_sample_rate();
    test_wav_extreme_sample_rate();
    test_wav_valid_sample_rate();

    printf("Audio integration tests: %d/%d passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
