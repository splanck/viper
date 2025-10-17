//===----------------------------------------------------------------------===//
// MIT License. See LICENSE file in the project root for full text.
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements shared helper utilities for instruction verification.
/// @details Provides predicates for integer range checks and type-category
/// mapping used across the IL verifier components.

#include "il/verify/InstructionCheckUtils.hpp"

#include <limits>

namespace il::verify::detail
{

/// @brief Determine whether a signed value fits within the specified integer kind.
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

