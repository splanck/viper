//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// This file declares floating-point arithmetic helpers that enforce BASIC language
// domain and error semantics. While the underlying implementation uses IEEE-754
// arithmetic, BASIC requires explicit error handling for domain violations that
// would produce NaN or infinity in C.
//
// BASIC's mathematical functions have well-defined error conditions: negative
// bases with fractional exponents, logarithms of negative numbers, square roots
// of negative values. Standard C library functions return NaN or infinity for
// these cases, propagating special values through subsequent calculations. BASIC
// requires immediate traps for domain errors.
//
// The helpers in this file wrap standard math operations and provide out-parameters
// to signal domain or overflow conditions. The IL lowering from BASIC generates
// code that checks these flags and branches to trap handlers when errors occur,
// maintaining BASIC's error semantics while using efficient floating-point hardware.
//
// Key Design Points:
// - Domain checking: Functions validate inputs and set error flags before computation
// - IEEE-754 preservation: When inputs are valid, results match standard C math library
// - Trap coordination: Error flags integrate with IL's branch-on-condition patterns
//   for efficient error handling without exception overhead
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

#ifdef __cplusplus
}
#endif
