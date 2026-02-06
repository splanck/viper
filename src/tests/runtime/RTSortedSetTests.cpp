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
static void test_result(const char *name, bool passed)
{
    printf("  %s: %s\n", name, passed ? "PASS" : "FAIL");
    assert(passed);
}

//=============================================================================
// Basic Tests
//=============================================================================

static void test_sortedset_new()
{
    printf("Testing SortedSet New:\n");

    void *set = rt_sortedset_new();
    test_result("Set created", set != NULL);
    test_result("Initially empty", rt_sortedset_is_empty(set) == 1);
    test_result("Length is 0", rt_sortedset_len(set) == 0);

    printf("\n");
}

static void test_sortedset_put_has()
{
    printf("Testing SortedSet Put/Has:\n");

    void *set = rt_sortedset_new();

    rt_string apple = rt_const_cstr("apple");
    rt_string banana = rt_const_cstr("banana");
    rt_string cherry = rt_const_cstr("cherry");

    // Add elements
    test_result("Put apple returns 1 (new)", rt_sortedset_put(set, apple) == 1);
    test_result("Put cherry returns 1 (new)", rt_sortedset_put(set, cherry) == 1);
    test_result("Put banana returns 1 (new)", rt_sortedset_put(set, banana) == 1);

    // Duplicate
    test_result("Put apple again returns 0 (exists)", rt_sortedset_put(set, apple) == 0);

    test_result("Has apple", rt_sortedset_has(set, apple) == 1);
    test_result("Has banana", rt_sortedset_has(set, banana) == 1);
    test_result("Has cherry", rt_sortedset_has(set, cherry) == 1);
    test_result("Does not have date", rt_sortedset_has(set, rt_const_cstr("date")) == 0);

    test_result("Length is 3", rt_sortedset_len(set) == 3);

    printf("\n");
}

static void test_sortedset_order()
{
    printf("Testing SortedSet Order:\n");

    void *set = rt_sortedset_new();

    // Add in reverse order
    rt_sortedset_put(set, rt_const_cstr("zebra"));
    rt_sortedset_put(set, rt_const_cstr("apple"));
    rt_sortedset_put(set, rt_const_cstr("mango"));

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

static void test_sortedset_drop()
{
    printf("Testing SortedSet Drop:\n");

    void *set = rt_sortedset_new();
    rt_sortedset_put(set, rt_const_cstr("a"));
    rt_sortedset_put(set, rt_const_cstr("b"));
    rt_sortedset_put(set, rt_const_cstr("c"));

    test_result("Length is 3", rt_sortedset_len(set) == 3);
    test_result("Drop b returns 1", rt_sortedset_drop(set, rt_const_cstr("b")) == 1);
    test_result("Length is 2", rt_sortedset_len(set) == 2);
    test_result("No longer has b", rt_sortedset_has(set, rt_const_cstr("b")) == 0);
    test_result("Still has a and c", rt_sortedset_has(set, rt_const_cstr("a")) == 1);
    test_result("Drop nonexistent returns 0", rt_sortedset_drop(set, rt_const_cstr("x")) == 0);

    printf("\n");
}

//=============================================================================
// Ordered Access Tests
//=============================================================================

static void test_sortedset_floor_ceil()
{
    printf("Testing SortedSet Floor/Ceil:\n");

    void *set = rt_sortedset_new();
    rt_sortedset_put(set, rt_const_cstr("b"));
    rt_sortedset_put(set, rt_const_cstr("d"));
    rt_sortedset_put(set, rt_const_cstr("f"));

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

static void test_sortedset_lower_higher()
{
    printf("Testing SortedSet Lower/Higher:\n");

    void *set = rt_sortedset_new();
    rt_sortedset_put(set, rt_const_cstr("b"));
    rt_sortedset_put(set, rt_const_cstr("d"));
    rt_sortedset_put(set, rt_const_cstr("f"));

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

static void test_sortedset_items()
{
    printf("Testing SortedSet Items:\n");

    void *set = rt_sortedset_new();
    rt_sortedset_put(set, rt_const_cstr("c"));
    rt_sortedset_put(set, rt_const_cstr("a"));
    rt_sortedset_put(set, rt_const_cstr("b"));

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

//=============================================================================
// Set Operations Tests
//=============================================================================

static void test_sortedset_merge()
{
    printf("Testing SortedSet Merge:\n");

    void *set1 = rt_sortedset_new();
    rt_sortedset_put(set1, rt_const_cstr("a"));
    rt_sortedset_put(set1, rt_const_cstr("b"));

    void *set2 = rt_sortedset_new();
    rt_sortedset_put(set2, rt_const_cstr("b"));
    rt_sortedset_put(set2, rt_const_cstr("c"));

    void *merged = rt_sortedset_merge(set1, set2);
    test_result("Merged set has 3 elements", rt_sortedset_len(merged) == 3);
    test_result("Merged has a", rt_sortedset_has(merged, rt_const_cstr("a")) == 1);
    test_result("Merged has b", rt_sortedset_has(merged, rt_const_cstr("b")) == 1);
    test_result("Merged has c", rt_sortedset_has(merged, rt_const_cstr("c")) == 1);

    printf("\n");
}

static void test_sortedset_common()
{
    printf("Testing SortedSet Common:\n");

    void *set1 = rt_sortedset_new();
    rt_sortedset_put(set1, rt_const_cstr("a"));
    rt_sortedset_put(set1, rt_const_cstr("b"));
    rt_sortedset_put(set1, rt_const_cstr("c"));

    void *set2 = rt_sortedset_new();
    rt_sortedset_put(set2, rt_const_cstr("b"));
    rt_sortedset_put(set2, rt_const_cstr("c"));
    rt_sortedset_put(set2, rt_const_cstr("d"));

    void *common = rt_sortedset_common(set1, set2);
    test_result("Common set has 2 elements", rt_sortedset_len(common) == 2);
    test_result("Common has b", rt_sortedset_has(common, rt_const_cstr("b")) == 1);
    test_result("Common has c", rt_sortedset_has(common, rt_const_cstr("c")) == 1);
    test_result("Common does not have a", rt_sortedset_has(common, rt_const_cstr("a")) == 0);

    printf("\n");
}

static void test_sortedset_diff()
{
    printf("Testing SortedSet Diff:\n");

    void *set1 = rt_sortedset_new();
    rt_sortedset_put(set1, rt_const_cstr("a"));
    rt_sortedset_put(set1, rt_const_cstr("b"));
    rt_sortedset_put(set1, rt_const_cstr("c"));

    void *set2 = rt_sortedset_new();
    rt_sortedset_put(set2, rt_const_cstr("b"));

    void *diff = rt_sortedset_diff(set1, set2);
    test_result("Diff set has 2 elements", rt_sortedset_len(diff) == 2);
    test_result("Diff has a", rt_sortedset_has(diff, rt_const_cstr("a")) == 1);
    test_result("Diff has c", rt_sortedset_has(diff, rt_const_cstr("c")) == 1);
    test_result("Diff does not have b", rt_sortedset_has(diff, rt_const_cstr("b")) == 0);

    printf("\n");
}

static void test_sortedset_is_subset()
{
    printf("Testing SortedSet IsSubset:\n");

    void *set1 = rt_sortedset_new();
    rt_sortedset_put(set1, rt_const_cstr("a"));
    rt_sortedset_put(set1, rt_const_cstr("b"));

    void *set2 = rt_sortedset_new();
    rt_sortedset_put(set2, rt_const_cstr("a"));
    rt_sortedset_put(set2, rt_const_cstr("b"));
    rt_sortedset_put(set2, rt_const_cstr("c"));

    void *set3 = rt_sortedset_new();
    rt_sortedset_put(set3, rt_const_cstr("x"));

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

static void test_sortedset_null_handling()
{
    printf("Testing SortedSet NULL handling:\n");

    test_result("Len(NULL) returns 0", rt_sortedset_len(NULL) == 0);
    test_result("IsEmpty(NULL) returns 1", rt_sortedset_is_empty(NULL) == 1);
    test_result("Put(NULL) returns 0", rt_sortedset_put(NULL, rt_const_cstr("x")) == 0);
    test_result("Has(NULL) returns 0", rt_sortedset_has(NULL, rt_const_cstr("x")) == 0);

    printf("\n");
}

//=============================================================================
// Entry Point
//=============================================================================

int main()
{
    printf("=== RT SortedSet Tests ===\n\n");

    test_sortedset_new();
    test_sortedset_put_has();
    test_sortedset_order();
    test_sortedset_drop();
    test_sortedset_floor_ceil();
    test_sortedset_lower_higher();
    test_sortedset_items();
    test_sortedset_merge();
    test_sortedset_common();
    test_sortedset_diff();
    test_sortedset_is_subset();
    test_sortedset_null_handling();

    printf("All SortedSet tests passed!\n");
    return 0;
}
