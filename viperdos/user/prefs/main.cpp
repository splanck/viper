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
 * Refactored using OOP principles with separate panel classes for each
 * preference category.
 */
//===----------------------------------------------------------------------===//

#include "../syscall.hpp"
#include "../version.h"
#include <gui.h>
#include <stdio.h>
#include <string.h>
#include <viperdos/mem_info.hpp>

//===----------------------------------------------------------------------===//
// Color and Layout Constants
//===----------------------------------------------------------------------===//

namespace prefs {

namespace colors {
constexpr uint32_t BLUE = 0xFF0055AA;
constexpr uint32_t WHITE = 0xFFFFFFFF;
constexpr uint32_t BLACK = 0xFF000000;
constexpr uint32_t GRAY_LIGHT = 0xFFAAAAAA;
constexpr uint32_t GRAY_MED = 0xFF888888;
constexpr uint32_t GRAY_DARK = 0xFF555555;
} // namespace colors

namespace layout {
constexpr int WIN_WIDTH = 500;
constexpr int WIN_HEIGHT = 360;
constexpr int SIDEBAR_WIDTH = 110;
constexpr int CONTENT_X = SIDEBAR_WIDTH + 10;
constexpr int BUTTON_HEIGHT = 24;
constexpr int CATEGORY_HEIGHT = 28;
} // namespace layout

//===----------------------------------------------------------------------===//
// Button3D - Reusable 3D button widget
//===----------------------------------------------------------------------===//

class Button3D {
  public:
    static void draw(gui_window_t *win, int x, int y, int w, int h,
                     const char *label, bool pressed) {
        uint32_t bg = pressed ? colors::GRAY_MED : colors::GRAY_LIGHT;
        gui_fill_rect(win, x, y, w, h, bg);

        if (pressed) {
            gui_draw_hline(win, x, x + w - 1, y, colors::GRAY_DARK);
            gui_draw_vline(win, x, y, y + h - 1, colors::GRAY_DARK);
            gui_draw_hline(win, x, x + w - 1, y + h - 1, colors::WHITE);
            gui_draw_vline(win, x + w - 1, y, y + h - 1, colors::WHITE);
        } else {
            gui_draw_hline(win, x, x + w - 1, y, colors::WHITE);
            gui_draw_vline(win, x, y, y + h - 1, colors::WHITE);
            gui_draw_hline(win, x, x + w - 1, y + h - 1, colors::GRAY_DARK);
            gui_draw_vline(win, x + w - 1, y, y + h - 1, colors::GRAY_DARK);
        }

        int textX = x + (w - static_cast<int>(strlen(label)) * 8) / 2;
        int textY = y + (h - 10) / 2;
        gui_draw_text(win, textX, textY, label, colors::BLACK);
    }
};

//===----------------------------------------------------------------------===//
// PrefsPanel - Abstract base class for preference panels
//===----------------------------------------------------------------------===//

class PrefsPanel {
  public:
    virtual ~PrefsPanel() = default;
    virtual void draw(gui_window_t *win) = 0;
    virtual const char *name() const = 0;
    virtual const char *icon() const = 0;
};

//===----------------------------------------------------------------------===//
// ScreenPrefsPanel
//===----------------------------------------------------------------------===//

class ScreenPrefsPanel : public PrefsPanel {
  public:
    void draw(gui_window_t *win) override {
        int y = 25;

        gui_draw_text(win, layout::CONTENT_X, y, "Screen Preferences", colors::BLACK);
        y += 25;

        gui_draw_hline(win, layout::CONTENT_X, layout::WIN_WIDTH - 20, y, colors::GRAY_DARK);
        y += 15;

        gui_draw_text(win, layout::CONTENT_X, y, "Resolution:", colors::BLACK);
        gui_draw_text(win, layout::CONTENT_X + 100, y, "1024 x 768", colors::GRAY_DARK);
        y += 25;

        gui_draw_text(win, layout::CONTENT_X, y, "Color Depth:", colors::BLACK);
        gui_draw_text(win, layout::CONTENT_X + 100, y, "32-bit (True Color)", colors::GRAY_DARK);
        y += 25;

        gui_draw_text(win, layout::CONTENT_X, y, "Backdrop:", colors::BLACK);
        gui_fill_rect(win, layout::CONTENT_X + 100, y - 2, 80, 16, colors::BLUE);
        gui_draw_text(win, layout::CONTENT_X + 190, y, "Workbench Blue", colors::GRAY_DARK);
        y += 35;

        gui_fill_rect(win, layout::CONTENT_X, y,
                      layout::WIN_WIDTH - layout::CONTENT_X - 20, 50, colors::BLUE);
        gui_draw_text(win, layout::CONTENT_X + 10, y + 10,
                      "Screen preferences are read-only", colors::WHITE);
        gui_draw_text(win, layout::CONTENT_X + 10, y + 28,
                      "in this version of ViperDOS.", colors::WHITE);
    }

    const char *name() const override { return "Screen"; }
    const char *icon() const override { return "[S]"; }
};

//===----------------------------------------------------------------------===//
// InputPrefsPanel
//===----------------------------------------------------------------------===//

class InputPrefsPanel : public PrefsPanel {
  public:
    void draw(gui_window_t *win) override {
        int y = 25;

        gui_draw_text(win, layout::CONTENT_X, y, "Input Preferences", colors::BLACK);
        y += 25;

        gui_draw_hline(win, layout::CONTENT_X, layout::WIN_WIDTH - 20, y, colors::GRAY_DARK);
        y += 15;

        gui_draw_text(win, layout::CONTENT_X, y, "Pointer", colors::BLUE);
        y += 20;

        gui_draw_text(win, layout::CONTENT_X + 10, y, "Speed:", colors::BLACK);
        gui_draw_text(win, layout::CONTENT_X + 100, y, "Medium", colors::GRAY_DARK);
        y += 20;

        gui_draw_text(win, layout::CONTENT_X + 10, y, "Double-click:", colors::BLACK);
        gui_draw_text(win, layout::CONTENT_X + 100, y, "400 ms", colors::GRAY_DARK);
        y += 30;

        gui_draw_text(win, layout::CONTENT_X, y, "Keyboard", colors::BLUE);
        y += 20;

        gui_draw_text(win, layout::CONTENT_X + 10, y, "Repeat delay:", colors::BLACK);
        gui_draw_text(win, layout::CONTENT_X + 120, y, "500 ms", colors::GRAY_DARK);
        y += 20;

        gui_draw_text(win, layout::CONTENT_X + 10, y, "Repeat rate:", colors::BLACK);
        gui_draw_text(win, layout::CONTENT_X + 120, y, "30 Hz", colors::GRAY_DARK);
        y += 20;

        gui_draw_text(win, layout::CONTENT_X + 10, y, "Layout:", colors::BLACK);
        gui_draw_text(win, layout::CONTENT_X + 120, y, "US English", colors::GRAY_DARK);
    }

    const char *name() const override { return "Input"; }
    const char *icon() const override { return "[I]"; }
};

//===----------------------------------------------------------------------===//
// TimePrefsPanel
//===----------------------------------------------------------------------===//

class TimePrefsPanel : public PrefsPanel {
  public:
    void draw(gui_window_t *win) override {
        int y = 25;

        gui_draw_text(win, layout::CONTENT_X, y, "Time Preferences", colors::BLACK);
        y += 25;

        gui_draw_hline(win, layout::CONTENT_X, layout::WIN_WIDTH - 20, y, colors::GRAY_DARK);
        y += 15;

        uint64_t uptime = sys::uptime();
        uint64_t seconds = uptime / 1000;
        uint64_t minutes = (seconds / 60) % 60;
        uint64_t hours = (seconds / 3600) % 24;

        char timeBuf[32];
        snprintf(timeBuf, sizeof(timeBuf), "%02llu:%02llu:%02llu", hours, minutes, seconds % 60);

        gui_draw_text(win, layout::CONTENT_X, y, "System Time:", colors::BLACK);
        gui_draw_text(win, layout::CONTENT_X + 110, y, timeBuf, colors::GRAY_DARK);
        y += 25;

        uint64_t upHours = seconds / 3600;
        uint64_t upMins = (seconds / 60) % 60;
        snprintf(timeBuf, sizeof(timeBuf), "%llu hours, %llu minutes", upHours, upMins);

        gui_draw_text(win, layout::CONTENT_X, y, "Uptime:", colors::BLACK);
        gui_draw_text(win, layout::CONTENT_X + 110, y, timeBuf, colors::GRAY_DARK);
        y += 25;

        gui_draw_text(win, layout::CONTENT_X, y, "Time Zone:", colors::BLACK);
        gui_draw_text(win, layout::CONTENT_X + 110, y, "UTC", colors::GRAY_DARK);
        y += 25;

        gui_draw_text(win, layout::CONTENT_X, y, "Clock Format:", colors::BLACK);
        gui_draw_text(win, layout::CONTENT_X + 110, y, "24-hour", colors::GRAY_DARK);
    }

    const char *name() const override { return "Time"; }
    const char *icon() const override { return "[T]"; }
};

//===----------------------------------------------------------------------===//
// AboutPrefsPanel
//===----------------------------------------------------------------------===//

class AboutPrefsPanel : public PrefsPanel {
  public:
    void draw(gui_window_t *win) override {
        int y = 25;

        gui_draw_text(win, layout::CONTENT_X, y, "About ViperDOS", colors::BLACK);
        y += 25;

        gui_draw_hline(win, layout::CONTENT_X, layout::WIN_WIDTH - 20, y, colors::GRAY_DARK);
        y += 20;

        // Logo area
        gui_fill_rect(win, layout::CONTENT_X, y, 60, 60, colors::BLUE);
        gui_draw_text(win, layout::CONTENT_X + 8, y + 20, "VIPER", colors::WHITE);
        gui_draw_text(win, layout::CONTENT_X + 12, y + 35, "DOS", colors::WHITE);

        gui_draw_text(win, layout::CONTENT_X + 80, y + 5, "ViperDOS Workbench", colors::BLACK);
        gui_draw_text(win, layout::CONTENT_X + 80, y + 22,
                      "Version " VIPERDOS_VERSION_STRING, colors::GRAY_DARK);
        gui_draw_text(win, layout::CONTENT_X + 80, y + 39, "Hybrid Kernel OS", colors::GRAY_DARK);
        y += 75;

        MemInfo memInfo;
        sys::mem_info(&memInfo);

        char buf[64];
        snprintf(buf, sizeof(buf), "Memory: %llu MB total, %llu MB free",
                 memInfo.total_bytes / (1024 * 1024),
                 memInfo.free_bytes / (1024 * 1024));
        gui_draw_text(win, layout::CONTENT_X, y, buf, colors::BLACK);
        y += 20;

        gui_draw_text(win, layout::CONTENT_X, y, "Platform: AArch64 (ARM64)", colors::BLACK);
        y += 20;

        gui_draw_text(win, layout::CONTENT_X, y, "Display: 1024x768 32bpp", colors::BLACK);
        y += 30;

        gui_draw_text(win, layout::CONTENT_X, y, "(C) 2025 ViperDOS Team", colors::GRAY_DARK);
    }

    const char *name() const override { return "About"; }
    const char *icon() const override { return "[?]"; }
};

//===----------------------------------------------------------------------===//
// PreferencesApp - Main application class
//===----------------------------------------------------------------------===//

class PreferencesApp {
  public:
    PreferencesApp() : m_window(nullptr), m_currentPanel(0), m_hoveredPanel(-1) {
        m_panels[0] = &m_screenPanel;
        m_panels[1] = &m_inputPanel;
        m_panels[2] = &m_timePanel;
        m_panels[3] = &m_aboutPanel;
    }

    bool init() {
        if (gui_init() != 0) {
            return false;
        }

        m_window = gui_create_window("Preferences", layout::WIN_WIDTH, layout::WIN_HEIGHT);
        return m_window != nullptr;
    }

    void run() {
        draw();

        uint64_t lastRefresh = sys::uptime();
        bool running = true;

        while (running) {
            gui_event_t event;
            if (gui_poll_event(m_window, &event) == 0) {
                switch (event.type) {
                    case GUI_EVENT_CLOSE:
                        running = false;
                        break;

                    case GUI_EVENT_MOUSE:
                        if (event.mouse.event_type == 1) {
                            if (handleClick(event.mouse.x, event.mouse.y, event.mouse.button)) {
                                running = false;
                            }
                            draw();
                        } else if (event.mouse.event_type == 0) {
                            int newHover = findPanelAt(event.mouse.x, event.mouse.y);
                            if (newHover != m_hoveredPanel) {
                                m_hoveredPanel = newHover;
                                draw();
                            }
                        }
                        break;

                    default:
                        break;
                }
            }

            // Refresh time display every second if on Time tab
            if (m_currentPanel == 2) { // Time panel
                uint64_t now = sys::uptime();
                if (now - lastRefresh >= 1000) {
                    draw();
                    lastRefresh = now;
                }
            }

            __asm__ volatile("mov x8, #0x00\n\tsvc #0" ::: "x8");
        }
    }

    void shutdown() {
        if (m_window) {
            gui_destroy_window(m_window);
        }
        gui_shutdown();
    }

  private:
    static constexpr int NUM_PANELS = 4;

    gui_window_t *m_window;
    int m_currentPanel;
    int m_hoveredPanel;

    ScreenPrefsPanel m_screenPanel;
    InputPrefsPanel m_inputPanel;
    TimePrefsPanel m_timePanel;
    AboutPrefsPanel m_aboutPanel;
    PrefsPanel *m_panels[NUM_PANELS];

    void draw() {
        drawSidebar();
        drawContent();
        drawBottomBar();
        gui_present(m_window);
    }

    void drawSidebar() {
        gui_fill_rect(m_window, 0, 0, layout::SIDEBAR_WIDTH, layout::WIN_HEIGHT, colors::GRAY_MED);

        int y = 15;
        for (int i = 0; i < NUM_PANELS; i++) {
            bool selected = (i == m_currentPanel);

            if (selected) {
                gui_fill_rect(m_window, 5, y, layout::SIDEBAR_WIDTH - 10,
                              layout::CATEGORY_HEIGHT, colors::BLUE);
            } else if (i == m_hoveredPanel) {
                gui_fill_rect(m_window, 5, y, layout::SIDEBAR_WIDTH - 10,
                              layout::CATEGORY_HEIGHT, colors::GRAY_LIGHT);
            }

            uint32_t textColor = selected ? colors::WHITE : colors::BLACK;
            gui_draw_text(m_window, 12, y + 8, m_panels[i]->icon(), textColor);
            gui_draw_text(m_window, 38, y + 8, m_panels[i]->name(), textColor);

            y += layout::CATEGORY_HEIGHT + 4;
        }

        gui_draw_vline(m_window, layout::SIDEBAR_WIDTH - 1, 0, layout::WIN_HEIGHT, colors::GRAY_DARK);
    }

    void drawContent() {
        gui_fill_rect(m_window, layout::SIDEBAR_WIDTH, 0,
                      layout::WIN_WIDTH - layout::SIDEBAR_WIDTH,
                      layout::WIN_HEIGHT - 45, colors::GRAY_LIGHT);

        m_panels[m_currentPanel]->draw(m_window);
    }

    void drawBottomBar() {
        gui_fill_rect(m_window, layout::SIDEBAR_WIDTH, layout::WIN_HEIGHT - 45,
                      layout::WIN_WIDTH - layout::SIDEBAR_WIDTH, 45, colors::GRAY_LIGHT);
        gui_draw_hline(m_window, layout::SIDEBAR_WIDTH, layout::WIN_WIDTH,
                       layout::WIN_HEIGHT - 45, colors::GRAY_DARK);

        int btnY = layout::WIN_HEIGHT - 35;
        int btnW = 70;

        Button3D::draw(m_window, layout::WIN_WIDTH - 240, btnY, btnW,
                       layout::BUTTON_HEIGHT, "Use", false);
        Button3D::draw(m_window, layout::WIN_WIDTH - 160, btnY, btnW,
                       layout::BUTTON_HEIGHT, "Cancel", false);
        Button3D::draw(m_window, layout::WIN_WIDTH - 80, btnY, btnW,
                       layout::BUTTON_HEIGHT, "Save", false);
    }

    int findPanelAt(int x, int y) {
        if (x >= layout::SIDEBAR_WIDTH) return -1;

        int panelY = 15;
        for (int i = 0; i < NUM_PANELS; i++) {
            if (y >= panelY && y < panelY + layout::CATEGORY_HEIGHT) {
                return i;
            }
            panelY += layout::CATEGORY_HEIGHT + 4;
        }
        return -1;
    }

    bool handleClick(int x, int y, int button) {
        if (button != 0) return false;

        int panelIdx = findPanelAt(x, y);
        if (panelIdx >= 0) {
            m_currentPanel = panelIdx;
            return false;
        }

        int btnY = layout::WIN_HEIGHT - 35;
        if (y >= btnY && y < btnY + layout::BUTTON_HEIGHT) {
            if (x >= layout::WIN_WIDTH - 160 && x < layout::WIN_WIDTH - 90) {
                return true; // Cancel clicked
            }
        }
        return false;
    }
};

} // namespace prefs

//===----------------------------------------------------------------------===//
// Main Entry Point
//===----------------------------------------------------------------------===//

extern "C" int main() {
    prefs::PreferencesApp app;

    if (!app.init()) {
        return 1;
    }

    app.run();
    app.shutdown();
    return 0;
}
