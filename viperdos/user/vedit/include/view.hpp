#pragma once
//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
/**
 * @file view.hpp
 * @brief Editor view/rendering for VEdit text editor.
 *
 * This file defines the View class and related types for rendering the VEdit
 * user interface. The View is responsible for all visual output, including
 * the menu bar, text area, cursor, and status bar.
 *
 * ## Visual Layout
 *
 * ```
 * +------------------------------------------+
 * | File  Edit  View                         |  Menu Bar (20px)
 * +------------------------------------------+
 * | 1 | Hello, World!                        |  Text Area
 * | 2 | This is VEdit.                       |  (with optional
 * | 3 |                                      |   line numbers)
 * | 4 |                                      |
 * |   |_                                     |  Cursor
 * +------------------------------------------+
 * | untitled                    Ln 4, Col 1  |  Status Bar (20px)
 * +------------------------------------------+
 * ```
 *
 * ## Components
 *
 * - **Menu Bar**: File/Edit/View menus with dropdown support
 * - **Line Number Gutter**: Optional column showing line numbers
 * - **Text Area**: Main editing region with scrollable content
 * - **Cursor**: Blinking vertical line at insertion point
 * - **Status Bar**: Filename, modification indicator, cursor position
 *
 * ## Menu System
 *
 * The menu system provides familiar desktop-style menus:
 * - Click on menu name to open/close dropdown
 * - Hover over items for highlighting
 * - Click item to trigger action
 * - Press any key to close menu
 *
 * Menu actions are returned as single-character codes that the main
 * loop dispatches to appropriate handlers.
 *
 * @see editor.hpp for editing state
 * @see buffer.hpp for text storage
 */
//===----------------------------------------------------------------------===//

#include "editor.hpp"
#include <gui.h>

namespace vedit {

//===----------------------------------------------------------------------===//
// Color Constants
//===----------------------------------------------------------------------===//

/**
 * @defgroup VEditColors VEdit Color Palette
 * @brief Color constants for VEdit UI elements.
 *
 * Colors are in ARGB format (0xAARRGGBB). The palette uses the standard
 * ViperDOS Workbench gray scheme with blue highlights for selections
 * and active menu items.
 * @{
 */
namespace colors {

/** @brief Window and inactive area background (Workbench gray). */
constexpr uint32_t BACKGROUND = 0xFFAAAAAA;

/** @brief Text editing area background (Amiga blue). */
constexpr uint32_t TEXT_AREA = 0xFF0055AA;

/** @brief Normal text color in text area (white). */
constexpr uint32_t TEXT = 0xFFFFFFFF;

/** @brief Line number gutter background (darker blue). */
constexpr uint32_t GUTTER = 0xFF003366;

/** @brief Line number text color (light blue). */
constexpr uint32_t LINE_NUMBER = 0xFF88AACC;

/** @brief Text cursor color (white vertical line). */
constexpr uint32_t CURSOR = 0xFFFFFFFF;

/** @brief Selected text background color (blue). */
constexpr uint32_t SELECTION = 0xFF0055AA;

/** @brief Text color within selection (white for contrast). */
constexpr uint32_t SELECTION_TEXT = 0xFFFFFFFF;

/** @brief Menu bar background color (Workbench gray). */
constexpr uint32_t MENUBAR = 0xFFAAAAAA;

/** @brief Active menu item/hovered item background (blue). */
constexpr uint32_t MENU_HIGHLIGHT = 0xFF0055AA;

/** @brief Status bar background color (Workbench gray). */
constexpr uint32_t STATUSBAR = 0xFFAAAAAA;

/** @brief 3D border highlight color (light edge). */
constexpr uint32_t BORDER_LIGHT = 0xFFFFFFFF;

/** @brief 3D border shadow color (dark edge). */
constexpr uint32_t BORDER_DARK = 0xFF555555;

} // namespace colors
/** @} */ // end VEditColors

//===----------------------------------------------------------------------===//
// Dimension Constants
//===----------------------------------------------------------------------===//

/**
 * @defgroup VEditDimensions VEdit Layout Dimensions
 * @brief Size constants for VEdit UI layout.
 *
 * All dimensions are in pixels. The editor uses a fixed-width font
 * (8 pixels wide) and a line height slightly larger than the character
 * height for comfortable reading.
 * @{
 */
namespace dims {

/** @brief Total window width in pixels. */
constexpr int WIN_WIDTH = 640;

/** @brief Total window height in pixels. */
constexpr int WIN_HEIGHT = 480;

/** @brief Height of the menu bar in pixels. */
constexpr int MENUBAR_HEIGHT = 20;

/** @brief Height of the status bar in pixels. */
constexpr int STATUSBAR_HEIGHT = 20;

/** @brief Width of the line number gutter when enabled. */
constexpr int LINE_NUMBER_WIDTH = 50;

/** @brief Width of a single character in pixels (fixed-width font). */
constexpr int CHAR_WIDTH = 8;

/** @brief Height of a single character glyph in pixels. */
constexpr int CHAR_HEIGHT = 12;

/** @brief Height of a text line including spacing. */
constexpr int LINE_HEIGHT = 14;

} // namespace dims
/** @} */ // end VEditDimensions

//===----------------------------------------------------------------------===//
// Menu Types
//===----------------------------------------------------------------------===//

/**
 * @brief Represents a single item in a dropdown menu.
 *
 * Each menu item has a label, optional keyboard shortcut display text,
 * and an action character that identifies the action to perform when
 * the item is selected.
 *
 * ## Special Items
 *
 * - **Separator**: Use "-" as the label to create a horizontal line
 * - **Disabled**: Set action to 0 for items that cannot be selected
 */
struct MenuItem {
    const char *label;    /**< Display text for the menu item. */
    const char *shortcut; /**< Keyboard shortcut display (e.g., "Ctrl+S"). */
    char action;          /**< Action code returned when selected. */
};

/**
 * @brief Represents a top-level menu in the menu bar.
 *
 * Each menu has a label displayed in the menu bar, an array of items
 * shown in the dropdown, and layout information computed at runtime.
 */
struct Menu {
    const char *label;    /**< Menu name displayed in menu bar. */
    MenuItem items[10];   /**< Array of menu items (max 10). */
    int itemCount;        /**< Number of valid items in array. */
    int x;                /**< Computed X position of menu label. */
    int width;            /**< Computed width of menu label area. */
};

//===----------------------------------------------------------------------===//
// View Class
//===----------------------------------------------------------------------===//

/**
 * @brief Manages the visual display of the text editor.
 *
 * The View class handles all rendering for VEdit, translating the Editor
 * state into pixels on screen. It also provides hit-testing for mouse
 * interaction with menus and text.
 *
 * ## Usage
 *
 * @code
 * gui_window_t *win = gui_create_window("VEdit", 640, 480);
 * View view(win);
 * Editor editor;
 *
 * // Render the editor
 * view.render(editor);
 *
 * // Handle menu click
 * int menu = view.findMenuAt(mouseX, mouseY);
 * if (menu >= 0) {
 *     view.setActiveMenu(menu);
 *     view.render(editor);
 * }
 * @endcode
 *
 * ## Rendering Pipeline
 *
 * 1. Clear background
 * 2. Draw text area with visible lines
 * 3. Draw cursor if visible
 * 4. Draw status bar with file info
 * 5. Draw menu bar
 * 6. Draw open menu dropdown (if any)
 * 7. Present to display
 */
class View {
  public:
    /**
     * @brief Constructs a View for the given window.
     *
     * @param win GUI window to render to.
     */
    View(gui_window_t *win);

    //=== Rendering ===//

    /**
     * @brief Renders the complete editor interface.
     *
     * Draws all UI elements based on the current editor state and
     * presents the result to the display.
     *
     * @param editor The Editor state to render.
     */
    void render(const Editor &editor);

    //=== Menu State ===//

    /**
     * @brief Returns the index of the currently open menu.
     *
     * @return Menu index (0-based), or -1 if no menu is open.
     */
    int activeMenu() const { return m_activeMenu; }

    /**
     * @brief Sets the active (open) menu.
     *
     * @param menu Menu index to open, or -1 to close all menus.
     */
    void setActiveMenu(int menu) { m_activeMenu = menu; }

    /**
     * @brief Returns the index of the hovered menu item.
     *
     * @return Item index within the active menu, or -1 if none.
     */
    int hoveredMenuItem() const { return m_hoveredMenuItem; }

    /**
     * @brief Sets the hovered menu item for highlighting.
     *
     * @param item Item index within the active menu, or -1 for none.
     */
    void setHoveredMenuItem(int item) { m_hoveredMenuItem = item; }

    //=== Hit Testing ===//

    /**
     * @brief Finds which menu label is at a screen position.
     *
     * @param x X coordinate in window pixels.
     * @param y Y coordinate in window pixels.
     * @return Menu index if clicking a menu label, -1 otherwise.
     */
    int findMenuAt(int x, int y) const;

    /**
     * @brief Finds which menu item is at a screen position.
     *
     * Only valid when a menu is open. Tests the dropdown area of
     * the specified menu.
     *
     * @param menuIdx Index of the open menu.
     * @param x       X coordinate in window pixels.
     * @param y       Y coordinate in window pixels.
     * @return Item index if over a selectable item, -1 otherwise.
     */
    int findMenuItemAt(int menuIdx, int x, int y) const;

    /**
     * @brief Gets the action code for a menu item.
     *
     * @param menuIdx Index of the menu.
     * @param itemIdx Index of the item within the menu.
     * @return Action character, or 0 if invalid indices.
     */
    char getMenuAction(int menuIdx, int itemIdx) const;

    //=== Layout Calculations ===//

    /**
     * @brief Calculates how many text lines fit in the text area.
     *
     * @return Number of complete lines that fit vertically.
     */
    int visibleLines() const;

    /**
     * @brief Calculates how many characters fit horizontally.
     *
     * @param showLineNumbers Whether line numbers are displayed.
     * @return Number of characters that fit in one line.
     */
    int visibleCols(bool showLineNumbers) const;

    /**
     * @brief Returns the X coordinate where the text area begins.
     *
     * @param showLineNumbers Whether line numbers are displayed.
     * @return X offset of text area left edge.
     */
    int textAreaX(bool showLineNumbers) const;

    /**
     * @brief Returns the Y coordinate where the text area begins.
     *
     * @return Y offset of text area top edge (below menu bar).
     */
    int textAreaY() const;

  private:
    /**
     * @brief Draws the menu bar at the top of the window.
     *
     * Renders menu labels with highlighting for the active menu.
     *
     * @param editor The Editor state (unused, for interface consistency).
     */
    void drawMenuBar(const Editor &editor);

    /**
     * @brief Draws a dropdown menu.
     *
     * Renders the dropdown box with items, showing hover highlighting.
     *
     * @param menuIdx Index of the menu to draw.
     */
    void drawMenu(int menuIdx);

    /**
     * @brief Draws the status bar at the bottom of the window.
     *
     * Shows filename, modification indicator, and cursor position.
     *
     * @param editor The Editor state for status information.
     */
    void drawStatusBar(const Editor &editor);

    /**
     * @brief Draws the main text editing area.
     *
     * Renders line numbers (if enabled) and visible text lines based
     * on the current scroll position.
     *
     * @param editor The Editor state with buffer and scroll info.
     */
    void drawTextArea(const Editor &editor);

    /**
     * @brief Draws the text cursor.
     *
     * Renders a vertical line at the current cursor position if it's
     * within the visible area.
     *
     * @param editor The Editor state with cursor position.
     */
    void drawCursor(const Editor &editor);

    gui_window_t *m_win;   /**< Window to render to. */
    int m_activeMenu;      /**< Currently open menu (-1 if none). */
    int m_hoveredMenuItem; /**< Hovered item in open menu (-1 if none). */
};

//===----------------------------------------------------------------------===//
// Global Menu Definitions
//===----------------------------------------------------------------------===//

/**
 * @brief Array of menu definitions.
 *
 * Defines the File, Edit, and View menus with their items and actions.
 * This array is populated in view.cpp.
 */
extern Menu g_menus[];

/**
 * @brief Number of menus in g_menus array.
 */
extern const int NUM_MENUS;

} // namespace vedit
