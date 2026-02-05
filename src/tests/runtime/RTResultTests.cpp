//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/runtime/RTResultTests.cpp
// Purpose: Validate Result type for error handling.
//
//===----------------------------------------------------------------------===//

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
// Result Tests
//=============================================================================

static void test_result_ok_creation()
{
    printf("Testing Result Ok creation:\n");

    // Test 1: Create Ok with pointer
    {
        int dummy = 42;
        void *r = rt_result_ok(&dummy);
        test_result("Ok with pointer", rt_result_is_ok(r) == 1);
        test_result("Ok not Err", rt_result_is_err(r) == 0);
        test_result("Unwrap returns value", rt_result_unwrap(r) == &dummy);
    }

    // Test 2: Create Ok with string
    {
        rt_string s = rt_const_cstr("success");
        void *r = rt_result_ok_str(s);
        test_result("OkStr is Ok", rt_result_is_ok(r) == 1);
        rt_string result = rt_result_unwrap_str(r);
        test_result("OkStr unwrap matches", strcmp(rt_string_cstr(result), "success") == 0);
    }

    // Test 3: Create Ok with i64
    {
        void *r = rt_result_ok_i64(12345);
        test_result("OkI64 is Ok", rt_result_is_ok(r) == 1);
        test_result("OkI64 unwrap value", rt_result_unwrap_i64(r) == 12345);
    }

    // Test 4: Create Ok with f64
    {
        void *r = rt_result_ok_f64(3.14159);
        test_result("OkF64 is Ok", rt_result_is_ok(r) == 1);
        double val = rt_result_unwrap_f64(r);
        test_result("OkF64 unwrap value", val > 3.14 && val < 3.15);
    }

    printf("\n");
}

static void test_result_err_creation()
{
    printf("Testing Result Err creation:\n");

    // Test 1: Create Err with pointer
    {
        int dummy = 99;
        void *r = rt_result_err(&dummy);
        test_result("Err with pointer", rt_result_is_err(r) == 1);
        test_result("Err not Ok", rt_result_is_ok(r) == 0);
        test_result("UnwrapErr returns value", rt_result_unwrap_err(r) == &dummy);
    }

    // Test 2: Create Err with string
    {
        rt_string s = rt_const_cstr("file not found");
        void *r = rt_result_err_str(s);
        test_result("ErrStr is Err", rt_result_is_err(r) == 1);
        rt_string result = rt_result_unwrap_err_str(r);
        test_result("ErrStr unwrap matches", strcmp(rt_string_cstr(result), "file not found") == 0);
    }

    printf("\n");
}

static void test_result_unwrap_or()
{
    printf("Testing Result UnwrapOr:\n");

    // Test 1: UnwrapOr on Ok returns value
    {
        int val = 42, def = 99;
        void *r = rt_result_ok(&val);
        void *result = rt_result_unwrap_or(r, &def);
        test_result("UnwrapOr on Ok returns value", result == &val);
    }

    // Test 2: UnwrapOr on Err returns default
    {
        int val = 42, def = 99;
        void *r = rt_result_err(&val);
        void *result = rt_result_unwrap_or(r, &def);
        test_result("UnwrapOr on Err returns default", result == &def);
    }

    // Test 3: UnwrapOrI64 on Ok
    {
        void *r = rt_result_ok_i64(100);
        test_result("UnwrapOrI64 on Ok returns value", rt_result_unwrap_or_i64(r, -1) == 100);
    }

    // Test 4: UnwrapOrI64 on Err
    {
        void *r = rt_result_err_str(rt_const_cstr("error"));
        test_result("UnwrapOrI64 on Err returns default", rt_result_unwrap_or_i64(r, -1) == -1);
    }

    // Test 5: UnwrapOrStr on Ok
    {
        void *r = rt_result_ok_str(rt_const_cstr("hello"));
        rt_string result = rt_result_unwrap_or_str(r, rt_const_cstr("default"));
        test_result("UnwrapOrStr on Ok returns value", strcmp(rt_string_cstr(result), "hello") == 0);
    }

    // Test 6: UnwrapOrStr on Err
    {
        void *r = rt_result_err_str(rt_const_cstr("error"));
        rt_string result = rt_result_unwrap_or_str(r, rt_const_cstr("default"));
        test_result("UnwrapOrStr on Err returns default", strcmp(rt_string_cstr(result), "default") == 0);
    }

    printf("\n");
}

static void test_result_ok_err_value()
{
    printf("Testing Result OkValue/ErrValue:\n");

    // Test 1: OkValue on Ok returns value
    {
        int val = 42;
        void *r = rt_result_ok(&val);
        test_result("OkValue on Ok returns value", rt_result_ok_value(r) == &val);
    }

    // Test 2: OkValue on Err returns NULL
    {
        void *r = rt_result_err_str(rt_const_cstr("error"));
        test_result("OkValue on Err returns NULL", rt_result_ok_value(r) == NULL);
    }

    // Test 3: ErrValue on Err returns value
    {
        int val = 99;
        void *r = rt_result_err(&val);
        test_result("ErrValue on Err returns value", rt_result_err_value(r) == &val);
    }

    // Test 4: ErrValue on Ok returns NULL
    {
        void *r = rt_result_ok_i64(123);
        test_result("ErrValue on Ok returns NULL", rt_result_err_value(r) == NULL);
    }

    printf("\n");
}

static void test_result_to_string()
{
    printf("Testing Result ToString:\n");

    // Test 1: Ok pointer
    {
        void *r = rt_result_ok(NULL);
        rt_string s = rt_result_to_string(r);
        test_result("Ok(null) string", strstr(rt_string_cstr(s), "Ok(") != NULL);
    }

    // Test 2: Ok string
    {
        void *r = rt_result_ok_str(rt_const_cstr("hello"));
        rt_string s = rt_result_to_string(r);
        test_result("Ok(string) contains value", strstr(rt_string_cstr(s), "hello") != NULL);
    }

    // Test 3: Ok i64
    {
        void *r = rt_result_ok_i64(42);
        rt_string s = rt_result_to_string(r);
        test_result("Ok(i64) contains value", strstr(rt_string_cstr(s), "42") != NULL);
    }

    // Test 4: Err string
    {
        void *r = rt_result_err_str(rt_const_cstr("failure"));
        rt_string s = rt_result_to_string(r);
        test_result("Err(string) contains Err", strstr(rt_string_cstr(s), "Err(") != NULL);
        test_result("Err(string) contains value", strstr(rt_string_cstr(s), "failure") != NULL);
    }

    printf("\n");
}

static void test_result_equality()
{
    printf("Testing Result Equality:\n");

    // Test 1: Two Ok i64 with same value
    {
        void *r1 = rt_result_ok_i64(42);
        void *r2 = rt_result_ok_i64(42);
        test_result("Equal Ok i64", rt_result_equals(r1, r2) == 1);
    }

    // Test 2: Two Ok i64 with different values
    {
        void *r1 = rt_result_ok_i64(42);
        void *r2 = rt_result_ok_i64(99);
        test_result("Unequal Ok i64", rt_result_equals(r1, r2) == 0);
    }

    // Test 3: Ok vs Err
    {
        void *r1 = rt_result_ok_i64(42);
        void *r2 = rt_result_err_str(rt_const_cstr("error"));
        test_result("Ok vs Err not equal", rt_result_equals(r1, r2) == 0);
    }

    // Test 4: Two Ok strings with same value
    {
        void *r1 = rt_result_ok_str(rt_const_cstr("hello"));
        void *r2 = rt_result_ok_str(rt_const_cstr("hello"));
        test_result("Equal Ok strings", rt_result_equals(r1, r2) == 1);
    }

    // Test 5: Two Ok strings with different values
    {
        void *r1 = rt_result_ok_str(rt_const_cstr("hello"));
        void *r2 = rt_result_ok_str(rt_const_cstr("world"));
        test_result("Unequal Ok strings", rt_result_equals(r1, r2) == 0);
    }

    printf("\n");
}

static void test_result_null_handling()
{
    printf("Testing Result NULL handling:\n");

    // Test 1: IsOk on NULL
    {
        test_result("IsOk on NULL returns 0", rt_result_is_ok(NULL) == 0);
    }

    // Test 2: IsErr on NULL
    {
        test_result("IsErr on NULL returns 0", rt_result_is_err(NULL) == 0);
    }

    // Test 3: UnwrapOr on NULL
    {
        int def = 99;
        void *result = rt_result_unwrap_or(NULL, &def);
        test_result("UnwrapOr on NULL returns default", result == &def);
    }

    printf("\n");
}

//=============================================================================
// Entry Point
//=============================================================================

int main()
{
    printf("=== RT Result Tests ===\n\n");

    test_result_ok_creation();
    test_result_err_creation();
    test_result_unwrap_or();
    test_result_ok_err_value();
    test_result_to_string();
    test_result_equality();
    test_result_null_handling();

    printf("All Result tests passed!\n");
    return 0;
}
