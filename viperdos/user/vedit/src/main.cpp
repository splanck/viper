//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
/**
 * @file main.cpp
 * @brief VEdit text editor entry point and event loop.
 *
 * Refactored using OOP principles with KeyMapper and VEditApp classes.
 */
//===----------------------------------------------------------------------===//

#include "../include/editor.hpp"
#include "../include/view.hpp"
#include <stdlib.h>
#include <widget.h>

using namespace vedit;

//===----------------------------------------------------------------------===//
// KeyMapper - Converts HID keycodes to ASCII characters
//===----------------------------------------------------------------------===//

class KeyMapper {
  public:
    // Navigation keycodes (evdev)
    static constexpr uint16_t KEY_LEFT = 105;
    static constexpr uint16_t KEY_RIGHT = 106;
    static constexpr uint16_t KEY_UP = 103;
    static constexpr uint16_t KEY_DOWN = 108;
    static constexpr uint16_t KEY_HOME = 102;
    static constexpr uint16_t KEY_END = 107;
    static constexpr uint16_t KEY_PAGEUP = 104;
    static constexpr uint16_t KEY_PAGEDOWN = 109;

    // Editing keycodes
    static constexpr uint16_t KEY_BACKSPACE = 14;
    static constexpr uint16_t KEY_DELETE = 111;
    static constexpr uint16_t KEY_ENTER = 28;
    static constexpr uint16_t KEY_TAB = 15;
    static constexpr uint16_t KEY_SPACE = 57;

    static bool isNavigation(uint16_t keycode) {
        return keycode == KEY_LEFT || keycode == KEY_RIGHT || keycode == KEY_UP ||
               keycode == KEY_DOWN || keycode == KEY_HOME || keycode == KEY_END ||
               keycode == KEY_PAGEUP || keycode == KEY_PAGEDOWN;
    }

    static bool isEditing(uint16_t keycode) {
        return keycode == KEY_BACKSPACE || keycode == KEY_DELETE || keycode == KEY_ENTER ||
               keycode == KEY_TAB;
    }

    static char toChar(uint16_t keycode, bool shift) {
        // Letters: QWERTY row (Q=16 to P=25)
        if (keycode >= 16 && keycode <= 25) {
            char ch = "qwertyuiop"[keycode - 16];
            return shift ? (ch - 'a' + 'A') : ch;
        }

        // Letters: ASDF row (A=30 to L=38)
        if (keycode >= 30 && keycode <= 38) {
            char ch = "asdfghjkl"[keycode - 30];
            return shift ? (ch - 'a' + 'A') : ch;
        }

        // Letters: ZXCV row (Z=44 to M=50)
        if (keycode >= 44 && keycode <= 50) {
            char ch = "zxcvbnm"[keycode - 44];
            return shift ? (ch - 'a' + 'A') : ch;
        }

        // Numbers 1-9 (keycodes 2-10)
        if (keycode >= 2 && keycode <= 10) {
            return shift ? "!@#$%^&*("[keycode - 2] : ('0' + keycode - 1);
        }

        // Number 0 (keycode 11)
        if (keycode == 11) {
            return shift ? ')' : '0';
        }

        // Space
        if (keycode == KEY_SPACE) {
            return ' ';
        }

        // Punctuation with shift variants
        switch (keycode) {
            case 12:
                return shift ? '_' : '-';
            case 13:
                return shift ? '+' : '=';
            case 26:
                return shift ? '{' : '[';
            case 27:
                return shift ? '}' : ']';
            case 39:
                return shift ? ':' : ';';
            case 40:
                return shift ? '"' : '\'';
            case 51:
                return shift ? '<' : ',';
            case 52:
                return shift ? '>' : '.';
            case 53:
                return shift ? '?' : '/';
            case 43:
                return shift ? '|' : '\\';
            case 41:
                return shift ? '~' : '`';
            default:
                return 0;
        }
    }
};

//===----------------------------------------------------------------------===//
// MenuHandler - Handles menu action dispatch
//===----------------------------------------------------------------------===//

class MenuHandler {
  public:
    static void handle(Editor &editor, View &view, gui_window_t *win, char action) {
        switch (action) {
            case 'N':
                editor.newFile();
                break;

            case 'O': {
                char *path = filedialog_open(win, "Open File", nullptr, "/");
                if (path) {
                    editor.loadFile(path);
                    free(path);
                }
                break;
            }

            case 'S':
                if (editor.buffer().filename()[0] == '\0') {
                    char *path = filedialog_save(win, "Save File", nullptr, "/");
                    if (path) {
                        editor.saveFileAs(path);
                        free(path);
                    }
                } else {
                    editor.saveFile();
                }
                break;

            case 'A': {
                char *path = filedialog_save(win, "Save File As", nullptr, "/");
                if (path) {
                    editor.saveFileAs(path);
                    free(path);
                }
                break;
            }

            case 'L':
                editor.config().showLineNumbers = !editor.config().showLineNumbers;
                break;

            case 'W':
                editor.config().wordWrap = !editor.config().wordWrap;
                break;

            case 'Q':
                // Quit handled in main loop
                break;
        }

        view.setActiveMenu(-1);
    }
};

//===----------------------------------------------------------------------===//
// MenuRegistrar - Registers menus with displayd
//===----------------------------------------------------------------------===//

class MenuRegistrar {
  public:
    static void registerMenus(gui_window_t *win) {
        gui_menu_def_t gui_menus[3];

        // Zero-initialize
        for (int m = 0; m < 3 && m < NUM_MENUS; m++) {
            clearMenu(gui_menus[m]);
        }

        // Convert menus to GUI format
        for (int m = 0; m < 3 && m < NUM_MENUS; m++) {
            copyTitle(gui_menus[m], g_menus[m].label);
            gui_menus[m].item_count = static_cast<uint8_t>(g_menus[m].itemCount);

            for (int j = 0; j < g_menus[m].itemCount && j < GUI_MAX_MENU_ITEMS; j++) {
                copyItem(gui_menus[m].items[j], g_menus[m].items[j]);
            }
        }

        gui_set_menu(win, gui_menus, static_cast<uint8_t>(NUM_MENUS));
    }

  private:
    static void clearMenu(gui_menu_def_t &menu) {
        for (int i = 0; i < 24; i++)
            menu.title[i] = '\0';
        menu.item_count = 0;
        menu._pad[0] = menu._pad[1] = menu._pad[2] = 0;

        for (int j = 0; j < GUI_MAX_MENU_ITEMS; j++) {
            for (int k = 0; k < 32; k++)
                menu.items[j].label[k] = '\0';
            for (int k = 0; k < 16; k++)
                menu.items[j].shortcut[k] = '\0';
            menu.items[j].action = 0;
            menu.items[j].enabled = 0;
            menu.items[j].checked = 0;
            menu.items[j]._pad = 0;
        }
    }

    static void copyTitle(gui_menu_def_t &menu, const char *src) {
        for (int i = 0; i < 23 && src[i]; i++) {
            menu.title[i] = src[i];
        }
    }

    static void copyItem(gui_menu_item_t &dest, const MenuItem &src) {
        if (src.label) {
            for (int k = 0; k < 31 && src.label[k]; k++) {
                dest.label[k] = src.label[k];
            }
        }

        if (src.shortcut) {
            for (int k = 0; k < 15 && src.shortcut[k]; k++) {
                dest.shortcut[k] = src.shortcut[k];
            }
        }

        dest.action = static_cast<uint8_t>(src.action);
        dest.enabled = (src.label[0] != '-') ? 1 : 0;
        dest.checked = 0;
    }
};

//===----------------------------------------------------------------------===//
// VEditApp - Main application class
//===----------------------------------------------------------------------===//

class VEditApp {
  public:
    VEditApp() : m_window(nullptr), m_running(false) {}

    bool init(int argc, char **argv) {
        if (gui_init() != 0) {
            return false;
        }

        m_window = gui_create_window("VEdit", dims::WIN_WIDTH, dims::WIN_HEIGHT);
        if (!m_window) {
            gui_shutdown();
            return false;
        }

        m_view = new View(m_window);
        MenuRegistrar::registerMenus(m_window);

        if (argc > 1) {
            m_editor.loadFile(argv[1]);
        }

        m_view->render(m_editor);
        return true;
    }

    void run() {
        m_running = true;

        while (m_running) {
            gui_event_t event;
            if (gui_poll_event(m_window, &event) == 0) {
                bool needsRedraw = processEvent(event);
                if (needsRedraw) {
                    m_view->render(m_editor);
                }
            }

            // Yield CPU
            __asm__ volatile("mov x8, #0x00\n\tsvc #0" ::: "x8");
        }
    }

    void shutdown() {
        delete m_view;
        gui_destroy_window(m_window);
        gui_shutdown();
    }

  private:
    gui_window_t *m_window;
    Editor m_editor;
    View *m_view;
    bool m_running;

    bool processEvent(const gui_event_t &event) {
        switch (event.type) {
            case GUI_EVENT_CLOSE:
                m_running = false;
                return false;

            case GUI_EVENT_MENU:
                return handleMenuEvent(event);

            case GUI_EVENT_MOUSE:
                return handleMouseEvent(event);

            case GUI_EVENT_KEY:
                return handleKeyEvent(event);

            default:
                return false;
        }
    }

    bool handleMenuEvent(const gui_event_t &event) {
        char action = static_cast<char>(event.menu.action);
        if (action == 'Q') {
            m_running = false;
        } else if (action != 0) {
            MenuHandler::handle(m_editor, *m_view, m_window, action);
        }
        return true;
    }

    bool handleMouseEvent(const gui_event_t &event) {
        if (event.mouse.event_type == 1 && event.mouse.button == 0) {
            if (event.mouse.y > m_view->textAreaY() &&
                event.mouse.y < dims::WIN_HEIGHT - dims::STATUSBAR_HEIGHT) {
                int relX = event.mouse.x;
                int relY = event.mouse.y - m_view->textAreaY();
                m_editor.setCursorFromClick(relX,
                                            relY,
                                            m_view->textAreaX(m_editor.config().showLineNumbers),
                                            m_view->visibleLines());
            }
            return true;
        }
        return false;
    }

    bool handleKeyEvent(const gui_event_t &event) {
        if (!event.key.pressed) {
            return false;
        }

        uint16_t kc = event.key.keycode;
        bool shift = (event.key.modifiers & 1) != 0;
        bool handled = true;

        // Navigation keys
        if (KeyMapper::isNavigation(kc)) {
            handleNavigation(kc);
        }
        // Editing keys
        else if (KeyMapper::isEditing(kc)) {
            handleEditing(kc);
        }
        // Character input
        else {
            char ch = KeyMapper::toChar(kc, shift);
            if (ch) {
                m_editor.insertChar(ch);
            } else {
                handled = false;
            }
        }

        if (handled) {
            m_editor.ensureCursorVisible(m_view->visibleLines(),
                                         m_view->visibleCols(m_editor.config().showLineNumbers));
        }

        return handled;
    }

    void handleNavigation(uint16_t keycode) {
        switch (keycode) {
            case KeyMapper::KEY_LEFT:
                m_editor.moveCursorLeft();
                break;
            case KeyMapper::KEY_RIGHT:
                m_editor.moveCursorRight();
                break;
            case KeyMapper::KEY_UP:
                m_editor.moveCursorUp();
                break;
            case KeyMapper::KEY_DOWN:
                m_editor.moveCursorDown();
                break;
            case KeyMapper::KEY_HOME:
                m_editor.moveCursorHome();
                break;
            case KeyMapper::KEY_END:
                m_editor.moveCursorEnd();
                break;
            case KeyMapper::KEY_PAGEUP:
                m_editor.moveCursorPageUp(m_view->visibleLines());
                break;
            case KeyMapper::KEY_PAGEDOWN:
                m_editor.moveCursorPageDown(m_view->visibleLines());
                break;
        }
    }

    void handleEditing(uint16_t keycode) {
        switch (keycode) {
            case KeyMapper::KEY_BACKSPACE:
                m_editor.backspace();
                break;
            case KeyMapper::KEY_DELETE:
                m_editor.deleteChar();
                break;
            case KeyMapper::KEY_ENTER:
                m_editor.insertNewline();
                break;
            case KeyMapper::KEY_TAB:
                m_editor.insertTab();
                break;
        }
    }
};

//===----------------------------------------------------------------------===//
// Main Entry Point
//===----------------------------------------------------------------------===//

extern "C" int main(int argc, char **argv) {
    VEditApp app;

    if (!app.init(argc, argv)) {
        return 1;
    }

    app.run();
    app.shutdown();
    return 0;
}
