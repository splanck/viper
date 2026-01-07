//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/RTDiagTests.cpp
// Purpose: Tests for Viper.Diagnostics assert functions.
//
// Note: These tests verify that passing assertions don't trap. The failure
// cases are tested separately since they terminate the process.
//
//===----------------------------------------------------------------------===//

#include "rt_internal.h"
#include "rt_string.h"
#include "rt_trap.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>

extern "C" void vm_trap(const char *msg)
{
    rt_abort(msg);
}

// ============================================================================
// Helper
// ============================================================================

static rt_string make_str(const char *s)
{
    return rt_const_cstr(s);
}

// ============================================================================
// AssertEq Tests (passing cases)
// ============================================================================

static void test_assert_eq_passing()
{
    // These should all pass (not trap)
    rt_diag_assert_eq(42, 42, make_str("equal integers"));
    rt_diag_assert_eq(0, 0, make_str("zero equals zero"));
    rt_diag_assert_eq(-100, -100, make_str("negative integers"));
    rt_diag_assert_eq(INT64_MAX, INT64_MAX, make_str("max int64"));
    rt_diag_assert_eq(INT64_MIN, INT64_MIN, make_str("min int64"));

    printf("test_assert_eq_passing: PASSED\n");
}

// ============================================================================
// AssertNeq Tests (passing cases)
// ============================================================================

static void test_assert_neq_passing()
{
    rt_diag_assert_neq(1, 2, make_str("different integers"));
    rt_diag_assert_neq(0, 1, make_str("zero vs one"));
    rt_diag_assert_neq(-1, 1, make_str("negative vs positive"));
    rt_diag_assert_neq(INT64_MAX, INT64_MIN, make_str("max vs min"));

    printf("test_assert_neq_passing: PASSED\n");
}

// ============================================================================
// AssertEqNum Tests (passing cases)
// ============================================================================

static void test_assert_eq_num_passing()
{
    rt_diag_assert_eq_num(3.14, 3.14, make_str("equal doubles"));
    rt_diag_assert_eq_num(0.0, 0.0, make_str("zero equals zero"));
    rt_diag_assert_eq_num(-2.5, -2.5, make_str("negative doubles"));

    // Test with very close values (within epsilon)
    rt_diag_assert_eq_num(1.0, 1.0 + 1e-12, make_str("nearly equal"));

    // Test NaN equality (special case - NaN equals NaN for this assertion)
    rt_diag_assert_eq_num(NAN, NAN, make_str("NaN equals NaN"));

    // Test infinity
    rt_diag_assert_eq_num(INFINITY, INFINITY, make_str("infinity equals infinity"));
    rt_diag_assert_eq_num(-INFINITY, -INFINITY, make_str("neg infinity"));

    printf("test_assert_eq_num_passing: PASSED\n");
}

// ============================================================================
// AssertEqStr Tests (passing cases)
// ============================================================================

static void test_assert_eq_str_passing()
{
    rt_diag_assert_eq_str(make_str("hello"), make_str("hello"), make_str("equal strings"));
    rt_diag_assert_eq_str(make_str(""), make_str(""), make_str("empty strings"));
    rt_diag_assert_eq_str(make_str("abc123"), make_str("abc123"), make_str("alphanumeric"));

    printf("test_assert_eq_str_passing: PASSED\n");
}

// ============================================================================
// AssertNull Tests (passing cases)
// ============================================================================

static void test_assert_null_passing()
{
    rt_diag_assert_null(nullptr, make_str("null pointer"));

    printf("test_assert_null_passing: PASSED\n");
}

// ============================================================================
// AssertNotNull Tests (passing cases)
// ============================================================================

static void test_assert_not_null_passing()
{
    int dummy = 42;
    rt_diag_assert_not_null(&dummy, make_str("non-null pointer"));

    const char *str = "test";
    rt_diag_assert_not_null((void *)str, make_str("string pointer"));

    printf("test_assert_not_null_passing: PASSED\n");
}

// ============================================================================
// AssertGt Tests (passing cases)
// ============================================================================

static void test_assert_gt_passing()
{
    rt_diag_assert_gt(10, 5, make_str("10 > 5"));
    rt_diag_assert_gt(0, -1, make_str("0 > -1"));
    rt_diag_assert_gt(INT64_MAX, 0, make_str("max > 0"));
    rt_diag_assert_gt(1, INT64_MIN, make_str("1 > min"));

    printf("test_assert_gt_passing: PASSED\n");
}

// ============================================================================
// AssertLt Tests (passing cases)
// ============================================================================

static void test_assert_lt_passing()
{
    rt_diag_assert_lt(5, 10, make_str("5 < 10"));
    rt_diag_assert_lt(-1, 0, make_str("-1 < 0"));
    rt_diag_assert_lt(0, INT64_MAX, make_str("0 < max"));
    rt_diag_assert_lt(INT64_MIN, 1, make_str("min < 1"));

    printf("test_assert_lt_passing: PASSED\n");
}

// ============================================================================
// AssertGte Tests (passing cases)
// ============================================================================

static void test_assert_gte_passing()
{
    rt_diag_assert_gte(10, 5, make_str("10 >= 5"));
    rt_diag_assert_gte(5, 5, make_str("5 >= 5 (equal)"));
    rt_diag_assert_gte(0, -1, make_str("0 >= -1"));
    rt_diag_assert_gte(0, 0, make_str("0 >= 0"));

    printf("test_assert_gte_passing: PASSED\n");
}

// ============================================================================
// AssertLte Tests (passing cases)
// ============================================================================

static void test_assert_lte_passing()
{
    rt_diag_assert_lte(5, 10, make_str("5 <= 10"));
    rt_diag_assert_lte(5, 5, make_str("5 <= 5 (equal)"));
    rt_diag_assert_lte(-1, 0, make_str("-1 <= 0"));
    rt_diag_assert_lte(0, 0, make_str("0 <= 0"));

    printf("test_assert_lte_passing: PASSED\n");
}

// ============================================================================
// Basic Assert Tests (passing cases)
// ============================================================================

static void test_basic_assert_passing()
{
    rt_diag_assert(1, make_str("true condition"));
    rt_diag_assert(42, make_str("non-zero is true"));
    rt_diag_assert(-1, make_str("negative non-zero is true"));

    printf("test_basic_assert_passing: PASSED\n");
}

// ============================================================================
// Main
// ============================================================================

int main()
{
    printf("=== Viper.Diagnostics Assert Tests ===\n\n");

    // Basic assert
    test_basic_assert_passing();

    // Equality assertions
    test_assert_eq_passing();
    test_assert_neq_passing();
    test_assert_eq_num_passing();
    test_assert_eq_str_passing();

    // Null assertions
    test_assert_null_passing();
    test_assert_not_null_passing();

    // Comparison assertions
    test_assert_gt_passing();
    test_assert_lt_passing();
    test_assert_gte_passing();
    test_assert_lte_passing();

    printf("\nAll RTDiagTests passed!\n");
    printf("(Note: Failure cases not tested here as they terminate the process)\n");
    return 0;
}
