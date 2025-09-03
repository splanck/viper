// File: tests/runtime/RTRandomTests.cpp
// Purpose: Validate deterministic RNG core.
// Key invariants: Sequence depends solely on seed; outputs are in [0,1).
// Ownership: Uses runtime library.
// Links: docs/runtime-abi.md
#include "rt_random.h"
#include <cassert>
#include <cmath>

int main()
{
    const double expect[] = {
        0.34500051599441928,
        0.75270919858134688,
        0.79574526991954397,
        0.77739245673250346,
    };
    const double eps = 1e-12;
    rt_randomize_i64(1);
    for (int i = 0; i < 4; ++i)
    {
        double x = rt_rnd();
        assert(std::fabs(x - expect[i]) < eps);
        assert(x >= 0.0 && x < 1.0);
    }
    rt_randomize_i64(0);
    double z = rt_rnd();
    assert(std::fabs(z - 0.0) < eps);
    assert(z >= 0.0 && z < 1.0);
    return 0;
}
