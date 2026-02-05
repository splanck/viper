//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/il/transform/simplifycfg_values_equal_fp.cpp
// Purpose: Validate SimplifyCFG value comparisons handle floating-point edge cases.
// Key invariants: Floating constants compare using bit patterns preserving NaN payloads and zero
// Ownership/Lifetime: To be documented.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#include "il/core/Value.hpp"
#include "il/transform/SimplifyCFG/Utils.hpp"

#include "tests/TestHarness.hpp"
#include <bit>
#include <cstdint>
#include <limits>

TEST(IL, SimplifyCFGValuesEqualFP)
{
    using il::core::Value;
    using il::transform::simplify_cfg::valuesEqual;

    const Value posZero = Value::constFloat(0.0);
    const Value negZero = Value::constFloat(-0.0);

    ASSERT_TRUE(valuesEqual(posZero, posZero));
    ASSERT_TRUE(valuesEqual(negZero, negZero));
    ASSERT_FALSE(valuesEqual(posZero, negZero));

    constexpr std::uint64_t quietNanBits = 0x7ff8000000000001ULL;
    constexpr std::uint64_t quietNanOtherBits = 0x7ff8000000000002ULL;
    const Value quietNanA = Value::constFloat(std::bit_cast<double>(quietNanBits));
    const Value quietNanB = Value::constFloat(std::bit_cast<double>(quietNanBits));
    const Value quietNanOther = Value::constFloat(std::bit_cast<double>(quietNanOtherBits));

    ASSERT_TRUE(valuesEqual(quietNanA, quietNanB));
    ASSERT_FALSE(valuesEqual(quietNanA, quietNanOther));

    const Value infin = Value::constFloat(std::numeric_limits<double>::infinity());
    ASSERT_FALSE(valuesEqual(quietNanA, infin));
}

int main(int argc, char **argv)
{
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
