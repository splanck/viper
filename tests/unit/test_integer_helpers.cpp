//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_integer_helpers.cpp
// Purpose: Exercise il::common::integer helpers with targeted invariants.
// Key invariants: Wrapping conversions must sign-extend for negative results.
// Ownership/Lifetime: Standalone unit test binary.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#include "common/IntegerHelpers.hpp"

#include <cassert>

using il::common::integer::narrow_to;
using il::common::integer::OverflowPolicy;
using il::common::integer::Value;

int main()
{
    const Value neg_one = -1;
    const Value neg_two = -2;
    const Value neg_sixty_five = -65;

    const Value wrapped8 = narrow_to(neg_one, 8, OverflowPolicy::Wrap);
    const Value wrapped12 = narrow_to(neg_two, 12, OverflowPolicy::Wrap);
    const Value wrapped17 = narrow_to(neg_sixty_five, 17, OverflowPolicy::Wrap);

    assert(wrapped8 == neg_one);
    assert(wrapped12 == neg_two);
    assert(wrapped17 == neg_sixty_five);

    const Value wrapped_zero_bits = narrow_to(neg_one, 0, OverflowPolicy::Wrap);
    assert(wrapped_zero_bits == 0);

    return 0;
}
