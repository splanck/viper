//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// This file declares runtime trap handlers for unrecoverable error conditions
// in IL programs. When a program violates fundamental invariants (division by zero,
// array bounds violations, null dereference), execution must terminate immediately
// with a diagnostic message.
//
// The IL uses an explicit error-handling model without exceptions. Instructions
// that can fail (division, array access, file operations) either return error codes
// or trap immediately for unrecoverable conditions. This file provides trap handlers
// for the latter category.
//
// Trap handlers print diagnostic messages to stderr and terminate the process with
// a non-zero exit code. They are designed to be called from IL-generated code and
// runtime library implementations when continuing execution would be unsafe or
// meaningless.
//
// Key Properties:
// - Immediate termination: Trap functions never return to caller
// - Diagnostic output: Each trap prints a descriptive error message before exit
// - Process-wide scope: No attempt at recovery or cleanup beyond basic message printing
// - ABI stability: These functions are part of the runtime's stable C interface
//
// Integration: The IL verifier ensures that paths calling trap functions are properly
// marked as terminating. The codegen backends can optimize subsequent code knowing
// that trap calls do not return.
//
//===----------------------------------------------------------------------===//

#pragma once

#include <stdint.h>

#include "rt_string.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Traps the runtime on division by zero.
    void rt_trap_div0(void);

    /// @brief Trap when @p condition is false with the supplied diagnostic.
    /// @param condition Non-zero when the assertion passes.
    /// @param message Runtime string describing the assertion; falls back to a
    ///        default when empty.
    void rt_diag_assert(int8_t condition, rt_string message);

    /// @brief Assert two integers are equal.
    /// @param expected The expected value.
    /// @param actual The actual value.
    /// @param message Description of what was being tested.
    void rt_diag_assert_eq(int64_t expected, int64_t actual, rt_string message);

    /// @brief Assert two integers are not equal.
    /// @param a First value.
    /// @param b Second value.
    /// @param message Description of what was being tested.
    void rt_diag_assert_neq(int64_t a, int64_t b, rt_string message);

    /// @brief Assert two numbers are approximately equal.
    /// @param expected The expected value.
    /// @param actual The actual value.
    /// @param message Description of what was being tested.
    void rt_diag_assert_eq_num(double expected, double actual, rt_string message);

    /// @brief Assert two strings are equal.
    /// @param expected The expected string.
    /// @param actual The actual string.
    /// @param message Description of what was being tested.
    void rt_diag_assert_eq_str(rt_string expected, rt_string actual, rt_string message);

    /// @brief Assert an object reference is null.
    /// @param obj The object to check.
    /// @param message Description of what was being tested.
    void rt_diag_assert_null(void *obj, rt_string message);

    /// @brief Assert an object reference is not null.
    /// @param obj The object to check.
    /// @param message Description of what was being tested.
    void rt_diag_assert_not_null(void *obj, rt_string message);

    /// @brief Unconditionally fail with a message.
    /// @param message Failure description.
    void rt_diag_assert_fail(rt_string message);

    /// @brief Assert first value is greater than second.
    /// @param a First value.
    /// @param b Second value.
    /// @param message Description of what was being tested.
    void rt_diag_assert_gt(int64_t a, int64_t b, rt_string message);

    /// @brief Assert first value is less than second.
    /// @param a First value.
    /// @param b Second value.
    /// @param message Description of what was being tested.
    void rt_diag_assert_lt(int64_t a, int64_t b, rt_string message);

    /// @brief Assert first value is greater than or equal to second.
    /// @param a First value.
    /// @param b Second value.
    /// @param message Description of what was being tested.
    void rt_diag_assert_gte(int64_t a, int64_t b, rt_string message);

    /// @brief Assert first value is less than or equal to second.
    /// @param a First value.
    /// @param b Second value.
    /// @param message Description of what was being tested.
    void rt_diag_assert_lte(int64_t a, int64_t b, rt_string message);

#ifdef __cplusplus
}
#endif
