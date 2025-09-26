// File: src/runtime/rt_math.h
// Purpose: Declares portable math helpers for the runtime.
// Key invariants: Functions mirror C math library semantics.
// Ownership/Lifetime: None.
// Links: docs/runtime-vm.md#runtime-abi
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    double rt_sqrt(double);
    double rt_floor(double);
    double rt_ceil(double);
    double rt_sin(double);
    double rt_cos(double);
    long long rt_abs_i64(long long);
    double rt_abs_f64(double);

#ifdef __cplusplus
}
#endif
