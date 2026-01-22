//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file RuntimeAdapter.cpp
/// @brief Type conversion utilities for Zia frontend to use RuntimeRegistry.
///
//===----------------------------------------------------------------------===//

#include "frontends/zia/RuntimeAdapter.hpp"

namespace il::frontends::zia
{

TypeRef toZiaType(il::runtime::ILScalarType t)
{
    switch (t)
    {
    case il::runtime::ILScalarType::I64:
        return types::integer();
    case il::runtime::ILScalarType::F64:
        return types::number();
    case il::runtime::ILScalarType::Bool:
        return types::boolean();
    case il::runtime::ILScalarType::String:
        return types::string();
    case il::runtime::ILScalarType::Void:
        return types::voidType();
    case il::runtime::ILScalarType::Object:
        return types::ptr();
    case il::runtime::ILScalarType::Unknown:
    default:
        return types::unknown();
    }
}

std::vector<TypeRef> toZiaParamTypes(const il::runtime::ParsedSignature &sig)
{
    std::vector<TypeRef> result;
    result.reserve(sig.params.size());
    for (auto p : sig.params)
        result.push_back(toZiaType(p));
    return result;
}

} // namespace il::frontends::zia
