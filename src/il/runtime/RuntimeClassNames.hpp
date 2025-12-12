//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/il/runtime/RuntimeClassNames.hpp
// Purpose: Canonical string constants for Viper runtime class names.
//          These constants should be used instead of hardcoded string literals
//          to ensure consistency and enable compile-time checking.
// Links:
//   - src/il/runtime/classes/RuntimeClasses.inc (source of truth)

#pragma once

#include <string_view>

namespace il::runtime
{

// Canonical runtime class names (from RuntimeClasses.inc)
inline constexpr std::string_view RTCLASS_STRING = "Viper.String";
inline constexpr std::string_view RTCLASS_OBJECT = "Viper.Object";
inline constexpr std::string_view RTCLASS_STRINGBUILDER = "Viper.Text.StringBuilder";
inline constexpr std::string_view RTCLASS_FILE = "Viper.IO.File";
inline constexpr std::string_view RTCLASS_LIST = "Viper.Collections.List";
inline constexpr std::string_view RTCLASS_DICTIONARY = "Viper.Collections.Dictionary";
inline constexpr std::string_view RTCLASS_MATH = "Viper.Math";
// RTCLASS_CONSOLE deprecated - Console consolidated into Terminal
inline constexpr std::string_view RTCLASS_CONSOLE = "Viper.Terminal";
inline constexpr std::string_view RTCLASS_CONVERT = "Viper.Convert";
inline constexpr std::string_view RTCLASS_RANDOM = "Viper.Random";
inline constexpr std::string_view RTCLASS_ENVIRONMENT = "Viper.Environment";
inline constexpr std::string_view RTCLASS_DATETIME = "Viper.DateTime";
inline constexpr std::string_view RTCLASS_GFX_WINDOW = "Viper.Graphics.Window";
inline constexpr std::string_view RTCLASS_GFX_COLOR = "Viper.Graphics.Color";
inline constexpr std::string_view RTCLASS_TERMINAL = "Viper.Terminal";
inline constexpr std::string_view RTCLASS_TIME = "Viper.Time";

// Utility namespace prefixes
inline constexpr std::string_view RTNS_STRINGS = "Viper.Strings";

// Helper to check if a type matches a specific runtime class
inline bool isRuntimeClass(std::string_view qname, std::string_view expected)
{
    return qname == expected;
}

} // namespace il::runtime
