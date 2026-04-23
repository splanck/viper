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
#include <cstdlib>
#include <cstring>
#include <limits>

int main() {
    struct FormatCase {
        double value;
        const char *expected;
    };

    const std::array<FormatCase, 10> cases = {{{0.0, "0"},
                                               {-0.0, "-0"},
                                               {0.5, "0.5"},
                                               {1.5, "1.5"},
                                               {2.5, "2.5"},
                                               {1e20, "1e+20"},
                                               {1e-20, "9.9999999999999995e-21"},
                                               {std::numeric_limits<double>::quiet_NaN(), "NaN"},
                                               {std::numeric_limits<double>::infinity(), "Inf"},
                                               {-std::numeric_limits<double>::infinity(), "-Inf"}}};

    for (const auto &test : cases) {
        char buffer[64] = {};
        rt_format_f64(test.value, buffer, sizeof(buffer));
        assert(std::strcmp(buffer, test.expected) == 0);
    }

    const std::array<double, 7> roundtrip = {{
        0.1,
        1.0 / 3.0,
        std::numeric_limits<double>::min(),
        std::numeric_limits<double>::max(),
        9007199254740992.0,
        1.2345678901234567e-120,
        -1.2345678901234567e120,
    }};

    for (double value : roundtrip) {
        rt_string s = rt_f64_to_str(value);
        const char *text = rt_string_cstr(s);
        char *end = nullptr;
        double parsed = std::strtod(text, &end);
        assert(end != text);
        assert(end && *end == '\0');
        assert(parsed == value);
        rt_string_unref(s);
    }

    return 0;
}
