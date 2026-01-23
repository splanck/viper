#pragma once
/// @file types.hpp
/// @brief Common types for Workbench.

#include <stdint.h>

namespace workbench {

// Layout constants
constexpr int MENU_BAR_HEIGHT   = 20;
constexpr int ICON_SIZE         = 24;      // Icons are 24x24
constexpr int ICON_SPACING_X    = 80;
constexpr int ICON_SPACING_Y    = 70;
constexpr int ICON_START_X      = 40;
constexpr int ICON_START_Y      = 50;
constexpr int ICON_LABEL_OFFSET = 36;
constexpr int DOUBLE_CLICK_MS   = 400;

// File browser constants
constexpr int FB_TOOLBAR_HEIGHT = 24;
constexpr int FB_STATUSBAR_HEIGHT = 20;
constexpr int FB_ICON_GRID_X    = 80;
constexpr int FB_ICON_GRID_Y    = 64;
constexpr int FB_PADDING        = 8;

// Maximum limits
constexpr int MAX_PATH_LEN      = 256;
constexpr int MAX_FILENAME_LEN  = 64;
constexpr int MAX_FILES_PER_DIR = 128;
constexpr int MAX_BROWSERS      = 8;

/// @brief File entry type.
enum class FileType {
    Directory,
    Executable,   // .sys, .prg
    Text,         // .txt, .md, .c, .h, .cpp, .hpp
    Image,        // .bmp
    Unknown
};

/// @brief Represents a file/directory entry.
struct FileEntry {
    char name[MAX_FILENAME_LEN];
    FileType type;
    uint64_t size;
    bool selected;
};

/// @brief Desktop icon action types.
enum class IconAction {
    None,
    OpenFileBrowser,  // Open a file browser window (for drives)
    LaunchProgram,    // Launch an executable
    ShowDialog        // Show a dialog (Settings, About)
};

/// @brief Desktop icon definition.
struct DesktopIcon {
    int x, y;                      // Position on desktop
    const char *label;             // Text below icon
    const char *target;            // Path or command
    const uint32_t *pixels;        // 24x24 icon pixel data
    IconAction action;             // What to do on double-click
    bool selected;
};

} // namespace workbench
