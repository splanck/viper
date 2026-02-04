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

/// @file
/// @brief Canonical runtime class names used by the IL runtime.
/// @details Defines compile-time string constants for well-known runtime
///          classes. Centralizing the names avoids hardcoded literals and keeps
///          lookups consistent across frontends and runtime components.

#pragma once

#include <string_view>

namespace il::runtime
{

/// @brief Canonical runtime class name for Viper.String.
inline constexpr std::string_view RTCLASS_STRING = "Viper.String";
/// @brief Canonical runtime class name for Viper.Object.
inline constexpr std::string_view RTCLASS_OBJECT = "Viper.Object";
/// @brief Canonical runtime class name for Viper.Text.StringBuilder.
inline constexpr std::string_view RTCLASS_STRINGBUILDER = "Viper.Text.StringBuilder";
/// @brief Canonical runtime class name for Viper.IO.File.
inline constexpr std::string_view RTCLASS_FILE = "Viper.IO.File";
/// @brief Canonical runtime class name for Viper.Collections.List.
inline constexpr std::string_view RTCLASS_LIST = "Viper.Collections.List";
/// @brief Canonical runtime class name for Viper.Collections.Map.
inline constexpr std::string_view RTCLASS_MAP = "Viper.Collections.Map";
/// @brief Canonical runtime class name for Viper.Math.
inline constexpr std::string_view RTCLASS_MATH = "Viper.Math";
// RTCLASS_CONSOLE deprecated - Console consolidated into Terminal
/// @brief Canonical runtime class name for Viper.Terminal (legacy Console alias).
inline constexpr std::string_view RTCLASS_CONSOLE = "Viper.Terminal";
/// @brief Canonical runtime class name for Viper.Convert.
inline constexpr std::string_view RTCLASS_CONVERT = "Viper.Convert";
/// @brief Canonical runtime class name for Viper.Random.
inline constexpr std::string_view RTCLASS_RANDOM = "Viper.Random";
/// @brief Canonical runtime class name for Viper.Environment.
inline constexpr std::string_view RTCLASS_ENVIRONMENT = "Viper.Environment";
/// @brief Canonical runtime class name for Viper.DateTime.
inline constexpr std::string_view RTCLASS_DATETIME = "Viper.DateTime";
/// @brief Canonical runtime class name for Viper.Graphics.Window.
inline constexpr std::string_view RTCLASS_GFX_WINDOW = "Viper.Graphics.Window";
/// @brief Canonical runtime class name for Viper.Graphics.Color.
inline constexpr std::string_view RTCLASS_GFX_COLOR = "Viper.Graphics.Color";
/// @brief Canonical runtime class name for Viper.Terminal.
inline constexpr std::string_view RTCLASS_TERMINAL = "Viper.Terminal";
/// @brief Canonical runtime class name for Viper.Time (deprecated).
inline constexpr std::string_view RTCLASS_TIME = "Viper.Time"; // Deprecated, use RTCLASS_CLOCK
/// @brief Canonical runtime class name for Viper.Time.Clock.
inline constexpr std::string_view RTCLASS_CLOCK = "Viper.Time.Clock";
/// @brief Canonical runtime class name for Viper.Diagnostics.Stopwatch.
inline constexpr std::string_view RTCLASS_STOPWATCH = "Viper.Diagnostics.Stopwatch";
/// @brief Canonical runtime class name for Viper.Text.Guid.
inline constexpr std::string_view RTCLASS_GUID = "Viper.Text.Guid";

// Utility namespace prefixes
/// @brief Namespace prefix for Viper.String helpers.
inline constexpr std::string_view RTNS_STRINGS = "Viper.String";

// Helper to check if a type matches a specific runtime class
/// @brief Check whether a qualified name matches a runtime class.
/// @details Compares the fully qualified name against an expected constant.
/// @param qname Qualified name to test.
/// @param expected Expected runtime class name constant.
/// @return True if the names match exactly; false otherwise.
inline bool isRuntimeClass(std::string_view qname, std::string_view expected)
{
    return qname == expected;
}

} // namespace il::runtime
