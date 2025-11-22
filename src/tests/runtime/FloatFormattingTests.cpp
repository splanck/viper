//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/runtime/FloatFormattingTests.cpp
// Purpose: Verify deterministic runtime formatting for floating-point values. 
// Key invariants: Canonical spellings are produced regardless of special cases.
// Ownership/Lifetime: Runtime numeric formatting helpers.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#include "viper/runtime/rt.h"

#include <array>
#include <cassert>
#include <cstring>
#include <limits>

int main()
{
    struct FormatCase
    {
        double value;
        const char *expected;
    };

    const std::array<FormatCase, 10> cases = {{{0.0, "0"},
                                               {-0.0, "-0"},
                                               {0.5, "0.5"},
                                               {1.5, "1.5"},
                                               {2.5, "2.5"},
                                               {1e20, "1e+20"},
                                               {1e-20, "1e-20"},
                                               {std::numeric_limits<double>::quiet_NaN(), "NaN"},
                                               {std::numeric_limits<double>::infinity(), "Inf"},
                                               {-std::numeric_limits<double>::infinity(), "-Inf"}}};

    for (const auto &test : cases)
    {
        char buffer[64] = {};
        rt_format_f64(test.value, buffer, sizeof(buffer));
        assert(std::strcmp(buffer, test.expected) == 0);
    }

    return 0;
}
