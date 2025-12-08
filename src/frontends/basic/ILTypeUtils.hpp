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
// NOTE: This file re-exports the common type utilities for backwards
//       compatibility. New code should use frontends/common/TypeUtils.hpp.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "frontends/common/TypeUtils.hpp"
#include "il/core/Type.hpp"

namespace il::frontends::basic::il_utils
{

// Re-export common type utilities for backwards compatibility
using ::il::frontends::common::type_utils::isIntegerType;
using ::il::frontends::common::type_utils::isFloatType;
using ::il::frontends::common::type_utils::isNumericType;
using ::il::frontends::common::type_utils::isPointerType;
using ::il::frontends::common::type_utils::isVoidType;
using ::il::frontends::common::type_utils::isBoolType;
using ::il::frontends::common::type_utils::isSignedIntegerType;
using ::il::frontends::common::type_utils::getIntegerBitWidth;
using ::il::frontends::common::type_utils::getFloatBitWidth;
using ::il::frontends::common::type_utils::areTypesCompatible;

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
