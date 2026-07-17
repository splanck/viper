//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_integer_helpers.cpp
// Purpose: Exercise il::common::integer helpers with targeted invariants.
// Key invariants: Wrapping conversions must sign-extend for negative results.
// Ownership/Lifetime: Standalone unit test binary.
// Links: docs/internals/codemap.md
//
//===----------------------------------------------------------------------===//

#include "common/IntegerHelpers.hpp"

#include <cassert>
#include <stdexcept>

using il::common::integer::narrow_to;
using il::common::integer::OverflowPolicy;
using il::common::integer::Signedness;
using il::common::integer::Value;
using il::common::integer::widen_to;

int main() {
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

    const Value signed_wrapped_255 = narrow_to(255, 8, Signedness::Signed, OverflowPolicy::Wrap);
    const Value unsigned_wrapped_255 =
        narrow_to(255, 8, Signedness::Unsigned, OverflowPolicy::Wrap);
    assert(signed_wrapped_255 == -1);
    assert(unsigned_wrapped_255 == 255);

    bool trapped_unsigned_underflow = false;
    try {
        (void)narrow_to(-1, 8, Signedness::Unsigned, OverflowPolicy::Trap);
    } catch (const std::overflow_error &) {
        trapped_unsigned_underflow = true;
    }
    assert(trapped_unsigned_underflow);

    const Value saturated_unsigned_underflow =
        narrow_to(-1, 8, Signedness::Unsigned, OverflowPolicy::Saturate);
    assert(saturated_unsigned_underflow == 0);

    bool rejected_negative_width = false;
    try {
        (void)narrow_to(1, -1, OverflowPolicy::Wrap);
    } catch (const std::invalid_argument &) {
        rejected_negative_width = true;
    }
    assert(rejected_negative_width);

    bool rejected_oversized_width = false;
    try {
        (void)widen_to(1, 65, Signedness::Unsigned);
    } catch (const std::invalid_argument &) {
        rejected_oversized_width = true;
    }
    assert(rejected_oversized_width);

    return 0;
}
