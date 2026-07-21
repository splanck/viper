//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/common/ConstantFolding.hpp
// Purpose: Pure arithmetic constant folding utilities for language frontends.
//
// This header provides language-agnostic constant folding operations that can
// be used by any language frontend. The functions operate on primitive values
// and return optional results (empty on overflow/error).
// Key invariants: Signed operations never execute undefined overflow; invalid
//                 arithmetic and non-representable conversions return nullopt.
// Ownership/Lifetime: Header-only, stateless value utilities.
// Links: docs/il/il-guide.md
//
//===----------------------------------------------------------------------===//
#pragma once

#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>

namespace il::frontends::common::const_fold {

//===----------------------------------------------------------------------===//
// Integer Arithmetic
//===----------------------------------------------------------------------===//

/// @brief Fold integer addition with overflow detection.
/// @return Result if no overflow, empty otherwise.
[[nodiscard]] inline std::optional<int64_t> foldIntAdd(int64_t lhs, int64_t rhs) noexcept {
    if ((rhs > 0 && lhs > std::numeric_limits<int64_t>::max() - rhs) ||
        (rhs < 0 && lhs < std::numeric_limits<int64_t>::min() - rhs))
        return std::nullopt;
    return lhs + rhs;
}

/// @brief Fold integer subtraction with overflow detection.
/// @return Result if no overflow, empty otherwise.
[[nodiscard]] inline std::optional<int64_t> foldIntSub(int64_t lhs, int64_t rhs) noexcept {
    if ((rhs < 0 && lhs > std::numeric_limits<int64_t>::max() + rhs) ||
        (rhs > 0 && lhs < std::numeric_limits<int64_t>::min() + rhs))
        return std::nullopt;
    return lhs - rhs;
}

/// @brief Fold integer multiplication with overflow detection.
/// @return Result if no overflow, empty otherwise.
[[nodiscard]] inline std::optional<int64_t> foldIntMul(int64_t lhs, int64_t rhs) noexcept {
    if (lhs > 0) {
        if ((rhs > 0 && lhs > std::numeric_limits<int64_t>::max() / rhs) ||
            (rhs < 0 && rhs < std::numeric_limits<int64_t>::min() / lhs))
            return std::nullopt;
    } else if (lhs < 0) {
        if ((rhs > 0 && lhs < std::numeric_limits<int64_t>::min() / rhs) ||
            (rhs < 0 && lhs < std::numeric_limits<int64_t>::max() / rhs))
            return std::nullopt;
    }

    if (lhs == std::numeric_limits<int64_t>::min() && rhs == -1)
        return std::nullopt;

    return lhs * rhs;
}

/// @brief Fold integer division with zero check.
/// @return Result if divisor non-zero, empty otherwise.
[[nodiscard]] inline std::optional<int64_t> foldIntDiv(int64_t lhs, int64_t rhs) noexcept {
    if (rhs == 0)
        return std::nullopt;

    // Handle MIN / -1 overflow case
    if (lhs == std::numeric_limits<int64_t>::min() && rhs == -1)
        return std::nullopt;

    return lhs / rhs;
}

/// @brief Fold integer modulo with zero check.
/// @return Result if divisor non-zero, empty otherwise.
[[nodiscard]] inline std::optional<int64_t> foldIntMod(int64_t lhs, int64_t rhs) noexcept {
    if (rhs == 0)
        return std::nullopt;

    if (lhs == std::numeric_limits<int64_t>::min() && rhs == -1)
        return std::nullopt;

    return lhs % rhs;
}

/// @brief Fold integer negation with overflow detection.
/// @return Result if no overflow, empty otherwise.
[[nodiscard]] inline std::optional<int64_t> foldIntNeg(int64_t val) noexcept {
    if (val == std::numeric_limits<int64_t>::min())
        return std::nullopt;

    return -val;
}

/// @brief Fold integer absolute value with overflow detection.
/// @return Result if no overflow, empty otherwise.
[[nodiscard]] inline std::optional<int64_t> foldIntAbs(int64_t val) noexcept {
    if (val == std::numeric_limits<int64_t>::min())
        return std::nullopt;

    return val < 0 ? -val : val;
}

//===----------------------------------------------------------------------===//
// Floating-Point Arithmetic
//===----------------------------------------------------------------------===//

/// @brief Fold floating-point addition.
[[nodiscard]] inline double foldFloatAdd(double lhs, double rhs) noexcept {
    return lhs + rhs;
}

/// @brief Fold floating-point subtraction.
[[nodiscard]] inline double foldFloatSub(double lhs, double rhs) noexcept {
    return lhs - rhs;
}

/// @brief Fold floating-point multiplication.
[[nodiscard]] inline double foldFloatMul(double lhs, double rhs) noexcept {
    return lhs * rhs;
}

/// @brief Fold floating-point division.
/// @return Result, or NaN/Inf for division by zero.
[[nodiscard]] inline double foldFloatDiv(double lhs, double rhs) noexcept {
    return lhs / rhs; // IEEE 754 handles div by zero
}

/// @brief Fold floating-point negation.
[[nodiscard]] inline double foldFloatNeg(double val) noexcept {
    return -val;
}

/// @brief Fold floating-point absolute value.
[[nodiscard]] inline double foldFloatAbs(double val) noexcept {
    return std::fabs(val);
}

/// @brief Fold floating-point power.
[[nodiscard]] inline double foldFloatPow(double base, double exp) noexcept {
    return std::pow(base, exp);
}

/// @brief Fold floating-point square root.
[[nodiscard]] inline double foldFloatSqrt(double val) noexcept {
    return std::sqrt(val);
}

//===----------------------------------------------------------------------===//
// Comparison Operations
//===----------------------------------------------------------------------===//

/// @brief Fold integer comparison (less than).
[[nodiscard]] inline bool foldIntLt(int64_t lhs, int64_t rhs) noexcept {
    return lhs < rhs;
}

/// @brief Fold integer comparison (less than or equal).
[[nodiscard]] inline bool foldIntLe(int64_t lhs, int64_t rhs) noexcept {
    return lhs <= rhs;
}

/// @brief Fold integer comparison (greater than).
[[nodiscard]] inline bool foldIntGt(int64_t lhs, int64_t rhs) noexcept {
    return lhs > rhs;
}

/// @brief Fold integer comparison (greater than or equal).
[[nodiscard]] inline bool foldIntGe(int64_t lhs, int64_t rhs) noexcept {
    return lhs >= rhs;
}

/// @brief Fold integer comparison (equal).
[[nodiscard]] inline bool foldIntEq(int64_t lhs, int64_t rhs) noexcept {
    return lhs == rhs;
}

/// @brief Fold integer comparison (not equal).
[[nodiscard]] inline bool foldIntNe(int64_t lhs, int64_t rhs) noexcept {
    return lhs != rhs;
}

/// @brief Fold floating-point comparison (less than).
[[nodiscard]] inline bool foldFloatLt(double lhs, double rhs) noexcept {
    return lhs < rhs;
}

/// @brief Fold floating-point comparison (less than or equal).
[[nodiscard]] inline bool foldFloatLe(double lhs, double rhs) noexcept {
    return lhs <= rhs;
}

/// @brief Fold floating-point comparison (greater than).
[[nodiscard]] inline bool foldFloatGt(double lhs, double rhs) noexcept {
    return lhs > rhs;
}

/// @brief Fold floating-point comparison (greater than or equal).
[[nodiscard]] inline bool foldFloatGe(double lhs, double rhs) noexcept {
    return lhs >= rhs;
}

/// @brief Fold floating-point comparison (equal).
[[nodiscard]] inline bool foldFloatEq(double lhs, double rhs) noexcept {
    return lhs == rhs;
}

/// @brief Fold floating-point comparison (not equal).
[[nodiscard]] inline bool foldFloatNe(double lhs, double rhs) noexcept {
    return lhs != rhs;
}

//===----------------------------------------------------------------------===//
// Logical Operations
//===----------------------------------------------------------------------===//

/// @brief Fold logical AND.
[[nodiscard]] inline bool foldAnd(bool lhs, bool rhs) noexcept {
    return lhs && rhs;
}

/// @brief Fold logical OR.
[[nodiscard]] inline bool foldOr(bool lhs, bool rhs) noexcept {
    return lhs || rhs;
}

/// @brief Fold logical NOT.
[[nodiscard]] inline bool foldNot(bool val) noexcept {
    return !val;
}

/// @brief Fold logical XOR.
[[nodiscard]] inline bool foldXor(bool lhs, bool rhs) noexcept {
    return lhs != rhs;
}

//===----------------------------------------------------------------------===//
// Bitwise Operations
//===----------------------------------------------------------------------===//

/// @brief Fold bitwise AND.
[[nodiscard]] inline int64_t foldBitAnd(int64_t lhs, int64_t rhs) noexcept {
    return lhs & rhs;
}

/// @brief Fold bitwise OR.
[[nodiscard]] inline int64_t foldBitOr(int64_t lhs, int64_t rhs) noexcept {
    return lhs | rhs;
}

/// @brief Fold bitwise XOR.
[[nodiscard]] inline int64_t foldBitXor(int64_t lhs, int64_t rhs) noexcept {
    return lhs ^ rhs;
}

/// @brief Fold bitwise NOT.
[[nodiscard]] inline int64_t foldBitNot(int64_t val) noexcept {
    return ~val;
}

/// @brief Fold left shift with overflow check.
/// @return Result if shift amount valid, empty otherwise.
[[nodiscard]] inline std::optional<int64_t> foldShl(int64_t val, int64_t shift) noexcept {
    if (shift < 0 || shift >= 64)
        return std::nullopt;

    const auto bits = static_cast<uint64_t>(val) << static_cast<unsigned>(shift);
    return static_cast<int64_t>(bits);
}

/// @brief Fold arithmetic right shift with overflow check.
/// @return Result if shift amount valid, empty otherwise.
[[nodiscard]] inline std::optional<int64_t> foldShr(int64_t val, int64_t shift) noexcept {
    if (shift < 0 || shift >= 64)
        return std::nullopt;

    if (shift == 0)
        return val;
    auto bits = static_cast<uint64_t>(val) >> static_cast<unsigned>(shift);
    if (val < 0)
        bits |= ~uint64_t{0} << (64U - static_cast<unsigned>(shift));
    return static_cast<int64_t>(bits);
}

//===----------------------------------------------------------------------===//
// Type Conversions
//===----------------------------------------------------------------------===//

/// @brief Convert integer to floating-point.
[[nodiscard]] inline double intToFloat(int64_t val) noexcept {
    return static_cast<double>(val);
}

/// @brief Convert floating-point to integer (truncate toward zero).
[[nodiscard]] inline std::optional<int64_t> floatToInt(double val) noexcept {
    if (std::isnan(val) || std::isinf(val))
        return std::nullopt;

    constexpr double kInt64UpperExclusive = 0x1p63;
    constexpr double kInt64LowerInclusive = -0x1p63;
    if (val >= kInt64UpperExclusive || val < kInt64LowerInclusive)
        return std::nullopt;

    return static_cast<int64_t>(val);
}

/// @brief Convert floating-point to integer (floor).
[[nodiscard]] inline std::optional<int64_t> floatFloor(double val) noexcept {
    double floored = std::floor(val);
    return floatToInt(floored);
}

/// @brief Convert floating-point to integer (ceiling).
[[nodiscard]] inline std::optional<int64_t> floatCeil(double val) noexcept {
    double ceiled = std::ceil(val);
    return floatToInt(ceiled);
}

/// @brief Convert floating-point to integer (round to nearest).
[[nodiscard]] inline std::optional<int64_t> floatRound(double val) noexcept {
    double rounded = std::round(val);
    return floatToInt(rounded);
}

} // namespace il::frontends::common::const_fold
