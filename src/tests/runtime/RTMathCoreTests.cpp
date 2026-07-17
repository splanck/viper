//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/runtime/RTMathCoreTests.cpp
// Purpose: Validate basic math runtime wrappers.
// Key invariants: Results match libm within tolerance.
// Ownership/Lifetime: Uses runtime library.
// Links: docs/runtime-vm.md#runtime-abi
//
//===----------------------------------------------------------------------===//

#include "zanna/runtime/rt.h"
#include <cassert>
#include <cmath>
#include <limits>

int main() {
    const double eps = 1e-12;
    assert(std::fabs(rt_sqrt(9.0) - 3.0) < eps);
    assert(std::fabs(rt_floor(3.7) - 3.0) < eps);
    assert(std::fabs(rt_ceil(3.2) - 4.0) < eps);
    assert(std::fabs(rt_sin(0.0) - 0.0) < eps);
    assert(std::fabs(rt_cos(0.0) - 1.0) < eps);
    bool ok = true;
    assert(std::fabs(rt_pow_f64_chkdom(2.0, 10.0, &ok) - 1024.0) < eps);
    assert(ok);
    ok = true;
    (void)rt_pow_f64_chkdom(-2.0, 0.5, &ok);
    assert(!ok);
    assert(rt_abs_i64(-42) == 42);
    assert(std::fabs(rt_abs_f64(-3.5) - 3.5) < eps);

    double nan = std::numeric_limits<double>::quiet_NaN();
    assert(std::isnan(rt_min_f64(nan, 5.0)));
    assert(std::isnan(rt_min_f64(5.0, nan)));
    assert(std::isnan(rt_max_f64(nan, 5.0)));
    assert(std::isnan(rt_max_f64(5.0, nan)));
    double min_zero = rt_min_f64(+0.0, -0.0);
    double max_zero = rt_max_f64(-0.0, +0.0);
    assert(min_zero == 0.0 && std::signbit(min_zero));
    assert(max_zero == 0.0 && !std::signbit(max_zero));
    return 0;
}
