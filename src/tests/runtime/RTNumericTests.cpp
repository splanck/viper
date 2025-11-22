//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/runtime/RTNumericTests.cpp
// Purpose: Validate BASIC numeric helper semantics in the runtime library. 
// Key invariants: Banker rounding and conversion overflow reporting behave as specified.
// Ownership/Lifetime: Uses runtime helpers directly.
// Links: docs/specs/numerics.md
//
//===----------------------------------------------------------------------===//

#include "viper/runtime/rt.h"

#include <cassert>

int main()
{
    assert(rt_round_even(2.5, 0) == 2.0);
    assert(rt_round_even(3.5, 0) == 4.0);

    bool ok = true;
    (void)rt_cint_from_double(32767.5, &ok);
    assert(!ok);

    assert(rt_int_floor(-1.5) == -2.0);
    assert(rt_fix_trunc(-1.5) == -1.0);

    return 0;
}
