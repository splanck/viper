//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
/// @file
/// @brief Implements BASIC-to-IL type conversion helpers.
/// @details Provides small, stateless utilities that map BASIC AST types to IL
///          core types and compute ABI-relevant sizes for field storage. The
///          mappings are canonical and shared across the lowering pipeline to
///          keep type reasoning consistent.
//
//===----------------------------------------------------------------------===//

#include "ILTypeUtils.hpp"

#include "BasicTypes.hpp"
#include "ast/NodeFwd.hpp"
#include "il/core/Type.hpp"

#include <cstddef>
#include <string_view>

namespace il::frontends::basic::type_conv
{

/// @brief Convert a BASIC AST scalar type to an IL core type.
/// @details Returns the canonical IL kind used by the lowering pipeline for the
///          given BASIC type. The mapping is total; unknown cases fall back to
///          I64 to keep lowering resilient.
/// @param ty BASIC AST type enumerator.
/// @return Corresponding IL core type.
il::core::Type astToIlType(::il::frontends::basic::Type ty) noexcept
{
    using IlType = il::core::Type;
    switch (ty)
    {
        case ::il::frontends::basic::Type::I64:
            return IlType(IlType::Kind::I64);
        case ::il::frontends::basic::Type::F64:
            return IlType(IlType::Kind::F64);
        case ::il::frontends::basic::Type::Str:
            return IlType(IlType::Kind::Str);
        case ::il::frontends::basic::Type::Bool:
            return IlType(IlType::Kind::I1);
    }
    return IlType(IlType::Kind::I64);
}

/// @brief Return the storage size for a BASIC field type.
/// @details Reports the byte size used when laying out fields for the BASIC
///          frontend. Strings are represented as pointers, floating-point values
///          as 64-bit, booleans as 1 byte, and integers as 8 bytes.
/// @param type BASIC field type.
/// @return Size in bytes for the type's storage representation.
std::size_t getFieldSize(::il::frontends::basic::Type type) noexcept
{
    constexpr std::size_t kPointerSize = sizeof(void *);

    switch (type)
    {
        case ::il::frontends::basic::Type::Str:
            return kPointerSize;
        case ::il::frontends::basic::Type::F64:
            return 8;
        case ::il::frontends::basic::Type::Bool:
            return 1;
        case ::il::frontends::basic::Type::I64:
        default:
            return 8;
    }
}

il::core::Type::Kind basicTypeToIlKind(BasicType t) noexcept
{
    using Kind = il::core::Type::Kind;
    switch (t)
    {
        case BasicType::String:
            return Kind::Str;
        case BasicType::Float:
            return Kind::F64;
        case BasicType::Bool:
            return Kind::I1;
        case BasicType::Void:
            return Kind::Void;
        case BasicType::Object:
            return Kind::Ptr;
        case BasicType::Int:
        case BasicType::Unknown:
        default:
            return Kind::I64;
    }
}

il::core::Type runtimeScalarToType(std::string_view token) noexcept
{
    using IlType = il::core::Type;
    if (token == "i64")
        return IlType(IlType::Kind::I64);
    if (token == "f64")
        return IlType(IlType::Kind::F64);
    if (token == "i1")
        return IlType(IlType::Kind::I1);
    if (token == "str")
        return IlType(IlType::Kind::Str);
    if (token == "obj")
        return IlType(IlType::Kind::Ptr);
    if (token == "void")
        return IlType(IlType::Kind::Void);
    return IlType(IlType::Kind::I64);
}

} // namespace il::frontends::basic::type_conv
