//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/RTMusicGenTests.cpp
// Purpose: Tests for MusicGen procedural music composition — builder API,
//   channel management, envelope/effect configuration, note addition, and
//   Build() rendering. Tests do NOT require audio hardware; Build() may
//   return NULL when audio is disabled, which is expected.
// Key invariants:
//   - All functions are null-safe.
//   - Parameters are clamped to valid ranges.
//   - Maximum 8 channels, 4096 notes per channel.
//   - Build() returns NULL for empty songs.
// Links: rt_musicgen.c, rt_musicgen.h
//
//===----------------------------------------------------------------------===//

#include "rt_musicgen.h"

#include <cassert>
#include <cstdio>

extern "C" void vm_trap(const char *msg)
{
    (void)msg;
}

// ============================================================================
// Null Safety
// ============================================================================

static void test_null_safety()
{
    // All operations on NULL song should not crash
    assert(rt_musicgen_add_channel(nullptr, 0) == -1);
    assert(rt_musicgen_get_bpm(nullptr) == 0);
    assert(rt_musicgen_get_length(nullptr) == 0);
    assert(rt_musicgen_get_channel_count(nullptr) == 0);
    assert(rt_musicgen_add_note(nullptr, 0, 0, 60, 100) == 0);
    assert(rt_musicgen_add_note_vel(nullptr, 0, 0, 60, 100, 80) == 0);
    assert(rt_musicgen_build(nullptr) == nullptr);

    // Setters on NULL should not crash
    rt_musicgen_set_envelope(nullptr, 0, 10, 50, 80, 100);
    rt_musicgen_set_channel_vol(nullptr, 0, 80);
    rt_musicgen_set_duty(nullptr, 0, 50);
    rt_musicgen_set_pan(nullptr, 0, 0);
    rt_musicgen_set_detune(nullptr, 0, 0);
    rt_musicgen_set_vibrato(nullptr, 0, 15, 500);
    rt_musicgen_set_tremolo(nullptr, 0, 10, 400);
    rt_musicgen_set_arpeggio(nullptr, 0, 4, 7, 1500);
    rt_musicgen_set_portamento(nullptr, 0, 60);
    rt_musicgen_set_length(nullptr, 400);
    rt_musicgen_set_swing(nullptr, 0);
    rt_musicgen_set_loopable(nullptr, 1);

    printf("  test_null_safety: PASSED\n");
}

// ============================================================================
// Creation and Defaults
// ============================================================================

static void test_create()
{
    void *song = rt_musicgen_new(120);
    assert(song != nullptr);
    assert(rt_musicgen_get_bpm(song) == 120);
    assert(rt_musicgen_get_length(song) == 0);
    assert(rt_musicgen_get_channel_count(song) == 0);

    printf("  test_create: PASSED\n");
}

static void test_bpm_clamping()
{
    // Below minimum
    void *slow = rt_musicgen_new(5);
    assert(rt_musicgen_get_bpm(slow) == 20);

    // Above maximum
    void *fast = rt_musicgen_new(999);
    assert(rt_musicgen_get_bpm(fast) == 300);

    // Exact boundaries
    void *lo = rt_musicgen_new(20);
    assert(rt_musicgen_get_bpm(lo) == 20);
    void *hi = rt_musicgen_new(300);
    assert(rt_musicgen_get_bpm(hi) == 300);

    printf("  test_bpm_clamping: PASSED\n");
}

// ============================================================================
// Channel Management
// ============================================================================

static void test_add_channels()
{
    void *song = rt_musicgen_new(120);

    // Add all 5 waveform types
    assert(rt_musicgen_add_channel(song, 0) == 0); // sine
    assert(rt_musicgen_add_channel(song, 1) == 1); // square
    assert(rt_musicgen_add_channel(song, 2) == 2); // saw
    assert(rt_musicgen_add_channel(song, 3) == 3); // triangle
    assert(rt_musicgen_add_channel(song, 4) == 4); // noise
    assert(rt_musicgen_get_channel_count(song) == 5);

    // Fill remaining slots
    assert(rt_musicgen_add_channel(song, 0) == 5);
    assert(rt_musicgen_add_channel(song, 0) == 6);
    assert(rt_musicgen_add_channel(song, 0) == 7);
    assert(rt_musicgen_get_channel_count(song) == 8);

    // 9th channel should fail
    assert(rt_musicgen_add_channel(song, 0) == -1);
    assert(rt_musicgen_get_channel_count(song) == 8);

    printf("  test_add_channels: PASSED\n");
}

static void test_waveform_clamping()
{
    void *song = rt_musicgen_new(120);

    // Negative waveform clamped to 0
    int64_t ch = rt_musicgen_add_channel(song, -5);
    assert(ch == 0);

    // Over-max waveform clamped to 4
    ch = rt_musicgen_add_channel(song, 99);
    assert(ch == 1);

    printf("  test_waveform_clamping: PASSED\n");
}

// ============================================================================
// Envelope Configuration
// ============================================================================

static void test_set_envelope()
{
    void *song = rt_musicgen_new(120);
    rt_musicgen_add_channel(song, 1);

    // Valid envelope — should not crash
    rt_musicgen_set_envelope(song, 0, 10, 80, 60, 120);

    // Invalid channel index — should not crash
    rt_musicgen_set_envelope(song, 5, 10, 80, 60, 120);
    rt_musicgen_set_envelope(song, -1, 10, 80, 60, 120);

    printf("  test_set_envelope: PASSED\n");
}

// ============================================================================
// Effect Settings
// ============================================================================

static void test_set_effects()
{
    void *song = rt_musicgen_new(120);
    rt_musicgen_add_channel(song, 1);

    // All effect setters on valid channel — should not crash
    rt_musicgen_set_channel_vol(song, 0, 80);
    rt_musicgen_set_duty(song, 0, 25);
    rt_musicgen_set_pan(song, 0, -40);
    rt_musicgen_set_detune(song, 0, 8);
    rt_musicgen_set_vibrato(song, 0, 15, 500);
    rt_musicgen_set_tremolo(song, 0, 10, 400);
    rt_musicgen_set_arpeggio(song, 0, 4, 7, 1500);
    rt_musicgen_set_portamento(song, 0, 60);

    // Invalid channel — should not crash
    rt_musicgen_set_vibrato(song, 9, 15, 500);
    rt_musicgen_set_pan(song, -1, 50);

    printf("  test_set_effects: PASSED\n");
}

// ============================================================================
// Note Addition
// ============================================================================

static void test_add_notes()
{
    void *song = rt_musicgen_new(120);
    rt_musicgen_add_channel(song, 1);

    // Basic note
    assert(rt_musicgen_add_note(song, 0, 0, 60, 100) == 1);

    // Note with velocity
    assert(rt_musicgen_add_note_vel(song, 0, 100, 64, 100, 80) == 1);

    // Invalid channel
    assert(rt_musicgen_add_note(song, 5, 0, 60, 100) == 0);

    printf("  test_add_notes: PASSED\n");
}

static void test_note_capacity()
{
    void *song = rt_musicgen_new(120);
    rt_musicgen_add_channel(song, 0);

    // Fill to capacity
    for (int i = 0; i < MUSICGEN_MAX_NOTES; i++)
    {
        assert(rt_musicgen_add_note(song, 0, (int64_t)i * 10, 60, 10) == 1);
    }

    // Next note should fail
    assert(rt_musicgen_add_note(song, 0, 99999, 60, 10) == 0);

    printf("  test_note_capacity: PASSED\n");
}

// ============================================================================
// Build — Empty Song
// ============================================================================

static void test_build_empty()
{
    // No channels
    void *song1 = rt_musicgen_new(120);
    rt_musicgen_set_length(song1, 400);
    assert(rt_musicgen_build(song1) == nullptr);

    // No length
    void *song2 = rt_musicgen_new(120);
    rt_musicgen_add_channel(song2, 1);
    assert(rt_musicgen_build(song2) == nullptr);

    // Zero length explicitly
    void *song3 = rt_musicgen_new(120);
    rt_musicgen_add_channel(song3, 1);
    rt_musicgen_set_length(song3, 0);
    assert(rt_musicgen_build(song3) == nullptr);

    printf("  test_build_empty: PASSED\n");
}

// ============================================================================
// Build — Single Note (smoke test)
// ============================================================================

static void test_build_single_note()
{
    void *song = rt_musicgen_new(120);
    rt_musicgen_add_channel(song, 1); // square
    rt_musicgen_set_length(song, 400); // 4 beats
    rt_musicgen_add_note(song, 0, 0, 69, 200); // A4, 2 beats

    void *sound = rt_musicgen_build(song);
    // sound may be NULL if audio subsystem is not available — that's OK
    // The important thing is that it doesn't crash
    if (sound)
        printf("  test_build_single_note: PASSED (sound created)\n");
    else
        printf("  test_build_single_note: PASSED (audio unavailable)\n");
}

// ============================================================================
// Build — Multi-channel with Effects
// ============================================================================

static void test_build_multichannel()
{
    void *song = rt_musicgen_new(140);

    int64_t mel = rt_musicgen_add_channel(song, 1);  // square
    int64_t bass = rt_musicgen_add_channel(song, 2); // saw
    int64_t drum = rt_musicgen_add_channel(song, 4); // noise

    // Configure
    rt_musicgen_set_envelope(song, mel, 10, 80, 60, 120);
    rt_musicgen_set_duty(song, mel, 25);
    rt_musicgen_set_pan(song, mel, -30);
    rt_musicgen_set_vibrato(song, mel, 15, 500);

    rt_musicgen_set_envelope(song, bass, 5, 40, 90, 50);
    rt_musicgen_set_pan(song, bass, 0);

    rt_musicgen_set_envelope(song, drum, 1, 30, 0, 50);

    // Add notes
    rt_musicgen_add_note(song, mel, 0, 72, 100);
    rt_musicgen_add_note(song, mel, 100, 76, 100);
    rt_musicgen_add_note(song, mel, 200, 79, 50);
    rt_musicgen_add_note(song, mel, 250, 84, 150);

    rt_musicgen_add_note(song, bass, 0, 36, 200);
    rt_musicgen_add_note(song, bass, 200, 41, 200);

    rt_musicgen_add_note(song, drum, 0, 2, 25);
    rt_musicgen_add_note(song, drum, 100, 10, 15);
    rt_musicgen_add_note(song, drum, 200, 2, 25);
    rt_musicgen_add_note(song, drum, 300, 10, 15);

    rt_musicgen_set_length(song, 400);

    void *sound = rt_musicgen_build(song);
    if (sound)
        printf("  test_build_multichannel: PASSED (sound created)\n");
    else
        printf("  test_build_multichannel: PASSED (audio unavailable)\n");
}

// ============================================================================
// Build — Loopable
// ============================================================================

static void test_build_loopable()
{
    void *song = rt_musicgen_new(120);
    rt_musicgen_add_channel(song, 0); // sine
    rt_musicgen_set_length(song, 400);
    rt_musicgen_set_loopable(song, 1);
    rt_musicgen_add_note(song, 0, 0, 60, 400);

    void *sound = rt_musicgen_build(song);
    if (sound)
        printf("  test_build_loopable: PASSED (sound created)\n");
    else
        printf("  test_build_loopable: PASSED (audio unavailable)\n");
}

// ============================================================================
// Build — All Effects Active
// ============================================================================

static void test_build_all_effects()
{
    void *song = rt_musicgen_new(120);
    int64_t ch = rt_musicgen_add_channel(song, 1);

    rt_musicgen_set_envelope(song, ch, 20, 100, 70, 200);
    rt_musicgen_set_duty(song, ch, 25);
    rt_musicgen_set_pan(song, ch, -50);
    rt_musicgen_set_detune(song, ch, 10);
    rt_musicgen_set_vibrato(song, ch, 20, 600);
    rt_musicgen_set_tremolo(song, ch, 15, 400);
    rt_musicgen_set_arpeggio(song, ch, 4, 7, 1500);
    rt_musicgen_set_portamento(song, ch, 80);
    rt_musicgen_set_swing(song, 30);

    rt_musicgen_add_note(song, ch, 0, 60, 100);
    rt_musicgen_add_note(song, ch, 100, 64, 100);
    rt_musicgen_add_note(song, ch, 200, 67, 100);
    rt_musicgen_add_note(song, ch, 300, 72, 100);

    rt_musicgen_set_length(song, 400);

    void *sound = rt_musicgen_build(song);
    if (sound)
        printf("  test_build_all_effects: PASSED (sound created)\n");
    else
        printf("  test_build_all_effects: PASSED (audio unavailable)\n");
}

// ============================================================================
// Edge Cases
// ============================================================================

static void test_overlapping_notes()
{
    void *song = rt_musicgen_new(120);
    rt_musicgen_add_channel(song, 1);
    rt_musicgen_set_length(song, 400);

    // Two overlapping notes on same channel
    rt_musicgen_add_note(song, 0, 0, 60, 300);
    rt_musicgen_add_note(song, 0, 100, 64, 300);

    void *sound = rt_musicgen_build(song);
    // Should not crash — overlapping notes accumulate
    if (sound)
        printf("  test_overlapping_notes: PASSED (sound created)\n");
    else
        printf("  test_overlapping_notes: PASSED (audio unavailable)\n");
}

static void test_zero_duration_note()
{
    void *song = rt_musicgen_new(120);
    rt_musicgen_add_channel(song, 1);
    rt_musicgen_set_length(song, 400);

    // Zero-duration note (clamped to 1 centbeat)
    rt_musicgen_add_note(song, 0, 0, 60, 0);

    void *sound = rt_musicgen_build(song);
    if (sound)
        printf("  test_zero_duration_note: PASSED (sound created)\n");
    else
        printf("  test_zero_duration_note: PASSED (audio unavailable)\n");
}

static void test_song_properties()
{
    void *song = rt_musicgen_new(120);

    rt_musicgen_set_length(song, 800);
    assert(rt_musicgen_get_length(song) == 800);

    rt_musicgen_set_length(song, 1600);
    assert(rt_musicgen_get_length(song) == 1600);

    // Negative clamped to 0
    rt_musicgen_set_length(song, -100);
    assert(rt_musicgen_get_length(song) == 0);

    printf("  test_song_properties: PASSED\n");
}

// ============================================================================
// Main
// ============================================================================

int main()
{
    printf("RTMusicGenTests:\n");

    test_null_safety();
    test_create();
    test_bpm_clamping();
    test_add_channels();
    test_waveform_clamping();
    test_set_envelope();
    test_set_effects();
    test_add_notes();
    test_note_capacity();
    test_build_empty();
    test_build_single_note();
    test_build_multichannel();
    test_build_loopable();
    test_build_all_effects();
    test_overlapping_notes();
    test_zero_duration_note();
    test_song_properties();

    printf("RTMusicGenTests: ALL PASSED\n");
    return 0;
}
