//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//

#include "rt_diff.h"
#include "rt_internal.h"
#include "rt_seq.h"
#include "rt_string.h"

#include "rt_trap.h"

#include <cassert>
#include <csetjmp>
#include <cstring>

extern "C" void vm_trap(const char *msg) {
    rt_abort(msg);
}

static rt_string make_str(const char *s) {
    return rt_string_from_bytes(s, strlen(s));
}

static bool str_eq(rt_string s, const char *expected) {
    const char *cstr = rt_string_cstr(s);
    return cstr && strcmp(cstr, expected) == 0;
}

static bool starts_with(rt_string s, char prefix) {
    const char *cstr = rt_string_cstr(s);
    return cstr && cstr[0] == prefix;
}

static void test_identical() {
    rt_string a = make_str("hello\nworld");
    rt_string b = make_str("hello\nworld");
    void *diff = rt_diff_lines(a, b);

    // All lines should be unchanged (space prefix)
    int64_t len = rt_seq_len(diff);
    assert(len == 2);
    assert(starts_with((rt_string)rt_seq_get(diff, 0), ' '));
    assert(starts_with((rt_string)rt_seq_get(diff, 1), ' '));

    rt_string_unref(a);
    rt_string_unref(b);
}

static void test_addition() {
    rt_string a = make_str("line1\nline2");
    rt_string b = make_str("line1\nline2\nline3");
    void *diff = rt_diff_lines(a, b);

    int64_t len = rt_seq_len(diff);
    assert(len == 3);
    assert(starts_with((rt_string)rt_seq_get(diff, 0), ' '));
    assert(starts_with((rt_string)rt_seq_get(diff, 1), ' '));
    assert(starts_with((rt_string)rt_seq_get(diff, 2), '+'));

    rt_string_unref(a);
    rt_string_unref(b);
}

static void test_removal() {
    rt_string a = make_str("line1\nline2\nline3");
    rt_string b = make_str("line1\nline3");
    void *diff = rt_diff_lines(a, b);

    // Should have removal of line2
    int changes = 0;
    for (int64_t i = 0; i < rt_seq_len(diff); i++) {
        if (starts_with((rt_string)rt_seq_get(diff, i), '-'))
            changes++;
    }
    assert(changes >= 1);

    rt_string_unref(a);
    rt_string_unref(b);
}

static void test_count_changes() {
    rt_string a = make_str("line1\nline2\nline3");
    rt_string b = make_str("line1\nchanged\nline3");

    int64_t changes = rt_diff_count_changes(a, b);
    assert(changes >= 2); // At least removal + addition

    rt_string_unref(a);
    rt_string_unref(b);
}

static void test_patch() {
    rt_string a = make_str("hello\nworld");
    rt_string b = make_str("hello\nbeautiful\nworld");
    void *diff = rt_diff_lines(a, b);

    rt_string patched = rt_diff_patch(a, diff);
    assert(str_eq(patched, "hello\nbeautiful\nworld"));
    rt_string_unref(patched);

    rt_string_unref(a);
    rt_string_unref(b);
}

static void test_unified() {
    rt_string a = make_str("line1\nline2");
    rt_string b = make_str("line1\nline3");

    rt_string unified = rt_diff_unified(a, b, 3);
    const char *cstr = rt_string_cstr(unified);
    assert(cstr != NULL);
    // Should contain header
    assert(strstr(cstr, "--- a") != NULL);
    assert(strstr(cstr, "+++ b") != NULL);

    rt_string_unref(unified);
    rt_string_unref(a);
    rt_string_unref(b);
}

static void test_empty_inputs() {
    rt_string empty = make_str("");
    rt_string text = make_str("hello");

    void *diff = rt_diff_lines(empty, text);
    assert(rt_seq_len(diff) >= 1);

    diff = rt_diff_lines(text, empty);
    assert(rt_seq_len(diff) >= 1);

    rt_string_unref(empty);
    rt_string_unref(text);
}

static void test_embedded_nul_lines_are_length_aware() {
    const char a_bytes[] = {'a', '\0', 'x', '\n', 's', 'a', 'm', 'e'};
    const char b_bytes[] = {'a', '\0', 'y', '\n', 's', 'a', 'm', 'e'};
    const char removal[] = {'-', 'a', '\0', 'x'};
    const char addition[] = {'+', 'a', '\0', 'y'};

    rt_string a = rt_string_from_bytes(a_bytes, sizeof(a_bytes));
    rt_string b = rt_string_from_bytes(b_bytes, sizeof(b_bytes));
    void *diff = rt_diff_lines(a, b);

    assert(rt_seq_len(diff) == 3);
    int saw_removal = 0;
    int saw_addition = 0;
    for (int64_t i = 0; i < 2; i++) {
        rt_string line = (rt_string)rt_seq_get(diff, i);
        if (rt_str_len(line) == (int64_t)sizeof(removal) &&
            memcmp(rt_string_cstr(line), removal, sizeof(removal)) == 0) {
            saw_removal = 1;
        }
        if (rt_str_len(line) == (int64_t)sizeof(addition) &&
            memcmp(rt_string_cstr(line), addition, sizeof(addition)) == 0) {
            saw_addition = 1;
        }
    }
    assert(saw_removal == 1);
    assert(saw_addition == 1);

    rt_string patched = rt_diff_patch(a, diff);
    assert(rt_str_len(patched) == (int64_t)sizeof(b_bytes));
    assert(memcmp(rt_string_cstr(patched), b_bytes, sizeof(b_bytes)) == 0);

    rt_string_unref(a);
    rt_string_unref(b);
    rt_string_unref(patched);
}

/// @brief Main.
static void test_patch_validates_original() {
    // VDOC-061: Patch verifies the diff applies to the supplied original.
    rt_string a = make_str("hello\nworld");
    rt_string b = make_str("hello\nbeautiful\nworld");
    void *diff = rt_diff_lines(a, b);

    jmp_buf env;
    rt_trap_set_recovery(&env);
    bool trapped = true;
    if (setjmp(env) == 0) {
        (void)rt_diff_patch(make_str("not the source"), diff);
        trapped = false;
    }
    rt_trap_clear_recovery();
    assert(trapped && "Patch must reject a mismatched original");

    // The matching original still round-trips.
    rt_string patched = rt_diff_patch(a, diff);
    assert(str_eq(patched, "hello\nbeautiful\nworld"));
    rt_string_unref(patched);
    rt_string_unref(a);
    rt_string_unref(b);
}

int main() {
    test_patch_validates_original();
    test_identical();
    test_addition();
    test_removal();
    test_count_changes();
    test_patch();
    test_unified();
    test_empty_inputs();
    test_embedded_nul_lines_are_length_aware();

    return 0;
}
