//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: user/libwidget/src/dialog.c
// Purpose: Dialog box implementations.
//
//===----------------------------------------------------------------------===//

#include <widget.h>
#include <stdlib.h>
#include <string.h>

//===----------------------------------------------------------------------===//
// Message Box
//===----------------------------------------------------------------------===//

msgbox_result_t msgbox_show(gui_window_t *parent, const char *title, const char *message,
                            msgbox_type_t type, msgbox_icon_t icon) {
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
    gui_window_t *dialog = gui_create_window(title ? title : "Message", dialog_width, dialog_height);
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
                // Enter = OK/Yes, Escape = Cancel/No
                if (event.key.keycode == 0x28) { // Enter
                    result = btn1_result;
                    running = false;
                } else if (event.key.keycode == 0x29) { // Escape
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

char *filedialog_open(gui_window_t *parent, const char *title, const char *filter,
                      const char *initial_dir) {
    (void)parent;
    (void)title;
    (void)filter;
    (void)initial_dir;

    // Stub - would need full file browser implementation
    // For now, return NULL indicating cancel
    return NULL;
}

char *filedialog_save(gui_window_t *parent, const char *title, const char *filter,
                      const char *initial_dir) {
    (void)parent;
    (void)title;
    (void)filter;
    (void)initial_dir;

    // Stub - would need full file browser implementation
    return NULL;
}

char *filedialog_folder(gui_window_t *parent, const char *title, const char *initial_dir) {
    (void)parent;
    (void)title;
    (void)initial_dir;

    // Stub - would need full file browser implementation
    return NULL;
}
