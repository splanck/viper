// File: tests/runtime/RTRandomTests.cpp
// Purpose: Validate deterministic LCG random generator.
// Key invariants: Sequence reproducible for given seed; outputs in [0,1).
// Ownership: Uses runtime library.
// Links: docs/runtime-vm.md#runtime-abi
#include "viper/runtime/rt.h"
#include <cassert>

int main()
{
    const double expected[] = {0.3450005159944193, 0.7527091985813469, 0.795745269919544};
    rt_randomize_i64(1);
    for (int i = 0; i < 3; ++i)
    {
        double x = rt_rnd();
        assert(x == expected[i]);
    }

    rt_randomize_u64(0xDEADBEEFCAFEBABEULL);
    double first = rt_rnd();
    assert(first == 0.5272554727616845);

    rt_randomize_i64(1);
    for (int i = 0; i < 100; ++i)
    {
        double x = rt_rnd();
        assert(x >= 0.0);
        assert(x < 1.0);
    }
    return 0;
}
