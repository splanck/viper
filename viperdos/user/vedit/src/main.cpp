//===----------------------------------------------------------------------===//
// VEdit - Entry point
//===----------------------------------------------------------------------===//

#include "../include/editor.hpp"
#include "../include/view.hpp"

using namespace vedit;

static void handleMenuAction(Editor &editor, View &view, char action) {
    switch (action) {
    case 'N': // New
        editor.newFile();
        break;

    case 'S': // Save
        editor.saveFile();
        break;

    case 'L': // Toggle line numbers
        editor.config().showLineNumbers = !editor.config().showLineNumbers;
        break;

    case 'W': // Toggle word wrap
        editor.config().wordWrap = !editor.config().wordWrap;
        break;

    case 'Q': // Quit - handled in main loop
        break;
    }

    view.setActiveMenu(-1);
}

extern "C" int main(int argc, char **argv) {
    if (gui_init() != 0) {
        return 1;
    }

    gui_window_t *win = gui_create_window("VEdit", dims::WIN_WIDTH, dims::WIN_HEIGHT);
    if (!win) {
        gui_shutdown();
        return 1;
    }

    Editor editor;
    View view(win);

    // Load file from command line
    if (argc > 1) {
        editor.loadFile(argv[1]);
    }

    view.render(editor);

    bool running = true;
    while (running) {
        gui_event_t event;
        if (gui_poll_event(win, &event) == 0) {
            bool needsRedraw = false;

            switch (event.type) {
            case GUI_EVENT_CLOSE:
                running = false;
                break;

            case GUI_EVENT_MOUSE:
                if (event.mouse.event_type == 1 && event.mouse.button == 0) {
                    int clickMenu = view.findMenuAt(event.mouse.x, event.mouse.y);

                    if (view.activeMenu() >= 0) {
                        // Menu is open
                        int itemIdx =
                            view.findMenuItemAt(view.activeMenu(), event.mouse.x, event.mouse.y);
                        if (itemIdx >= 0) {
                            char action = view.getMenuAction(view.activeMenu(), itemIdx);
                            if (action == 'Q') {
                                running = false;
                            } else {
                                handleMenuAction(editor, view, action);
                            }
                        } else if (clickMenu >= 0 && clickMenu != view.activeMenu()) {
                            view.setActiveMenu(clickMenu);
                            view.setHoveredMenuItem(-1);
                        } else {
                            view.setActiveMenu(-1);
                        }
                    } else if (clickMenu >= 0) {
                        view.setActiveMenu(clickMenu);
                        view.setHoveredMenuItem(-1);
                    } else if (event.mouse.y > view.textAreaY() &&
                               event.mouse.y < dims::WIN_HEIGHT - dims::STATUSBAR_HEIGHT) {
                        // Click in text area
                        int relX = event.mouse.x;
                        int relY = event.mouse.y - view.textAreaY();
                        editor.setCursorFromClick(relX, relY,
                                                  view.textAreaX(editor.config().showLineNumbers),
                                                  view.visibleLines());
                    }
                    needsRedraw = true;
                } else if (event.mouse.event_type == 0) {
                    // Mouse move
                    if (view.activeMenu() >= 0) {
                        view.setHoveredMenuItem(
                            view.findMenuItemAt(view.activeMenu(), event.mouse.x, event.mouse.y));
                        needsRedraw = true;
                    }
                }
                break;

            case GUI_EVENT_KEY:
                if (view.activeMenu() >= 0) {
                    // Close menu
                    view.setActiveMenu(-1);
                    needsRedraw = true;
                } else {
                    // Handle key input
                    bool handled = true;

                    switch (event.key.keycode) {
                    case 0x50: // Left
                        editor.moveCursorLeft();
                        break;
                    case 0x4F: // Right
                        editor.moveCursorRight();
                        break;
                    case 0x52: // Up
                        editor.moveCursorUp();
                        break;
                    case 0x51: // Down
                        editor.moveCursorDown();
                        break;
                    case 0x4A: // Home
                        editor.moveCursorHome();
                        break;
                    case 0x4D: // End
                        editor.moveCursorEnd();
                        break;
                    case 0x4B: // Page Up
                        editor.moveCursorPageUp(view.visibleLines());
                        break;
                    case 0x4E: // Page Down
                        editor.moveCursorPageDown(view.visibleLines());
                        break;
                    case 0x2A: // Backspace
                        editor.backspace();
                        break;
                    case 0x4C: // Delete
                        editor.deleteChar();
                        break;
                    case 0x28: // Enter
                        editor.insertNewline();
                        break;
                    case 0x2B: // Tab
                        editor.insertTab();
                        break;
                    default: {
                        // Convert keycode to character
                        char ch = 0;
                        uint16_t kc = event.key.keycode;
                        bool shift = (event.key.modifiers & 1) != 0;

                        // Letters (evdev: Q=16..P=25, A=30..L=38, Z=44..M=50)
                        if (kc >= 16 && kc <= 25) {
                            ch = "qwertyuiop"[kc - 16];
                        } else if (kc >= 30 && kc <= 38) {
                            ch = "asdfghjkl"[kc - 30];
                        } else if (kc >= 44 && kc <= 50) {
                            ch = "zxcvbnm"[kc - 44];
                        } else if (kc >= 2 && kc <= 10) {
                            // Numbers 1-9
                            ch = shift ? "!@#$%^&*("[kc - 2] : '0' + kc - 1;
                        } else if (kc == 11) {
                            ch = shift ? ')' : '0';
                        } else if (kc == 57) {
                            ch = ' '; // Space
                        } else if (kc == 12) {
                            ch = shift ? '_' : '-';
                        } else if (kc == 13) {
                            ch = shift ? '+' : '=';
                        } else if (kc == 26) {
                            ch = shift ? '{' : '[';
                        } else if (kc == 27) {
                            ch = shift ? '}' : ']';
                        } else if (kc == 39) {
                            ch = shift ? ':' : ';';
                        } else if (kc == 40) {
                            ch = shift ? '"' : '\'';
                        } else if (kc == 51) {
                            ch = shift ? '<' : ',';
                        } else if (kc == 52) {
                            ch = shift ? '>' : '.';
                        } else if (kc == 53) {
                            ch = shift ? '?' : '/';
                        } else if (kc == 43) {
                            ch = shift ? '|' : '\\';
                        } else if (kc == 41) {
                            ch = shift ? '~' : '`';
                        }

                        if (ch) {
                            if (shift && ch >= 'a' && ch <= 'z') {
                                ch = ch - 'a' + 'A';
                            }
                            editor.insertChar(ch);
                        } else {
                            handled = false;
                        }
                        break;
                    }
                    }

                    if (handled) {
                        editor.ensureCursorVisible(view.visibleLines(),
                                                   view.visibleCols(editor.config().showLineNumbers));
                        needsRedraw = true;
                    }
                }
                break;

            default:
                break;
            }

            if (needsRedraw) {
                view.render(editor);
            }
        }

        // Yield CPU
        __asm__ volatile("mov x8, #0x0E\n\tsvc #0" ::: "x8");
    }

    gui_destroy_window(win);
    gui_shutdown();
    return 0;
}
