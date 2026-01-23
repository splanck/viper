/// @file desktop.cpp
/// @brief Desktop class implementation.

#include "../include/desktop.hpp"
#include "../include/filebrowser.hpp"
#include "../include/colors.hpp"
#include "../include/icons.hpp"
#include <gui.h>
#include <string.h>
#include <stdint.h>

namespace workbench {

// Get system uptime in milliseconds (SYS_TIME_UPTIME = 0xA2)
static uint64_t get_uptime_ms()
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

Desktop::Desktop()
{
}

Desktop::~Desktop()
{
    // Close any open file browsers
    for (int i = 0; i < m_browserCount; i++) {
        if (m_browsers[i]) {
            closeFileBrowser(m_browsers[i]);
        }
    }

    if (m_window) {
        gui_destroy_window(m_window);
    }
    gui_shutdown();
}

bool Desktop::init()
{
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
    m_window = gui_create_window_ex("Workbench", m_width, m_height,
                                     GUI_FLAG_SYSTEM | GUI_FLAG_NO_DECORATIONS);
    if (!m_window) {
        gui_shutdown();
        return false;
    }

    // Position at 0,0 (behind all other windows)
    gui_set_position(m_window, 0, 0);

    // Set up desktop icons
    m_icons[0] = { 0, 0, "SYS:",     "/",                  icons::disk_24,     IconAction::OpenFileBrowser, false };
    m_icons[1] = { 0, 0, "Shell",    "/sys/consoled.sys",  icons::shell_24,    IconAction::LaunchProgram,   false };
    m_icons[2] = { 0, 0, "Settings", nullptr,              icons::settings_24, IconAction::ShowDialog,      false };
    m_icons[3] = { 0, 0, "About",    nullptr,              icons::about_24,    IconAction::ShowDialog,      false };
    m_iconCount = 4;

    // Layout and draw
    layoutIcons();
    redraw();

    return true;
}

void Desktop::run()
{
    while (true) {
        // Handle desktop events
        gui_event_t event;
        if (gui_poll_event(m_window, &event) == 0) {
            handleDesktopEvent(event);
        }

        // Handle file browser events
        handleBrowserEvents();

        // Yield to other processes
        __asm__ volatile("mov x8, #0x0E\n\t"
                         "svc #0"
                         ::: "x8");
    }
}

void Desktop::openFileBrowser(const char *path)
{
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

void Desktop::closeFileBrowser(FileBrowser *browser)
{
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

void Desktop::spawnProgram(const char *path)
{
    debug_serial("[workbench] Spawning: ");
    debug_serial(path);
    debug_serial("\n");

    // Use inline assembly for spawn syscall
    // SYS_TASK_SPAWN = 0x03
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
        : [path] "r" (path)
        : "x0", "x1", "x2", "x8", "memory"
    );

    (void)pid;
    (void)tid;
}

void Desktop::drawBackdrop()
{
    // Solid Workbench blue
    gui_fill_rect(m_window, 0, MENU_BAR_HEIGHT, m_width,
                  m_height - MENU_BAR_HEIGHT, WB_BLUE);
}

void Desktop::drawMenuBar()
{
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

void Desktop::drawIconPixels(int x, int y, const uint32_t *pixels)
{
    uint32_t *fb = gui_get_pixels(m_window);
    uint32_t stride = gui_get_stride(m_window) / 4;

    for (int py = 0; py < ICON_SIZE; py++) {
        for (int px = 0; px < ICON_SIZE; px++) {
            uint32_t color = pixels[py * ICON_SIZE + px];
            if (color != 0) {  // 0 = transparent
                int dx = x + px;
                int dy = y + py;
                if (dx >= 0 && dx < static_cast<int>(m_width) &&
                    dy >= 0 && dy < static_cast<int>(m_height)) {
                    fb[dy * stride + dx] = color;
                }
            }
        }
    }
}

void Desktop::drawIcon(DesktopIcon &icon)
{
    // Draw selection highlight if selected
    if (icon.selected) {
        // Orange highlight box behind icon
        gui_fill_rect(m_window, icon.x - 4, icon.y - 4, 32, 32, WB_ORANGE);
    }

    // Draw the icon pixels (centered in a 24x24 area)
    drawIconPixels(icon.x, icon.y, icon.pixels);

    // Draw label below icon (centered)
    int label_len = strlen(icon.label);
    int label_x = icon.x + 12 - (label_len * 4);  // Center under 24px icon
    int label_y = icon.y + ICON_LABEL_OFFSET;

    // Label background for readability (if selected)
    if (icon.selected) {
        gui_fill_rect(m_window, label_x - 2, label_y - 1,
                      label_len * 8 + 4, 10, WB_ORANGE);
        gui_draw_text(m_window, label_x, label_y, icon.label, WB_WHITE);
    } else {
        // Draw text with shadow for visibility on blue
        gui_draw_text(m_window, label_x + 1, label_y + 1, icon.label, WB_BLACK);
        gui_draw_text(m_window, label_x, label_y, icon.label, WB_WHITE);
    }
}

void Desktop::drawAllIcons()
{
    for (int i = 0; i < m_iconCount; i++) {
        drawIcon(m_icons[i]);
    }
}

void Desktop::redraw()
{
    drawBackdrop();
    drawMenuBar();
    drawAllIcons();
    gui_present(m_window);
}

void Desktop::layoutIcons()
{
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

int Desktop::findIconAt(int x, int y)
{
    for (int i = 0; i < m_iconCount; i++) {
        DesktopIcon &icon = m_icons[i];
        // Icon clickable area: 24x24 icon + label below
        int icon_left = icon.x - 4;
        int icon_top = icon.y - 4;
        int icon_right = icon.x + 28;
        int icon_bottom = icon.y + ICON_LABEL_OFFSET + 12;

        if (x >= icon_left && x < icon_right &&
            y >= icon_top && y < icon_bottom) {
            return i;
        }
    }
    return -1;
}

void Desktop::deselectAll()
{
    for (int i = 0; i < m_iconCount; i++) {
        m_icons[i].selected = false;
    }
}

void Desktop::selectIcon(int index)
{
    deselectAll();
    if (index >= 0 && index < m_iconCount) {
        m_icons[index].selected = true;
    }
    redraw();
}

void Desktop::handleClick(int x, int y, int button)
{
    if (button != 0) return;  // Only handle left button

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
                // TODO: Implement dialogs
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

void Desktop::handleDesktopEvent(const gui_event_t &event)
{
    switch (event.type) {
        case GUI_EVENT_MOUSE:
            if (event.mouse.event_type == 1) {  // Button down
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

void Desktop::handleBrowserEvents()
{
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

} // namespace workbench
