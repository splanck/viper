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
 * Refactored using OOP principles with TaskListView and TaskManagerApp classes.
 */
//===----------------------------------------------------------------------===//

#include "../syscall.hpp"
#include <gui.h>
#include <stdio.h>
#include <string.h>
#include <viperdos/mem_info.hpp>
#include <viperdos/task_info.hpp>

//===----------------------------------------------------------------------===//
// Color and Layout Constants
//===----------------------------------------------------------------------===//

namespace taskman {

namespace colors {
constexpr uint32_t BLUE = 0xFF0055AA;
constexpr uint32_t WHITE = 0xFFFFFFFF;
constexpr uint32_t BLACK = 0xFF000000;
constexpr uint32_t GRAY_LIGHT = 0xFFAAAAAA;
constexpr uint32_t GRAY_MED = 0xFF888888;
constexpr uint32_t GRAY_DARK = 0xFF555555;
constexpr uint32_t RED = 0xFFFF4444;
constexpr uint32_t GREEN = 0xFF00AA44;
} // namespace colors

namespace layout {
constexpr int WIN_WIDTH = 480;
constexpr int WIN_HEIGHT = 380;
constexpr int HEADER_HEIGHT = 30;
constexpr int ROW_HEIGHT = 18;
constexpr int LIST_TOP = 50;
constexpr int LIST_BOTTOM = WIN_HEIGHT - 50;
constexpr int BUTTON_HEIGHT = 24;
constexpr int BUTTON_Y = WIN_HEIGHT - 35;

// Column positions
constexpr int COL_PID = 15;
constexpr int COL_NAME = 55;
constexpr int COL_STATE = 200;
constexpr int COL_PRI = 280;
constexpr int COL_CPU = 330;
} // namespace layout

//===----------------------------------------------------------------------===//
// Button3D - Reusable 3D button widget
//===----------------------------------------------------------------------===//

class Button3D {
  public:
    static void draw(gui_window_t *win, int x, int y, int w, const char *label, bool enabled) {
        uint32_t bgColor = enabled ? colors::GRAY_LIGHT : colors::GRAY_MED;
        uint32_t textColor = enabled ? colors::BLACK : colors::GRAY_DARK;

        gui_fill_rect(win, x, y, w, layout::BUTTON_HEIGHT, bgColor);

        gui_draw_hline(win, x, x + w - 1, y, colors::WHITE);
        gui_draw_vline(win, x, y, y + layout::BUTTON_HEIGHT - 1, colors::WHITE);
        gui_draw_hline(win, x, x + w - 1, y + layout::BUTTON_HEIGHT - 1, colors::GRAY_DARK);
        gui_draw_vline(win, x + w - 1, y, y + layout::BUTTON_HEIGHT - 1, colors::GRAY_DARK);

        int textX = x + (w - static_cast<int>(strlen(label)) * 8) / 2;
        int textY = y + 6;
        gui_draw_text(win, textX, textY, label, textColor);
    }

    static bool hitTest(int mx, int my, int bx, int by, int bw) {
        return mx >= bx && mx < bx + bw && my >= by && my < by + layout::BUTTON_HEIGHT;
    }
};

//===----------------------------------------------------------------------===//
// TaskDataSource - Manages task and memory data
//===----------------------------------------------------------------------===//

class TaskDataSource {
  public:
    static constexpr int MAX_TASKS = 64;

    TaskDataSource() : m_taskCount(0), m_selectedTask(-1), m_scrollOffset(0) {}

    void refresh() {
        m_taskCount = sys::task_list(m_tasks, MAX_TASKS);
        if (m_taskCount < 0) {
            m_taskCount = 0;
        }
        sys::mem_info(&m_memInfo);

        if (m_selectedTask >= m_taskCount) {
            m_selectedTask = m_taskCount - 1;
        }
    }

    int taskCount() const {
        return m_taskCount;
    }

    int selectedTask() const {
        return m_selectedTask;
    }

    int scrollOffset() const {
        return m_scrollOffset;
    }

    const MemInfo &memInfo() const {
        return m_memInfo;
    }

    const TaskInfo &task(int idx) const {
        return m_tasks[idx];
    }

    void selectTask(int idx) {
        if (idx >= 0 && idx < m_taskCount) {
            m_selectedTask = idx;
        }
    }

    void selectPrevious(int /* maxVisible */) {
        if (m_selectedTask > 0) {
            m_selectedTask--;
            if (m_selectedTask < m_scrollOffset) {
                m_scrollOffset = m_selectedTask;
            }
        }
    }

    void selectNext(int maxVisible) {
        if (m_selectedTask < m_taskCount - 1) {
            m_selectedTask++;
            if (m_selectedTask >= m_scrollOffset + maxVisible) {
                m_scrollOffset = m_selectedTask - maxVisible + 1;
            }
        }
    }

    bool hasSelection() const {
        return m_selectedTask >= 0 && m_selectedTask < m_taskCount;
    }

  private:
    TaskInfo m_tasks[MAX_TASKS];
    int m_taskCount;
    int m_selectedTask;
    int m_scrollOffset;
    MemInfo m_memInfo;
};

//===----------------------------------------------------------------------===//
// TaskListView - Renders the task list
//===----------------------------------------------------------------------===//

class TaskListView {
  public:
    int maxVisibleRows() const {
        return (layout::LIST_BOTTOM - layout::LIST_TOP) / layout::ROW_HEIGHT;
    }

    int findTaskAt(int y) const {
        if (y < layout::LIST_TOP || y >= layout::LIST_BOTTOM) {
            return -1;
        }
        return (y - layout::LIST_TOP) / layout::ROW_HEIGHT;
    }

    void drawHeader(gui_window_t *win, int taskCount) {
        gui_fill_rect(win, 0, 0, layout::WIN_WIDTH, layout::HEADER_HEIGHT, colors::BLUE);
        gui_draw_text(win, 15, 8, "Task Manager", colors::WHITE);

        char buf[32];
        snprintf(buf, sizeof(buf), "%d tasks", taskCount);
        gui_draw_text(win, layout::WIN_WIDTH - 100, 8, buf, colors::WHITE);
    }

    void drawColumnHeaders(gui_window_t *win) {
        int headerY = layout::LIST_TOP - 18;
        gui_draw_text(win, layout::COL_PID, headerY, "PID", colors::GRAY_DARK);
        gui_draw_text(win, layout::COL_NAME, headerY, "Name", colors::GRAY_DARK);
        gui_draw_text(win, layout::COL_STATE, headerY, "State", colors::GRAY_DARK);
        gui_draw_text(win, layout::COL_PRI, headerY, "Pri", colors::GRAY_DARK);
        gui_draw_text(win, layout::COL_CPU, headerY, "CPU", colors::GRAY_DARK);

        gui_draw_hline(win, 10, layout::WIN_WIDTH - 10, layout::LIST_TOP - 4, colors::GRAY_DARK);
    }

    void drawTaskList(gui_window_t *win, const TaskDataSource &data) {
        gui_fill_rect(win,
                      10,
                      layout::LIST_TOP,
                      layout::WIN_WIDTH - 20,
                      layout::LIST_BOTTOM - layout::LIST_TOP,
                      colors::WHITE);

        int maxVisible = maxVisibleRows();
        int y = layout::LIST_TOP + 2;

        for (int i = data.scrollOffset();
             i < data.taskCount() && i < data.scrollOffset() + maxVisible;
             i++) {
            bool selected = (i == data.selectedTask());
            if (selected) {
                gui_fill_rect(
                    win, 11, y - 1, layout::WIN_WIDTH - 22, layout::ROW_HEIGHT, colors::BLUE);
            }

            drawTaskRow(win, data.task(i), y, selected);
            y += layout::ROW_HEIGHT;
        }

        drawListBorder(win);
    }

    void drawStatusBar(gui_window_t *win, const TaskDataSource &data) {
        gui_fill_rect(win, 0, layout::WIN_HEIGHT - 45, layout::WIN_WIDTH, 45, colors::GRAY_LIGHT);
        gui_draw_hline(win, 0, layout::WIN_WIDTH, layout::WIN_HEIGHT - 45, colors::GRAY_DARK);

        uint64_t usedMB = data.memInfo().used_bytes / (1024 * 1024);
        uint64_t totalMB = data.memInfo().total_bytes / (1024 * 1024);

        char buf[64];
        snprintf(buf, sizeof(buf), "Memory: %llu / %llu MB", usedMB, totalMB);
        gui_draw_text(win, 15, layout::WIN_HEIGHT - 40, buf, colors::BLACK);
    }

    void drawButtons(gui_window_t *win, bool hasSelection) {
        Button3D::draw(win, 15, layout::BUTTON_Y, 90, "End Task", hasSelection);
        Button3D::draw(win, 115, layout::BUTTON_Y, 90, "Priority...", hasSelection);
        Button3D::draw(win, layout::WIN_WIDTH - 105, layout::BUTTON_Y, 90, "Refresh", true);
    }

  private:
    void drawTaskRow(gui_window_t *win, const TaskInfo &task, int y, bool selected) {
        uint32_t textColor = selected ? colors::WHITE : colors::BLACK;
        char buf[32];

        // PID
        snprintf(buf, sizeof(buf), "%d", task.id);
        gui_draw_text(win, layout::COL_PID, y, buf, textColor);

        // Name
        char nameBuf[24];
        strncpy(nameBuf, task.name, 20);
        nameBuf[20] = '\0';
        gui_draw_text(win, layout::COL_NAME, y, nameBuf, textColor);

        // State
        const char *stateStr = "???";
        uint32_t stateColor = textColor;

        switch (task.state) {
            case TASK_STATE_READY:
                stateStr = "Ready";
                stateColor = selected ? colors::WHITE : colors::BLACK;
                break;
            case TASK_STATE_RUNNING:
                stateStr = "Running";
                stateColor = selected ? colors::WHITE : colors::GREEN;
                break;
            case TASK_STATE_BLOCKED:
                stateStr = "Blocked";
                stateColor = selected ? colors::WHITE : colors::GRAY_MED;
                break;
            case TASK_STATE_EXITED:
                stateStr = "Exited";
                stateColor = selected ? colors::WHITE : colors::RED;
                break;
        }
        gui_draw_text(win, layout::COL_STATE, y, stateStr, stateColor);

        // Priority
        snprintf(buf, sizeof(buf), "%d", task.priority);
        gui_draw_text(win, layout::COL_PRI, y, buf, textColor);

        // CPU ticks
        if (task.cpu_ticks > 1000000) {
            snprintf(buf, sizeof(buf), "%lluM", task.cpu_ticks / 1000000);
        } else if (task.cpu_ticks > 1000) {
            snprintf(buf, sizeof(buf), "%lluK", task.cpu_ticks / 1000);
        } else {
            snprintf(buf, sizeof(buf), "%llu", task.cpu_ticks);
        }
        gui_draw_text(win, layout::COL_CPU, y, buf, textColor);
    }

    void drawListBorder(gui_window_t *win) {
        gui_draw_hline(win, 10, layout::WIN_WIDTH - 10, layout::LIST_TOP, colors::GRAY_DARK);
        gui_draw_hline(win, 10, layout::WIN_WIDTH - 10, layout::LIST_BOTTOM, colors::GRAY_DARK);
        gui_draw_vline(win, 10, layout::LIST_TOP, layout::LIST_BOTTOM, colors::GRAY_DARK);
        gui_draw_vline(
            win, layout::WIN_WIDTH - 10, layout::LIST_TOP, layout::LIST_BOTTOM, colors::GRAY_DARK);
    }
};

//===----------------------------------------------------------------------===//
// TaskManagerApp - Main application class
//===----------------------------------------------------------------------===//

class TaskManagerApp {
  public:
    TaskManagerApp() : m_window(nullptr), m_running(false) {}

    bool init() {
        if (gui_init() != 0) {
            return false;
        }

        m_window = gui_create_window("Task Manager", layout::WIN_WIDTH, layout::WIN_HEIGHT);
        if (!m_window) {
            gui_shutdown();
            return false;
        }

        m_data.refresh();
        return true;
    }

    void run() {
        draw();

        uint64_t lastRefresh = sys::uptime();
        m_running = true;

        while (m_running) {
            gui_event_t event;
            if (gui_poll_event(m_window, &event) == 0) {
                if (processEvent(event)) {
                    draw();
                }
            }

            // Auto-refresh every 3 seconds
            uint64_t now = sys::uptime();
            if (now - lastRefresh >= 3000) {
                m_data.refresh();
                draw();
                lastRefresh = now;
            }

            __asm__ volatile("mov x8, #0x00\n\tsvc #0" ::: "x8");
        }
    }

    void shutdown() {
        gui_destroy_window(m_window);
        gui_shutdown();
    }

  private:
    gui_window_t *m_window;
    TaskDataSource m_data;
    TaskListView m_view;
    bool m_running;

    void draw() {
        gui_fill_rect(m_window, 0, 0, layout::WIN_WIDTH, layout::WIN_HEIGHT, colors::GRAY_LIGHT);

        m_view.drawHeader(m_window, m_data.taskCount());
        m_view.drawColumnHeaders(m_window);
        m_view.drawTaskList(m_window, m_data);
        m_view.drawStatusBar(m_window, m_data);
        m_view.drawButtons(m_window, m_data.hasSelection());

        gui_present(m_window);
    }

    bool processEvent(const gui_event_t &event) {
        switch (event.type) {
            case GUI_EVENT_CLOSE:
                m_running = false;
                return false;

            case GUI_EVENT_MOUSE:
                if (event.mouse.event_type == 1) {
                    return handleClick(event.mouse.x, event.mouse.y, event.mouse.button);
                }
                return false;

            case GUI_EVENT_KEY:
                if (event.key.pressed) {
                    return handleKey(event.key.keycode);
                }
                return false;

            default:
                return false;
        }
    }

    bool handleClick(int x, int y, int button) {
        if (button != 0)
            return false;

        // Check task list click
        int rowIdx = m_view.findTaskAt(y);
        if (rowIdx >= 0) {
            m_data.selectTask(m_data.scrollOffset() + rowIdx);
            return true;
        }

        // Check button clicks
        if (y >= layout::BUTTON_Y && y < layout::BUTTON_Y + layout::BUTTON_HEIGHT) {
            if (Button3D::hitTest(x, y, layout::WIN_WIDTH - 105, layout::BUTTON_Y, 90)) {
                m_data.refresh();
                return true;
            }
        }

        return false;
    }

    bool handleKey(uint16_t keycode) {
        int maxVisible = m_view.maxVisibleRows();

        switch (keycode) {
            case 0x52: // Up
                m_data.selectPrevious(maxVisible);
                return true;

            case 0x51: // Down
                m_data.selectNext(maxVisible);
                return true;

            case 0x3E: // F5 = Refresh
                m_data.refresh();
                return true;

            default:
                return false;
        }
    }
};

} // namespace taskman

//===----------------------------------------------------------------------===//
// Main Entry Point
//===----------------------------------------------------------------------===//

extern "C" int main() {
    taskman::TaskManagerApp app;

    if (!app.init()) {
        return 1;
    }

    app.run();
    app.shutdown();
    return 0;
}
