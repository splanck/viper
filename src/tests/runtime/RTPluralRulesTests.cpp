//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/runtime/RTPluralRulesTests.cpp
// Purpose: Validate Viper.Localization.PluralRules against the baked en-US
//          CLDR cardinal and ordinal rule tables. Locales other than en-US
//          are not yet available (JSON loader lands in a later phase), so
//          the test matrix focuses on exercising every AST node kind
//          (VAR, INT, EQ, NE, AND, TRUE) and every operand (n, i, v, f, t)
//          through the en-US rules.
//
//===----------------------------------------------------------------------===//

#include "rt_locale.h"
#include "rt_plural_rules.h"
#include "rt_string.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static void test_result(const char *name, bool passed) {
    printf("  %s: %s\n", name, passed ? "PASS" : "FAIL");
    assert(passed);
}

static rt_string S(const char *s) {
    return rt_string_from_bytes(s, strlen(s));
}

static bool eq(rt_string s, const char *expected) {
    const char *cs = rt_string_cstr(s);
    bool ok = cs && strcmp(cs, expected) == 0;
    rt_string_unref(s);
    return ok;
}

static void *en_rules() {
    rt_string in = S("en-US");
    void *loc = rt_locale_parse(in);
    rt_string_unref(in);
    return rt_plural_rules_for_locale(loc);
}

//=============================================================================
// Cardinal — integer path
//=============================================================================

static void test_cardinal_int_en() {
    printf("Testing PluralRules.CardinalInt (en-US):\n");
    void *pr = en_rules();

    test_result("CardinalInt(1) = one",  eq(rt_plural_rules_cardinal_int(pr, 1), "one"));
    test_result("CardinalInt(0) = other",eq(rt_plural_rules_cardinal_int(pr, 0), "other"));
    test_result("CardinalInt(2) = other",eq(rt_plural_rules_cardinal_int(pr, 2), "other"));
    test_result("CardinalInt(3) = other",eq(rt_plural_rules_cardinal_int(pr, 3), "other"));
    test_result("CardinalInt(100) = other",
                eq(rt_plural_rules_cardinal_int(pr, 100), "other"));
    test_result("CardinalInt(-1) = one (abs)",
                eq(rt_plural_rules_cardinal_int(pr, -1), "one"));
    test_result("CardinalInt(-7) = other (abs)",
                eq(rt_plural_rules_cardinal_int(pr, -7), "other"));
}

//=============================================================================
// Cardinal — double path (operand computation)
//=============================================================================

static void test_cardinal_double_en() {
    printf("Testing PluralRules.Cardinal (en-US):\n");
    void *pr = en_rules();

    // Integer-valued double: same as int path.
    test_result("Cardinal(1.0) = one",   eq(rt_plural_rules_cardinal(pr, 1.0), "one"));
    test_result("Cardinal(0.0) = other", eq(rt_plural_rules_cardinal(pr, 0.0), "other"));
    test_result("Cardinal(2.0) = other", eq(rt_plural_rules_cardinal(pr, 2.0), "other"));

    // Fractional doubles trigger v > 0 which forces "other" even for i==1.
    test_result("Cardinal(1.5) = other", eq(rt_plural_rules_cardinal(pr, 1.5), "other"));
    test_result("Cardinal(1.0000001) = other",
                eq(rt_plural_rules_cardinal(pr, 1.0000001), "other"));
    test_result("Cardinal(0.5) = other", eq(rt_plural_rules_cardinal(pr, 0.5), "other"));
}

//=============================================================================
// Ordinal
//=============================================================================

static void test_ordinal_en() {
    printf("Testing PluralRules.Ordinal (en-US):\n");
    void *pr = en_rules();

    // n mod 10 == 1 and n mod 100 != 11 -> one
    test_result("Ordinal(1) = one",  eq(rt_plural_rules_ordinal(pr, 1),  "one"));
    test_result("Ordinal(21) = one", eq(rt_plural_rules_ordinal(pr, 21), "one"));
    test_result("Ordinal(31) = one", eq(rt_plural_rules_ordinal(pr, 31), "one"));
    test_result("Ordinal(101) = one",eq(rt_plural_rules_ordinal(pr, 101),"one"));

    // n mod 100 == 11 -> other (the teen exception)
    test_result("Ordinal(11) = other", eq(rt_plural_rules_ordinal(pr, 11), "other"));
    test_result("Ordinal(111) = other",eq(rt_plural_rules_ordinal(pr, 111),"other"));

    // n mod 10 == 2 and n mod 100 != 12 -> two
    test_result("Ordinal(2) = two",  eq(rt_plural_rules_ordinal(pr, 2),  "two"));
    test_result("Ordinal(22) = two", eq(rt_plural_rules_ordinal(pr, 22), "two"));
    test_result("Ordinal(102) = two",eq(rt_plural_rules_ordinal(pr, 102),"two"));
    test_result("Ordinal(12) = other",eq(rt_plural_rules_ordinal(pr, 12),"other"));
    test_result("Ordinal(112) = other",eq(rt_plural_rules_ordinal(pr, 112),"other"));

    // n mod 10 == 3 and n mod 100 != 13 -> few
    test_result("Ordinal(3) = few",  eq(rt_plural_rules_ordinal(pr, 3),  "few"));
    test_result("Ordinal(23) = few", eq(rt_plural_rules_ordinal(pr, 23), "few"));
    test_result("Ordinal(103) = few",eq(rt_plural_rules_ordinal(pr, 103),"few"));
    test_result("Ordinal(13) = other",eq(rt_plural_rules_ordinal(pr, 13),"other"));
    test_result("Ordinal(113) = other",eq(rt_plural_rules_ordinal(pr, 113),"other"));

    // other catch-all
    test_result("Ordinal(4) = other",  eq(rt_plural_rules_ordinal(pr, 4),  "other"));
    test_result("Ordinal(10) = other", eq(rt_plural_rules_ordinal(pr, 10), "other"));
    test_result("Ordinal(100) = other",eq(rt_plural_rules_ordinal(pr, 100),"other"));
    test_result("Ordinal(0) = other",  eq(rt_plural_rules_ordinal(pr, 0),  "other"));
}

//=============================================================================
// Categories() roster
//=============================================================================

static void test_categories() {
    printf("Testing PluralRules.Categories (en-US):\n");
    void *pr = en_rules();
    void *list = rt_plural_rules_categories(pr);

    extern int64_t rt_list_len(void *);
    extern void *rt_list_get(void *, int64_t);
    int64_t n = rt_list_len(list);
    test_result("Categories count >= 4 (one/two/few/other)", n >= 4);

    bool has_one = false, has_two = false, has_few = false, has_other = false;
    for (int64_t i = 0; i < n; ++i) {
        rt_string entry = (rt_string)rt_list_get(list, i);
        const char *cs = rt_string_cstr(entry);
        if (!cs) continue;
        if (strcmp(cs, "one") == 0)   has_one = true;
        if (strcmp(cs, "two") == 0)   has_two = true;
        if (strcmp(cs, "few") == 0)   has_few = true;
        if (strcmp(cs, "other") == 0) has_other = true;
    }
    test_result("Categories includes one",   has_one);
    test_result("Categories includes two",   has_two);
    test_result("Categories includes few",   has_few);
    test_result("Categories includes other", has_other);
}

//=============================================================================
// Invariant fallback
//=============================================================================

static void test_invariant_fallback() {
    printf("Testing PluralRules fallback:\n");
    void *pr = rt_plural_rules_for_locale(nullptr);
    // Invariant (en-US baked) still resolves one vs. other.
    test_result("ForLocale(null).CardinalInt(1) = one",
                eq(rt_plural_rules_cardinal_int(pr, 1), "one"));
    test_result("ForLocale(null).CardinalInt(5) = other",
                eq(rt_plural_rules_cardinal_int(pr, 5), "other"));
}

//=============================================================================
// Main
//=============================================================================

int main() {
    printf("=== RT PluralRules Tests ===\n\n");
    test_cardinal_int_en();
    test_cardinal_double_en();
    test_ordinal_en();
    test_categories();
    test_invariant_fallback();
    printf("\nAll PluralRules tests passed!\n");
    return 0;
}
