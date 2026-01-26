//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
/**
 * @file main.cpp
 * @brief GUI Preferences application for ViperDOS.
 *
 * This application provides a tabbed preferences panel for viewing and
 * (in future versions) modifying system settings. The interface is inspired
 * by the Amiga Workbench Preferences application.
 *
 * ## Application Structure
 *
 * The preferences panel has a sidebar with category buttons and a content
 * area that displays settings for the selected category.
 *
 * ```
 * +------------+--------------------------------+
 * | [S] Screen |  Screen Preferences            |
 * | [I] Input  |  ----------------------------- |
 * | [T] Time   |                                |
 * | [?] About  |  Resolution: 1024 x 768        |
 * |            |  Color Depth: 32-bit           |
 * |            |  Backdrop: Workbench Blue      |
 * |            |                                |
 * +------------+--------------------------------+
 * |            | [Use] [Cancel] [Save]          |
 * +------------+--------------------------------+
 * ```
 *
 * ## Categories
 *
 * | Category | Description                        |
 * |----------|------------------------------------|
 * | Screen   | Display resolution and backdrop    |
 * | Input    | Pointer and keyboard settings      |
 * | Time     | System clock and uptime            |
 * | About    | System information and version     |
 *
 * ## Limitations
 *
 * Currently, preferences are read-only. The "Use", "Cancel", and "Save"
 * buttons are placeholders for future functionality.
 *
 * @see workbench for theme switching (available via Tools menu)
 */
//===----------------------------------------------------------------------===//

#include "../syscall.hpp"
#include <gui.h>
#include <stdio.h>
#include <string.h>
#include <viperdos/mem_info.hpp>

//===----------------------------------------------------------------------===//
// Color Constants
//===----------------------------------------------------------------------===//

/**
 * @defgroup PrefsColors Preferences Color Palette
 * @brief Workbench-style colors for the preferences UI.
 * @{
 */

/** @brief Classic Workbench blue (0xFF0055AA). */
constexpr uint32_t WB_BLUE = 0xFF0055AA;

/** @brief Pure white (0xFFFFFFFF). */
constexpr uint32_t WB_WHITE = 0xFFFFFFFF;

/** @brief Pure black (0xFF000000). */
constexpr uint32_t WB_BLACK = 0xFF000000;

/** @brief Light gray for backgrounds (0xFFAAAAAA). */
constexpr uint32_t WB_GRAY_LIGHT = 0xFFAAAAAA;

/** @brief Medium gray for disabled elements (0xFF888888). */
constexpr uint32_t WB_GRAY_MED = 0xFF888888;

/** @brief Dark gray for shadows (0xFF555555). */
constexpr uint32_t WB_GRAY_DARK = 0xFF555555;

/** @brief Orange for highlights (0xFFFF8800). Unused. */
[[maybe_unused]] constexpr uint32_t WB_ORANGE = 0xFFFF8800;

/** @} */ // end PrefsColors

//===----------------------------------------------------------------------===//
// Layout Constants
//===----------------------------------------------------------------------===//

/**
 * @defgroup PrefsLayout Preferences Layout Constants
 * @brief Dimensions for the preferences window layout.
 * @{
 */

/** @brief Total window width in pixels. */
constexpr int WIN_WIDTH = 500;

/** @brief Total window height in pixels. */
constexpr int WIN_HEIGHT = 360;

/** @brief Width of the category sidebar in pixels. */
constexpr int SIDEBAR_WIDTH = 110;

/** @brief X position where content area begins. */
constexpr int CONTENT_X = SIDEBAR_WIDTH + 10;

/** @brief Height of action buttons at the bottom. */
constexpr int BUTTON_HEIGHT = 24;

/** @brief Height of each category button in sidebar. */
constexpr int CATEGORY_HEIGHT = 28;

/** @} */ // end PrefsLayout

//===----------------------------------------------------------------------===//
// Category Types
//===----------------------------------------------------------------------===//

/**
 * @brief Enumeration of preference categories.
 *
 * Each category corresponds to a tab in the sidebar and a different
 * content panel.
 */
enum class Category {
    Screen, /**< Display settings (resolution, backdrop). */
    Input,  /**< Pointer and keyboard settings. */
    Time,   /**< System time and uptime display. */
    About   /**< System information and version. */
};

/**
 * @brief Metadata for a category button in the sidebar.
 */
struct CategoryInfo {
    const char *name; /**< Display name for the category. */
    const char *icon; /**< Icon text (e.g., "[S]"). */
    Category cat;     /**< Category enum value. */
};

/**
 * @brief Array of category definitions.
 */
static const CategoryInfo g_categories[] = {
    {"Screen", "[S]", Category::Screen},
    {"Input", "[I]", Category::Input},
    {"Time", "[T]", Category::Time},
    {"About", "[?]", Category::About},
};

/** @brief Number of categories. */
constexpr int NUM_CATEGORIES = sizeof(g_categories) / sizeof(g_categories[0]);

// State
static Category g_currentCategory = Category::Screen;
static int g_hoveredCategory = -1;
static MemInfo g_memInfo;

static void drawButton3D(
    gui_window_t *win, int x, int y, int w, int h, const char *label, bool pressed) {
    uint32_t bg = pressed ? WB_GRAY_MED : WB_GRAY_LIGHT;
    gui_fill_rect(win, x, y, w, h, bg);

    if (pressed) {
        gui_draw_hline(win, x, x + w - 1, y, WB_GRAY_DARK);
        gui_draw_vline(win, x, y, y + h - 1, WB_GRAY_DARK);
        gui_draw_hline(win, x, x + w - 1, y + h - 1, WB_WHITE);
        gui_draw_vline(win, x + w - 1, y, y + h - 1, WB_WHITE);
    } else {
        gui_draw_hline(win, x, x + w - 1, y, WB_WHITE);
        gui_draw_vline(win, x, y, y + h - 1, WB_WHITE);
        gui_draw_hline(win, x, x + w - 1, y + h - 1, WB_GRAY_DARK);
        gui_draw_vline(win, x + w - 1, y, y + h - 1, WB_GRAY_DARK);
    }

    int textX = x + (w - static_cast<int>(strlen(label)) * 8) / 2;
    int textY = y + (h - 10) / 2;
    gui_draw_text(win, textX, textY, label, WB_BLACK);
}

static void drawSidebar(gui_window_t *win) {
    // Sidebar background
    gui_fill_rect(win, 0, 0, SIDEBAR_WIDTH, WIN_HEIGHT, WB_GRAY_MED);

    // Category buttons
    int y = 15;
    for (int i = 0; i < NUM_CATEGORIES; i++) {
        bool selected = (g_categories[i].cat == g_currentCategory);

        if (selected) {
            gui_fill_rect(win, 5, y, SIDEBAR_WIDTH - 10, CATEGORY_HEIGHT, WB_BLUE);
        } else if (i == g_hoveredCategory) {
            gui_fill_rect(win, 5, y, SIDEBAR_WIDTH - 10, CATEGORY_HEIGHT, WB_GRAY_LIGHT);
        }

        // Icon
        uint32_t textColor = selected ? WB_WHITE : WB_BLACK;
        gui_draw_text(win, 12, y + 8, g_categories[i].icon, textColor);

        // Name
        gui_draw_text(win, 38, y + 8, g_categories[i].name, textColor);

        y += CATEGORY_HEIGHT + 4;
    }

    // Sidebar border
    gui_draw_vline(win, SIDEBAR_WIDTH - 1, 0, WIN_HEIGHT, WB_GRAY_DARK);
}

static void drawScreenPrefs(gui_window_t *win) {
    int y = 25;

    gui_draw_text(win, CONTENT_X, y, "Screen Preferences", WB_BLACK);
    y += 25;

    gui_draw_hline(win, CONTENT_X, WIN_WIDTH - 20, y, WB_GRAY_DARK);
    y += 15;

    // Resolution
    gui_draw_text(win, CONTENT_X, y, "Resolution:", WB_BLACK);
    gui_draw_text(win, CONTENT_X + 100, y, "1024 x 768", WB_GRAY_DARK);
    y += 25;

    // Color depth
    gui_draw_text(win, CONTENT_X, y, "Color Depth:", WB_BLACK);
    gui_draw_text(win, CONTENT_X + 100, y, "32-bit (True Color)", WB_GRAY_DARK);
    y += 25;

    // Backdrop
    gui_draw_text(win, CONTENT_X, y, "Backdrop:", WB_BLACK);
    gui_fill_rect(win, CONTENT_X + 100, y - 2, 80, 16, WB_BLUE);
    gui_draw_text(win, CONTENT_X + 190, y, "Workbench Blue", WB_GRAY_DARK);
    y += 35;

    // Note
    gui_fill_rect(win, CONTENT_X, y, WIN_WIDTH - CONTENT_X - 20, 50, WB_BLUE);
    gui_draw_text(win, CONTENT_X + 10, y + 10, "Screen preferences are read-only", WB_WHITE);
    gui_draw_text(win, CONTENT_X + 10, y + 28, "in this version of ViperDOS.", WB_WHITE);
}

static void drawInputPrefs(gui_window_t *win) {
    int y = 25;

    gui_draw_text(win, CONTENT_X, y, "Input Preferences", WB_BLACK);
    y += 25;

    gui_draw_hline(win, CONTENT_X, WIN_WIDTH - 20, y, WB_GRAY_DARK);
    y += 15;

    // Pointer section
    gui_draw_text(win, CONTENT_X, y, "Pointer", WB_BLUE);
    y += 20;

    gui_draw_text(win, CONTENT_X + 10, y, "Speed:", WB_BLACK);
    gui_draw_text(win, CONTENT_X + 100, y, "Medium", WB_GRAY_DARK);
    y += 20;

    gui_draw_text(win, CONTENT_X + 10, y, "Double-click:", WB_BLACK);
    gui_draw_text(win, CONTENT_X + 100, y, "400 ms", WB_GRAY_DARK);
    y += 30;

    // Keyboard section
    gui_draw_text(win, CONTENT_X, y, "Keyboard", WB_BLUE);
    y += 20;

    gui_draw_text(win, CONTENT_X + 10, y, "Repeat delay:", WB_BLACK);
    gui_draw_text(win, CONTENT_X + 120, y, "500 ms", WB_GRAY_DARK);
    y += 20;

    gui_draw_text(win, CONTENT_X + 10, y, "Repeat rate:", WB_BLACK);
    gui_draw_text(win, CONTENT_X + 120, y, "30 Hz", WB_GRAY_DARK);
    y += 20;

    gui_draw_text(win, CONTENT_X + 10, y, "Layout:", WB_BLACK);
    gui_draw_text(win, CONTENT_X + 120, y, "US English", WB_GRAY_DARK);
}

static void drawTimePrefs(gui_window_t *win) {
    int y = 25;

    gui_draw_text(win, CONTENT_X, y, "Time Preferences", WB_BLACK);
    y += 25;

    gui_draw_hline(win, CONTENT_X, WIN_WIDTH - 20, y, WB_GRAY_DARK);
    y += 15;

    // Current time
    uint64_t uptime = sys::uptime();
    uint64_t seconds = uptime / 1000;
    uint64_t minutes = (seconds / 60) % 60;
    uint64_t hours = (seconds / 3600) % 24;

    char timeBuf[32];
    snprintf(timeBuf, sizeof(timeBuf), "%02llu:%02llu:%02llu", hours, minutes, seconds % 60);

    gui_draw_text(win, CONTENT_X, y, "System Time:", WB_BLACK);
    gui_draw_text(win, CONTENT_X + 110, y, timeBuf, WB_GRAY_DARK);
    y += 25;

    // Uptime
    uint64_t upHours = seconds / 3600;
    uint64_t upMins = (seconds / 60) % 60;
    snprintf(timeBuf, sizeof(timeBuf), "%llu hours, %llu minutes", upHours, upMins);

    gui_draw_text(win, CONTENT_X, y, "Uptime:", WB_BLACK);
    gui_draw_text(win, CONTENT_X + 110, y, timeBuf, WB_GRAY_DARK);
    y += 25;

    // Time zone
    gui_draw_text(win, CONTENT_X, y, "Time Zone:", WB_BLACK);
    gui_draw_text(win, CONTENT_X + 110, y, "UTC", WB_GRAY_DARK);
    y += 25;

    // Clock format
    gui_draw_text(win, CONTENT_X, y, "Clock Format:", WB_BLACK);
    gui_draw_text(win, CONTENT_X + 110, y, "24-hour", WB_GRAY_DARK);
}

static void drawAboutPrefs(gui_window_t *win) {
    int y = 25;

    gui_draw_text(win, CONTENT_X, y, "About ViperDOS", WB_BLACK);
    y += 25;

    gui_draw_hline(win, CONTENT_X, WIN_WIDTH - 20, y, WB_GRAY_DARK);
    y += 20;

    // Logo area
    gui_fill_rect(win, CONTENT_X, y, 60, 60, WB_BLUE);
    gui_draw_text(win, CONTENT_X + 8, y + 20, "VIPER", WB_WHITE);
    gui_draw_text(win, CONTENT_X + 12, y + 35, "DOS", WB_WHITE);

    // Version info
    gui_draw_text(win, CONTENT_X + 80, y + 5, "ViperDOS Workbench", WB_BLACK);
    gui_draw_text(win, CONTENT_X + 80, y + 22, "Version 0.3.1", WB_GRAY_DARK);
    gui_draw_text(win, CONTENT_X + 80, y + 39, "Viper Microkernel OS", WB_GRAY_DARK);
    y += 75;

    // System info
    sys::mem_info(&g_memInfo);

    char buf[64];
    snprintf(buf,
             sizeof(buf),
             "Memory: %llu MB total, %llu MB free",
             g_memInfo.total_bytes / (1024 * 1024),
             g_memInfo.free_bytes / (1024 * 1024));
    gui_draw_text(win, CONTENT_X, y, buf, WB_BLACK);
    y += 20;

    gui_draw_text(win, CONTENT_X, y, "Platform: AArch64 (ARM64)", WB_BLACK);
    y += 20;

    gui_draw_text(win, CONTENT_X, y, "Display: 1024x768 32bpp", WB_BLACK);
    y += 30;

    // Copyright
    gui_draw_text(win, CONTENT_X, y, "(C) 2025 ViperDOS Team", WB_GRAY_DARK);
}

static void drawContent(gui_window_t *win) {
    // Content area background
    gui_fill_rect(win, SIDEBAR_WIDTH, 0, WIN_WIDTH - SIDEBAR_WIDTH, WIN_HEIGHT - 45, WB_GRAY_LIGHT);

    switch (g_currentCategory) {
        case Category::Screen:
            drawScreenPrefs(win);
            break;
        case Category::Input:
            drawInputPrefs(win);
            break;
        case Category::Time:
            drawTimePrefs(win);
            break;
        case Category::About:
            drawAboutPrefs(win);
            break;
    }
}

static void drawBottomBar(gui_window_t *win) {
    // Bottom bar background
    gui_fill_rect(
        win, SIDEBAR_WIDTH, WIN_HEIGHT - 45, WIN_WIDTH - SIDEBAR_WIDTH, 45, WB_GRAY_LIGHT);
    gui_draw_hline(win, SIDEBAR_WIDTH, WIN_WIDTH, WIN_HEIGHT - 45, WB_GRAY_DARK);

    // Buttons
    int btnY = WIN_HEIGHT - 35;
    int btnW = 70;

    drawButton3D(win, WIN_WIDTH - 240, btnY, btnW, BUTTON_HEIGHT, "Use", false);
    drawButton3D(win, WIN_WIDTH - 160, btnY, btnW, BUTTON_HEIGHT, "Cancel", false);
    drawButton3D(win, WIN_WIDTH - 80, btnY, btnW, BUTTON_HEIGHT, "Save", false);
}

static void drawWindow(gui_window_t *win) {
    drawSidebar(win);
    drawContent(win);
    drawBottomBar(win);
    gui_present(win);
}

static int findCategoryAt(int x, int y) {
    if (x >= SIDEBAR_WIDTH)
        return -1;

    int catY = 15;
    for (int i = 0; i < NUM_CATEGORIES; i++) {
        if (y >= catY && y < catY + CATEGORY_HEIGHT) {
            return i;
        }
        catY += CATEGORY_HEIGHT + 4;
    }
    return -1;
}

static bool handleClick(int x, int y, int button) {
    if (button != 0)
        return false;

    // Check category clicks
    int catIdx = findCategoryAt(x, y);
    if (catIdx >= 0) {
        g_currentCategory = g_categories[catIdx].cat;
        return false;
    }

    // Check button clicks
    int btnY = WIN_HEIGHT - 35;
    if (y >= btnY && y < btnY + BUTTON_HEIGHT) {
        // Cancel button closes window
        if (x >= WIN_WIDTH - 160 && x < WIN_WIDTH - 90) {
            return true; // Signal to close
        }
    }
    return false;
}

extern "C" int main() {
    // Initialize GUI
    if (gui_init() != 0) {
        return 1;
    }

    // Create window
    gui_window_t *win = gui_create_window("Preferences", WIN_WIDTH, WIN_HEIGHT);
    if (!win) {
        gui_shutdown();
        return 1;
    }

    // Initial draw
    drawWindow(win);

    // Event loop
    uint64_t lastRefresh = sys::uptime();
    bool running = true;

    while (running) {
        gui_event_t event;
        if (gui_poll_event(win, &event) == 0) {
            switch (event.type) {
                case GUI_EVENT_CLOSE:
                    running = false;
                    break;

                case GUI_EVENT_MOUSE:
                    if (event.mouse.event_type == 1) { // Button down
                        if (handleClick(event.mouse.x, event.mouse.y, event.mouse.button)) {
                            running = false;
                        }
                        drawWindow(win);
                    } else if (event.mouse.event_type == 0) { // Mouse move
                        int newHover = findCategoryAt(event.mouse.x, event.mouse.y);
                        if (newHover != g_hoveredCategory) {
                            g_hoveredCategory = newHover;
                            drawWindow(win);
                        }
                    }
                    break;

                default:
                    break;
            }
        }

        // Refresh time display every second if on Time tab
        if (g_currentCategory == Category::Time) {
            uint64_t now = sys::uptime();
            if (now - lastRefresh >= 1000) {
                drawWindow(win);
                lastRefresh = now;
            }
        }

        // Yield CPU
        __asm__ volatile("mov x8, #0x0E\n\tsvc #0" ::: "x8");
    }

    gui_destroy_window(win);
    gui_shutdown();
    return 0;
}
