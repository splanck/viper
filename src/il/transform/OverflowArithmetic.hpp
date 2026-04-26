//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: il/transform/OverflowArithmetic.hpp
// Purpose: Portable overflow-checked arithmetic for long long values.
//          On GCC/Clang, delegates to __builtin_*_overflow.
//          On MSVC, provides equivalent manual implementations.
// Key invariants:
//   - Semantics match __builtin_*_overflow: returns true on overflow.
//   - Result is always written (even on overflow, wraps like unsigned).
// Ownership/Lifetime: Header-only, no state.
// Links: il/transform/ConstFold.cpp, il/transform/SCCP.cpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include <limits>

#ifdef _MSC_VER

namespace il::transform::detail {

inline bool overflow_add(long long a, long long b, long long *result) {
    *result = static_cast<long long>(static_cast<unsigned long long>(a) +
                                     static_cast<unsigned long long>(b));
    if (b >= 0)
        return *result < a;
    else
        return *result > a;
}

inline bool overflow_sub(long long a, long long b, long long *result) {
    *result = static_cast<long long>(static_cast<unsigned long long>(a) -
                                     static_cast<unsigned long long>(b));
    if (b >= 0)
        return *result > a;
    else
        return *result < a;
}

inline bool overflow_mul(long long a, long long b, long long *result) {
    const auto ua = static_cast<unsigned long long>(a);
    const auto ub = static_cast<unsigned long long>(b);
    *result = static_cast<long long>(ua * ub);

    if (a == 0 || b == 0) {
        return false;
    }

    const long long max = (std::numeric_limits<long long>::max)();
    const long long min = (std::numeric_limits<long long>::min)();

    if (a > 0)
        return b > 0 ? a > max / b : b < min / a;
    return b > 0 ? a < min / b : b < max / a;
}

} // namespace il::transform::detail

#define __builtin_add_overflow(a, b, r) il::transform::detail::overflow_add(a, b, r)
#define __builtin_sub_overflow(a, b, r) il::transform::detail::overflow_sub(a, b, r)
#define __builtin_mul_overflow(a, b, r) il::transform::detail::overflow_mul(a, b, r)

#endif // _MSC_VER
