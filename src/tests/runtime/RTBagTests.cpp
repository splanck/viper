//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/runtime/RTBagTests.cpp
// Purpose: Validate Viper.Collections.Bag runtime functions for string sets.
// Key invariants: Bags store unique strings; set operations work correctly.
// Links: docs/viperlib.md

#include "rt_bag.h"
#include "rt_seq.h"
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
// Basic Operations Tests
//=============================================================================

static void test_bag_new_empty()
{
    printf("Testing Bag.New and empty state:\n");

    void *bag = rt_bag_new();
    test_result("New bag is not null", bag != NULL);
    test_result("New bag length is 0", rt_bag_len(bag) == 0);
    test_result("New bag is empty", rt_bag_is_empty(bag) == 1);

    printf("\n");
}

static void test_bag_put_has()
{
    printf("Testing Bag.Put and Bag.Has:\n");

    void *bag = rt_bag_new();

    // Put first element
    rt_string apple = rt_const_cstr("apple");
    int8_t was_new = rt_bag_put(bag, apple);
    test_result("Put 'apple' returns true (new)", was_new == 1);
    test_result("Bag length is 1", rt_bag_len(bag) == 1);
    test_result("Bag is not empty", rt_bag_is_empty(bag) == 0);
    test_result("Has 'apple'", rt_bag_has(bag, apple) == 1);

    // Put duplicate
    was_new = rt_bag_put(bag, apple);
    test_result("Put duplicate 'apple' returns false", was_new == 0);
    test_result("Bag length still 1", rt_bag_len(bag) == 1);

    // Put second element
    rt_string banana = rt_const_cstr("banana");
    was_new = rt_bag_put(bag, banana);
    test_result("Put 'banana' returns true (new)", was_new == 1);
    test_result("Bag length is 2", rt_bag_len(bag) == 2);
    test_result("Has 'banana'", rt_bag_has(bag, banana) == 1);

    // Check non-existent
    rt_string cherry = rt_const_cstr("cherry");
    test_result("Has 'cherry' returns false", rt_bag_has(bag, cherry) == 0);

    printf("\n");
}

static void test_bag_drop()
{
    printf("Testing Bag.Drop:\n");

    void *bag = rt_bag_new();

    rt_string a = rt_const_cstr("a");
    rt_string b = rt_const_cstr("b");
    rt_string c = rt_const_cstr("c");

    rt_bag_put(bag, a);
    rt_bag_put(bag, b);
    rt_bag_put(bag, c);
    test_result("Bag has 3 elements", rt_bag_len(bag) == 3);

    // Drop existing
    int8_t was_removed = rt_bag_drop(bag, b);
    test_result("Drop 'b' returns true", was_removed == 1);
    test_result("Bag has 2 elements", rt_bag_len(bag) == 2);
    test_result("No longer has 'b'", rt_bag_has(bag, b) == 0);
    test_result("Still has 'a'", rt_bag_has(bag, a) == 1);
    test_result("Still has 'c'", rt_bag_has(bag, c) == 1);

    // Drop non-existent
    was_removed = rt_bag_drop(bag, b);
    test_result("Drop 'b' again returns false", was_removed == 0);
    test_result("Bag still has 2 elements", rt_bag_len(bag) == 2);

    printf("\n");
}

static void test_bag_clear()
{
    printf("Testing Bag.Clear:\n");

    void *bag = rt_bag_new();

    rt_bag_put(bag, rt_const_cstr("x"));
    rt_bag_put(bag, rt_const_cstr("y"));
    rt_bag_put(bag, rt_const_cstr("z"));
    test_result("Bag has 3 elements", rt_bag_len(bag) == 3);

    rt_bag_clear(bag);
    test_result("After clear, length is 0", rt_bag_len(bag) == 0);
    test_result("After clear, is empty", rt_bag_is_empty(bag) == 1);
    test_result("After clear, 'x' not present", rt_bag_has(bag, rt_const_cstr("x")) == 0);

    printf("\n");
}

static void test_bag_items()
{
    printf("Testing Bag.Items:\n");

    void *bag = rt_bag_new();

    rt_bag_put(bag, rt_const_cstr("one"));
    rt_bag_put(bag, rt_const_cstr("two"));
    rt_bag_put(bag, rt_const_cstr("three"));

    void *items = rt_bag_items(bag);
    test_result("Items is not null", items != NULL);
    test_result("Items has 3 elements", rt_seq_len(items) == 3);

    // Verify all items are present (order may vary due to hash table)
    bool found_one = false, found_two = false, found_three = false;
    for (int64_t i = 0; i < rt_seq_len(items); i++)
    {
        rt_string s = (rt_string)rt_seq_get(items, i);
        const char *cstr = rt_string_cstr(s);
        if (strcmp(cstr, "one") == 0)
            found_one = true;
        if (strcmp(cstr, "two") == 0)
            found_two = true;
        if (strcmp(cstr, "three") == 0)
            found_three = true;
    }
    test_result("Items contains 'one'", found_one);
    test_result("Items contains 'two'", found_two);
    test_result("Items contains 'three'", found_three);

    printf("\n");
}

//=============================================================================
// Set Operations Tests
//=============================================================================

static void test_bag_merge()
{
    printf("Testing Bag.Merge (union):\n");

    void *bag1 = rt_bag_new();
    rt_bag_put(bag1, rt_const_cstr("a"));
    rt_bag_put(bag1, rt_const_cstr("b"));
    rt_bag_put(bag1, rt_const_cstr("c"));

    void *bag2 = rt_bag_new();
    rt_bag_put(bag2, rt_const_cstr("b"));
    rt_bag_put(bag2, rt_const_cstr("c"));
    rt_bag_put(bag2, rt_const_cstr("d"));

    void *merged = rt_bag_merge(bag1, bag2);
    test_result("Merged bag has 4 elements", rt_bag_len(merged) == 4);
    test_result("Merged has 'a'", rt_bag_has(merged, rt_const_cstr("a")) == 1);
    test_result("Merged has 'b'", rt_bag_has(merged, rt_const_cstr("b")) == 1);
    test_result("Merged has 'c'", rt_bag_has(merged, rt_const_cstr("c")) == 1);
    test_result("Merged has 'd'", rt_bag_has(merged, rt_const_cstr("d")) == 1);

    // Original bags unchanged
    test_result("Original bag1 still has 3", rt_bag_len(bag1) == 3);
    test_result("Original bag2 still has 3", rt_bag_len(bag2) == 3);

    printf("\n");
}

static void test_bag_common()
{
    printf("Testing Bag.Common (intersection):\n");

    void *bag1 = rt_bag_new();
    rt_bag_put(bag1, rt_const_cstr("a"));
    rt_bag_put(bag1, rt_const_cstr("b"));
    rt_bag_put(bag1, rt_const_cstr("c"));

    void *bag2 = rt_bag_new();
    rt_bag_put(bag2, rt_const_cstr("b"));
    rt_bag_put(bag2, rt_const_cstr("c"));
    rt_bag_put(bag2, rt_const_cstr("d"));

    void *common = rt_bag_common(bag1, bag2);
    test_result("Common bag has 2 elements", rt_bag_len(common) == 2);
    test_result("Common has 'b'", rt_bag_has(common, rt_const_cstr("b")) == 1);
    test_result("Common has 'c'", rt_bag_has(common, rt_const_cstr("c")) == 1);
    test_result("Common does not have 'a'", rt_bag_has(common, rt_const_cstr("a")) == 0);
    test_result("Common does not have 'd'", rt_bag_has(common, rt_const_cstr("d")) == 0);

    printf("\n");
}

static void test_bag_diff()
{
    printf("Testing Bag.Diff (difference):\n");

    void *bag1 = rt_bag_new();
    rt_bag_put(bag1, rt_const_cstr("a"));
    rt_bag_put(bag1, rt_const_cstr("b"));
    rt_bag_put(bag1, rt_const_cstr("c"));

    void *bag2 = rt_bag_new();
    rt_bag_put(bag2, rt_const_cstr("b"));
    rt_bag_put(bag2, rt_const_cstr("c"));
    rt_bag_put(bag2, rt_const_cstr("d"));

    void *diff = rt_bag_diff(bag1, bag2);
    test_result("Diff bag has 1 element", rt_bag_len(diff) == 1);
    test_result("Diff has 'a'", rt_bag_has(diff, rt_const_cstr("a")) == 1);
    test_result("Diff does not have 'b'", rt_bag_has(diff, rt_const_cstr("b")) == 0);
    test_result("Diff does not have 'c'", rt_bag_has(diff, rt_const_cstr("c")) == 0);
    test_result("Diff does not have 'd'", rt_bag_has(diff, rt_const_cstr("d")) == 0);

    // Reverse diff
    void *diff2 = rt_bag_diff(bag2, bag1);
    test_result("Reverse diff has 1 element", rt_bag_len(diff2) == 1);
    test_result("Reverse diff has 'd'", rt_bag_has(diff2, rt_const_cstr("d")) == 1);

    printf("\n");
}

static void test_bag_empty_operations()
{
    printf("Testing operations with empty bags:\n");

    void *empty1 = rt_bag_new();
    void *empty2 = rt_bag_new();

    void *bag = rt_bag_new();
    rt_bag_put(bag, rt_const_cstr("x"));

    // Merge with empty
    void *m1 = rt_bag_merge(empty1, bag);
    test_result("Merge empty+bag has 1 element", rt_bag_len(m1) == 1);

    void *m2 = rt_bag_merge(bag, empty1);
    test_result("Merge bag+empty has 1 element", rt_bag_len(m2) == 1);

    void *m3 = rt_bag_merge(empty1, empty2);
    test_result("Merge empty+empty has 0 elements", rt_bag_len(m3) == 0);

    // Common with empty
    void *c1 = rt_bag_common(empty1, bag);
    test_result("Common empty&bag has 0 elements", rt_bag_len(c1) == 0);

    void *c2 = rt_bag_common(bag, empty1);
    test_result("Common bag&empty has 0 elements", rt_bag_len(c2) == 0);

    // Diff with empty
    void *d1 = rt_bag_diff(bag, empty1);
    test_result("Diff bag-empty has 1 element", rt_bag_len(d1) == 1);

    void *d2 = rt_bag_diff(empty1, bag);
    test_result("Diff empty-bag has 0 elements", rt_bag_len(d2) == 0);

    printf("\n");
}

//=============================================================================
// Resize Tests
//=============================================================================

static void test_bag_resize()
{
    printf("Testing Bag resize with many elements:\n");

    void *bag = rt_bag_new();

    // Add many elements to trigger resize
    char buf[32];
    for (int i = 0; i < 100; i++)
    {
        snprintf(buf, sizeof(buf), "element_%d", i);
        rt_bag_put(bag, rt_const_cstr(buf));
    }

    test_result("Bag has 100 elements", rt_bag_len(bag) == 100);

    // Verify all elements are present
    bool all_present = true;
    for (int i = 0; i < 100; i++)
    {
        snprintf(buf, sizeof(buf), "element_%d", i);
        if (!rt_bag_has(bag, rt_const_cstr(buf)))
        {
            all_present = false;
            break;
        }
    }
    test_result("All 100 elements present", all_present);

    printf("\n");
}

//=============================================================================
// Entry Point
//=============================================================================

int main()
{
    printf("=== RT Bag Tests ===\n\n");

    test_bag_new_empty();
    test_bag_put_has();
    test_bag_drop();
    test_bag_clear();
    test_bag_items();
    test_bag_merge();
    test_bag_common();
    test_bag_diff();
    test_bag_empty_operations();
    test_bag_resize();

    printf("All Bag tests passed!\n");
    return 0;
}
