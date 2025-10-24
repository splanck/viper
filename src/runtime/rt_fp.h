// File: src/runtime/rt_fp.h
// Purpose: Declares floating-point helpers enforcing BASIC domain semantics.
// Key invariants: Helpers preserve IEEE-754 defaults and report domain errors via ok flags.
// Ownership/Lifetime: None.
// Links: docs/specs/numerics.md
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
