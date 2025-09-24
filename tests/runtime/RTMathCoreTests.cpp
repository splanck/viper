// File: tests/runtime/RTMathCoreTests.cpp
// Purpose: Validate basic math runtime wrappers.
// Key invariants: Results match libm within tolerance.
// Ownership: Uses runtime library.
// Links: docs/runtime-vm.md#runtime-abi
#include "rt_math.h"
#include <cassert>
#include <cmath>

int main()
{
    const double eps = 1e-12;
    assert(std::fabs(rt_sqrt(9.0) - 3.0) < eps);
    assert(std::fabs(rt_floor(3.7) - 3.0) < eps);
    assert(std::fabs(rt_ceil(3.2) - 4.0) < eps);
    assert(std::fabs(rt_sin(0.0) - 0.0) < eps);
    assert(std::fabs(rt_cos(0.0) - 1.0) < eps);
    assert(std::fabs(rt_pow(2.0, 10.0) - 1024.0) < eps);
    assert(rt_abs_i64(-42) == 42);
    assert(std::fabs(rt_abs_f64(-3.5) - 3.5) < eps);
    return 0;
}
