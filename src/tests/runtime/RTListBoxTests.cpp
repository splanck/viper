//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/runtime/RTListBoxTests.cpp
// Purpose: Validate List.Find/Has/Remove content-aware equality for boxed values.
// Key invariants: Boxed values are compared by content, not pointer identity.

#include "rt_box.h"
#include "rt_list.h"
#include "rt_string.h"

#include <cassert>
#include <cstdio>

static void test_result(const char *name, bool passed)
{
    printf("  %s: %s\n", name, passed ? "PASS" : "FAIL");
    assert(passed);
}

//=============================================================================
// List.Find / List.Has with boxed strings
//=============================================================================

static void test_list_find_boxed_strings()
{
    printf("Testing List.Find/Has with boxed strings:\n");

    void *list = rt_ns_list_new();

    void *apple1 = rt_box_str(rt_const_cstr("apple"));
    void *banana = rt_box_str(rt_const_cstr("banana"));
    void *cherry = rt_box_str(rt_const_cstr("cherry"));

    rt_list_push(list, apple1);
    rt_list_push(list, banana);
    rt_list_push(list, cherry);

    test_result("Count is 3", rt_list_len(list) == 3);

    // Create DIFFERENT boxed strings with same content
    void *apple2 = rt_box_str(rt_const_cstr("apple"));
    void *banana2 = rt_box_str(rt_const_cstr("banana"));

    test_result("apple1 != apple2 (different pointers)", apple1 != apple2);
    test_result("Find apple2 returns 0", rt_list_find(list, apple2) == 0);
    test_result("Find banana2 returns 1", rt_list_find(list, banana2) == 1);
    test_result("Has apple2", rt_list_has(list, apple2) == 1);
    test_result("Has banana2", rt_list_has(list, banana2) == 1);

    // Non-existent
    void *grape = rt_box_str(rt_const_cstr("grape"));
    test_result("Find grape returns -1", rt_list_find(list, grape) == -1);
    test_result("Has grape is false", rt_list_has(list, grape) == 0);

    printf("\n");
}

//=============================================================================
// List.Find / List.Has with boxed integers
//=============================================================================

static void test_list_find_boxed_integers()
{
    printf("Testing List.Find/Has with boxed integers:\n");

    void *list = rt_ns_list_new();

    void *i42a = rt_box_i64(42);
    void *i99 = rt_box_i64(99);
    void *i0 = rt_box_i64(0);

    rt_list_push(list, i42a);
    rt_list_push(list, i99);
    rt_list_push(list, i0);

    void *i42b = rt_box_i64(42);
    void *i99b = rt_box_i64(99);

    test_result("i42a != i42b (different pointers)", i42a != i42b);
    test_result("Find i42b returns 0", rt_list_find(list, i42b) == 0);
    test_result("Find i99b returns 1", rt_list_find(list, i99b) == 1);
    test_result("Has i42b", rt_list_has(list, i42b) == 1);

    void *i77 = rt_box_i64(77);
    test_result("Find i77 returns -1", rt_list_find(list, i77) == -1);

    printf("\n");
}

//=============================================================================
// List.Find / List.Has with boxed floats
//=============================================================================

static void test_list_find_boxed_floats()
{
    printf("Testing List.Find/Has with boxed floats:\n");

    void *list = rt_ns_list_new();

    void *f1a = rt_box_f64(3.14);
    void *f2 = rt_box_f64(2.718);
    rt_list_push(list, f1a);
    rt_list_push(list, f2);

    void *f1b = rt_box_f64(3.14);
    test_result("f1a != f1b (different pointers)", f1a != f1b);
    test_result("Find f1b returns 0", rt_list_find(list, f1b) == 0);
    test_result("Has f1b", rt_list_has(list, f1b) == 1);

    void *f3 = rt_box_f64(1.0);
    test_result("Find f3 returns -1", rt_list_find(list, f3) == -1);

    printf("\n");
}

//=============================================================================
// List.Remove with boxed values
//=============================================================================

static void test_list_remove_boxed()
{
    printf("Testing List.Remove with boxed values:\n");

    void *list = rt_ns_list_new();

    void *i10 = rt_box_i64(10);
    void *i20 = rt_box_i64(20);
    void *i30 = rt_box_i64(30);
    rt_list_push(list, i10);
    rt_list_push(list, i20);
    rt_list_push(list, i30);

    test_result("Count is 3", rt_list_len(list) == 3);

    // Remove by content-equal boxed value (different pointer)
    void *i20b = rt_box_i64(20);
    test_result("i20 != i20b (different pointers)", i20 != i20b);
    int8_t removed = rt_list_remove(list, i20b);
    test_result("Remove i20b returns 1", removed == 1);
    test_result("Count is 2", rt_list_len(list) == 2);

    // Verify i20 is gone
    void *i20c = rt_box_i64(20);
    test_result("Has i20c is false", rt_list_has(list, i20c) == 0);

    // Verify others remain
    void *i10b = rt_box_i64(10);
    void *i30b = rt_box_i64(30);
    test_result("Has i10b", rt_list_has(list, i10b) == 1);
    test_result("Has i30b", rt_list_has(list, i30b) == 1);

    printf("\n");
}

//=============================================================================
// Boxed booleans
//=============================================================================

static void test_list_find_boxed_booleans()
{
    printf("Testing List.Find/Has with boxed booleans:\n");

    void *list = rt_ns_list_new();

    void *btrue1 = rt_box_i1(1);
    rt_list_push(list, btrue1);

    void *btrue2 = rt_box_i1(1);
    void *bfalse = rt_box_i1(0);

    test_result("btrue1 != btrue2 (different pointers)", btrue1 != btrue2);
    test_result("Has btrue2", rt_list_has(list, btrue2) == 1);
    test_result("Has bfalse is false", rt_list_has(list, bfalse) == 0);

    printf("\n");
}

//=============================================================================
// Main
//=============================================================================

int main()
{
    printf("=== List Box Content Equality Tests ===\n\n");

    test_list_find_boxed_strings();
    test_list_find_boxed_integers();
    test_list_find_boxed_floats();
    test_list_remove_boxed();
    test_list_find_boxed_booleans();

    printf("All List box equality tests passed!\n");
    return 0;
}
