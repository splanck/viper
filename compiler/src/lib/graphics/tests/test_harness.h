//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/lib/graphics/tests/test_harness.h
// Purpose: Lightweight test harness utilities for ViperGFX C tests.
// Key invariants: Header-only; no global state; safe for inclusion in multiple
//                 translation units.
// Ownership/Lifetime: N/A (declarations/macros only).
// Links: docs/vgfx-testing.md
//
//===----------------------------------------------------------------------===//

/*
 * ViperGFX - Simple Test Harness
 * Provides assertion macros for unit testing
 */

#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /* Global test tracking */
    static int _test_total = 0;
    static int _test_passed = 0;
    static const char *_test_current = NULL;

/* Start a test case */
#define TEST_BEGIN(name)                                                                           \
    do                                                                                             \
    {                                                                                              \
        _test_current = (name);                                                                    \
        printf("\n[ RUN      ] %s\n", _test_current);                                              \
    } while (0)

/* End a test case */
#define TEST_END()                                                                                 \
    do                                                                                             \
    {                                                                                              \
        printf("[       OK ] %s\n", _test_current);                                                \
        _test_total++;                                                                             \
        _test_passed++;                                                                            \
        _test_current = NULL;                                                                      \
    } while (0)

/* Assertion failure handler */
#define TEST_FAIL(msg)                                                                             \
    do                                                                                             \
    {                                                                                              \
        printf("[  FAILED  ] %s\n", _test_current);                                                \
        printf("  %s:%d: %s\n", __FILE__, __LINE__, (msg));                                        \
        _test_total++;                                                                             \
        _test_current = NULL;                                                                      \
    } while (0)

/* Assertion macros */
#define ASSERT_TRUE(cond)                                                                          \
    do                                                                                             \
    {                                                                                              \
        if (!(cond))                                                                               \
        {                                                                                          \
            printf("  Assertion failed: %s\n", #cond);                                             \
            TEST_FAIL("Expected true, got false");                                                 \
            return;                                                                                \
        }                                                                                          \
    } while (0)

#define ASSERT_FALSE(cond)                                                                         \
    do                                                                                             \
    {                                                                                              \
        if (cond)                                                                                  \
        {                                                                                          \
            printf("  Assertion failed: !(%s)\n", #cond);                                          \
            TEST_FAIL("Expected false, got true");                                                 \
            return;                                                                                \
        }                                                                                          \
    } while (0)

#define ASSERT_EQ(a, b)                                                                            \
    do                                                                                             \
    {                                                                                              \
        if ((a) != (b))                                                                            \
        {                                                                                          \
            printf("  Assertion failed: %s == %s\n", #a, #b);                                      \
            printf("  Expected: %d\n", (int)(b));                                                  \
            printf("  Actual:   %d\n", (int)(a));                                                  \
            TEST_FAIL("Values not equal");                                                         \
            return;                                                                                \
        }                                                                                          \
    } while (0)

#define ASSERT_NE(a, b)                                                                            \
    do                                                                                             \
    {                                                                                              \
        if ((a) == (b))                                                                            \
        {                                                                                          \
            printf("  Assertion failed: %s != %s\n", #a, #b);                                      \
            printf("  Both values: %d\n", (int)(a));                                               \
            TEST_FAIL("Values are equal");                                                         \
            return;                                                                                \
        }                                                                                          \
    } while (0)

#define ASSERT_NULL(ptr)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if ((ptr) != NULL)                                                                         \
        {                                                                                          \
            printf("  Assertion failed: %s == NULL\n", #ptr);                                      \
            TEST_FAIL("Expected NULL pointer");                                                    \
            return;                                                                                \
        }                                                                                          \
    } while (0)

#define ASSERT_NOT_NULL(ptr)                                                                       \
    do                                                                                             \
    {                                                                                              \
        if ((ptr) == NULL)                                                                         \
        {                                                                                          \
            printf("  Assertion failed: %s != NULL\n", #ptr);                                      \
            TEST_FAIL("Unexpected NULL pointer");                                                  \
            return;                                                                                \
        }                                                                                          \
    } while (0)

#define ASSERT_STR_EQ(a, b)                                                                        \
    do                                                                                             \
    {                                                                                              \
        if (strcmp((a), (b)) != 0)                                                                 \
        {                                                                                          \
            printf("  Assertion failed: %s == %s\n", #a, #b);                                      \
            printf("  Expected: \"%s\"\n", (b));                                                   \
            printf("  Actual:   \"%s\"\n", (a));                                                   \
            TEST_FAIL("Strings not equal");                                                        \
            return;                                                                                \
        }                                                                                          \
    } while (0)

/* Test runner summary */
#define TEST_SUMMARY()                                                                             \
    do                                                                                             \
    {                                                                                              \
        printf("\n========================================\n");                                    \
        if (_test_passed == _test_total)                                                           \
        {                                                                                          \
            printf("SUCCESS: All %d tests passed!\n", _test_total);                                \
        }                                                                                          \
        else                                                                                       \
        {                                                                                          \
            printf("FAILURE: %d/%d tests passed\n", _test_passed, _test_total);                    \
        }                                                                                          \
        printf("========================================\n");                                      \
    } while (0)

/* Return value for main() */
#define TEST_RETURN_CODE() (_test_passed == _test_total ? 0 : 1)

#ifdef __cplusplus
}
#endif
//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/lib/graphics/tests/test_harness.h
// Purpose: Lightweight test harness utilities for ViperGFX C tests.
// Key invariants: Header-only; no global state; safe for inclusion in multiple
//                 translation units.
// Ownership/Lifetime: N/A (declarations/macros only).
// Links: docs/vgfx-testing.md
//
//===----------------------------------------------------------------------===//
