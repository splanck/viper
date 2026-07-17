//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//

#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

namespace il::core {

enum class CheckedFPCastFailure {
    None,
    Invalid,
    Overflow,
};

struct CheckedFPCastResult {
    CheckedFPCastFailure failure{CheckedFPCastFailure::None};
    int64_t value{0};

    [[nodiscard]] bool ok() const noexcept {
        return failure == CheckedFPCastFailure::None;
    }
};

inline double roundToNearestTiesToEven(double operand) {
    const bool negative = std::signbit(operand);
    double integral = 0.0;
    const double fractional = std::modf(std::fabs(operand), &integral);

    if (fractional > 0.5) {
        integral += 1.0;
    } else if (fractional == 0.5 && std::fmod(integral, 2.0) != 0.0) {
        integral += 1.0;
    }

    return negative ? -integral : integral;
}

inline double signedLowerBoundForBits(int bits) {
    if (bits >= 64)
        return static_cast<double>(std::numeric_limits<int64_t>::min());
    return -std::ldexp(1.0, bits - 1);
}

inline double signedUpperExclusiveForBits(int bits) {
    if (bits >= 64)
        return 9223372036854775808.0;
    return std::ldexp(1.0, bits - 1);
}

inline double unsignedUpperExclusiveForBits(int bits) {
    if (bits >= 64)
        return 18446744073709551616.0;
    return std::ldexp(1.0, bits);
}

inline CheckedFPCastResult checkedFpToSiRte(double operand, int bits) {
    if (!std::isfinite(operand))
        return {CheckedFPCastFailure::Invalid, 0};

    const double rounded = roundToNearestTiesToEven(operand);
    if (!std::isfinite(rounded) || rounded < signedLowerBoundForBits(bits) ||
        rounded >= signedUpperExclusiveForBits(bits)) {
        return {CheckedFPCastFailure::Overflow, 0};
    }

    return {CheckedFPCastFailure::None, static_cast<int64_t>(rounded)};
}

inline CheckedFPCastResult checkedFpToUiRte(double operand, int bits) {
    if (!std::isfinite(operand) || operand < 0.0)
        return {CheckedFPCastFailure::Invalid, 0};

    if (operand >= unsignedUpperExclusiveForBits(bits))
        return {CheckedFPCastFailure::Overflow, 0};

    const double rounded = roundToNearestTiesToEven(operand);
    if (!std::isfinite(rounded) || rounded >= unsignedUpperExclusiveForBits(bits))
        return {CheckedFPCastFailure::Overflow, 0};

    uint64_t payload = static_cast<uint64_t>(rounded);
    int64_t stored = 0;
    std::memcpy(&stored, &payload, sizeof(stored));
    return {CheckedFPCastFailure::None, stored};
}

} // namespace il::core
