//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/runtime/RTOptionTests.cpp
// Purpose: Validate Option type.
//
//===----------------------------------------------------------------------===//

#include "rt_option.h"
#include "rt_result.h"
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
// Option Tests
//=============================================================================

static void test_option_some_creation()
{
    printf("Testing Option Some creation:\n");

    // Test 1: Create Some with pointer
    {
        int dummy = 42;
        void *o = rt_option_some(&dummy);
        test_result("Some with pointer", rt_option_is_some(o) == 1);
        test_result("Some not None", rt_option_is_none(o) == 0);
        test_result("Unwrap returns value", rt_option_unwrap(o) == &dummy);
    }

    // Test 2: Create Some with string
    {
        rt_string s = rt_const_cstr("hello");
        void *o = rt_option_some_str(s);
        test_result("SomeStr is Some", rt_option_is_some(o) == 1);
        rt_string result = rt_option_unwrap_str(o);
        test_result("SomeStr unwrap matches", strcmp(rt_string_cstr(result), "hello") == 0);
    }

    // Test 3: Create Some with i64
    {
        void *o = rt_option_some_i64(12345);
        test_result("SomeI64 is Some", rt_option_is_some(o) == 1);
        test_result("SomeI64 unwrap value", rt_option_unwrap_i64(o) == 12345);
    }

    // Test 4: Create Some with f64
    {
        void *o = rt_option_some_f64(3.14159);
        test_result("SomeF64 is Some", rt_option_is_some(o) == 1);
        double val = rt_option_unwrap_f64(o);
        test_result("SomeF64 unwrap value", val > 3.14 && val < 3.15);
    }

    printf("\n");
}

static void test_option_none_creation()
{
    printf("Testing Option None creation:\n");

    // Test 1: Create None
    {
        void *o = rt_option_none();
        test_result("None is None", rt_option_is_none(o) == 1);
        test_result("None not Some", rt_option_is_some(o) == 0);
    }

    printf("\n");
}

static void test_option_unwrap_or()
{
    printf("Testing Option UnwrapOr:\n");

    // Test 1: UnwrapOr on Some returns value
    {
        int val = 42, def = 99;
        void *o = rt_option_some(&val);
        void *result = rt_option_unwrap_or(o, &def);
        test_result("UnwrapOr on Some returns value", result == &val);
    }

    // Test 2: UnwrapOr on None returns default
    {
        int def = 99;
        void *o = rt_option_none();
        void *result = rt_option_unwrap_or(o, &def);
        test_result("UnwrapOr on None returns default", result == &def);
    }

    // Test 3: UnwrapOrI64 on Some
    {
        void *o = rt_option_some_i64(100);
        test_result("UnwrapOrI64 on Some returns value", rt_option_unwrap_or_i64(o, -1) == 100);
    }

    // Test 4: UnwrapOrI64 on None
    {
        void *o = rt_option_none();
        test_result("UnwrapOrI64 on None returns default", rt_option_unwrap_or_i64(o, -1) == -1);
    }

    // Test 5: UnwrapOrStr on Some
    {
        void *o = rt_option_some_str(rt_const_cstr("hello"));
        rt_string result = rt_option_unwrap_or_str(o, rt_const_cstr("default"));
        test_result("UnwrapOrStr on Some returns value",
                    strcmp(rt_string_cstr(result), "hello") == 0);
    }

    // Test 6: UnwrapOrStr on None
    {
        void *o = rt_option_none();
        rt_string result = rt_option_unwrap_or_str(o, rt_const_cstr("default"));
        test_result("UnwrapOrStr on None returns default",
                    strcmp(rt_string_cstr(result), "default") == 0);
    }

    printf("\n");
}

static void test_option_value()
{
    printf("Testing Option Value:\n");

    // Test 1: Value on Some returns value
    {
        int val = 42;
        void *o = rt_option_some(&val);
        test_result("Value on Some returns value", rt_option_value(o) == &val);
    }

    // Test 2: Value on None returns NULL
    {
        void *o = rt_option_none();
        test_result("Value on None returns NULL", rt_option_value(o) == NULL);
    }

    printf("\n");
}

static void test_option_to_string()
{
    printf("Testing Option ToString:\n");

    // Test 1: Some pointer
    {
        void *o = rt_option_some(NULL);
        rt_string s = rt_option_to_string(o);
        test_result("Some(null) string contains Some", strstr(rt_string_cstr(s), "Some(") != NULL);
    }

    // Test 2: Some string
    {
        void *o = rt_option_some_str(rt_const_cstr("world"));
        rt_string s = rt_option_to_string(o);
        test_result("Some(string) contains value", strstr(rt_string_cstr(s), "world") != NULL);
    }

    // Test 3: Some i64
    {
        void *o = rt_option_some_i64(42);
        rt_string s = rt_option_to_string(o);
        test_result("Some(i64) contains value", strstr(rt_string_cstr(s), "42") != NULL);
    }

    // Test 4: None
    {
        void *o = rt_option_none();
        rt_string s = rt_option_to_string(o);
        test_result("None string is None", strcmp(rt_string_cstr(s), "None") == 0);
    }

    printf("\n");
}

static void test_option_equality()
{
    printf("Testing Option Equality:\n");

    // Test 1: Two Some i64 with same value
    {
        void *o1 = rt_option_some_i64(42);
        void *o2 = rt_option_some_i64(42);
        test_result("Equal Some i64", rt_option_equals(o1, o2) == 1);
    }

    // Test 2: Two Some i64 with different values
    {
        void *o1 = rt_option_some_i64(42);
        void *o2 = rt_option_some_i64(99);
        test_result("Unequal Some i64", rt_option_equals(o1, o2) == 0);
    }

    // Test 3: Some vs None
    {
        void *o1 = rt_option_some_i64(42);
        void *o2 = rt_option_none();
        test_result("Some vs None not equal", rt_option_equals(o1, o2) == 0);
    }

    // Test 4: Two None
    {
        void *o1 = rt_option_none();
        void *o2 = rt_option_none();
        test_result("Two None are equal", rt_option_equals(o1, o2) == 1);
    }

    // Test 5: Two Some strings with same value
    {
        void *o1 = rt_option_some_str(rt_const_cstr("hello"));
        void *o2 = rt_option_some_str(rt_const_cstr("hello"));
        test_result("Equal Some strings", rt_option_equals(o1, o2) == 1);
    }

    printf("\n");
}

static void test_option_conversion()
{
    printf("Testing Option Conversion:\n");

    // Test 1: Some to Ok
    {
        void *o = rt_option_some_i64(42);
        void *r = rt_option_ok_or(o, NULL);
        test_result("Some converts to Ok", rt_result_is_ok(r) == 1);
    }

    // Test 2: None to Err
    {
        int err = 99;
        void *o = rt_option_none();
        void *r = rt_option_ok_or(o, &err);
        test_result("None converts to Err", rt_result_is_err(r) == 1);
    }

    // Test 3: None to Err with string
    {
        void *o = rt_option_none();
        void *r = rt_option_ok_or_str(o, rt_const_cstr("not found"));
        test_result("None to Err with string", rt_result_is_err(r) == 1);
    }

    printf("\n");
}

static void test_option_null_handling()
{
    printf("Testing Option NULL handling:\n");

    // Test 1: IsSome on NULL
    {
        test_result("IsSome on NULL returns 0", rt_option_is_some(NULL) == 0);
    }

    // Test 2: IsNone on NULL
    {
        test_result("IsNone on NULL returns 1", rt_option_is_none(NULL) == 1);
    }

    // Test 3: UnwrapOr on NULL
    {
        int def = 99;
        void *result = rt_option_unwrap_or(NULL, &def);
        test_result("UnwrapOr on NULL returns default", result == &def);
    }

    printf("\n");
}

//=============================================================================
// Entry Point
//=============================================================================

int main()
{
    printf("=== RT Option Tests ===\n\n");

    test_option_some_creation();
    test_option_none_creation();
    test_option_unwrap_or();
    test_option_value();
    test_option_to_string();
    test_option_equality();
    test_option_conversion();
    test_option_null_handling();

    printf("All Option tests passed!\n");
    return 0;
}
