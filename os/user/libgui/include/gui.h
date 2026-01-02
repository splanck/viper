/**
 * @file gui.h
 * @brief ViperOS GUI client library public API.
 *
 * @details
 * Provides a simple C API for creating windows and handling events.
 * Communicates with displayd via IPC channels.
 */

#ifndef VIPEROS_GUI_H
#define VIPEROS_GUI_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Opaque window handle.
 */
typedef struct gui_window gui_window_t;

/**
 * @brief Event types returned by gui_poll_event/gui_wait_event.
 */
typedef enum
{
    GUI_EVENT_NONE = 0,
    GUI_EVENT_KEY,
    GUI_EVENT_MOUSE,
    GUI_EVENT_FOCUS,
    GUI_EVENT_RESIZE,
    GUI_EVENT_CLOSE,
} gui_event_type_t;

/**
 * @brief Keyboard event data.
 */
typedef struct
{
    uint16_t keycode;   ///< Linux evdev keycode
    uint8_t modifiers;  ///< Modifier keys (Shift=1, Ctrl=2, Alt=4)
    uint8_t pressed;    ///< 1 = key down, 0 = key up
} gui_key_event_t;

/**
 * @brief Mouse event data.
 */
typedef struct
{
    int32_t x;          ///< X position relative to window
    int32_t y;          ///< Y position relative to window
    int32_t dx;         ///< X movement delta
    int32_t dy;         ///< Y movement delta
    uint8_t buttons;    ///< Button state (bit0=left, bit1=right, bit2=middle)
    uint8_t event_type; ///< 0=move, 1=button_down, 2=button_up
    uint8_t button;     ///< Which button changed
    uint8_t _pad;
} gui_mouse_event_t;

/**
 * @brief Focus event data.
 */
typedef struct
{
    uint8_t gained;     ///< 1 = gained focus, 0 = lost focus
    uint8_t _pad[3];
} gui_focus_event_t;

/**
 * @brief Resize event data.
 */
typedef struct
{
    uint32_t width;
    uint32_t height;
} gui_resize_event_t;

/**
 * @brief Event union structure.
 */
typedef struct
{
    gui_event_type_t type;
    union
    {
        gui_key_event_t key;
        gui_mouse_event_t mouse;
        gui_focus_event_t focus;
        gui_resize_event_t resize;
    };
} gui_event_t;

/**
 * @brief Display information.
 */
typedef struct
{
    uint32_t width;
    uint32_t height;
    uint32_t format;    ///< Pixel format (XRGB8888 = 0x34325258)
} gui_display_info_t;

// =============================================================================
// Initialization
// =============================================================================

/**
 * @brief Initialize the GUI library and connect to displayd.
 * @return 0 on success, negative error code on failure.
 */
int gui_init(void);

/**
 * @brief Shut down the GUI library and disconnect from displayd.
 */
void gui_shutdown(void);

/**
 * @brief Get display information.
 * @param info Output structure for display info.
 * @return 0 on success, negative error code on failure.
 */
int gui_get_display_info(gui_display_info_t *info);

// =============================================================================
// Window Management
// =============================================================================

/**
 * @brief Create a new window.
 * @param title Window title (max 63 chars).
 * @param width Window width in pixels.
 * @param height Window height in pixels.
 * @return Window handle on success, NULL on failure.
 */
gui_window_t *gui_create_window(const char *title, uint32_t width, uint32_t height);

/**
 * @brief Destroy a window and release its resources.
 * @param win Window handle.
 */
void gui_destroy_window(gui_window_t *win);

/**
 * @brief Set window title.
 * @param win Window handle.
 * @param title New title string.
 */
void gui_set_title(gui_window_t *win, const char *title);

/**
 * @brief Get window title.
 * @param win Window handle.
 * @return Pointer to title string.
 */
const char *gui_get_title(gui_window_t *win);

// =============================================================================
// Pixel Buffer Access
// =============================================================================

/**
 * @brief Get direct pointer to window's pixel buffer.
 * @param win Window handle.
 * @return Pointer to XRGB8888 pixel array, or NULL on error.
 *
 * The pixel buffer uses XRGB8888 format (0xAARRGGBB).
 * Stride is width * 4 bytes.
 */
uint32_t *gui_get_pixels(gui_window_t *win);

/**
 * @brief Get window content width.
 * @param win Window handle.
 * @return Width in pixels.
 */
uint32_t gui_get_width(gui_window_t *win);

/**
 * @brief Get window content height.
 * @param win Window handle.
 * @return Height in pixels.
 */
uint32_t gui_get_height(gui_window_t *win);

/**
 * @brief Get window pixel buffer stride.
 * @param win Window handle.
 * @return Stride in bytes (width * 4 for XRGB8888).
 */
uint32_t gui_get_stride(gui_window_t *win);

// =============================================================================
// Display Update
// =============================================================================

/**
 * @brief Present window content to screen (full surface).
 * @param win Window handle.
 */
void gui_present(gui_window_t *win);

/**
 * @brief Present a specific region of the window.
 * @param win Window handle.
 * @param x Region X offset.
 * @param y Region Y offset.
 * @param w Region width.
 * @param h Region height.
 */
void gui_present_region(gui_window_t *win, uint32_t x, uint32_t y, uint32_t w, uint32_t h);

// =============================================================================
// Events
// =============================================================================

/**
 * @brief Poll for an event (non-blocking).
 * @param win Window handle.
 * @param event Output event structure.
 * @return 0 if event available, -1 if no event pending.
 */
int gui_poll_event(gui_window_t *win, gui_event_t *event);

/**
 * @brief Wait for an event (blocking).
 * @param win Window handle.
 * @param event Output event structure.
 * @return 0 on success, negative error code on failure.
 */
int gui_wait_event(gui_window_t *win, gui_event_t *event);

// =============================================================================
// Drawing Helpers (Optional Convenience Functions)
// =============================================================================

/**
 * @brief Fill a rectangle with a solid color.
 * @param win Window handle.
 * @param x X position.
 * @param y Y position.
 * @param w Width.
 * @param h Height.
 * @param color XRGB8888 color value.
 */
void gui_fill_rect(gui_window_t *win, uint32_t x, uint32_t y,
                   uint32_t w, uint32_t h, uint32_t color);

/**
 * @brief Draw a rectangle outline.
 * @param win Window handle.
 * @param x X position.
 * @param y Y position.
 * @param w Width.
 * @param h Height.
 * @param color XRGB8888 color value.
 */
void gui_draw_rect(gui_window_t *win, uint32_t x, uint32_t y,
                   uint32_t w, uint32_t h, uint32_t color);

/**
 * @brief Draw text at position.
 * @param win Window handle.
 * @param x X position.
 * @param y Y position.
 * @param text Text string to draw.
 * @param color XRGB8888 color value.
 *
 * Uses built-in 8x8 bitmap font.
 */
void gui_draw_text(gui_window_t *win, uint32_t x, uint32_t y,
                   const char *text, uint32_t color);

/**
 * @brief Draw a horizontal line.
 * @param win Window handle.
 * @param x1 Start X position.
 * @param x2 End X position.
 * @param y Y position.
 * @param color XRGB8888 color value.
 */
void gui_draw_hline(gui_window_t *win, uint32_t x1, uint32_t x2,
                    uint32_t y, uint32_t color);

/**
 * @brief Draw a vertical line.
 * @param win Window handle.
 * @param x X position.
 * @param y1 Start Y position.
 * @param y2 End Y position.
 * @param color XRGB8888 color value.
 */
void gui_draw_vline(gui_window_t *win, uint32_t x, uint32_t y1,
                    uint32_t y2, uint32_t color);

#ifdef __cplusplus
}
#endif

#endif // VIPEROS_GUI_H
