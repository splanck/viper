//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE in the project root for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/core/rt_trap.c
// Purpose: Fatal runtime trap helpers for the Viper runtime C ABI. Provides
//   narrow convenience wrappers such as rt_trap_div0() and assertion helpers
//   that ultimately route through the structured runtime trap dispatcher.
//   Centralising trap logic here keeps trap kinds, error codes, and messages
//   consistent across the VM, native code, and runtime C shim paths.
//
// Key invariants:
//   - rt_trap(msg) is reserved for unrecoverable conditions such as invariant
//     violations and checked arithmetic faults.
//   - rt_trap_div0() and rt_trap_ovf() preserve the VM-visible trap kind/code
//     expected by diagnostics and trap-recovering tests.
//   - In unit tests, vm_trap()/runtime trap hooks can be overridden so traps
//     are observed without killing the test process.
//
// Ownership/Lifetime:
//   - Helpers may allocate small stack buffers for formatted diagnostics only.
//
// Links: src/runtime/core/rt_trap.h (public API),
//        src/runtime/core/rt_internal.h (rt_trap macro shim)
//
//===----------------------------------------------------------------------===//

#include <inttypes.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rt_error.h"
#include "rt_internal.h"
#include "rt_trap.h"

static void append_escaped_string(char *dst, size_t dst_cap, rt_string s) {
    if (!dst || dst_cap == 0)
        return;
    size_t pos = strlen(dst);
    if (pos >= dst_cap)
        return;

    if (s && !rt_string_is_handle((const void *)s)) {
        snprintf(dst + pos, dst_cap - pos, "<invalid string>");
        return;
    }

    const char *bytes = s ? rt_string_cstr(s) : "";
    size_t len = s ? (size_t)rt_str_len(s) : 0;
    size_t shown = len < 64 ? len : 64;
    for (size_t i = 0; i < shown && pos + 5 < dst_cap; ++i) {
        unsigned char ch = (unsigned char)bytes[i];
        if (ch == '\\' || ch == '"') {
            dst[pos++] = '\\';
            dst[pos++] = (char)ch;
        } else if (ch >= 32 && ch < 127) {
            dst[pos++] = (char)ch;
        } else {
            int n = snprintf(dst + pos, dst_cap - pos, "\\x%02X", ch);
            if (n < 0)
                break;
            pos += (size_t)n;
        }
    }
    if (len > shown && pos + 4 < dst_cap) {
        dst[pos++] = '.';
        dst[pos++] = '.';
        dst[pos++] = '.';
    }
    dst[pos] = '\0';
}

static int message_has_bytes(rt_string message) {
    if (message && !rt_string_is_handle((const void *)message))
        return 0;
    if (!message || !message->data)
        return 0;
    return rt_str_len(message) > 0 ? 1 : 0;
}

static const char *format_message(rt_string message, const char *fallback, char *buf, size_t cap) {
    if (!message_has_bytes(message))
        return fallback;
    if (!buf || cap == 0)
        return fallback;
    buf[0] = '\0';
    append_escaped_string(buf, cap, message);
    return buf;
}

/// @brief Report a division-by-zero trap and terminate the process.
/// @details Prints a fixed diagnostic to stderr, flushes the stream to ensure
///          embedders observe the message, and exits with status code 1.  The
///          behaviour mirrors the VM trap hook so test suites observe consistent
///          failure semantics across execution modes.
void rt_trap_div0(void) {
    rt_trap_raise_kind(RT_TRAP_KIND_DIVIDE_BY_ZERO, 0, -1, "Viper runtime trap: division by zero");
}

/// @brief Report an integer-overflow trap and terminate the process.
/// @details Mirrors the checked-arithmetic trap path used by the VM/native
///          backends so backend lowering can call a no-argument helper.
void rt_trap_ovf(void) {
    rt_trap_raise_kind(
        RT_TRAP_KIND_OVERFLOW, Err_Overflow, -1, "Viper runtime trap: integer overflow");
}

/// @brief Assert that @p condition holds; otherwise trap with @p message.
/// @details When @p condition is zero, evaluates @p message and raises a runtime
///          trap using @ref rt_trap. Empty or null messages use the default
///          text "Assertion failed" to avoid silent failures.
/// @param condition Non-zero when the assertion succeeded.
/// @param message Optional runtime string describing the failure.
void rt_diag_assert(int8_t condition, rt_string message) {
    if (condition)
        return;

    char msg_buf[160];
    rt_trap(format_message(message, "Assertion failed", msg_buf, sizeof(msg_buf)));
}

/// @brief Assert two integers are equal.
void rt_diag_assert_eq(int64_t expected, int64_t actual, rt_string message) {
    if (expected == actual)
        return;

    char msg_buf[160];
    const char *msg = format_message(message, "AssertEq failed", msg_buf, sizeof(msg_buf));
    char buf[256];
    snprintf(buf, sizeof(buf), "%s: expected %" PRId64 ", got %" PRId64, msg, expected, actual);
    rt_trap(buf);
}

/// @brief Assert two integers are not equal.
void rt_diag_assert_neq(int64_t a, int64_t b, rt_string message) {
    if (a != b)
        return;

    char msg_buf[160];
    const char *msg = format_message(message, "AssertNeq failed", msg_buf, sizeof(msg_buf));
    char buf[256];
    snprintf(buf, sizeof(buf), "%s: values should not be equal (both are %" PRId64 ")", msg, a);
    rt_trap(buf);
}

/// @brief Assert two numbers are approximately equal.
void rt_diag_assert_eq_num(double expected, double actual, rt_string message) {
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

    char msg_buf[160];
    const char *msg = format_message(message, "AssertEqNum failed", msg_buf, sizeof(msg_buf));
    char buf[256];
    snprintf(buf, sizeof(buf), "%s: expected %g, got %g (diff=%g)", msg, expected, actual, diff);
    rt_trap(buf);
}

/// @brief Assert two strings are equal.
void rt_diag_assert_eq_str(rt_string expected, rt_string actual, rt_string message) {
    int expected_valid = !expected || rt_string_is_handle((const void *)expected);
    int actual_valid = !actual || rt_string_is_handle((const void *)actual);
    if (expected_valid && actual_valid && rt_str_eq(expected, actual) != 0)
        return;

    char msg_buf[160];
    const char *msg = format_message(message, "AssertEqStr failed", msg_buf, sizeof(msg_buf));
    char buf[512];
    if (!expected_valid || !actual_valid)
        snprintf(buf, sizeof(buf), "%s: invalid string handle; expected \"", msg);
    else
        snprintf(buf, sizeof(buf), "%s: expected \"", msg);
    append_escaped_string(buf, sizeof(buf), expected);
    strncat(buf, "\", got \"", sizeof(buf) - strlen(buf) - 1);
    append_escaped_string(buf, sizeof(buf), actual);
    strncat(buf, "\"", sizeof(buf) - strlen(buf) - 1);
    rt_trap(buf);
}

/// @brief Assert an object reference is null.
void rt_diag_assert_null(void *obj, rt_string message) {
    if (obj == NULL)
        return;

    char msg_buf[160];
    const char *msg = format_message(message, "AssertNull failed", msg_buf, sizeof(msg_buf));
    char buf[256];
    snprintf(buf, sizeof(buf), "%s: expected null, got non-null object", msg);
    rt_trap(buf);
}

/// @brief Assert an object reference is not null.
void rt_diag_assert_not_null(void *obj, rt_string message) {
    if (obj != NULL)
        return;

    char msg_buf[160];
    const char *msg = format_message(message, "AssertNotNull failed", msg_buf, sizeof(msg_buf));
    char buf[256];
    snprintf(buf, sizeof(buf), "%s: expected non-null, got null", msg);
    rt_trap(buf);
}

/// @brief Unconditionally fail with a message.
void rt_diag_assert_fail(rt_string message) {
    char msg_buf[160];
    const char *msg = format_message(message, "AssertFail called", msg_buf, sizeof(msg_buf));
    rt_trap(msg);
}

/// @brief Assert first value is greater than second.
void rt_diag_assert_gt(int64_t a, int64_t b, rt_string message) {
    if (a > b)
        return;

    char msg_buf[160];
    const char *msg = format_message(message, "AssertGt failed", msg_buf, sizeof(msg_buf));
    char buf[256];
    snprintf(buf, sizeof(buf), "%s: expected %" PRId64 " > %" PRId64, msg, a, b);
    rt_trap(buf);
}

/// @brief Assert first value is less than second.
void rt_diag_assert_lt(int64_t a, int64_t b, rt_string message) {
    if (a < b)
        return;

    char msg_buf[160];
    const char *msg = format_message(message, "AssertLt failed", msg_buf, sizeof(msg_buf));
    char buf[256];
    snprintf(buf, sizeof(buf), "%s: expected %" PRId64 " < %" PRId64, msg, a, b);
    rt_trap(buf);
}

/// @brief Assert first value is greater than or equal to second.
void rt_diag_assert_gte(int64_t a, int64_t b, rt_string message) {
    if (a >= b)
        return;

    char msg_buf[160];
    const char *msg = format_message(message, "AssertGte failed", msg_buf, sizeof(msg_buf));
    char buf[256];
    snprintf(buf, sizeof(buf), "%s: expected %" PRId64 " >= %" PRId64, msg, a, b);
    rt_trap(buf);
}

/// @brief Assert first value is less than or equal to second.
void rt_diag_assert_lte(int64_t a, int64_t b, rt_string message) {
    if (a <= b)
        return;

    char msg_buf[160];
    const char *msg = format_message(message, "AssertLte failed", msg_buf, sizeof(msg_buf));
    char buf[256];
    snprintf(buf, sizeof(buf), "%s: expected %" PRId64 " <= %" PRId64, msg, a, b);
    rt_trap(buf);
}
