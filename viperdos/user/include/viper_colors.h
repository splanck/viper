/**
 * @file viper_colors.h
 * @brief Centralized color definitions for ViperDOS.
 *
 * All colors are in ARGB format (0xAARRGGBB).
 * Include this file instead of defining magic color numbers.
 */
#ifndef VIPER_COLORS_H
#define VIPER_COLORS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =============================================================================
 * Core System Colors
 * ============================================================================= */

/** Desktop/workbench background - Amiga-inspired blue */
#define VIPER_COLOR_DESKTOP 0xFF0055AA

/** Screen border - darker blue */
#define VIPER_COLOR_BORDER 0xFF003366

/** Default text color - white */
#define VIPER_COLOR_TEXT 0xFFFFFFFF

/** Default console background - same as desktop */
#define VIPER_COLOR_CONSOLE_BG 0xFF0055AA

/* =============================================================================
 * Window Decoration Colors
 * ============================================================================= */

/** Focused window title bar */
#define VIPER_COLOR_TITLE_FOCUSED 0xFF4080C0

/** Unfocused window title bar */
#define VIPER_COLOR_TITLE_UNFOCUSED 0xFF606060

/** Window border */
#define VIPER_COLOR_WINDOW_BORDER 0xFF303030

/** Close button */
#define VIPER_COLOR_BTN_CLOSE 0xFFCC4444

/** Minimize button */
#define VIPER_COLOR_BTN_MIN 0xFF4040C0

/** Maximize button */
#define VIPER_COLOR_BTN_MAX 0xFF40C040

/* =============================================================================
 * Basic Colors
 * ============================================================================= */

#define VIPER_COLOR_WHITE 0xFFFFFFFF
#define VIPER_COLOR_BLACK 0xFF000000
#define VIPER_COLOR_RED 0xFFCC3333
#define VIPER_COLOR_GREEN 0xFF00AA44
#define VIPER_COLOR_BLUE 0xFF0055AA
#define VIPER_COLOR_YELLOW 0xFFFFDD00
#define VIPER_COLOR_CYAN 0xFF00AAAA
#define VIPER_COLOR_MAGENTA 0xFFAA00AA
#define VIPER_COLOR_ORANGE 0xFFFF8800

/* =============================================================================
 * Gray Shades
 * ============================================================================= */

#define VIPER_COLOR_GRAY_LIGHT 0xFFAAAAAA
#define VIPER_COLOR_GRAY_MED 0xFF888888
#define VIPER_COLOR_GRAY_DARK 0xFF555555

/* =============================================================================
 * Standard ANSI Terminal Colors (for consoled)
 * ============================================================================= */

/* Standard colors (30-37 fg, 40-47 bg) */
#define ANSI_COLOR_BLACK 0xFF000000
#define ANSI_COLOR_RED 0xFFAA0000
#define ANSI_COLOR_GREEN 0xFF00AA00
#define ANSI_COLOR_YELLOW 0xFFAAAA00
#define ANSI_COLOR_BLUE 0xFF0000AA
#define ANSI_COLOR_MAGENTA 0xFFAA00AA
#define ANSI_COLOR_CYAN 0xFF00AAAA
#define ANSI_COLOR_WHITE 0xFFAAAAAA

/* Bright colors (90-97 fg, 100-107 bg) */
#define ANSI_COLOR_BRIGHT_BLACK 0xFF555555
#define ANSI_COLOR_BRIGHT_RED 0xFFFF5555
#define ANSI_COLOR_BRIGHT_GREEN 0xFF55FF55
#define ANSI_COLOR_BRIGHT_YELLOW 0xFFFFFF55
#define ANSI_COLOR_BRIGHT_BLUE 0xFF5555FF
#define ANSI_COLOR_BRIGHT_MAGENTA 0xFFFF55FF
#define ANSI_COLOR_BRIGHT_CYAN 0xFF55FFFF
#define ANSI_COLOR_BRIGHT_WHITE 0xFFFFFFFF

/* =============================================================================
 * ANSI Escape Sequences for Console Output
 * ============================================================================= */

/** Reset to default colors (white on blue) */
#define ANSI_RESET "\033[0m"

/** Set foreground to bright white */
#define ANSI_FG_WHITE "\033[97m"

/** Set background to blue (ANSI color 4) */
#define ANSI_BG_BLUE "\033[44m"

/** Combined: white text on blue background */
#define ANSI_DEFAULT_COLORS "\033[0m"

#ifdef __cplusplus
}

/* C++ namespace version for type safety */
namespace viper {
namespace colors {

constexpr uint32_t DESKTOP = VIPER_COLOR_DESKTOP;
constexpr uint32_t BORDER = VIPER_COLOR_BORDER;
constexpr uint32_t TEXT = VIPER_COLOR_TEXT;
constexpr uint32_t CONSOLE_BG = VIPER_COLOR_CONSOLE_BG;

constexpr uint32_t WHITE = VIPER_COLOR_WHITE;
constexpr uint32_t BLACK = VIPER_COLOR_BLACK;
constexpr uint32_t RED = VIPER_COLOR_RED;
constexpr uint32_t GREEN = VIPER_COLOR_GREEN;
constexpr uint32_t BLUE = VIPER_COLOR_BLUE;
constexpr uint32_t YELLOW = VIPER_COLOR_YELLOW;

constexpr uint32_t GRAY_LIGHT = VIPER_COLOR_GRAY_LIGHT;
constexpr uint32_t GRAY_MED = VIPER_COLOR_GRAY_MED;
constexpr uint32_t GRAY_DARK = VIPER_COLOR_GRAY_DARK;

} // namespace colors
} // namespace viper

#endif /* __cplusplus */

#endif /* VIPER_COLORS_H */
