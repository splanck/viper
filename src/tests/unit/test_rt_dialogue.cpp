//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_rt_dialogue.cpp
// Purpose: Unit tests for the Dialogue typewriter text system.
//
// Key invariants:
//   - Typewriter reveals characters over time based on speed.
//   - Advance() skips to end of line or moves to next.
//   - IsFinished becomes true after all lines acknowledged.
//
// Ownership/Lifetime:
//   - Uses runtime library. Dialogue objects are GC-managed.
//
// Links: src/runtime/collections/rt_dialogue.h
//
//===----------------------------------------------------------------------===//

#include "rt.hpp"
#include "rt_dialogue.h"
#include "rt_internal.h"
#include "rt_string.h"
#include <cassert>
#include <cstdio>
#include <cstring>

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

static rt_string make_str(const char *s) {
    return rt_string_from_bytes(s, strlen(s));
}

static void test_creation(void) {
    TEST("Dialogue creation");
    void *d = rt_dialogue_new(10, 200, 300, 80);
    assert(d != NULL);
    assert(rt_dialogue_is_active(d) == 0);
    assert(rt_dialogue_is_finished(d) == 0);
    assert(rt_dialogue_get_line_count(d) == 0);
    PASS();
}

static void test_say_activates(void) {
    TEST("Say activates dialogue");
    void *d = rt_dialogue_new(0, 0, 200, 80);
    rt_dialogue_say(d, make_str("NPC"), make_str("Hello world"));
    assert(rt_dialogue_is_active(d) == 1);
    assert(rt_dialogue_get_line_count(d) == 1);
    assert(rt_dialogue_get_current_line(d) == 0);
    PASS();
}

static void test_typewriter_progression(void) {
    TEST("Typewriter reveals chars over time");
    void *d = rt_dialogue_new(0, 0, 200, 80);
    rt_dialogue_set_speed(d, 10); // 10 chars/sec
    rt_dialogue_say(d, make_str("A"), make_str("Hello world"));

    // After 100ms at 10 cps = 1 char revealed
    rt_dialogue_update(d, 100);
    assert(rt_dialogue_is_line_complete(d) == 0);

    // After 2000ms total at 10 cps = 20 chars (more than "Hello world" = 11)
    rt_dialogue_update(d, 1900);
    assert(rt_dialogue_is_line_complete(d) == 1);
    assert(rt_dialogue_is_waiting(d) == 1);
    PASS();
}

static void test_advance_skips_then_progresses(void) {
    TEST("Advance skips then moves to next line");
    void *d = rt_dialogue_new(0, 0, 200, 80);
    rt_dialogue_set_speed(d, 10);
    rt_dialogue_say(d, make_str("A"), make_str("First"));
    rt_dialogue_say(d, make_str("B"), make_str("Second"));

    // Partially reveal first line
    rt_dialogue_update(d, 100);
    assert(rt_dialogue_is_line_complete(d) == 0);

    // Advance: skip to end of first line
    rt_dialogue_advance(d);
    assert(rt_dialogue_is_line_complete(d) == 1);
    assert(rt_dialogue_get_current_line(d) == 0);

    // Advance again: move to second line
    rt_dialogue_advance(d);
    assert(rt_dialogue_get_current_line(d) == 1);
    assert(rt_dialogue_is_line_complete(d) == 0);

    // Check speaker
    const char *sp = rt_string_cstr(rt_dialogue_get_speaker(d));
    assert(sp != NULL);
    assert(strcmp(sp, "B") == 0);
    PASS();
}

static void test_finished_state(void) {
    TEST("Finished after all lines acknowledged");
    void *d = rt_dialogue_new(0, 0, 200, 80);
    rt_dialogue_say_text(d, make_str("Only line"));
    rt_dialogue_set_speed(d, 1000);

    rt_dialogue_update(d, 1000);
    assert(rt_dialogue_is_line_complete(d) == 1);

    rt_dialogue_advance(d); // acknowledge last line
    assert(rt_dialogue_is_finished(d) == 1);
    assert(rt_dialogue_is_active(d) == 0);
    PASS();
}

static void test_clear(void) {
    TEST("Clear resets dialogue");
    void *d = rt_dialogue_new(0, 0, 200, 80);
    rt_dialogue_say_text(d, make_str("Test"));
    assert(rt_dialogue_is_active(d) == 1);

    rt_dialogue_clear(d);
    assert(rt_dialogue_is_active(d) == 0);
    assert(rt_dialogue_get_line_count(d) == 0);
    PASS();
}

static void test_skip(void) {
    TEST("Skip instantly completes current line");
    void *d = rt_dialogue_new(0, 0, 200, 80);
    rt_dialogue_set_speed(d, 1);
    rt_dialogue_say_text(d, make_str("Long text here"));

    rt_dialogue_update(d, 10);
    assert(rt_dialogue_is_line_complete(d) == 0);

    rt_dialogue_skip(d);
    assert(rt_dialogue_is_line_complete(d) == 1);
    PASS();
}

static void test_empty_text(void) {
    TEST("Empty text immediately completes");
    void *d = rt_dialogue_new(0, 0, 200, 80);
    rt_dialogue_say_text(d, make_str(""));

    rt_dialogue_update(d, 1);
    assert(rt_dialogue_is_line_complete(d) == 1);
    PASS();
}

static void test_null_safety(void) {
    TEST("NULL safety");
    assert(rt_dialogue_is_active(NULL) == 0);
    assert(rt_dialogue_is_finished(NULL) == 0);
    assert(rt_dialogue_get_line_count(NULL) == 0);
    rt_dialogue_update(NULL, 100);
    rt_dialogue_advance(NULL);
    rt_dialogue_skip(NULL);
    rt_dialogue_clear(NULL);
    rt_dialogue_draw(NULL, NULL);
    PASS();
}

int main() {
    printf("test_rt_dialogue:\n");
    test_creation();
    test_say_activates();
    test_typewriter_progression();
    test_advance_skips_then_progresses();
    test_finished_state();
    test_clear();
    test_skip();
    test_empty_text();
    test_null_safety();

    printf("\n  %d/%d tests passed\n", tests_passed, tests_total);
    assert(tests_passed == tests_total);
    return 0;
}
