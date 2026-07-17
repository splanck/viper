//===----------------------------------------------------------------------===//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
// File: tests/runtime/RTArithmeticUBTests.c
// Purpose: Verify arithmetic UB fixes — INT64_MIN handling, f64 to i64 clamping,
//          and mat4 NaN/Inf guards.
//===----------------------------------------------------------------------===//

#include "rt_duration.h"
#include "rt_fmt.h"
#include "rt_mat4.h"
#include "rt_numeric.h"
#include "rt_string.h"

#include <assert.h>
#include <fenv.h>
#include <math.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests_run = 0;
static int tests_failed = 0;
static jmp_buf g_trap_env;
static int g_expect_trap = 0;

void vm_trap(const char *msg) {
    if (g_expect_trap)
        longjmp(g_trap_env, 1);
    fprintf(stderr, "unexpected trap: %s\n", msg ? msg : "(null)");
    abort();
}

#define ASSERT(cond)                                                                               \
    do {                                                                                           \
        tests_run++;                                                                               \
        if (!(cond)) {                                                                             \
            tests_failed++;                                                                        \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                        \
        }                                                                                          \
    } while (0)

#define EXPECT_TRAP(expr)                                                                          \
    do {                                                                                           \
        tests_run++;                                                                               \
        g_expect_trap = 1;                                                                         \
        if (setjmp(g_trap_env) == 0) {                                                             \
            (void)(expr);                                                                          \
            g_expect_trap = 0;                                                                     \
            tests_failed++;                                                                        \
            fprintf(stderr, "FAIL %s:%d: expected trap for %s\n", __FILE__, __LINE__, #expr);      \
        } else {                                                                                   \
            g_expect_trap = 0;                                                                     \
        }                                                                                          \
    } while (0)

//=============================================================================
// rt_duration_abs — Bug R-13
//=============================================================================

static void test_duration_abs_int64_min(void) {
    EXPECT_TRAP(rt_duration_abs(INT64_MIN));
}

static void test_duration_abs_positive(void) {
    ASSERT(rt_duration_abs(42LL) == 42LL);
}

static void test_duration_abs_negative(void) {
    ASSERT(rt_duration_abs(-1000LL) == 1000LL);
}

static void test_duration_abs_zero(void) {
    ASSERT(rt_duration_abs(0LL) == 0LL);
}

//=============================================================================
// rt_duration_neg — Bug R-13
//=============================================================================

static void test_duration_neg_int64_min(void) {
    EXPECT_TRAP(rt_duration_neg(INT64_MIN));
}

static void test_duration_neg_positive(void) {
    ASSERT(rt_duration_neg(5000LL) == -5000LL);
}

static void test_duration_neg_negative(void) {
    ASSERT(rt_duration_neg(-5000LL) == 5000LL);
}

static void test_duration_neg_zero(void) {
    ASSERT(rt_duration_neg(0LL) == 0LL);
}

//=============================================================================
// rt_fmt_to_words — Bug R-20
//=============================================================================

static void test_fmt_to_words_int64_min(void) {
    // Before the fix, value = -value was UB for INT64_MIN.
    // After the fix the function should return a non-NULL string without crashing.
    rt_string s = rt_fmt_to_words(INT64_MIN);
    ASSERT(s != NULL);
    const char *cstr = rt_string_cstr(s);
    ASSERT(cstr != NULL && cstr[0] != '\0');
    rt_string_unref(s);
}

static void test_fmt_to_words_zero(void) {
    rt_string s = rt_fmt_to_words(0LL);
    ASSERT(s != NULL);
    const char *cstr = rt_string_cstr(s);
    ASSERT(cstr != NULL);
    ASSERT(strcmp(cstr, "zero") == 0);
    rt_string_unref(s);
}

static void test_fmt_to_words_negative(void) {
    // A typical negative number that isn't INT64_MIN.
    rt_string s = rt_fmt_to_words(-1LL);
    ASSERT(s != NULL);
    const char *cstr = rt_string_cstr(s);
    ASSERT(cstr != NULL && cstr[0] != '\0');
    rt_string_unref(s);
}

static void test_fmt_to_words_positive(void) {
    rt_string s = rt_fmt_to_words(1000LL);
    ASSERT(s != NULL);
    const char *cstr = rt_string_cstr(s);
    ASSERT(cstr != NULL);
    ASSERT(strcmp(cstr, "one thousand") == 0);
    rt_string_unref(s);
}

//=============================================================================
// rt_f64_to_i64 — Bug R-26
//=============================================================================

static void test_f64_to_i64_clamp_max(void) {
    // A double value clearly above INT64_MAX should clamp to INT64_MAX.
    long long result = rt_f64_to_i64(1.0e19);
    ASSERT(result == INT64_MAX);
}

static void test_f64_to_i64_clamp_min(void) {
    // A double value clearly below INT64_MIN should clamp to INT64_MIN.
    long long result = rt_f64_to_i64(-1.0e19);
    ASSERT(result == INT64_MIN);
}

static void test_f64_to_i64_nan(void) {
    long long result = rt_f64_to_i64((double)NAN);
    ASSERT(result == 0LL);
}

static void test_f64_to_i64_positive_inf(void) {
    long long result = rt_f64_to_i64((double)INFINITY);
    ASSERT(result == INT64_MAX);
}

static void test_f64_to_i64_negative_inf(void) {
    long long result = rt_f64_to_i64(-(double)INFINITY);
    ASSERT(result == INT64_MIN);
}

static void test_f64_to_i64_normal(void) {
    ASSERT(rt_f64_to_i64(3.9) == 3LL);
    ASSERT(rt_f64_to_i64(-3.9) == -3LL);
    ASSERT(rt_f64_to_i64(0.0) == 0LL);
}

static void test_f64_to_i64_boundaries(void) {
    const double two63 = 0x1.0p63;
    const double below_two63 = nextafter(two63, 0.0);
    const double above_two63 = nextafter(two63, INFINITY);
    ASSERT(rt_f64_to_i64(below_two63) == INT64_MAX - 1023LL);
    ASSERT(rt_f64_to_i64(two63) == INT64_MAX);
    ASSERT(rt_f64_to_i64(above_two63) == INT64_MAX);
    ASSERT(rt_f64_to_i64(-two63) == INT64_MIN);
    ASSERT(rt_f64_to_i64(nextafter(-two63, -INFINITY)) == INT64_MIN);
}

static void test_round_even_ignores_fenv_rounding_mode(void) {
    int original = fegetround();
    bool ok = true;

    ASSERT(fesetround(FE_UPWARD) == 0);
    ASSERT(rt_cint_from_double(2.1, &ok) == 2);
    ASSERT(ok);
    ASSERT(rt_cint_from_double(2.5, &ok) == 2);
    ASSERT(ok);
    ASSERT(rt_clng_from_double(3.5, &ok) == 4);
    ASSERT(ok);

    ASSERT(fesetround(FE_DOWNWARD) == 0);
    ASSERT(rt_cint_from_double(-2.1, &ok) == -2);
    ASSERT(ok);
    ASSERT(rt_round_even(2.5, 0) == 2.0);
    ASSERT(rt_round_even(3.5, 0) == 4.0);

    ASSERT(fesetround(original) == 0);
}

//=============================================================================
// rt_mat4_perspective — Bug R-27
//=============================================================================

static void test_mat4_perspective_zero_fov(void) {
    // fov == 0 → division by zero → NaN matrix without the guard.
    void *m = rt_mat4_perspective(0.0, 1.0, 0.1, 100.0);
    ASSERT(m != NULL);
    double v = rt_mat4_get(m, 0, 0);
    ASSERT(!isnan(v) && !isinf(v));
}

static void test_mat4_perspective_negative_fov(void) {
    void *m = rt_mat4_perspective(-1.0, 1.0, 0.1, 100.0);
    ASSERT(m != NULL);
    double v = rt_mat4_get(m, 0, 0);
    ASSERT(!isnan(v) && !isinf(v));
}

static void test_mat4_perspective_zero_aspect(void) {
    void *m = rt_mat4_perspective(1.0, 0.0, 0.1, 100.0);
    ASSERT(m != NULL);
    double v = rt_mat4_get(m, 0, 0);
    ASSERT(!isnan(v) && !isinf(v));
}

static void test_mat4_perspective_near_zero(void) {
    void *m = rt_mat4_perspective(1.0, 1.0, 0.0, 100.0);
    ASSERT(m != NULL);
    double v = rt_mat4_get(m, 0, 0);
    ASSERT(!isnan(v) && !isinf(v));
}

static void test_mat4_perspective_near_equals_far(void) {
    // near == far → division by zero in (near - far).
    void *m = rt_mat4_perspective(1.0, 1.0, 10.0, 10.0);
    ASSERT(m != NULL);
    double v = rt_mat4_get(m, 2, 2);
    ASSERT(!isnan(v) && !isinf(v));
}

static void test_mat4_perspective_valid(void) {
    // A valid perspective call should not produce NaN or Inf in any element.
    void *m = rt_mat4_perspective(1.0, 16.0 / 9.0, 0.1, 1000.0);
    ASSERT(m != NULL);
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            double v = rt_mat4_get(m, r, c);
            ASSERT(!isnan(v) && !isinf(v));
        }
    }
}

//=============================================================================
// rt_mat4_ortho — Bug R-27
//=============================================================================

static void test_mat4_ortho_equal_left_right(void) {
    // right == left → division by zero.
    void *m = rt_mat4_ortho(5.0, 5.0, -1.0, 1.0, 0.1, 100.0);
    ASSERT(m != NULL);
    double v = rt_mat4_get(m, 0, 0);
    ASSERT(!isnan(v) && !isinf(v));
}

static void test_mat4_ortho_equal_top_bottom(void) {
    // top == bottom → division by zero.
    void *m = rt_mat4_ortho(-1.0, 1.0, 3.0, 3.0, 0.1, 100.0);
    ASSERT(m != NULL);
    double v = rt_mat4_get(m, 1, 1);
    ASSERT(!isnan(v) && !isinf(v));
}

static void test_mat4_ortho_equal_near_far(void) {
    // near == far → division by zero.
    void *m = rt_mat4_ortho(-1.0, 1.0, -1.0, 1.0, 50.0, 50.0);
    ASSERT(m != NULL);
    double v = rt_mat4_get(m, 2, 2);
    ASSERT(!isnan(v) && !isinf(v));
}

static void test_mat4_ortho_valid(void) {
    void *m = rt_mat4_ortho(-10.0, 10.0, -10.0, 10.0, 0.1, 100.0);
    ASSERT(m != NULL);
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            double v = rt_mat4_get(m, r, c);
            ASSERT(!isnan(v) && !isinf(v));
        }
    }
}

//=============================================================================
// main
//=============================================================================

int main(void) {
    // rt_duration_abs
    test_duration_abs_int64_min();
    test_duration_abs_positive();
    test_duration_abs_negative();
    test_duration_abs_zero();

    // rt_duration_neg
    test_duration_neg_int64_min();
    test_duration_neg_positive();
    test_duration_neg_negative();
    test_duration_neg_zero();

    // rt_fmt_to_words
    test_fmt_to_words_int64_min();
    test_fmt_to_words_zero();
    test_fmt_to_words_negative();
    test_fmt_to_words_positive();

    // rt_f64_to_i64
    test_f64_to_i64_clamp_max();
    test_f64_to_i64_clamp_min();
    test_f64_to_i64_nan();
    test_f64_to_i64_positive_inf();
    test_f64_to_i64_negative_inf();
    test_f64_to_i64_normal();
    test_f64_to_i64_boundaries();
    test_round_even_ignores_fenv_rounding_mode();

    // rt_mat4_perspective
    test_mat4_perspective_zero_fov();
    test_mat4_perspective_negative_fov();
    test_mat4_perspective_zero_aspect();
    test_mat4_perspective_near_zero();
    test_mat4_perspective_near_equals_far();
    test_mat4_perspective_valid();

    // rt_mat4_ortho
    test_mat4_ortho_equal_left_right();
    test_mat4_ortho_equal_top_bottom();
    test_mat4_ortho_equal_near_far();
    test_mat4_ortho_valid();

    printf("%d/%d tests passed\n", tests_run - tests_failed, tests_run);
    return tests_failed > 0 ? 1 : 0;
}
