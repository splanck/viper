//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/basic/ILTypeUtils.hpp
// Purpose: Helper utilities for IL type checking in BASIC lowering 
// Key invariants: All functions are constexpr-compatible and stateless
// Ownership/Lifetime: Non-owning utilities operating on IL types
// Links: docs/codemap.md, docs/il-guide.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include "il/core/Type.hpp"

namespace il::frontends::basic::il_utils
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
/// @return Bit width (16, 32, or 64) or 0 if not an integer type.
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

} // namespace il::frontends::basic::il_utils

// Forward declare BASIC AST Type enum
namespace il::frontends::basic
{
enum class Type : std::uint8_t;
}

namespace il::frontends::basic::type_conv
{

/// @brief Translate a BASIC AST type into the corresponding IL core type.
///
/// @details Lowering frequently needs to turn semantic types expressed by the
///          BASIC AST (`Type`) into the concrete IL type descriptor understood by
///          the builder. The mapping is intentionally narrow: each BASIC type
///          collapses to a single IL `Type::Kind`. Should the language evolve,
///          new cases can be added here without touching call sites.
///
///          This function consolidates three previously duplicate implementations:
///          - Lower_OOP_Emit.cpp::ilTypeForAstType()
///          - Lower_OOP_Expr.cpp::ilTypeForAstType()
///          - Lowerer.Program.cpp::pipeline_detail::coreTypeForAstType()
///
/// @param ty BASIC type enumeration value.
/// @return Concrete IL type used during lowering. Defaults to `I64` for
///         robustness when the caller passes an unrecognized type.
[[nodiscard]] il::core::Type astToIlType(::il::frontends::basic::Type ty) noexcept;

/// @brief Determine the storage size for a BASIC field type.
///
/// @details Maps BASIC semantic types to their runtime storage requirements.
///          String fields are treated as pointers to managed buffers, numeric
///          fields use their natural width, and any unrecognized types default
///          to 64 bits so layouts remain conservative.
///
///          Boolean types use 1 byte for efficient packing in class layouts.
///          All other integer and floating-point types use 8 bytes.
///
///          This function consolidates the implementation previously in:
///          - Lower_OOP_Scan.cpp::fieldSize()
///
/// @param type BASIC field type enumerator.
/// @return Size in bytes required to store the field.
[[nodiscard]] std::size_t getFieldSize(::il::frontends::basic::Type type) noexcept;

} // namespace il::frontends::basic::type_conv
