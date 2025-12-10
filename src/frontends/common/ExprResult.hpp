//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/common/ExprResult.hpp
// Purpose: Common expression result type for all language frontends.
//
// This type represents the result of lowering an expression: the IL value
// produced and its IL type. Both BASIC (RVal) and Pascal (LowerResult) use
// essentially the same structure, so this provides a unified abstraction.
//
// Key Invariants:
//   - value is a valid IL Value (temp, const, or global reference)
//   - type matches the IL type of value
//
//===----------------------------------------------------------------------===//
#pragma once

#include "il/core/Type.hpp"
#include "il/core/Value.hpp"

namespace il::frontends::common
{

/// @brief Result of lowering an expression to a value and type pair.
/// @details This is the common abstraction shared by all frontends for
///          representing the output of expression lowering.
struct ExprResult
{
    il::core::Value value; ///< The lowered value (temp, const, or global).
    il::core::Type type;   ///< The IL type of the value.

    /// @brief Default constructor creates an invalid result.
    ExprResult() = default;

    /// @brief Construct with a value and type.
    ExprResult(il::core::Value v, il::core::Type t) : value(v), type(t) {}

    /// @brief Check if this result is valid (has a non-void type or is a constant).
    [[nodiscard]] bool isValid() const noexcept
    {
        return type.kind != il::core::Type::Kind::Void ||
               value.kind == il::core::Value::Kind::ConstInt ||
               value.kind == il::core::Value::Kind::ConstFloat;
    }

    /// @brief Check if this is an integer type.
    [[nodiscard]] bool isInteger() const noexcept
    {
        return type.kind == il::core::Type::Kind::I64 || type.kind == il::core::Type::Kind::I32 ||
               type.kind == il::core::Type::Kind::I16 || type.kind == il::core::Type::Kind::I1;
    }

    /// @brief Check if this is a floating-point type.
    [[nodiscard]] bool isFloat() const noexcept
    {
        return type.kind == il::core::Type::Kind::F64;
    }

    /// @brief Check if this is a string type.
    [[nodiscard]] bool isString() const noexcept
    {
        return type.kind == il::core::Type::Kind::Str;
    }

    /// @brief Check if this is a boolean type.
    [[nodiscard]] bool isBool() const noexcept
    {
        return type.kind == il::core::Type::Kind::I1;
    }

    /// @brief Check if this is a pointer type.
    [[nodiscard]] bool isPointer() const noexcept
    {
        return type.kind == il::core::Type::Kind::Ptr;
    }
};

} // namespace il::frontends::common
