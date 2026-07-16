//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/common/IntegerHelpers.hpp
// Purpose: Provide reusable helpers for manipulating fixed-width integers while
//          preserving two's-complement semantics.
// Key invariants: Helper functions never trigger undefined behaviour when
//                 operating on signed integers; conversions honour the selected
//                 overflow policy.
// Ownership/Lifetime: Header-only utilities with no dynamic ownership.
// Links: docs/internals/codemap.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include <bit>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace il::common::integer {

using Value = long long;

/// @brief Indicates how sign-extension should be applied when widening values.
enum class Signedness {
    Signed,
    Unsigned,
};

/// @brief Selects the behaviour used when narrowing would overflow.
enum class OverflowPolicy {
    Wrap,     ///< Wrap around modulo 2^n.
    Trap,     ///< Throw std::overflow_error when the value does not fit.
    Saturate, ///< Clamp to the representable range.
};

/// @brief Result of promoting two operands to a common width and signedness.
struct PromotePair {
    Value lhs{0};                              ///< Left operand after promotion.
    Value rhs{0};                              ///< Right operand after promotion.
    int width{0};                              ///< Common bit-width of the promoted operands.
    Signedness signedness{Signedness::Signed}; ///< Signedness used for promotion.
};

namespace detail {

/// @brief Test whether a bit width fits in the 64-bit carrier domain.
/// @param bits Candidate bit width.
/// @return True when @p bits is in the inclusive range 0..64.
[[nodiscard]] inline bool valid_width(int bits) noexcept {
    return bits >= 0 && bits <= 64;
}

/// @brief Enforce the valid bit-width range used by integer helpers.
/// @param bits Candidate bit width.
/// @throws std::invalid_argument when @p bits is outside 0..64.
inline void validate_width(int bits) {
    if (!valid_width(bits)) {
        throw std::invalid_argument("integer bit width must be in the range 0..64");
    }
}

[[nodiscard]] inline std::uint64_t mask_for(int bits) noexcept {
    if (bits <= 0) {
        return 0;
    }
    if (bits >= 64) {
        return std::numeric_limits<std::uint64_t>::max();
    }
    return (std::uint64_t{1} << static_cast<unsigned>(bits)) - 1U;
}

[[nodiscard]] inline Value min_for(int bits) noexcept {
    if (bits <= 0) {
        return 0;
    }
    if (bits >= 64) {
        return std::numeric_limits<Value>::min();
    }
    const std::uint64_t sign = std::uint64_t{1} << static_cast<unsigned>(bits - 1);
    return -static_cast<Value>(sign);
}

[[nodiscard]] inline Value max_for(int bits) noexcept {
    if (bits <= 0) {
        return 0;
    }
    if (bits >= 64) {
        return std::numeric_limits<Value>::max();
    }
    const std::uint64_t payload = (std::uint64_t{1} << static_cast<unsigned>(bits - 1)) - 1U;
    return static_cast<Value>(payload);
}

[[nodiscard]] inline int bits_required_signed(Value v) noexcept {
    if (v == 0 || v == -1) {
        return 1;
    }
    for (int bits = 1; bits < 64; ++bits) {
        if (v >= min_for(bits) && v <= max_for(bits)) {
            return bits;
        }
    }
    return 64;
}

[[nodiscard]] inline int bits_required_unsigned(Value v) noexcept {
    const std::uint64_t u = static_cast<std::uint64_t>(v);
    if (u == 0) {
        return 1;
    }
    return 64 - static_cast<int>(std::countl_zero(u));
}

} // namespace detail

/// @brief Validate that @p bits is representable by the helper APIs.
/// @details Width zero is accepted for callers that intentionally model an
///          empty/truncated value.  Negative widths and widths above 64 are
///          rejected because the helpers store values in a signed 64-bit carrier.
/// @param bits Bit width to validate.
/// @throws std::invalid_argument when @p bits is outside the inclusive range 0..64.
inline void validate_bit_width(int bits) {
    detail::validate_width(bits);
}

/// @brief Return the minimum representable value for a width and signedness.
/// @details Signed widths use two's-complement ranges.  Unsigned widths have a
///          minimum of zero.  Width zero also returns zero.
/// @param bits Bit width in the inclusive range 0..64.
/// @param signedness Interpretation used for the representable range.
/// @return Minimum value representable by the requested integer domain.
/// @throws std::invalid_argument when @p bits is outside 0..64.
[[nodiscard]] inline Value min_value_for(int bits, Signedness signedness) {
    detail::validate_width(bits);
    if (signedness == Signedness::Unsigned) {
        return 0;
    }
    return detail::min_for(bits);
}

/// @brief Return the maximum representable value for a width and signedness.
/// @details The helper returns the largest value expressible in the signed
///          64-bit carrier type.  For unsigned 64-bit domains this means
///          @c std::numeric_limits<Value>::max() because values above that
///          cannot be represented by @ref Value.
/// @param bits Bit width in the inclusive range 0..64.
/// @param signedness Interpretation used for the representable range.
/// @return Maximum value representable by the requested integer domain.
/// @throws std::invalid_argument when @p bits is outside 0..64.
[[nodiscard]] inline Value max_value_for(int bits, Signedness signedness) {
    detail::validate_width(bits);
    if (signedness == Signedness::Signed) {
        return detail::max_for(bits);
    }
    if (bits <= 0) {
        return 0;
    }
    if (bits >= 63) {
        return std::numeric_limits<Value>::max();
    }
    return static_cast<Value>(detail::mask_for(bits));
}

/// @brief Widen @p value from @p bits to 64 bits using the requested signedness.
/// @param value Source value whose low @p bits should be interpreted.
/// @param bits Source bit width in the inclusive range 0..64.
/// @param signedness Whether to sign-extend or zero-extend the low bits.
/// @return 64-bit carrier value after extension.
/// @throws std::invalid_argument when @p bits is outside 0..64.
[[nodiscard]] inline Value widen_to(Value value, int bits, Signedness signedness) {
    detail::validate_width(bits);
    if (bits >= 64) {
        return value;
    }

    const std::uint64_t mask = detail::mask_for(bits);
    const std::uint64_t truncated = static_cast<std::uint64_t>(value) & mask;
    if (signedness == Signedness::Unsigned) {
        return static_cast<Value>(truncated);
    }
    if (bits <= 0) {
        return 0;
    }
    const std::uint64_t signBit = std::uint64_t{1} << static_cast<unsigned>(bits - 1);
    if ((truncated & signBit) == 0) {
        return static_cast<Value>(truncated);
    }
    const std::uint64_t extend = detail::mask_for(64) ^ mask;
    return static_cast<Value>(truncated | extend);
}

/// @brief Narrow @p value to @p bits using an explicit signedness.
/// @details Wrap mode truncates modulo 2^bits and then interprets the result
///          according to @p signedness.  Trap and saturate modes compare against
///          the explicit signed or unsigned representable range.
/// @param value Source value in the 64-bit carrier type.
/// @param bits Destination bit width in the inclusive range 0..64.
/// @param signedness Interpretation used for the narrowed result.
/// @param policy Overflow handling policy.
/// @return Narrowed value after applying @p policy.
/// @throws std::invalid_argument when @p bits is outside 0..64.
/// @throws std::overflow_error when @p policy is @ref OverflowPolicy::Trap and
///         @p value is outside the destination range.
[[nodiscard]] inline Value narrow_to(Value value,
                                     int bits,
                                     Signedness signedness,
                                     OverflowPolicy policy) {
    detail::validate_width(bits);
    if (bits >= 64) {
        if (signedness == Signedness::Unsigned && value < 0) {
            if (policy == OverflowPolicy::Trap) {
                throw std::overflow_error("integer narrowing unsigned underflow");
            }
            if (policy == OverflowPolicy::Saturate) {
                return 0;
            }
        }
        return value;
    }

    const std::uint64_t mask = detail::mask_for(bits);
    const std::uint64_t truncated = static_cast<std::uint64_t>(value) & mask;
    if (policy == OverflowPolicy::Wrap) {
        if (bits <= 0) {
            return 0;
        }
        if (signedness == Signedness::Unsigned) {
            return static_cast<Value>(truncated);
        }

        const std::uint64_t signBit = std::uint64_t{1} << static_cast<unsigned>(bits - 1);
        if ((truncated & signBit) == 0) {
            return static_cast<Value>(truncated);
        }

        const std::uint64_t extend = detail::mask_for(64) ^ mask;
        return static_cast<Value>(truncated | extend);
    }

    const Value min = min_value_for(bits, signedness);
    const Value max = max_value_for(bits, signedness);
    if (value < min) {
        if (policy == OverflowPolicy::Trap) {
            throw std::overflow_error("integer narrowing underflow");
        }
        return min;
    }
    if (value > max) {
        if (policy == OverflowPolicy::Trap) {
            throw std::overflow_error("integer narrowing overflow");
        }
        return max;
    }
    return value;
}

/// @brief Narrow @p value to @p bits using signed two's-complement semantics.
/// @details This compatibility overload preserves the historical signed
///          interpretation.  New code that is working with unsigned values
///          should call the overload that accepts @ref Signedness explicitly.
/// @param value Source value in the 64-bit carrier type.
/// @param bits Destination bit width in the inclusive range 0..64.
/// @param policy Overflow handling policy.
/// @return Narrowed signed value after applying @p policy.
/// @throws std::invalid_argument when @p bits is outside 0..64.
/// @throws std::overflow_error when @p policy is @ref OverflowPolicy::Trap and
///         @p value is outside the signed destination range.
[[nodiscard]] inline Value narrow_to(Value value, int bits, OverflowPolicy policy) {
    return narrow_to(value, bits, Signedness::Signed, policy);
}

/// @brief Promote both operands to a common width and signedness.
/// @details This compatibility overload infers signedness from the runtime
///          values.  Prefer the explicit-signedness overload when source types
///          are known because non-negative signed and unsigned values otherwise
///          look identical in the carrier type.
/// @param lhs Left operand.
/// @param rhs Right operand.
/// @return Both operands widened to the inferred common domain.
[[nodiscard]] inline PromotePair promote_binary(Value lhs, Value rhs) {
    PromotePair out{};
    out.signedness = (lhs < 0 || rhs < 0) ? Signedness::Signed : Signedness::Unsigned;

    const int lhsBits = (out.signedness == Signedness::Signed)
                            ? detail::bits_required_signed(lhs)
                            : detail::bits_required_unsigned(lhs);
    const int rhsBits = (out.signedness == Signedness::Signed)
                            ? detail::bits_required_signed(rhs)
                            : detail::bits_required_unsigned(rhs);
    out.width = lhsBits > rhsBits ? lhsBits : rhsBits;

    const int promotedBits = out.width > 64 ? 64 : out.width;
    out.lhs = widen_to(lhs, promotedBits, out.signedness);
    out.rhs = widen_to(rhs, promotedBits, out.signedness);
    return out;
}

/// @brief Promote both operands to a common width with explicit signedness.
/// @details The required width is computed using @p signedness instead of
///          guessing from operand values.  This is the preferred overload for
///          frontend and backend code that knows the source integer domain.
/// @param lhs Left operand.
/// @param rhs Right operand.
/// @param signedness Signedness to use while computing required widths.
/// @return Both operands widened to the requested common domain.
[[nodiscard]] inline PromotePair promote_binary(Value lhs, Value rhs, Signedness signedness) {
    PromotePair out{};
    out.signedness = signedness;
    const int lhsBits = (signedness == Signedness::Signed) ? detail::bits_required_signed(lhs)
                                                          : detail::bits_required_unsigned(lhs);
    const int rhsBits = (signedness == Signedness::Signed) ? detail::bits_required_signed(rhs)
                                                          : detail::bits_required_unsigned(rhs);
    out.width = lhsBits > rhsBits ? lhsBits : rhsBits;
    const int promotedBits = out.width > 64 ? 64 : out.width;
    out.lhs = widen_to(lhs, promotedBits, signedness);
    out.rhs = widen_to(rhs, promotedBits, signedness);
    return out;
}

} // namespace il::common::integer
