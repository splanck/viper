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
 * This file contains the main function for VEdit, the ViperDOS text editor.
 * VEdit provides a graphical text editing environment with menus, keyboard
 * input, and mouse interaction.
 *
 * ## Application Structure
 *
 * VEdit is organized into layers:
 * - **main.cpp** (this file): Event loop and input dispatch
 * - **buffer.hpp/cpp**: Text storage and low-level editing
 * - **editor.hpp/cpp**: Cursor, scroll, and high-level operations
 * - **view.hpp/cpp**: UI rendering and menu system
 *
 * ## Event Loop
 *
 * The main loop handles three types of events:
 * 1. **Close events**: Terminate the application
 * 2. **Mouse events**: Menu interaction and text area clicks
 * 3. **Keyboard events**: Text input and navigation
 *
 * ## Menu System
 *
 * Menus are handled through a state machine:
 * 1. Click on menu bar opens dropdown (sets activeMenu)
 * 2. Mouse move updates highlighted item
 * 3. Click on item triggers action
 * 4. Any key press closes menu
 *
 * ## Keyboard Input
 *
 * The editor translates HID keycodes to characters or actions:
 * - Navigation: Arrow keys, Home, End, Page Up/Down
 * - Editing: Backspace, Delete, Enter, Tab
 * - Characters: Letters, numbers, punctuation (with shift support)
 *
 * ## Keycode Mapping
 *
 * The keyboard uses evdev keycodes. Character mappings:
 * - Letters: Q=16..P=25, A=30..L=38, Z=44..M=50
 * - Numbers: 1=2..0=11
 * - Space: 57
 * - Various punctuation: See switch statement in handleKeyPress
 *
 * @see editor.hpp for editing operations
 * @see view.hpp for rendering
 */
//===----------------------------------------------------------------------===//

#include "../include/editor.hpp"
#include "../include/view.hpp"
#include <stdlib.h>
#include <widget.h>

using namespace vedit;

//===----------------------------------------------------------------------===//
// Menu Action Handling
//===----------------------------------------------------------------------===//

/**
 * @brief Handles a menu action by dispatching to the appropriate operation.
 *
 * Called when the user selects a menu item. Maps single-character action
 * codes to editor operations and closes the menu after execution.
 *
 * ## Action Codes
 *
 * | Code | Menu Item      | Operation               |
 * |------|----------------|-------------------------|
 * | 'N'  | File > New     | Clear buffer, new file  |
 * | 'S'  | File > Save    | Save to current file    |
 * | 'L'  | View > Lines   | Toggle line numbers     |
 * | 'W'  | View > Wrap    | Toggle word wrap        |
 * | 'Q'  | File > Quit    | (Handled in main loop)  |
 *
 * @param editor Reference to the Editor for state changes.
 * @param view   Reference to the View to close menu.
 * @param action Single-character action code from menu item.
 *
 * @note The 'Q' (Quit) action is not handled here; it's detected in the
 *       main loop to break out of the event loop.
 */
static void handleMenuAction(Editor &editor, View &view, gui_window_t *win, char action) {
    switch (action) {
    case 'N': // New
        editor.newFile();
        break;

    case 'O': { // Open
        char *path = filedialog_open(win, "Open File", nullptr, "/");
        if (path) {
            editor.loadFile(path);
            free(path);
        }
        break;
    }

    case 'S': // Save
        if (editor.buffer().filename()[0] == '\0') {
            // No filename set - use Save As
            char *path = filedialog_save(win, "Save File", nullptr, "/");
            if (path) {
                editor.saveFileAs(path);
                free(path);
            }
        } else {
            editor.saveFile();
        }
        break;

    case 'A': { // Save As
        char *path = filedialog_save(win, "Save File As", nullptr, "/");
        if (path) {
            editor.saveFileAs(path);
            free(path);
        }
        break;
    }

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

//===----------------------------------------------------------------------===//
// Main Entry Point
//===----------------------------------------------------------------------===//

/**
 * @brief Application entry point for VEdit.
 *
 * Initializes the GUI, creates the editor window, and runs the main event
 * loop. Optionally loads a file specified on the command line.
 *
 * ## Initialization
 *
 * 1. Initialize GUI library (connect to displayd)
 * 2. Create window with fixed dimensions (640x480)
 * 3. Create Editor and View instances
 * 4. Load file from command line if provided
 * 5. Render initial display
 *
 * ## Event Processing
 *
 * Each iteration of the main loop:
 * 1. Poll for GUI events (non-blocking)
 * 2. Dispatch to appropriate handler based on event type
 * 3. Update cursor visibility after changes
 * 4. Re-render if any state changed
 * 5. Yield CPU to prevent busy-waiting
 *
 * ## Keyboard Handling
 *
 * The keyboard handler implements a full character conversion layer,
 * translating HID keycodes with shift modifier to ASCII characters.
 * This includes:
 * - Lowercase/uppercase letters
 * - Numbers and shifted symbols
 * - Punctuation with shift variants
 *
 * ## Exit Conditions
 *
 * The application exits when:
 * - User clicks the close button (GUI_EVENT_CLOSE)
 * - User selects File > Quit menu item
 *
 * @param argc Number of command-line arguments.
 * @param argv Array of argument strings. argv[1] is the file to open.
 * @return 0 on normal exit, 1 on initialization failure.
 *
 * @code
 * // Command line usage:
 * vedit                  // Opens with empty untitled buffer
 * vedit /path/to/file    // Opens and loads specified file
 * @endcode
 */
extern "C" int main(int argc, char **argv) {
    // Initialize GUI library
    if (gui_init() != 0) {
        return 1;
    }

    // Create editor window
    gui_window_t *win = gui_create_window("VEdit", dims::WIN_WIDTH, dims::WIN_HEIGHT);
    if (!win) {
        gui_shutdown();
        return 1;
    }

    // Initialize editor and view
    Editor editor;
    View view(win);

    // Register menus with displayd for global menu bar (Amiga/Mac style)
    {
        gui_menu_def_t gui_menus[3]; // VEdit has 3 menus: File, Edit, View

        // Zero-initialize
        for (int m = 0; m < 3 && m < NUM_MENUS; m++) {
            for (int i = 0; i < 24; i++)
                gui_menus[m].title[i] = '\0';
            gui_menus[m].item_count = 0;
            gui_menus[m]._pad[0] = 0;
            gui_menus[m]._pad[1] = 0;
            gui_menus[m]._pad[2] = 0;
            for (int j = 0; j < GUI_MAX_MENU_ITEMS; j++) {
                for (int k = 0; k < 32; k++)
                    gui_menus[m].items[j].label[k] = '\0';
                for (int k = 0; k < 16; k++)
                    gui_menus[m].items[j].shortcut[k] = '\0';
                gui_menus[m].items[j].action = 0;
                gui_menus[m].items[j].enabled = 0;
                gui_menus[m].items[j].checked = 0;
                gui_menus[m].items[j]._pad = 0;
            }
        }

        // Convert our menus to gui format
        for (int m = 0; m < 3 && m < NUM_MENUS; m++) {
            // Copy title (menu label)
            const char *src = g_menus[m].label;
            for (int i = 0; i < 23 && src[i]; i++)
                gui_menus[m].title[i] = src[i];

            gui_menus[m].item_count = static_cast<uint8_t>(g_menus[m].itemCount);

            // Copy items
            for (int j = 0; j < g_menus[m].itemCount && j < GUI_MAX_MENU_ITEMS; j++) {
                const MenuItem &item = g_menus[m].items[j];

                // Copy label
                if (item.label) {
                    for (int k = 0; k < 31 && item.label[k]; k++)
                        gui_menus[m].items[j].label[k] = item.label[k];
                }

                // Copy shortcut
                if (item.shortcut) {
                    for (int k = 0; k < 15 && item.shortcut[k]; k++)
                        gui_menus[m].items[j].shortcut[k] = item.shortcut[k];
                }

                // Action is the single-character code
                gui_menus[m].items[j].action = static_cast<uint8_t>(item.action);
                // Enable all non-separator items
                gui_menus[m].items[j].enabled = (item.label[0] != '-') ? 1 : 0;
                gui_menus[m].items[j].checked = 0;
            }
        }

        gui_set_menu(win, gui_menus, static_cast<uint8_t>(NUM_MENUS));
    }

    // Load file from command line
    if (argc > 1) {
        editor.loadFile(argv[1]);
    }

    // Initial render
    view.render(editor);

    bool running = true;
    while (running) {
        gui_event_t event;
        if (gui_poll_event(win, &event) == 0) {
            bool needsRedraw = false;

            switch (event.type) {
            case GUI_EVENT_CLOSE:
                // Window close button clicked
                running = false;
                break;

            case GUI_EVENT_MENU:
                // Handle global menu bar item selection (Amiga/Mac style)
                {
                    char action = static_cast<char>(event.menu.action);
                    if (action == 'Q') {
                        running = false;
                    } else if (action != 0) {
                        handleMenuAction(editor, view, win, action);
                    }
                    needsRedraw = true;
                }
                break;

            case GUI_EVENT_MOUSE:
                // Handle mouse button press (button down)
                // Note: Menu bar clicks are now handled by displayd (global menu bar)
                if (event.mouse.event_type == 1 && event.mouse.button == 0) {
                    if (event.mouse.y > view.textAreaY() &&
                               event.mouse.y < dims::WIN_HEIGHT - dims::STATUSBAR_HEIGHT) {
                        // Clicked in text area - position cursor
                        int relX = event.mouse.x;
                        int relY = event.mouse.y - view.textAreaY();
                        editor.setCursorFromClick(relX, relY,
                                                  view.textAreaX(editor.config().showLineNumbers),
                                                  view.visibleLines());
                    }
                    needsRedraw = true;
                }
                // Note: Menu hover is now handled by displayd (global menu bar)
                break;

            case GUI_EVENT_KEY:
                {
                    // Only process key press events, ignore key release
                    if (!event.key.pressed) {
                        break;
                    }

                    // Handle keyboard input for text editing
                    bool handled = true;

                    switch (event.key.keycode) {
                    // Navigation keys
                    case 0x50: // Left arrow
                        editor.moveCursorLeft();
                        break;
                    case 0x4F: // Right arrow
                        editor.moveCursorRight();
                        break;
                    case 0x52: // Up arrow
                        editor.moveCursorUp();
                        break;
                    case 0x51: // Down arrow
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

                    // Editing keys
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

                        // Letters (evdev keycodes)
                        // QWERTY row: Q=16 to P=25
                        if (kc >= 16 && kc <= 25) {
                            ch = "qwertyuiop"[kc - 16];
                        }
                        // ASDF row: A=30 to L=38
                        else if (kc >= 30 && kc <= 38) {
                            ch = "asdfghjkl"[kc - 30];
                        }
                        // ZXCV row: Z=44 to M=50
                        else if (kc >= 44 && kc <= 50) {
                            ch = "zxcvbnm"[kc - 44];
                        }
                        // Numbers 1-9 (keycodes 2-10)
                        else if (kc >= 2 && kc <= 10) {
                            ch = shift ? "!@#$%^&*("[kc - 2] : '0' + kc - 1;
                        }
                        // Number 0 (keycode 11)
                        else if (kc == 11) {
                            ch = shift ? ')' : '0';
                        }
                        // Space
                        else if (kc == 57) {
                            ch = ' ';
                        }
                        // Punctuation with shift variants
                        else if (kc == 12) {
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
                            // Convert to uppercase if shift is pressed
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
                        // Keep cursor in view after any edit/navigation
                        editor.ensureCursorVisible(view.visibleLines(),
                                                   view.visibleCols(editor.config().showLineNumbers));
                        needsRedraw = true;
                    }
                }
                break;

            default:
                break;
            }

            // Re-render if state changed
            if (needsRedraw) {
                view.render(editor);
            }
        }

        // Yield CPU to prevent busy-waiting
        // System call 0x0E = sched_yield()
        __asm__ volatile("mov x8, #0x0E\n\tsvc #0" ::: "x8");
    }

    // Cleanup
    gui_destroy_window(win);
    gui_shutdown();
    return 0;
}
