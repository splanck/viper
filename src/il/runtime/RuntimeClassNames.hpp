//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/il/runtime/RuntimeClassNames.hpp
// Purpose: Canonical string constants for Zanna runtime class names.
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

namespace il::runtime {

/// @brief Canonical runtime class name for Zanna.String.
inline constexpr std::string_view RTCLASS_STRING = "Zanna.String";
/// @brief Canonical runtime class name for Zanna.Core.Object.
inline constexpr std::string_view RTCLASS_OBJECT = "Zanna.Core.Object";
/// @brief Canonical runtime class name for Zanna.Text.StringBuilder.
inline constexpr std::string_view RTCLASS_STRINGBUILDER = "Zanna.Text.StringBuilder";
/// @brief Canonical runtime class name for Zanna.IO.File.
inline constexpr std::string_view RTCLASS_FILE = "Zanna.IO.File";
/// @brief Canonical runtime class name for Zanna.Collections.List.
inline constexpr std::string_view RTCLASS_LIST = "Zanna.Collections.List";
/// @brief Canonical runtime class name for Zanna.Collections.Map.
inline constexpr std::string_view RTCLASS_MAP = "Zanna.Collections.Map";
/// @brief Canonical runtime class name for Zanna.Math.
inline constexpr std::string_view RTCLASS_MATH = "Zanna.Math";
/// @brief Canonical runtime class name for Zanna.Core.Convert.
inline constexpr std::string_view RTCLASS_CONVERT = "Zanna.Core.Convert";
/// @brief Canonical runtime class name for Zanna.Math.Random.
inline constexpr std::string_view RTCLASS_RANDOM = "Zanna.Math.Random";
/// @brief Canonical runtime class name for Zanna.System.Environment.
inline constexpr std::string_view RTCLASS_ENVIRONMENT = "Zanna.System.Environment";
/// @brief Canonical runtime class name for Zanna.DateTime.
inline constexpr std::string_view RTCLASS_DATETIME = "Zanna.DateTime";
/// @brief Canonical runtime class name for Zanna.Graphics.Window.
inline constexpr std::string_view RTCLASS_GFX_WINDOW = "Zanna.Graphics.Window";
/// @brief Canonical runtime class name for Zanna.Graphics.Color.
inline constexpr std::string_view RTCLASS_GFX_COLOR = "Zanna.Graphics.Color";
/// @brief Canonical runtime class name for Zanna.Terminal.
inline constexpr std::string_view RTCLASS_TERMINAL = "Zanna.Terminal";
/// @brief Canonical runtime class name for Zanna.Time.Clock.
inline constexpr std::string_view RTCLASS_CLOCK = "Zanna.Time.Clock";
/// @brief Canonical runtime class name for Zanna.Core.Diagnostics.Stopwatch.
inline constexpr std::string_view RTCLASS_STOPWATCH = "Zanna.Core.Diagnostics.Stopwatch";
/// @brief Canonical runtime class name for Zanna.Text.Uuid.
inline constexpr std::string_view RTCLASS_UUID = "Zanna.Text.Uuid";

// Utility namespace prefixes
/// @brief Namespace prefix for Zanna.String helpers.
inline constexpr std::string_view RTNS_STRINGS = "Zanna.String";

// Helper to check if a type matches a specific runtime class
/// @brief Check whether a qualified name matches a runtime class.
/// @details Compares the fully qualified name against an expected constant.
/// @param qname Qualified name to test.
/// @param expected Expected runtime class name constant.
/// @return True if the names match exactly; false otherwise.
inline bool isRuntimeClass(std::string_view qname, std::string_view expected) {
    return qname == expected;
}

} // namespace il::runtime
