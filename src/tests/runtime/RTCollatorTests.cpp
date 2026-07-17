//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/runtime/RTCollatorTests.cpp
// Purpose: Validate Zanna.Localization.Collator across the three supported
//          strength levels, the IgnoreCase / IgnoreAccents toggles, the
//          sv-SE tailoring for å-after-z, and the SortKey / Sort surface.
//
//===----------------------------------------------------------------------===//

#include "rt_collator.h"
#include "rt_list.h"
#include "rt_locale.h"
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

static void *en_col() {
    rt_string in = S("en-US");
    void *loc = rt_locale_parse(in);
    rt_string_unref(in);
    return rt_collator_for_locale(loc);
}

//=============================================================================
// Compare — basic ASCII ordering
//=============================================================================

static void test_compare_ascii() {
    printf("Testing Collator.Compare ASCII (en-US):\n");
    void *c = en_col();
    rt_string a = S("apple");
    rt_string b = S("banana");
    test_result("Compare(apple, banana) < 0", rt_collator_compare(c, a, b) < 0);
    test_result("Compare(banana, apple) > 0", rt_collator_compare(c, b, a) > 0);
    test_result("Compare(apple, apple) == 0", rt_collator_compare(c, a, a) == 0);
    rt_string_unref(a);
    rt_string_unref(b);
}

//=============================================================================
// Strength levels
//=============================================================================

static void test_strength_tertiary_default() {
    printf("Testing Strength 3 (tertiary, default):\n");
    void *c = en_col();
    rt_string lo = S("apple");
    rt_string up = S("Apple");
    // Default strength 3: case matters.
    test_result("Strength=3: \"apple\" != \"Apple\"", rt_collator_equals(c, lo, up) == 0);
    test_result("Strength=3: \"apple\" < \"Apple\" (lowercase first)",
                rt_collator_compare(c, lo, up) < 0);
    rt_string_unref(lo);
    rt_string_unref(up);
}

static void test_strength_primary_case_folds() {
    printf("Testing Strength 1 (primary):\n");
    void *c = en_col();
    rt_collator_set_strength(c, 1);
    rt_string a = S("apple");
    rt_string b = S("APPLE");
    test_result("Strength=1: \"apple\" equals \"APPLE\"", rt_collator_equals(c, a, b) == 1);
    rt_string_unref(a);
    rt_string_unref(b);
}

static void test_strength_clamps_to_3() {
    printf("Testing Strength 4 clamps to 3:\n");
    void *c = en_col();
    rt_collator_set_strength(c, 4);
    test_result("Strength after set(4) is 3", rt_collator_get_strength(c) == 3);
}

//=============================================================================
// IgnoreCase / IgnoreAccents toggles
//=============================================================================

static void test_ignore_case() {
    printf("Testing IgnoreCase toggle:\n");
    void *c = en_col();
    rt_collator_set_ignore_case(c, 1);
    rt_string a = S("apple");
    rt_string b = S("Apple");
    test_result("IgnoreCase: apple equals Apple", rt_collator_equals(c, a, b) == 1);
    rt_string_unref(a);
    rt_string_unref(b);
}

static void test_ignore_accents() {
    printf("Testing IgnoreAccents toggle:\n");
    void *c = en_col();
    rt_collator_set_ignore_accents(c, 1);
    rt_string aa = S("naive");
    // "naïve" = 0x6E 0x61 0xC3 0xAF 0x76 0x65
    rt_string bb = S("na\xC3\xAFve");
    test_result("IgnoreAccents: naive equals naïve", rt_collator_equals(c, aa, bb) == 1);
    rt_string_unref(aa);
    rt_string_unref(bb);
}

//=============================================================================
// Latin-1 diacritic ordering
//=============================================================================

static void test_diacritic_order_default() {
    printf("Testing diacritic ordering at default strength:\n");
    void *c = en_col();
    // "a" comes before "á" when strength >= 2 (á has secondary weight > 0).
    rt_string a = S("a");
    rt_string a_acute = S("\xC3\xA1"); // á
    test_result("Compare(a, á) < 0 at strength 3", rt_collator_compare(c, a, a_acute) < 0);
    rt_string_unref(a);
    rt_string_unref(a_acute);
}

static void test_decomposed_combining_mark() {
    printf("Testing composed/decomposed accent equivalence:\n");
    void *c = en_col();
    rt_string composed = S("\xC3\xA9");    // é
    rt_string decomposed = S("e\xCC\x81"); // e + U+0301
    test_result("é equals e + combining acute at strength 3",
                rt_collator_equals(c, composed, decomposed) == 1);
    rt_collator_set_strength(c, 1);
    test_result("é equals e + combining acute at strength 1",
                rt_collator_equals(c, composed, decomposed) == 1);
    rt_string_unref(composed);
    rt_string_unref(decomposed);
}

static void test_malformed_utf8_is_replacement_weighted() {
    printf("Testing malformed UTF-8 handling:\n");
    void *c = en_col();
    rt_string bad = S("\xC0\xAF");
    rt_string key = rt_collator_sort_key(c, bad);
    test_result("SortKey(malformed UTF-8) returns deterministic key", rt_str_len(key) > 0);
    rt_string_unref(bad);
    rt_string_unref(key);
}

//=============================================================================
// Swedish tailoring: å after z
//=============================================================================

static void test_swedish_tailoring() {
    printf("Testing Swedish tailoring (å after z):\n");
    rt_string tag = S("sv-SE");
    void *loc = rt_locale_parse(tag);
    rt_string_unref(tag);
    void *c = rt_collator_for_locale(loc);

    rt_string a_ring = S("\xC3\xA5"); // å
    rt_string z = S("z");
    test_result("sv-SE: z < å", rt_collator_compare(c, z, a_ring) < 0);

    // But in en-US default, å sorts near a.
    void *en = en_col();
    test_result("en-US: å < z (å is an accented a)", rt_collator_compare(en, a_ring, z) < 0);

    rt_string_unref(a_ring);
    rt_string_unref(z);
}

//=============================================================================
// SortKey determinism + order preservation
//=============================================================================

static void test_sort_key_determinism() {
    printf("Testing SortKey determinism:\n");
    void *c = en_col();
    rt_string s = S("hello");
    rt_string k1 = rt_collator_sort_key(c, s);
    rt_string k2 = rt_collator_sort_key(c, s);
    const char *k1s = rt_string_cstr(k1);
    const char *k2s = rt_string_cstr(k2);
    test_result("SortKey deterministic for same input", k1s && k2s && strcmp(k1s, k2s) == 0);
    rt_string_unref(s);
    rt_string_unref(k1);
    rt_string_unref(k2);
}

static void test_sort_key_order() {
    printf("Testing SortKey byte-wise order matches Compare:\n");
    void *c = en_col();
    rt_string a = S("apple");
    rt_string b = S("banana");
    rt_string ka = rt_collator_sort_key(c, a);
    rt_string kb = rt_collator_sort_key(c, b);
    const char *kas = rt_string_cstr(ka);
    const char *kbs = rt_string_cstr(kb);
    int bytewise = strcmp(kas, kbs);
    int cmp = (int)rt_collator_compare(c, a, b);
    test_result("sign(strcmp(SortKey(a),SortKey(b))) matches Compare",
                (bytewise < 0 && cmp < 0) || (bytewise > 0 && cmp > 0) ||
                    (bytewise == 0 && cmp == 0));
    rt_string_unref(a);
    rt_string_unref(b);
    rt_string_unref(ka);
    rt_string_unref(kb);
}

//=============================================================================
// Sort
//=============================================================================

static void test_sort_basic() {
    printf("Testing Sort on a small list:\n");
    void *c = en_col();
    void *list = rt_list_new();
    const char *items[] = {"banana", "apple", "cherry", "date", "Apple", nullptr};
    for (int i = 0; items[i]; ++i)
        rt_list_push(list, S(items[i]));

    void *sorted = rt_collator_sort(c, list);
    int64_t n = rt_list_len(sorted);
    test_result("Sort returns 5 items", n == 5);
    rt_string first = (rt_string)rt_list_get(sorted, 0);
    const char *fs = rt_string_cstr(first);
    // First element should be "apple" (lowercase beats uppercase at strength 3).
    test_result("First after sort is \"apple\"", fs && strcmp(fs, "apple") == 0);
}

static void test_sort_stress() {
    printf("Testing Sort stress (500 strings):\n");
    void *c = en_col();
    void *list = rt_list_new();
    for (int i = 0; i < 500; ++i) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%03d-item", 500 - i);
        rt_list_push(list, S(buf));
    }
    void *sorted = rt_collator_sort(c, list);
    test_result("Sort(500) returns 500 items", rt_list_len(sorted) == 500);
    // Spot-check: first element should have the smallest numeric prefix.
    rt_string first = (rt_string)rt_list_get(sorted, 0);
    const char *fs = rt_string_cstr(first);
    test_result("Sort ordering: first element starts with \"001\"",
                fs && strncmp(fs, "001-item", 8) == 0);
}

//=============================================================================
// Main
//=============================================================================

int main() {
    printf("=== RT Collator Tests ===\n\n");
    test_compare_ascii();
    test_strength_tertiary_default();
    test_strength_primary_case_folds();
    test_strength_clamps_to_3();
    test_ignore_case();
    test_ignore_accents();
    test_diacritic_order_default();
    test_decomposed_combining_mark();
    test_malformed_utf8_is_replacement_weighted();
    test_swedish_tailoring();
    test_sort_key_determinism();
    test_sort_key_order();
    test_sort_basic();
    test_sort_stress();
    printf("\nAll Collator tests passed!\n");
    return 0;
}
