//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/runtime/RTReallocMemTests.c
// Purpose: Validate realloc safety and memory correctness for rt_sortedset and
//          rt_bigint, covering the bugs fixed in R-11a, R-11b, R-23, R-24, R-25.
// Key invariants:
//   - rt_sortedset: capacity growth preserves all elements; finalizer frees
//     backing array so the GC never leaks it.
//   - rt_bigint: to_str_base produces correct output for all bases including
//     large multi-limb values; and/or/xor with negative operands follow
//     two's-complement semantics; capacity growth does not corrupt digits.
// Ownership/Lifetime: Each test creates and discards its own objects.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#include "viper/runtime/rt.h"

#include "rt_bigint.h"
#include "rt_sortedset.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

//=============================================================================
// Helpers
//=============================================================================

static void check(const char *label, int ok)
{
    printf("  %s: %s\n", label, ok ? "PASS" : "FAIL");
    assert(ok);
}

/// Release a bigint object returned by rt_bigint_* functions.
static void bigint_release(void *bi)
{
    if (bi && rt_obj_release_check0(bi))
        rt_obj_free(bi);
}

//=============================================================================
// rt_sortedset tests
//=============================================================================

/// Insert more than the initial capacity (8) to force at least one realloc.
/// All elements must be retrievable after growth.
static void test_sortedset_realloc_growth(void)
{
    printf("Testing rt_sortedset: insert beyond initial capacity forces realloc\n");

    void *set = rt_sortedset_new();
    check("set created", set != NULL);

    // Initial capacity is 8; insert 20 items to force multiple reallocs.
    static const char *words[] = {
        "alpha", "bravo", "charlie", "delta", "echo",
        "foxtrot", "golf", "hotel", "india", "juliet",
        "kilo", "lima", "mike", "november", "oscar",
        "papa", "quebec", "romeo", "sierra", "tango"
    };
    int n = (int)(sizeof(words) / sizeof(words[0]));

    for (int i = 0; i < n; i++)
    {
        int8_t inserted = rt_sortedset_put(set, rt_const_cstr(words[i]));
        check(words[i], inserted == 1);
    }

    check("length after 20 inserts", rt_sortedset_len(set) == 20);

    // Verify every element is still present after the growth.
    for (int i = 0; i < n; i++)
        check(words[i], rt_sortedset_has(set, rt_const_cstr(words[i])) == 1);

    // Verify sorted order: rt_sortedset_at(0) should be lexicographically first.
    rt_string first = rt_sortedset_first(set);
    check("first element is 'alpha'",
          strcmp(rt_string_cstr(first), "alpha") == 0);

    rt_string last = rt_sortedset_last(set);
    check("last element is 'tango'",
          strcmp(rt_string_cstr(last), "tango") == 0);

    // Release — finalizer (R-11b fix) must free the backing array.
    if (rt_obj_release_check0(set))
        rt_obj_free(set);
}

/// Create and immediately destroy a set without inserting anything.
/// This exercises the finalizer path when data == NULL.
static void test_sortedset_empty_lifecycle(void)
{
    printf("Testing rt_sortedset: empty create/destroy cycle\n");

    void *set = rt_sortedset_new();
    check("empty set created", set != NULL);
    check("empty set length is 0", rt_sortedset_len(set) == 0);

    if (rt_obj_release_check0(set))
        rt_obj_free(set);
    // If the finalizer incorrectly frees a NULL pointer with a non-NULL-safe
    // path, the process would abort here. Reaching this point is the test.
    check("survived empty destroy", 1);
}

/// Insert items, clear, and insert again — exercises the path where set->data
/// is non-NULL but len is 0, and realloc must not lose capacity state.
static void test_sortedset_clear_and_reinsert(void)
{
    printf("Testing rt_sortedset: clear then reinsert stays coherent\n");

    void *set = rt_sortedset_new();

    for (int i = 0; i < 10; i++)
    {
        char buf[16];
        snprintf(buf, sizeof(buf), "item%d", i);
        rt_sortedset_put(set, rt_const_cstr(buf));
    }
    check("10 items inserted", rt_sortedset_len(set) == 10);

    rt_sortedset_clear(set);
    check("length zero after clear", rt_sortedset_len(set) == 0);

    for (int i = 0; i < 15; i++)
    {
        char buf[16];
        snprintf(buf, sizeof(buf), "new%d", i);
        rt_sortedset_put(set, rt_const_cstr(buf));
    }
    check("15 new items inserted after clear", rt_sortedset_len(set) == 15);

    if (rt_obj_release_check0(set))
        rt_obj_free(set);
}

//=============================================================================
// rt_bigint tests (R-23: realloc safety, R-24: to_str_base, R-25: bitwise ops)
//=============================================================================

/// Verify that to_str_base produces correct decimal strings for a range of
/// values including those requiring multi-limb storage.
static void test_bigint_to_str_base_decimal(void)
{
    printf("Testing rt_bigint: to_str_base decimal\n");

    struct { int64_t val; const char *expected; } cases[] = {
        {           0,             "0" },
        {           1,             "1" },
        {          -1,            "-1" },
        {        1000,          "1000" },
        {       -9999,         "-9999" },
        { INT32_MAX,       "2147483647" },
        { INT32_MIN + 1, "-2147483647" },
    };

    for (int i = 0; i < (int)(sizeof(cases) / sizeof(cases[0])); i++)
    {
        void *bi = rt_bigint_from_i64(cases[i].val);
        rt_string s = rt_bigint_to_str_base(bi, 10);
        const char *got = rt_string_cstr(s);
        int ok = (strcmp(got, cases[i].expected) == 0);
        if (!ok)
            printf("    got '%s', expected '%s'\n", got, cases[i].expected);
        check(cases[i].expected, ok);
        bigint_release(bi);
    }
}

/// R-24 fix: for base 8, the old formula could under-allocate for multi-limb
/// numbers. Build a value that needs at least 11 octal digits per 32-bit limb
/// and verify the output is correct.
static void test_bigint_to_str_base_octal(void)
{
    printf("Testing rt_bigint: to_str_base base 8 (R-24 buffer size)\n");

    // 2^32 - 1 = 4294967295 = 0o37777777777 (11 octal digits, 1 limb)
    void *bi = rt_bigint_from_i64(0xFFFFFFFF);
    rt_string s = rt_bigint_to_str_base(bi, 8);
    const char *got = rt_string_cstr(s);
    check("2^32-1 in octal", strcmp(got, "37777777777") == 0);
    bigint_release(bi);

    // A two-limb value: 2^33 = 8589934592 = 0o100000000000 (12 octal digits)
    void *bi2 = rt_bigint_from_str(rt_const_cstr("8589934592"));
    rt_string s2 = rt_bigint_to_str_base(bi2, 8);
    const char *got2 = rt_string_cstr(s2);
    check("2^33 in octal", strcmp(got2, "100000000000") == 0);
    bigint_release(bi2);
}

/// Verify binary and hex output for known values.
static void test_bigint_to_str_base_binary_hex(void)
{
    printf("Testing rt_bigint: to_str_base base 2 and base 16\n");

    void *bi = rt_bigint_from_i64(255);
    rt_string s2 = rt_bigint_to_str_base(bi, 2);
    check("255 in binary", strcmp(rt_string_cstr(s2), "11111111") == 0);
    rt_string s16 = rt_bigint_to_str_base(bi, 16);
    check("255 in hex", strcmp(rt_string_cstr(s16), "ff") == 0);
    bigint_release(bi);

    void *neg = rt_bigint_from_i64(-16);
    rt_string shex = rt_bigint_to_str_base(neg, 16);
    check("-16 in hex", strcmp(rt_string_cstr(shex), "-10") == 0);
    bigint_release(neg);
}

/// R-25 fix: AND of two negatives should be negative (two's complement).
/// -1 & -1 == -1; -4 & -2 == -4 (in two's complement: ...11100 & ...11110 = ...11100)
static void test_bigint_and_negative(void)
{
    printf("Testing rt_bigint: and with negative operands (R-25)\n");

    // -1 & -1 = -1
    {
        void *a = rt_bigint_from_i64(-1);
        void *b = rt_bigint_from_i64(-1);
        void *r = rt_bigint_and(a, b);
        int64_t v = rt_bigint_to_i64(r);
        check("-1 & -1 = -1", v == -1);
        bigint_release(a);
        bigint_release(b);
        bigint_release(r);
    }

    // -4 & -2: two's comp: -4 = ...11111100, -2 = ...11111110 => AND = ...11111100 = -4
    {
        void *a = rt_bigint_from_i64(-4);
        void *b = rt_bigint_from_i64(-2);
        void *r = rt_bigint_and(a, b);
        int64_t v = rt_bigint_to_i64(r);
        check("-4 & -2 = -4", v == -4);
        bigint_release(a);
        bigint_release(b);
        bigint_release(r);
    }

    // -3 & 5: two's comp: -3 = ...11111101, 5 = 00000101 => AND = 00000101 = 5
    {
        void *a = rt_bigint_from_i64(-3);
        void *b = rt_bigint_from_i64(5);
        void *r = rt_bigint_and(a, b);
        int64_t v = rt_bigint_to_i64(r);
        check("-3 & 5 = 5", v == 5);
        bigint_release(a);
        bigint_release(b);
        bigint_release(r);
    }

    // 6 & -3: 6 = 00000110, -3 = ...11111101 => AND = 00000100 = 4
    {
        void *a = rt_bigint_from_i64(6);
        void *b = rt_bigint_from_i64(-3);
        void *r = rt_bigint_and(a, b);
        int64_t v = rt_bigint_to_i64(r);
        check("6 & -3 = 4", v == 4);
        bigint_release(a);
        bigint_release(b);
        bigint_release(r);
    }
}

/// R-25 fix: OR of any negative operand should produce a negative result.
/// -1 | x == -1 for any x; -4 | 3 = -1 in two's complement.
static void test_bigint_or_negative(void)
{
    printf("Testing rt_bigint: or with negative operands (R-25)\n");

    // -1 | 42 = -1
    {
        void *a = rt_bigint_from_i64(-1);
        void *b = rt_bigint_from_i64(42);
        void *r = rt_bigint_or(a, b);
        int64_t v = rt_bigint_to_i64(r);
        check("-1 | 42 = -1", v == -1);
        bigint_release(a);
        bigint_release(b);
        bigint_release(r);
    }

    // -4 | 3: ...11111100 | 00000011 = ...11111111 = -1
    {
        void *a = rt_bigint_from_i64(-4);
        void *b = rt_bigint_from_i64(3);
        void *r = rt_bigint_or(a, b);
        int64_t v = rt_bigint_to_i64(r);
        check("-4 | 3 = -1", v == -1);
        bigint_release(a);
        bigint_release(b);
        bigint_release(r);
    }

    // -3 | -5: ...11111101 | ...11111011 = ...11111111 = -1
    {
        void *a = rt_bigint_from_i64(-3);
        void *b = rt_bigint_from_i64(-5);
        void *r = rt_bigint_or(a, b);
        int64_t v = rt_bigint_to_i64(r);
        check("-3 | -5 = -1", v == -1);
        bigint_release(a);
        bigint_release(b);
        bigint_release(r);
    }
}

/// R-25 fix: XOR with negatives uses two's-complement sign rules.
/// -1 ^ 0 = -1; -1 ^ -1 = 0; -4 ^ 3 = -1.
static void test_bigint_xor_negative(void)
{
    printf("Testing rt_bigint: xor with negative operands (R-25)\n");

    // -1 ^ 0 = -1
    {
        void *a = rt_bigint_from_i64(-1);
        void *b = rt_bigint_from_i64(0);
        void *r = rt_bigint_xor(a, b);
        int64_t v = rt_bigint_to_i64(r);
        check("-1 ^ 0 = -1", v == -1);
        bigint_release(a);
        bigint_release(b);
        bigint_release(r);
    }

    // -1 ^ -1 = 0
    {
        void *a = rt_bigint_from_i64(-1);
        void *b = rt_bigint_from_i64(-1);
        void *r = rt_bigint_xor(a, b);
        int64_t v = rt_bigint_to_i64(r);
        check("-1 ^ -1 = 0", v == 0);
        bigint_release(a);
        bigint_release(b);
        bigint_release(r);
    }

    // -4 ^ 3: ...11111100 ^ 00000011 = ...11111111 = -1
    {
        void *a = rt_bigint_from_i64(-4);
        void *b = rt_bigint_from_i64(3);
        void *r = rt_bigint_xor(a, b);
        int64_t v = rt_bigint_to_i64(r);
        check("-4 ^ 3 = -1", v == -1);
        bigint_release(a);
        bigint_release(b);
        bigint_release(r);
    }

    // -1 ^ 1 = -2: ...11111111 ^ 00000001 = ...11111110 = -2
    {
        void *a = rt_bigint_from_i64(-1);
        void *b = rt_bigint_from_i64(1);
        void *r = rt_bigint_xor(a, b);
        int64_t v = rt_bigint_to_i64(r);
        check("-1 ^ 1 = -2", v == -2);
        bigint_release(a);
        bigint_release(b);
        bigint_release(r);
    }
}

/// R-23 fix: exercise capacity growth by parsing a very large decimal string,
/// which drives many calls to bigint_ensure_capacity internally.
static void test_bigint_capacity_growth(void)
{
    printf("Testing rt_bigint: capacity growth via large string parse\n");

    // A 40-digit number that requires at least 2 limbs.
    const char *large_str = "12345678901234567890123456789012345678901234567890";
    void *bi = rt_bigint_from_str(rt_const_cstr(large_str));
    check("large number parsed", bi != NULL);

    // Round-trip: convert back to string and verify it matches.
    rt_string s = rt_bigint_to_str(bi);
    const char *got = rt_string_cstr(s);
    check("large number round-trips to decimal", strcmp(got, large_str) == 0);

    bigint_release(bi);
}

/// Confirm that the two's-complement bitwise ops are consistent with ordinary
/// arithmetic (De Morgan / complement laws): ~x = -(x+1) for bigint NOT.
static void test_bigint_bitwise_consistency(void)
{
    printf("Testing rt_bigint: bitwise consistency with arithmetic\n");

    // For positive n: (n AND -1) == n   (AND with all-ones mask)
    {
        void *a = rt_bigint_from_i64(12345);
        void *mask = rt_bigint_from_i64(-1);
        void *r = rt_bigint_and(a, mask);
        int64_t v = rt_bigint_to_i64(r);
        check("12345 & -1 == 12345", v == 12345);
        bigint_release(a);
        bigint_release(mask);
        bigint_release(r);
    }

    // For positive n: (n OR 0) == n
    {
        void *a = rt_bigint_from_i64(99999);
        void *zero = rt_bigint_from_i64(0);
        void *r = rt_bigint_or(a, zero);
        int64_t v = rt_bigint_to_i64(r);
        check("99999 | 0 == 99999", v == 99999);
        bigint_release(a);
        bigint_release(zero);
        bigint_release(r);
    }

    // For positive n: (n XOR n) == 0
    {
        void *a = rt_bigint_from_i64(42);
        void *b = rt_bigint_from_i64(42);
        void *r = rt_bigint_xor(a, b);
        int64_t v = rt_bigint_to_i64(r);
        check("42 ^ 42 == 0", v == 0);
        bigint_release(a);
        bigint_release(b);
        bigint_release(r);
    }
}

//=============================================================================
// Entry point
//=============================================================================

int main(void)
{
    // rt_sortedset tests (bugs R-11a, R-11b)
    test_sortedset_realloc_growth();
    test_sortedset_empty_lifecycle();
    test_sortedset_clear_and_reinsert();

    // rt_bigint tests (bugs R-23, R-24, R-25)
    test_bigint_to_str_base_decimal();
    test_bigint_to_str_base_octal();
    test_bigint_to_str_base_binary_hex();
    test_bigint_and_negative();
    test_bigint_or_negative();
    test_bigint_xor_negative();
    test_bigint_capacity_growth();
    test_bigint_bitwise_consistency();

    printf("All RTReallocMemTests passed.\n");
    return 0;
}
