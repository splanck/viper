/**
 * @file hello_gui.c
 * @brief Simple GUI test program for ViperDOS.
 *
 * @details
 * Demonstrates the libgui API by creating a window and drawing
 * some basic graphics.
 */

#include <gui.h>
#include <stdio.h>
#include <string.h>

// Colors
#define COLOR_BLUE      0xFF2040A0
#define COLOR_WHITE     0xFFFFFFFF
#define COLOR_BLACK     0xFF000000
#define COLOR_GRAY      0xFF808080
#define COLOR_LIGHTGRAY 0xFFC0C0C0
#define COLOR_RED       0xFFFF0000
#define COLOR_GREEN     0xFF00FF00
#define COLOR_YELLOW    0xFFFFFF00

// Draw a simple button
static void draw_button(gui_window_t *win, int x, int y, int w, int h,
                        const char *label, uint32_t bg_color)
{
    // Button background
    gui_fill_rect(win, x, y, w, h, bg_color);

    // Button border
    gui_draw_rect(win, x, y, w, h, COLOR_BLACK);

    // Button highlight (3D effect)
    gui_draw_hline(win, x + 1, x + w - 2, y + 1, COLOR_WHITE);
    gui_draw_vline(win, x + 1, y + 1, y + h - 2, COLOR_WHITE);

    // Button shadow
    gui_draw_hline(win, x + 1, x + w - 2, y + h - 2, COLOR_GRAY);
    gui_draw_vline(win, x + w - 2, y + 1, y + h - 2, COLOR_GRAY);

    // Center label
    int label_len = strlen(label);
    int label_x = x + (w - label_len * 8) / 2;
    int label_y = y + (h - 8) / 2;
    gui_draw_text(win, label_x, label_y, label, COLOR_BLACK);
}

int main(void)
{
    printf("Hello GUI - ViperDOS GUI Demo\n");
    printf("Initializing GUI...\n");

    // Initialize GUI
    if (gui_init() != 0)
    {
        printf("ERROR: Failed to initialize GUI (displayd not running?)\n");
        return 1;
    }
    printf("GUI initialized successfully\n");

    // Get display info
    gui_display_info_t info;
    if (gui_get_display_info(&info) == 0)
    {
        printf("Display: %ux%u, format=0x%08x\n", info.width, info.height, info.format);
    }

    // Create window
    printf("Creating window...\n");
    gui_window_t *win = gui_create_window("Hello GUI", 400, 300);
    if (!win)
    {
        printf("ERROR: Failed to create window\n");
        gui_shutdown();
        return 1;
    }
    printf("Window created: %ux%u\n", gui_get_width(win), gui_get_height(win));

    // Get pixel buffer
    uint32_t *pixels = gui_get_pixels(win);
    uint32_t width = gui_get_width(win);
    uint32_t height = gui_get_height(win);

    if (!pixels)
    {
        printf("ERROR: No pixel buffer\n");
        gui_destroy_window(win);
        gui_shutdown();
        return 1;
    }

    // Clear to blue background
    gui_fill_rect(win, 0, 0, width, height, COLOR_BLUE);

    // Draw a title
    gui_draw_text(win, 130, 30, "Welcome to ViperDOS!", COLOR_WHITE);
    gui_draw_text(win, 125, 50, "GUI Desktop Working!", COLOR_YELLOW);

    // Draw some colored boxes
    gui_fill_rect(win, 30, 80, 80, 60, COLOR_RED);
    gui_draw_text(win, 45, 105, "Red", COLOR_WHITE);

    gui_fill_rect(win, 160, 80, 80, 60, COLOR_GREEN);
    gui_draw_text(win, 170, 105, "Green", COLOR_BLACK);

    gui_fill_rect(win, 290, 80, 80, 60, COLOR_YELLOW);
    gui_draw_text(win, 297, 105, "Yellow", COLOR_BLACK);

    // Draw buttons
    draw_button(win, 60, 170, 100, 30, "Button 1", COLOR_LIGHTGRAY);
    draw_button(win, 240, 170, 100, 30, "Button 2", COLOR_LIGHTGRAY);

    // Draw a frame
    gui_draw_rect(win, 20, 220, 360, 60, COLOR_WHITE);
    gui_draw_text(win, 40, 235, "This is a GUI test window", COLOR_WHITE);
    gui_draw_text(win, 40, 255, "Move the mouse to test cursor!", COLOR_WHITE);

    // Present to screen
    printf("Presenting window...\n");
    gui_present(win);

    printf("Window displayed! Press Ctrl+C to exit.\n");

    // Event loop with event testing
    int event_count = 0;
    int running = 1;

    printf("Entering event loop. Click in the window!\n");

    while (running)
    {
        // Poll for events
        gui_event_t event;
        if (gui_poll_event(win, &event) == 0)
        {
            event_count++;

            // Clear event area and show event info
            gui_fill_rect(win, 20, 220, 360, 60, COLOR_BLUE);
            gui_draw_rect(win, 20, 220, 360, 60, COLOR_WHITE);

            char buf[64];

            switch (event.type)
            {
                case GUI_EVENT_MOUSE:
                    if (event.mouse.event_type == 0)
                    {
                        // Mouse move - show position
                        snprintf(buf, sizeof(buf), "Mouse Move: %d, %d",
                                 event.mouse.x, event.mouse.y);
                    }
                    else if (event.mouse.event_type == 1)
                    {
                        // Button down
                        snprintf(buf, sizeof(buf), "Mouse Down: btn=%d at %d,%d",
                                 event.mouse.button, event.mouse.x, event.mouse.y);
                    }
                    else if (event.mouse.event_type == 2)
                    {
                        // Button up
                        snprintf(buf, sizeof(buf), "Mouse Up: btn=%d at %d,%d",
                                 event.mouse.button, event.mouse.x, event.mouse.y);
                    }
                    gui_draw_text(win, 40, 235, buf, COLOR_WHITE);
                    break;

                case GUI_EVENT_KEY:
                    snprintf(buf, sizeof(buf), "Key %s: code=%d mod=0x%02x",
                             event.key.pressed ? "Down" : "Up",
                             event.key.keycode, event.key.modifiers);
                    gui_draw_text(win, 40, 235, buf, COLOR_GREEN);
                    break;

                case GUI_EVENT_FOCUS:
                    snprintf(buf, sizeof(buf), "Focus: %s",
                             event.focus.gained ? "gained" : "lost");
                    gui_draw_text(win, 40, 235, buf, COLOR_YELLOW);
                    break;

                case GUI_EVENT_CLOSE:
                    gui_draw_text(win, 40, 235, "Close requested!", COLOR_RED);
                    gui_present(win);
                    running = 0;
                    break;

                default:
                    snprintf(buf, sizeof(buf), "Event type: %d", event.type);
                    gui_draw_text(win, 40, 235, buf, COLOR_WHITE);
                    break;
            }

            // Show event count
            snprintf(buf, sizeof(buf), "Events: %d", event_count);
            gui_draw_text(win, 40, 255, buf, COLOR_WHITE);

            gui_present(win);
        }

        // Yield to other processes
        __asm__ volatile("mov x8, #0x0E\n\t"  // SYS_YIELD
                         "svc #0"
                         ::: "x8");
    }

    // Clean up
    printf("Cleaning up...\n");
    gui_destroy_window(win);
    gui_shutdown();

    printf("Done.\n");
    return 0;
}
