//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
/**
 * @file main.cpp
 * @brief GUI System Information utility for ViperDOS.
 *
 * This application displays system information in a graphical window,
 * similar to "About This Mac" or the Amiga System Information window.
 * It provides a quick overview of:
 * - Operating system version
 * - Hardware platform
 * - Memory usage
 * - System uptime
 * - Running processes
 *
 * ## Window Layout
 *
 * ```
 * +--[ System Information ]--------------+
 * |    ViperDOS System Info              |
 * | ------------------------------------ |
 * | System:    ViperDOS v0.3.1           |
 * | Kernel:    Viper Hybrid Kernel       |
 * | Platform:  AArch64 (ARM64)           |
 * | CPU:       Cortex-A57 (QEMU)         |
 * |                                      |
 * | +----------------------------------+ |
 * | |  Memory                          | |
 * | |  Total: 128 MB    Free: 83 MB    | |
 * | |  [########............]          | |  Memory bar
 * | +----------------------------------+ |
 * |                                      |
 * | Uptime:    1:23:45                   |
 * | ------------------------------------ |
 * | Running Tasks (12)                   |
 * | PID  Name           State   Priority |
 * |   1  kernel         Running    0     |
 * |   2  displayd       Blocked    5     |
 * | ...                                  |
 * +--------------------------------------+
 * ```
 *
 * ## Auto-Refresh
 *
 * The display automatically refreshes every 2 seconds to show
 * current memory usage, uptime, and process states.
 *
 * @see mem_info.hpp for memory information structure
 * @see task_info.hpp for task information structure
 */
//===----------------------------------------------------------------------===//

#include "../syscall.hpp"
#include <gui.h>
#include <stdio.h>
#include <string.h>
#include <viperdos/mem_info.hpp>
#include <viperdos/task_info.hpp>

//===----------------------------------------------------------------------===//
// Color Constants
//===----------------------------------------------------------------------===//

/**
 * @defgroup SysInfoColors System Info Colors
 * @brief Workbench-style color palette for the system info UI.
 * @{
 */

/** @brief Workbench blue for section backgrounds (0xFF0055AA). */
constexpr uint32_t WB_BLUE = 0xFF0055AA;

/** @brief Pure white for text on blue (0xFFFFFFFF). */
constexpr uint32_t WB_WHITE = 0xFFFFFFFF;

/** @brief Pure black for primary text (0xFF000000). */
constexpr uint32_t WB_BLACK = 0xFF000000;

/** @brief Light gray window background (0xFFAAAAAA). */
constexpr uint32_t WB_GRAY_LIGHT = 0xFFAAAAAA;

/** @brief Dark gray for separators (0xFF555555). */
constexpr uint32_t WB_GRAY_DARK = 0xFF555555;

/** @brief Orange for memory usage bar (0xFFFF8800). */
constexpr uint32_t WB_ORANGE = 0xFFFF8800;

/** @} */ // end SysInfoColors

//===----------------------------------------------------------------------===//
// Layout Constants
//===----------------------------------------------------------------------===//

/**
 * @defgroup SysInfoLayout System Info Layout
 * @brief Window dimensions for the system info display.
 * @{
 */

/** @brief Total window width in pixels. */
constexpr int WIN_WIDTH = 400;

/** @brief Total window height in pixels. */
constexpr int WIN_HEIGHT = 340;

/** @} */ // end SysInfoLayout

//===----------------------------------------------------------------------===//
// System Data
//===----------------------------------------------------------------------===//

/**
 * @brief Aggregated system information for display.
 *
 * This structure holds all the data queried from the kernel
 * that gets displayed in the system info window.
 */
struct SystemData {
    MemInfo mem;        /**< Memory usage statistics. */
    TaskInfo tasks[32]; /**< Array of task information. */
    int taskCount;      /**< Number of tasks in the array. */
    uint64_t uptimeMs;  /**< System uptime in milliseconds. */
};

/**
 * @brief Global system data refreshed periodically.
 */
static SystemData g_data;

static void refreshData() {
    // Get memory info
    sys::mem_info(&g_data.mem);

    // Get task list
    g_data.taskCount = sys::task_list(g_data.tasks, 32);
    if (g_data.taskCount < 0) {
        g_data.taskCount = 0;
    }

    // Get uptime
    g_data.uptimeMs = sys::uptime();
}

static void formatUptime(char *buf, size_t len, uint64_t ms) {
    uint64_t seconds = ms / 1000;
    uint64_t minutes = seconds / 60;
    uint64_t hours = minutes / 60;
    uint64_t days = hours / 24;

    seconds %= 60;
    minutes %= 60;
    hours %= 24;

    if (days > 0) {
        snprintf(buf,
                 len,
                 "%llu day%s, %llu:%02llu:%02llu",
                 days,
                 days == 1 ? "" : "s",
                 hours,
                 minutes,
                 seconds);
    } else {
        snprintf(buf, len, "%llu:%02llu:%02llu", hours, minutes, seconds);
    }
}

static void formatBytes(char *buf, size_t len, uint64_t bytes) {
    if (bytes >= 1024 * 1024 * 1024) {
        snprintf(buf, len, "%llu GB", bytes / (1024 * 1024 * 1024));
    } else if (bytes >= 1024 * 1024) {
        snprintf(buf, len, "%llu MB", bytes / (1024 * 1024));
    } else if (bytes >= 1024) {
        snprintf(buf, len, "%llu KB", bytes / 1024);
    } else {
        snprintf(buf, len, "%llu bytes", bytes);
    }
}

static void drawWindow(gui_window_t *win) {
    // Background
    gui_fill_rect(win, 0, 0, WIN_WIDTH, WIN_HEIGHT, WB_GRAY_LIGHT);

    int y = 15;
    char buf[128];

    // Title
    gui_draw_text(win, 130, y, "ViperDOS System Info", WB_BLACK);
    y += 12;

    // Horizontal line
    gui_draw_hline(win, 20, WIN_WIDTH - 20, y, WB_GRAY_DARK);
    y += 15;

    // Version info
    gui_draw_text(win, 20, y, "System:", WB_BLACK);
    gui_draw_text(win, 120, y, "ViperDOS v0.3.1", WB_GRAY_DARK);
    y += 18;

    gui_draw_text(win, 20, y, "Kernel:", WB_BLACK);
    gui_draw_text(win, 120, y, "Viper Hybrid Kernel", WB_GRAY_DARK);
    y += 18;

    gui_draw_text(win, 20, y, "Platform:", WB_BLACK);
    gui_draw_text(win, 120, y, "AArch64 (ARM64)", WB_GRAY_DARK);
    y += 18;

    gui_draw_text(win, 20, y, "CPU:", WB_BLACK);
    gui_draw_text(win, 120, y, "Cortex-A57 (QEMU)", WB_GRAY_DARK);
    y += 25;

    // Memory section
    gui_fill_rect(win, 15, y - 3, WIN_WIDTH - 30, 60, WB_BLUE);
    gui_draw_text(win, 20, y, "Memory", WB_WHITE);
    y += 18;

    formatBytes(buf, sizeof(buf), g_data.mem.total_bytes);
    char totalBuf[64];
    strncpy(totalBuf, buf, sizeof(totalBuf) - 1);

    formatBytes(buf, sizeof(buf), g_data.mem.free_bytes);
    char freeBuf[64];
    strncpy(freeBuf, buf, sizeof(freeBuf) - 1);

    snprintf(buf, sizeof(buf), "Total: %s    Free: %s", totalBuf, freeBuf);
    gui_draw_text(win, 25, y, buf, WB_WHITE);
    y += 18;

    // Memory bar
    int barX = 25;
    int barW = WIN_WIDTH - 60;
    int barH = 12;
    gui_fill_rect(win, barX, y, barW, barH, WB_GRAY_DARK);

    int usedW = 0;
    if (g_data.mem.total_bytes > 0) {
        usedW = static_cast<int>((g_data.mem.used_bytes * barW) / g_data.mem.total_bytes);
    }
    gui_fill_rect(win, barX, y, usedW, barH, WB_ORANGE);
    y += 25;

    // Uptime
    gui_draw_text(win, 20, y, "Uptime:", WB_BLACK);
    formatUptime(buf, sizeof(buf), g_data.uptimeMs);
    gui_draw_text(win, 120, y, buf, WB_GRAY_DARK);
    y += 25;

    // Tasks section
    gui_draw_hline(win, 20, WIN_WIDTH - 20, y, WB_GRAY_DARK);
    y += 8;

    snprintf(buf, sizeof(buf), "Running Tasks (%d)", g_data.taskCount);
    gui_draw_text(win, 20, y, buf, WB_BLACK);
    y += 18;

    // Task header
    gui_draw_text(win, 25, y, "PID", WB_GRAY_DARK);
    gui_draw_text(win, 60, y, "Name", WB_GRAY_DARK);
    gui_draw_text(win, 200, y, "State", WB_GRAY_DARK);
    gui_draw_text(win, 280, y, "Priority", WB_GRAY_DARK);
    y += 14;

    gui_draw_hline(win, 25, WIN_WIDTH - 25, y, WB_GRAY_DARK);
    y += 4;

    // Task list (show up to 8 tasks)
    int maxTasks = (g_data.taskCount < 8) ? g_data.taskCount : 8;
    for (int i = 0; i < maxTasks; i++) {
        TaskInfo &task = g_data.tasks[i];

        // PID
        snprintf(buf, sizeof(buf), "%d", task.id);
        gui_draw_text(win, 25, y, buf, WB_BLACK);

        // Name (truncate if needed)
        char nameBuf[20];
        strncpy(nameBuf, task.name, 18);
        nameBuf[18] = '\0';
        gui_draw_text(win, 60, y, nameBuf, WB_BLACK);

        // State
        const char *stateStr = "???";
        switch (task.state) {
            case TASK_STATE_READY:
                stateStr = "Ready";
                break;
            case TASK_STATE_RUNNING:
                stateStr = "Running";
                break;
            case TASK_STATE_BLOCKED:
                stateStr = "Blocked";
                break;
            case TASK_STATE_EXITED:
                stateStr = "Exited";
                break;
        }
        gui_draw_text(win, 200, y, stateStr, WB_BLACK);

        // Priority
        snprintf(buf, sizeof(buf), "%d", task.priority);
        gui_draw_text(win, 290, y, buf, WB_BLACK);

        y += 14;
    }

    if (g_data.taskCount > 8) {
        snprintf(buf, sizeof(buf), "... and %d more", g_data.taskCount - 8);
        gui_draw_text(win, 60, y, buf, WB_GRAY_DARK);
    }

    gui_present(win);
}

extern "C" int main() {
    // Initialize GUI
    if (gui_init() != 0) {
        return 1;
    }

    // Create window
    gui_window_t *win = gui_create_window("System Information", WIN_WIDTH, WIN_HEIGHT);
    if (!win) {
        gui_shutdown();
        return 1;
    }

    // Initial data refresh
    refreshData();
    drawWindow(win);

    // Event loop
    uint64_t lastRefresh = sys::uptime();

    while (true) {
        gui_event_t event;
        if (gui_poll_event(win, &event) == 0) {
            if (event.type == GUI_EVENT_CLOSE) {
                break;
            }
        }

        // Refresh data every 2 seconds
        uint64_t now = sys::uptime();
        if (now - lastRefresh >= 2000) {
            refreshData();
            drawWindow(win);
            lastRefresh = now;
        }

        // Yield CPU
        __asm__ volatile("mov x8, #0x0E\n\tsvc #0" ::: "x8");
    }

    gui_destroy_window(win);
    gui_shutdown();
    return 0;
}
