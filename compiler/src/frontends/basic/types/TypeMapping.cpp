//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
/// @file
/// @brief Implements IL-to-BASIC type mapping for procedure signatures.
/// @details Converts IL core scalar types to the BASIC frontend's scalar type
///          system when building or validating procedure signatures. Non-scalar
///          IL kinds and unsupported types return `std::nullopt` to signal that
///          no BASIC equivalent exists.
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/types/TypeMapping.hpp"

namespace il::frontends::basic::types
{

/// @brief Map an IL core type to a BASIC scalar type.
/// @details Supports integer, floating-point, string, boolean, and pointer
///          kinds. Pointers are treated as integer handles for signature
///          compatibility, while void returns `std::nullopt` to signal SUB-like
///          procedures. Unsupported kinds return `std::nullopt`.
/// @param ilType IL core type to map.
/// @return BASIC scalar type when supported; otherwise `std::nullopt`.
std::optional<Type> mapIlToBasic(const il::core::Type &ilType)
{
    using K = il::core::Type::Kind;
    switch (ilType.kind)
    {
        case K::I32:
            // Map 32-bit integers to BASIC integer type (64-bit internal).
            return Type::I64;
        case K::I64:
            return Type::I64;
        case K::F64:
            return Type::F64;
        case K::Str:
            return Type::Str;
        case K::I1:
            return Type::Bool;
        case K::Void:
            return std::nullopt; // Indicates SUB/void
        case K::Ptr:
            // Treat opaque pointers as integer handles for BASIC signature purposes.
            // Semantic/OOP layers handle object identity and method resolution.
            return Type::I64;
        default:
            return std::nullopt; // Unsupported (i16/i32/ptr/error/resumetok)
    }
}

} // namespace il::frontends::basic::types
