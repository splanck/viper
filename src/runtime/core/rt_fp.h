//===----------------------------------------------------------------------===//
//
// File: src/runtime/core/rt_fp.h
// Purpose: Floating-point arithmetic helpers enforcing BASIC domain and overflow semantics via an
// out-parameter ok flag, wrapping IEEE-754 power operations with input validation for trap-based
// error handling.
//
// Key invariants:
//   - When *ok is true the result matches the C99 math library pow().
//   - When *ok is false the result is unspecified and the caller must trap.
//   - Valid inputs (non-negative base, finite exponent) never set *ok to false.
//   - rt_math_pow is the simple 2-arg form for IL calling convention; no domain checks.
//
// Ownership/Lifetime:
//   - All parameters are plain values or pointers to caller-owned booleans.
//   - No heap allocation occurs.
//
// Links: src/runtime/core/rt_fp.c (implementation)
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Compute base^exp while tracking domain/overflow conditions.
    /// @param base Input base promoted to f64.
    /// @param exp Input exponent promoted to f64.
    /// @param ok Output flag set to false when DomainError or Overflow should trap.
    /// @return Power result when @p ok is true; unspecified when false.
    double rt_pow_f64_chkdom(double base, double exp, bool *ok);

    /// @brief Simple 2-arg pow for IL calling convention (no domain checks).
    double rt_math_pow(double base, double exponent);

    /// @brief 2-arg pow with BASIC domain checking for native codegen.
    double rt_pow_f64(double base, double exponent);

#ifdef __cplusplus
}
#endif
