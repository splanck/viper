#pragma once
//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
/**
 * @file colors.hpp
 * @brief Workbench color palette with theme support.
 *
 * This file provides the color system for the Workbench desktop environment.
 * It offers two sets of colors:
 *
 * 1. **Compile-time constants** (WB_*): Used for static initializers and
 *    icon pixel data where constexpr is required.
 *
 * 2. **Theme-aware functions** (theme*()): Used for runtime drawing where
 *    colors should adapt to the currently active theme.
 *
 * ## Color Format
 *
 * All colors are in ARGB32 format: `0xAARRGGBB`
 * - AA = Alpha (FF = fully opaque)
 * - RR = Red (00-FF)
 * - GG = Green (00-FF)
 * - BB = Blue (00-FF)
 *
 * ## Theme System
 *
 * The Workbench supports multiple color themes:
 * - Classic Amiga (default)
 * - Dark Mode
 * - Modern Blue
 * - High Contrast
 *
 * Use the theme*() functions when drawing UI elements that should change
 * with the theme. Use WB_* constants only for static data like icon pixels.
 *
 * @see theme.hpp for theme definitions
 * @see viper_colors.h for system-wide color definitions
 */
//===----------------------------------------------------------------------===//

#include "../../include/viper_colors.h"
#include "theme.hpp"

namespace workbench {

//===----------------------------------------------------------------------===//
// Compile-Time Color Constants
//===----------------------------------------------------------------------===//

/**
 * @defgroup StaticColors Compile-Time Color Constants
 * @brief Fixed colors for icon definitions and static initializers.
 *
 * These colors match the Classic Amiga theme and are used where
 * constexpr values are required (e.g., icon pixel data arrays).
 * For runtime UI drawing, prefer the theme*() functions.
 * @{
 */

/** @brief Classic Workbench blue (0xFF0055AA). Used for desktop background. */
constexpr uint32_t WB_BLUE = VIPER_COLOR_DESKTOP;

/** @brief Dark blue for borders and accents (0xFF003366). */
constexpr uint32_t WB_BLUE_DARK = VIPER_COLOR_BORDER;

/** @brief Pure white (0xFFFFFFFF). */
constexpr uint32_t WB_WHITE = VIPER_COLOR_WHITE;

/** @brief Pure black (0xFF000000). */
constexpr uint32_t WB_BLACK = VIPER_COLOR_BLACK;

/** @brief Selection/highlight orange (0xFFFF8800). */
constexpr uint32_t WB_ORANGE = VIPER_COLOR_ORANGE;

/** @brief Light gray for window backgrounds (0xFFAAAAAA). */
constexpr uint32_t WB_GRAY_LIGHT = VIPER_COLOR_GRAY_LIGHT;

/** @brief Medium gray for disabled elements (0xFF888888). */
constexpr uint32_t WB_GRAY_MED = VIPER_COLOR_GRAY_MED;

/** @brief Dark gray for shadows and borders (0xFF555555). */
constexpr uint32_t WB_GRAY_DARK = VIPER_COLOR_GRAY_DARK;

/** @} */ // end StaticColors

//===----------------------------------------------------------------------===//
// Theme-Aware Color Functions
//===----------------------------------------------------------------------===//

/**
 * @defgroup ThemeFunctions Theme-Aware Color Accessors
 * @brief Runtime color functions that respond to theme changes.
 *
 * These inline functions return colors from the currently active theme.
 * Use these when drawing UI elements that should change appearance when
 * the user switches themes.
 * @{
 */

/** @brief Desktop/backdrop color from current theme. */
inline uint32_t themeDesktop() {
    return currentTheme().desktop;
}

/** @brief Window background color from current theme. */
inline uint32_t themeWindowBg() {
    return currentTheme().windowBg;
}

/** @brief Primary text color from current theme. */
inline uint32_t themeText() {
    return currentTheme().text;
}

/** @brief Disabled/grayed-out text color from current theme. */
inline uint32_t themeTextDisabled() {
    return currentTheme().textDisabled;
}

/** @brief Selection highlight color from current theme. */
inline uint32_t themeHighlight() {
    return currentTheme().highlight;
}

/** @brief 3D border light edge color from current theme. */
inline uint32_t themeBorderLight() {
    return currentTheme().border3dLight;
}

/** @brief 3D border dark edge color from current theme. */
inline uint32_t themeBorderDark() {
    return currentTheme().border3dDark;
}

/** @brief Menu bar and dropdown background from current theme. */
inline uint32_t themeMenuBg() {
    return currentTheme().menuBg;
}

/** @brief Menu item text color from current theme. */
inline uint32_t themeMenuText() {
    return currentTheme().menuText;
}

/** @brief Menu item hover/selection highlight from current theme. */
inline uint32_t themeMenuHighlight() {
    return currentTheme().menuHighlight;
}

/** @brief Menu item text when highlighted from current theme. */
inline uint32_t themeMenuHighlightText() {
    return currentTheme().menuHighlightText;
}

/** @brief Window title bar color from current theme. */
inline uint32_t themeTitleBar() {
    return currentTheme().titleBar;
}

/** @brief Window title bar text color from current theme. */
inline uint32_t themeTitleBarText() {
    return currentTheme().titleBarText;
}

/** @brief Desktop icon label background from current theme. */
inline uint32_t themeIconBg() {
    return currentTheme().iconBg;
}

/** @brief Desktop icon label text color from current theme. */
inline uint32_t themeIconText() {
    return currentTheme().iconText;
}

/** @brief Desktop icon label shadow color from current theme. */
inline uint32_t themeIconShadow() {
    return currentTheme().iconShadow;
}

/** @} */ // end ThemeFunctions

//===----------------------------------------------------------------------===//
// ViperDOS Accent Colors
//===----------------------------------------------------------------------===//

/**
 * @defgroup AccentColors ViperDOS Accent Colors
 * @brief Static accent colors that don't change with themes.
 * @{
 */

/** @brief ViperDOS green accent (0xFF00AA44). */
constexpr uint32_t VIPER_GREEN = VIPER_COLOR_GREEN;

/** @brief Legacy console background brown (0xFF1A1208). */
constexpr uint32_t VIPER_BROWN = 0xFF1A1208;

/** @} */ // end AccentColors

} // namespace workbench
