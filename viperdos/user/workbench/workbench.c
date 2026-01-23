/**
 * @file workbench.c
 * @brief ViperDOS Workbench - Amiga-inspired desktop environment.
 *
 * @details
 * Provides a graphical desktop with:
 * - Blue backdrop (classic Workbench style)
 * - Desktop icons for launching applications
 * - Menu bar at top of screen
 * - Click to select, double-click to launch
 */

#include <gui.h>
#include <stdio.h>
#include <string.h>

// Direct serial debug output (bypasses consoled)
static void debug_serial(const char *msg)
{
    __asm__ volatile(
        "mov x0, %[msg]\n\t"
        "mov x8, #0xF0\n\t"  // SYS_DEBUG_PRINT
        "svc #0"
        :: [msg] "r" (msg)
        : "x0", "x8", "memory"
    );
}

// Get system uptime in milliseconds (SYS_TIME_UPTIME = 0xA2)
static uint64_t get_uptime_ms(void)
{
    uint64_t result;
    __asm__ volatile(
        "mov x8, #0xA2\n\t"  // SYS_TIME_UPTIME
        "svc #0\n\t"
        "mov %[result], x1"  // Result is in x1 after syscall
        : [result] "=r" (result)
        :
        : "x0", "x1", "x8", "memory"
    );
    return result;
}

// ============================================================================
// Amiga-Inspired Color Palette
// ============================================================================

#define WB_BLUE         0xFF0055AA  // Classic Workbench blue backdrop
#define WB_BLUE_DARK    0xFF003366  // Darker blue for patterns
#define WB_WHITE        0xFFFFFFFF  // Text, highlights
#define WB_BLACK        0xFF000000  // Outlines
#define WB_ORANGE       0xFFFF8800  // Selected icons, accents
#define WB_GRAY_LIGHT   0xFFAAAAAA  // Menu bar, buttons
#define WB_GRAY_MED     0xFF888888  // Shadows
#define WB_GRAY_DARK    0xFF555555  // Dark elements

// ============================================================================
// Layout Constants
// ============================================================================

#define MENU_BAR_HEIGHT     20
#define ICON_WIDTH          48
#define ICON_HEIGHT         32
#define ICON_SPACING_X      80
#define ICON_SPACING_Y      70
#define ICON_START_X        40
#define ICON_START_Y        50
#define ICON_LABEL_OFFSET   36
#define DOUBLE_CLICK_MS     400

// ============================================================================
// Simple 24x24 Icons (hard-coded pixel art)
// ============================================================================

// Disk icon - represents SYS: drive
static const uint32_t icon_disk_24[24*24] = {
    // Row 0-3: Top of disk
    0,0,0,0,0,0,WB_GRAY_LIGHT,WB_GRAY_LIGHT,WB_GRAY_LIGHT,WB_GRAY_LIGHT,WB_GRAY_LIGHT,WB_GRAY_LIGHT,WB_GRAY_LIGHT,WB_GRAY_LIGHT,WB_GRAY_LIGHT,WB_GRAY_LIGHT,WB_GRAY_LIGHT,WB_GRAY_LIGHT,0,0,0,0,0,0,
    0,0,0,0,0,WB_GRAY_LIGHT,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_GRAY_LIGHT,0,0,0,0,0,
    0,0,0,0,WB_GRAY_LIGHT,WB_WHITE,WB_GRAY_LIGHT,WB_GRAY_LIGHT,WB_GRAY_LIGHT,WB_GRAY_LIGHT,WB_GRAY_LIGHT,WB_GRAY_LIGHT,WB_GRAY_LIGHT,WB_GRAY_LIGHT,WB_GRAY_LIGHT,WB_GRAY_LIGHT,WB_GRAY_LIGHT,WB_GRAY_LIGHT,WB_WHITE,WB_GRAY_LIGHT,0,0,0,0,
    0,0,0,WB_GRAY_LIGHT,WB_WHITE,WB_GRAY_LIGHT,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_LIGHT,WB_WHITE,WB_GRAY_LIGHT,0,0,0,
    // Row 4-7: Body
    0,0,WB_GRAY_LIGHT,WB_WHITE,WB_GRAY_LIGHT,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_LIGHT,WB_WHITE,WB_GRAY_LIGHT,0,0,
    0,0,WB_GRAY_LIGHT,WB_WHITE,WB_GRAY_LIGHT,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_LIGHT,WB_WHITE,WB_GRAY_LIGHT,0,0,
    0,0,WB_GRAY_LIGHT,WB_WHITE,WB_GRAY_LIGHT,WB_GRAY_MED,WB_BLUE,WB_BLUE,WB_BLUE,WB_BLUE,WB_BLUE,WB_BLUE,WB_BLUE,WB_BLUE,WB_BLUE,WB_BLUE,WB_BLUE,WB_BLUE,WB_GRAY_MED,WB_GRAY_LIGHT,WB_WHITE,WB_GRAY_LIGHT,0,0,
    0,0,WB_GRAY_LIGHT,WB_WHITE,WB_GRAY_LIGHT,WB_GRAY_MED,WB_BLUE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_BLUE,WB_GRAY_MED,WB_GRAY_LIGHT,WB_WHITE,WB_GRAY_LIGHT,0,0,
    // Row 8-11: Label area
    0,0,WB_GRAY_LIGHT,WB_WHITE,WB_GRAY_LIGHT,WB_GRAY_MED,WB_BLUE,WB_WHITE,WB_BLACK,WB_BLACK,WB_BLACK,WB_BLACK,WB_BLACK,WB_BLACK,WB_BLACK,WB_BLACK,WB_WHITE,WB_BLUE,WB_GRAY_MED,WB_GRAY_LIGHT,WB_WHITE,WB_GRAY_LIGHT,0,0,
    0,0,WB_GRAY_LIGHT,WB_WHITE,WB_GRAY_LIGHT,WB_GRAY_MED,WB_BLUE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_BLUE,WB_GRAY_MED,WB_GRAY_LIGHT,WB_WHITE,WB_GRAY_LIGHT,0,0,
    0,0,WB_GRAY_LIGHT,WB_WHITE,WB_GRAY_LIGHT,WB_GRAY_MED,WB_BLUE,WB_BLUE,WB_BLUE,WB_BLUE,WB_BLUE,WB_BLUE,WB_BLUE,WB_BLUE,WB_BLUE,WB_BLUE,WB_BLUE,WB_BLUE,WB_GRAY_MED,WB_GRAY_LIGHT,WB_WHITE,WB_GRAY_LIGHT,0,0,
    0,0,WB_GRAY_LIGHT,WB_WHITE,WB_GRAY_LIGHT,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_LIGHT,WB_WHITE,WB_GRAY_LIGHT,0,0,
    // Row 12-15: Slot area
    0,0,WB_GRAY_LIGHT,WB_WHITE,WB_GRAY_LIGHT,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_BLACK,WB_BLACK,WB_BLACK,WB_BLACK,WB_BLACK,WB_BLACK,WB_BLACK,WB_BLACK,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_LIGHT,WB_WHITE,WB_GRAY_LIGHT,0,0,
    0,0,WB_GRAY_LIGHT,WB_WHITE,WB_GRAY_LIGHT,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_BLACK,WB_GRAY_DARK,WB_GRAY_DARK,WB_GRAY_DARK,WB_GRAY_DARK,WB_GRAY_DARK,WB_GRAY_DARK,WB_BLACK,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_LIGHT,WB_WHITE,WB_GRAY_LIGHT,0,0,
    0,0,WB_GRAY_LIGHT,WB_WHITE,WB_GRAY_LIGHT,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_BLACK,WB_BLACK,WB_BLACK,WB_BLACK,WB_BLACK,WB_BLACK,WB_BLACK,WB_BLACK,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_LIGHT,WB_WHITE,WB_GRAY_LIGHT,0,0,
    0,0,WB_GRAY_LIGHT,WB_WHITE,WB_GRAY_LIGHT,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_LIGHT,WB_WHITE,WB_GRAY_LIGHT,0,0,
    // Row 16-19: Bottom
    0,0,WB_GRAY_LIGHT,WB_WHITE,WB_GRAY_LIGHT,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_LIGHT,WB_WHITE,WB_GRAY_LIGHT,0,0,
    0,0,WB_GRAY_LIGHT,WB_WHITE,WB_GRAY_LIGHT,WB_GRAY_LIGHT,WB_GRAY_LIGHT,WB_GRAY_LIGHT,WB_GRAY_LIGHT,WB_GRAY_LIGHT,WB_GRAY_LIGHT,WB_GRAY_LIGHT,WB_GRAY_LIGHT,WB_GRAY_LIGHT,WB_GRAY_LIGHT,WB_GRAY_LIGHT,WB_GRAY_LIGHT,WB_GRAY_LIGHT,WB_GRAY_LIGHT,WB_GRAY_LIGHT,WB_WHITE,WB_GRAY_LIGHT,0,0,
    0,0,0,WB_GRAY_LIGHT,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_GRAY_LIGHT,0,0,0,
    0,0,0,0,WB_GRAY_LIGHT,WB_GRAY_LIGHT,WB_GRAY_LIGHT,WB_GRAY_LIGHT,WB_GRAY_LIGHT,WB_GRAY_LIGHT,WB_GRAY_LIGHT,WB_GRAY_LIGHT,WB_GRAY_LIGHT,WB_GRAY_LIGHT,WB_GRAY_LIGHT,WB_GRAY_LIGHT,WB_GRAY_LIGHT,WB_GRAY_LIGHT,WB_GRAY_LIGHT,WB_GRAY_LIGHT,0,0,0,0,
    // Row 20-23: Empty
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
};

// Terminal icon - for Shell
static const uint32_t icon_shell_24[24*24] = {
    // Simple terminal/monitor shape
    0,0,WB_BLACK,WB_BLACK,WB_BLACK,WB_BLACK,WB_BLACK,WB_BLACK,WB_BLACK,WB_BLACK,WB_BLACK,WB_BLACK,WB_BLACK,WB_BLACK,WB_BLACK,WB_BLACK,WB_BLACK,WB_BLACK,WB_BLACK,WB_BLACK,WB_BLACK,WB_BLACK,0,0,
    0,WB_BLACK,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_BLACK,0,
    WB_BLACK,WB_WHITE,WB_BLACK,WB_BLACK,WB_BLACK,WB_BLACK,WB_BLACK,WB_BLACK,WB_BLACK,WB_BLACK,WB_BLACK,WB_BLACK,WB_BLACK,WB_BLACK,WB_BLACK,WB_BLACK,WB_BLACK,WB_BLACK,WB_BLACK,WB_BLACK,WB_BLACK,WB_BLACK,WB_WHITE,WB_BLACK,
    WB_BLACK,WB_WHITE,WB_BLACK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLACK,WB_WHITE,WB_BLACK,
    WB_BLACK,WB_WHITE,WB_BLACK,WB_BLUE_DARK,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLACK,WB_WHITE,WB_BLACK,
    WB_BLACK,WB_WHITE,WB_BLACK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLACK,WB_WHITE,WB_BLACK,
    WB_BLACK,WB_WHITE,WB_BLACK,WB_BLUE_DARK,WB_WHITE,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLACK,WB_WHITE,WB_BLACK,
    WB_BLACK,WB_WHITE,WB_BLACK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLACK,WB_WHITE,WB_BLACK,
    WB_BLACK,WB_WHITE,WB_BLACK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLACK,WB_WHITE,WB_BLACK,
    WB_BLACK,WB_WHITE,WB_BLACK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLACK,WB_WHITE,WB_BLACK,
    WB_BLACK,WB_WHITE,WB_BLACK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLACK,WB_WHITE,WB_BLACK,
    WB_BLACK,WB_WHITE,WB_BLACK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLACK,WB_WHITE,WB_BLACK,
    WB_BLACK,WB_WHITE,WB_BLACK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLUE_DARK,WB_BLACK,WB_WHITE,WB_BLACK,
    WB_BLACK,WB_WHITE,WB_BLACK,WB_BLACK,WB_BLACK,WB_BLACK,WB_BLACK,WB_BLACK,WB_BLACK,WB_BLACK,WB_BLACK,WB_BLACK,WB_BLACK,WB_BLACK,WB_BLACK,WB_BLACK,WB_BLACK,WB_BLACK,WB_BLACK,WB_BLACK,WB_BLACK,WB_BLACK,WB_WHITE,WB_BLACK,
    0,WB_BLACK,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_BLACK,0,
    0,0,WB_BLACK,WB_BLACK,WB_BLACK,WB_BLACK,WB_BLACK,WB_BLACK,WB_BLACK,WB_BLACK,WB_BLACK,WB_BLACK,WB_BLACK,WB_BLACK,WB_BLACK,WB_BLACK,WB_BLACK,WB_BLACK,WB_BLACK,WB_BLACK,WB_BLACK,WB_BLACK,0,0,
    0,0,0,0,0,0,0,0,0,WB_BLACK,WB_BLACK,WB_BLACK,WB_BLACK,WB_BLACK,WB_BLACK,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,WB_BLACK,WB_GRAY_LIGHT,WB_GRAY_LIGHT,WB_GRAY_LIGHT,WB_GRAY_LIGHT,WB_GRAY_LIGHT,WB_GRAY_LIGHT,WB_BLACK,0,0,0,0,0,0,0,0,
    0,0,0,0,0,WB_BLACK,WB_BLACK,WB_BLACK,WB_BLACK,WB_BLACK,WB_BLACK,WB_BLACK,WB_BLACK,WB_BLACK,WB_BLACK,WB_BLACK,WB_BLACK,WB_BLACK,WB_BLACK,0,0,0,0,0,
    0,0,0,0,WB_BLACK,WB_GRAY_LIGHT,WB_GRAY_LIGHT,WB_GRAY_LIGHT,WB_GRAY_LIGHT,WB_GRAY_LIGHT,WB_GRAY_LIGHT,WB_GRAY_LIGHT,WB_GRAY_LIGHT,WB_GRAY_LIGHT,WB_GRAY_LIGHT,WB_GRAY_LIGHT,WB_GRAY_LIGHT,WB_GRAY_LIGHT,WB_GRAY_LIGHT,WB_BLACK,0,0,0,0,
    0,0,0,0,WB_BLACK,WB_BLACK,WB_BLACK,WB_BLACK,WB_BLACK,WB_BLACK,WB_BLACK,WB_BLACK,WB_BLACK,WB_BLACK,WB_BLACK,WB_BLACK,WB_BLACK,WB_BLACK,WB_BLACK,WB_BLACK,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
};

// Gear icon - for Settings (simplified)
static const uint32_t icon_settings_24[24*24] = {
    0,0,0,0,0,0,0,0,WB_GRAY_LIGHT,WB_GRAY_LIGHT,WB_GRAY_LIGHT,WB_GRAY_LIGHT,WB_GRAY_LIGHT,WB_GRAY_LIGHT,WB_GRAY_LIGHT,WB_GRAY_LIGHT,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,WB_GRAY_LIGHT,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_LIGHT,0,0,0,0,0,0,0,
    0,0,0,0,0,0,WB_GRAY_LIGHT,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_LIGHT,0,0,0,0,0,0,
    0,0,0,WB_GRAY_LIGHT,WB_GRAY_LIGHT,WB_GRAY_LIGHT,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_LIGHT,WB_GRAY_LIGHT,WB_GRAY_LIGHT,WB_GRAY_LIGHT,WB_GRAY_LIGHT,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_LIGHT,WB_GRAY_LIGHT,WB_GRAY_LIGHT,0,0,0,0,
    0,0,0,WB_GRAY_LIGHT,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_LIGHT,0,0,0,0,0,0,WB_GRAY_LIGHT,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_LIGHT,0,0,0,0,
    0,0,0,WB_GRAY_LIGHT,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_LIGHT,0,0,0,0,0,0,0,0,WB_GRAY_LIGHT,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_LIGHT,0,0,0,0,
    0,0,WB_GRAY_LIGHT,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_LIGHT,0,0,0,WB_GRAY_DARK,WB_GRAY_DARK,WB_GRAY_DARK,WB_GRAY_DARK,0,0,0,WB_GRAY_LIGHT,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_LIGHT,0,0,0,
    0,WB_GRAY_LIGHT,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_LIGHT,0,0,0,WB_GRAY_DARK,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_GRAY_DARK,0,0,0,WB_GRAY_LIGHT,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_LIGHT,0,0,
    WB_GRAY_LIGHT,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_LIGHT,0,0,0,WB_GRAY_DARK,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_GRAY_DARK,0,0,0,WB_GRAY_LIGHT,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_LIGHT,0,
    WB_GRAY_LIGHT,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_LIGHT,0,0,0,0,WB_GRAY_DARK,WB_WHITE,WB_WHITE,WB_GRAY_DARK,WB_GRAY_DARK,WB_WHITE,WB_WHITE,WB_GRAY_DARK,0,0,0,0,WB_GRAY_LIGHT,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_LIGHT,
    WB_GRAY_LIGHT,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_LIGHT,0,0,0,0,WB_GRAY_DARK,WB_WHITE,WB_GRAY_DARK,0,0,WB_GRAY_DARK,WB_WHITE,WB_GRAY_DARK,0,0,0,0,WB_GRAY_LIGHT,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_LIGHT,
    WB_GRAY_LIGHT,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_LIGHT,0,0,0,0,WB_GRAY_DARK,WB_WHITE,WB_GRAY_DARK,0,0,WB_GRAY_DARK,WB_WHITE,WB_GRAY_DARK,0,0,0,0,WB_GRAY_LIGHT,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_LIGHT,
    WB_GRAY_LIGHT,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_LIGHT,0,0,0,0,WB_GRAY_DARK,WB_WHITE,WB_GRAY_DARK,0,0,WB_GRAY_DARK,WB_WHITE,WB_GRAY_DARK,0,0,0,0,WB_GRAY_LIGHT,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_LIGHT,
    WB_GRAY_LIGHT,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_LIGHT,0,0,0,0,WB_GRAY_DARK,WB_WHITE,WB_WHITE,WB_GRAY_DARK,WB_GRAY_DARK,WB_WHITE,WB_WHITE,WB_GRAY_DARK,0,0,0,0,WB_GRAY_LIGHT,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_LIGHT,
    WB_GRAY_LIGHT,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_LIGHT,0,0,0,WB_GRAY_DARK,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_GRAY_DARK,0,0,0,WB_GRAY_LIGHT,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_LIGHT,0,
    0,WB_GRAY_LIGHT,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_LIGHT,0,0,0,WB_GRAY_DARK,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_GRAY_DARK,0,0,0,WB_GRAY_LIGHT,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_LIGHT,0,0,
    0,0,WB_GRAY_LIGHT,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_LIGHT,0,0,0,WB_GRAY_DARK,WB_GRAY_DARK,WB_GRAY_DARK,WB_GRAY_DARK,0,0,0,WB_GRAY_LIGHT,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_LIGHT,0,0,0,
    0,0,0,WB_GRAY_LIGHT,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_LIGHT,0,0,0,0,0,0,0,0,WB_GRAY_LIGHT,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_LIGHT,0,0,0,0,
    0,0,0,WB_GRAY_LIGHT,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_LIGHT,0,0,0,0,0,0,WB_GRAY_LIGHT,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_LIGHT,0,0,0,0,
    0,0,0,WB_GRAY_LIGHT,WB_GRAY_LIGHT,WB_GRAY_LIGHT,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_LIGHT,WB_GRAY_LIGHT,WB_GRAY_LIGHT,WB_GRAY_LIGHT,WB_GRAY_LIGHT,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_LIGHT,WB_GRAY_LIGHT,WB_GRAY_LIGHT,0,0,0,0,
    0,0,0,0,0,0,WB_GRAY_LIGHT,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_LIGHT,0,0,0,0,0,0,
    0,0,0,0,0,0,0,WB_GRAY_LIGHT,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_MED,WB_GRAY_LIGHT,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,WB_GRAY_LIGHT,WB_GRAY_LIGHT,WB_GRAY_LIGHT,WB_GRAY_LIGHT,WB_GRAY_LIGHT,WB_GRAY_LIGHT,WB_GRAY_LIGHT,WB_GRAY_LIGHT,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
};

// Info icon - for About (simple "i" in circle)
static const uint32_t icon_about_24[24*24] = {
    0,0,0,0,0,0,0,WB_BLUE,WB_BLUE,WB_BLUE,WB_BLUE,WB_BLUE,WB_BLUE,WB_BLUE,WB_BLUE,WB_BLUE,WB_BLUE,0,0,0,0,0,0,0,
    0,0,0,0,0,WB_BLUE,WB_BLUE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_BLUE,WB_BLUE,0,0,0,0,0,
    0,0,0,0,WB_BLUE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_BLUE,0,0,0,0,
    0,0,0,WB_BLUE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_BLUE,0,0,0,
    0,0,WB_BLUE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_BLUE,WB_BLUE,WB_BLUE,WB_BLUE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_BLUE,0,0,
    0,WB_BLUE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_BLUE,WB_BLUE,WB_BLUE,WB_BLUE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_BLUE,0,
    0,WB_BLUE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_BLUE,0,
    WB_BLUE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_BLUE,
    WB_BLUE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_BLUE,WB_BLUE,WB_BLUE,WB_BLUE,WB_BLUE,WB_BLUE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_BLUE,
    WB_BLUE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_BLUE,WB_BLUE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_BLUE,
    WB_BLUE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_BLUE,WB_BLUE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_BLUE,
    WB_BLUE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_BLUE,WB_BLUE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_BLUE,
    WB_BLUE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_BLUE,WB_BLUE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_BLUE,
    WB_BLUE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_BLUE,WB_BLUE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_BLUE,
    WB_BLUE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_BLUE,WB_BLUE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_BLUE,
    WB_BLUE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_BLUE,WB_BLUE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_BLUE,
    0,WB_BLUE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_BLUE,WB_BLUE,WB_BLUE,WB_BLUE,WB_BLUE,WB_BLUE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_BLUE,0,
    0,WB_BLUE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_BLUE,0,
    0,0,WB_BLUE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_BLUE,0,0,
    0,0,0,WB_BLUE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_BLUE,0,0,0,
    0,0,0,0,WB_BLUE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_BLUE,0,0,0,0,
    0,0,0,0,0,WB_BLUE,WB_BLUE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_WHITE,WB_BLUE,WB_BLUE,0,0,0,0,0,
    0,0,0,0,0,0,0,WB_BLUE,WB_BLUE,WB_BLUE,WB_BLUE,WB_BLUE,WB_BLUE,WB_BLUE,WB_BLUE,WB_BLUE,WB_BLUE,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
};

// ============================================================================
// Desktop Icon Structure
// ============================================================================

typedef struct {
    int x, y;                   // Position on desktop
    const char *label;          // Text below icon
    const char *command;        // Path to launch (NULL = no action)
    const uint32_t *pixels;     // 24x24 icon pixel data
    int selected;               // Is this icon selected?
} desktop_icon_t;

// ============================================================================
// Global State
// ============================================================================

static gui_window_t *g_desktop = NULL;
static uint32_t g_screen_width = 1024;
static uint32_t g_screen_height = 768;

static desktop_icon_t g_icons[] = {
    { 0, 0, "SYS:",     NULL,                   icon_disk_24,     0 },
    { 0, 0, "Shell",    "/sys/consoled.sys",    icon_shell_24,    0 },
    { 0, 0, "Settings", NULL,                   icon_settings_24, 0 },
    { 0, 0, "About",    NULL,                   icon_about_24,    0 },
};
static const int g_icon_count = sizeof(g_icons) / sizeof(g_icons[0]);

// Double-click detection
static int g_last_click_x = -1;
static int g_last_click_y = -1;
static int g_last_click_icon = -1;
static uint64_t g_last_click_time = 0;

// ============================================================================
// Drawing Functions
// ============================================================================

/**
 * @brief Draw the Workbench blue backdrop.
 */
static void draw_backdrop(void)
{
    // Solid Workbench blue
    gui_fill_rect(g_desktop, 0, MENU_BAR_HEIGHT, g_screen_width,
                  g_screen_height - MENU_BAR_HEIGHT, WB_BLUE);
}

/**
 * @brief Draw the menu bar at top of screen.
 */
static void draw_menu_bar(void)
{
    // Menu bar background
    gui_fill_rect(g_desktop, 0, 0, g_screen_width, MENU_BAR_HEIGHT, WB_GRAY_LIGHT);

    // Bottom border
    gui_draw_hline(g_desktop, 0, g_screen_width - 1, MENU_BAR_HEIGHT - 1, WB_GRAY_DARK);

    // Top highlight
    gui_draw_hline(g_desktop, 0, g_screen_width - 1, 0, WB_WHITE);

    // Menu titles
    gui_draw_text(g_desktop, 8, 6, "Workbench", WB_BLACK);
    gui_draw_text(g_desktop, 96, 6, "Window", WB_BLACK);
    gui_draw_text(g_desktop, 168, 6, "Tools", WB_BLACK);

    // Right side: ViperDOS branding
    gui_draw_text(g_desktop, g_screen_width - 80, 6, "ViperDOS", WB_GRAY_DARK);
}

/**
 * @brief Draw a 24x24 icon at the specified position.
 */
static void draw_icon_pixels(int x, int y, const uint32_t *pixels)
{
    uint32_t *fb = gui_get_pixels(g_desktop);
    uint32_t stride = gui_get_stride(g_desktop) / 4;

    for (int py = 0; py < 24; py++) {
        for (int px = 0; px < 24; px++) {
            uint32_t color = pixels[py * 24 + px];
            if (color != 0) {  // 0 = transparent
                int dx = x + px;
                int dy = y + py;
                if (dx >= 0 && dx < (int)g_screen_width &&
                    dy >= 0 && dy < (int)g_screen_height) {
                    fb[dy * stride + dx] = color;
                }
            }
        }
    }
}

/**
 * @brief Draw a single desktop icon.
 */
static void draw_icon(desktop_icon_t *icon)
{
    // Draw selection highlight if selected
    if (icon->selected) {
        // Orange highlight box behind icon
        gui_fill_rect(g_desktop, icon->x - 4, icon->y - 4, 32, 32, WB_ORANGE);
    }

    // Draw the icon pixels (centered in a 24x24 area)
    draw_icon_pixels(icon->x, icon->y, icon->pixels);

    // Draw label below icon (centered)
    int label_len = strlen(icon->label);
    int label_x = icon->x + 12 - (label_len * 4);  // Center under 24px icon
    int label_y = icon->y + ICON_LABEL_OFFSET;

    // Label background for readability (if selected)
    if (icon->selected) {
        gui_fill_rect(g_desktop, label_x - 2, label_y - 1,
                      label_len * 8 + 4, 10, WB_ORANGE);
        gui_draw_text(g_desktop, label_x, label_y, icon->label, WB_WHITE);
    } else {
        // Draw text with shadow for visibility on blue
        gui_draw_text(g_desktop, label_x + 1, label_y + 1, icon->label, WB_BLACK);
        gui_draw_text(g_desktop, label_x, label_y, icon->label, WB_WHITE);
    }
}

/**
 * @brief Draw all desktop icons.
 */
static void draw_all_icons(void)
{
    for (int i = 0; i < g_icon_count; i++) {
        draw_icon(&g_icons[i]);
    }
}

/**
 * @brief Redraw the entire desktop.
 */
static void redraw_desktop(void)
{
    draw_backdrop();
    draw_menu_bar();
    draw_all_icons();
    gui_present(g_desktop);
}

// ============================================================================
// Icon Interaction
// ============================================================================

/**
 * @brief Check if a point is within an icon's clickable area.
 */
static int point_in_icon(int x, int y, desktop_icon_t *icon)
{
    // Icon clickable area: 24x24 icon + label below
    int icon_left = icon->x - 4;
    int icon_top = icon->y - 4;
    int icon_right = icon->x + 28;
    int icon_bottom = icon->y + ICON_LABEL_OFFSET + 12;

    return (x >= icon_left && x < icon_right &&
            y >= icon_top && y < icon_bottom);
}

/**
 * @brief Find which icon (if any) is at the given coordinates.
 * @return Icon index, or -1 if none.
 */
static int find_icon_at(int x, int y)
{
    for (int i = 0; i < g_icon_count; i++) {
        if (point_in_icon(x, y, &g_icons[i])) {
            return i;
        }
    }
    return -1;
}

/**
 * @brief Deselect all icons.
 */
static void deselect_all(void)
{
    for (int i = 0; i < g_icon_count; i++) {
        g_icons[i].selected = 0;
    }
}

/**
 * @brief Select an icon by index.
 */
static void select_icon(int index)
{
    deselect_all();
    if (index >= 0 && index < g_icon_count) {
        g_icons[index].selected = 1;
    }
    redraw_desktop();
}

/**
 * @brief Launch the command associated with an icon.
 */
static void launch_icon(desktop_icon_t *icon)
{
    debug_serial("[workbench] launch_icon called\n");

    if (!icon->command) {
        debug_serial("[workbench] Icon has no command\n");
        printf("[workbench] Icon '%s' has no command\n", icon->label);
        return;
    }

    debug_serial("[workbench] Launching: ");
    debug_serial(icon->command);
    debug_serial("\n");
    printf("[workbench] Launching: %s\n", icon->command);

    // Use inline assembly for spawn syscall
    // SYS_TASK_SPAWN = 0x03 (NOT 0x05 which is SYS_TASK_LIST!)
    uint64_t pid = 0, tid = 0;
    int64_t result;

    __asm__ volatile(
        "mov x0, %[path]\n\t"
        "mov x1, xzr\n\t"       // name = NULL
        "mov x2, xzr\n\t"       // args = NULL
        "mov x8, #0x03\n\t"     // SYS_TASK_SPAWN
        "svc #0\n\t"
        "mov %[result], x0\n\t"
        "mov %[pid], x1\n\t"
        "mov %[tid], x2\n\t"
        : [result] "=r" (result), [pid] "=r" (pid), [tid] "=r" (tid)
        : [path] "r" (icon->command)
        : "x0", "x1", "x2", "x8", "memory"
    );

    if (result == 0) {
        debug_serial("[workbench] Spawn succeeded\n");
        printf("[workbench] Spawned '%s' (pid=%lu)\n", icon->label, pid);
    } else {
        debug_serial("[workbench] Spawn FAILED\n");
        printf("[workbench] Failed to spawn '%s' (error=%ld)\n", icon->label, result);
    }
}

/**
 * @brief Handle a mouse click on the desktop.
 */
static void handle_click(int x, int y, int button)
{
    if (button != 0) return;  // Only handle left button

    int icon_idx = find_icon_at(x, y);

    // Check for double-click using real time
    uint64_t now = get_uptime_ms();
    int is_double_click = 0;

    if (icon_idx >= 0 && icon_idx == g_last_click_icon) {
        uint64_t elapsed = now - g_last_click_time;
        if (elapsed < DOUBLE_CLICK_MS) {  // Use defined constant (400ms)
            is_double_click = 1;
        }
    }

    g_last_click_icon = icon_idx;
    g_last_click_time = now;
    g_last_click_x = x;
    g_last_click_y = y;

    if (is_double_click && icon_idx >= 0) {
        // Double-click: launch the icon
        debug_serial("[workbench] Double-click detected, launching\n");
        launch_icon(&g_icons[icon_idx]);
        // Reset double-click state to prevent immediate re-trigger
        g_last_click_icon = -1;
        g_last_click_time = 0;
    } else if (icon_idx >= 0) {
        // Single click: select the icon
        debug_serial("[workbench] Single click on icon\n");
        select_icon(icon_idx);
    } else {
        // Click on backdrop: deselect all
        deselect_all();
        redraw_desktop();
    }
}

// ============================================================================
// Initialization
// ============================================================================

/**
 * @brief Position icons in a grid layout.
 */
static void layout_icons(void)
{
    int x = ICON_START_X;
    int y = ICON_START_Y;

    for (int i = 0; i < g_icon_count; i++) {
        g_icons[i].x = x;
        g_icons[i].y = y;

        x += ICON_SPACING_X;
        if (x + ICON_WIDTH > (int)g_screen_width - 40) {
            x = ICON_START_X;
            y += ICON_SPACING_Y;
        }
    }
}

// ============================================================================
// Main
// ============================================================================

int main(void)
{
    printf("[workbench] Starting ViperDOS Workbench\n");

    // Initialize GUI
    if (gui_init() != 0) {
        printf("[workbench] Failed to initialize GUI\n");
        return 1;
    }

    // Get display dimensions
    gui_display_info_t info;
    if (gui_get_display_info(&info) == 0) {
        g_screen_width = info.width;
        g_screen_height = info.height;
    }
    printf("[workbench] Display: %ux%u\n", g_screen_width, g_screen_height);

    // Create full-screen desktop surface
    g_desktop = gui_create_window_ex("Workbench", g_screen_width, g_screen_height,
                                      GUI_FLAG_SYSTEM | GUI_FLAG_NO_DECORATIONS);
    if (!g_desktop) {
        printf("[workbench] Failed to create desktop surface\n");
        gui_shutdown();
        return 1;
    }

    // Position at 0,0 (behind all other windows)
    gui_set_position(g_desktop, 0, 0);

    // Layout and draw icons
    layout_icons();
    redraw_desktop();

    printf("[workbench] Desktop ready - double-click Shell to open terminal\n");

    // Event loop
    while (1) {
        gui_event_t event;
        if (gui_poll_event(g_desktop, &event) == 0) {
            switch (event.type) {
                case GUI_EVENT_MOUSE:
                    if (event.mouse.event_type == 1) {  // Button down
                        handle_click(event.mouse.x, event.mouse.y, event.mouse.button);
                    }
                    break;

                case GUI_EVENT_KEY:
                    // Could handle keyboard shortcuts here
                    break;

                case GUI_EVENT_CLOSE:
                    // Don't close the desktop
                    break;

                default:
                    break;
            }
        }

        // Yield to other processes
        __asm__ volatile("mov x8, #0x0E\n\t"
                         "svc #0"
                         ::: "x8");
    }

    gui_destroy_window(g_desktop);
    gui_shutdown();
    return 0;
}
