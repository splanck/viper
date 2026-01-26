#pragma once
//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
/**
 * @file desktop.hpp
 * @brief Desktop manager class for the ViperDOS Workbench.
 *
 * This file defines the Desktop class which manages the main Workbench
 * desktop environment including:
 * - Full-screen backdrop surface
 * - Desktop icons (volumes, programs, utilities)
 * - Pulldown menu bar
 * - File browser window management
 * - Dialog windows
 *
 * ## Architecture
 *
 * The Desktop is the root object of the Workbench application:
 *
 * ```
 * Desktop
 *   |
 *   +-- m_window (full-screen GUI surface)
 *   |
 *   +-- m_icons[] (desktop icons array)
 *   |
 *   +-- m_menus[] (pulldown menu definitions)
 *   |
 *   +-- m_browsers[] (open file browser windows)
 *   |
 *   +-- m_aboutDialog, m_prefsDialog (modal dialogs)
 * ```
 *
 * ## Event Loop
 *
 * The Desktop runs the main event loop which:
 * 1. Polls the desktop surface for events
 * 2. Polls all open file browser windows
 * 3. Polls any open dialog windows
 * 4. Yields CPU to prevent busy-waiting
 *
 * ## Volume Discovery
 *
 * On startup, the Desktop queries the kernel for mounted volumes
 * (assigns) and creates icons for each. This allows dynamic
 * discovery of storage devices.
 *
 * @see filebrowser.hpp for file browser windows
 * @see types.hpp for icon and menu types
 */
//===----------------------------------------------------------------------===//

#include "types.hpp"
#include <gui.h>

namespace workbench {

class FileBrowser; // Forward declaration

//===----------------------------------------------------------------------===//
// Desktop Class
//===----------------------------------------------------------------------===//

/**
 * @brief Manages the Workbench desktop environment.
 *
 * The Desktop class is the main controller for the Workbench GUI. It
 * creates a full-screen window, renders the desktop backdrop and icons,
 * handles the menu bar, and manages child windows (file browsers, dialogs).
 *
 * ## Lifecycle
 *
 * 1. Construct Desktop object
 * 2. Call init() to connect to displayd and create surface
 * 3. Call run() to enter the main event loop
 * 4. Destructor cleans up all windows
 *
 * ## Usage
 *
 * ```cpp
 * Desktop desktop;
 * if (!desktop.init()) {
 *     return 1;
 * }
 * desktop.run();  // Never returns normally
 * ```
 */
class Desktop {
  public:
    /**
     * @brief Constructs an uninitialized Desktop.
     *
     * Call init() after construction to create the GUI surface.
     */
    Desktop();

    /**
     * @brief Destroys the Desktop and all managed windows.
     *
     * Closes all file browsers, dialogs, and the main window.
     * Calls gui_shutdown() to disconnect from displayd.
     */
    ~Desktop();

    //=== Initialization ===//

    /**
     * @brief Initializes the desktop GUI.
     *
     * Performs the following initialization:
     * 1. Connects to displayd via gui_init()
     * 2. Queries display dimensions
     * 3. Creates full-screen desktop surface
     * 4. Sets up pulldown menus
     * 5. Discovers and creates volume icons
     * 6. Adds system icons (Shell, Prefs, Help)
     * 7. Performs initial render
     *
     * @return true on success, false if GUI initialization fails.
     */
    bool init();

    /**
     * @brief Runs the main event loop.
     *
     * This function never returns under normal operation. It continuously:
     * - Polls desktop events (mouse, keyboard)
     * - Polls file browser window events
     * - Polls dialog window events
     * - Yields CPU with sched_yield syscall
     */
    void run();

    //=== Accessors ===//

    /**
     * @brief Returns the desktop width in pixels.
     * @return Screen width (typically 1024).
     */
    uint32_t width() const { return m_width; }

    /**
     * @brief Returns the desktop height in pixels.
     * @return Screen height (typically 768).
     */
    uint32_t height() const { return m_height; }

    /**
     * @brief Returns the desktop window handle.
     * @return Pointer to the GUI window structure.
     */
    gui_window_t *window() const { return m_window; }

    //=== Window Management ===//

    /**
     * @brief Opens a new file browser window.
     *
     * Creates a FileBrowser for the specified path and adds it
     * to the managed browser list.
     *
     * @param path Initial directory path (e.g., "/", "/c").
     *
     * @note Limited to MAX_BROWSERS simultaneous windows.
     */
    void openFileBrowser(const char *path);

    /**
     * @brief Closes a file browser window.
     *
     * Destroys the browser and removes it from the managed list.
     *
     * @param browser Pointer to the browser to close.
     */
    void closeFileBrowser(FileBrowser *browser);

    /**
     * @brief Spawns a new process.
     *
     * Uses the SYS_TASK_SPAWN syscall to launch a program.
     *
     * @param path Path to the executable (e.g., "/c/prefs.prg").
     * @param args Optional command line arguments (e.g., a file path to open).
     */
    void spawnProgram(const char *path, const char *args = nullptr);

  private:
    void drawBackdrop();
    void drawMenuBar();
    void drawPulldownMenu();
    void registerMenuBar();
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
