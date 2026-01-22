//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file RuntimeAdapter.hpp
/// @brief Type conversion utilities for Zia frontend to use RuntimeRegistry.
///
/// This file provides conversion functions from IL-layer ILScalarType to
/// Zia semantic types, enabling the Zia frontend to use the unified
/// RuntimeRegistry for runtime function signatures.
///
//===----------------------------------------------------------------------===//

#pragma once

#include "frontends/zia/Types.hpp"
#include "il/runtime/classes/RuntimeClasses.hpp"
#include <vector>

namespace il::frontends::zia
{

/// @brief Convert ILScalarType to Zia TypeRef.
/// @param t The IL scalar type from the RuntimeRegistry.
/// @return Corresponding Zia TypeRef.
TypeRef toZiaType(il::runtime::ILScalarType t);

/// @brief Convert a vector of ILScalarTypes to Zia TypeRefs.
/// @param params Vector of IL scalar types.
/// @return Vector of Zia TypeRefs.
std::vector<TypeRef> toZiaParamTypes(const il::runtime::ParsedSignature &sig);

} // namespace il::frontends::zia
