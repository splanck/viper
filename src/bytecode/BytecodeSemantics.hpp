//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/bytecode/BytecodeSemantics.hpp
// Purpose: Bytecode adapters for the shared IL scalar semantic kernel.
// Key invariants: Width encodings are decoded in one place, trap categories are
//                 mapped losslessly where bytecode has an equivalent trap kind,
//                 and version-3 scalar bytecodes carry explicit result width.
// Ownership: Header-only adapter; owns no state and performs no VM dispatch.
// Lifetime: Stateless helpers used by the compiler, verifier, switch VM, and
//           threaded VM.
// Links: il/semantics/ScalarOps.hpp, bytecode/BytecodeVM.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "bytecode/BytecodeVM.hpp"
#include "il/core/Type.hpp"
#include "il/semantics/ScalarOps.hpp"

#include <cstdint>

/// @file
/// @brief Bytecode encoding and trap adapters for shared scalar semantics.
/// @details The bytecode interpreter stores type metadata in compact 8-bit
///          instruction fields.  This header centralizes the translation between
///          those fields and @ref il::semantics::IntWidth so both dispatch loops,
///          the compiler, and validation use the same interpretation.

namespace viper::bytecode::detail {

/// @brief Convert an IL type kind into shared integer-width metadata.
/// @details Unsupported or non-integer kinds conservatively select I64 to match
///          the interpreter fallback behavior used before the shared semantic
///          kernel was introduced.
/// @param kind IL type kind associated with an instruction result.
/// @return Shared integer width used by scalar semantics.
[[nodiscard]] constexpr il::semantics::IntWidth widthFromTypeKind(
    il::core::Type::Kind kind) noexcept {
    switch (kind) {
        case il::core::Type::Kind::I1:
            return il::semantics::IntWidth::I1;
        case il::core::Type::Kind::I16:
            return il::semantics::IntWidth::I16;
        case il::core::Type::Kind::I32:
            return il::semantics::IntWidth::I32;
        case il::core::Type::Kind::I64:
        default:
            return il::semantics::IntWidth::I64;
    }
}

/// @brief Encode a width for bytecodes whose semantics depend on result width.
/// @details Checked arithmetic, checked division/remainder, `idx.chk`, and
///          checked f64-to-int casts use compact width metadata in bytecode
///          format version 3.  The compiler maps I1 to I64 for this opcode
///          family because IL boolean-width arithmetic/casts are not defined;
///          hand-built bytecode may still use raw encoding 0 and the decoder
///          will expose it as I1.
/// @param width Shared integer width to encode.
/// @return Bytecode arg0 encoding: 1=I16, 2=I32, 3=I64.
[[nodiscard]] constexpr uint8_t encodeArithmeticWidthArg(
    il::semantics::IntWidth width) noexcept {
    switch (width) {
        case il::semantics::IntWidth::I16:
            return 1;
        case il::semantics::IntWidth::I32:
            return 2;
        case il::semantics::IntWidth::I1:
        case il::semantics::IntWidth::I64:
        default:
            return 3;
    }
}

/// @brief Encode a result type kind for arithmetic-like bytecode metadata.
/// @param kind IL result type kind.
/// @return Bytecode arg0 encoding produced by @ref encodeArithmeticWidthArg.
[[nodiscard]] constexpr uint8_t encodeArithmeticWidthArg(il::core::Type::Kind kind) noexcept {
    return encodeArithmeticWidthArg(widthFromTypeKind(kind));
}

/// @brief Decode width metadata for arithmetic-like bytecodes.
/// @details Version-3 scalar bytecodes use the full compact mapping:
///          0=I1, 1=I16, 2=I32, 3=I64.
/// @param encoded Raw bytecode arg0 value.
/// @return Shared integer width for the operation.
[[nodiscard]] constexpr il::semantics::IntWidth decodeArithmeticWidthArg(
    uint8_t encoded) noexcept {
    return il::semantics::widthFromLegacyEncoding(encoded);
}

/// @brief Encode a width for narrowing bytecodes.
/// @details Narrowing bytecodes need the full legacy mapping because zero is the
///          only compact representation for boolean `i1` targets.
/// @param width Shared integer width to encode.
/// @return Bytecode arg0 encoding: 0=I1, 1=I16, 2=I32, 3=I64.
[[nodiscard]] constexpr uint8_t encodeNarrowWidthArg(il::semantics::IntWidth width) noexcept {
    return il::semantics::encodeLegacyWidth(width);
}

/// @brief Encode a result type kind for narrowing bytecode metadata.
/// @param kind IL result type kind.
/// @return Bytecode arg0 encoding produced by @ref encodeNarrowWidthArg.
[[nodiscard]] constexpr uint8_t encodeNarrowWidthArg(il::core::Type::Kind kind) noexcept {
    return encodeNarrowWidthArg(widthFromTypeKind(kind));
}

/// @brief Decode width metadata for narrowing bytecodes.
/// @param encoded Raw bytecode arg0 value.
/// @return Shared integer width using the legacy 0=I1 mapping.
[[nodiscard]] constexpr il::semantics::IntWidth decodeNarrowWidthArg(uint8_t encoded) noexcept {
    return il::semantics::widthFromLegacyEncoding(encoded);
}

/// @brief Check whether a raw byte is a valid compact width argument.
/// @param encoded Raw bytecode arg0 value.
/// @return True when @p encoded is one of the defined compact width values.
[[nodiscard]] constexpr bool isValidWidthArg(uint8_t encoded) noexcept {
    return encoded <= 3;
}

/// @brief Translate shared semantic trap categories to bytecode trap categories.
/// @details Every trap emitted by the scalar kernel has a direct bytecode
///          counterpart.  Unknown future semantic categories conservatively map
///          to DomainError so callers do not silently treat failures as success.
/// @param trap Shared semantic trap category.
/// @return Bytecode VM trap kind.
[[nodiscard]] constexpr TrapKind toBytecodeTrap(il::semantics::TrapKind trap) noexcept {
    switch (trap) {
        case il::semantics::TrapKind::None:
            return TrapKind::None;
        case il::semantics::TrapKind::DivideByZero:
            return TrapKind::DivideByZero;
        case il::semantics::TrapKind::Overflow:
            return TrapKind::Overflow;
        case il::semantics::TrapKind::InvalidCast:
            return TrapKind::InvalidCast;
        case il::semantics::TrapKind::Bounds:
            return TrapKind::Bounds;
        case il::semantics::TrapKind::InvalidOperation:
            return TrapKind::InvalidOperation;
        case il::semantics::TrapKind::DomainError:
        default:
            return TrapKind::DomainError;
    }
}

} // namespace viper::bytecode::detail
