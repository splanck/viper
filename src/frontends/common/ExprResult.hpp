//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/common/ExprResult.hpp
// Purpose: Common expression result type for all language frontends.
//
// This type represents the result of lowering an expression: the IL value
// produced and its IL type. All language frontends use this unified
// abstraction for expression lowering results.
//
// Key Invariants:
//   - value is a valid IL Value (temp, const, or global reference)
//   - type matches the IL type of value
// Ownership/Lifetime: Stores IL values and types by value.
// Links: src/il/core/Value.hpp, src/il/core/Type.hpp
//
//===----------------------------------------------------------------------===//
#pragma once

#include "il/core/Type.hpp"
#include "il/core/Value.hpp"

namespace il::frontends::common {

/// @brief Result of lowering an expression to a value and type pair.
/// @details This is the common abstraction shared by all frontends for
///          representing the output of expression lowering.
struct ExprResult {
    il::core::Value value; ///< The lowered value (temp, const, or global).
    il::core::Type type;   ///< The IL type of the value.

    /// @brief Default constructor creates an invalid result.
    ExprResult() = default;

    /// @brief Construct with a value and type.
    ExprResult(il::core::Value v, il::core::Type t) : value(v), type(t) {}

    /// @brief Check if this result has a usable, non-error IL type.
    [[nodiscard]] bool isValid() const noexcept {
        return type.kind != il::core::Type::Kind::Void &&
               type.kind != il::core::Type::Kind::Error;
    }

    /// @brief Check if this is an integer type.
    [[nodiscard]] bool isInteger() const noexcept {
        return type.kind == il::core::Type::Kind::I64 || type.kind == il::core::Type::Kind::I32 ||
               type.kind == il::core::Type::Kind::I16 || type.kind == il::core::Type::Kind::I1;
    }

    /// @brief Check if this is a floating-point type.
    [[nodiscard]] bool isFloat() const noexcept {
        return type.kind == il::core::Type::Kind::F64;
    }

    /// @brief Check if this is a string type.
    [[nodiscard]] bool isString() const noexcept {
        return type.kind == il::core::Type::Kind::Str;
    }

    /// @brief Check if this is a boolean type.
    [[nodiscard]] bool isBool() const noexcept {
        return type.kind == il::core::Type::Kind::I1;
    }

    /// @brief Check if this is a pointer type.
    [[nodiscard]] bool isPointer() const noexcept {
        return type.kind == il::core::Type::Kind::Ptr;
    }
};

} // namespace il::frontends::common
