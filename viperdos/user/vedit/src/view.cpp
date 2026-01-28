//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
/**
 * @file view.cpp
 * @brief View rendering implementation for VEdit text editor.
 *
 * This file implements the View class which handles all visual rendering
 * for the VEdit text editor. The View is responsible for drawing:
 * - Menu bar with pulldown menus
 * - Text editing area with optional line numbers
 * - Cursor (blinking vertical bar)
 * - Status bar showing filename and cursor position
 *
 * ## Rendering Pipeline
 *
 * The render() method orchestrates drawing in this order:
 * 1. Clear background
 * 2. Draw text area with line numbers (if enabled)
 * 3. Draw cursor at current position
 * 4. Draw status bar
 * 5. Draw menu bar (last, so menus appear on top)
 * 6. Draw active dropdown menu (if any)
 * 7. Present to screen
 *
 * ## Menu System
 *
 * The editor has three menus defined in g_menus[]:
 * - **File**: New, Open, Save, Save As, Quit
 * - **Edit**: Cut, Copy, Paste, Select All
 * - **View**: Toggle line numbers, word wrap
 *
 * Each menu item has:
 * - Label: Display text
 * - Shortcut: Keyboard shortcut hint (e.g., "Ctrl+S")
 * - Action: Single character command code
 *
 * ## Coordinate System
 *
 * ```
 * (0,0) +----------------------+ (WIN_WIDTH, 0)
 *       | Menu Bar (20px)      |
 *       +------+---------------+
 *       | Line | Text Area     |
 *       | Nums |               |
 *       | 40px |               |
 *       |      |               |
 *       +------+---------------+
 *       | Status Bar (20px)    |
 *       +----------------------+
 * ```
 *
 * ## Hit Testing
 *
 * The View provides methods to determine what UI element is at a
 * given coordinate:
 * - findMenuAt(): Returns menu index for a position in menu bar
 * - findMenuItemAt(): Returns item index within an open menu
 *
 * @see view.hpp for View class definition
 * @see editor.hpp for Editor class that provides data
 */
//===----------------------------------------------------------------------===//

#include "../include/view.hpp"
#include <stdio.h>
#include <string.h>

namespace vedit {

// Menu definitions
Menu g_menus[] = {
    {"File",
     {{"New", "Ctrl+N", 'N'},
      {"Open...", "Ctrl+O", 'O'},
      {"Save", "Ctrl+S", 'S'},
      {"Save As...", "", 'A'},
      {"-", "", 0},
      {"Quit", "Ctrl+Q", 'Q'}},
     6,
     0,
     0},
    {"Edit",
     {{"Cut", "Ctrl+X", 'X'},
      {"Copy", "Ctrl+C", 'C'},
      {"Paste", "Ctrl+V", 'V'},
      {"-", "", 0},
      {"Select All", "Ctrl+A", 'a'}},
     5,
     0,
     0},
    {"View", {{"Line Numbers", "", 'L'}, {"Word Wrap", "", 'W'}}, 2, 0, 0},
};
const int NUM_MENUS = sizeof(g_menus) / sizeof(g_menus[0]);

View::View(gui_window_t *win) : m_win(win), m_activeMenu(-1), m_hoveredMenuItem(-1) {}

int View::visibleLines() const {
    // Note: Menu bar is now drawn by displayd, so full window height is available
    return (dims::WIN_HEIGHT - dims::STATUSBAR_HEIGHT) / dims::LINE_HEIGHT;
}

int View::visibleCols(bool showLineNumbers) const {
    int width = dims::WIN_WIDTH - (showLineNumbers ? dims::LINE_NUMBER_WIDTH : 0);
    return width / dims::CHAR_WIDTH;
}

int View::textAreaX(bool showLineNumbers) const {
    return showLineNumbers ? dims::LINE_NUMBER_WIDTH : 0;
}

int View::textAreaY() const {
    // Note: Menu bar is now drawn by displayd (global menu bar, Amiga/Mac style)
    // So text area starts at y=0 in window coordinates
    return 0;
}

void View::render(const Editor &editor) {
    // Clear background
    gui_fill_rect(m_win, 0, 0, dims::WIN_WIDTH, dims::WIN_HEIGHT, colors::BACKGROUND);

    drawTextArea(editor);
    drawCursor(editor);
    drawStatusBar(editor);
    // Note: Menu bar is now drawn by displayd (global menu bar, Amiga/Mac style)
    // We no longer draw our own menu bar - menus are registered via gui_set_menu()

    gui_present(m_win);
}

void View::drawMenuBar(const Editor &editor) {
    (void)editor;

    // Background
    gui_fill_rect(m_win, 0, 0, dims::WIN_WIDTH, dims::MENUBAR_HEIGHT, colors::MENUBAR);
    gui_draw_hline(m_win, 0, dims::WIN_WIDTH - 1, dims::MENUBAR_HEIGHT - 1, colors::BORDER_DARK);

    // Menu labels
    int x = 10;
    for (int i = 0; i < NUM_MENUS; i++) {
        g_menus[i].x = x;
        int labelLen = static_cast<int>(strlen(g_menus[i].label));
        g_menus[i].width = labelLen * dims::CHAR_WIDTH + 16;

        if (i == m_activeMenu) {
            gui_fill_rect(m_win,
                          x - 4,
                          0,
                          g_menus[i].width,
                          dims::MENUBAR_HEIGHT - 1,
                          colors::MENU_HIGHLIGHT);
            gui_draw_text(m_win, x, 5, g_menus[i].label, colors::SELECTION_TEXT);
        } else {
            gui_draw_text(m_win, x, 5, g_menus[i].label, colors::TEXT);
        }

        x += g_menus[i].width;
    }
}

void View::drawMenu(int menuIdx) {
    if (menuIdx < 0 || menuIdx >= NUM_MENUS) {
        return;
    }

    Menu &menu = g_menus[menuIdx];

    // Calculate dimensions
    int maxWidth = 0;
    for (int i = 0; i < menu.itemCount; i++) {
        int itemWidth = static_cast<int>(strlen(menu.items[i].label)) * dims::CHAR_WIDTH;
        if (menu.items[i].shortcut[0]) {
            itemWidth += static_cast<int>(strlen(menu.items[i].shortcut)) * dims::CHAR_WIDTH + 40;
        }
        if (itemWidth > maxWidth) {
            maxWidth = itemWidth;
        }
    }

    int menuWidth = maxWidth + 20;
    int menuHeight = menu.itemCount * 20 + 4;
    int x = menu.x;
    int y = dims::MENUBAR_HEIGHT;

    // Background with border
    gui_fill_rect(m_win, x, y, menuWidth, menuHeight, colors::MENUBAR);
    gui_draw_hline(m_win, x, x + menuWidth - 1, y, colors::BORDER_LIGHT);
    gui_draw_vline(m_win, x, y, y + menuHeight - 1, colors::BORDER_LIGHT);
    gui_draw_hline(m_win, x, x + menuWidth - 1, y + menuHeight - 1, colors::BORDER_DARK);
    gui_draw_vline(m_win, x + menuWidth - 1, y, y + menuHeight - 1, colors::BORDER_DARK);

    // Items
    int itemY = y + 2;
    for (int i = 0; i < menu.itemCount; i++) {
        if (menu.items[i].label[0] == '-') {
            // Separator
            int sepY = itemY + 9;
            gui_draw_hline(m_win, x + 4, x + menuWidth - 5, sepY, colors::BORDER_DARK);
            gui_draw_hline(m_win, x + 4, x + menuWidth - 5, sepY + 1, colors::BORDER_LIGHT);
        } else {
            uint32_t textColor = colors::TEXT;
            if (i == m_hoveredMenuItem) {
                gui_fill_rect(m_win, x + 2, itemY, menuWidth - 4, 18, colors::MENU_HIGHLIGHT);
                textColor = colors::SELECTION_TEXT;
            }

            gui_draw_text(m_win, x + 8, itemY + 4, menu.items[i].label, textColor);

            if (menu.items[i].shortcut[0]) {
                int shortcutX =
                    x + menuWidth -
                    static_cast<int>(strlen(menu.items[i].shortcut)) * dims::CHAR_WIDTH - 10;
                gui_draw_text(m_win,
                              shortcutX,
                              itemY + 4,
                              menu.items[i].shortcut,
                              i == m_hoveredMenuItem ? textColor : colors::LINE_NUMBER);
            }
        }
        itemY += 20;
    }
}

void View::drawStatusBar(const Editor &editor) {
    int y = dims::WIN_HEIGHT - dims::STATUSBAR_HEIGHT;

    // Background
    gui_fill_rect(m_win, 0, y, dims::WIN_WIDTH, dims::STATUSBAR_HEIGHT, colors::STATUSBAR);
    gui_draw_hline(m_win, 0, dims::WIN_WIDTH - 1, y, colors::BORDER_DARK);

    // Filename
    char statusBuf[128];
    const char *fname = editor.buffer().filename();
    if (fname[0] == '\0') {
        fname = "untitled";
    }
    snprintf(statusBuf, sizeof(statusBuf), "%s%s", fname, editor.buffer().isModified() ? " *" : "");
    gui_draw_text(m_win, 10, y + 5, statusBuf, colors::TEXT);

    // Line/column
    snprintf(statusBuf,
             sizeof(statusBuf),
             "Ln %d, Col %d",
             editor.cursorLine() + 1,
             editor.cursorCol() + 1);
    int infoX = dims::WIN_WIDTH - static_cast<int>(strlen(statusBuf)) * dims::CHAR_WIDTH - 10;
    gui_draw_text(m_win, infoX, y + 5, statusBuf, colors::TEXT);
}

void View::drawTextArea(const Editor &editor) {
    bool showLineNumbers = editor.config().showLineNumbers;
    int textX = textAreaX(showLineNumbers);
    int textY = textAreaY();
    int textWidth = dims::WIN_WIDTH - textX;
    int textHeight = dims::WIN_HEIGHT - dims::STATUSBAR_HEIGHT;

    // Text background
    gui_fill_rect(m_win, textX, textY, textWidth, textHeight, colors::TEXT_AREA);

    // Line number gutter
    if (showLineNumbers) {
        gui_fill_rect(m_win, 0, textY, dims::LINE_NUMBER_WIDTH, textHeight, colors::GUTTER);
        gui_draw_vline(
            m_win, dims::LINE_NUMBER_WIDTH - 1, textY, textY + textHeight - 1, colors::BORDER_DARK);
    }

    int lines = visibleLines();
    int scrollY = editor.scrollY();
    int scrollX = editor.scrollX();

    for (int i = 0; i < lines && (scrollY + i) < editor.buffer().lineCount(); i++) {
        int lineIdx = scrollY + i;
        int y = textY + i * dims::LINE_HEIGHT;

        // Line number
        if (showLineNumbers) {
            char lineNumBuf[16];
            snprintf(lineNumBuf, sizeof(lineNumBuf), "%4d", lineIdx + 1);
            gui_draw_text(m_win, 4, y + 2, lineNumBuf, colors::LINE_NUMBER);
        }

        // Line text
        const char *lineText = editor.buffer().lineText(lineIdx);
        int lineLen = editor.buffer().lineLength(lineIdx);

        if (scrollX < lineLen) {
            const char *visibleText = lineText + scrollX;
            int visibleLen = lineLen - scrollX;
            int maxChars = textWidth / dims::CHAR_WIDTH;

            char displayBuf[512];
            int copyLen = visibleLen < maxChars ? visibleLen : maxChars;
            if (copyLen > static_cast<int>(sizeof(displayBuf)) - 1) {
                copyLen = sizeof(displayBuf) - 1;
            }
            memcpy(displayBuf, visibleText, copyLen);
            displayBuf[copyLen] = '\0';

            gui_draw_text(m_win, textX + 4, y + 2, displayBuf, colors::TEXT);
        }
    }
}

void View::drawCursor(const Editor &editor) {
    bool showLineNumbers = editor.config().showLineNumbers;
    int textX = textAreaX(showLineNumbers);
    int textY = textAreaY();

    int cursorLine = editor.cursorLine();
    int cursorCol = editor.cursorCol();
    int scrollY = editor.scrollY();
    int scrollX = editor.scrollX();

    int screenLine = cursorLine - scrollY;
    int screenCol = cursorCol - scrollX;

    if (screenLine >= 0 && screenLine < visibleLines() && screenCol >= 0) {
        int cursorX = textX + 4 + screenCol * dims::CHAR_WIDTH;
        int cursorY = textY + screenLine * dims::LINE_HEIGHT + 1;
        gui_draw_vline(m_win, cursorX, cursorY, cursorY + dims::LINE_HEIGHT - 2, colors::CURSOR);
    }
}

int View::findMenuAt(int x, int y) const {
    if (y >= dims::MENUBAR_HEIGHT) {
        return -1;
    }

    for (int i = 0; i < NUM_MENUS; i++) {
        if (x >= g_menus[i].x && x < g_menus[i].x + g_menus[i].width) {
            return i;
        }
    }
    return -1;
}

int View::findMenuItemAt(int menuIdx, int x, int y) const {
    (void)x;

    if (menuIdx < 0 || menuIdx >= NUM_MENUS) {
        return -1;
    }

    int itemY = dims::MENUBAR_HEIGHT + 2;
    for (int i = 0; i < g_menus[menuIdx].itemCount; i++) {
        if (y >= itemY && y < itemY + 20) {
            if (g_menus[menuIdx].items[i].label[0] != '-') {
                return i;
            }
        }
        itemY += 20;
    }
    return -1;
}

char View::getMenuAction(int menuIdx, int itemIdx) const {
    if (menuIdx < 0 || menuIdx >= NUM_MENUS) {
        return 0;
    }
    if (itemIdx < 0 || itemIdx >= g_menus[menuIdx].itemCount) {
        return 0;
    }
    return g_menus[menuIdx].items[itemIdx].action;
}

} // namespace vedit
