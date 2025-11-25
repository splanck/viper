//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/il/transform/simplifycfg_bitvector_resize.cpp
// Purpose: Validate SimplifyCFG utility BitVector resize preserves set bits.
// Key invariants: Growing fills new bits with the provided value; shrinking keeps leading bits.
// Ownership/Lifetime: Operates on a local BitVector instance within the test.
// Links: docs/il-guide.md#reference
//
//===----------------------------------------------------------------------===//

#include "il/transform/SimplifyCFG/Utils.hpp"

#include <cassert>

int main()
{
    using il::transform::simplify_cfg::BitVector;

    BitVector bits(2);
    bits.set(0);

    bits.resize(5, true);
    assert(bits.size() == 5);
    assert(bits.test(0));
    assert(bits.test(2));
    assert(bits.test(4));

    bits.set(1);
    bits.resize(3);
    assert(bits.size() == 3);
    assert(bits.test(0));
    assert(bits.test(1));

    return 0;
}
