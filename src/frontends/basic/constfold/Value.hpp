//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Shared literal representation used by the BASIC constant folder.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Defines the lightweight value container used by folding helpers.
/// @details BASIC folding routines operate on small tagged scalars that model
///          integer and floating-point literals.  The helpers in this header
///          provide a consistent representation alongside promotion utilities
///          that obey the language's suffix rules.  Keeping the primitives in a
///          single translation unit avoids subtle drift between arithmetic and
///          comparison folders.

#pragma once

#include "frontends/basic/constfold/Dispatch.hpp"

#include <utility>

namespace il::frontends::basic::constfold
{

/// @brief Kind tags understood by the constant-folding helpers.
enum class ValueKind
{
    Int,
    Float,
};

/// @brief Lightweight tagged scalar used by arithmetic and comparison folders.
struct Value
{
    ValueKind kind = ValueKind::Int; ///< Representation tag of the payload.
    double f = 0.0;                  ///< Floating payload (always finite).
    long long i = 0;                 ///< Integer payload using two's-complement.
    bool valid = false;              ///< Indicates whether the value is usable.

    /// @brief Factory for invalid values used to signal folding failures.
    static constexpr Value invalid() noexcept
    {
        return Value{ValueKind::Int, 0.0, 0, false};
    }

    /// @brief Construct an integer literal.
    static constexpr Value fromInt(long long v) noexcept
    {
        return Value{ValueKind::Int, static_cast<double>(v), v, true};
    }

    /// @brief Construct a floating-point literal.
    static constexpr Value fromFloat(double v) noexcept
    {
        return Value{ValueKind::Float, v, static_cast<long long>(v), true};
    }

    /// @brief Query whether the payload models a float.
    [[nodiscard]] constexpr bool isFloat() const noexcept
    {
        return valid && kind == ValueKind::Float;
    }

    /// @brief Query whether the payload models an integer.
    [[nodiscard]] constexpr bool isInt() const noexcept
    {
        return valid && kind == ValueKind::Int;
    }

    /// @brief Obtain the value as a double regardless of representation.
    [[nodiscard]] constexpr double asDouble() const noexcept
    {
        return kind == ValueKind::Float ? f : static_cast<double>(i);
    }
};

/// @brief Convert @p numeric into a folding value.
[[nodiscard]] inline Value makeValue(const NumericValue &numeric) noexcept
{
    return numeric.isFloat ? Value::fromFloat(numeric.f) : Value::fromInt(numeric.i);
}

/// @brief Convert @p value back into the dispatcher representation.
[[nodiscard]] inline NumericValue toNumericValue(const Value &value) noexcept
{
    NumericValue numeric;
    numeric.isFloat = value.isFloat();
    numeric.f = value.isFloat() ? value.f : static_cast<double>(value.i);
    numeric.i = value.i;
    return numeric;
}

/// @brief Promote @p lhs and @p rhs following BASIC's suffix rules.
[[nodiscard]] inline std::pair<Value, Value> promote(Value lhs, Value rhs) noexcept
{
    if (!lhs.valid || !rhs.valid)
        return {Value::invalid(), Value::invalid()};
    if (lhs.isFloat() || rhs.isFloat())
    {
        if (!lhs.isFloat())
            lhs = Value::fromFloat(static_cast<double>(lhs.i));
        if (!rhs.isFloat())
            rhs = Value::fromFloat(static_cast<double>(rhs.i));
    }
    return {lhs, rhs};
}

} // namespace il::frontends::basic::constfold

