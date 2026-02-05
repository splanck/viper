//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/runtime/RTSeqFunctionalTests.cpp
// Purpose: Validate Seq functional operations (Keep, Reject, Apply, Fold, etc.)
// Key invariants: Function pointers work correctly as predicates/transforms.
// Links: docs/viperlib/collections.md
//
//===----------------------------------------------------------------------===//

#include "rt_box.h"
#include "rt_seq.h"

#include <cassert>
#include <cstdio>
#include <cstdint>

/// @brief Helper to print test result.
static void test_result(const char *name, bool passed)
{
    printf("  %s: %s\n", name, passed ? "PASS" : "FAIL");
    assert(passed);
}

/// @brief Create a Seq with integer values (boxed).
static void *make_int_seq(int64_t *values, size_t count)
{
    void *seq = rt_seq_new();
    for (size_t i = 0; i < count; i++)
    {
        rt_seq_push(seq, rt_box_i64(values[i]));
    }
    return seq;
}

/// @brief Get integer value from boxed element.
static int64_t unbox_int(void *obj)
{
    return rt_unbox_i64(obj);
}

//=============================================================================
// Predicate Functions
//=============================================================================

/// @brief Predicate: returns true if value is even.
static int8_t is_even(void *obj)
{
    int64_t val = unbox_int(obj);
    return (val % 2 == 0) ? 1 : 0;
}

/// @brief Predicate: returns true if value is positive.
static int8_t is_positive(void *obj)
{
    int64_t val = unbox_int(obj);
    return (val > 0) ? 1 : 0;
}

/// @brief Predicate: returns true if value > 5.
static int8_t is_greater_than_5(void *obj)
{
    int64_t val = unbox_int(obj);
    return (val > 5) ? 1 : 0;
}

/// @brief Predicate: always returns true.
static int8_t always_true(void *obj)
{
    (void)obj;
    return 1;
}

/// @brief Predicate: always returns false.
static int8_t always_false(void *obj)
{
    (void)obj;
    return 0;
}

//=============================================================================
// Transform Functions
//=============================================================================

/// @brief Transform: doubles the value.
static void *double_value(void *obj)
{
    int64_t val = unbox_int(obj);
    return rt_box_i64(val * 2);
}

/// @brief Transform: squares the value.
static void *square_value(void *obj)
{
    int64_t val = unbox_int(obj);
    return rt_box_i64(val * val);
}

//=============================================================================
// Reducer Functions
//=============================================================================

/// @brief Reducer: sums two values.
static void *sum_reducer(void *acc, void *elem)
{
    int64_t a = unbox_int(acc);
    int64_t b = unbox_int(elem);
    return rt_box_i64(a + b);
}

/// @brief Reducer: multiplies two values.
static void *product_reducer(void *acc, void *elem)
{
    int64_t a = unbox_int(acc);
    int64_t b = unbox_int(elem);
    return rt_box_i64(a * b);
}

//=============================================================================
// Keep Tests
//=============================================================================

static void test_seq_keep()
{
    printf("Testing Seq.Keep:\n");

    // Test 1: Keep even numbers
    {
        int64_t values[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
        void *seq = make_int_seq(values, 10);

        void *result = rt_seq_keep(seq, is_even);
        test_result("Keep evens - length", rt_seq_len(result) == 5);

        // Check values are 2, 4, 6, 8, 10
        test_result("Keep evens - first", unbox_int(rt_seq_get(result, 0)) == 2);
        test_result("Keep evens - last", unbox_int(rt_seq_get(result, 4)) == 10);
    }

    // Test 2: Keep with empty result
    {
        int64_t values[] = {1, 3, 5, 7, 9};
        void *seq = make_int_seq(values, 5);

        void *result = rt_seq_keep(seq, is_even);
        test_result("Keep none - empty result", rt_seq_len(result) == 0);
    }

    // Test 3: Keep all
    {
        int64_t values[] = {2, 4, 6, 8};
        void *seq = make_int_seq(values, 4);

        void *result = rt_seq_keep(seq, is_even);
        test_result("Keep all - same length", rt_seq_len(result) == 4);
    }

    printf("\n");
}

//=============================================================================
// Reject Tests
//=============================================================================

static void test_seq_reject()
{
    printf("Testing Seq.Reject:\n");

    // Test 1: Reject even numbers (keep odds)
    {
        int64_t values[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
        void *seq = make_int_seq(values, 10);

        void *result = rt_seq_reject(seq, is_even);
        test_result("Reject evens - length", rt_seq_len(result) == 5);
        test_result("Reject evens - first", unbox_int(rt_seq_get(result, 0)) == 1);
        test_result("Reject evens - last", unbox_int(rt_seq_get(result, 4)) == 9);
    }

    printf("\n");
}

//=============================================================================
// Apply Tests
//=============================================================================

static void test_seq_apply()
{
    printf("Testing Seq.Apply:\n");

    // Test 1: Double all values
    {
        int64_t values[] = {1, 2, 3, 4, 5};
        void *seq = make_int_seq(values, 5);

        void *result = rt_seq_apply(seq, double_value);
        test_result("Apply double - length", rt_seq_len(result) == 5);
        test_result("Apply double - first", unbox_int(rt_seq_get(result, 0)) == 2);
        test_result("Apply double - last", unbox_int(rt_seq_get(result, 4)) == 10);
    }

    // Test 2: Square all values
    {
        int64_t values[] = {1, 2, 3, 4};
        void *seq = make_int_seq(values, 4);

        void *result = rt_seq_apply(seq, square_value);
        test_result("Apply square - values",
                    unbox_int(rt_seq_get(result, 0)) == 1 &&
                    unbox_int(rt_seq_get(result, 1)) == 4 &&
                    unbox_int(rt_seq_get(result, 2)) == 9 &&
                    unbox_int(rt_seq_get(result, 3)) == 16);
    }

    printf("\n");
}

//=============================================================================
// All/Any/None Tests
//=============================================================================

static void test_seq_predicates()
{
    printf("Testing Seq.All/Any/None:\n");

    // Test 1: All positive
    {
        int64_t values[] = {1, 2, 3, 4, 5};
        void *seq = make_int_seq(values, 5);

        test_result("All positive - true", rt_seq_all(seq, is_positive) == 1);
    }

    // Test 2: Not all positive
    {
        int64_t values[] = {1, 2, -3, 4, 5};
        void *seq = make_int_seq(values, 5);

        test_result("All positive - false", rt_seq_all(seq, is_positive) == 0);
    }

    // Test 3: Any positive
    {
        int64_t values[] = {-1, -2, 3, -4};
        void *seq = make_int_seq(values, 4);

        test_result("Any positive - true", rt_seq_any(seq, is_positive) == 1);
    }

    // Test 4: No positive
    {
        int64_t values[] = {-1, -2, -3};
        void *seq = make_int_seq(values, 3);

        test_result("Any positive - false", rt_seq_any(seq, is_positive) == 0);
    }

    // Test 5: None positive
    {
        int64_t values[] = {-1, -2, -3};
        void *seq = make_int_seq(values, 3);

        test_result("None positive - true", rt_seq_none(seq, is_positive) == 1);
    }

    // Test 6: Empty seq
    {
        void *seq = rt_seq_new();

        test_result("Empty all - vacuous truth", rt_seq_all(seq, is_positive) == 1);
        test_result("Empty any - false", rt_seq_any(seq, is_positive) == 0);
        test_result("Empty none - true", rt_seq_none(seq, is_positive) == 1);
    }

    printf("\n");
}

//=============================================================================
// CountWhere/FindWhere Tests
//=============================================================================

static void test_seq_count_find()
{
    printf("Testing Seq.CountWhere/FindWhere:\n");

    // Test 1: Count evens
    {
        int64_t values[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
        void *seq = make_int_seq(values, 10);

        test_result("CountWhere even", rt_seq_count_where(seq, is_even) == 5);
    }

    // Test 2: Find first even
    {
        int64_t values[] = {1, 3, 5, 6, 7};
        void *seq = make_int_seq(values, 5);

        void *found = rt_seq_find_where(seq, is_even);
        test_result("FindWhere even - found", found != NULL);
        test_result("FindWhere even - value", unbox_int(found) == 6);
    }

    // Test 3: Find with no match
    {
        int64_t values[] = {1, 3, 5, 7};
        void *seq = make_int_seq(values, 4);

        void *found = rt_seq_find_where(seq, is_even);
        test_result("FindWhere no match - NULL", found == NULL);
    }

    printf("\n");
}

//=============================================================================
// TakeWhile/DropWhile Tests
//=============================================================================

static void test_seq_take_drop_while()
{
    printf("Testing Seq.TakeWhile/DropWhile:\n");

    // Test 1: TakeWhile positive
    {
        int64_t values[] = {1, 2, 3, -4, 5, 6};
        void *seq = make_int_seq(values, 6);

        void *result = rt_seq_take_while(seq, is_positive);
        test_result("TakeWhile positive - length", rt_seq_len(result) == 3);
        test_result("TakeWhile positive - last", unbox_int(rt_seq_get(result, 2)) == 3);
    }

    // Test 2: DropWhile positive
    {
        int64_t values[] = {1, 2, 3, -4, 5, 6};
        void *seq = make_int_seq(values, 6);

        void *result = rt_seq_drop_while(seq, is_positive);
        test_result("DropWhile positive - length", rt_seq_len(result) == 3);
        test_result("DropWhile positive - first", unbox_int(rt_seq_get(result, 0)) == -4);
    }

    // Test 3: TakeWhile all match
    {
        int64_t values[] = {1, 2, 3};
        void *seq = make_int_seq(values, 3);

        void *result = rt_seq_take_while(seq, is_positive);
        test_result("TakeWhile all - length", rt_seq_len(result) == 3);
    }

    // Test 4: DropWhile none match
    {
        int64_t values[] = {-1, -2, -3};
        void *seq = make_int_seq(values, 3);

        void *result = rt_seq_drop_while(seq, is_positive);
        test_result("DropWhile none - length", rt_seq_len(result) == 3);
    }

    printf("\n");
}

//=============================================================================
// Fold Tests
//=============================================================================

static void test_seq_fold()
{
    printf("Testing Seq.Fold:\n");

    // Test 1: Sum
    {
        int64_t values[] = {1, 2, 3, 4, 5};
        void *seq = make_int_seq(values, 5);

        void *result = rt_seq_fold(seq, rt_box_i64(0), sum_reducer);
        test_result("Fold sum", unbox_int(result) == 15);
    }

    // Test 2: Product
    {
        int64_t values[] = {1, 2, 3, 4, 5};
        void *seq = make_int_seq(values, 5);

        void *result = rt_seq_fold(seq, rt_box_i64(1), product_reducer);
        test_result("Fold product", unbox_int(result) == 120);
    }

    // Test 3: Empty fold returns init
    {
        void *seq = rt_seq_new();

        void *result = rt_seq_fold(seq, rt_box_i64(42), sum_reducer);
        test_result("Fold empty - returns init", unbox_int(result) == 42);
    }

    printf("\n");
}

//=============================================================================
// Entry Point
//=============================================================================

int main()
{
    printf("=== RT Seq Functional Tests ===\n\n");

    test_seq_keep();
    test_seq_reject();
    test_seq_apply();
    test_seq_predicates();
    test_seq_count_find();
    test_seq_take_drop_while();
    test_seq_fold();

    printf("All Seq functional tests passed!\n");
    return 0;
}
