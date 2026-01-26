//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
/**
 * @file main.cpp
 * @brief GUI Task Manager for ViperDOS.
 *
 * This application provides a graphical view of running processes on the
 * system. It displays process information and provides controls for
 * managing tasks.
 *
 * ## Window Layout
 *
 * ```
 * +--[ Task Manager ]-------------------+
 * | Task Manager              X tasks   |  Header
 * +-------------------------------------+
 * | PID  Name           State  Pri  CPU |  Column Headers
 * +-------------------------------------+
 * | 1    kernel         Running  0   1M |
 * | 2    displayd       Blocked  5   2K |  Task List
 * | 3    workbench      Running  5   15K|
 * | ...                                 |
 * +-------------------------------------+
 * | Memory: 45 / 128 MB                 |  Status Bar
 * | [End Task] [Priority...] [Refresh] |  Buttons
 * +-------------------------------------+
 * ```
 *
 * ## Task States
 *
 * | State   | Description                     | Color  |
 * |---------|---------------------------------|--------|
 * | Ready   | Waiting to be scheduled         | Black  |
 * | Running | Currently executing             | Green  |
 * | Blocked | Waiting for I/O or event        | Gray   |
 * | Exited  | Terminated but not yet reaped   | Red    |
 *
 * ## Controls
 *
 * - **Arrow keys**: Navigate task selection
 * - **F5**: Refresh task list
 * - **End Task**: Terminate selected process (not yet implemented)
 * - **Priority**: Change process priority (not yet implemented)
 * - **Refresh**: Manually refresh the task list
 *
 * ## Auto-Refresh
 *
 * The task list automatically refreshes every 3 seconds to show
 * current process states.
 *
 * @see task_info.hpp for TaskInfo structure
 * @see mem_info.hpp for MemInfo structure
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
 * @defgroup TaskManColors Task Manager Colors
 * @brief Color palette for the task manager UI.
 * @{
 */

/** @brief Workbench blue for headers (0xFF0055AA). */
constexpr uint32_t WB_BLUE = 0xFF0055AA;

/** @brief Pure white (0xFFFFFFFF). */
constexpr uint32_t WB_WHITE = 0xFFFFFFFF;

/** @brief Pure black (0xFF000000). */
constexpr uint32_t WB_BLACK = 0xFF000000;

/** @brief Light gray background (0xFFAAAAAA). */
constexpr uint32_t WB_GRAY_LIGHT = 0xFFAAAAAA;

/** @brief Medium gray for disabled items (0xFF888888). */
constexpr uint32_t WB_GRAY_MED = 0xFF888888;

/** @brief Dark gray for borders (0xFF555555). */
constexpr uint32_t WB_GRAY_DARK = 0xFF555555;

/** @brief Orange highlight (unused). */
[[maybe_unused]] constexpr uint32_t WB_ORANGE = 0xFFFF8800;

/** @brief Red for exited/error states (0xFFFF4444). */
constexpr uint32_t WB_RED = 0xFFFF4444;

/** @brief Green for running processes (0xFF00AA44). */
constexpr uint32_t WB_GREEN = 0xFF00AA44;

/** @} */ // end TaskManColors

//===----------------------------------------------------------------------===//
// Layout Constants
//===----------------------------------------------------------------------===//

/**
 * @defgroup TaskManLayout Task Manager Layout
 * @brief Dimensions for the task manager window.
 * @{
 */

/** @brief Total window width in pixels. */
constexpr int WIN_WIDTH = 480;

/** @brief Total window height in pixels. */
constexpr int WIN_HEIGHT = 380;

/** @brief Height of the title/header area. */
constexpr int HEADER_HEIGHT = 30;

/** @brief Height of each task row in the list. */
constexpr int ROW_HEIGHT = 18;

/** @brief Y position where the task list begins. */
constexpr int LIST_TOP = 50;

/** @brief Y position where the task list ends. */
constexpr int LIST_BOTTOM = WIN_HEIGHT - 50;

/** @brief Height of action buttons. */
constexpr int BUTTON_HEIGHT = 24;

/** @brief Y position of the button row. */
constexpr int BUTTON_Y = WIN_HEIGHT - 35;

/** @} */ // end TaskManLayout

//===----------------------------------------------------------------------===//
// Column Positions
//===----------------------------------------------------------------------===//

/**
 * @defgroup TaskManColumns Task List Columns
 * @brief X positions for task list columns.
 * @{
 */

/** @brief X position of PID column. */
constexpr int COL_PID = 15;

/** @brief X position of Name column. */
constexpr int COL_NAME = 55;

/** @brief X position of State column. */
constexpr int COL_STATE = 200;

/** @brief X position of Priority column. */
constexpr int COL_PRI = 280;

/** @brief X position of CPU ticks column. */
constexpr int COL_CPU = 330;

// constexpr int COL_MEM = 400;  // Reserved for future memory column

/** @} */ // end TaskManColumns

// State
static TaskInfo g_tasks[64];
static int g_taskCount = 0;
static int g_selectedTask = -1;
static int g_scrollOffset = 0;
static MemInfo g_memInfo;

static void refreshTasks() {
    g_taskCount = sys::task_list(g_tasks, 64);
    if (g_taskCount < 0) {
        g_taskCount = 0;
    }
    sys::mem_info(&g_memInfo);

    // Validate selection
    if (g_selectedTask >= g_taskCount) {
        g_selectedTask = g_taskCount - 1;
    }
}

static void drawButton(gui_window_t *win, int x, int y, int w, const char *label, bool enabled) {
    uint32_t bgColor = enabled ? WB_GRAY_LIGHT : WB_GRAY_MED;
    uint32_t textColor = enabled ? WB_BLACK : WB_GRAY_DARK;

    // Button background
    gui_fill_rect(win, x, y, w, BUTTON_HEIGHT, bgColor);

    // 3D effect
    gui_draw_hline(win, x, x + w - 1, y, WB_WHITE);
    gui_draw_vline(win, x, y, y + BUTTON_HEIGHT - 1, WB_WHITE);
    gui_draw_hline(win, x, x + w - 1, y + BUTTON_HEIGHT - 1, WB_GRAY_DARK);
    gui_draw_vline(win, x + w - 1, y, y + BUTTON_HEIGHT - 1, WB_GRAY_DARK);

    // Center text
    int textX = x + (w - static_cast<int>(strlen(label)) * 8) / 2;
    int textY = y + 6;
    gui_draw_text(win, textX, textY, label, textColor);
}

static void drawWindow(gui_window_t *win) {
    // Background
    gui_fill_rect(win, 0, 0, WIN_WIDTH, WIN_HEIGHT, WB_GRAY_LIGHT);

    char buf[128];

    // Title bar area
    gui_fill_rect(win, 0, 0, WIN_WIDTH, HEADER_HEIGHT, WB_BLUE);
    gui_draw_text(win, 15, 8, "Task Manager", WB_WHITE);

    snprintf(buf, sizeof(buf), "%d tasks", g_taskCount);
    gui_draw_text(win, WIN_WIDTH - 100, 8, buf, WB_WHITE);

    // Column headers
    int headerY = LIST_TOP - 18;
    gui_draw_text(win, COL_PID, headerY, "PID", WB_GRAY_DARK);
    gui_draw_text(win, COL_NAME, headerY, "Name", WB_GRAY_DARK);
    gui_draw_text(win, COL_STATE, headerY, "State", WB_GRAY_DARK);
    gui_draw_text(win, COL_PRI, headerY, "Pri", WB_GRAY_DARK);
    gui_draw_text(win, COL_CPU, headerY, "CPU", WB_GRAY_DARK);

    // Header underline
    gui_draw_hline(win, 10, WIN_WIDTH - 10, LIST_TOP - 4, WB_GRAY_DARK);

    // List background
    gui_fill_rect(win, 10, LIST_TOP, WIN_WIDTH - 20, LIST_BOTTOM - LIST_TOP, WB_WHITE);

    // Draw task rows
    int maxVisible = (LIST_BOTTOM - LIST_TOP) / ROW_HEIGHT;
    int y = LIST_TOP + 2;

    for (int i = g_scrollOffset; i < g_taskCount && i < g_scrollOffset + maxVisible; i++) {
        TaskInfo &task = g_tasks[i];

        // Selection highlight
        if (i == g_selectedTask) {
            gui_fill_rect(win, 11, y - 1, WIN_WIDTH - 22, ROW_HEIGHT, WB_BLUE);
        }

        uint32_t textColor = (i == g_selectedTask) ? WB_WHITE : WB_BLACK;

        // PID
        snprintf(buf, sizeof(buf), "%d", task.id);
        gui_draw_text(win, COL_PID, y, buf, textColor);

        // Name
        char nameBuf[24];
        strncpy(nameBuf, task.name, 20);
        nameBuf[20] = '\0';
        gui_draw_text(win, COL_NAME, y, nameBuf, textColor);

        // State with color
        const char *stateStr = "???";
        uint32_t stateColor = textColor;
        switch (task.state) {
            case TASK_STATE_READY:
                stateStr = "Ready";
                stateColor = (i == g_selectedTask) ? WB_WHITE : WB_BLACK;
                break;
            case TASK_STATE_RUNNING:
                stateStr = "Running";
                stateColor = (i == g_selectedTask) ? WB_WHITE : WB_GREEN;
                break;
            case TASK_STATE_BLOCKED:
                stateStr = "Blocked";
                stateColor = (i == g_selectedTask) ? WB_WHITE : WB_GRAY_MED;
                break;
            case TASK_STATE_EXITED:
                stateStr = "Exited";
                stateColor = (i == g_selectedTask) ? WB_WHITE : WB_RED;
                break;
        }
        gui_draw_text(win, COL_STATE, y, stateStr, stateColor);

        // Priority
        snprintf(buf, sizeof(buf), "%d", task.priority);
        gui_draw_text(win, COL_PRI, y, buf, textColor);

        // CPU ticks (simplified display)
        if (task.cpu_ticks > 1000000) {
            snprintf(buf, sizeof(buf), "%lluM", task.cpu_ticks / 1000000);
        } else if (task.cpu_ticks > 1000) {
            snprintf(buf, sizeof(buf), "%lluK", task.cpu_ticks / 1000);
        } else {
            snprintf(buf, sizeof(buf), "%llu", task.cpu_ticks);
        }
        gui_draw_text(win, COL_CPU, y, buf, textColor);

        y += ROW_HEIGHT;
    }

    // List border
    gui_draw_hline(win, 10, WIN_WIDTH - 10, LIST_TOP, WB_GRAY_DARK);
    gui_draw_hline(win, 10, WIN_WIDTH - 10, LIST_BOTTOM, WB_GRAY_DARK);
    gui_draw_vline(win, 10, LIST_TOP, LIST_BOTTOM, WB_GRAY_DARK);
    gui_draw_vline(win, WIN_WIDTH - 10, LIST_TOP, LIST_BOTTOM, WB_GRAY_DARK);

    // Status bar
    gui_fill_rect(win, 0, WIN_HEIGHT - 45, WIN_WIDTH, 45, WB_GRAY_LIGHT);
    gui_draw_hline(win, 0, WIN_WIDTH, WIN_HEIGHT - 45, WB_GRAY_DARK);

    // Memory summary
    uint64_t usedMB = g_memInfo.used_bytes / (1024 * 1024);
    uint64_t totalMB = g_memInfo.total_bytes / (1024 * 1024);
    snprintf(buf, sizeof(buf), "Memory: %llu / %llu MB", usedMB, totalMB);
    gui_draw_text(win, 15, WIN_HEIGHT - 40, buf, WB_BLACK);

    // Buttons
    bool hasSelection = (g_selectedTask >= 0 && g_selectedTask < g_taskCount);

    drawButton(win, 15, BUTTON_Y, 90, "End Task", hasSelection);
    drawButton(win, 115, BUTTON_Y, 90, "Priority...", hasSelection);
    drawButton(win, WIN_WIDTH - 105, BUTTON_Y, 90, "Refresh", true);

    gui_present(win);
}

static int findTaskAt(int y) {
    if (y < LIST_TOP || y >= LIST_BOTTOM) {
        return -1;
    }

    int row = (y - LIST_TOP) / ROW_HEIGHT;
    int taskIdx = g_scrollOffset + row;

    if (taskIdx >= 0 && taskIdx < g_taskCount) {
        return taskIdx;
    }
    return -1;
}

static bool isButtonClicked(int mx, int my, int bx, int by, int bw) {
    return mx >= bx && mx < bx + bw && my >= by && my < by + BUTTON_HEIGHT;
}

static void handleClick(gui_window_t *win, int x, int y, int button) {
    (void)win;
    if (button != 0)
        return;

    // Check task list click
    int taskIdx = findTaskAt(y);
    if (taskIdx >= 0) {
        g_selectedTask = taskIdx;
        return;
    }

    // Check button clicks
    if (y >= BUTTON_Y && y < BUTTON_Y + BUTTON_HEIGHT) {
        // Refresh button
        if (isButtonClicked(x, y, WIN_WIDTH - 105, BUTTON_Y, 90)) {
            refreshTasks();
            return;
        }

        // End Task button
        if (isButtonClicked(x, y, 15, BUTTON_Y, 90)) {
            if (g_selectedTask >= 0 && g_selectedTask < g_taskCount) {
                // TODO: Implement kill syscall
                // For now, just log
            }
            return;
        }
    }
}

extern "C" int main() {
    // Initialize GUI
    if (gui_init() != 0) {
        return 1;
    }

    // Create window
    gui_window_t *win = gui_create_window("Task Manager", WIN_WIDTH, WIN_HEIGHT);
    if (!win) {
        gui_shutdown();
        return 1;
    }

    // Initial refresh
    refreshTasks();
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
                        handleClick(win, event.mouse.x, event.mouse.y, event.mouse.button);
                        drawWindow(win);
                    }
                    break;

                case GUI_EVENT_KEY:
                    if (event.key.pressed) {
                        int maxVisible = (LIST_BOTTOM - LIST_TOP) / ROW_HEIGHT;
                        // Arrow keys for selection
                        if (event.key.keycode == 0x52 && g_selectedTask > 0) { // Up
                            g_selectedTask--;
                            // Scroll up if needed
                            if (g_selectedTask < g_scrollOffset) {
                                g_scrollOffset = g_selectedTask;
                            }
                            drawWindow(win);
                        } else if (event.key.keycode == 0x51 &&
                                   g_selectedTask < g_taskCount - 1) { // Down
                            g_selectedTask++;
                            // Scroll down if needed
                            if (g_selectedTask >= g_scrollOffset + maxVisible) {
                                g_scrollOffset = g_selectedTask - maxVisible + 1;
                            }
                            drawWindow(win);
                        } else if (event.key.keycode == 0x3E) { // F5 = Refresh
                            refreshTasks();
                            drawWindow(win);
                        }
                    }
                    break;

                default:
                    break;
            }
        }

        // Auto-refresh every 3 seconds
        uint64_t now = sys::uptime();
        if (now - lastRefresh >= 3000) {
            refreshTasks();
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
