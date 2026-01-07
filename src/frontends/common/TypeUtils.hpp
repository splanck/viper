//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/common/TypeUtils.hpp
// Purpose: Helper utilities for IL type checking shared across frontends
// Key invariants: All functions are constexpr-compatible and stateless
// Ownership/Lifetime: Non-owning utilities operating on IL types
// Links: docs/codemap.md, docs/il-guide.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include "il/core/Type.hpp"

namespace il::frontends::common::type_utils
{

/// @brief Check if an IL type is an integer type.
/// @details Returns true for i16, i32, i64 integer types.
/// @param k Type kind to check.
/// @return True if the type is an integer type.
[[nodiscard]] constexpr bool isIntegerType(il::core::Type::Kind k) noexcept
{
    using Kind = il::core::Type::Kind;
    return k == Kind::I16 || k == Kind::I32 || k == Kind::I64;
}

/// @brief Check if an IL type is a floating-point type.
/// @details Returns true for f64 floating-point type.
/// @param k Type kind to check.
/// @return True if the type is a floating-point type.
[[nodiscard]] constexpr bool isFloatType(il::core::Type::Kind k) noexcept
{
    using Kind = il::core::Type::Kind;
    return k == Kind::F64;
}

/// @brief Check if an IL type is a numeric type (integer or float).
/// @details Returns true for any integer or floating-point type.
/// @param k Type kind to check.
/// @return True if the type is numeric.
[[nodiscard]] constexpr bool isNumericType(il::core::Type::Kind k) noexcept
{
    return isIntegerType(k) || isFloatType(k);
}

/// @brief Check if an IL type is a pointer type.
/// @details Returns true for ptr type.
/// @param k Type kind to check.
/// @return True if the type is a pointer.
[[nodiscard]] constexpr bool isPointerType(il::core::Type::Kind k) noexcept
{
    return k == il::core::Type::Kind::Ptr;
}

/// @brief Check if an IL type is a string type.
/// @param k Type kind to check.
/// @return True if the type is str.
[[nodiscard]] constexpr bool isStringType(il::core::Type::Kind k) noexcept
{
    return k == il::core::Type::Kind::Str;
}

/// @brief Check if an IL type is void.
/// @param k Type kind to check.
/// @return True if the type is void.
[[nodiscard]] constexpr bool isVoidType(il::core::Type::Kind k) noexcept
{
    return k == il::core::Type::Kind::Void;
}

/// @brief Check if an IL type is a boolean (i1).
/// @param k Type kind to check.
/// @return True if the type is i1 (boolean).
[[nodiscard]] constexpr bool isBoolType(il::core::Type::Kind k) noexcept
{
    return k == il::core::Type::Kind::I1;
}

/// @brief Check if an IL type is a signed integer type.
/// @details All integer types in IL are signed.
/// @param k Type kind to check.
/// @return True if the type is a signed integer.
[[nodiscard]] constexpr bool isSignedIntegerType(il::core::Type::Kind k) noexcept
{
    return isIntegerType(k);
}

/// @brief Get the bit width of an IL integer type.
/// @param k Type kind to check.
/// @return Bit width (1, 16, 32, or 64) or 0 if not an integer type.
[[nodiscard]] constexpr unsigned getIntegerBitWidth(il::core::Type::Kind k) noexcept
{
    using Kind = il::core::Type::Kind;
    switch (k)
    {
        case Kind::I1:
            return 1;
        case Kind::I16:
            return 16;
        case Kind::I32:
            return 32;
        case Kind::I64:
            return 64;
        default:
            return 0;
    }
}

/// @brief Get the bit width of an IL floating-point type.
/// @param k Type kind to check.
/// @return Bit width (64) or 0 if not a float type.
[[nodiscard]] constexpr unsigned getFloatBitWidth(il::core::Type::Kind k) noexcept
{
    using Kind = il::core::Type::Kind;
    switch (k)
    {
        case Kind::F64:
            return 64;
        default:
            return 0;
    }
}

/// @brief Get the storage size in bytes for an IL type.
/// @param k Type kind to check.
/// @return Size in bytes (1, 2, 4, or 8) or 8 for unknown types.
[[nodiscard]] constexpr std::size_t getTypeSize(il::core::Type::Kind k) noexcept
{
    using Kind = il::core::Type::Kind;
    switch (k)
    {
        case Kind::I1:
            return 1;
        case Kind::I16:
            return 2;
        case Kind::I32:
            return 4;
        case Kind::I64:
        case Kind::F64:
        case Kind::Ptr:
        case Kind::Str:
            return 8;
        case Kind::Void:
            return 0;
        default:
            return 8; // Default to 64-bit for safety
    }
}

/// @brief Check if two types are compatible for binary operations.
/// @details Two types are compatible if they are the same or both numeric.
/// @param lhs Left-hand side type.
/// @param rhs Right-hand side type.
/// @return True if types are compatible.
[[nodiscard]] constexpr bool areTypesCompatible(il::core::Type::Kind lhs,
                                                il::core::Type::Kind rhs) noexcept
{
    if (lhs == rhs)
        return true;
    return isNumericType(lhs) && isNumericType(rhs);
}

/// @brief Determine the common type for binary operations between two types.
/// @details For mixed integer/float operations, returns float. Otherwise returns
///          the wider integer type.
/// @param lhs Left-hand side type.
/// @param rhs Right-hand side type.
/// @return Common type to use for the operation, or Void if incompatible.
[[nodiscard]] constexpr il::core::Type::Kind getCommonType(il::core::Type::Kind lhs,
                                                           il::core::Type::Kind rhs) noexcept
{
    using Kind = il::core::Type::Kind;

    // Same types - use that type
    if (lhs == rhs)
        return lhs;

    // If either is float, result is float
    if (isFloatType(lhs) || isFloatType(rhs))
        return Kind::F64;

    // Both integers - use wider type
    if (isIntegerType(lhs) && isIntegerType(rhs))
    {
        unsigned lhsWidth = getIntegerBitWidth(lhs);
        unsigned rhsWidth = getIntegerBitWidth(rhs);
        return lhsWidth >= rhsWidth ? lhs : rhs;
    }

    // Incompatible types
    return Kind::Void;
}

} // namespace il::frontends::common::type_utils
