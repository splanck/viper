//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file RuntimeNames.hpp
/// @brief BASIC frontend runtime name constants.
///
/// This header imports the canonical runtime names from the generated
/// RuntimeNames.hpp for use in BASIC lowering code.
///
//===----------------------------------------------------------------------===//

#pragma once

#include "il/runtime/generated/RuntimeNames.hpp"

namespace il::frontends::basic::runtime
{

// Import all generated names into this namespace
using namespace il::runtime::names;

// Aliases for removed String.From* functions (use Convert.ToString_* canonical names)
inline constexpr const char *kStringFromInt = kConvertToStringInt;
inline constexpr const char *kStringFromDouble = kConvertToStringDouble;

} // namespace il::frontends::basic::runtime
