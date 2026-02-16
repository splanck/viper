/**
 * @file gui.h
 * @brief ViperDOS GUI client library public API.
 *
 * @details
 * Provides a simple C API for creating windows and handling events.
 * Communicates with displayd via IPC channels.
 */

#ifndef VIPERDOS_GUI_H
#define VIPERDOS_GUI_H

#include <stddef.h>
#include <stdint.h>

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
typedef enum {
    GUI_EVENT_NONE = 0,
    GUI_EVENT_KEY,
    GUI_EVENT_MOUSE,
    GUI_EVENT_FOCUS,
    GUI_EVENT_RESIZE,
    GUI_EVENT_CLOSE,
    GUI_EVENT_SCROLL,
    GUI_EVENT_MENU, ///< Global menu bar item selected (Amiga/Mac style)
} gui_event_type_t;

/**
 * @brief Keyboard event data.
 */
typedef struct {
    uint16_t keycode;  ///< Linux evdev keycode
    uint8_t modifiers; ///< Modifier keys (Shift=1, Ctrl=2, Alt=4)
    uint8_t pressed;   ///< 1 = key down, 0 = key up
} gui_key_event_t;

/**
 * @brief Mouse event data.
 */
typedef struct {
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
typedef struct {
    uint8_t gained; ///< 1 = gained focus, 0 = lost focus
    uint8_t _pad[3];
} gui_focus_event_t;

/**
 * @brief Resize event data.
 */
typedef struct {
    uint32_t width;
    uint32_t height;
} gui_resize_event_t;

/**
 * @brief Scroll event data.
 */
typedef struct {
    int32_t position; ///< New scroll position in pixels
    uint8_t vertical; ///< 1 = vertical, 0 = horizontal
    uint8_t _pad[3];
} gui_scroll_event_t;

/**
 * @brief Menu event data (Amiga/Mac style global menu bar).
 */
typedef struct {
    uint8_t menu_index; ///< Which menu (0 = first menu)
    uint8_t item_index; ///< Which item in that menu
    uint8_t action;     ///< Action code from gui_menu_item_t
    uint8_t _pad;
} gui_menu_event_t;

/**
 * @brief Event union structure.
 */
typedef struct {
    gui_event_type_t type;

    union {
        gui_key_event_t key;
        gui_mouse_event_t mouse;
        gui_focus_event_t focus;
        gui_resize_event_t resize;
        gui_scroll_event_t scroll;
        gui_menu_event_t menu;
    };
} gui_event_t;

/**
 * @brief Display information.
 */
typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t format; ///< Pixel format (XRGB8888 = 0x34325258)
} gui_display_info_t;

/**
 * @brief Window information for window list.
 */
typedef struct {
    uint32_t surface_id;
    uint8_t minimized;
    uint8_t maximized;
    uint8_t focused;
    uint8_t _pad;
    char title[64];
} gui_window_info_t;

/**
 * @brief Window list structure.
 */
typedef struct {
    uint32_t count;
    gui_window_info_t windows[16];
} gui_window_list_t;

/**
 * @brief Surface creation flags.
 */
typedef enum {
    GUI_FLAG_NONE = 0,
    GUI_FLAG_SYSTEM = 1,         ///< System surface (taskbar) - not in window list
    GUI_FLAG_NO_DECORATIONS = 2, ///< No title bar or borders
} gui_surface_flags_t;

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
 * @brief Create a new window with flags.
 * @param title Window title (max 63 chars).
 * @param width Window width in pixels.
 * @param height Window height in pixels.
 * @param flags Surface creation flags.
 * @return Window handle on success, NULL on failure.
 */
gui_window_t *gui_create_window_ex(const char *title,
                                   uint32_t width,
                                   uint32_t height,
                                   uint32_t flags);

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

/**
 * @brief Get list of all windows (for taskbar).
 * @param list Output window list structure.
 * @return 0 on success, negative error code on failure.
 */
int gui_list_windows(gui_window_list_t *list);

/**
 * @brief Restore and focus a window by surface ID.
 * @param surface_id Surface ID from window list.
 * @return 0 on success, negative error code on failure.
 */
int gui_restore_window(uint32_t surface_id);

/**
 * @brief Set window position.
 * @param win Window handle.
 * @param x X position.
 * @param y Y position.
 */
void gui_set_position(gui_window_t *win, int32_t x, int32_t y);

// =============================================================================
// Scrollbar Support
// =============================================================================

/**
 * @brief Configure vertical scrollbar for a window.
 * @param win Window handle.
 * @param content_height Total content height in pixels.
 * @param viewport_height Visible area height in pixels.
 * @param scroll_pos Current scroll position (0 = top).
 *
 * Set content_height to 0 to disable the scrollbar.
 */
void gui_set_vscrollbar(gui_window_t *win,
                        int32_t content_height,
                        int32_t viewport_height,
                        int32_t scroll_pos);

/**
 * @brief Configure horizontal scrollbar for a window.
 * @param win Window handle.
 * @param content_width Total content width in pixels.
 * @param viewport_width Visible area width in pixels.
 * @param scroll_pos Current scroll position (0 = left).
 *
 * Set content_width to 0 to disable the scrollbar.
 */
void gui_set_hscrollbar(gui_window_t *win,
                        int32_t content_width,
                        int32_t viewport_width,
                        int32_t scroll_pos);

// =============================================================================
// Global Menu Bar (Amiga/Mac style)
// =============================================================================

/// Maximum menus per window (File, Edit, View, etc.)
#define GUI_MAX_MENUS 8

/// Maximum items per menu
#define GUI_MAX_MENU_ITEMS 16

/**
 * @brief Menu item definition.
 */
typedef struct {
    char label[32];    ///< Display text (empty or "-" = separator)
    char shortcut[16]; ///< Keyboard shortcut text (e.g., "Ctrl+S")
    uint8_t action;    ///< Action code returned in gui_menu_event_t (0 = disabled/separator)
    uint8_t enabled;   ///< 1 = enabled, 0 = disabled (grayed out)
    uint8_t checked;   ///< 1 = show checkmark, 0 = no checkmark
    uint8_t _pad;
} gui_menu_item_t;

/**
 * @brief Menu definition (one pulldown menu like "File" or "Edit").
 */
typedef struct {
    char title[24];     ///< Menu title shown in menu bar
    uint8_t item_count; ///< Number of items in this menu
    uint8_t _pad[3];
    gui_menu_item_t items[GUI_MAX_MENU_ITEMS]; ///< Menu items
} gui_menu_def_t;

/**
 * @brief Set the global menu bar for a window.
 *
 * When this window has focus, these menus appear in the global menu bar
 * at the top of the screen (Amiga/Mac style).
 *
 * @param win Window handle.
 * @param menus Array of menu definitions.
 * @param menu_count Number of menus (0 to clear, max GUI_MAX_MENUS).
 * @return 0 on success, negative error code on failure.
 *
 * Example:
 * @code
 *   gui_menu_def_t menus[2];
 *
 *   // File menu
 *   strcpy(menus[0].title, "File");
 *   menus[0].item_count = 3;
 *   strcpy(menus[0].items[0].label, "New");
 *   strcpy(menus[0].items[0].shortcut, "Ctrl+N");
 *   menus[0].items[0].action = 1;
 *   menus[0].items[0].enabled = 1;
 *   // ... more items ...
 *
 *   gui_set_menu(win, menus, 2);
 * @endcode
 */
int gui_set_menu(gui_window_t *win, const gui_menu_def_t *menus, uint8_t menu_count);

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
 *
 * This is synchronous - waits for displayd to acknowledge.
 */
void gui_present(gui_window_t *win);

/**
 * @brief Present window content to screen asynchronously (fire-and-forget).
 * @param win Window handle.
 *
 * Does not wait for displayd to respond. Use this for better performance
 * when you don't need to synchronize with the display.
 */
int gui_present_async(gui_window_t *win);

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
 * @brief Request keyboard focus for this window.
 * @param win Window handle.
 * @return 0 on success, -1 on failure.
 */
int gui_request_focus(gui_window_t *win);

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
void gui_fill_rect(
    gui_window_t *win, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);

/**
 * @brief Draw a rectangle outline.
 * @param win Window handle.
 * @param x X position.
 * @param y Y position.
 * @param w Width.
 * @param h Height.
 * @param color XRGB8888 color value.
 */
void gui_draw_rect(
    gui_window_t *win, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);

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
void gui_draw_text(gui_window_t *win, uint32_t x, uint32_t y, const char *text, uint32_t color);

/**
 * @brief Draw a single character with foreground and background colors.
 * @param win Window handle.
 * @param x X position.
 * @param y Y position.
 * @param c Character to draw.
 * @param fg Foreground (text) color in XRGB8888 format.
 * @param bg Background color in XRGB8888 format.
 *
 * Uses built-in 8x8 bitmap font. Draws an 8x8 pixel cell.
 */
void gui_draw_char(gui_window_t *win, uint32_t x, uint32_t y, char c, uint32_t fg, uint32_t bg);

/**
 * @brief Draw a scaled character from the built-in 8x8 font.
 * @param win Window handle.
 * @param x X position of top-left corner.
 * @param y Y position of top-left corner.
 * @param c ASCII character to draw.
 * @param fg Foreground (text) color in XRGB8888 format.
 * @param bg Background color in XRGB8888 format.
 * @param scale Scale factor (1 = 8x8, 2 = 16x16, etc.)
 *
 * Uses built-in 8x8 bitmap font scaled by the given factor.
 */
void gui_draw_char_scaled(
    gui_window_t *win, uint32_t x, uint32_t y, char c, uint32_t fg, uint32_t bg, uint32_t scale);

/**
 * @brief Draw a horizontal line.
 * @param win Window handle.
 * @param x1 Start X position.
 * @param x2 End X position.
 * @param y Y position.
 * @param color XRGB8888 color value.
 */
void gui_draw_hline(gui_window_t *win, uint32_t x1, uint32_t x2, uint32_t y, uint32_t color);

/**
 * @brief Draw a vertical line.
 * @param win Window handle.
 * @param x X position.
 * @param y1 Start Y position.
 * @param y2 End Y position.
 * @param color XRGB8888 color value.
 */
void gui_draw_vline(gui_window_t *win, uint32_t x, uint32_t y1, uint32_t y2, uint32_t color);

#ifdef __cplusplus
}
#endif

#endif // VIPERDOS_GUI_H
