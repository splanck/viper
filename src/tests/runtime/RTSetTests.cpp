//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/runtime/RTSetTests.cpp
// Purpose: Validate Viper.Collections.Set content-aware hashing and equality.
// Key invariants: Boxed values are compared by content, not pointer identity.
// Links: docs/viperlib.md

#include "rt_box.h"
#include "rt_seq.h"
#include "rt_set.h"
#include "rt_string.h"

#include <cassert>
#include <cstdio>

/// @brief Helper to print test result.
static void test_result(const char *name, bool passed)
{
    printf("  %s: %s\n", name, passed ? "PASS" : "FAIL");
    assert(passed);
}

//=============================================================================
// Basic Operations Tests
//=============================================================================

static void test_set_new_empty()
{
    printf("Testing Set.New and empty state:\n");

    void *set = rt_set_new();
    test_result("New set is not null", set != NULL);
    test_result("New set length is 0", rt_set_len(set) == 0);
    test_result("New set is empty", rt_set_is_empty(set) == 1);

    printf("\n");
}

static void test_set_put_has_boxed_strings()
{
    printf("Testing Set.Put/Has with boxed strings (content equality):\n");

    void *set = rt_set_new();

    // Box "apple" and put it in the set
    void *apple1 = rt_box_str(rt_const_cstr("apple"));
    int8_t was_new = rt_set_put(set, apple1);
    test_result("Put boxed 'apple' returns 1 (new)", was_new == 1);
    test_result("Set length is 1", rt_set_len(set) == 1);

    // Create a DIFFERENT boxed "apple" (different pointer, same content)
    void *apple2 = rt_box_str(rt_const_cstr("apple"));
    test_result("Two boxes are different pointers", apple1 != apple2);
    test_result("Has boxed 'apple' (different box)", rt_set_has(set, apple2) == 1);

    // Put duplicate should return 0
    was_new = rt_set_put(set, apple2);
    test_result("Put duplicate boxed 'apple' returns 0", was_new == 0);
    test_result("Set length still 1", rt_set_len(set) == 1);

    // Add "banana"
    void *banana = rt_box_str(rt_const_cstr("banana"));
    was_new = rt_set_put(set, banana);
    test_result("Put boxed 'banana' returns 1 (new)", was_new == 1);
    test_result("Set length is 2", rt_set_len(set) == 2);

    // Check non-existent
    void *cherry = rt_box_str(rt_const_cstr("cherry"));
    test_result("Has boxed 'cherry' returns 0", rt_set_has(set, cherry) == 0);

    printf("\n");
}

static void test_set_put_has_boxed_integers()
{
    printf("Testing Set.Put/Has with boxed integers:\n");

    void *set = rt_set_new();

    void *i42a = rt_box_i64(42);
    void *i42b = rt_box_i64(42);
    void *i99 = rt_box_i64(99);

    rt_set_put(set, i42a);
    test_result("Set length is 1", rt_set_len(set) == 1);
    test_result("Has boxed 42 (different box)", rt_set_has(set, i42b) == 1);

    int8_t was_new = rt_set_put(set, i42b);
    test_result("Put duplicate 42 returns 0", was_new == 0);
    test_result("Set length still 1", rt_set_len(set) == 1);

    rt_set_put(set, i99);
    test_result("Set length is 2", rt_set_len(set) == 2);
    test_result("Has boxed 99", rt_set_has(set, rt_box_i64(99)) == 1);
    test_result("Does not have 100", rt_set_has(set, rt_box_i64(100)) == 0);

    printf("\n");
}

static void test_set_put_has_boxed_floats()
{
    printf("Testing Set.Put/Has with boxed floats:\n");

    void *set = rt_set_new();

    void *f1 = rt_box_f64(3.14);
    void *f2 = rt_box_f64(3.14);
    void *f3 = rt_box_f64(2.71);

    rt_set_put(set, f1);
    test_result("Has boxed 3.14 (different box)", rt_set_has(set, f2) == 1);
    test_result("Does not have 2.71", rt_set_has(set, f3) == 0);

    rt_set_put(set, f3);
    test_result("Set length is 2", rt_set_len(set) == 2);

    printf("\n");
}

static void test_set_put_has_boxed_booleans()
{
    printf("Testing Set.Put/Has with boxed booleans:\n");

    void *set = rt_set_new();

    void *t1 = rt_box_i1(1);
    void *t2 = rt_box_i1(1);
    void *f1 = rt_box_i1(0);

    rt_set_put(set, t1);
    test_result("Has boxed true (different box)", rt_set_has(set, t2) == 1);
    test_result("Does not have false", rt_set_has(set, f1) == 0);

    rt_set_put(set, f1);
    test_result("Set length is 2", rt_set_len(set) == 2);

    printf("\n");
}

static void test_set_drop_boxed()
{
    printf("Testing Set.Drop with boxed values:\n");

    void *set = rt_set_new();

    void *a = rt_box_str(rt_const_cstr("alpha"));
    void *b = rt_box_str(rt_const_cstr("beta"));
    rt_set_put(set, a);
    rt_set_put(set, b);
    test_result("Set length is 2", rt_set_len(set) == 2);

    // Drop using a different boxed "alpha" (same content, different pointer)
    void *a2 = rt_box_str(rt_const_cstr("alpha"));
    int8_t dropped = rt_set_drop(set, a2);
    test_result("Drop boxed 'alpha' (different box) returns 1", dropped == 1);
    test_result("Set length is 1", rt_set_len(set) == 1);
    test_result("No longer has 'alpha'", rt_set_has(set, a) == 0);
    test_result("Still has 'beta'", rt_set_has(set, b) == 1);

    // Drop non-existent
    void *c = rt_box_str(rt_const_cstr("gamma"));
    dropped = rt_set_drop(set, c);
    test_result("Drop non-existent returns 0", dropped == 0);

    printf("\n");
}

static void test_set_clear()
{
    printf("Testing Set.Clear:\n");

    void *set = rt_set_new();
    rt_set_put(set, rt_box_str(rt_const_cstr("x")));
    rt_set_put(set, rt_box_str(rt_const_cstr("y")));
    test_result("Set length is 2", rt_set_len(set) == 2);

    rt_set_clear(set);
    test_result("After clear, length is 0", rt_set_len(set) == 0);
    test_result("After clear, is empty", rt_set_is_empty(set) == 1);
    test_result("After clear, has 'x' returns 0",
                rt_set_has(set, rt_box_str(rt_const_cstr("x"))) == 0);

    printf("\n");
}

//=============================================================================
// Set Operations Tests
//=============================================================================

static void test_set_merge()
{
    printf("Testing Set.Merge (union) with boxed strings:\n");

    void *s1 = rt_set_new();
    rt_set_put(s1, rt_box_str(rt_const_cstr("a")));
    rt_set_put(s1, rt_box_str(rt_const_cstr("b")));

    void *s2 = rt_set_new();
    rt_set_put(s2, rt_box_str(rt_const_cstr("b")));
    rt_set_put(s2, rt_box_str(rt_const_cstr("c")));

    void *merged = rt_set_merge(s1, s2);
    test_result("Merged has 3 elements (not 4)", rt_set_len(merged) == 3);
    test_result("Merged has 'a'", rt_set_has(merged, rt_box_str(rt_const_cstr("a"))) == 1);
    test_result("Merged has 'b'", rt_set_has(merged, rt_box_str(rt_const_cstr("b"))) == 1);
    test_result("Merged has 'c'", rt_set_has(merged, rt_box_str(rt_const_cstr("c"))) == 1);

    printf("\n");
}

static void test_set_common()
{
    printf("Testing Set.Common (intersection) with boxed strings:\n");

    void *s1 = rt_set_new();
    rt_set_put(s1, rt_box_str(rt_const_cstr("a")));
    rt_set_put(s1, rt_box_str(rt_const_cstr("b")));
    rt_set_put(s1, rt_box_str(rt_const_cstr("c")));

    void *s2 = rt_set_new();
    rt_set_put(s2, rt_box_str(rt_const_cstr("b")));
    rt_set_put(s2, rt_box_str(rt_const_cstr("c")));
    rt_set_put(s2, rt_box_str(rt_const_cstr("d")));

    void *common = rt_set_common(s1, s2);
    test_result("Common has 2 elements", rt_set_len(common) == 2);
    test_result("Common has 'b'", rt_set_has(common, rt_box_str(rt_const_cstr("b"))) == 1);
    test_result("Common has 'c'", rt_set_has(common, rt_box_str(rt_const_cstr("c"))) == 1);
    test_result("Common does not have 'a'",
                rt_set_has(common, rt_box_str(rt_const_cstr("a"))) == 0);
    test_result("Common does not have 'd'",
                rt_set_has(common, rt_box_str(rt_const_cstr("d"))) == 0);

    printf("\n");
}

static void test_set_diff()
{
    printf("Testing Set.Diff (difference) with boxed strings:\n");

    void *s1 = rt_set_new();
    rt_set_put(s1, rt_box_str(rt_const_cstr("a")));
    rt_set_put(s1, rt_box_str(rt_const_cstr("b")));
    rt_set_put(s1, rt_box_str(rt_const_cstr("c")));

    void *s2 = rt_set_new();
    rt_set_put(s2, rt_box_str(rt_const_cstr("b")));
    rt_set_put(s2, rt_box_str(rt_const_cstr("c")));
    rt_set_put(s2, rt_box_str(rt_const_cstr("d")));

    void *diff = rt_set_diff(s1, s2);
    test_result("Diff has 1 element", rt_set_len(diff) == 1);
    test_result("Diff has 'a'", rt_set_has(diff, rt_box_str(rt_const_cstr("a"))) == 1);
    test_result("Diff does not have 'b'", rt_set_has(diff, rt_box_str(rt_const_cstr("b"))) == 0);

    // Reverse diff
    void *diff2 = rt_set_diff(s2, s1);
    test_result("Reverse diff has 1 element", rt_set_len(diff2) == 1);
    test_result("Reverse diff has 'd'", rt_set_has(diff2, rt_box_str(rt_const_cstr("d"))) == 1);

    printf("\n");
}

static void test_set_subset_superset()
{
    printf("Testing Set.IsSubset/IsSuperset with boxed strings:\n");

    void *small = rt_set_new();
    rt_set_put(small, rt_box_str(rt_const_cstr("a")));
    rt_set_put(small, rt_box_str(rt_const_cstr("b")));

    void *large = rt_set_new();
    rt_set_put(large, rt_box_str(rt_const_cstr("a")));
    rt_set_put(large, rt_box_str(rt_const_cstr("b")));
    rt_set_put(large, rt_box_str(rt_const_cstr("c")));

    test_result("small is subset of large", rt_set_is_subset(small, large) == 1);
    test_result("large is not subset of small", rt_set_is_subset(large, small) == 0);
    test_result("large is superset of small", rt_set_is_superset(large, small) == 1);
    test_result("small is not superset of large", rt_set_is_superset(small, large) == 0);

    printf("\n");
}

static void test_set_disjoint()
{
    printf("Testing Set.IsDisjoint with boxed strings:\n");

    void *s1 = rt_set_new();
    rt_set_put(s1, rt_box_str(rt_const_cstr("a")));
    rt_set_put(s1, rt_box_str(rt_const_cstr("b")));

    void *s2 = rt_set_new();
    rt_set_put(s2, rt_box_str(rt_const_cstr("c")));
    rt_set_put(s2, rt_box_str(rt_const_cstr("d")));

    void *s3 = rt_set_new();
    rt_set_put(s3, rt_box_str(rt_const_cstr("b")));
    rt_set_put(s3, rt_box_str(rt_const_cstr("c")));

    test_result("s1 and s2 are disjoint", rt_set_is_disjoint(s1, s2) == 1);
    test_result("s1 and s3 are not disjoint", rt_set_is_disjoint(s1, s3) == 0);

    printf("\n");
}

//=============================================================================
// Resize Tests
//=============================================================================

static void test_set_resize()
{
    printf("Testing Set resize with many boxed elements:\n");

    void *set = rt_set_new();

    // Add many elements to trigger resize (initial capacity = 16)
    // Use rt_string_from_bytes (copies data) since rt_const_cstr stores pointer to original
    char buf[32];
    for (int i = 0; i < 100; i++)
    {
        int n = snprintf(buf, sizeof(buf), "element_%d", i);
        rt_set_put(set, rt_box_str(rt_string_from_bytes(buf, (size_t)n)));
    }

    test_result("Set has 100 elements", rt_set_len(set) == 100);

    // Verify all elements are present using DIFFERENT box objects
    bool all_present = true;
    for (int i = 0; i < 100; i++)
    {
        int n = snprintf(buf, sizeof(buf), "element_%d", i);
        if (!rt_set_has(set, rt_box_str(rt_string_from_bytes(buf, (size_t)n))))
        {
            printf("    Missing: %s\n", buf);
            all_present = false;
            break;
        }
    }
    test_result("All 100 elements present (content lookup)", all_present);

    printf("\n");
}

//=============================================================================
// Mixed Type Tests
//=============================================================================

static void test_set_mixed_box_types()
{
    printf("Testing Set with mixed boxed types:\n");

    void *set = rt_set_new();

    // Add different boxed types
    rt_set_put(set, rt_box_i64(42));
    rt_set_put(set, rt_box_f64(3.14));
    rt_set_put(set, rt_box_str(rt_const_cstr("hello")));
    rt_set_put(set, rt_box_i1(1));

    test_result("Set has 4 elements", rt_set_len(set) == 4);
    test_result("Has boxed 42", rt_set_has(set, rt_box_i64(42)) == 1);
    test_result("Has boxed 3.14", rt_set_has(set, rt_box_f64(3.14)) == 1);
    test_result("Has boxed 'hello'", rt_set_has(set, rt_box_str(rt_const_cstr("hello"))) == 1);
    test_result("Has boxed true", rt_set_has(set, rt_box_i1(1)) == 1);

    // Different types with same numeric value should NOT match
    test_result("Boxed i64(0) != boxed bool(false)",
                rt_set_has(set, rt_box_i64(0)) == 0); // 0 as i64 != false as i1

    printf("\n");
}

static void test_set_items()
{
    printf("Testing Set.Items:\n");

    void *set = rt_set_new();
    rt_set_put(set, rt_box_str(rt_const_cstr("x")));
    rt_set_put(set, rt_box_str(rt_const_cstr("y")));
    rt_set_put(set, rt_box_str(rt_const_cstr("z")));

    void *items = rt_set_items(set);
    test_result("Items seq has 3 elements", rt_seq_len(items) == 3);

    printf("\n");
}

//=============================================================================
// Entry Point
//=============================================================================

int main()
{
    printf("=== RT Set Tests ===\n\n");

    test_set_new_empty();
    test_set_put_has_boxed_strings();
    test_set_put_has_boxed_integers();
    test_set_put_has_boxed_floats();
    test_set_put_has_boxed_booleans();
    test_set_drop_boxed();
    test_set_clear();
    test_set_merge();
    test_set_common();
    test_set_diff();
    test_set_subset_superset();
    test_set_disjoint();
    test_set_resize();
    test_set_mixed_box_types();
    test_set_items();

    printf("All Set tests passed!\n");
    return 0;
}
