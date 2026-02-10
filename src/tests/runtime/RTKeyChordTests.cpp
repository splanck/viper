//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/RTKeyChordTests.cpp
// Purpose: Tests for Viper.Input.KeyChord chord and combo detection.
//
//===----------------------------------------------------------------------===//

#include "rt_keychord.h"
#include "rt_input.h"
#include "rt_internal.h"
#include "rt_seq.h"
#include "rt_string.h"

#include <cassert>
#include <cstdio>

extern "C" void vm_trap(const char *msg)
{
    rt_abort(msg);
}

static rt_string make_str(const char *s)
{
    return rt_const_cstr(s);
}

/// Helper: create a Seq of key codes from a C array.
static void *make_key_seq(const int64_t *keys, int count)
{
    void *seq = rt_seq_new();
    for (int i = 0; i < count; i++)
    {
        rt_seq_push(seq, (void *)(intptr_t)keys[i]);
    }
    return seq;
}

/// Helper: simulate one frame with given key press/release events.
/// press_keys/release_keys are arrays of key codes, -1 terminated.
static void sim_frame(const int64_t *press_keys, const int64_t *release_keys)
{
    rt_keyboard_begin_frame();
    if (press_keys)
    {
        for (int i = 0; press_keys[i] >= 0; i++)
            rt_keyboard_on_key_down(press_keys[i]);
    }
    if (release_keys)
    {
        for (int i = 0; release_keys[i] >= 0; i++)
            rt_keyboard_on_key_up(release_keys[i]);
    }
}

// ============================================================================
// Chord tests
// ============================================================================

static void test_chord_basic()
{
    rt_keyboard_init();
    void *kc = rt_keychord_new();

    int64_t keys[] = {VIPER_KEY_LCTRL, VIPER_KEY_C};
    rt_keychord_define(kc, make_str("copy"), make_key_seq(keys, 2));

    /* Frame 1: press Ctrl only */
    int64_t press1[] = {VIPER_KEY_LCTRL, -1};
    sim_frame(press1, NULL);
    rt_keychord_update(kc);
    assert(rt_keychord_active(kc, make_str("copy")) == 0);
    assert(rt_keychord_triggered(kc, make_str("copy")) == 0);

    /* Frame 2: press C while Ctrl still held */
    int64_t press2[] = {VIPER_KEY_C, -1};
    sim_frame(press2, NULL);
    rt_keychord_update(kc);
    assert(rt_keychord_active(kc, make_str("copy")) == 1);
    assert(rt_keychord_triggered(kc, make_str("copy")) == 1);

    /* Frame 3: both still held, no new press */
    sim_frame(NULL, NULL);
    rt_keychord_update(kc);
    assert(rt_keychord_active(kc, make_str("copy")) == 1);
    assert(rt_keychord_triggered(kc, make_str("copy")) == 0);

    /* Frame 4: release C */
    int64_t release4[] = {VIPER_KEY_C, -1};
    sim_frame(NULL, release4);
    rt_keychord_update(kc);
    assert(rt_keychord_active(kc, make_str("copy")) == 0);
    assert(rt_keychord_triggered(kc, make_str("copy")) == 0);

    /* Clean up keyboard state */
    int64_t release_all[] = {VIPER_KEY_LCTRL, -1};
    sim_frame(NULL, release_all);

    printf("test_chord_basic: PASSED\n");
}

static void test_chord_three_keys()
{
    rt_keyboard_init();
    void *kc = rt_keychord_new();

    int64_t keys[] = {VIPER_KEY_LCTRL, VIPER_KEY_LSHIFT, VIPER_KEY_S};
    rt_keychord_define(kc, make_str("save_as"), make_key_seq(keys, 3));

    /* Press Ctrl */
    int64_t p1[] = {VIPER_KEY_LCTRL, -1};
    sim_frame(p1, NULL);
    rt_keychord_update(kc);
    assert(rt_keychord_triggered(kc, make_str("save_as")) == 0);

    /* Press Shift */
    int64_t p2[] = {VIPER_KEY_LSHIFT, -1};
    sim_frame(p2, NULL);
    rt_keychord_update(kc);
    assert(rt_keychord_triggered(kc, make_str("save_as")) == 0);

    /* Press S — all three held */
    int64_t p3[] = {VIPER_KEY_S, -1};
    sim_frame(p3, NULL);
    rt_keychord_update(kc);
    assert(rt_keychord_active(kc, make_str("save_as")) == 1);
    assert(rt_keychord_triggered(kc, make_str("save_as")) == 1);

    /* Clean up */
    int64_t r[] = {VIPER_KEY_LCTRL, VIPER_KEY_LSHIFT, VIPER_KEY_S, -1};
    sim_frame(NULL, r);

    printf("test_chord_three_keys: PASSED\n");
}

static void test_chord_not_triggered_without_all_keys()
{
    rt_keyboard_init();
    void *kc = rt_keychord_new();

    int64_t keys[] = {VIPER_KEY_A, VIPER_KEY_B};
    rt_keychord_define(kc, make_str("ab"), make_key_seq(keys, 2));

    /* Press A only */
    int64_t p1[] = {VIPER_KEY_A, -1};
    sim_frame(p1, NULL);
    rt_keychord_update(kc);
    assert(rt_keychord_active(kc, make_str("ab")) == 0);

    /* Release A, press B only */
    int64_t r1[] = {VIPER_KEY_A, -1};
    int64_t p2[] = {VIPER_KEY_B, -1};
    sim_frame(p2, r1);
    rt_keychord_update(kc);
    assert(rt_keychord_active(kc, make_str("ab")) == 0);

    /* Clean up */
    int64_t r[] = {VIPER_KEY_B, -1};
    sim_frame(NULL, r);

    printf("test_chord_not_triggered_without_all_keys: PASSED\n");
}

// ============================================================================
// Combo tests
// ============================================================================

static void test_combo_basic()
{
    rt_keyboard_init();
    void *kc = rt_keychord_new();

    int64_t keys[] = {VIPER_KEY_DOWN, VIPER_KEY_RIGHT, VIPER_KEY_P};
    rt_keychord_define_combo(kc, make_str("hadouken"), make_key_seq(keys, 3), 30);

    /* Frame 1: press DOWN */
    int64_t p1[] = {VIPER_KEY_DOWN, -1};
    int64_t r1[] = {VIPER_KEY_DOWN, -1};
    sim_frame(p1, NULL);
    rt_keychord_update(kc);
    assert(rt_keychord_triggered(kc, make_str("hadouken")) == 0);
    assert(rt_keychord_progress(kc, make_str("hadouken")) == 1);

    /* Frame 2: release DOWN, press RIGHT */
    int64_t p2[] = {VIPER_KEY_RIGHT, -1};
    sim_frame(p2, r1);
    rt_keychord_update(kc);
    assert(rt_keychord_triggered(kc, make_str("hadouken")) == 0);
    assert(rt_keychord_progress(kc, make_str("hadouken")) == 2);

    /* Frame 3: release RIGHT, press P */
    int64_t r2[] = {VIPER_KEY_RIGHT, -1};
    int64_t p3[] = {VIPER_KEY_P, -1};
    sim_frame(p3, r2);
    rt_keychord_update(kc);
    assert(rt_keychord_triggered(kc, make_str("hadouken")) == 1);
    assert(rt_keychord_progress(kc, make_str("hadouken")) == 0); /* reset */

    /* Frame 4: nothing — triggered clears */
    int64_t r3[] = {VIPER_KEY_P, -1};
    sim_frame(NULL, r3);
    rt_keychord_update(kc);
    assert(rt_keychord_triggered(kc, make_str("hadouken")) == 0);

    printf("test_combo_basic: PASSED\n");
}

static void test_combo_timeout()
{
    rt_keyboard_init();
    void *kc = rt_keychord_new();

    int64_t keys[] = {VIPER_KEY_A, VIPER_KEY_B};
    rt_keychord_define_combo(kc, make_str("ab"), make_key_seq(keys, 2), 3);

    /* Frame 1: press A */
    int64_t p1[] = {VIPER_KEY_A, -1};
    int64_t r1[] = {VIPER_KEY_A, -1};
    sim_frame(p1, NULL);
    rt_keychord_update(kc);
    assert(rt_keychord_progress(kc, make_str("ab")) == 1);

    /* Frames 2-5: idle (exceed window of 3) */
    sim_frame(NULL, r1);
    rt_keychord_update(kc);
    sim_frame(NULL, NULL);
    rt_keychord_update(kc);
    sim_frame(NULL, NULL);
    rt_keychord_update(kc);
    sim_frame(NULL, NULL);
    rt_keychord_update(kc);

    /* Progress should have been reset due to timeout */
    assert(rt_keychord_progress(kc, make_str("ab")) == 0);

    /* Press B — should not trigger because combo timed out */
    int64_t p2[] = {VIPER_KEY_B, -1};
    sim_frame(p2, NULL);
    rt_keychord_update(kc);
    assert(rt_keychord_triggered(kc, make_str("ab")) == 0);

    /* Clean up */
    int64_t r[] = {VIPER_KEY_B, -1};
    sim_frame(NULL, r);

    printf("test_combo_timeout: PASSED\n");
}

static void test_combo_wrong_key_does_not_advance()
{
    rt_keyboard_init();
    void *kc = rt_keychord_new();

    int64_t keys[] = {VIPER_KEY_A, VIPER_KEY_B};
    rt_keychord_define_combo(kc, make_str("ab"), make_key_seq(keys, 2), 30);

    /* Press A */
    int64_t p1[] = {VIPER_KEY_A, -1};
    sim_frame(p1, NULL);
    rt_keychord_update(kc);
    assert(rt_keychord_progress(kc, make_str("ab")) == 1);

    /* Press C (wrong key) — should NOT advance */
    int64_t r1[] = {VIPER_KEY_A, -1};
    int64_t p2[] = {VIPER_KEY_C, -1};
    sim_frame(p2, r1);
    rt_keychord_update(kc);
    assert(rt_keychord_progress(kc, make_str("ab")) == 1);
    assert(rt_keychord_triggered(kc, make_str("ab")) == 0);

    /* Now press B — should complete */
    int64_t r2[] = {VIPER_KEY_C, -1};
    int64_t p3[] = {VIPER_KEY_B, -1};
    sim_frame(p3, r2);
    rt_keychord_update(kc);
    assert(rt_keychord_triggered(kc, make_str("ab")) == 1);

    /* Clean up */
    int64_t r[] = {VIPER_KEY_B, -1};
    sim_frame(NULL, r);

    printf("test_combo_wrong_key_does_not_advance: PASSED\n");
}

// ============================================================================
// Management tests
// ============================================================================

static void test_count_and_clear()
{
    void *kc = rt_keychord_new();
    assert(rt_keychord_count(kc) == 0);

    int64_t keys1[] = {VIPER_KEY_A, VIPER_KEY_B};
    int64_t keys2[] = {VIPER_KEY_C, VIPER_KEY_D};
    rt_keychord_define(kc, make_str("ab"), make_key_seq(keys1, 2));
    rt_keychord_define_combo(kc, make_str("cd"), make_key_seq(keys2, 2), 10);
    assert(rt_keychord_count(kc) == 2);

    rt_keychord_clear(kc);
    assert(rt_keychord_count(kc) == 0);

    printf("test_count_and_clear: PASSED\n");
}

static void test_remove()
{
    void *kc = rt_keychord_new();

    int64_t keys[] = {VIPER_KEY_A, VIPER_KEY_B};
    rt_keychord_define(kc, make_str("ab"), make_key_seq(keys, 2));
    assert(rt_keychord_count(kc) == 1);

    int8_t removed = rt_keychord_remove(kc, make_str("ab"));
    assert(removed == 1);
    assert(rt_keychord_count(kc) == 0);

    removed = rt_keychord_remove(kc, make_str("ab"));
    assert(removed == 0);

    printf("test_remove: PASSED\n");
}

static void test_redefine_overwrites()
{
    void *kc = rt_keychord_new();

    int64_t keys1[] = {VIPER_KEY_A, VIPER_KEY_B};
    int64_t keys2[] = {VIPER_KEY_C, VIPER_KEY_D};
    rt_keychord_define(kc, make_str("test"), make_key_seq(keys1, 2));
    assert(rt_keychord_count(kc) == 1);

    /* Redefine with same name */
    rt_keychord_define(kc, make_str("test"), make_key_seq(keys2, 2));
    assert(rt_keychord_count(kc) == 1);

    printf("test_redefine_overwrites: PASSED\n");
}

// ============================================================================
// NULL safety
// ============================================================================

static void test_null_safety()
{
    assert(rt_keychord_active(NULL, make_str("x")) == 0);
    assert(rt_keychord_triggered(NULL, make_str("x")) == 0);
    assert(rt_keychord_progress(NULL, make_str("x")) == 0);
    assert(rt_keychord_remove(NULL, make_str("x")) == 0);
    assert(rt_keychord_count(NULL) == 0);
    rt_keychord_update(NULL); /* should not crash */
    rt_keychord_clear(NULL);  /* should not crash */

    printf("test_null_safety: PASSED\n");
}

static void test_unknown_name()
{
    void *kc = rt_keychord_new();
    assert(rt_keychord_active(kc, make_str("nonexistent")) == 0);
    assert(rt_keychord_triggered(kc, make_str("nonexistent")) == 0);
    assert(rt_keychord_progress(kc, make_str("nonexistent")) == 0);

    printf("test_unknown_name: PASSED\n");
}

// ============================================================================
// Main
// ============================================================================

int main()
{
    printf("=== KeyChord Tests ===\n\n");

    /* Chords */
    test_chord_basic();
    test_chord_three_keys();
    test_chord_not_triggered_without_all_keys();

    /* Combos */
    test_combo_basic();
    test_combo_timeout();
    test_combo_wrong_key_does_not_advance();

    /* Management */
    test_count_and_clear();
    test_remove();
    test_redefine_overwrites();

    /* Safety */
    test_null_safety();
    test_unknown_name();

    printf("\nAll KeyChord tests passed!\n");
    return 0;
}
