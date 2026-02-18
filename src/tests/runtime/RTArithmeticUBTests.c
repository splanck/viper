//===----------------------------------------------------------------------===//
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
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int tests_run = 0;
static int tests_failed = 0;

#define ASSERT(cond)                                                                               \
    do                                                                                             \
    {                                                                                              \
        tests_run++;                                                                               \
        if (!(cond))                                                                               \
        {                                                                                          \
            tests_failed++;                                                                        \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                        \
        }                                                                                          \
    } while (0)

//=============================================================================
// rt_duration_abs — Bug R-13
//=============================================================================

static void test_duration_abs_int64_min(void)
{
    // INT64_MIN cannot be negated as signed without UB.
    // The fix casts through uint64_t, so the result wraps to INT64_MIN
    // (which is the only representable "abs" of INT64_MIN as int64_t).
    int64_t result = rt_duration_abs(INT64_MIN);
    // The wrapped result must not be positive (it equals INT64_MIN by two's complement).
    ASSERT(result == INT64_MIN);
}

static void test_duration_abs_positive(void)
{
    ASSERT(rt_duration_abs(42LL) == 42LL);
}

static void test_duration_abs_negative(void)
{
    ASSERT(rt_duration_abs(-1000LL) == 1000LL);
}

static void test_duration_abs_zero(void)
{
    ASSERT(rt_duration_abs(0LL) == 0LL);
}

//=============================================================================
// rt_duration_neg — Bug R-13
//=============================================================================

static void test_duration_neg_int64_min(void)
{
    // Negating INT64_MIN as signed is UB.  The fix casts through uint64_t.
    // -(uint64_t)INT64_MIN wraps around to INT64_MIN in two's complement.
    int64_t result = rt_duration_neg(INT64_MIN);
    ASSERT(result == INT64_MIN);
}

static void test_duration_neg_positive(void)
{
    ASSERT(rt_duration_neg(5000LL) == -5000LL);
}

static void test_duration_neg_negative(void)
{
    ASSERT(rt_duration_neg(-5000LL) == 5000LL);
}

static void test_duration_neg_zero(void)
{
    ASSERT(rt_duration_neg(0LL) == 0LL);
}

//=============================================================================
// rt_fmt_to_words — Bug R-20
//=============================================================================

static void test_fmt_to_words_int64_min(void)
{
    // Before the fix, value = -value was UB for INT64_MIN.
    // After the fix the function should return a non-NULL string without crashing.
    rt_string s = rt_fmt_to_words(INT64_MIN);
    ASSERT(s != NULL);
    const char *cstr = rt_string_cstr(s);
    ASSERT(cstr != NULL && cstr[0] != '\0');
    rt_string_unref(s);
}

static void test_fmt_to_words_zero(void)
{
    rt_string s = rt_fmt_to_words(0LL);
    ASSERT(s != NULL);
    const char *cstr = rt_string_cstr(s);
    ASSERT(cstr != NULL);
    ASSERT(strcmp(cstr, "zero") == 0);
    rt_string_unref(s);
}

static void test_fmt_to_words_negative(void)
{
    // A typical negative number that isn't INT64_MIN.
    rt_string s = rt_fmt_to_words(-1LL);
    ASSERT(s != NULL);
    const char *cstr = rt_string_cstr(s);
    ASSERT(cstr != NULL && cstr[0] != '\0');
    rt_string_unref(s);
}

static void test_fmt_to_words_positive(void)
{
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

static void test_f64_to_i64_clamp_max(void)
{
    // A double value clearly above INT64_MAX should clamp to INT64_MAX.
    long long result = rt_f64_to_i64(1.0e19);
    ASSERT(result == INT64_MAX);
}

static void test_f64_to_i64_clamp_min(void)
{
    // A double value clearly below INT64_MIN should clamp to INT64_MIN.
    long long result = rt_f64_to_i64(-1.0e19);
    ASSERT(result == INT64_MIN);
}

static void test_f64_to_i64_nan(void)
{
    long long result = rt_f64_to_i64((double)NAN);
    ASSERT(result == 0LL);
}

static void test_f64_to_i64_positive_inf(void)
{
    long long result = rt_f64_to_i64((double)INFINITY);
    ASSERT(result == INT64_MAX);
}

static void test_f64_to_i64_negative_inf(void)
{
    long long result = rt_f64_to_i64(-(double)INFINITY);
    ASSERT(result == INT64_MIN);
}

static void test_f64_to_i64_normal(void)
{
    ASSERT(rt_f64_to_i64(3.9) == 3LL);
    ASSERT(rt_f64_to_i64(-3.9) == -3LL);
    ASSERT(rt_f64_to_i64(0.0) == 0LL);
}

//=============================================================================
// rt_mat4_perspective — Bug R-27
//=============================================================================

static void test_mat4_perspective_zero_fov(void)
{
    // fov == 0 → division by zero → NaN matrix without the guard.
    void *m = rt_mat4_perspective(0.0, 1.0, 0.1, 100.0);
    ASSERT(m != NULL);
    double v = rt_mat4_get(m, 0, 0);
    ASSERT(!isnan(v) && !isinf(v));
}

static void test_mat4_perspective_negative_fov(void)
{
    void *m = rt_mat4_perspective(-1.0, 1.0, 0.1, 100.0);
    ASSERT(m != NULL);
    double v = rt_mat4_get(m, 0, 0);
    ASSERT(!isnan(v) && !isinf(v));
}

static void test_mat4_perspective_zero_aspect(void)
{
    void *m = rt_mat4_perspective(1.0, 0.0, 0.1, 100.0);
    ASSERT(m != NULL);
    double v = rt_mat4_get(m, 0, 0);
    ASSERT(!isnan(v) && !isinf(v));
}

static void test_mat4_perspective_near_zero(void)
{
    void *m = rt_mat4_perspective(1.0, 1.0, 0.0, 100.0);
    ASSERT(m != NULL);
    double v = rt_mat4_get(m, 0, 0);
    ASSERT(!isnan(v) && !isinf(v));
}

static void test_mat4_perspective_near_equals_far(void)
{
    // near == far → division by zero in (near - far).
    void *m = rt_mat4_perspective(1.0, 1.0, 10.0, 10.0);
    ASSERT(m != NULL);
    double v = rt_mat4_get(m, 2, 2);
    ASSERT(!isnan(v) && !isinf(v));
}

static void test_mat4_perspective_valid(void)
{
    // A valid perspective call should not produce NaN or Inf in any element.
    void *m = rt_mat4_perspective(1.0, 16.0 / 9.0, 0.1, 1000.0);
    ASSERT(m != NULL);
    for (int r = 0; r < 4; ++r)
    {
        for (int c = 0; c < 4; ++c)
        {
            double v = rt_mat4_get(m, r, c);
            ASSERT(!isnan(v) && !isinf(v));
        }
    }
}

//=============================================================================
// rt_mat4_ortho — Bug R-27
//=============================================================================

static void test_mat4_ortho_equal_left_right(void)
{
    // right == left → division by zero.
    void *m = rt_mat4_ortho(5.0, 5.0, -1.0, 1.0, 0.1, 100.0);
    ASSERT(m != NULL);
    double v = rt_mat4_get(m, 0, 0);
    ASSERT(!isnan(v) && !isinf(v));
}

static void test_mat4_ortho_equal_top_bottom(void)
{
    // top == bottom → division by zero.
    void *m = rt_mat4_ortho(-1.0, 1.0, 3.0, 3.0, 0.1, 100.0);
    ASSERT(m != NULL);
    double v = rt_mat4_get(m, 1, 1);
    ASSERT(!isnan(v) && !isinf(v));
}

static void test_mat4_ortho_equal_near_far(void)
{
    // near == far → division by zero.
    void *m = rt_mat4_ortho(-1.0, 1.0, -1.0, 1.0, 50.0, 50.0);
    ASSERT(m != NULL);
    double v = rt_mat4_get(m, 2, 2);
    ASSERT(!isnan(v) && !isinf(v));
}

static void test_mat4_ortho_valid(void)
{
    void *m = rt_mat4_ortho(-10.0, 10.0, -10.0, 10.0, 0.1, 100.0);
    ASSERT(m != NULL);
    for (int r = 0; r < 4; ++r)
    {
        for (int c = 0; c < 4; ++c)
        {
            double v = rt_mat4_get(m, r, c);
            ASSERT(!isnan(v) && !isinf(v));
        }
    }
}

//=============================================================================
// main
//=============================================================================

int main(void)
{
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
