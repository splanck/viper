//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/fuzz/fuzz_plural_rule_parser.cpp
// Purpose: libFuzzer harness exercising Zanna.Localization.PluralRules on
//          the baked en-US cardinal/ordinal rule chains across a wide range
//          of numeric inputs. The rule AST is static; the fuzz coverage
//          focuses on operand computation (n/i/v/f/t) for arbitrary double
//          and int bit patterns — cases where unusual inputs might break
//          classify or exponent parsing paths.
// Key invariants:
//   - Input capped at 16 KB.
//   - Each query must terminate; the evaluator is O(AST size) per call.
// Links: src/runtime/localization/rt_plural_rules.h
//
//===----------------------------------------------------------------------===//

#include "rt_locale.h"
#include "rt_plural_rules.h"
#include "rt_string.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    constexpr size_t kMaxInputSize = 16 * 1024;
    if (size > kMaxInputSize)
        return 0;

    rt_string tag = rt_string_from_bytes("en-US", 5);
    void *loc = rt_locale_parse(tag);
    rt_string_unref(tag);
    void *pr = rt_plural_rules_for_locale(loc);

    // Slice input into 8-byte chunks, interpret each as alternating int64 and
    // double to exercise both paths.
    size_t i = 0;
    bool use_double = false;
    while (i + 8 <= size) {
        if (use_double) {
            double v;
            memcpy(&v, data + i, 8);
            rt_string r = rt_plural_rules_cardinal(pr, v);
            rt_string_unref(r);
        } else {
            int64_t n;
            memcpy(&n, data + i, 8);
            rt_string r1 = rt_plural_rules_cardinal_int(pr, n);
            rt_string r2 = rt_plural_rules_ordinal(pr, n);
            rt_string_unref(r1);
            rt_string_unref(r2);
        }
        use_double = !use_double;
        i += 8;
    }

    return 0;
}
