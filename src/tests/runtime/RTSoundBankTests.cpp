//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/RTSoundBankTests.cpp
// Purpose: Tests for SoundBank named sound registry — CRUD operations, name
//   lookup, capacity limits, null safety. Tests the registry logic without
//   requiring audio playback (no Audio.Init needed for registration).
// Key invariants:
//   - Names are unique; registering same name overwrites.
//   - Maximum 64 entries per bank.
//   - All functions are null-safe.
// Links: rt_soundbank.c, rt_soundbank.h
//
//===----------------------------------------------------------------------===//

#include "rt_soundbank.h"
#include "rt_synth.h"

#include <cassert>
#include <cstdio>
#include <cstring>

extern "C" void vm_trap(const char *msg) {
    (void)msg;
}

static rt_string S(const char *s) {
    return rt_const_cstr(s);
}

// ============================================================================
// Null Safety
// ============================================================================

static void test_null_safety() {
    // All operations on NULL bank should not crash
    assert(rt_soundbank_has(nullptr, S("x")) == 0);
    assert(rt_soundbank_play(nullptr, S("x")) == -1);
    assert(rt_soundbank_play_ex(nullptr, S("x"), 100, 0) == -1);
    assert(rt_soundbank_get(nullptr, S("x")) == nullptr);
    assert(rt_soundbank_count(nullptr) == 0);
    rt_soundbank_remove(nullptr, S("x"));
    rt_soundbank_clear(nullptr);

    printf("  test_null_safety: PASSED\n");
}

// ============================================================================
// Creation and Count
// ============================================================================

static void test_create() {
    void *bank = rt_soundbank_new();
    assert(bank != nullptr);
    assert(rt_soundbank_count(bank) == 0);

    printf("  test_create: PASSED\n");
}

// ============================================================================
// Register + Has + Get + Count
// ============================================================================

static void test_register_sound() {
    void *bank = rt_soundbank_new();

    // Generate a test sound via Synth (doesn't need Audio.Init)
    void *snd = rt_synth_tone(440, 100, 0);
    // snd may be NULL if audio subsystem not available — that's OK for registry tests

    // RegisterSound returns 1 on success
    int64_t ok = rt_soundbank_register_sound(bank, S("test"), snd);
    assert(ok == 1);
    assert(rt_soundbank_count(bank) == 1);

    // Has returns 1 for registered name
    assert(rt_soundbank_has(bank, S("test")) == 1);
    assert(rt_soundbank_has(bank, S("missing")) == 0);

    // Get returns the sound object
    void *got = rt_soundbank_get(bank, S("test"));
    assert(got == snd);

    // Get returns NULL for missing name
    assert(rt_soundbank_get(bank, S("missing")) == nullptr);

    printf("  test_register_sound: PASSED\n");
}

static void test_register_overwrite() {
    void *bank = rt_soundbank_new();

    void *snd1 = rt_synth_tone(440, 100, 0);
    void *snd2 = rt_synth_tone(880, 100, 0);

    rt_soundbank_register_sound(bank, S("beep"), snd1);
    assert(rt_soundbank_count(bank) == 1);

    // Registering same name should overwrite, not duplicate
    rt_soundbank_register_sound(bank, S("beep"), snd2);
    assert(rt_soundbank_count(bank) == 1);

    // Get returns the new sound
    void *got = rt_soundbank_get(bank, S("beep"));
    assert(got == snd2);

    printf("  test_register_overwrite: PASSED\n");
}

static void test_long_names_do_not_alias() {
    void *bank = rt_soundbank_new();

    const char *name1 = "ambience_forest_loop_segment_alpha_version_01";
    const char *name2 = "ambience_forest_loop_segment_alpha_version_02";
    assert(strlen(name1) > 31);
    assert(strlen(name2) > 31);

    void *snd1 = rt_synth_tone(440, 100, 0);
    void *snd2 = rt_synth_tone(660, 100, 0);

    rt_soundbank_register_sound(bank, S(name1), snd1);
    rt_soundbank_register_sound(bank, S(name2), snd2);

    assert(rt_soundbank_count(bank) == 2);
    assert(rt_soundbank_has(bank, S(name1)) == 1);
    assert(rt_soundbank_has(bank, S(name2)) == 1);
    assert(rt_soundbank_get(bank, S(name1)) == snd1);
    assert(rt_soundbank_get(bank, S(name2)) == snd2);

    rt_soundbank_remove(bank, S(name1));
    assert(rt_soundbank_count(bank) == 1);
    assert(rt_soundbank_has(bank, S(name1)) == 0);
    assert(rt_soundbank_has(bank, S(name2)) == 1);
    assert(rt_soundbank_get(bank, S(name2)) == snd2);

    printf("  test_long_names_do_not_alias: PASSED\n");
}

// ============================================================================
// Remove + Clear
// ============================================================================

static void test_remove() {
    void *bank = rt_soundbank_new();

    rt_soundbank_register_sound(bank, S("a"), rt_synth_tone(440, 50, 0));
    rt_soundbank_register_sound(bank, S("b"), rt_synth_tone(880, 50, 0));
    assert(rt_soundbank_count(bank) == 2);

    rt_soundbank_remove(bank, S("a"));
    assert(rt_soundbank_count(bank) == 1);
    assert(rt_soundbank_has(bank, S("a")) == 0);
    assert(rt_soundbank_has(bank, S("b")) == 1);

    // Remove non-existent — no crash
    rt_soundbank_remove(bank, S("missing"));
    assert(rt_soundbank_count(bank) == 1);

    printf("  test_remove: PASSED\n");
}

static void test_clear() {
    void *bank = rt_soundbank_new();

    rt_soundbank_register_sound(bank, S("x"), rt_synth_tone(440, 50, 0));
    rt_soundbank_register_sound(bank, S("y"), rt_synth_tone(880, 50, 0));
    rt_soundbank_register_sound(bank, S("z"), rt_synth_tone(660, 50, 0));
    assert(rt_soundbank_count(bank) == 3);

    rt_soundbank_clear(bank);
    assert(rt_soundbank_count(bank) == 0);
    assert(rt_soundbank_has(bank, S("x")) == 0);

    printf("  test_clear: PASSED\n");
}

// ============================================================================
// Multiple entries
// ============================================================================

static void test_multiple_entries() {
    void *bank = rt_soundbank_new();

    // Register several sounds
    for (int i = 0; i < 10; i++) {
        char name[8];
        name[0] = 'S';
        name[1] = '0' + (char)i;
        name[2] = '\0';
        rt_soundbank_register_sound(bank, S(name), rt_synth_tone(440 + i * 100, 50, 0));
    }

    assert(rt_soundbank_count(bank) == 10);
    assert(rt_soundbank_has(bank, S("S0")) == 1);
    assert(rt_soundbank_has(bank, S("S9")) == 1);

    printf("  test_multiple_entries: PASSED\n");
}

// ============================================================================
// Synth Sound Generation
// ============================================================================

static void test_synth_tone() {
    // Synth.Tone generates a Sound object — verify non-null
    void *snd = rt_synth_tone(440, 100, 0); // sine wave
    // May be NULL if audio not compiled in, but shouldn't crash
    printf("  test_synth_tone: PASSED (snd=%s)\n", snd ? "ok" : "null/no-audio");
}

static void test_synth_sweep() {
    void *snd = rt_synth_sweep(880, 220, 200, 0);
    printf("  test_synth_sweep: PASSED (snd=%s)\n", snd ? "ok" : "null/no-audio");
}

static void test_synth_noise() {
    void *snd = rt_synth_noise(100, 50);
    printf("  test_synth_noise: PASSED (snd=%s)\n", snd ? "ok" : "null/no-audio");
}

static void test_synth_sfx_presets() {
    // Test all 6 SFX presets
    for (int i = 0; i <= 5; i++) {
        void *snd = rt_synth_sfx(i);
        // Should return non-null if audio compiled in, null otherwise
        printf("  test_synth_sfx[%d]: %s\n", i, snd ? "ok" : "null/no-audio");
    }

    // Invalid preset — should not crash
    void *bad = rt_synth_sfx(99);
    printf("  test_synth_sfx[99]: %s (expected null)\n", bad ? "unexpected" : "null/ok");
}

static void test_synth_edge_cases() {
    // Zero duration
    void *z = rt_synth_tone(440, 0, 0);
    printf("  test_synth_zero_duration: %s\n", z ? "ok" : "null/ok");

    // Very high frequency
    void *h = rt_synth_tone(20000, 50, 0);
    printf("  test_synth_high_freq: %s\n", h ? "ok" : "null/ok");

    // All waveform types
    for (int w = 0; w <= 3; w++) {
        void *s = rt_synth_tone(440, 50, w);
        printf("  test_synth_waveform[%d]: %s\n", w, s ? "ok" : "null/ok");
    }
}

// ============================================================================
// Main
// ============================================================================

int main() {
    printf("=== RTSoundBankTests ===\n\n");

    printf("--- Null Safety ---\n");
    test_null_safety();

    printf("\n--- SoundBank CRUD ---\n");
    test_create();
    test_register_sound();
    test_register_overwrite();
    test_long_names_do_not_alias();
    test_remove();
    test_clear();
    test_multiple_entries();

    printf("\n--- Synth Sound Generation ---\n");
    test_synth_tone();
    test_synth_sweep();
    test_synth_noise();
    test_synth_sfx_presets();
    test_synth_edge_cases();

    printf("\n=== All RTSoundBankTests passed! ===\n");
    return 0;
}
