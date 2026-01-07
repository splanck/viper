/**
 * @file taskbar.c
 * @brief ViperOS Taskbar - Desktop shell component.
 *
 * @details
 * Displays a taskbar at the bottom of the screen with:
 * - Buttons for each open window
 * - Click to restore/focus windows
 * - Visual feedback for minimized/focused state
 */

#include <gui.h>
#include <stdio.h>
#include <string.h>

// Colors
#define COLOR_TASKBAR_BG    0xFF303050  // Dark blue-gray
#define COLOR_BUTTON_BG     0xFF404060  // Lighter gray
#define COLOR_BUTTON_ACTIVE 0xFF5060A0  // Blue highlight for focused
#define COLOR_BUTTON_MIN    0xFF505070  // Dimmer for minimized
#define COLOR_TEXT          0xFFFFFFFF  // White
#define COLOR_TEXT_DIM      0xFFA0A0A0  // Gray for minimized
#define COLOR_BORDER        0xFF202030  // Dark border

// Layout
#define TASKBAR_HEIGHT      32
#define BUTTON_HEIGHT       24
#define BUTTON_MARGIN       4
#define BUTTON_WIDTH        120
#define BUTTON_SPACING      4

// State
static gui_window_t *g_taskbar = NULL;
static uint32_t g_screen_width = 800;
static uint32_t g_screen_height = 600;
static gui_window_list_t g_windows;

// Button tracking for click detection
typedef struct {
    uint32_t surface_id;
    int32_t x;
    int32_t w;
} TaskbarButton;

static TaskbarButton g_buttons[16];
static uint32_t g_button_count = 0;

// Draw a taskbar button
static void draw_button(int x, int y, int w, int h, const char *label,
                        int minimized, int focused)
{
    uint32_t bg_color = focused ? COLOR_BUTTON_ACTIVE :
                        (minimized ? COLOR_BUTTON_MIN : COLOR_BUTTON_BG);
    uint32_t text_color = minimized ? COLOR_TEXT_DIM : COLOR_TEXT;

    // Button background
    gui_fill_rect(g_taskbar, x, y, w, h, bg_color);

    // Button border
    gui_draw_rect(g_taskbar, x, y, w, h, COLOR_BORDER);

    // Truncate label to fit
    char truncated[16];
    int max_chars = (w - 8) / 8;  // 8 pixels per char
    if (max_chars > 15) max_chars = 15;

    int len = 0;
    while (label[len] && len < max_chars) {
        truncated[len] = label[len];
        len++;
    }
    truncated[len] = '\0';

    // Draw label centered
    int text_x = x + (w - len * 8) / 2;
    int text_y = y + (h - 8) / 2;
    gui_draw_text(g_taskbar, text_x, text_y, truncated, text_color);
}

// Redraw the taskbar
static void redraw_taskbar(void)
{
    if (!g_taskbar) {
        printf("[taskbar] redraw: g_taskbar is NULL!\n");
        return;
    }

    uint32_t *pixels = gui_get_pixels(g_taskbar);
    printf("[taskbar] redraw: pixels=%p, w=%u, h=%u\n",
           (void*)pixels, gui_get_width(g_taskbar), gui_get_height(g_taskbar));

    // Clear background
    gui_fill_rect(g_taskbar, 0, 0, g_screen_width, TASKBAR_HEIGHT, COLOR_TASKBAR_BG);

    // Top border line
    gui_draw_hline(g_taskbar, 0, g_screen_width - 1, 0, COLOR_BORDER);

    // Get window list
    if (gui_list_windows(&g_windows) != 0) {
        gui_present(g_taskbar);
        return;
    }

    // Draw buttons for each window
    int x = BUTTON_MARGIN;
    g_button_count = 0;

    for (uint32_t i = 0; i < g_windows.count && i < 16; i++) {
        gui_window_info_t *info = &g_windows.windows[i];

        // Calculate button width (use fixed width or remaining space)
        int btn_w = BUTTON_WIDTH;
        if (x + btn_w > (int)g_screen_width - BUTTON_MARGIN) {
            btn_w = g_screen_width - BUTTON_MARGIN - x;
            if (btn_w < 40) break;  // Too small
        }

        draw_button(x, BUTTON_MARGIN, btn_w, BUTTON_HEIGHT,
                    info->title, info->minimized, info->focused);

        // Track button position for click handling
        g_buttons[g_button_count].surface_id = info->surface_id;
        g_buttons[g_button_count].x = x;
        g_buttons[g_button_count].w = btn_w;
        g_button_count++;

        x += btn_w + BUTTON_SPACING;
    }

    gui_present(g_taskbar);
}

// Handle click on taskbar
static void handle_click(int32_t x, int32_t y)
{
    (void)y;  // Only care about x position

    for (uint32_t i = 0; i < g_button_count; i++) {
        if (x >= g_buttons[i].x && x < g_buttons[i].x + g_buttons[i].w) {
            // Clicked on this button - restore/focus the window
            gui_restore_window(g_buttons[i].surface_id);
            // Redraw after a short delay to show updated state
            for (int j = 0; j < 100; j++) {
                __asm__ volatile("mov x8, #0x0E\n\t"  // SYS_YIELD
                                 "svc #0"
                                 ::: "x8");
            }
            redraw_taskbar();
            return;
        }
    }
}

int main(void)
{
    printf("[taskbar] Starting ViperOS Taskbar\n");

    // Initialize GUI
    if (gui_init() != 0) {
        printf("[taskbar] Failed to initialize GUI\n");
        return 1;
    }

    // Get display size
    gui_display_info_t info;
    if (gui_get_display_info(&info) == 0) {
        g_screen_width = info.width;
        g_screen_height = info.height;
    }

    printf("[taskbar] Display: %ux%u\n", g_screen_width, g_screen_height);

    // Create taskbar surface (system window, no decorations)
    g_taskbar = gui_create_window_ex("Taskbar", g_screen_width, TASKBAR_HEIGHT,
                                      GUI_FLAG_SYSTEM | GUI_FLAG_NO_DECORATIONS);
    if (!g_taskbar) {
        printf("[taskbar] Failed to create taskbar window\n");
        gui_shutdown();
        return 1;
    }

    // Position at bottom of screen
    int32_t taskbar_y = (int32_t)(g_screen_height - TASKBAR_HEIGHT);
    printf("[taskbar] Setting position to 0, %d\n", taskbar_y);
    gui_set_position(g_taskbar, 0, taskbar_y);

    // Initial draw
    printf("[taskbar] Drawing taskbar...\n");
    redraw_taskbar();
    printf("[taskbar] Taskbar running\n");

    // Event loop
    int update_counter = 0;
    while (1) {
        gui_event_t event;
        if (gui_poll_event(g_taskbar, &event) == 0) {
            switch (event.type) {
                case GUI_EVENT_MOUSE:
                    if (event.mouse.event_type == 1) {  // Button down
                        handle_click(event.mouse.x, event.mouse.y);
                    }
                    break;

                case GUI_EVENT_CLOSE:
                    // Don't close the taskbar
                    break;

                default:
                    break;
            }
        }

        // Periodically refresh the window list
        update_counter++;
        if (update_counter >= 500) {  // Every ~500 yields (reduced frequency)
            redraw_taskbar();
            update_counter = 0;
        }

        // Yield to other processes
        __asm__ volatile("mov x8, #0x0E\n\t"  // SYS_YIELD
                         "svc #0"
                         ::: "x8");
    }

    gui_destroy_window(g_taskbar);
    gui_shutdown();
    return 0;
}
