#pragma once
/// @file filebrowser.hpp
/// @brief File browser window class.

#include "types.hpp"
#include <gui.h>

namespace workbench {

class Desktop;  // Forward declaration

/// @brief Manages a file browser window for navigating directories.
class FileBrowser {
public:
    FileBrowser(Desktop *desktop, const char *initialPath);
    ~FileBrowser();

    /// @brief Initialize the file browser window.
    /// @return true on success.
    bool init();

    /// @brief Get the browser window handle.
    gui_window_t *window() const { return m_window; }

    /// @brief Check if this browser is still open.
    bool isOpen() const { return m_window != nullptr; }

    /// @brief Handle an event for this browser.
    /// @return true if the event was consumed.
    bool handleEvent(const gui_event_t &event);

    /// @brief Navigate to a new directory.
    void navigateTo(const char *path);

    /// @brief Navigate to parent directory.
    void navigateUp();

    /// @brief Get the current path.
    const char *currentPath() const { return m_currentPath; }

private:
    void loadDirectory();
    void redraw();
    void drawToolbar();
    void drawFileList();
    void drawStatusBar();
    void drawFileIcon(int x, int y, FileType type);

    int findFileAt(int x, int y);
    void handleClick(int x, int y, int button);
    void handleDoubleClick(int fileIndex);

    FileType determineFileType(const char *name, bool isDir);
    const uint32_t *getIconForType(FileType type);

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
};

} // namespace workbench
