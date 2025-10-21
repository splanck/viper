//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
// File: src/il/verify/InstructionCheckUtils.cpp
// Purpose: Provide reusable predicates that back IL instruction verification.
// Key invariants: Numeric range helpers mirror the IL type widths and category
//                 mappings described in docs/il-guide.md#reference.
// Ownership/Lifetime: Pure utility routines with no hidden state or caching.
// Links: docs/il-guide.md#reference, docs/codemap.md#il-verify
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements shared helper utilities for instruction verification.
/// @details Provides predicates for integer range checks and type-category
///          mapping used across the IL verifier components.  Keeping the
///          definitions centralised ensures the operand and result checkers see
///          identical semantics when translating metadata categories into
///          concrete IL types.

#include "il/verify/InstructionCheckUtils.hpp"

#include <limits>

namespace il::verify::detail
{

/// @brief Determine whether a signed value fits within the specified integer kind.
///
/// @details The verifier frequently needs to check whether literal operands or
///          constant-folded values stay within the width required by opcode
///          metadata.  Instead of duplicating limit computations in each call
///          site, the helper maps the IL type kind to the correct C++ range and
///          performs the comparison using @c std::numeric_limits.  Boolean
///          operands are treated specially because their domain is explicitly
///          {0, 1} regardless of the underlying storage width.
///
/// @param value Signed integer to test.
/// @param kind Target IL integer kind.
/// @return @c true when @p value lies within the representable range of @p kind.
bool fitsInIntegerKind(long long value, il::core::Type::Kind kind)
{
    switch (kind)
    {
        case il::core::Type::Kind::I1:
            return value == 0 || value == 1;
        case il::core::Type::Kind::I16:
            return value >= std::numeric_limits<int16_t>::min() && value <= std::numeric_limits<int16_t>::max();
        case il::core::Type::Kind::I32:
            return value >= std::numeric_limits<int32_t>::min() && value <= std::numeric_limits<int32_t>::max();
        case il::core::Type::Kind::I64:
            return true;
        default:
            return false;
    }
}

/// @brief Translate a type category into a concrete IL type kind.
///
/// @details Opcode metadata expresses operands using coarse categories so a
///          single entry can describe a family of instructions (for example,
///          arithmetic ops that accept any integer width).  The verifier needs a
///          precise @ref il::core::Type::Kind to compare against instruction
///          operands.  This function performs that translation while explicitly
///          rejecting categories that are either polymorphic or tied to runtime
///          inference, returning @c std::nullopt so callers can handle those
///          cases separately.
///
/// @param category Operand category derived from opcode metadata.
/// @return Matching type kind or @c std::nullopt when the category represents a
///         polymorphic or unsupported type.
std::optional<il::core::Type::Kind> kindFromCategory(il::core::TypeCategory category)
{
    using il::core::Type;

    switch (category)
    {
        case il::core::TypeCategory::Void:
            return Type::Kind::Void;
        case il::core::TypeCategory::I1:
            return Type::Kind::I1;
        case il::core::TypeCategory::I16:
            return Type::Kind::I16;
        case il::core::TypeCategory::I32:
            return Type::Kind::I32;
        case il::core::TypeCategory::I64:
            return Type::Kind::I64;
        case il::core::TypeCategory::F64:
            return Type::Kind::F64;
        case il::core::TypeCategory::Ptr:
            return Type::Kind::Ptr;
        case il::core::TypeCategory::Str:
            return Type::Kind::Str;
        case il::core::TypeCategory::Error:
            return Type::Kind::Error;
        case il::core::TypeCategory::ResumeTok:
            return Type::Kind::ResumeTok;
        case il::core::TypeCategory::None:
        case il::core::TypeCategory::Any:
        case il::core::TypeCategory::InstrType:
        case il::core::TypeCategory::Dynamic:
            return std::nullopt;
    }
    return std::nullopt;
}

} // namespace il::verify::detail

