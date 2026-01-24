#pragma once
/// @file colors.hpp
/// @brief Workbench color palette - uses theme system with fallback to viper_colors.h.

#include "../../include/viper_colors.h"
#include "theme.hpp"

namespace workbench {

// ============================================================================
// Compile-time constants for icon definitions and static initializers
// These use the Classic Amiga color values
// ============================================================================
constexpr uint32_t WB_BLUE = VIPER_COLOR_DESKTOP;
constexpr uint32_t WB_BLUE_DARK = VIPER_COLOR_BORDER;
constexpr uint32_t WB_WHITE = VIPER_COLOR_WHITE;
constexpr uint32_t WB_BLACK = VIPER_COLOR_BLACK;
constexpr uint32_t WB_ORANGE = VIPER_COLOR_ORANGE;
constexpr uint32_t WB_GRAY_LIGHT = VIPER_COLOR_GRAY_LIGHT;
constexpr uint32_t WB_GRAY_MED = VIPER_COLOR_GRAY_MED;
constexpr uint32_t WB_GRAY_DARK = VIPER_COLOR_GRAY_DARK;

// ============================================================================
// Theme-aware color functions for runtime drawing
// Use these when drawing UI elements that should respond to theme changes
// ============================================================================
inline uint32_t themeDesktop() { return currentTheme().desktop; }
inline uint32_t themeWindowBg() { return currentTheme().windowBg; }
inline uint32_t themeText() { return currentTheme().text; }
inline uint32_t themeTextDisabled() { return currentTheme().textDisabled; }
inline uint32_t themeHighlight() { return currentTheme().highlight; }
inline uint32_t themeBorderLight() { return currentTheme().border3dLight; }
inline uint32_t themeBorderDark() { return currentTheme().border3dDark; }
inline uint32_t themeMenuBg() { return currentTheme().menuBg; }
inline uint32_t themeMenuText() { return currentTheme().menuText; }
inline uint32_t themeMenuHighlight() { return currentTheme().menuHighlight; }
inline uint32_t themeMenuHighlightText() { return currentTheme().menuHighlightText; }
inline uint32_t themeTitleBar() { return currentTheme().titleBar; }
inline uint32_t themeTitleBarText() { return currentTheme().titleBarText; }
inline uint32_t themeIconBg() { return currentTheme().iconBg; }
inline uint32_t themeIconText() { return currentTheme().iconText; }
inline uint32_t themeIconShadow() { return currentTheme().iconShadow; }

// ViperDOS accent colors (static, not themed)
constexpr uint32_t VIPER_GREEN = VIPER_COLOR_GREEN;
constexpr uint32_t VIPER_BROWN = 0xFF1A1208; // Console background (legacy)

} // namespace workbench
