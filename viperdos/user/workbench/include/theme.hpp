#pragma once
/// @file theme.hpp
/// @brief Theme system for Workbench colors.

#include <stdint.h>

namespace workbench {

/// @brief Theme color scheme.
struct Theme {
    const char *name;

    // Desktop colors
    uint32_t desktop;       // Backdrop color
    uint32_t desktopBorder; // Desktop border

    // Window colors
    uint32_t windowBg;      // Window background
    uint32_t titleBar;      // Active title bar
    uint32_t titleBarText;  // Title bar text
    uint32_t titleBarInactive;

    // UI element colors
    uint32_t highlight;     // Selection highlight (e.g. orange)
    uint32_t text;          // Default text color
    uint32_t textDisabled;  // Disabled text
    uint32_t border3dLight; // 3D border light side
    uint32_t border3dDark;  // 3D border dark side

    // Menu colors
    uint32_t menuBg;
    uint32_t menuText;
    uint32_t menuHighlight;
    uint32_t menuHighlightText;

    // Icon colors
    uint32_t iconBg;        // Icon label background
    uint32_t iconText;      // Icon label text
    uint32_t iconShadow;    // Icon label shadow
};

/// @brief Built-in themes.
namespace themes {

/// Classic Amiga Workbench 3.x colors
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
};

/// Dark mode theme
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
};

/// Modern blue theme
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
};

/// High Contrast theme for accessibility
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
};

} // namespace themes

/// @brief Get list of available themes.
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

/// @brief Currently active theme.
extern const Theme *g_currentTheme;

/// @brief Set the active theme.
void setTheme(const Theme *theme);

/// @brief Get the active theme.
inline const Theme &currentTheme() {
    return g_currentTheme ? *g_currentTheme : themes::ClassicAmiga;
}

} // namespace workbench
