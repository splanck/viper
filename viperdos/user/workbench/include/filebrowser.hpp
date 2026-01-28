#pragma once
//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
/**
 * @file filebrowser.hpp
 * @brief File browser window class for the Workbench.
 *
 * This file defines the FileBrowser class which provides a graphical
 * file browser window for navigating the filesystem. Features include:
 *
 * - Directory listing with icons per file type
 * - Navigation via double-click or toolbar
 * - File selection and multi-select
 * - Context menu for file operations
 * - Inline rename editor
 * - Scrollbar for long listings
 *
 * ## Window Layout
 *
 * ```
 * +--[ Files: /path ]--------[X]---+
 * | [^] /current/path              |  Toolbar
 * +--------------------------------+
 * |  +------+  +------+  +------+  |
 * |  | icon |  | icon |  | icon |  |
 * |  +------+  +------+  +------+  |
 * |   file1    file2     file3     |  File Grid
 * |                                |
 * +--------------------------------+
 * | file1.txt - 1234 bytes         |  Status Bar
 * +--------------------------------+
 * ```
 *
 * ## Event Handling
 *
 * The browser handles several event types:
 * - Mouse clicks for selection and navigation
 * - Right-clicks for context menu
 * - Keyboard for navigation and shortcuts
 * - Scroll events for scrollbar
 *
 * @see types.hpp for file entry and menu types
 * @see desktop.hpp for browser management
 */
//===----------------------------------------------------------------------===//

#include "types.hpp"
#include <gui.h>

namespace workbench {

class Desktop; // Forward declaration

//===----------------------------------------------------------------------===//
// FileBrowser Class
//===----------------------------------------------------------------------===//

/**
 * @brief Manages a file browser window for navigating directories.
 *
 * Each FileBrowser instance represents a single window showing the
 * contents of a directory. Multiple browsers can be open simultaneously,
 * up to MAX_BROWSERS.
 *
 * ## Lifecycle
 *
 * 1. Constructed by Desktop::openFileBrowser()
 * 2. init() creates the GUI window and loads directory
 * 3. Events are dispatched via handleEvent()
 * 4. Destroyed by Desktop::closeFileBrowser() on window close
 *
 * ## File Operations
 *
 * The browser supports several file operations:
 * - **Open**: Double-click or Enter to open file/directory
 * - **Delete**: Delete key or context menu
 * - **Rename**: F2 or context menu (inline editor)
 * - **Copy/Paste**: Context menu (uses global clipboard)
 * - **New Folder**: Context menu on empty area
 */
class FileBrowser {
  public:
    /**
     * @brief Constructs a file browser for the given path.
     *
     * @param desktop Pointer to the parent Desktop (for callbacks).
     * @param initialPath Directory path to display initially.
     */
    FileBrowser(Desktop *desktop, const char *initialPath);

    /**
     * @brief Destroys the browser and its window.
     */
    ~FileBrowser();

    //=== Initialization ===//

    /**
     * @brief Creates the browser window and loads directory contents.
     *
     * @return true on success, false if window creation fails.
     */
    bool init();

    //=== Accessors ===//

    /**
     * @brief Returns the browser's GUI window handle.
     * @return Pointer to gui_window_t.
     */
    gui_window_t *window() const {
        return m_window;
    }

    /**
     * @brief Checks if the browser window is still open.
     * @return true if the window exists.
     */
    bool isOpen() const {
        return m_window != nullptr;
    }

    /**
     * @brief Checks if the browser is marked for closing.
     * @return true if close was requested.
     */
    bool isClosing() const {
        return m_closing;
    }

    /**
     * @brief Returns the current directory path.
     * @return Null-terminated path string.
     */
    const char *currentPath() const {
        return m_currentPath;
    }

    //=== Event Handling ===//

    /**
     * @brief Handles a GUI event for this browser.
     *
     * Processes mouse clicks, keyboard input, scroll events,
     * and window close events.
     *
     * @param event The GUI event to process.
     * @return true if the event was consumed.
     */
    bool handleEvent(const gui_event_t &event);

    //=== Navigation ===//

    /**
     * @brief Navigates to a new directory.
     *
     * Loads the directory contents, updates the title bar,
     * and redraws the window.
     *
     * @param path Absolute path to navigate to.
     */
    void navigateTo(const char *path);

    /**
     * @brief Navigates to the parent directory.
     *
     * Moves up one level in the directory hierarchy.
     * Does nothing if already at root "/".
     */
    void navigateUp();

  private:
    void loadDirectory();
    void redraw();
    void drawToolbar();
    void drawFileList();
    void drawStatusBar();
    void drawFileIcon(int x, int y, FileType type);
    void updateScrollbar();
    int calculateContentHeight();

    int findFileAt(int x, int y);
    void handleClick(int x, int y, int button);
    void handleDoubleClick(int fileIndex);
    void handleKeyPress(int keycode);

    FileType determineFileType(const char *name, bool isDir);
    const uint32_t *getIconForType(FileType type);

    // Context menu
    void showContextMenu(int x, int y, int fileIndex);
    void hideContextMenu();
    void drawContextMenu();
    void handleMenuClick(int x, int y);
    void executeMenuAction(MenuAction action);

    // File operations
    bool deleteFile(int fileIndex);
    bool renameFile(int fileIndex, const char *newName);
    void refreshDirectory();
    void copyFile(int fileIndex);
    void cutFile(int fileIndex);
    bool pasteFile();
    bool createNewFolder();

    // Inline rename editor
    void startRename(int fileIndex);
    void cancelRename();
    void commitRename();
    void handleRenameKey(int keycode, bool shift);
    void drawRenameEditor();

    // Properties dialog
    void showProperties(int fileIndex);

  private:
    Desktop *m_desktop;
    gui_window_t *m_window = nullptr;

    char m_currentPath[MAX_PATH_LEN];
    FileEntry m_files[MAX_FILES_PER_DIR];
    int m_fileCount = 0;

    int m_scrollOffset = 0;
    int m_selectedFile = -1;

    // Window dimensions
    int m_width = 400;
    int m_height = 300;

    // Double-click detection
    int m_lastClickFile = -1;
    uint64_t m_lastClickTime = 0;

    // Context menu state
    ContextMenu m_contextMenu = {};
    int m_contextMenuFile = -1; // File index the menu was opened for

    // Inline rename editor state
    RenameEditor m_renameEditor = {};

    // Close request flag (deferred deletion to avoid use-after-free)
    bool m_closing = false;
};

} // namespace workbench
