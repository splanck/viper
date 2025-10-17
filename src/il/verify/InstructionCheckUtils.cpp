//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements utility helpers shared by multiple instruction verification
// components.  These routines handle low-level conversions and range checks for
// IL type categories.
//
//===----------------------------------------------------------------------===//

#include "il/verify/InstructionCheckUtils.hpp"

#include <limits>

namespace il::verify::detail
{

/// @brief Determine whether an integer literal fits within the given IL integer kind.
///
/// Performs bounds checking for the supported integer kinds so constant
/// verification can ensure literals respect the declared operand type.
///
/// @param value Integer literal to test.
/// @param kind  Target integer kind.
/// @return True when @p value is representable in @p kind.
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

/// @brief Translate a type category enumeration into a concrete IL type kind.
///
/// Most categories map directly to a single type kind.  Categories describing
/// dynamic or instruction-dependent types yield @c std::nullopt so callers know
/// additional context is required.
///
/// @param category Type category describing an operand or result.
/// @return The corresponding type kind when deterministic; otherwise nullopt.
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

