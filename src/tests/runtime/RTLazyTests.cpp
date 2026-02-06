//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/runtime/RTLazyTests.cpp
// Purpose: Validate Lazy type.
//
//===----------------------------------------------------------------------===//

#include "rt_lazy.h"
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

// Counter to track supplier calls
static int supplier_call_count = 0;

// Test supplier function
static void *test_supplier(void)
{
    supplier_call_count++;
    static int value = 42;
    return &value;
}

//=============================================================================
// Lazy Tests
//=============================================================================

static void test_lazy_of()
{
    printf("Testing Lazy Of:\n");

    // Test 1: Create with value
    {
        int value = 99;
        void *l = rt_lazy_of(&value);
        test_result("Lazy of value created", l != NULL);
        test_result("Is already evaluated", rt_lazy_is_evaluated(l) == 1);
        test_result("Get returns value", rt_lazy_get(l) == &value);
    }

    // Test 2: Create with string
    {
        rt_string s = rt_const_cstr("hello");
        void *l = rt_lazy_of_str(s);
        test_result("Lazy of string", rt_lazy_is_evaluated(l) == 1);
        rt_string result = rt_lazy_get_str(l);
        test_result("Get string returns value", strcmp(rt_string_cstr(result), "hello") == 0);
    }

    // Test 3: Create with i64
    {
        void *l = rt_lazy_of_i64(12345);
        test_result("Lazy of i64", rt_lazy_is_evaluated(l) == 1);
        test_result("Get i64 returns value", rt_lazy_get_i64(l) == 12345);
    }

    printf("\n");
}

static void test_lazy_new()
{
    printf("Testing Lazy New:\n");

    // Test 1: Create with supplier (not yet evaluated)
    {
        supplier_call_count = 0;
        void *l = rt_lazy_new(test_supplier);
        test_result("Lazy new created", l != NULL);
        test_result("Not yet evaluated", rt_lazy_is_evaluated(l) == 0);
        test_result("Supplier not called yet", supplier_call_count == 0);
    }

    // Test 2: First get triggers evaluation
    {
        supplier_call_count = 0;
        void *l = rt_lazy_new(test_supplier);

        void *result = rt_lazy_get(l);
        test_result("Get returns value", result != NULL);
        test_result("Supplier called once", supplier_call_count == 1);
        test_result("Now evaluated", rt_lazy_is_evaluated(l) == 1);
    }

    // Test 3: Second get doesn't re-evaluate
    {
        supplier_call_count = 0;
        void *l = rt_lazy_new(test_supplier);

        rt_lazy_get(l); // First get
        rt_lazy_get(l); // Second get
        test_result("Supplier called only once", supplier_call_count == 1);
    }

    printf("\n");
}

static void test_lazy_force()
{
    printf("Testing Lazy Force:\n");

    // Test: Force evaluates without returning
    {
        supplier_call_count = 0;
        void *l = rt_lazy_new(test_supplier);

        test_result("Not evaluated before force", rt_lazy_is_evaluated(l) == 0);
        rt_lazy_force(l);
        test_result("Evaluated after force", rt_lazy_is_evaluated(l) == 1);
        test_result("Supplier was called", supplier_call_count == 1);
    }

    printf("\n");
}

// Helper for map test
static void *map_double_value(void *v)
{
    static int result;
    result = *(int *)v * 2;
    return &result;
}

static void test_lazy_map()
{
    printf("Testing Lazy Map:\n");

    // Test: Map already evaluated lazy
    {
        int value = 21;
        void *l = rt_lazy_of(&value);
        void *mapped = rt_lazy_map(l, map_double_value);

        test_result("Mapped lazy created", mapped != NULL);
        int *result = (int *)rt_lazy_get(mapped);
        test_result("Mapped value is doubled", *result == 42);
    }

    printf("\n");
}

static void test_lazy_null_handling()
{
    printf("Testing Lazy NULL handling:\n");

    test_result("Get NULL returns NULL", rt_lazy_get(NULL) == NULL);
    test_result("IsEvaluated NULL returns 1", rt_lazy_is_evaluated(NULL) == 1);
    test_result("GetStr NULL returns empty",
                strcmp(rt_string_cstr(rt_lazy_get_str(NULL)), "") == 0);
    test_result("GetI64 NULL returns 0", rt_lazy_get_i64(NULL) == 0);

    printf("\n");
}

//=============================================================================
// Entry Point
//=============================================================================

int main()
{
    printf("=== RT Lazy Tests ===\n\n");

    test_lazy_of();
    test_lazy_new();
    test_lazy_force();
    test_lazy_map();
    test_lazy_null_handling();

    printf("All Lazy tests passed!\n");
    return 0;
}
