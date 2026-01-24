#pragma once
//===----------------------------------------------------------------------===//
// Editor view/rendering
//===----------------------------------------------------------------------===//

#include "editor.hpp"
#include <gui.h>

namespace vedit {

// Colors
namespace colors {
constexpr uint32_t BACKGROUND = 0xFFAAAAAA;
constexpr uint32_t TEXT_AREA = 0xFFFFFFFF;
constexpr uint32_t TEXT = 0xFF000000;
constexpr uint32_t GUTTER = 0xFFAAAAAA;
constexpr uint32_t LINE_NUMBER = 0xFF888888;
constexpr uint32_t CURSOR = 0xFF000000;
constexpr uint32_t SELECTION = 0xFF0055AA;
constexpr uint32_t SELECTION_TEXT = 0xFFFFFFFF;
constexpr uint32_t MENUBAR = 0xFFAAAAAA;
constexpr uint32_t MENU_HIGHLIGHT = 0xFF0055AA;
constexpr uint32_t STATUSBAR = 0xFFAAAAAA;
constexpr uint32_t BORDER_LIGHT = 0xFFFFFFFF;
constexpr uint32_t BORDER_DARK = 0xFF555555;
} // namespace colors

// Dimensions
namespace dims {
constexpr int WIN_WIDTH = 640;
constexpr int WIN_HEIGHT = 480;
constexpr int MENUBAR_HEIGHT = 20;
constexpr int STATUSBAR_HEIGHT = 20;
constexpr int LINE_NUMBER_WIDTH = 50;
constexpr int CHAR_WIDTH = 8;
constexpr int CHAR_HEIGHT = 12;
constexpr int LINE_HEIGHT = 14;
} // namespace dims

// Menu item
struct MenuItem {
    const char *label;
    const char *shortcut;
    char action;
};

// Menu
struct Menu {
    const char *label;
    MenuItem items[10];
    int itemCount;
    int x;
    int width;
};

// View class
class View {
  public:
    View(gui_window_t *win);

    // Rendering
    void render(const Editor &editor);

    // Menu handling
    int activeMenu() const { return m_activeMenu; }
    void setActiveMenu(int menu) { m_activeMenu = menu; }
    int hoveredMenuItem() const { return m_hoveredMenuItem; }
    void setHoveredMenuItem(int item) { m_hoveredMenuItem = item; }

    // Hit testing
    int findMenuAt(int x, int y) const;
    int findMenuItemAt(int menuIdx, int x, int y) const;
    char getMenuAction(int menuIdx, int itemIdx) const;

    // Calculated values
    int visibleLines() const;
    int visibleCols(bool showLineNumbers) const;
    int textAreaX(bool showLineNumbers) const;
    int textAreaY() const;

  private:
    void drawMenuBar(const Editor &editor);
    void drawMenu(int menuIdx);
    void drawStatusBar(const Editor &editor);
    void drawTextArea(const Editor &editor);
    void drawCursor(const Editor &editor);

    gui_window_t *m_win;
    int m_activeMenu;
    int m_hoveredMenuItem;
};

// Menu definitions (extern)
extern Menu g_menus[];
extern const int NUM_MENUS;

} // namespace vedit
