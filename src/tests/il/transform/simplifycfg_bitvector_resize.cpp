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

#include "tests/TestHarness.hpp"

TEST(IL, SimplifyCFGBitVectorResize)
{
    using il::transform::simplify_cfg::BitVector;

    BitVector bits(2);
    bits.set(0);

    bits.resize(5, true);
    ASSERT_EQ(bits.size(), 5);
    ASSERT_TRUE(bits.test(0));
    ASSERT_TRUE(bits.test(2));
    ASSERT_TRUE(bits.test(4));

    bits.set(1);
    bits.resize(3);
    ASSERT_EQ(bits.size(), 3);
    ASSERT_TRUE(bits.test(0));
    ASSERT_TRUE(bits.test(1));
}

int main(int argc, char **argv)
{
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
