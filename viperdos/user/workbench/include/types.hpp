#pragma once
//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
/**
 * @file types.hpp
 * @brief Common types and constants for the ViperDOS Workbench application.
 *
 * This file defines the fundamental data structures and constants used
 * throughout the Workbench desktop environment, including:
 * - Layout constants for desktop icons and menu bar
 * - File browser dimensions and constraints
 * - Type definitions for files, icons, and menus
 *
 * ## Workbench Visual Layout
 *
 * ```
 * +----------------------------------------------------------+
 * | Workbench  Window  Tools                    ViperDOS     |  Menu Bar (20px)
 * +----------------------------------------------------------+
 * |  +------+    +------+    +------+    +------+            |
 * |  | icon |    | icon |    | icon |    | icon |            |
 * |  +------+    +------+    +------+    +------+            |
 * |   SYS:       C:         Shell      Prefs                 |  Desktop Icons
 * |                                                          |
 * |  +------+                                                |
 * |  | icon |                                                |
 * |  +------+                                                |
 * |   Help                                                   |
 * |                                                          |
 * +----------------------------------------------------------+
 * ```
 *
 * ## Icon Layout Calculations
 *
 * Icons are arranged in a grid pattern starting at (ICON_START_X, ICON_START_Y):
 * - Horizontal spacing: ICON_SPACING_X pixels between icon centers
 * - Vertical spacing: ICON_SPACING_Y pixels between icon centers
 * - Icon size: 24x24 pixels
 * - Label appears ICON_LABEL_OFFSET pixels below the icon top
 *
 * @see desktop.hpp for the Desktop class
 * @see filebrowser.hpp for file browser types
 */
//===----------------------------------------------------------------------===//

#include <stdint.h>

namespace workbench {

//===----------------------------------------------------------------------===//
// Desktop Layout Constants
//===----------------------------------------------------------------------===//

/**
 * @defgroup DesktopLayout Desktop Layout Constants
 * @brief Pixel dimensions for the Workbench desktop layout.
 * @{
 */

/** @brief Height of the menu bar at the top of the screen (pixels). */
constexpr int MENU_BAR_HEIGHT = 20;

/** @brief Size of desktop and file browser icons (width and height in pixels). */
constexpr int ICON_SIZE = 24;

/** @brief Horizontal spacing between icon centers on the desktop (pixels). */
constexpr int ICON_SPACING_X = 80;

/** @brief Vertical spacing between icon centers on the desktop (pixels). */
constexpr int ICON_SPACING_Y = 70;

/** @brief X coordinate where the first icon column begins (pixels from left). */
constexpr int ICON_START_X = 40;

/** @brief Y coordinate where the first icon row begins (pixels from top). */
constexpr int ICON_START_Y = 50;

/** @brief Y offset from icon top to the label text (pixels). */
constexpr int ICON_LABEL_OFFSET = 36;

/** @brief Maximum time between clicks to register as a double-click (milliseconds). */
constexpr int DOUBLE_CLICK_MS = 400;

/** @} */ // end DesktopLayout

//===----------------------------------------------------------------------===//
// File Browser Layout Constants
//===----------------------------------------------------------------------===//

/**
 * @defgroup FileBrowserLayout File Browser Layout Constants
 * @brief Pixel dimensions for file browser windows.
 * @{
 */

/** @brief Height of the toolbar at the top of file browser windows (pixels). */
constexpr int FB_TOOLBAR_HEIGHT = 24;

/** @brief Height of the status bar at the bottom of file browser windows (pixels). */
constexpr int FB_STATUSBAR_HEIGHT = 20;

/** @brief Horizontal spacing between file icons in the grid (pixels). */
constexpr int FB_ICON_GRID_X = 80;

/** @brief Vertical spacing between file icons in the grid (pixels). */
constexpr int FB_ICON_GRID_Y = 64;

/** @brief Padding around the file list area (pixels). */
constexpr int FB_PADDING = 8;

/** @} */ // end FileBrowserLayout

//===----------------------------------------------------------------------===//
// System Limits
//===----------------------------------------------------------------------===//

/**
 * @defgroup SystemLimits System Limits
 * @brief Maximum sizes for paths, filenames, and collections.
 * @{
 */

/** @brief Maximum length of a file path including null terminator. */
constexpr int MAX_PATH_LEN = 256;

/** @brief Maximum length of a filename including null terminator. */
constexpr int MAX_FILENAME_LEN = 64;

/** @brief Maximum number of files that can be displayed in one directory. */
constexpr int MAX_FILES_PER_DIR = 128;

/** @brief Maximum number of simultaneous file browser windows. */
constexpr int MAX_BROWSERS = 8;

/** @} */ // end SystemLimits

//===----------------------------------------------------------------------===//
// Context Menu Constants
//===----------------------------------------------------------------------===//

/**
 * @defgroup ContextMenuLayout Context Menu Layout
 * @brief Dimensions for context (right-click) menus.
 * @{
 */

/** @brief Height of each item in a context menu (pixels). */
constexpr int MENU_ITEM_HEIGHT = 20;

/** @brief Width of context menus (pixels). */
constexpr int MENU_WIDTH = 120;

/** @brief Maximum number of items in a context menu. */
constexpr int MAX_MENU_ITEMS = 8;

/** @} */ // end ContextMenuLayout

//===----------------------------------------------------------------------===//
// File Entry Types
//===----------------------------------------------------------------------===//

/**
 * @brief Classification of file types in the file browser.
 *
 * The file browser uses this enumeration to determine which icon to display
 * and what action to take on double-click. The type is determined from the
 * file extension.
 *
 * ## Extension Mappings
 *
 * | Type       | Extensions                      | Icon        |
 * |------------|---------------------------------|-------------|
 * | Directory  | (d_type == DT_DIR)              | folder_24   |
 * | Executable | .sys, .prg                      | file_exe_24 |
 * | Text       | .txt, .md, .c, .h, .cpp, .hpp   | file_text_24|
 * | Image      | .bmp                            | file_24     |
 * | Unknown    | (all other extensions)          | file_24     |
 */
enum class FileType {
    Directory,  /**< Directory (folder). */
    Executable, /**< Executable program (.sys, .prg). */
    Text,       /**< Text file (.txt, .md, .c, .h, .cpp, .hpp). */
    Image,      /**< Image file (.bmp). */
    Unknown     /**< Unknown file type. */
};

/**
 * @brief Represents a file or directory entry in the file browser.
 *
 * Each entry in a directory listing is stored as a FileEntry. The file
 * browser maintains an array of these entries for the current directory.
 *
 * ## Memory Layout
 *
 * The name buffer is fixed-size to avoid dynamic allocation. Files with
 * names longer than MAX_FILENAME_LEN-1 characters are truncated.
 */
struct FileEntry {
    char name[MAX_FILENAME_LEN]; /**< Filename (null-terminated, possibly truncated). */
    FileType type;               /**< Type classification for icon and actions. */
    uint64_t size;               /**< File size in bytes (0 for directories). */
    bool selected;               /**< Whether this entry is currently selected. */
};

//===----------------------------------------------------------------------===//
// Desktop Icon Types
//===----------------------------------------------------------------------===//

/**
 * @brief Actions that can be performed when double-clicking a desktop icon.
 *
 * Each desktop icon has an associated action that determines what happens
 * when the user double-clicks it.
 */
enum class IconAction {
    None,            /**< No action (disabled icon). */
    OpenFileBrowser, /**< Open a file browser window for the target path. */
    LaunchProgram,   /**< Spawn the target program as a new process. */
    ShowDialog       /**< Display a dialog window (e.g., About, Settings). */
};

/**
 * @brief Defines a desktop icon with its position, appearance, and behavior.
 *
 * Desktop icons are displayed on the Workbench background and provide
 * quick access to volumes, programs, and system functions. Icons can be
 * selected by clicking and activated by double-clicking.
 *
 * ## Visual Layout
 *
 * ```
 * +--------+
 * |  icon  |  <- 24x24 pixel icon (pixels field)
 * |  data  |
 * +--------+
 *   Label     <- Text label centered below icon
 * ```
 *
 * ## Selection Highlighting
 *
 * When selected, the icon displays an orange (or themed) background box
 * behind both the icon and label.
 */
struct DesktopIcon {
    int x, y;               /**< Position of icon top-left corner on desktop. */
    const char *label;      /**< Text label displayed below the icon. */
    const char *target;     /**< Path for file browser or program to launch. */
    const uint32_t *pixels; /**< Pointer to 24x24 ARGB pixel data. */
    IconAction action;      /**< Action to perform on double-click. */
    bool selected;          /**< Whether this icon is currently selected. */
};

//===----------------------------------------------------------------------===//
// Pulldown Menu Types
//===----------------------------------------------------------------------===//

/**
 * @brief Actions that can be triggered from the menu bar pulldown menus.
 *
 * The Workbench menu bar provides three pulldown menus: Workbench, Window,
 * and Tools. Each menu item has an associated action from this enumeration.
 *
 * ## Menu Structure
 *
 * | Menu      | Actions                                           |
 * |-----------|---------------------------------------------------|
 * | Workbench | About, Execute Command, Redraw, Quit              |
 * | Window    | New Drawer, Open Parent, Close Window, Clean Up   |
 * | Tools     | Shell, Prefs, SysInfo, TaskMan, Theme options     |
 */
enum class PulldownAction {
    None, /**< No action (placeholder). */
    // Workbench menu
    Backdrop,       /**< Toggle backdrop mode. */
    ExecuteCommand, /**< Open command execution dialog. */
    Redraw,         /**< Redraw all windows. */
    UpdateAll,      /**< Update all window contents. */
    LastMessage,    /**< Show last system message. */
    AboutWorkbench, /**< Show About ViperDOS dialog. */
    QuitWorkbench,  /**< Exit Workbench (no-op currently). */
    // Window menu
    NewDrawer,      /**< Create a new folder. */
    OpenParent,     /**< Navigate to parent directory. */
    CloseWindow,    /**< Close the active window. */
    Update,         /**< Refresh the current window. */
    SelectContents, /**< Select all files in window. */
    CleanUp,        /**< Rearrange icons in grid. */
    // Tools menu
    ResetWB, /**< Reset Workbench state. */
    Prefs,   /**< Open Preferences application. */
    Shell,   /**< Open a new shell window. */
    SysInfo, /**< Open System Information. */
    TaskMan, /**< Open Task Manager. */
    // Theme switching
    ThemeClassic,     /**< Switch to Classic Amiga theme. */
    ThemeDark,        /**< Switch to Dark Mode theme. */
    ThemeModern,      /**< Switch to Modern Blue theme. */
    ThemeHighContrast /**< Switch to High Contrast theme. */
};

/**
 * @brief Represents a single item in a pulldown menu.
 *
 * Menu items can display a label, an optional keyboard shortcut hint,
 * and can be enabled or disabled. A separator line can be drawn after
 * the item.
 *
 * ## Example Usage
 *
 * ```cpp
 * PulldownItem items[] = {
 *     {"About...", nullptr, PulldownAction::AboutWorkbench, false, true},
 *     {"Quit", "Ctrl+Q", PulldownAction::QuitWorkbench, false, true},
 * };
 * ```
 */
struct PulldownItem {
    const char *label;     /**< Display text for the menu item. */
    const char *shortcut;  /**< Optional shortcut hint (e.g., "Ctrl+Q"). */
    PulldownAction action; /**< Action to perform when selected. */
    bool separator;        /**< If true, draw separator line after this item. */
    bool enabled;          /**< If false, item is grayed out and not selectable. */
};

/**
 * @brief Defines a complete pulldown menu with title and items.
 *
 * Each pulldown menu appears in the menu bar and drops down when clicked.
 * The menu tracks its title position for hit-testing mouse clicks.
 *
 * ## Layout
 *
 * ```
 * +------------+------------+--------+
 * | Workbench  |  Window    | Tools  |   <- Menu bar
 * +------------+------------+--------+
 * | About...   |            |
 * | Execute... |            |
 * |------------|            |
 * | Quit       |            |   <- Dropdown menu
 * +------------+            |
 * ```
 */
struct PulldownMenu {
    const char *title;      /**< Menu name displayed in the menu bar. */
    int titleX;             /**< X position of title in menu bar (pixels). */
    int titleWidth;         /**< Width of clickable title area (pixels). */
    PulldownItem items[12]; /**< Array of menu items (max 12). */
    int itemCount;          /**< Number of valid items in the array. */
};

//===----------------------------------------------------------------------===//
// Context Menu Types
//===----------------------------------------------------------------------===//

/**
 * @brief Actions for context (right-click) menu items.
 *
 * Context menus provide file operations when right-clicking on files
 * or empty space in the file browser.
 *
 * ## Context-Dependent Items
 *
 * | Context      | Available Actions                    |
 * |--------------|--------------------------------------|
 * | On file      | Open, Copy, Rename, Delete, Properties |
 * | On empty     | New Folder, Paste                    |
 */
enum class MenuAction {
    None,      /**< No action. */
    Open,      /**< Open the file or directory. */
    Delete,    /**< Delete the selected file. */
    Rename,    /**< Start inline rename editor. */
    Copy,      /**< Copy file path to clipboard. */
    Paste,     /**< Paste file from clipboard. */
    NewFolder, /**< Create a new folder. */
    Properties /**< Show file properties dialog. */
};

/**
 * @brief Represents a single item in a context menu.
 *
 * Context menu items display in the dropdown when right-clicking.
 * Items can be enabled or disabled based on the current selection.
 */
struct MenuItem {
    const char *label; /**< Display text for the item. */
    MenuAction action; /**< Action to perform when clicked. */
    bool separator;    /**< If true, draw separator after this item. */
    bool enabled;      /**< If false, item is grayed out. */
};

/**
 * @brief Manages the state of a visible context menu.
 *
 * The context menu appears at the mouse position when right-clicking.
 * It tracks which item is hovered for visual highlighting and handles
 * click detection.
 *
 * ## State Machine
 *
 * 1. Right-click -> visible=true, menu populated
 * 2. Mouse move -> hoveredItem updated
 * 3. Left-click on item -> action executed, visible=false
 * 4. Click elsewhere -> visible=false
 */
struct ContextMenu {
    int x, y;                       /**< Screen position of menu top-left. */
    MenuItem items[MAX_MENU_ITEMS]; /**< Menu items to display. */
    int itemCount;                  /**< Number of items in the menu. */
    int hoveredItem;                /**< Index of hovered item (-1 if none). */
    bool visible;                   /**< Whether the menu is currently shown. */
};

//===----------------------------------------------------------------------===//
// Clipboard Types
//===----------------------------------------------------------------------===//

/**
 * @brief Type of file clipboard operation.
 *
 * Distinguishes between copy and cut operations. Cut operations delete
 * the source file after successful paste.
 */
enum class ClipboardOp {
    None, /**< No operation pending. */
    Copy, /**< Copy operation (source preserved). */
    Cut   /**< Cut operation (source deleted after paste). */
};

/**
 * @brief Simple file clipboard for copy/paste operations.
 *
 * The clipboard stores a single file path and the type of operation.
 * It is shared across all file browser windows.
 *
 * ## Copy/Paste Workflow
 *
 * 1. User right-clicks file -> Copy
 * 2. Clipboard stores path, operation=Copy, hasContent=true
 * 3. User navigates to destination
 * 4. User right-clicks empty area -> Paste
 * 5. File is copied to destination
 * 6. For Cut: source file is deleted, hasContent=false
 */
struct FileClipboard {
    char path[MAX_PATH_LEN]; /**< Full path of the copied/cut file. */
    ClipboardOp operation;   /**< Type of operation (Copy or Cut). */
    bool hasContent;         /**< True if clipboard contains a valid path. */
};

/**
 * @brief Global clipboard shared across all file browsers.
 *
 * Defined in filebrowser.cpp. Allows copy/paste between different
 * file browser windows.
 */
extern FileClipboard g_clipboard;

//===----------------------------------------------------------------------===//
// Inline Rename Editor
//===----------------------------------------------------------------------===//

/**
 * @brief State for the inline filename rename editor.
 *
 * When the user presses F2 or selects Rename from the context menu,
 * an inline text editor appears over the file's label. This structure
 * tracks the editing state.
 *
 * ## Editor Features
 *
 * - Cursor navigation (Left, Right, Home, End)
 * - Selection with Shift+Arrow keys
 * - Character insertion and deletion
 * - Enter to commit, Escape to cancel
 *
 * ## Visual Appearance
 *
 * ```
 * +------------------+
 * | icon             |
 * +--[filename|]-----+  <- White background, black border, cursor bar
 * ```
 */
struct RenameEditor {
    int fileIndex;                 /**< Index of file being renamed in the file list. */
    char buffer[MAX_FILENAME_LEN]; /**< Edit buffer with current filename text. */
    int cursorPos;                 /**< Cursor position in the buffer (0 = start). */
    int selStart;                  /**< Selection start position (-1 if no selection). */
    bool active;                   /**< True when rename editor is visible and active. */
};

} // namespace workbench
