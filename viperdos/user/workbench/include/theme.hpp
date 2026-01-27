#pragma once
//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
/**
 * @file theme.hpp
 * @brief Theme system for Workbench colors.
 *
 * This file defines the theming infrastructure for the Workbench desktop.
 * Themes allow users to customize the visual appearance of the entire
 * desktop environment with coordinated color schemes.
 *
 * ## Available Themes
 *
 * | Theme          | Description                              |
 * |----------------|------------------------------------------|
 * | Classic Amiga  | Traditional Workbench 3.x blue and gray  |
 * | Dark Mode      | Dark backgrounds with soft text          |
 * | Modern Blue    | Contemporary light theme with blue       |
 * | High Contrast  | Accessibility theme with high contrast   |
 *
 * ## Theme Architecture
 *
 * ```
 * +-------------------+
 * | Theme struct      |  <- Color values for all UI elements
 * +-------------------+
 *          |
 *          v
 * +-------------------+
 * | g_currentTheme    |  <- Global pointer to active theme
 * +-------------------+
 *          |
 *          v
 * +-------------------+
 * | theme*() funcs    |  <- Inline accessors in colors.hpp
 * +-------------------+
 * ```
 *
 * ## Usage
 *
 * ```cpp
 * // Switch to dark mode
 * setTheme(&themes::DarkMode);
 *
 * // Draw using current theme
 * gui_fill_rect(win, x, y, w, h, currentTheme().desktop);
 * ```
 *
 * @see colors.hpp for theme accessor functions
 */
//===----------------------------------------------------------------------===//

#include <stdint.h>

namespace workbench {

//===----------------------------------------------------------------------===//
// Theme Structure
//===----------------------------------------------------------------------===//

/**
 * @brief Complete color scheme for the Workbench desktop environment.
 *
 * Each theme defines colors for all UI elements including the desktop
 * background, windows, menus, icons, and text. Colors are stored in
 * ARGB32 format (0xAARRGGBB).
 *
 * ## Color Categories
 *
 * - **Desktop**: Background and border colors for the desktop area
 * - **Window**: Background, title bar, and border colors for windows
 * - **UI Elements**: Text, highlights, and 3D effect colors
 * - **Menu**: Colors for the menu bar and dropdown menus
 * - **Icon**: Colors for desktop icon labels
 *
 * ## 3D Border Effect
 *
 * The 3D border effect uses two colors:
 * - `border3dLight`: Top and left edges (simulates light from top-left)
 * - `border3dDark`: Bottom and right edges (shadow effect)
 */
struct Theme {
    const char *name;          /**< Human-readable theme name. */

    // Desktop colors
    uint32_t desktop;          /**< Main desktop/backdrop color. */
    uint32_t desktopBorder;    /**< Desktop border color. */

    // Window colors
    uint32_t windowBg;         /**< Window content area background. */
    uint32_t titleBar;         /**< Active window title bar. */
    uint32_t titleBarText;     /**< Title bar text color. */
    uint32_t titleBarInactive; /**< Inactive window title bar. */

    // UI element colors
    uint32_t highlight;        /**< Selection highlight (icons, menus). */
    uint32_t text;             /**< Default text color. */
    uint32_t textDisabled;     /**< Disabled/grayed-out text. */
    uint32_t border3dLight;    /**< 3D border light edge (top/left). */
    uint32_t border3dDark;     /**< 3D border dark edge (bottom/right). */

    // Menu colors
    uint32_t menuBg;           /**< Menu bar and dropdown background. */
    uint32_t menuText;         /**< Menu item text color. */
    uint32_t menuHighlight;    /**< Hovered menu item background. */
    uint32_t menuHighlightText;/**< Hovered menu item text. */

    // Icon colors
    uint32_t iconBg;           /**< Selected icon label background. */
    uint32_t iconText;         /**< Icon label text color. */
    uint32_t iconShadow;       /**< Icon label drop shadow. */

    // Text area colors (for editors, dialogs, etc.)
    uint32_t textAreaBg;       /**< Text editing area background. */
    uint32_t textAreaText;     /**< Text editing area text color. */
};

//===----------------------------------------------------------------------===//
// Built-in Theme Definitions
//===----------------------------------------------------------------------===//

/**
 * @brief Built-in theme definitions.
 *
 * This namespace contains constexpr Theme definitions that can be used
 * directly at compile time. Each theme is fully specified with all
 * required colors.
 */
namespace themes {

/**
 * @brief Classic Amiga Workbench 3.x color scheme.
 *
 * The default theme, inspired by the Amiga Workbench 3.x interface:
 * - Blue desktop background
 * - Light gray window backgrounds
 * - Orange selection highlights
 * - Classic 3D button effects
 */
constexpr Theme ClassicAmiga = {
    "Classic Amiga",
    // Desktop
    0xFF0055AA, // desktop (Amiga blue)
    0xFF003366, // desktopBorder
    // Window
    0xFFAAAAAA, // windowBg (light gray)
    0xFF0055AA, // titleBar (blue)
    0xFFFFFFFF, // titleBarText
    0xFF888888, // titleBarInactive
    // UI elements
    0xFFFF8800, // highlight (orange)
    0xFF000000, // text
    0xFF888888, // textDisabled
    0xFFFFFFFF, // border3dLight
    0xFF555555, // border3dDark
    // Menu
    0xFFAAAAAA, // menuBg
    0xFF000000, // menuText
    0xFF0055AA, // menuHighlight
    0xFFFFFFFF, // menuHighlightText
    // Icons
    0xFFFF8800, // iconBg
    0xFFFFFFFF, // iconText
    0xFF000000, // iconShadow
    // Text area (classic Amiga blue on white)
    0xFF0055AA, // textAreaBg (Amiga blue)
    0xFFFFFFFF, // textAreaText (white)
};

/**
 * @brief Dark mode theme for reduced eye strain.
 *
 * A modern dark theme with:
 * - Dark blue-gray backgrounds
 * - Light text for contrast
 * - Pink/red accent colors
 * - Soft edges with reduced contrast
 */
constexpr Theme DarkMode = {
    "Dark Mode",
    // Desktop
    0xFF1E1E2E, // desktop (dark blue-gray)
    0xFF11111B, // desktopBorder
    // Window
    0xFF313244, // windowBg (dark gray)
    0xFF45475A, // titleBar
    0xFFCDD6F4, // titleBarText
    0xFF585B70, // titleBarInactive
    // UI elements
    0xFFF38BA8, // highlight (pink/red)
    0xFFCDD6F4, // text (light)
    0xFF6C7086, // textDisabled
    0xFF585B70, // border3dLight
    0xFF11111B, // border3dDark
    // Menu
    0xFF313244, // menuBg
    0xFFCDD6F4, // menuText
    0xFF585B70, // menuHighlight
    0xFFCDD6F4, // menuHighlightText
    // Icons
    0xFFF38BA8, // iconBg
    0xFFCDD6F4, // iconText
    0xFF11111B, // iconShadow
    // Text area
    0xFF1E1E2E, // textAreaBg (dark)
    0xFFCDD6F4, // textAreaText (light)
};

/**
 * @brief Modern blue theme with contemporary styling.
 *
 * A clean, modern look with:
 * - Navy blue desktop
 * - Near-white window backgrounds
 * - Bright blue accents and highlights
 * - Subtle 3D effects
 */
constexpr Theme ModernBlue = {
    "Modern Blue",
    // Desktop
    0xFF1E3A5F, // desktop (modern navy)
    0xFF152238, // desktopBorder
    // Window
    0xFFF0F0F0, // windowBg (almost white)
    0xFF3B82F6, // titleBar (bright blue)
    0xFFFFFFFF, // titleBarText
    0xFF94A3B8, // titleBarInactive
    // UI elements
    0xFF3B82F6, // highlight (blue)
    0xFF1F2937, // text (dark)
    0xFF9CA3AF, // textDisabled
    0xFFFFFFFF, // border3dLight
    0xFFD1D5DB, // border3dDark
    // Menu
    0xFFF0F0F0, // menuBg
    0xFF1F2937, // menuText
    0xFF3B82F6, // menuHighlight
    0xFFFFFFFF, // menuHighlightText
    // Icons
    0xFF3B82F6, // iconBg
    0xFFFFFFFF, // iconText
    0xFF1F2937, // iconShadow
    // Text area
    0xFFFFFFFF, // textAreaBg (white)
    0xFF1F2937, // textAreaText (dark)
};

/**
 * @brief High contrast theme for accessibility.
 *
 * An accessibility-focused theme with:
 * - Pure black backgrounds
 * - Pure white text for maximum contrast
 * - Yellow highlights for visibility
 * - No subtle gradations
 */
constexpr Theme HighContrast = {
    "High Contrast",
    // Desktop
    0xFF000000, // desktop (pure black)
    0xFF000000, // desktopBorder
    // Window
    0xFF000000, // windowBg
    0xFF000000, // titleBar
    0xFFFFFFFF, // titleBarText
    0xFF000000, // titleBarInactive
    // UI elements
    0xFFFFFF00, // highlight (yellow)
    0xFFFFFFFF, // text (white)
    0xFF808080, // textDisabled
    0xFFFFFFFF, // border3dLight
    0xFFFFFFFF, // border3dDark
    // Menu
    0xFF000000, // menuBg
    0xFFFFFFFF, // menuText
    0xFFFFFF00, // menuHighlight
    0xFF000000, // menuHighlightText
    // Icons
    0xFFFFFF00, // iconBg
    0xFFFFFFFF, // iconText
    0xFFFFFFFF, // iconShadow
    // Text area
    0xFF000000, // textAreaBg (black)
    0xFFFFFFFF, // textAreaText (white)
};

} // namespace themes

//===----------------------------------------------------------------------===//
// Theme Management Functions
//===----------------------------------------------------------------------===//

/**
 * @brief Returns an array of all built-in themes.
 *
 * Use this function to enumerate available themes for a theme
 * selection UI.
 *
 * @param[out] count If non-null, receives the number of themes.
 * @return Pointer to array of Theme objects.
 *
 * ## Example
 *
 * ```cpp
 * int count;
 * const Theme *themes = getBuiltinThemes(&count);
 * for (int i = 0; i < count; i++) {
 *     printf("Theme: %s\n", themes[i].name);
 * }
 * ```
 */
inline const Theme *getBuiltinThemes(int *count) {
    static const Theme themes[] = {
        themes::ClassicAmiga,
        themes::DarkMode,
        themes::ModernBlue,
        themes::HighContrast,
    };
    if (count) {
        *count = 4;
    }
    return themes;
}

/**
 * @brief Pointer to the currently active theme.
 *
 * This global variable is managed by setTheme() and read by
 * currentTheme(). It is null before setTheme() is first called,
 * in which case currentTheme() returns ClassicAmiga.
 *
 * Defined in theme.cpp.
 */
extern const Theme *g_currentTheme;

/**
 * @brief Sets the active theme for the Workbench.
 *
 * After calling this function, all theme*() color accessors
 * will return colors from the new theme. UI elements should
 * be redrawn after a theme change.
 *
 * @param theme Pointer to the theme to activate.
 *
 * ## Example
 *
 * ```cpp
 * // Switch to dark mode
 * setTheme(&themes::DarkMode);
 * desktop.redraw();
 * ```
 */
void setTheme(const Theme *theme);

/**
 * @brief Returns a reference to the currently active theme.
 *
 * This is the primary way to access theme colors at runtime.
 * If no theme has been set, returns the Classic Amiga theme.
 *
 * @return Reference to the active Theme.
 *
 * ## Usage
 *
 * ```cpp
 * uint32_t bgColor = currentTheme().desktop;
 * gui_fill_rect(win, 0, 0, w, h, bgColor);
 * ```
 */
inline const Theme &currentTheme() {
    return g_currentTheme ? *g_currentTheme : themes::ClassicAmiga;
}

} // namespace workbench
