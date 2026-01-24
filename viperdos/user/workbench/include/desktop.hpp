#pragma once
/// @file desktop.hpp
/// @brief Desktop manager class.

#include "types.hpp"
#include <gui.h>

namespace workbench {

class FileBrowser; // Forward declaration

/// @brief Manages the desktop surface, icons, and menu bar.
class Desktop {
  public:
    Desktop();
    ~Desktop();

    /// @brief Initialize the desktop (connect to displayd, create surface).
    /// @return true on success.
    bool init();

    /// @brief Run the main event loop.
    void run();

    /// @brief Get screen dimensions.
    uint32_t width() const {
        return m_width;
    }

    uint32_t height() const {
        return m_height;
    }

    /// @brief Get the desktop window handle.
    gui_window_t *window() const {
        return m_window;
    }

    /// @brief Open a file browser for the given path.
    void openFileBrowser(const char *path);

    /// @brief Close a file browser window.
    void closeFileBrowser(FileBrowser *browser);

    /// @brief Spawn a program.
    void spawnProgram(const char *path);

  private:
    void drawBackdrop();
    void drawMenuBar();
    void drawPulldownMenu();
    void drawIcon(DesktopIcon &icon);
    void drawAllIcons();
    void redraw();

    // Menu handling
    int findMenuAt(int x, int y);
    int findMenuItemAt(int x, int y);
    void handleMenuAction(PulldownAction action);
    void openMenu(int menuIdx);
    void closeMenu();

    void layoutIcons();
    int findIconAt(int x, int y);
    void deselectAll();
    void selectIcon(int index);

    void handleClick(int x, int y, int button);
    void handleDesktopEvent(const gui_event_t &event);
    void handleBrowserEvents();

    void drawIconPixels(int x, int y, const uint32_t *pixels);

    // Volume discovery
    void discoverVolumes();

    // Dialog support
    void showAboutDialog();
    void showPrefsDialog();
    void handleDialogEvents();

  private:
    gui_window_t *m_window = nullptr;
    uint32_t m_width = 1024;
    uint32_t m_height = 768;

    DesktopIcon m_icons[16]; // Support up to 16 desktop icons
    int m_iconCount = 0;

    // Double-click detection
    int m_lastClickIcon = -1;
    uint64_t m_lastClickTime = 0;

    // File browser windows
    FileBrowser *m_browsers[MAX_BROWSERS] = {};
    int m_browserCount = 0;

    // Dialog windows
    gui_window_t *m_aboutDialog = nullptr;
    gui_window_t *m_prefsDialog = nullptr;

    // Pulldown menu state
    int m_activeMenu = -1;    // Currently open menu (-1 = none)
    int m_hoveredItem = -1;   // Currently hovered item in open menu
    PulldownMenu m_menus[3];  // Workbench, Window, Tools
    int m_menuCount = 3;
};

} // namespace workbench
