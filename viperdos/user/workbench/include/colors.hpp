#pragma once
/// @file colors.hpp
/// @brief Workbench color palette - aliases to centralized viper_colors.h.

#include "../../include/viper_colors.h"

namespace workbench {

// Classic Amiga Workbench colors (aliases to centralized definitions)
constexpr uint32_t WB_BLUE       = VIPER_COLOR_DESKTOP;
constexpr uint32_t WB_BLUE_DARK  = VIPER_COLOR_BORDER;
constexpr uint32_t WB_WHITE      = VIPER_COLOR_WHITE;
constexpr uint32_t WB_BLACK      = VIPER_COLOR_BLACK;
constexpr uint32_t WB_ORANGE     = VIPER_COLOR_ORANGE;
constexpr uint32_t WB_GRAY_LIGHT = VIPER_COLOR_GRAY_LIGHT;
constexpr uint32_t WB_GRAY_MED   = VIPER_COLOR_GRAY_MED;
constexpr uint32_t WB_GRAY_DARK  = VIPER_COLOR_GRAY_DARK;

// ViperDOS accent colors
constexpr uint32_t VIPER_GREEN   = VIPER_COLOR_GREEN;
constexpr uint32_t VIPER_BROWN   = 0xFF1A1208;  // Console background (legacy)

} // namespace workbench
