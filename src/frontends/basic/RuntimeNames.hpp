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

#include "il/runtime/RuntimeNames.hpp"

namespace il::frontends::basic::runtime {

// Import all generated names into this namespace
using namespace il::runtime::names;

// Aliases for removed String.From* functions (use Core.Convert canonical names)
inline constexpr const char *kStringFromInt = kCoreConvertToStringInt;
inline constexpr const char *kStringFromDouble = kCoreConvertToStringDouble;

// Core.Convert short aliases
inline constexpr const char *kConvertToDouble = kCoreConvertToDouble;
inline constexpr const char *kConvertToInt = kCoreConvertToInt64;

// Core.Parse short aliases
inline constexpr const char *kParseDouble = kCoreParseTryNum;
inline constexpr const char *kParseInt64 = kCoreParseTryInt;
inline constexpr const char *kParseDoubleCStr = "rt_parse_double";
inline constexpr const char *kParseInt64CStr = "rt_parse_int64";
inline constexpr const char *kStringSplitFieldsRaw = "rt_str_split_fields";

} // namespace il::frontends::basic::runtime
