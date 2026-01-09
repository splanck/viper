//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE in the project root for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the BASIC runtime's fatal trap helpers.  These routines print a
// diagnostic describing the failure before terminating the hosting process.
// Centralising the logic keeps trap text and exit codes consistent between the
// VM and native code paths.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Fatal trap helpers shared by the runtime C ABI.
/// @details Provides @ref rt_trap_div0, mirroring the VM's behaviour when a
///          division by zero occurs.  The helper writes a deterministic message
///          to stderr and terminates the process with exit code 1.

#include <inttypes.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rt_internal.h"
#include "rt_trap.h"

/// @brief Report a division-by-zero trap and terminate the process.
/// @details Prints a fixed diagnostic to stderr, flushes the stream to ensure
///          embedders observe the message, and exits with status code 1.  The
///          behaviour mirrors the VM trap hook so test suites observe consistent
///          failure semantics across execution modes.
void rt_trap_div0(void)
{
    fprintf(stderr, "Viper runtime trap: division by zero\n");
    fflush(stderr);
    exit(1); // Match VM behavior if your VM uses a specific code; adjust here later if needed.
}

/// @brief Assert that @p condition holds; otherwise trap with @p message.
/// @details When @p condition is zero, evaluates @p message and raises a runtime
///          trap using @ref rt_trap. Empty or null messages use the default
///          text "Assertion failed" to avoid silent failures.
/// @param condition Non-zero when the assertion succeeded.
/// @param message Optional runtime string describing the failure.
void rt_diag_assert(int8_t condition, rt_string message)
{
    if (condition)
        return;

    const char *msg = "Assertion failed";
    if (message && message->data)
    {
        size_t len = (message->heap && message->heap != RT_SSO_SENTINEL)
                         ? rt_heap_len(message->data)
                         : message->literal_len;
        if (len > 0)
            msg = rt_string_cstr(message);
    }

    rt_trap(msg);
}

/// @brief Helper to extract message string with fallback.
static const char *get_message(rt_string message, const char *fallback)
{
    if (message && message->data)
    {
        size_t len = (message->heap && message->heap != RT_SSO_SENTINEL)
                         ? rt_heap_len(message->data)
                         : message->literal_len;
        if (len > 0)
            return rt_string_cstr(message);
    }
    return fallback;
}

/// @brief Assert two integers are equal.
void rt_diag_assert_eq(int64_t expected, int64_t actual, rt_string message)
{
    if (expected == actual)
        return;

    const char *msg = get_message(message, "AssertEq failed");
    char buf[256];
    snprintf(buf, sizeof(buf), "%s: expected %" PRId64 ", got %" PRId64, msg, expected, actual);
    rt_trap(buf);
}

/// @brief Assert two integers are not equal.
void rt_diag_assert_neq(int64_t a, int64_t b, rt_string message)
{
    if (a != b)
        return;

    const char *msg = get_message(message, "AssertNeq failed");
    char buf[256];
    snprintf(buf, sizeof(buf), "%s: values should not be equal (both are %" PRId64 ")", msg, a);
    rt_trap(buf);
}

/// @brief Assert two numbers are approximately equal.
void rt_diag_assert_eq_num(double expected, double actual, rt_string message)
{
    // Use a relative epsilon for float comparison
    double epsilon = 1e-9;
    double diff = fabs(expected - actual);
    double maxval = fmax(fabs(expected), fabs(actual));

    // Handle special cases
    if (expected == actual)
        return;
    if (isnan(expected) && isnan(actual))
        return;

    // Use relative comparison for large values, absolute for small
    bool equal = (maxval < 1.0) ? (diff < epsilon) : (diff / maxval < epsilon);
    if (equal)
        return;

    const char *msg = get_message(message, "AssertEqNum failed");
    char buf[256];
    snprintf(buf, sizeof(buf), "%s: expected %g, got %g (diff=%g)", msg, expected, actual, diff);
    rt_trap(buf);
}

/// @brief Assert two strings are equal.
void rt_diag_assert_eq_str(rt_string expected, rt_string actual, rt_string message)
{
    const char *exp_str = expected ? rt_string_cstr(expected) : "";
    const char *act_str = actual ? rt_string_cstr(actual) : "";

    if (exp_str == NULL)
        exp_str = "";
    if (act_str == NULL)
        act_str = "";

    if (strcmp(exp_str, act_str) == 0)
        return;

    const char *msg = get_message(message, "AssertEqStr failed");
    char buf[512];
    snprintf(buf, sizeof(buf), "%s: expected \"%s\", got \"%s\"", msg, exp_str, act_str);
    rt_trap(buf);
}

/// @brief Assert an object reference is null.
void rt_diag_assert_null(void *obj, rt_string message)
{
    if (obj == NULL)
        return;

    const char *msg = get_message(message, "AssertNull failed");
    char buf[256];
    snprintf(buf, sizeof(buf), "%s: expected null, got non-null object", msg);
    rt_trap(buf);
}

/// @brief Assert an object reference is not null.
void rt_diag_assert_not_null(void *obj, rt_string message)
{
    if (obj != NULL)
        return;

    const char *msg = get_message(message, "AssertNotNull failed");
    char buf[256];
    snprintf(buf, sizeof(buf), "%s: expected non-null, got null", msg);
    rt_trap(buf);
}

/// @brief Unconditionally fail with a message.
void rt_diag_assert_fail(rt_string message)
{
    const char *msg = get_message(message, "AssertFail called");
    rt_trap(msg);
}

/// @brief Assert first value is greater than second.
void rt_diag_assert_gt(int64_t a, int64_t b, rt_string message)
{
    if (a > b)
        return;

    const char *msg = get_message(message, "AssertGt failed");
    char buf[256];
    snprintf(buf, sizeof(buf), "%s: expected %" PRId64 " > %" PRId64, msg, a, b);
    rt_trap(buf);
}

/// @brief Assert first value is less than second.
void rt_diag_assert_lt(int64_t a, int64_t b, rt_string message)
{
    if (a < b)
        return;

    const char *msg = get_message(message, "AssertLt failed");
    char buf[256];
    snprintf(buf, sizeof(buf), "%s: expected %" PRId64 " < %" PRId64, msg, a, b);
    rt_trap(buf);
}

/// @brief Assert first value is greater than or equal to second.
void rt_diag_assert_gte(int64_t a, int64_t b, rt_string message)
{
    if (a >= b)
        return;

    const char *msg = get_message(message, "AssertGte failed");
    char buf[256];
    snprintf(buf, sizeof(buf), "%s: expected %" PRId64 " >= %" PRId64, msg, a, b);
    rt_trap(buf);
}

/// @brief Assert first value is less than or equal to second.
void rt_diag_assert_lte(int64_t a, int64_t b, rt_string message)
{
    if (a <= b)
        return;

    const char *msg = get_message(message, "AssertLte failed");
    char buf[256];
    snprintf(buf, sizeof(buf), "%s: expected %" PRId64 " <= %" PRId64, msg, a, b);
    rt_trap(buf);
}
