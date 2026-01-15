/**
 * @file test_framework.h
 * @brief Minimal unit test framework for ViperDOS host tests.
 *
 * This is a simple, header-only test framework for running unit tests
 * on the host machine. Tests are pure C and run natively (not in QEMU).
 *
 * Usage:
 *   #include "test_framework.h"
 *
 *   int main(void) {
 *       TEST_BEGIN("My Test Suite");
 *
 *       TEST_ASSERT(1 + 1 == 2, "basic math");
 *       TEST_ASSERT_EQ(42, 42, "equality");
 *       TEST_ASSERT_STR_EQ("hello", "hello", "strings");
 *
 *       TEST_END();
 *   }
 */

#ifndef VIPERDOS_TEST_FRAMEWORK_H
#define VIPERDOS_TEST_FRAMEWORK_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Test state - static to be file-local */
static int _test_passed = 0;
static int _test_failed = 0;
static const char *_test_suite_name = "Unknown";

/**
 * Begin a test suite.
 */
#define TEST_BEGIN(name) \
    do { \
        _test_suite_name = (name); \
        _test_passed = 0; \
        _test_failed = 0; \
        printf("=== %s ===\n", _test_suite_name); \
    } while (0)

/**
 * End a test suite and return appropriate exit code.
 */
#define TEST_END() \
    do { \
        printf("\n--- Results: %d passed, %d failed ---\n", \
               _test_passed, _test_failed); \
        if (_test_failed == 0) { \
            printf("OK\n"); \
            return 0; \
        } else { \
            printf("FAILED\n"); \
            return 1; \
        } \
    } while (0)

/**
 * Assert a condition is true.
 */
#define TEST_ASSERT(cond, msg) \
    do { \
        if (cond) { \
            _test_passed++; \
            printf("  [PASS] %s\n", (msg)); \
        } else { \
            _test_failed++; \
            printf("  [FAIL] %s\n", (msg)); \
            printf("         at %s:%d\n", __FILE__, __LINE__); \
        } \
    } while (0)

/**
 * Assert two integers are equal.
 */
#define TEST_ASSERT_EQ(expected, actual, msg) \
    do { \
        long long _exp = (long long)(expected); \
        long long _act = (long long)(actual); \
        if (_exp == _act) { \
            _test_passed++; \
            printf("  [PASS] %s\n", (msg)); \
        } else { \
            _test_failed++; \
            printf("  [FAIL] %s\n", (msg)); \
            printf("         expected: %lld, got: %lld\n", _exp, _act); \
            printf("         at %s:%d\n", __FILE__, __LINE__); \
        } \
    } while (0)

/**
 * Assert two integers are not equal.
 */
#define TEST_ASSERT_NE(not_expected, actual, msg) \
    do { \
        long long _nexp = (long long)(not_expected); \
        long long _act = (long long)(actual); \
        if (_nexp != _act) { \
            _test_passed++; \
            printf("  [PASS] %s\n", (msg)); \
        } else { \
            _test_failed++; \
            printf("  [FAIL] %s\n", (msg)); \
            printf("         expected NOT: %lld, got: %lld\n", _nexp, _act); \
            printf("         at %s:%d\n", __FILE__, __LINE__); \
        } \
    } while (0)

/**
 * Assert two strings are equal.
 */
#define TEST_ASSERT_STR_EQ(expected, actual, msg) \
    do { \
        const char *_exp = (expected); \
        const char *_act = (actual); \
        if (_exp && _act && strcmp(_exp, _act) == 0) { \
            _test_passed++; \
            printf("  [PASS] %s\n", (msg)); \
        } else { \
            _test_failed++; \
            printf("  [FAIL] %s\n", (msg)); \
            printf("         expected: \"%s\"\n", _exp ? _exp : "(null)"); \
            printf("         got:      \"%s\"\n", _act ? _act : "(null)"); \
            printf("         at %s:%d\n", __FILE__, __LINE__); \
        } \
    } while (0)

/**
 * Assert a pointer is not NULL.
 */
#define TEST_ASSERT_NOT_NULL(ptr, msg) \
    do { \
        if ((ptr) != NULL) { \
            _test_passed++; \
            printf("  [PASS] %s\n", (msg)); \
        } else { \
            _test_failed++; \
            printf("  [FAIL] %s (got NULL)\n", (msg)); \
            printf("         at %s:%d\n", __FILE__, __LINE__); \
        } \
    } while (0)

/**
 * Assert a pointer is NULL.
 */
#define TEST_ASSERT_NULL(ptr, msg) \
    do { \
        if ((ptr) == NULL) { \
            _test_passed++; \
            printf("  [PASS] %s\n", (msg)); \
        } else { \
            _test_failed++; \
            printf("  [FAIL] %s (got non-NULL)\n", (msg)); \
            printf("         at %s:%d\n", __FILE__, __LINE__); \
        } \
    } while (0)

/**
 * Print a section header within a test suite.
 */
#define TEST_SECTION(name) \
    printf("\n[%s]\n", (name))

#endif /* VIPERDOS_TEST_FRAMEWORK_H */
