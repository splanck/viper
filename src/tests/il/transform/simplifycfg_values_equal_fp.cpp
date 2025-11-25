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

#include <bit>
#include <cassert>
#include <cstdint>
#include <limits>

int main()
{
    using il::core::Value;
    using il::transform::simplify_cfg::valuesEqual;

    const Value posZero = Value::constFloat(0.0);
    const Value negZero = Value::constFloat(-0.0);

    assert(valuesEqual(posZero, posZero) && "+0.0 should equal itself");
    assert(valuesEqual(negZero, negZero) && "-0.0 should equal itself");
    assert(!valuesEqual(posZero, negZero) && "Signed zeros must remain distinguishable");

    constexpr std::uint64_t quietNanBits = 0x7ff8000000000001ULL;
    constexpr std::uint64_t quietNanOtherBits = 0x7ff8000000000002ULL;
    const Value quietNanA = Value::constFloat(std::bit_cast<double>(quietNanBits));
    const Value quietNanB = Value::constFloat(std::bit_cast<double>(quietNanBits));
    const Value quietNanOther = Value::constFloat(std::bit_cast<double>(quietNanOtherBits));

    assert(valuesEqual(quietNanA, quietNanB) && "Identical NaN payloads should compare equal");
    assert(!valuesEqual(quietNanA, quietNanOther) && "Distinct NaN payloads must remain unique");

    const Value infin = Value::constFloat(std::numeric_limits<double>::infinity());
    assert(!valuesEqual(quietNanA, infin) && "NaN must not compare equal to infinity");

    return 0;
}
