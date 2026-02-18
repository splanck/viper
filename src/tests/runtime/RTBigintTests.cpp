//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/runtime/RTBigintTests.cpp
// Purpose: Validate Viper.Math.BigInt (rt_bigint_*) arithmetic and conversions.
// Key invariants: All operations produce normalized results; i64 roundtrip is
//                 exact for values in [-2^63, 2^63-1].
// Ownership/Lifetime: BigInt objects are released via rt_obj_release_check0.
//
//===----------------------------------------------------------------------===//

#include "viper/runtime/rt.h"

#include "rt_bigint.h"
#include "rt_string.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void check(const char *label, int ok)
{
    printf("  %-50s %s\n", label, ok ? "PASS" : "FAIL");
    assert(ok);
}

static void bi_release(void *bi)
{
    if (bi && rt_obj_release_check0(bi))
        rt_obj_free(bi);
}

static int str_eq_c(rt_string s, const char *expected)
{
    size_t elen = strlen(expected);
    rt_string exp = rt_string_from_bytes(expected, elen);
    int result = rt_str_eq(s, exp);
    rt_string_unref(exp);
    return result;
}

static void test_from_i64(void)
{
    printf("rt_bigint_from_i64 / rt_bigint_to_i64:\n");
    void *zero = rt_bigint_zero();
    check("zero to_i64 == 0", rt_bigint_to_i64(zero) == 0);
    check("zero fits_i64", rt_bigint_fits_i64(zero));
    bi_release(zero);

    void *one = rt_bigint_one();
    check("one to_i64 == 1", rt_bigint_to_i64(one) == 1);
    bi_release(one);

    void *neg = rt_bigint_from_i64(-42LL);
    check("neg to_i64 == -42", rt_bigint_to_i64(neg) == -42LL);
    check("neg fits_i64", rt_bigint_fits_i64(neg));
    bi_release(neg);

    void *large = rt_bigint_from_i64(9999999999LL);
    check("large to_i64", rt_bigint_to_i64(large) == 9999999999LL);
    bi_release(large);
}

static void test_to_str(void)
{
    printf("rt_bigint_to_str:\n");
    void *bi = rt_bigint_from_i64(123456789LL);
    rt_string s = rt_bigint_to_str(bi);
    check("to_str '123456789'", str_eq_c(s, "123456789"));
    rt_string_unref(s);
    bi_release(bi);

    void *neg = rt_bigint_from_i64(-987LL);
    rt_string ns = rt_bigint_to_str(neg);
    check("neg to_str '-987'", str_eq_c(ns, "-987"));
    rt_string_unref(ns);
    bi_release(neg);

    // Binary base
    void *eight = rt_bigint_from_i64(8LL);
    rt_string bs = rt_bigint_to_str_base(eight, 2);
    check("8 in base 2 is '1000'", str_eq_c(bs, "1000"));
    rt_string_unref(bs);
    bi_release(eight);

    // Hex base
    void *ff = rt_bigint_from_i64(255LL);
    rt_string hs = rt_bigint_to_str_base(ff, 16);
    check("255 in base 16 is 'ff'", str_eq_c(hs, "ff"));
    rt_string_unref(hs);
    bi_release(ff);
}

static void test_from_str(void)
{
    printf("rt_bigint_from_str:\n");
    rt_string s = rt_string_from_bytes("999999999999999999", 18);
    void *bi = rt_bigint_from_str(s);
    rt_string_unref(s);
    check("from_str non-null", bi != NULL);
    check("fits_i64 == 1", rt_bigint_fits_i64(bi));
    check("to_i64 == 999999999999999999", rt_bigint_to_i64(bi) == 999999999999999999LL);
    bi_release(bi);

    // Very large number — beyond i64
    rt_string big = rt_string_from_bytes("99999999999999999999999999", 26);
    void *huge = rt_bigint_from_str(big);
    rt_string_unref(big);
    check("huge non-null", huge != NULL);
    check("huge fits_i64 == 0", !rt_bigint_fits_i64(huge));
    bi_release(huge);
}

static void test_arithmetic(void)
{
    printf("rt_bigint arithmetic:\n");
    void *a = rt_bigint_from_i64(100LL);
    void *b = rt_bigint_from_i64(37LL);

    void *sum = rt_bigint_add(a, b);
    check("100 + 37 == 137", rt_bigint_to_i64(sum) == 137LL);
    bi_release(sum);

    void *diff = rt_bigint_sub(a, b);
    check("100 - 37 == 63", rt_bigint_to_i64(diff) == 63LL);
    bi_release(diff);

    void *prod = rt_bigint_mul(a, b);
    check("100 * 37 == 3700", rt_bigint_to_i64(prod) == 3700LL);
    bi_release(prod);

    void *quot = rt_bigint_div(a, b);
    check("100 / 37 == 2", rt_bigint_to_i64(quot) == 2LL);
    bi_release(quot);

    void *rem = rt_bigint_mod(a, b);
    check("100 % 37 == 26", rt_bigint_to_i64(rem) == 26LL);
    bi_release(rem);

    void *neg_a = rt_bigint_neg(a);
    check("neg(100) == -100", rt_bigint_to_i64(neg_a) == -100LL);

    void *abs_val = rt_bigint_abs(neg_a);
    check("abs(-100) == 100", rt_bigint_to_i64(abs_val) == 100LL);
    bi_release(abs_val);
    bi_release(neg_a);

    bi_release(b);
    bi_release(a);
}

static void test_comparison(void)
{
    printf("rt_bigint comparison:\n");
    void *a = rt_bigint_from_i64(10LL);
    void *b = rt_bigint_from_i64(20LL);
    void *c = rt_bigint_from_i64(10LL);

    check("cmp(10, 20) < 0", rt_bigint_cmp(a, b) < 0);
    check("cmp(20, 10) > 0", rt_bigint_cmp(b, a) > 0);
    check("cmp(10, 10) == 0", rt_bigint_cmp(a, c) == 0);
    check("eq(10, 10)", rt_bigint_eq(a, c));

    bi_release(c);
    bi_release(b);
    bi_release(a);
}

int main(void)
{
    printf("=== RTBigintTests ===\n");
    test_from_i64();
    test_to_str();
    test_from_str();
    test_arithmetic();
    test_comparison();
    printf("All BigInt tests passed.\n");
    return 0;
}
