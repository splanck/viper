//===----------------------------------------------------------------------===//
//
// File: src/runtime/core/rt_trap.h
// Purpose: Runtime trap handlers for unrecoverable error conditions in IL programs, providing immediate-termination functions for division by zero, bounds violations, and assertion failures.
//
// Key invariants:
//   - Trap functions never return to their caller (noreturn).
//   - Each trap prints a descriptive diagnostic message to stderr before calling exit(1).
//   - rt_diag_assert accepts an int8_t condition; non-zero means the assertion passed.
//   - The ABI of these functions is stable; codegen backends depend on their signatures.
//
// Ownership/Lifetime:
//   - No heap allocation or cleanup is performed beyond printing the diagnostic.
//   - These functions are designed for terminal error conditions only.
//
// Links: src/runtime/core/rt_trap.c (implementation), src/runtime/core/rt_string.h
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
