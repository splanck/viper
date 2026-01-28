//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
/**
 * @file dialog.c
 * @brief Standard dialog box implementations for the libwidget toolkit.
 *
 * This file provides ready-to-use dialog boxes for common user interaction
 * patterns. The dialogs are modal—they block interaction with other windows
 * until dismissed.
 *
 * ## Available Dialogs
 *
 * - **Message Box** (msgbox_show): Displays a message with configurable
 *   buttons (OK, OK/Cancel, Yes/No, Yes/No/Cancel) and icons.
 *
 * - **File Dialogs** (stubs): Open, save, and folder selection dialogs
 *   are declared but not yet implemented.
 *
 * ## Modal Behavior
 *
 * The msgbox_show function runs its own event loop internally. It blocks
 * the calling code until the user dismisses the dialog by clicking a button,
 * pressing Enter/Escape, or closing the window. This provides familiar
 * modal dialog behavior similar to desktop operating systems.
 *
 * ## Usage Example
 *
 * @code
 * // Show a confirmation dialog
 * msgbox_result_t result = msgbox_show(
 *     parent,
 *     "Confirm Delete",
 *     "Are you sure you want to delete this file?",
 *     MB_YES_NO,
 *     MB_ICON_QUESTION
 * );
 *
 * if (result == MB_RESULT_YES) {
 *     delete_file(filename);
 * }
 * @endcode
 *
 * @see widget.h for dialog type and result enumerations
 */
//===----------------------------------------------------------------------===//

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <widget.h>

/* File dialog constants */
#define FD_WIDTH 400
#define FD_HEIGHT 350
#define FD_PATH_HEIGHT 24
#define FD_LIST_TOP 30
#define FD_LIST_HEIGHT 250
#define FD_BUTTON_Y (FD_HEIGHT - 40)
#define FD_ITEM_HEIGHT 20
#define FD_MAX_ENTRIES 256
#define FD_MAX_PATH 512
#define FD_MAX_NAME 256

/* File entry structure for dialog */
struct fd_entry {
    char name[FD_MAX_NAME];
    bool is_dir;
};

/* Yield CPU to avoid busy-waiting */
static inline void fd_yield(void) {
    __asm__ volatile("mov x8, #0x0E\n\tsvc #0" ::: "x8");
}

//===----------------------------------------------------------------------===//
// Message Box
//===----------------------------------------------------------------------===//

/**
 * @brief Displays a modal message box dialog and waits for user response.
 *
 * This function creates a modal dialog window displaying a message with an
 * icon and one or more buttons. It blocks until the user dismisses the
 * dialog by clicking a button, pressing Enter/Escape, or closing the window.
 *
 * ## Dialog Layout
 *
 * ```
 * +---------------------------+
 * | Title                     |
 * +---------------------------+
 * | [Icon] Message text that  |
 * |        can wrap to multi- |
 * |        ple lines          |
 * |                           |
 * |    [OK]    [Cancel]       |
 * +---------------------------+
 * ```
 *
 * ## Button Configurations
 *
 * The `type` parameter determines which buttons appear:
 * - **MB_OK**: Single "OK" button
 * - **MB_OK_CANCEL**: "OK" and "Cancel" buttons
 * - **MB_YES_NO**: "Yes" and "No" buttons
 * - **MB_YES_NO_CANCEL**: "Yes", "No", and "Cancel" buttons
 *
 * ## Icon Types
 *
 * The `icon` parameter affects the icon color and symbol:
 * - **MB_ICON_INFO**: Blue icon with "i" (information)
 * - **MB_ICON_WARNING**: Orange icon with "!" (warning)
 * - **MB_ICON_ERROR**: Red icon with "X" (error)
 * - **MB_ICON_QUESTION**: Blue icon with "?" (question/confirmation)
 *
 * ## Keyboard Support
 *
 * - **Enter**: Selects the first button (OK or Yes)
 * - **Escape**: Selects Cancel, or OK if only OK is available
 *
 * ## Dialog Sizing
 *
 * The dialog width is calculated based on message length:
 * - Minimum: 200 pixels
 * - Maximum: 400 pixels
 * - Text that exceeds the width wraps to multiple lines
 *
 * @param parent  The parent window for positioning (currently unused).
 * @param title   The dialog window title. If NULL, "Message" is used.
 * @param message The message text to display. Supports newlines (\n) for
 *                explicit line breaks. Long lines are automatically wrapped.
 * @param type    The button configuration (MB_OK, MB_OK_CANCEL, etc.).
 * @param icon    The icon to display (MB_ICON_INFO, MB_ICON_WARNING, etc.).
 *
 * @return The result indicating which button was clicked:
 *         - MB_RESULT_OK: OK button clicked or Enter pressed
 *         - MB_RESULT_CANCEL: Cancel button clicked, Escape pressed, or window closed
 *         - MB_RESULT_YES: Yes button clicked
 *         - MB_RESULT_NO: No button clicked
 *
 * @note This function runs its own event loop internally. The caller's code
 *       is blocked until the dialog is dismissed.
 *
 * @note The dialog is automatically sized based on the message length,
 *       with a minimum width of 200 pixels and maximum of 400 pixels.
 *
 * @code
 * // Error message with single OK button
 * msgbox_show(parent, "Error", "File not found!", MB_OK, MB_ICON_ERROR);
 *
 * // Confirmation with Yes/No buttons
 * if (msgbox_show(parent, "Save?", "Save changes?",
 *                 MB_YES_NO, MB_ICON_QUESTION) == MB_RESULT_YES) {
 *     save_file();
 * }
 * @endcode
 *
 * @see msgbox_type_t for button configurations
 * @see msgbox_icon_t for icon types
 * @see msgbox_result_t for return values
 */
msgbox_result_t msgbox_show(gui_window_t *parent,
                            const char *title,
                            const char *message,
                            msgbox_type_t type,
                            msgbox_icon_t icon) {
    (void)parent;
    (void)icon;

    // Calculate dialog size
    int msg_len = message ? (int)strlen(message) : 0;
    int dialog_width = msg_len * 8 + 80;
    if (dialog_width < 200)
        dialog_width = 200;
    if (dialog_width > 400)
        dialog_width = 400;
    int dialog_height = 120;

    // Create dialog window
    gui_window_t *dialog =
        gui_create_window(title ? title : "Message", dialog_width, dialog_height);
    if (!dialog)
        return MB_RESULT_CANCEL;

    // Determine button layout
    const char *btn1_text = NULL;
    const char *btn2_text = NULL;
    const char *btn3_text = NULL;
    msgbox_result_t btn1_result = MB_RESULT_OK;
    msgbox_result_t btn2_result = MB_RESULT_CANCEL;
    msgbox_result_t btn3_result = MB_RESULT_CANCEL;

    switch (type) {
        case MB_OK:
            btn1_text = "OK";
            btn1_result = MB_RESULT_OK;
            break;
        case MB_OK_CANCEL:
            btn1_text = "OK";
            btn2_text = "Cancel";
            btn1_result = MB_RESULT_OK;
            btn2_result = MB_RESULT_CANCEL;
            break;
        case MB_YES_NO:
            btn1_text = "Yes";
            btn2_text = "No";
            btn1_result = MB_RESULT_YES;
            btn2_result = MB_RESULT_NO;
            break;
        case MB_YES_NO_CANCEL:
            btn1_text = "Yes";
            btn2_text = "No";
            btn3_text = "Cancel";
            btn1_result = MB_RESULT_YES;
            btn2_result = MB_RESULT_NO;
            btn3_result = MB_RESULT_CANCEL;
            break;
    }

    msgbox_result_t result = MB_RESULT_CANCEL;
    bool running = true;

    while (running) {
        // Draw dialog
        gui_fill_rect(dialog, 0, 0, dialog_width, dialog_height, WB_GRAY_LIGHT);

        // Draw icon area (simplified - just a colored box)
        int icon_x = 20;
        int icon_y = 20;
        uint32_t icon_color = WB_BLUE;

        switch (icon) {
            case MB_ICON_WARNING:
                icon_color = WB_ORANGE;
                break;
            case MB_ICON_ERROR:
                icon_color = WB_RED;
                break;
            case MB_ICON_QUESTION:
                icon_color = WB_BLUE;
                break;
            case MB_ICON_INFO:
            default:
                icon_color = WB_BLUE;
                break;
        }

        gui_fill_rect(dialog, icon_x, icon_y, 32, 32, icon_color);

        // Draw icon symbol
        const char *icon_sym = "i";
        switch (icon) {
            case MB_ICON_WARNING:
                icon_sym = "!";
                break;
            case MB_ICON_ERROR:
                icon_sym = "X";
                break;
            case MB_ICON_QUESTION:
                icon_sym = "?";
                break;
            case MB_ICON_INFO:
            default:
                icon_sym = "i";
                break;
        }
        gui_draw_text(dialog, icon_x + 12, icon_y + 11, icon_sym, WB_WHITE);

        // Draw message
        if (message) {
            // Simple word wrap
            int text_x = 70;
            int text_y = 25;
            int max_width = dialog_width - text_x - 20;
            int chars_per_line = max_width / 8;

            const char *p = message;
            while (*p) {
                int line_len = 0;
                while (p[line_len] && p[line_len] != '\n' && line_len < chars_per_line) {
                    line_len++;
                }

                char line_buf[128];
                if (line_len > (int)sizeof(line_buf) - 1)
                    line_len = sizeof(line_buf) - 1;
                memcpy(line_buf, p, line_len);
                line_buf[line_len] = '\0';

                gui_draw_text(dialog, text_x, text_y, line_buf, WB_BLACK);
                text_y += 14;

                p += line_len;
                if (*p == '\n')
                    p++;
            }
        }

        // Draw buttons
        int btn_y = dialog_height - 35;
        int btn_width = 70;
        int btn_height = 24;
        int btn_spacing = 10;

        int num_buttons = 1;
        if (btn2_text)
            num_buttons++;
        if (btn3_text)
            num_buttons++;

        int total_btn_width = num_buttons * btn_width + (num_buttons - 1) * btn_spacing;
        int btn_x = (dialog_width - total_btn_width) / 2;

        // Button 1
        draw_3d_button(dialog, btn_x, btn_y, btn_width, btn_height, false);
        int text_offset = (btn_width - (int)strlen(btn1_text) * 8) / 2;
        gui_draw_text(dialog, btn_x + text_offset, btn_y + 7, btn1_text, WB_BLACK);
        int btn1_x = btn_x;

        // Button 2
        int btn2_x = 0;
        if (btn2_text) {
            btn_x += btn_width + btn_spacing;
            btn2_x = btn_x;
            draw_3d_button(dialog, btn_x, btn_y, btn_width, btn_height, false);
            text_offset = (btn_width - (int)strlen(btn2_text) * 8) / 2;
            gui_draw_text(dialog, btn_x + text_offset, btn_y + 7, btn2_text, WB_BLACK);
        }

        // Button 3
        int btn3_x = 0;
        if (btn3_text) {
            btn_x += btn_width + btn_spacing;
            btn3_x = btn_x;
            draw_3d_button(dialog, btn_x, btn_y, btn_width, btn_height, false);
            text_offset = (btn_width - (int)strlen(btn3_text) * 8) / 2;
            gui_draw_text(dialog, btn_x + text_offset, btn_y + 7, btn3_text, WB_BLACK);
        }

        gui_present(dialog);

        // Handle events
        gui_event_t event;
        if (gui_poll_event(dialog, &event) == 0) {
            switch (event.type) {
                case GUI_EVENT_CLOSE:
                    result = MB_RESULT_CANCEL;
                    running = false;
                    break;

                case GUI_EVENT_MOUSE:
                    if (event.mouse.event_type == 1 && event.mouse.button == 0) {
                        int mx = event.mouse.x;
                        int my = event.mouse.y;

                        if (my >= btn_y && my < btn_y + btn_height) {
                            // Check button 1
                            if (mx >= btn1_x && mx < btn1_x + btn_width) {
                                result = btn1_result;
                                running = false;
                            }
                            // Check button 2
                            else if (btn2_text && mx >= btn2_x && mx < btn2_x + btn_width) {
                                result = btn2_result;
                                running = false;
                            }
                            // Check button 3
                            else if (btn3_text && mx >= btn3_x && mx < btn3_x + btn_width) {
                                result = btn3_result;
                                running = false;
                            }
                        }
                    }
                    break;

                case GUI_EVENT_KEY:
                    // Enter = OK/Yes, Escape = Cancel/No (evdev keycodes)
                    if (event.key.keycode == 28) { // Enter
                        result = btn1_result;
                        running = false;
                    } else if (event.key.keycode == 1) { // Escape
                        result = (type == MB_OK) ? MB_RESULT_OK : MB_RESULT_CANCEL;
                        running = false;
                    }
                    break;

                default:
                    break;
            }
        }

        // Yield CPU
        __asm__ volatile("mov x8, #0x0E\n\tsvc #0" ::: "x8");
    }

    gui_destroy_window(dialog);
    return result;
}

//===----------------------------------------------------------------------===//
// File Dialogs (Simplified stubs - full implementation would need file browser)
//===----------------------------------------------------------------------===//

/**
 * @brief Opens a file selection dialog for choosing an existing file.
 *
 * Displays a modal file browser with directory navigation, file list,
 * and OK/Cancel buttons. Returns the selected file path or NULL if canceled.
 */
char *filedialog_open(gui_window_t *parent,
                      const char *title,
                      const char *filter,
                      const char *initial_dir) {
    (void)parent;
    (void)filter; /* TODO: implement filtering */

    /* Set initial directory */
    char current_path[FD_MAX_PATH];
    if (initial_dir && initial_dir[0]) {
        strncpy(current_path, initial_dir, FD_MAX_PATH - 1);
        current_path[FD_MAX_PATH - 1] = '\0';
    } else {
        strcpy(current_path, "/");
    }

    /* Create dialog window */
    gui_window_t *dialog = gui_create_window(title ? title : "Open File", FD_WIDTH, FD_HEIGHT);
    if (!dialog)
        return NULL;

    /* Allocate entry array */
    fd_entry *entries = static_cast<fd_entry *>(malloc(FD_MAX_ENTRIES * sizeof(fd_entry)));
    if (!entries) {
        gui_destroy_window(dialog);
        return NULL;
    }

    char *result = NULL;
    bool running = true;
    int entry_count = 0;
    int selected = -1;
    int scroll_offset = 0;
    int visible_items = FD_LIST_HEIGHT / FD_ITEM_HEIGHT;

    /* Load directory function */
    auto load_dir = [&]() {
        entry_count = 0;
        selected = -1;
        scroll_offset = 0;

        DIR *dir = opendir(current_path);
        if (!dir)
            return;

        /* Add parent directory entry if not at root */
        if (strcmp(current_path, "/") != 0 && entry_count < FD_MAX_ENTRIES) {
            strcpy(entries[entry_count].name, "..");
            entries[entry_count].is_dir = true;
            entry_count++;
        }

        struct dirent *ent;
        while ((ent = readdir(dir)) != NULL && entry_count < FD_MAX_ENTRIES) {
            if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
                continue;

            strncpy(entries[entry_count].name, ent->d_name, FD_MAX_NAME - 1);
            entries[entry_count].name[FD_MAX_NAME - 1] = '\0';
            entries[entry_count].is_dir = (ent->d_type == DT_DIR);
            entry_count++;
        }
        closedir(dir);
    };

    /* Navigate up one directory */
    auto navigate_up = [&]() {
        if (strcmp(current_path, "/") == 0)
            return;
        char *last_slash = strrchr(current_path, '/');
        if (last_slash == current_path) {
            strcpy(current_path, "/");
        } else if (last_slash) {
            *last_slash = '\0';
        }
        load_dir();
    };

    /* Navigate into directory */
    auto navigate_into = [&](const char *name) {
        if (strcmp(name, "..") == 0) {
            navigate_up();
            return;
        }
        size_t cur_len = strlen(current_path);
        if (cur_len + strlen(name) + 2 < FD_MAX_PATH) {
            if (current_path[cur_len - 1] != '/')
                strcat(current_path, "/");
            strcat(current_path, name);
        }
        load_dir();
    };

    /* Initial load */
    load_dir();

    while (running) {
        /* Draw dialog background */
        gui_fill_rect(dialog, 0, 0, FD_WIDTH, FD_HEIGHT, WB_GRAY_LIGHT);

        /* Draw path bar */
        gui_fill_rect(dialog, 5, 5, FD_WIDTH - 10, FD_PATH_HEIGHT, WB_DARK_BG);
        gui_draw_rect(dialog, 5, 5, FD_WIDTH - 10, FD_PATH_HEIGHT, WB_GRAY_DARK);
        gui_draw_text(dialog, 10, 10, current_path, WB_CREAM);

        /* Draw file list */
        gui_fill_rect(dialog, 5, FD_LIST_TOP, FD_WIDTH - 10, FD_LIST_HEIGHT, WB_DARK_BG);
        gui_draw_rect(dialog, 5, FD_LIST_TOP, FD_WIDTH - 10, FD_LIST_HEIGHT, WB_GRAY_DARK);

        for (int i = 0; i < visible_items && (i + scroll_offset) < entry_count; i++) {
            int idx = i + scroll_offset;
            int y = FD_LIST_TOP + 2 + i * FD_ITEM_HEIGHT;

            /* Selection highlight */
            if (idx == selected) {
                gui_fill_rect(dialog, 6, y, FD_WIDTH - 12, FD_ITEM_HEIGHT, WB_BLUE);
            }

            /* Icon indicator and name */
            uint32_t text_color = (idx == selected) ? WB_WHITE : WB_CREAM;
            const char *icon = entries[idx].is_dir ? "[D] " : "    ";

            char display[FD_MAX_NAME + 8];
            snprintf(display, sizeof(display), "%s%s", icon, entries[idx].name);
            gui_draw_text(dialog, 10, y + 3, display, text_color);
        }

        /* Draw buttons */
        int btn_width = 80;
        int btn_height = 26;
        int ok_x = FD_WIDTH / 2 - btn_width - 10;
        int cancel_x = FD_WIDTH / 2 + 10;

        draw_3d_button(dialog, ok_x, FD_BUTTON_Y, btn_width, btn_height, false);
        draw_3d_button(dialog, cancel_x, FD_BUTTON_Y, btn_width, btn_height, false);

        int text_offset = (btn_width - 16) / 2;
        gui_draw_text(dialog, ok_x + text_offset, FD_BUTTON_Y + 7, "OK", WB_BLACK);
        text_offset = (btn_width - 48) / 2;
        gui_draw_text(dialog, cancel_x + text_offset, FD_BUTTON_Y + 7, "Cancel", WB_BLACK);

        gui_present(dialog);

        /* Handle events */
        gui_event_t event;
        if (gui_poll_event(dialog, &event) == 0) {
            switch (event.type) {
                case GUI_EVENT_CLOSE:
                    running = false;
                    break;

                case GUI_EVENT_MOUSE:
                    if (event.mouse.event_type == 1 && event.mouse.button == 0) {
                        int mx = event.mouse.x;
                        int my = event.mouse.y;

                        /* Check file list click */
                        if (mx >= 5 && mx < FD_WIDTH - 5 && my >= FD_LIST_TOP &&
                            my < FD_LIST_TOP + FD_LIST_HEIGHT) {
                            int clicked_idx =
                                (my - FD_LIST_TOP - 2) / FD_ITEM_HEIGHT + scroll_offset;
                            if (clicked_idx >= 0 && clicked_idx < entry_count) {
                                if (selected == clicked_idx) {
                                    /* Double-click behavior */
                                    if (entries[clicked_idx].is_dir) {
                                        navigate_into(entries[clicked_idx].name);
                                    } else {
                                        /* Select and confirm */
                                        size_t path_len = strlen(current_path);
                                        size_t name_len = strlen(entries[clicked_idx].name);
                                        result =
                                            static_cast<char *>(malloc(path_len + name_len + 2));
                                        if (result) {
                                            strcpy(result, current_path);
                                            if (result[path_len - 1] != '/')
                                                strcat(result, "/");
                                            strcat(result, entries[clicked_idx].name);
                                        }
                                        running = false;
                                    }
                                } else {
                                    selected = clicked_idx;
                                }
                            }
                        }

                        /* Check OK button */
                        if (mx >= ok_x && mx < ok_x + btn_width && my >= FD_BUTTON_Y &&
                            my < FD_BUTTON_Y + btn_height) {
                            if (selected >= 0 && !entries[selected].is_dir) {
                                size_t path_len = strlen(current_path);
                                size_t name_len = strlen(entries[selected].name);
                                result = static_cast<char *>(malloc(path_len + name_len + 2));
                                if (result) {
                                    strcpy(result, current_path);
                                    if (result[path_len - 1] != '/')
                                        strcat(result, "/");
                                    strcat(result, entries[selected].name);
                                }
                            }
                            running = false;
                        }

                        /* Check Cancel button */
                        if (mx >= cancel_x && mx < cancel_x + btn_width && my >= FD_BUTTON_Y &&
                            my < FD_BUTTON_Y + btn_height) {
                            running = false;
                        }
                    }
                    break;

                case GUI_EVENT_KEY:
                    if (event.key.keycode == 28) { /* Enter (evdev) */
                        if (selected >= 0) {
                            if (entries[selected].is_dir) {
                                navigate_into(entries[selected].name);
                            } else {
                                size_t path_len = strlen(current_path);
                                size_t name_len = strlen(entries[selected].name);
                                result = static_cast<char *>(malloc(path_len + name_len + 2));
                                if (result) {
                                    strcpy(result, current_path);
                                    if (result[path_len - 1] != '/')
                                        strcat(result, "/");
                                    strcat(result, entries[selected].name);
                                }
                                running = false;
                            }
                        }
                    } else if (event.key.keycode == 1) { /* Escape (evdev) */
                        running = false;
                    } else if (event.key.keycode == 103) { /* Up (evdev) */
                        if (selected > 0)
                            selected--;
                        if (selected < scroll_offset)
                            scroll_offset = selected;
                    } else if (event.key.keycode == 108) { /* Down (evdev) */
                        if (selected < entry_count - 1)
                            selected++;
                        if (selected >= scroll_offset + visible_items)
                            scroll_offset = selected - visible_items + 1;
                    }
                    break;

                default:
                    break;
            }
        }

        fd_yield();
    }

    free(entries);
    gui_destroy_window(dialog);
    return result;
}

/**
 * @brief Opens a file selection dialog for choosing a save location.
 *
 * Displays a modal file browser with directory navigation, file list,
 * filename entry field, and Save/Cancel buttons.
 */
char *filedialog_save(gui_window_t *parent,
                      const char *title,
                      const char *filter,
                      const char *initial_dir) {
    (void)parent;
    (void)filter;

    /* Set initial directory */
    char current_path[FD_MAX_PATH];
    if (initial_dir && initial_dir[0]) {
        strncpy(current_path, initial_dir, FD_MAX_PATH - 1);
        current_path[FD_MAX_PATH - 1] = '\0';
    } else {
        strcpy(current_path, "/");
    }

    /* Filename buffer */
    char filename[FD_MAX_NAME] = "";
    int filename_cursor = 0;

    /* Create dialog window (taller for filename entry) */
    int dialog_height = FD_HEIGHT + 30;
    gui_window_t *dialog = gui_create_window(title ? title : "Save File", FD_WIDTH, dialog_height);
    if (!dialog)
        return NULL;

    fd_entry *entries = static_cast<fd_entry *>(malloc(FD_MAX_ENTRIES * sizeof(fd_entry)));
    if (!entries) {
        gui_destroy_window(dialog);
        return NULL;
    }

    char *result = NULL;
    bool running = true;
    int entry_count = 0;
    int selected = -1;
    int scroll_offset = 0;
    int visible_items = FD_LIST_HEIGHT / FD_ITEM_HEIGHT;

    /* Load directory (same as open dialog) */
    auto load_dir = [&]() {
        entry_count = 0;
        selected = -1;
        scroll_offset = 0;

        DIR *dir = opendir(current_path);
        if (!dir)
            return;

        if (strcmp(current_path, "/") != 0 && entry_count < FD_MAX_ENTRIES) {
            strcpy(entries[entry_count].name, "..");
            entries[entry_count].is_dir = true;
            entry_count++;
        }

        struct dirent *ent;
        while ((ent = readdir(dir)) != NULL && entry_count < FD_MAX_ENTRIES) {
            if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
                continue;
            strncpy(entries[entry_count].name, ent->d_name, FD_MAX_NAME - 1);
            entries[entry_count].name[FD_MAX_NAME - 1] = '\0';
            entries[entry_count].is_dir = (ent->d_type == DT_DIR);
            entry_count++;
        }
        closedir(dir);
    };

    auto navigate_up = [&]() {
        if (strcmp(current_path, "/") == 0)
            return;
        char *last_slash = strrchr(current_path, '/');
        if (last_slash == current_path) {
            strcpy(current_path, "/");
        } else if (last_slash) {
            *last_slash = '\0';
        }
        load_dir();
    };

    auto navigate_into = [&](const char *name) {
        if (strcmp(name, "..") == 0) {
            navigate_up();
            return;
        }
        size_t cur_len = strlen(current_path);
        if (cur_len + strlen(name) + 2 < FD_MAX_PATH) {
            if (current_path[cur_len - 1] != '/')
                strcat(current_path, "/");
            strcat(current_path, name);
        }
        load_dir();
    };

    load_dir();

    int filename_y = FD_LIST_TOP + FD_LIST_HEIGHT + 5;
    int button_y = dialog_height - 40;

    while (running) {
        gui_fill_rect(dialog, 0, 0, FD_WIDTH, dialog_height, WB_GRAY_LIGHT);

        /* Path bar */
        gui_fill_rect(dialog, 5, 5, FD_WIDTH - 10, FD_PATH_HEIGHT, WB_DARK_BG);
        gui_draw_rect(dialog, 5, 5, FD_WIDTH - 10, FD_PATH_HEIGHT, WB_GRAY_DARK);
        gui_draw_text(dialog, 10, 10, current_path, WB_CREAM);

        /* File list */
        gui_fill_rect(dialog, 5, FD_LIST_TOP, FD_WIDTH - 10, FD_LIST_HEIGHT, WB_DARK_BG);
        gui_draw_rect(dialog, 5, FD_LIST_TOP, FD_WIDTH - 10, FD_LIST_HEIGHT, WB_GRAY_DARK);

        for (int i = 0; i < visible_items && (i + scroll_offset) < entry_count; i++) {
            int idx = i + scroll_offset;
            int y = FD_LIST_TOP + 2 + i * FD_ITEM_HEIGHT;

            if (idx == selected) {
                gui_fill_rect(dialog, 6, y, FD_WIDTH - 12, FD_ITEM_HEIGHT, WB_BLUE);
            }

            uint32_t text_color = (idx == selected) ? WB_WHITE : WB_CREAM;
            const char *icon = entries[idx].is_dir ? "[D] " : "    ";

            char display[FD_MAX_NAME + 8];
            snprintf(display, sizeof(display), "%s%s", icon, entries[idx].name);
            gui_draw_text(dialog, 10, y + 3, display, text_color);
        }

        /* Filename label and entry */
        gui_draw_text(dialog, 10, filename_y + 5, "Filename:", WB_BLACK);
        gui_fill_rect(dialog, 80, filename_y, FD_WIDTH - 90, 24, WB_DARK_BG);
        gui_draw_rect(dialog, 80, filename_y, FD_WIDTH - 90, 24, WB_GRAY_DARK);
        gui_draw_text(dialog, 85, filename_y + 5, filename, WB_CREAM);

        /* Cursor */
        int cursor_x = 85 + filename_cursor * 8;
        gui_draw_vline(dialog, cursor_x, filename_y + 3, filename_y + 21, WB_CREAM);

        /* Buttons */
        int btn_width = 80;
        int btn_height = 26;
        int save_x = FD_WIDTH / 2 - btn_width - 10;
        int cancel_x = FD_WIDTH / 2 + 10;

        draw_3d_button(dialog, save_x, button_y, btn_width, btn_height, false);
        draw_3d_button(dialog, cancel_x, button_y, btn_width, btn_height, false);

        int text_offset = (btn_width - 32) / 2;
        gui_draw_text(dialog, save_x + text_offset, button_y + 7, "Save", WB_BLACK);
        text_offset = (btn_width - 48) / 2;
        gui_draw_text(dialog, cancel_x + text_offset, button_y + 7, "Cancel", WB_BLACK);

        gui_present(dialog);

        gui_event_t event;
        if (gui_poll_event(dialog, &event) == 0) {
            switch (event.type) {
                case GUI_EVENT_CLOSE:
                    running = false;
                    break;

                case GUI_EVENT_MOUSE:
                    if (event.mouse.event_type == 1 && event.mouse.button == 0) {
                        int mx = event.mouse.x;
                        int my = event.mouse.y;

                        /* File list click */
                        if (mx >= 5 && mx < FD_WIDTH - 5 && my >= FD_LIST_TOP &&
                            my < FD_LIST_TOP + FD_LIST_HEIGHT) {
                            int clicked_idx =
                                (my - FD_LIST_TOP - 2) / FD_ITEM_HEIGHT + scroll_offset;
                            if (clicked_idx >= 0 && clicked_idx < entry_count) {
                                if (selected == clicked_idx && entries[clicked_idx].is_dir) {
                                    navigate_into(entries[clicked_idx].name);
                                } else {
                                    selected = clicked_idx;
                                    if (!entries[selected].is_dir) {
                                        strncpy(filename, entries[selected].name, FD_MAX_NAME - 1);
                                        filename[FD_MAX_NAME - 1] = '\0';
                                        filename_cursor = static_cast<int>(strlen(filename));
                                    }
                                }
                            }
                        }

                        /* Save button */
                        if (mx >= save_x && mx < save_x + btn_width && my >= button_y &&
                            my < button_y + btn_height) {
                            if (filename[0] != '\0') {
                                size_t path_len = strlen(current_path);
                                size_t name_len = strlen(filename);
                                result = static_cast<char *>(malloc(path_len + name_len + 2));
                                if (result) {
                                    strcpy(result, current_path);
                                    if (result[path_len - 1] != '/')
                                        strcat(result, "/");
                                    strcat(result, filename);
                                }
                            }
                            running = false;
                        }

                        /* Cancel button */
                        if (mx >= cancel_x && mx < cancel_x + btn_width && my >= button_y &&
                            my < button_y + btn_height) {
                            running = false;
                        }
                    }
                    break;

                case GUI_EVENT_KEY: {
                    /* evdev keycodes */
                    uint16_t kc = event.key.keycode;
                    if (kc == 28) { /* Enter */
                        if (filename[0] != '\0') {
                            size_t path_len = strlen(current_path);
                            size_t name_len = strlen(filename);
                            result = static_cast<char *>(malloc(path_len + name_len + 2));
                            if (result) {
                                strcpy(result, current_path);
                                if (result[path_len - 1] != '/')
                                    strcat(result, "/");
                                strcat(result, filename);
                            }
                            running = false;
                        }
                    } else if (kc == 1) { /* Escape */
                        running = false;
                    } else if (kc == 14) { /* Backspace */
                        if (filename_cursor > 0) {
                            memmove(&filename[filename_cursor - 1],
                                    &filename[filename_cursor],
                                    strlen(filename) - filename_cursor + 1);
                            filename_cursor--;
                        }
                    } else if (kc == 103) { /* Up - navigate to file list */
                        if (selected > 0)
                            selected--;
                        if (selected < scroll_offset)
                            scroll_offset = selected;
                    } else if (kc == 108) { /* Down - navigate file list */
                        if (selected < entry_count - 1)
                            selected++;
                        if (selected >= scroll_offset + visible_items)
                            scroll_offset = selected - visible_items + 1;
                    } else {
                        /* Character input (evdev keycodes) */
                        char c = 0;
                        /* QWERTY row: Q=16..P=25 */
                        if (kc >= 16 && kc <= 25) {
                            c = "qwertyuiop"[kc - 16];
                        }
                        /* Home row: A=30..L=38 */
                        else if (kc >= 30 && kc <= 38) {
                            c = "asdfghjkl"[kc - 30];
                        }
                        /* Bottom row: Z=44..M=50 */
                        else if (kc >= 44 && kc <= 50) {
                            c = "zxcvbnm"[kc - 44];
                        }
                        /* Numbers 1-9: keycodes 2-10 */
                        else if (kc >= 2 && kc <= 10) {
                            c = '0' + (kc - 1);
                        }
                        /* Number 0: keycode 11 */
                        else if (kc == 11) {
                            c = '0';
                        }
                        /* Period: keycode 52 */
                        else if (kc == 52) {
                            c = '.';
                        }
                        /* Minus: keycode 12 */
                        else if (kc == 12) {
                            c = '-';
                        }
                        /* Underscore via shift (simplified - just use underscore) */
                        else if (kc == 57) { /* Space */
                            c = ' ';
                        }

                        if (c && filename_cursor < FD_MAX_NAME - 1) {
                            memmove(&filename[filename_cursor + 1],
                                    &filename[filename_cursor],
                                    strlen(filename) - filename_cursor + 1);
                            filename[filename_cursor++] = c;
                        }
                    }
                    break;
                }

                default:
                    break;
            }
        }

        fd_yield();
    }

    free(entries);
    gui_destroy_window(dialog);
    return result;
}

/**
 * @brief Opens a dialog for selecting a folder/directory.
 *
 * Displays a modal directory browser showing only folders, with navigation
 * and OK/Cancel buttons. Returns the selected directory path or NULL if canceled.
 *
 * @param parent      The parent window for positioning (currently unused).
 * @param title       The dialog window title. If NULL, "Select Folder" is used.
 * @param initial_dir Initial directory to display. If NULL, "/" is used.
 *
 * @return A dynamically allocated string containing the selected directory path,
 *         or NULL if canceled. Caller must free() the returned string.
 */
char *filedialog_folder(gui_window_t *parent, const char *title, const char *initial_dir) {
    (void)parent;

    /* Set initial directory */
    char current_path[FD_MAX_PATH];
    if (initial_dir && initial_dir[0]) {
        strncpy(current_path, initial_dir, FD_MAX_PATH - 1);
        current_path[FD_MAX_PATH - 1] = '\0';
    } else {
        strcpy(current_path, "/");
    }

    /* Create dialog window */
    gui_window_t *dialog = gui_create_window(title ? title : "Select Folder", FD_WIDTH, FD_HEIGHT);
    if (!dialog)
        return NULL;

    /* Allocate entry array */
    fd_entry *entries = static_cast<fd_entry *>(malloc(FD_MAX_ENTRIES * sizeof(fd_entry)));
    if (!entries) {
        gui_destroy_window(dialog);
        return NULL;
    }

    char *result = NULL;
    bool running = true;
    int entry_count = 0;
    int selected = -1;
    int scroll_offset = 0;
    int visible_items = FD_LIST_HEIGHT / FD_ITEM_HEIGHT;

    /* Load directory - only directories */
    auto load_dir = [&]() {
        entry_count = 0;
        selected = -1;
        scroll_offset = 0;

        DIR *dir = opendir(current_path);
        if (!dir)
            return;

        /* Add parent directory entry if not at root */
        if (strcmp(current_path, "/") != 0 && entry_count < FD_MAX_ENTRIES) {
            strcpy(entries[entry_count].name, "..");
            entries[entry_count].is_dir = true;
            entry_count++;
        }

        struct dirent *ent;
        while ((ent = readdir(dir)) != NULL && entry_count < FD_MAX_ENTRIES) {
            if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
                continue;

            /* Only show directories */
            if (ent->d_type != DT_DIR)
                continue;

            strncpy(entries[entry_count].name, ent->d_name, FD_MAX_NAME - 1);
            entries[entry_count].name[FD_MAX_NAME - 1] = '\0';
            entries[entry_count].is_dir = true;
            entry_count++;
        }
        closedir(dir);
    };

    /* Navigate up one directory */
    auto navigate_up = [&]() {
        if (strcmp(current_path, "/") == 0)
            return;
        char *last_slash = strrchr(current_path, '/');
        if (last_slash == current_path) {
            strcpy(current_path, "/");
        } else if (last_slash) {
            *last_slash = '\0';
        }
        load_dir();
    };

    /* Navigate into directory */
    auto navigate_into = [&](const char *name) {
        if (strcmp(name, "..") == 0) {
            navigate_up();
            return;
        }
        size_t cur_len = strlen(current_path);
        if (cur_len + strlen(name) + 2 < FD_MAX_PATH) {
            if (current_path[cur_len - 1] != '/')
                strcat(current_path, "/");
            strcat(current_path, name);
        }
        load_dir();
    };

    /* Initial load */
    load_dir();

    while (running) {
        /* Draw dialog background */
        gui_fill_rect(dialog, 0, 0, FD_WIDTH, FD_HEIGHT, WB_GRAY_LIGHT);

        /* Draw path bar with label */
        gui_draw_text(dialog, 10, 10, "Selected:", WB_BLACK);
        gui_fill_rect(dialog, 80, 5, FD_WIDTH - 90, FD_PATH_HEIGHT, WB_DARK_BG);
        gui_draw_rect(dialog, 80, 5, FD_WIDTH - 90, FD_PATH_HEIGHT, WB_GRAY_DARK);
        gui_draw_text(dialog, 85, 10, current_path, WB_CREAM);

        /* Draw folder list */
        gui_fill_rect(dialog, 5, FD_LIST_TOP, FD_WIDTH - 10, FD_LIST_HEIGHT, WB_DARK_BG);
        gui_draw_rect(dialog, 5, FD_LIST_TOP, FD_WIDTH - 10, FD_LIST_HEIGHT, WB_GRAY_DARK);

        for (int i = 0; i < visible_items && (i + scroll_offset) < entry_count; i++) {
            int idx = i + scroll_offset;
            int y = FD_LIST_TOP + 2 + i * FD_ITEM_HEIGHT;

            /* Selection highlight */
            if (idx == selected) {
                gui_fill_rect(dialog, 6, y, FD_WIDTH - 12, FD_ITEM_HEIGHT, WB_BLUE);
            }

            /* Folder icon and name */
            uint32_t text_color = (idx == selected) ? WB_WHITE : WB_CREAM;

            char display[FD_MAX_NAME + 8];
            snprintf(display, sizeof(display), "[D] %s", entries[idx].name);
            gui_draw_text(dialog, 10, y + 3, display, text_color);
        }

        /* Draw buttons */
        int btn_width = 80;
        int btn_height = 26;
        int ok_x = FD_WIDTH / 2 - btn_width - 10;
        int cancel_x = FD_WIDTH / 2 + 10;

        draw_3d_button(dialog, ok_x, FD_BUTTON_Y, btn_width, btn_height, false);
        draw_3d_button(dialog, cancel_x, FD_BUTTON_Y, btn_width, btn_height, false);

        int text_offset = (btn_width - 48) / 2;
        gui_draw_text(dialog, ok_x + text_offset, FD_BUTTON_Y + 7, "Select", WB_BLACK);
        text_offset = (btn_width - 48) / 2;
        gui_draw_text(dialog, cancel_x + text_offset, FD_BUTTON_Y + 7, "Cancel", WB_BLACK);

        gui_present(dialog);

        /* Handle events */
        gui_event_t event;
        if (gui_poll_event(dialog, &event) == 0) {
            switch (event.type) {
                case GUI_EVENT_CLOSE:
                    running = false;
                    break;

                case GUI_EVENT_MOUSE:
                    if (event.mouse.event_type == 1 && event.mouse.button == 0) {
                        int mx = event.mouse.x;
                        int my = event.mouse.y;

                        /* Check folder list click */
                        if (mx >= 5 && mx < FD_WIDTH - 5 && my >= FD_LIST_TOP &&
                            my < FD_LIST_TOP + FD_LIST_HEIGHT) {
                            int clicked_idx =
                                (my - FD_LIST_TOP - 2) / FD_ITEM_HEIGHT + scroll_offset;
                            if (clicked_idx >= 0 && clicked_idx < entry_count) {
                                if (selected == clicked_idx) {
                                    /* Double-click: navigate into folder */
                                    navigate_into(entries[clicked_idx].name);
                                } else {
                                    selected = clicked_idx;
                                }
                            }
                        }

                        /* Check Select button - select current directory */
                        if (mx >= ok_x && mx < ok_x + btn_width && my >= FD_BUTTON_Y &&
                            my < FD_BUTTON_Y + btn_height) {
                            result = static_cast<char *>(malloc(strlen(current_path) + 1));
                            if (result) {
                                strcpy(result, current_path);
                            }
                            running = false;
                        }

                        /* Check Cancel button */
                        if (mx >= cancel_x && mx < cancel_x + btn_width && my >= FD_BUTTON_Y &&
                            my < FD_BUTTON_Y + btn_height) {
                            running = false;
                        }
                    }
                    break;

                case GUI_EVENT_KEY:
                    if (event.key.keycode == 28) { /* Enter (evdev) */
                        /* If folder selected, navigate into it */
                        if (selected >= 0) {
                            navigate_into(entries[selected].name);
                        } else {
                            /* Select current directory */
                            result = static_cast<char *>(malloc(strlen(current_path) + 1));
                            if (result) {
                                strcpy(result, current_path);
                            }
                            running = false;
                        }
                    } else if (event.key.keycode == 1) { /* Escape (evdev) */
                        running = false;
                    } else if (event.key.keycode == 103) { /* Up (evdev) */
                        if (selected > 0)
                            selected--;
                        if (selected < scroll_offset)
                            scroll_offset = selected;
                    } else if (event.key.keycode == 108) { /* Down (evdev) */
                        if (selected < entry_count - 1)
                            selected++;
                        if (selected >= scroll_offset + visible_items)
                            scroll_offset = selected - visible_items + 1;
                    }
                    break;

                default:
                    break;
            }
        }

        fd_yield();
    }

    free(entries);
    gui_destroy_window(dialog);
    return result;
}
