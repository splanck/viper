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
 * Refactored using OOP principles with SystemInfoApp class.
 */
//===----------------------------------------------------------------------===//

#include "../syscall.hpp"
#include <gui.h>
#include <stdio.h>
#include <string.h>
#include <viperdos/mem_info.hpp>
#include <viperdos/task_info.hpp>

//===----------------------------------------------------------------------===//
// Constants
//===----------------------------------------------------------------------===//

namespace sysinfo {

namespace colors {
constexpr uint32_t BLUE = 0xFF0055AA;
constexpr uint32_t WHITE = 0xFFFFFFFF;
constexpr uint32_t BLACK = 0xFF000000;
constexpr uint32_t GRAY_LIGHT = 0xFFAAAAAA;
constexpr uint32_t GRAY_DARK = 0xFF555555;
constexpr uint32_t ORANGE = 0xFFFF8800;
} // namespace colors

namespace layout {
constexpr int WIN_WIDTH = 400;
constexpr int WIN_HEIGHT = 340;
constexpr int MAX_VISIBLE_TASKS = 8;
} // namespace layout

//===----------------------------------------------------------------------===//
// Formatter - Utility class for formatting values
//===----------------------------------------------------------------------===//

class Formatter {
  public:
    static void uptime(char *buf, size_t len, uint64_t ms) {
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

    static void bytes(char *buf, size_t len, uint64_t bytes) {
        if (bytes >= 1024ULL * 1024 * 1024) {
            snprintf(buf, len, "%llu GB", bytes / (1024ULL * 1024 * 1024));
        } else if (bytes >= 1024 * 1024) {
            snprintf(buf, len, "%llu MB", bytes / (1024 * 1024));
        } else if (bytes >= 1024) {
            snprintf(buf, len, "%llu KB", bytes / 1024);
        } else {
            snprintf(buf, len, "%llu bytes", bytes);
        }
    }
};

//===----------------------------------------------------------------------===//
// SystemDataSource - Manages system data collection
//===----------------------------------------------------------------------===//

class SystemDataSource {
  public:
    static constexpr int MAX_TASKS = 32;

    SystemDataSource() : m_taskCount(0), m_uptimeMs(0) {}

    void refresh() {
        sys::mem_info(&m_mem);

        m_taskCount = sys::task_list(m_tasks, MAX_TASKS);
        if (m_taskCount < 0) {
            m_taskCount = 0;
        }

        m_uptimeMs = sys::uptime();
    }

    const MemInfo &memInfo() const {
        return m_mem;
    }

    int taskCount() const {
        return m_taskCount;
    }

    uint64_t uptimeMs() const {
        return m_uptimeMs;
    }

    const TaskInfo &task(int idx) const {
        return m_tasks[idx];
    }

  private:
    MemInfo m_mem;
    TaskInfo m_tasks[MAX_TASKS];
    int m_taskCount;
    uint64_t m_uptimeMs;
};

//===----------------------------------------------------------------------===//
// SystemInfoView - Renders the system information
//===----------------------------------------------------------------------===//

class SystemInfoView {
  public:
    void draw(gui_window_t *win, const SystemDataSource &data) {
        gui_fill_rect(win, 0, 0, layout::WIN_WIDTH, layout::WIN_HEIGHT, colors::GRAY_LIGHT);

        int y = 15;
        y = drawTitle(win, y);
        y = drawSystemInfo(win, y);
        y = drawMemorySection(win, y, data);
        y = drawUptime(win, y, data);
        y = drawTasksSection(win, y, data);

        gui_present(win);
    }

  private:
    int drawTitle(gui_window_t *win, int y) {
        gui_draw_text(win, 130, y, "ViperDOS System Info", colors::BLACK);
        y += 12;
        gui_draw_hline(win, 20, layout::WIN_WIDTH - 20, y, colors::GRAY_DARK);
        return y + 15;
    }

    int drawSystemInfo(gui_window_t *win, int y) {
        drawLabelValue(win, 20, y, "System:", "ViperDOS v0.3.1");
        y += 18;
        drawLabelValue(win, 20, y, "Kernel:", "Viper Hybrid Kernel");
        y += 18;
        drawLabelValue(win, 20, y, "Platform:", "AArch64 (ARM64)");
        y += 18;
        drawLabelValue(win, 20, y, "CPU:", "Cortex-A57 (QEMU)");
        return y + 25;
    }

    int drawMemorySection(gui_window_t *win, int y, const SystemDataSource &data) {
        // Memory section background
        gui_fill_rect(win, 15, y - 3, layout::WIN_WIDTH - 30, 60, colors::BLUE);
        gui_draw_text(win, 20, y, "Memory", colors::WHITE);
        y += 18;

        // Format memory values
        char totalBuf[64], freeBuf[64], buf[128];
        Formatter::bytes(totalBuf, sizeof(totalBuf), data.memInfo().total_bytes);
        Formatter::bytes(freeBuf, sizeof(freeBuf), data.memInfo().free_bytes);

        snprintf(buf, sizeof(buf), "Total: %s    Free: %s", totalBuf, freeBuf);
        gui_draw_text(win, 25, y, buf, colors::WHITE);
        y += 18;

        // Memory bar
        drawMemoryBar(win, 25, y, layout::WIN_WIDTH - 60, 12, data.memInfo());
        return y + 25;
    }

    void drawMemoryBar(gui_window_t *win, int x, int y, int w, int h, const MemInfo &mem) {
        gui_fill_rect(win, x, y, w, h, colors::GRAY_DARK);

        int usedW = 0;
        if (mem.total_bytes > 0) {
            usedW = static_cast<int>((mem.used_bytes * w) / mem.total_bytes);
        }
        gui_fill_rect(win, x, y, usedW, h, colors::ORANGE);
    }

    int drawUptime(gui_window_t *win, int y, const SystemDataSource &data) {
        char buf[64];
        Formatter::uptime(buf, sizeof(buf), data.uptimeMs());

        gui_draw_text(win, 20, y, "Uptime:", colors::BLACK);
        gui_draw_text(win, 120, y, buf, colors::GRAY_DARK);
        return y + 25;
    }

    int drawTasksSection(gui_window_t *win, int y, const SystemDataSource &data) {
        gui_draw_hline(win, 20, layout::WIN_WIDTH - 20, y, colors::GRAY_DARK);
        y += 8;

        char buf[64];
        snprintf(buf, sizeof(buf), "Running Tasks (%d)", data.taskCount());
        gui_draw_text(win, 20, y, buf, colors::BLACK);
        y += 18;

        // Task header
        gui_draw_text(win, 25, y, "PID", colors::GRAY_DARK);
        gui_draw_text(win, 60, y, "Name", colors::GRAY_DARK);
        gui_draw_text(win, 200, y, "State", colors::GRAY_DARK);
        gui_draw_text(win, 280, y, "Priority", colors::GRAY_DARK);
        y += 14;

        gui_draw_hline(win, 25, layout::WIN_WIDTH - 25, y, colors::GRAY_DARK);
        y += 4;

        // Task list
        int maxTasks = (data.taskCount() < layout::MAX_VISIBLE_TASKS) ? data.taskCount()
                                                                      : layout::MAX_VISIBLE_TASKS;

        for (int i = 0; i < maxTasks; i++) {
            y = drawTaskRow(win, y, data.task(i));
        }

        if (data.taskCount() > layout::MAX_VISIBLE_TASKS) {
            snprintf(
                buf, sizeof(buf), "... and %d more", data.taskCount() - layout::MAX_VISIBLE_TASKS);
            gui_draw_text(win, 60, y, buf, colors::GRAY_DARK);
        }

        return y;
    }

    int drawTaskRow(gui_window_t *win, int y, const TaskInfo &task) {
        char buf[32];

        // PID
        snprintf(buf, sizeof(buf), "%d", task.id);
        gui_draw_text(win, 25, y, buf, colors::BLACK);

        // Name
        char nameBuf[20];
        strncpy(nameBuf, task.name, 18);
        nameBuf[18] = '\0';
        gui_draw_text(win, 60, y, nameBuf, colors::BLACK);

        // State
        const char *stateStr = stateToString(task.state);
        gui_draw_text(win, 200, y, stateStr, colors::BLACK);

        // Priority
        snprintf(buf, sizeof(buf), "%d", task.priority);
        gui_draw_text(win, 290, y, buf, colors::BLACK);

        return y + 14;
    }

    const char *stateToString(uint32_t state) {
        switch (state) {
            case TASK_STATE_READY:
                return "Ready";
            case TASK_STATE_RUNNING:
                return "Running";
            case TASK_STATE_BLOCKED:
                return "Blocked";
            case TASK_STATE_EXITED:
                return "Exited";
            default:
                return "???";
        }
    }

    void drawLabelValue(gui_window_t *win, int x, int y, const char *label, const char *value) {
        gui_draw_text(win, x, y, label, colors::BLACK);
        gui_draw_text(win, x + 100, y, value, colors::GRAY_DARK);
    }
};

//===----------------------------------------------------------------------===//
// SystemInfoApp - Main application class
//===----------------------------------------------------------------------===//

class SystemInfoApp {
  public:
    SystemInfoApp() : m_window(nullptr) {}

    bool init() {
        if (gui_init() != 0) {
            return false;
        }

        m_window = gui_create_window("System Information", layout::WIN_WIDTH, layout::WIN_HEIGHT);
        if (!m_window) {
            gui_shutdown();
            return false;
        }

        m_data.refresh();
        return true;
    }

    void run() {
        m_view.draw(m_window, m_data);

        uint64_t lastRefresh = sys::uptime();

        while (true) {
            gui_event_t event;
            if (gui_poll_event(m_window, &event) == 0) {
                if (event.type == GUI_EVENT_CLOSE) {
                    break;
                }
            }

            // Refresh every 2 seconds
            uint64_t now = sys::uptime();
            if (now - lastRefresh >= 2000) {
                m_data.refresh();
                m_view.draw(m_window, m_data);
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
    SystemDataSource m_data;
    SystemInfoView m_view;
};

} // namespace sysinfo

//===----------------------------------------------------------------------===//
// Main Entry Point
//===----------------------------------------------------------------------===//

extern "C" int main() {
    sysinfo::SystemInfoApp app;

    if (!app.init()) {
        return 1;
    }

    app.run();
    app.shutdown();
    return 0;
}
