//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: user/workbench/src/desktop.cpp
// Purpose: Desktop class implementation for ViperDOS Workbench GUI.
// Key invariants: Single desktop instance manages all windows.
// Ownership/Lifetime: Created by main(), lives for application lifetime.
// Links: user/workbench/include/desktop.hpp, user/workbench/src/filebrowser.cpp
//
//===----------------------------------------------------------------------===//

/**
 * @file desktop.cpp
 * @brief Desktop class implementation for the ViperDOS Workbench GUI.
 *
 * @details
 * The Desktop class manages the main graphical desktop environment, including:
 * - Desktop icon grid (drives, disk images, trash)
 * - Window management for file browser windows
 * - Mouse input handling and icon selection
 * - Drag and drop operations
 *
 * The desktop uses the GUI library (gui.h) for low-level window and event
 * management. Icons are rendered using predefined pixel art from icons.hpp.
 */

#include "../include/desktop.hpp"
#include "../../../syscall.hpp" // For assign_list()
#include "../include/colors.hpp"
#include "../include/filebrowser.hpp"
#include "../include/icons.hpp"
#include "../include/utils.hpp"
#include <gui.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

using sys::assign_list;
using sys::AssignInfo;

namespace workbench {

Desktop::Desktop() {}

Desktop::~Desktop() {
    // Close any open file browsers
    for (int i = 0; i < m_browserCount; i++) {
        if (m_browsers[i]) {
            closeFileBrowser(m_browsers[i]);
        }
    }

    // Close any open dialogs
    if (m_aboutDialog) {
        gui_destroy_window(m_aboutDialog);
    }
    if (m_prefsDialog) {
        gui_destroy_window(m_prefsDialog);
    }

    if (m_window) {
        gui_destroy_window(m_window);
    }
    gui_shutdown();
}

bool Desktop::init() {
    // Initialize GUI
    if (gui_init() != 0) {
        return false;
    }

    // Get display dimensions
    gui_display_info_t info;
    if (gui_get_display_info(&info) == 0) {
        m_width = info.width;
        m_height = info.height;
    }

    // Create full-screen desktop surface
    m_window = gui_create_window_ex(
        "Workbench", m_width, m_height, GUI_FLAG_SYSTEM | GUI_FLAG_NO_DECORATIONS);
    if (!m_window) {
        gui_shutdown();
        return false;
    }

    // Position at 0,0 (behind all other windows)
    gui_set_position(m_window, 0, 0);

    // Discover mounted volumes dynamically
    discoverVolumes();

    // Add system icons after volumes
    m_icons[m_iconCount++] = {
        0, 0, "Shell", "/sys/consoled.sys", icons::shell_24, IconAction::LaunchProgram, false};
    m_icons[m_iconCount++] = {
        0, 0, "Prefs", "/c/prefs.prg", icons::settings_24, IconAction::LaunchProgram, false};
    m_icons[m_iconCount++] = {
        0, 0, "Help", nullptr, icons::about_24, IconAction::ShowDialog, false};

    // Layout and draw
    layoutIcons();
    redraw();

    return true;
}

void Desktop::run() {
    while (true) {
        // Handle desktop events
        gui_event_t event;
        if (gui_poll_event(m_window, &event) == 0) {
            handleDesktopEvent(event);
        }

        // Handle file browser events
        handleBrowserEvents();

        // Handle dialog events
        handleDialogEvents();

        // Yield to other processes
        __asm__ volatile("mov x8, #0x0E\n\t"
                         "svc #0" ::
                             : "x8");
    }
}

void Desktop::openFileBrowser(const char *path) {
    // Check if we have room for another browser
    if (m_browserCount >= MAX_BROWSERS) {
        debug_serial("[workbench] Max browsers reached\n");
        return;
    }

    // Create the file browser
    FileBrowser *browser = new FileBrowser(this, path);
    if (!browser->init()) {
        debug_serial("[workbench] Failed to create file browser\n");
        delete browser;
        return;
    }

    // Add to our list
    m_browsers[m_browserCount++] = browser;
    debug_serial("[workbench] Opened file browser\n");
}

void Desktop::closeFileBrowser(FileBrowser *browser) {
    // Find and remove from our list
    for (int i = 0; i < m_browserCount; i++) {
        if (m_browsers[i] == browser) {
            delete browser;
            // Shift remaining browsers down
            for (int j = i; j < m_browserCount - 1; j++) {
                m_browsers[j] = m_browsers[j + 1];
            }
            m_browsers[--m_browserCount] = nullptr;
            debug_serial("[workbench] Closed file browser\n");
            return;
        }
    }
}

void Desktop::spawnProgram(const char *path) {
    debug_serial("[workbench] Spawning: ");
    debug_serial(path);
    debug_serial("\n");

    // Use inline assembly for spawn syscall
    // SYS_TASK_SPAWN = 0x03
    uint64_t pid = 0, tid = 0;
    int64_t result;

    __asm__ volatile("mov x0, %[path]\n\t"
                     "mov x1, xzr\n\t"   // name = NULL
                     "mov x2, xzr\n\t"   // args = NULL
                     "mov x8, #0x03\n\t" // SYS_TASK_SPAWN
                     "svc #0\n\t"
                     "mov %[result], x0\n\t"
                     "mov %[pid], x1\n\t"
                     "mov %[tid], x2\n\t"
                     : [result] "=r"(result), [pid] "=r"(pid), [tid] "=r"(tid)
                     : [path] "r"(path)
                     : "x0", "x1", "x2", "x8", "memory");

    (void)pid;
    (void)tid;
}

void Desktop::drawBackdrop() {
    // Solid Workbench blue
    gui_fill_rect(m_window, 0, MENU_BAR_HEIGHT, m_width, m_height - MENU_BAR_HEIGHT, WB_BLUE);
}

void Desktop::drawMenuBar() {
    // Menu bar background
    gui_fill_rect(m_window, 0, 0, m_width, MENU_BAR_HEIGHT, WB_GRAY_LIGHT);

    // Bottom border
    gui_draw_hline(m_window, 0, m_width - 1, MENU_BAR_HEIGHT - 1, WB_GRAY_DARK);

    // Top highlight
    gui_draw_hline(m_window, 0, m_width - 1, 0, WB_WHITE);

    // Menu titles
    gui_draw_text(m_window, 8, 6, "Workbench", WB_BLACK);
    gui_draw_text(m_window, 96, 6, "Window", WB_BLACK);
    gui_draw_text(m_window, 168, 6, "Tools", WB_BLACK);

    // Right side: ViperDOS branding
    gui_draw_text(m_window, m_width - 80, 6, "ViperDOS", WB_GRAY_DARK);
}

void Desktop::drawIconPixels(int x, int y, const uint32_t *pixels) {
    uint32_t *fb = gui_get_pixels(m_window);
    uint32_t stride = gui_get_stride(m_window) / 4;

    for (int py = 0; py < ICON_SIZE; py++) {
        for (int px = 0; px < ICON_SIZE; px++) {
            uint32_t color = pixels[py * ICON_SIZE + px];
            if (color != 0) { // 0 = transparent
                int dx = x + px;
                int dy = y + py;
                if (dx >= 0 && dx < static_cast<int>(m_width) && dy >= 0 &&
                    dy < static_cast<int>(m_height)) {
                    fb[dy * stride + dx] = color;
                }
            }
        }
    }
}

void Desktop::drawIcon(DesktopIcon &icon) {
    // Draw selection highlight if selected
    if (icon.selected) {
        // Orange highlight box behind icon
        gui_fill_rect(m_window, icon.x - 4, icon.y - 4, 32, 32, WB_ORANGE);
    }

    // Draw the icon pixels (centered in a 24x24 area)
    drawIconPixels(icon.x, icon.y, icon.pixels);

    // Draw label below icon (centered)
    int label_len = strlen(icon.label);
    int label_x = icon.x + 12 - (label_len * 4); // Center under 24px icon
    int label_y = icon.y + ICON_LABEL_OFFSET;

    // Label background for readability (if selected)
    if (icon.selected) {
        gui_fill_rect(m_window, label_x - 2, label_y - 1, label_len * 8 + 4, 10, WB_ORANGE);
        gui_draw_text(m_window, label_x, label_y, icon.label, WB_WHITE);
    } else {
        // Draw text with shadow for visibility on blue
        gui_draw_text(m_window, label_x + 1, label_y + 1, icon.label, WB_BLACK);
        gui_draw_text(m_window, label_x, label_y, icon.label, WB_WHITE);
    }
}

void Desktop::drawAllIcons() {
    for (int i = 0; i < m_iconCount; i++) {
        drawIcon(m_icons[i]);
    }
}

void Desktop::redraw() {
    drawBackdrop();
    drawMenuBar();
    drawAllIcons();
    gui_present(m_window);
}

void Desktop::layoutIcons() {
    int x = ICON_START_X;
    int y = ICON_START_Y;

    for (int i = 0; i < m_iconCount; i++) {
        m_icons[i].x = x;
        m_icons[i].y = y;

        x += ICON_SPACING_X;
        if (x + ICON_SIZE > static_cast<int>(m_width) - 40) {
            x = ICON_START_X;
            y += ICON_SPACING_Y;
        }
    }
}

void Desktop::discoverVolumes() {
    // Query available assigns (volumes) from the kernel
    AssignInfo assigns[16];
    usize count = 0;

    int result = assign_list(assigns, 16, &count);
    if (result != 0) {
        debug_serial("[workbench] Failed to list assigns, using defaults\n");
        // Fallback: add default SYS: icon
        static const char *sysLabel = "SYS:";
        static const char *sysTarget = "/";
        m_icons[m_iconCount++] = {
            0, 0, sysLabel, sysTarget, icons::disk_24, IconAction::OpenFileBrowser, false};
        return;
    }

    debug_serial("[workbench] Found ");
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", static_cast<int>(count));
    debug_serial(buf);
    debug_serial(" assigns\n");

    // Add volume icons for user-visible filesystem assigns
    // We store labels statically since DesktopIcon expects const char*
    static char volumeLabels[12][32]; // Up to 12 volumes
    static char volumePaths[12][MAX_PATH_LEN];
    int volumeIdx = 0;

    // Maximum volumes to show (leave room for Shell, Prefs, Help)
    constexpr int MAX_VOLUME_ICONS = 12;

    for (usize i = 0; i < count && volumeIdx < MAX_VOLUME_ICONS; i++) {
        // Skip service assigns (ASSIGN_SERVICE flag = 0x08)
        if (assigns[i].flags & 0x08) {
            continue;
        }

        // Skip D0: (duplicate of SYS:) and internal assigns like CERTS:
        if (strcmp(assigns[i].name, "D0") == 0 || strcmp(assigns[i].name, "CERTS") == 0) {
            continue;
        }

        // Create label with colon suffix
        snprintf(volumeLabels[volumeIdx], sizeof(volumeLabels[volumeIdx]), "%s:", assigns[i].name);

        // Map common assigns to their paths
        if (strcmp(assigns[i].name, "SYS") == 0) {
            strncpy(volumePaths[volumeIdx], "/", sizeof(volumePaths[volumeIdx]) - 1);
        } else if (strcmp(assigns[i].name, "C") == 0) {
            strncpy(volumePaths[volumeIdx], "/c", sizeof(volumePaths[volumeIdx]) - 1);
        } else if (strcmp(assigns[i].name, "S") == 0) {
            strncpy(volumePaths[volumeIdx], "/s", sizeof(volumePaths[volumeIdx]) - 1);
        } else if (strcmp(assigns[i].name, "L") == 0) {
            strncpy(volumePaths[volumeIdx], "/libs", sizeof(volumePaths[volumeIdx]) - 1);
        } else if (strcmp(assigns[i].name, "T") == 0) {
            strncpy(volumePaths[volumeIdx], "/t", sizeof(volumePaths[volumeIdx]) - 1);
        } else {
            // Default: use /name for the path (lowercase)
            snprintf(
                volumePaths[volumeIdx], sizeof(volumePaths[volumeIdx]), "/%s", assigns[i].name);
            for (char *p = volumePaths[volumeIdx] + 1; *p; p++) {
                if (*p >= 'A' && *p <= 'Z') {
                    *p = *p - 'A' + 'a';
                }
            }
        }

        debug_serial("[workbench] Volume: ");
        debug_serial(volumeLabels[volumeIdx]);
        debug_serial(" -> ");
        debug_serial(volumePaths[volumeIdx]);
        debug_serial("\n");

        // Add icon - use disk icon for all volumes
        m_icons[m_iconCount++] = {0,
                                  0,
                                  volumeLabels[volumeIdx],
                                  volumePaths[volumeIdx],
                                  icons::disk_24,
                                  IconAction::OpenFileBrowser,
                                  false};

        volumeIdx++;
    }

    // If no volumes were found, add default SYS:
    if (volumeIdx == 0) {
        static const char *sysLabel = "SYS:";
        static const char *sysTarget = "/";
        m_icons[m_iconCount++] = {
            0, 0, sysLabel, sysTarget, icons::disk_24, IconAction::OpenFileBrowser, false};
    }
}

int Desktop::findIconAt(int x, int y) {
    for (int i = 0; i < m_iconCount; i++) {
        DesktopIcon &icon = m_icons[i];
        // Icon clickable area: 24x24 icon + label below
        int icon_left = icon.x - 4;
        int icon_top = icon.y - 4;
        int icon_right = icon.x + 28;
        int icon_bottom = icon.y + ICON_LABEL_OFFSET + 12;

        if (x >= icon_left && x < icon_right && y >= icon_top && y < icon_bottom) {
            return i;
        }
    }
    return -1;
}

void Desktop::deselectAll() {
    for (int i = 0; i < m_iconCount; i++) {
        m_icons[i].selected = false;
    }
}

void Desktop::selectIcon(int index) {
    deselectAll();
    if (index >= 0 && index < m_iconCount) {
        m_icons[index].selected = true;
    }
    redraw();
}

void Desktop::handleClick(int x, int y, int button) {
    if (button != 0)
        return; // Only handle left button

    int icon_idx = findIconAt(x, y);

    // Check for double-click using real time
    uint64_t now = get_uptime_ms();
    bool is_double_click = false;

    if (icon_idx >= 0 && icon_idx == m_lastClickIcon) {
        uint64_t elapsed = now - m_lastClickTime;
        if (elapsed < static_cast<uint64_t>(DOUBLE_CLICK_MS)) {
            is_double_click = true;
        }
    }

    m_lastClickIcon = icon_idx;
    m_lastClickTime = now;

    if (is_double_click && icon_idx >= 0) {
        // Double-click: perform icon action
        DesktopIcon &icon = m_icons[icon_idx];
        switch (icon.action) {
            case IconAction::OpenFileBrowser:
                openFileBrowser(icon.target);
                break;
            case IconAction::LaunchProgram:
                if (icon.target) {
                    spawnProgram(icon.target);
                }
                break;
            case IconAction::ShowDialog:
                // Show appropriate dialog based on icon label
                if (strcmp(icon.label, "Help") == 0) {
                    showAboutDialog();
                } else if (strcmp(icon.label, "Prefs") == 0) {
                    showPrefsDialog();
                }
                break;
            case IconAction::None:
                break;
        }
        // Reset double-click state to prevent immediate re-trigger
        m_lastClickIcon = -1;
        m_lastClickTime = 0;
    } else if (icon_idx >= 0) {
        // Single click: select the icon
        selectIcon(icon_idx);
    } else {
        // Click on backdrop: deselect all
        deselectAll();
        redraw();
    }
}

void Desktop::handleDesktopEvent(const gui_event_t &event) {
    switch (event.type) {
        case GUI_EVENT_MOUSE:
            if (event.mouse.event_type == 1) { // Button down
                handleClick(event.mouse.x, event.mouse.y, event.mouse.button);
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

void Desktop::handleBrowserEvents() {
    // Poll events from all open file browser windows
    // Iterate backwards so we can safely remove closed browsers
    for (int i = m_browserCount - 1; i >= 0; i--) {
        FileBrowser *browser = m_browsers[i];
        if (!browser || !browser->isOpen()) {
            continue;
        }

        gui_event_t event;
        if (gui_poll_event(browser->window(), &event) == 0) {
            browser->handleEvent(event);
        }
    }
}

void Desktop::showAboutDialog() {
    // Close existing dialog if open
    if (m_aboutDialog) {
        gui_destroy_window(m_aboutDialog);
        m_aboutDialog = nullptr;
    }

    // Create the About dialog
    m_aboutDialog = gui_create_window("About ViperDOS", 300, 200);
    if (!m_aboutDialog) {
        debug_serial("[workbench] Failed to create About dialog\n");
        return;
    }

    // Draw dialog content
    gui_fill_rect(m_aboutDialog, 0, 0, 300, 200, WB_GRAY_LIGHT);

    // Title
    gui_draw_text(m_aboutDialog, 80, 20, "ViperDOS Workbench", WB_BLACK);

    // Version info
    gui_draw_text(m_aboutDialog, 100, 50, "Version 0.5.0", WB_GRAY_DARK);

    // Description
    gui_draw_text(m_aboutDialog, 40, 80, "An Amiga-inspired desktop", WB_BLACK);
    gui_draw_text(m_aboutDialog, 30, 100, "for the Viper microkernel OS", WB_BLACK);

    // Copyright
    gui_draw_text(m_aboutDialog, 60, 140, "(C) 2025 ViperDOS Team", WB_GRAY_DARK);

    // Close hint
    gui_draw_text(m_aboutDialog, 70, 170, "Click [X] to close", WB_GRAY_MED);

    gui_present(m_aboutDialog);
    debug_serial("[workbench] Opened About dialog\n");
}

void Desktop::showPrefsDialog() {
    // Close existing dialog if open
    if (m_prefsDialog) {
        gui_destroy_window(m_prefsDialog);
        m_prefsDialog = nullptr;
    }

    // Create the Prefs dialog
    m_prefsDialog = gui_create_window("Preferences", 350, 250);
    if (!m_prefsDialog) {
        debug_serial("[workbench] Failed to create Prefs dialog\n");
        return;
    }

    // Draw dialog content
    gui_fill_rect(m_prefsDialog, 0, 0, 350, 250, WB_GRAY_LIGHT);

    // Title
    gui_draw_text(m_prefsDialog, 100, 20, "Workbench Preferences", WB_BLACK);

    // Placeholder content
    gui_draw_text(m_prefsDialog, 20, 60, "Screen:", WB_BLACK);
    gui_draw_text(m_prefsDialog, 100, 60, "1024 x 768", WB_GRAY_DARK);

    gui_draw_text(m_prefsDialog, 20, 90, "Backdrop:", WB_BLACK);
    gui_draw_text(m_prefsDialog, 100, 90, "Workbench Blue", WB_GRAY_DARK);

    gui_draw_text(m_prefsDialog, 20, 120, "Icons:", WB_BLACK);
    gui_draw_text(m_prefsDialog, 100, 120, "4 desktop icons", WB_GRAY_DARK);

    // Note about future functionality
    gui_fill_rect(m_prefsDialog, 20, 160, 310, 50, WB_BLUE);
    gui_draw_text(m_prefsDialog, 40, 175, "Full preferences editor", WB_WHITE);
    gui_draw_text(m_prefsDialog, 40, 195, "coming in Phase 3!", WB_WHITE);

    gui_present(m_prefsDialog);
    debug_serial("[workbench] Opened Prefs dialog\n");
}

void Desktop::handleDialogEvents() {
    // Handle About dialog events
    if (m_aboutDialog) {
        gui_event_t event;
        if (gui_poll_event(m_aboutDialog, &event) == 0) {
            if (event.type == GUI_EVENT_CLOSE) {
                gui_destroy_window(m_aboutDialog);
                m_aboutDialog = nullptr;
                debug_serial("[workbench] Closed About dialog\n");
            }
        }
    }

    // Handle Prefs dialog events
    if (m_prefsDialog) {
        gui_event_t event;
        if (gui_poll_event(m_prefsDialog, &event) == 0) {
            if (event.type == GUI_EVENT_CLOSE) {
                gui_destroy_window(m_prefsDialog);
                m_prefsDialog = nullptr;
                debug_serial("[workbench] Closed Prefs dialog\n");
            }
        }
    }
}

} // namespace workbench
