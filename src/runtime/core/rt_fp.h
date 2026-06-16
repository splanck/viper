//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
// File: src/runtime/core/rt_fp.h
// Purpose: Floating-point arithmetic helpers enforcing BASIC domain and overflow semantics via an
// out-parameter ok flag, wrapping IEEE-754 power operations with input validation for trap-based
// error handling.
//
// Key invariants:
//   - When *ok is true the result matches the standard C math library pow().
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
extern "C" {
#endif

/// @brief Compute base^exp while tracking domain/overflow conditions.
/// @param base Input base promoted to f64.
/// @param exp Input exponent promoted to f64.
/// @param ok Output flag set to false when DomainError or Overflow should trap.
/// @return Power result when @p ok is true; unspecified when false.
double rt_pow_f64_chkdom(double base, double exp, bool *ok);

/// @brief Simple 2-arg `pow` for the IL `Math.Pow` calling convention.
/// @details Forwards directly to the C library `pow`; performs no domain
///          validation. Used by IL paths that do their own overflow check
///          before calling.
/// @param base     Base value.
/// @param exponent Exponent value.
/// @return `base ^ exponent` per IEEE-754 (may be NaN / Inf without trapping).
double rt_math_pow(double base, double exponent);

/// @brief 2-arg `pow` with BASIC domain checking for native codegen entries.
/// @details Wraps `rt_pow_f64_chkdom` and traps on the failure path so the
///          caller doesn't have to thread an `ok` flag through the IL.
///          Native codegen uses this entry directly from lowered BASIC `^` ops.
/// @param base     Base value.
/// @param exponent Exponent value.
/// @return Power result; traps with `DomainError` / `Overflow` on invalid input.
double rt_pow_f64(double base, double exponent);

#ifdef __cplusplus
}
#endif
