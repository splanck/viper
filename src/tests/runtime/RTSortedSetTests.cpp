//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/runtime/RTSortedSetTests.cpp
// Purpose: Validate SortedSet type.
//
//===----------------------------------------------------------------------===//

#include "rt_seq.h"
#include "rt_sortedset.h"
#include "rt_string.h"

#include <cassert>
#include <cstdio>
#include <cstring>

/// @brief Helper to print test result.
static void test_result(const char *name, bool passed) {
    printf("  %s: %s\n", name, passed ? "PASS" : "FAIL");
    assert(passed);
}

static rt_string make_bytes(const char *s, size_t len) {
    return rt_string_from_bytes(s, len);
}

//=============================================================================
// Basic Tests
//=============================================================================

static void test_sortedset_new() {
    printf("Testing SortedSet New:\n");

    void *set = rt_sortedset_new();
    test_result("Set created", set != NULL);
    test_result("Initially empty", rt_sortedset_is_empty(set) == 1);
    test_result("Length is 0", rt_sortedset_len(set) == 0);

    printf("\n");
}

static void test_sortedset_add_has() {
    printf("Testing SortedSet Add/Has:\n");

    void *set = rt_sortedset_new();

    rt_string apple = rt_const_cstr("apple");
    rt_string banana = rt_const_cstr("banana");
    rt_string cherry = rt_const_cstr("cherry");

    // Add elements
    test_result("Put apple returns 1 (new)", rt_sortedset_add(set, apple) == 1);
    test_result("Put cherry returns 1 (new)", rt_sortedset_add(set, cherry) == 1);
    test_result("Put banana returns 1 (new)", rt_sortedset_add(set, banana) == 1);

    // Duplicate
    test_result("Put apple again returns 0 (exists)", rt_sortedset_add(set, apple) == 0);

    test_result("Has apple", rt_sortedset_has(set, apple) == 1);
    test_result("Has banana", rt_sortedset_has(set, banana) == 1);
    test_result("Has cherry", rt_sortedset_has(set, cherry) == 1);
    test_result("Does not have date", rt_sortedset_has(set, rt_const_cstr("date")) == 0);

    test_result("Length is 3", rt_sortedset_len(set) == 3);

    printf("\n");
}

static void test_sortedset_order() {
    printf("Testing SortedSet Order:\n");

    void *set = rt_sortedset_new();

    // Add in reverse order
    rt_sortedset_add(set, rt_const_cstr("zebra"));
    rt_sortedset_add(set, rt_const_cstr("apple"));
    rt_sortedset_add(set, rt_const_cstr("mango"));

    // Check ordering
    rt_string first = rt_sortedset_first(set);
    rt_string last = rt_sortedset_last(set);

    test_result("First is apple", strcmp(rt_string_cstr(first), "apple") == 0);
    test_result("Last is zebra", strcmp(rt_string_cstr(last), "zebra") == 0);

    // Check at() access
    test_result("At(0) is apple", strcmp(rt_string_cstr(rt_sortedset_at(set, 0)), "apple") == 0);
    test_result("At(1) is mango", strcmp(rt_string_cstr(rt_sortedset_at(set, 1)), "mango") == 0);
    test_result("At(2) is zebra", strcmp(rt_string_cstr(rt_sortedset_at(set, 2)), "zebra") == 0);

    printf("\n");
}

static void test_sortedset_drop() {
    printf("Testing SortedSet Drop:\n");

    void *set = rt_sortedset_new();
    rt_sortedset_add(set, rt_const_cstr("a"));
    rt_sortedset_add(set, rt_const_cstr("b"));
    rt_sortedset_add(set, rt_const_cstr("c"));

    test_result("Length is 3", rt_sortedset_len(set) == 3);
    test_result("Drop b returns 1", rt_sortedset_remove(set, rt_const_cstr("b")) == 1);
    test_result("Length is 2", rt_sortedset_len(set) == 2);
    test_result("No longer has b", rt_sortedset_has(set, rt_const_cstr("b")) == 0);
    test_result("Still has a and c", rt_sortedset_has(set, rt_const_cstr("a")) == 1);
    test_result("Drop nonexistent returns 0", rt_sortedset_remove(set, rt_const_cstr("x")) == 0);

    printf("\n");
}

//=============================================================================
// Ordered Access Tests
//=============================================================================

static void test_sortedset_floor_ceil() {
    printf("Testing SortedSet Floor/Ceil:\n");

    void *set = rt_sortedset_new();
    rt_sortedset_add(set, rt_const_cstr("b"));
    rt_sortedset_add(set, rt_const_cstr("d"));
    rt_sortedset_add(set, rt_const_cstr("f"));

    // Floor: greatest element <= given
    test_result("Floor(d) is d",
                strcmp(rt_string_cstr(rt_sortedset_floor(set, rt_const_cstr("d"))), "d") == 0);
    test_result("Floor(c) is b",
                strcmp(rt_string_cstr(rt_sortedset_floor(set, rt_const_cstr("c"))), "b") == 0);
    test_result("Floor(a) is empty",
                strlen(rt_string_cstr(rt_sortedset_floor(set, rt_const_cstr("a")))) == 0);

    // Ceil: least element >= given
    test_result("Ceil(d) is d",
                strcmp(rt_string_cstr(rt_sortedset_ceil(set, rt_const_cstr("d"))), "d") == 0);
    test_result("Ceil(c) is d",
                strcmp(rt_string_cstr(rt_sortedset_ceil(set, rt_const_cstr("c"))), "d") == 0);
    test_result("Ceil(g) is empty",
                strlen(rt_string_cstr(rt_sortedset_ceil(set, rt_const_cstr("g")))) == 0);

    printf("\n");
}

static void test_sortedset_lower_higher() {
    printf("Testing SortedSet Lower/Higher:\n");

    void *set = rt_sortedset_new();
    rt_sortedset_add(set, rt_const_cstr("b"));
    rt_sortedset_add(set, rt_const_cstr("d"));
    rt_sortedset_add(set, rt_const_cstr("f"));

    // Lower: greatest element < given (strictly)
    test_result("Lower(d) is b",
                strcmp(rt_string_cstr(rt_sortedset_lower(set, rt_const_cstr("d"))), "b") == 0);
    test_result("Lower(e) is d",
                strcmp(rt_string_cstr(rt_sortedset_lower(set, rt_const_cstr("e"))), "d") == 0);
    test_result("Lower(b) is empty",
                strlen(rt_string_cstr(rt_sortedset_lower(set, rt_const_cstr("b")))) == 0);

    // Higher: least element > given (strictly)
    test_result("Higher(d) is f",
                strcmp(rt_string_cstr(rt_sortedset_higher(set, rt_const_cstr("d"))), "f") == 0);
    test_result("Higher(c) is d",
                strcmp(rt_string_cstr(rt_sortedset_higher(set, rt_const_cstr("c"))), "d") == 0);
    test_result("Higher(f) is empty",
                strlen(rt_string_cstr(rt_sortedset_higher(set, rt_const_cstr("f")))) == 0);

    printf("\n");
}

//=============================================================================
// Range Operations Tests
//=============================================================================

static void test_sortedset_items() {
    printf("Testing SortedSet Items:\n");

    void *set = rt_sortedset_new();
    rt_sortedset_add(set, rt_const_cstr("c"));
    rt_sortedset_add(set, rt_const_cstr("a"));
    rt_sortedset_add(set, rt_const_cstr("b"));

    void *items = rt_sortedset_items(set);
    test_result("Items returns Seq", items != NULL);
    test_result("Seq has 3 elements", rt_seq_len(items) == 3);

    // Items should be in sorted order
    rt_string s0 = (rt_string)rt_seq_get(items, 0);
    rt_string s1 = (rt_string)rt_seq_get(items, 1);
    rt_string s2 = (rt_string)rt_seq_get(items, 2);

    test_result("First item is a", strcmp(rt_string_cstr(s0), "a") == 0);
    test_result("Second item is b", strcmp(rt_string_cstr(s1), "b") == 0);
    test_result("Third item is c", strcmp(rt_string_cstr(s2), "c") == 0);

    printf("\n");
}

static void test_sortedset_embedded_nul_items_are_distinct() {
    printf("Testing SortedSet embedded NUL items:\n");

    void *set = rt_sortedset_new();
    const char bytes[] = {'a', '\0', 'b'};
    rt_string k1 = make_bytes(bytes, sizeof(bytes));
    rt_string k2 = rt_const_cstr("a");

    test_result("Add embedded-NUL item", rt_sortedset_add(set, k1) == 1);
    test_result("Add prefix item", rt_sortedset_add(set, k2) == 1);
    test_result("Set keeps both distinct items", rt_sortedset_len(set) == 2);
    test_result("Has embedded-NUL item", rt_sortedset_has(set, k1) == 1);
    test_result("Has prefix item", rt_sortedset_has(set, k2) == 1);
    test_result("Prefix sorts before longer item", rt_str_len(rt_sortedset_at(set, 0)) == 1);
    test_result("Longer item sorts after prefix", rt_str_len(rt_sortedset_at(set, 1)) == 3);

    rt_string_unref(k1);
    printf("\n");
}

static void test_sortedset_range_inclusive_and_open_upper() {
    printf("Testing SortedSet Range bounds:\n");

    void *set = rt_sortedset_new();
    rt_sortedset_add(set, rt_const_cstr("a"));
    rt_sortedset_add(set, rt_const_cstr("b"));
    rt_sortedset_add(set, rt_const_cstr("c"));
    rt_sortedset_add(set, rt_const_cstr("d"));

    void *inclusive = rt_sortedset_range(set, rt_const_cstr("b"), rt_const_cstr("d"));
    test_result("Range includes both bounds", rt_seq_len(inclusive) == 3);
    test_result("Range starts at b",
                strcmp(rt_string_cstr((rt_string)rt_seq_get(inclusive, 0)), "b") == 0);
    test_result("Range includes upper bound d",
                strcmp(rt_string_cstr((rt_string)rt_seq_get(inclusive, 2)), "d") == 0);

    void *open_upper = rt_sortedset_range(set, rt_const_cstr("c"), NULL);
    test_result("NULL upper bound is open-ended", rt_seq_len(open_upper) == 2);
    test_result("Open range starts at c",
                strcmp(rt_string_cstr((rt_string)rt_seq_get(open_upper, 0)), "c") == 0);
    test_result("Open range includes final item",
                strcmp(rt_string_cstr((rt_string)rt_seq_get(open_upper, 1)), "d") == 0);

    printf("\n");
}

static void test_sortedset_sequences_are_independent_snapshots() {
    printf("Testing SortedSet snapshot ownership:\n");

    void *set = rt_sortedset_new();
    rt_sortedset_add(set, rt_const_cstr("a"));
    rt_sortedset_add(set, rt_const_cstr("b"));
    rt_sortedset_add(set, rt_const_cstr("c"));

    void *items = rt_sortedset_items(set);
    void *range = rt_sortedset_range(set, rt_const_cstr("a"), rt_const_cstr("b"));
    void *take = rt_sortedset_take(set, 2);
    void *skip = rt_sortedset_skip(set, 1);

    test_result("Items string is copied", rt_seq_get(items, 0) != rt_sortedset_at(set, 0));
    test_result("Range string is copied", rt_seq_get(range, 0) != rt_sortedset_at(set, 0));
    test_result("Take string is copied", rt_seq_get(take, 0) != rt_sortedset_at(set, 0));
    test_result("Skip string is copied", rt_seq_get(skip, 0) != rt_sortedset_at(set, 1));

    rt_sortedset_clear(set);
    test_result("Items survive clear",
                strcmp(rt_string_cstr((rt_string)rt_seq_get(items, 0)), "a") == 0);
    test_result("Range survives clear",
                strcmp(rt_string_cstr((rt_string)rt_seq_get(range, 1)), "b") == 0);
    test_result("Take survives clear",
                strcmp(rt_string_cstr((rt_string)rt_seq_get(take, 1)), "b") == 0);
    test_result("Skip survives clear",
                strcmp(rt_string_cstr((rt_string)rt_seq_get(skip, 0)), "b") == 0);

    printf("\n");
}

//=============================================================================
// Set Operations Tests
//=============================================================================

static void test_sortedset_union() {
    printf("Testing SortedSet Union:\n");

    void *set1 = rt_sortedset_new();
    rt_sortedset_add(set1, rt_const_cstr("a"));
    rt_sortedset_add(set1, rt_const_cstr("b"));

    void *set2 = rt_sortedset_new();
    rt_sortedset_add(set2, rt_const_cstr("b"));
    rt_sortedset_add(set2, rt_const_cstr("c"));

    void *merged = rt_sortedset_union(set1, set2);
    test_result("Union set has 3 elements", rt_sortedset_len(merged) == 3);
    test_result("Union has a", rt_sortedset_has(merged, rt_const_cstr("a")) == 1);
    test_result("Union has b", rt_sortedset_has(merged, rt_const_cstr("b")) == 1);
    test_result("Union has c", rt_sortedset_has(merged, rt_const_cstr("c")) == 1);

    printf("\n");
}

static void test_sortedset_intersect() {
    printf("Testing SortedSet Intersect:\n");

    void *set1 = rt_sortedset_new();
    rt_sortedset_add(set1, rt_const_cstr("a"));
    rt_sortedset_add(set1, rt_const_cstr("b"));
    rt_sortedset_add(set1, rt_const_cstr("c"));

    void *set2 = rt_sortedset_new();
    rt_sortedset_add(set2, rt_const_cstr("b"));
    rt_sortedset_add(set2, rt_const_cstr("c"));
    rt_sortedset_add(set2, rt_const_cstr("d"));

    void *inter = rt_sortedset_intersect(set1, set2);
    test_result("Intersect set has 2 elements", rt_sortedset_len(inter) == 2);
    test_result("Intersect has b", rt_sortedset_has(inter, rt_const_cstr("b")) == 1);
    test_result("Intersect has c", rt_sortedset_has(inter, rt_const_cstr("c")) == 1);
    test_result("Intersect does not have a", rt_sortedset_has(inter, rt_const_cstr("a")) == 0);

    printf("\n");
}

static void test_sortedset_diff() {
    printf("Testing SortedSet Diff:\n");

    void *set1 = rt_sortedset_new();
    rt_sortedset_add(set1, rt_const_cstr("a"));
    rt_sortedset_add(set1, rt_const_cstr("b"));
    rt_sortedset_add(set1, rt_const_cstr("c"));

    void *set2 = rt_sortedset_new();
    rt_sortedset_add(set2, rt_const_cstr("b"));

    void *diff = rt_sortedset_diff(set1, set2);
    test_result("Diff set has 2 elements", rt_sortedset_len(diff) == 2);
    test_result("Diff has a", rt_sortedset_has(diff, rt_const_cstr("a")) == 1);
    test_result("Diff has c", rt_sortedset_has(diff, rt_const_cstr("c")) == 1);
    test_result("Diff does not have b", rt_sortedset_has(diff, rt_const_cstr("b")) == 0);

    printf("\n");
}

static void test_sortedset_is_subset() {
    printf("Testing SortedSet IsSubset:\n");

    void *set1 = rt_sortedset_new();
    rt_sortedset_add(set1, rt_const_cstr("a"));
    rt_sortedset_add(set1, rt_const_cstr("b"));

    void *set2 = rt_sortedset_new();
    rt_sortedset_add(set2, rt_const_cstr("a"));
    rt_sortedset_add(set2, rt_const_cstr("b"));
    rt_sortedset_add(set2, rt_const_cstr("c"));

    void *set3 = rt_sortedset_new();
    rt_sortedset_add(set3, rt_const_cstr("x"));

    test_result("set1 is subset of set2", rt_sortedset_is_subset(set1, set2) == 1);
    test_result("set2 is not subset of set1", rt_sortedset_is_subset(set2, set1) == 0);
    test_result("set1 is not subset of set3", rt_sortedset_is_subset(set1, set3) == 0);
    test_result("Empty is subset of anything",
                rt_sortedset_is_subset(rt_sortedset_new(), set1) == 1);

    printf("\n");
}

//=============================================================================
// NULL Handling Tests
//=============================================================================

static void test_sortedset_null_handling() {
    printf("Testing SortedSet NULL handling:\n");

    test_result("Len(NULL) returns 0", rt_sortedset_len(NULL) == 0);
    test_result("IsEmpty(NULL) returns 1", rt_sortedset_is_empty(NULL) == 1);
    test_result("Put(NULL) returns 0", rt_sortedset_add(NULL, rt_const_cstr("x")) == 0);
    test_result("Has(NULL) returns 0", rt_sortedset_has(NULL, rt_const_cstr("x")) == 0);

    printf("\n");
}

//=============================================================================
// Entry Point
//=============================================================================

int main() {
    printf("=== RT SortedSet Tests ===\n\n");

    test_sortedset_new();
    test_sortedset_add_has();
    test_sortedset_order();
    test_sortedset_drop();
    test_sortedset_floor_ceil();
    test_sortedset_lower_higher();
    test_sortedset_items();
    test_sortedset_embedded_nul_items_are_distinct();
    test_sortedset_range_inclusive_and_open_upper();
    test_sortedset_sequences_are_independent_snapshots();
    test_sortedset_union();
    test_sortedset_intersect();
    test_sortedset_diff();
    test_sortedset_is_subset();
    test_sortedset_null_handling();

    printf("All SortedSet tests passed!\n");
    return 0;
}
