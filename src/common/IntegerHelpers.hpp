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
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include <bit>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace il::common::integer
{

using Value = long long;

/// @brief Indicates how sign-extension should be applied when widening values.
enum class Signedness
{
    Signed,
    Unsigned,
};

/// @brief Selects the behaviour used when narrowing would overflow.
enum class OverflowPolicy
{
    Wrap,     ///< Wrap around modulo 2^n.
    Trap,     ///< Throw std::overflow_error when the value does not fit.
    Saturate, ///< Clamp to the representable range.
};

/// @brief Result of promoting two operands to a common width and signedness.
struct PromotePair
{
    Value lhs{0};                              ///< Left operand after promotion.
    Value rhs{0};                              ///< Right operand after promotion.
    int width{0};                              ///< Common bit-width of the promoted operands.
    Signedness signedness{Signedness::Signed}; ///< Signedness used for promotion.
};

namespace detail
{

[[nodiscard]] inline std::uint64_t mask_for(int bits) noexcept
{
    if (bits <= 0)
    {
        return 0;
    }
    if (bits >= 64)
    {
        return std::numeric_limits<std::uint64_t>::max();
    }
    return (std::uint64_t{1} << static_cast<unsigned>(bits)) - 1U;
}

[[nodiscard]] inline Value min_for(int bits) noexcept
{
    if (bits <= 0)
    {
        return 0;
    }
    if (bits >= 64)
    {
        return std::numeric_limits<Value>::min();
    }
    const std::uint64_t sign = std::uint64_t{1} << static_cast<unsigned>(bits - 1);
    return -static_cast<Value>(sign);
}

[[nodiscard]] inline Value max_for(int bits) noexcept
{
    if (bits <= 0)
    {
        return 0;
    }
    if (bits >= 64)
    {
        return std::numeric_limits<Value>::max();
    }
    const std::uint64_t payload = (std::uint64_t{1} << static_cast<unsigned>(bits - 1)) - 1U;
    return static_cast<Value>(payload);
}

[[nodiscard]] inline int bits_required_signed(Value v) noexcept
{
    if (v == 0 || v == -1)
    {
        return 1;
    }
    for (int bits = 1; bits < 64; ++bits)
    {
        if (v >= min_for(bits) && v <= max_for(bits))
        {
            return bits;
        }
    }
    return 64;
}

[[nodiscard]] inline int bits_required_unsigned(Value v) noexcept
{
    const std::uint64_t u = static_cast<std::uint64_t>(v);
    if (u == 0)
    {
        return 1;
    }
    return 64 - static_cast<int>(std::countl_zero(u));
}

} // namespace detail

/// @brief Widen @p value from @p bits to 64 bits using the requested signedness.
[[nodiscard]] inline Value widen_to(Value value, int bits, Signedness signedness) noexcept
{
    if (bits >= 64)
    {
        return value;
    }

    const std::uint64_t mask = detail::mask_for(bits);
    const std::uint64_t truncated = static_cast<std::uint64_t>(value) & mask;
    if (signedness == Signedness::Unsigned)
    {
        return static_cast<Value>(truncated);
    }
    if (bits <= 0)
    {
        return 0;
    }
    const std::uint64_t signBit = std::uint64_t{1} << static_cast<unsigned>(bits - 1);
    if ((truncated & signBit) == 0)
    {
        return static_cast<Value>(truncated);
    }
    const std::uint64_t extend = detail::mask_for(64) ^ mask;
    return static_cast<Value>(truncated | extend);
}

/// @brief Narrow @p value to @p bits while applying @p policy on overflow.
[[nodiscard]] inline Value narrow_to(Value value, int bits, OverflowPolicy policy)
{
    if (bits >= 64)
    {
        return value;
    }

    const std::uint64_t mask = detail::mask_for(bits);
    const std::uint64_t truncated = static_cast<std::uint64_t>(value) & mask;
    if (policy == OverflowPolicy::Wrap)
    {
        if (bits <= 0)
        {
            return 0;
        }

        const std::uint64_t signBit = std::uint64_t{1} << static_cast<unsigned>(bits - 1);
        if ((truncated & signBit) == 0)
        {
            return static_cast<Value>(truncated);
        }

        const std::uint64_t extend = detail::mask_for(64) ^ mask;
        return static_cast<Value>(truncated | extend);
    }

    const Value min = detail::min_for(bits);
    const Value max = detail::max_for(bits);
    if (value < min)
    {
        if (policy == OverflowPolicy::Trap)
        {
            throw std::overflow_error("integer narrowing underflow");
        }
        return min;
    }
    if (value > max)
    {
        if (policy == OverflowPolicy::Trap)
        {
            throw std::overflow_error("integer narrowing overflow");
        }
        return max;
    }
    return value;
}

/// @brief Promote both operands to a common width and signedness.
[[nodiscard]] inline PromotePair promote_binary(Value lhs, Value rhs) noexcept
{
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

} // namespace il::common::integer
