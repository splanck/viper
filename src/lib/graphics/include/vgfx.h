//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// ViperGFX Public API
//
// Provides a cross-platform software 2D graphics library with window
// management, pixel operations, drawing primitives, and event handling.
// The library implements a simple immediate-mode API where all drawing
// operations modify a software framebuffer that gets blitted to the native
// window surface on vgfx_update().
//
// Key design principles:
// - Pure software rendering (no GPU acceleration required)
// - Platform abstraction layer isolates OS-specific windowing code
// - Integer-only math for predictable, deterministic rendering
// - Direct framebuffer access for maximum flexibility
// - Lock-free SPSC event queue for thread-safe input handling
//
// Supported platforms:
// - macOS (Cocoa/AppKit backend)
// - Linux (X11 backend - stub)
// - Windows (Win32 backend - stub)
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Cross-platform software 2D graphics library public API.
/// @details Exposes window lifecycle management, drawing primitives (lines,
///          rectangles, circles), pixel operations, input polling, and event
///          handling.  All functions are safe to call from a single thread
///          (typically the main thread).  The platform backend handles OS
///          event translation and window rendering asynchronously.

#pragma once

#include "vgfx_config.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    //===----------------------------------------------------------------------===//
    // Library Version
    //===----------------------------------------------------------------------===//

#define VGFX_VERSION_MAJOR 1
#define VGFX_VERSION_MINOR 0
#define VGFX_VERSION_PATCH 0

    /// @brief Query the runtime library version as a packed integer.
    /// @details Encodes the version as (major << 16) | (minor << 8) | patch.
    ///          Useful for runtime version checks and compatibility assertions.
    /// @return Packed version number (e.g., 0x010000 for version 1.0.0).
    uint32_t vgfx_version(void);

    /// @brief Get the library version as a human-readable string.
    /// @details Returns a static string in the format "major.minor.patch".
    ///          The string is owned by the library and remains valid for the
    ///          lifetime of the process.
    /// @return Version string (e.g., "1.0.0"), never NULL.
    const char *vgfx_version_string(void);

    //===----------------------------------------------------------------------===//
    // Core Data Types
    //===----------------------------------------------------------------------===//

    /// @brief Opaque handle to a platform window.
    /// @details Internally points to a vgfx_window structure that contains the
    ///          framebuffer, event queue, input state, and platform-specific data.
    ///          All API functions accept this handle as their first parameter.
    ///          Windows are created via vgfx_create_window() and destroyed via
    ///          vgfx_destroy_window().
    typedef struct vgfx_window *vgfx_window_t;

    /// @brief 24-bit RGB color encoded in a 32-bit integer: 0x00RRGGBB.
    /// @details The high byte is ignored.  Colors are internally converted to
    ///          32-bit RGBA with alpha = 0xFF (fully opaque) when written to the
    ///          framebuffer.  Use the vgfx_rgb() helper or predefined constants
    ///          (VGFX_RED, VGFX_GREEN, etc.) for convenience.
    typedef uint32_t vgfx_color_t;

    /// @brief Window creation parameters.
    /// @details Configures the initial size, title, frame rate, and resizability
    ///          of a new window.  Invalid or zero values for width/height are
    ///          replaced with VGFX_DEFAULT_WIDTH and VGFX_DEFAULT_HEIGHT.
    typedef struct
    {
        int32_t width;     ///< Window width in pixels (≤ 0 → use default)
        int32_t height;    ///< Window height in pixels (≤ 0 → use default)
        const char *title; ///< Window title (UTF-8 string; NULL → use default)
        int32_t fps;       ///< Target FPS (< 0: unlimited, 0: default, > 0: limit)
        int32_t resizable; ///< 0 = fixed size, non-zero = user-resizable
    } vgfx_window_params_t;

    /// @brief Construct default window parameters.
    /// @details Returns a parameter struct initialised to sensible defaults:
    ///          width = VGFX_DEFAULT_WIDTH, height = VGFX_DEFAULT_HEIGHT,
    ///          title = VGFX_DEFAULT_TITLE, fps = VGFX_DEFAULT_FPS, resizable = 0.
    /// @return Default-initialised vgfx_window_params_t.
    vgfx_window_params_t vgfx_window_params_default(void);

    /// @brief Framebuffer descriptor for direct pixel access.
    /// @details Provides raw access to the RGBA pixel buffer.  Each pixel is 4
    ///          bytes (RGBA order, 8 bits per channel).  The stride is always
    ///          width * 4.  Pixels are stored in row-major order with (0, 0) at
    ///          the top-left corner.
    typedef struct
    {
        uint8_t *pixels; ///< RGBA pixel data (4 bytes per pixel)
        int32_t width;   ///< Framebuffer width in pixels
        int32_t height;  ///< Framebuffer height in pixels
        int32_t stride;  ///< Bytes per row (always width * 4)
    } vgfx_framebuffer_t;

    /// @brief Logging callback function type.
    /// @details When a log callback is installed via vgfx_set_log_callback, the
    ///          library forwards human-readable diagnostic messages to the client
    ///          for display or capture. The callback must be thread-safe.
    typedef void (*vgfx_log_fn)(const char *msg);

    //===----------------------------------------------------------------------===//
    // Event System
    //===----------------------------------------------------------------------===//

    /// @brief Event type enumeration.
    /// @details Identifies the kind of event in a vgfx_event_t structure.
    ///          Events are generated by the platform backend and placed in a
    ///          lock-free SPSC ring buffer for consumption by the application.
    typedef enum
    {
        VGFX_EVENT_NONE = 0,     ///< No event (queue empty)
        VGFX_EVENT_KEY_DOWN,     ///< Keyboard key pressed
        VGFX_EVENT_KEY_UP,       ///< Keyboard key released
        VGFX_EVENT_MOUSE_MOVE,   ///< Mouse cursor moved
        VGFX_EVENT_MOUSE_DOWN,   ///< Mouse button pressed
        VGFX_EVENT_MOUSE_UP,     ///< Mouse button released
        VGFX_EVENT_RESIZE,       ///< Window resized (framebuffer reallocated)
        VGFX_EVENT_CLOSE,        ///< Window close requested by user
        VGFX_EVENT_FOCUS_GAINED, ///< Window gained keyboard focus
        VGFX_EVENT_FOCUS_LOST,   ///< Window lost keyboard focus
        VGFX_EVENT_SCROLL        ///< Scroll wheel or trackpad scroll
    } vgfx_event_type_t;

    /// @brief Keyboard key codes.
    /// @details Maps common keys to integer constants.  The encoding is designed
    ///          to be compatible with ASCII for alphanumeric keys.  Special keys
    ///          use values >= 256.  Not all keys are represented; unmapped keys
    ///          report VGFX_KEY_UNKNOWN.
    typedef enum
    {
        VGFX_KEY_UNKNOWN = 0,

        /* Printable ASCII keys (A-Z share values with uppercase ASCII) */
        VGFX_KEY_SPACE = ' ',
        VGFX_KEY_0 = '0',
        VGFX_KEY_1,
        VGFX_KEY_2,
        VGFX_KEY_3,
        VGFX_KEY_4,
        VGFX_KEY_5,
        VGFX_KEY_6,
        VGFX_KEY_7,
        VGFX_KEY_8,
        VGFX_KEY_9,
        VGFX_KEY_A = 'A',
        VGFX_KEY_B,
        VGFX_KEY_C,
        VGFX_KEY_D,
        VGFX_KEY_E,
        VGFX_KEY_F,
        VGFX_KEY_G,
        VGFX_KEY_H,
        VGFX_KEY_I,
        VGFX_KEY_J,
        VGFX_KEY_K,
        VGFX_KEY_L,
        VGFX_KEY_M,
        VGFX_KEY_N,
        VGFX_KEY_O,
        VGFX_KEY_P,
        VGFX_KEY_Q,
        VGFX_KEY_R,
        VGFX_KEY_S,
        VGFX_KEY_T,
        VGFX_KEY_U,
        VGFX_KEY_V,
        VGFX_KEY_W,
        VGFX_KEY_X,
        VGFX_KEY_Y,
        VGFX_KEY_Z,

        /* Special keys (values >= 256) */
        VGFX_KEY_ESCAPE = 256,
        VGFX_KEY_ENTER = 257,
        VGFX_KEY_LEFT = 258,
        VGFX_KEY_RIGHT = 259,
        VGFX_KEY_UP = 260,
        VGFX_KEY_DOWN = 261,
        VGFX_KEY_BACKSPACE = 262,
        VGFX_KEY_DELETE = 263,
        VGFX_KEY_TAB = 264,
        VGFX_KEY_HOME = 265,
        VGFX_KEY_END = 266
    } vgfx_key_t;

    /// @brief Keyboard modifier flags
    typedef enum
    {
        VGFX_MOD_SHIFT = 1 << 0,
        VGFX_MOD_CTRL = 1 << 1,
        VGFX_MOD_ALT = 1 << 2,
        VGFX_MOD_CMD = 1 << 3 // macOS Command key
    } vgfx_mod_t;

    /// @brief Mouse button identifiers.
    /// @details Standard three-button mouse mapping.  Additional buttons may be
    ///          added in future versions.
    typedef enum
    {
        VGFX_MOUSE_LEFT = 0,  ///< Left mouse button (primary)
        VGFX_MOUSE_RIGHT = 1, ///< Right mouse button (secondary)
        VGFX_MOUSE_MIDDLE = 2 ///< Middle mouse button (tertiary)
    } vgfx_mouse_button_t;

    /// @brief Unified event structure.
    /// @details Contains the event type, timestamp, and type-specific data in a
    ///          tagged union.  Events are retrieved via vgfx_poll_event() from a
    ///          lock-free SPSC ring buffer populated by the platform backend.
    typedef struct
    {
        vgfx_event_type_t type; ///< Event discriminator
        int64_t time_ms;        ///< Event timestamp (milliseconds since epoch)

        /// @brief Event-specific data.
        /// @details Access the appropriate member based on the event type.
        union
        {
            /// @brief Key event data (KEY_DOWN, KEY_UP).
            struct
            {
                vgfx_key_t key; ///< Key code
                int is_repeat;  ///< 1 if key repeat, 0 if initial press
                int modifiers;  ///< Modifier flags (VGFX_MOD_SHIFT, VGFX_MOD_CTRL, etc.)
            } key;

            /// @brief Mouse movement event data (MOUSE_MOVE).
            struct
            {
                int32_t x; ///< Mouse X coordinate (pixels from left edge)
                int32_t y; ///< Mouse Y coordinate (pixels from top edge)
            } mouse_move;

            /// @brief Mouse button event data (MOUSE_DOWN, MOUSE_UP).
            struct
            {
                int32_t x;                  ///< Mouse X at time of click
                int32_t y;                  ///< Mouse Y at time of click
                vgfx_mouse_button_t button; ///< Which button was pressed/released
            } mouse_button;

            /// @brief Resize event data (RESIZE).
            /// @details The framebuffer has been reallocated and cleared to black.
            struct
            {
                int32_t width;  ///< New window width
                int32_t height; ///< New window height
            } resize;

            /// @brief Scroll event data (SCROLL).
            struct
            {
                float   delta_x; ///< Horizontal scroll delta (positive = right)
                float   delta_y; ///< Vertical scroll delta (positive = down)
                int32_t x;       ///< Cursor X at time of scroll (physical pixels)
                int32_t y;       ///< Cursor Y at time of scroll (physical pixels)
            } scroll;
        } data;
    } vgfx_event_t;

    //===----------------------------------------------------------------------===//
    // Error Handling
    //===----------------------------------------------------------------------===//

    /// @brief Error code enumeration.
    /// @details Identifies the category of the last error that occurred in a
    ///          ViperGFX API call.  Error details are stored in thread-local
    ///          storage and retrieved via vgfx_get_last_error().
    typedef enum
    {
        VGFX_ERR_NONE = 0,     ///< No error
        VGFX_ERR_ALLOC,        ///< Memory allocation failed
        VGFX_ERR_PLATFORM,     ///< Platform-specific error (window creation, etc.)
        VGFX_ERR_INVALID_PARAM ///< Invalid parameter (out of range, NULL, etc.)
    } vgfx_error_t;

    /// @brief Retrieve the last error message.
    /// @details Returns a descriptive error string for the most recent failure in
    ///          the current thread.  The string is stored in thread-local storage
    ///          and remains valid until the next API call or thread termination.
    /// @return Error message string, or NULL if no error has occurred.
    const char *vgfx_get_last_error(void);

    /// @brief Clear the last error state.
    /// @details Resets the thread-local error code and message.  Useful for
    ///          recovering from non-fatal errors.
    void vgfx_clear_error(void);

    /// @brief Install or clear a logging callback.
    /// @details Pass a non-null function pointer to receive diagnostic messages;
    ///          pass NULL to disable logging callbacks.
    /// @param fn Callback function or NULL to disable.
    void vgfx_set_log_callback(vgfx_log_fn fn);

    //===----------------------------------------------------------------------===//
    // Window Management
    //===----------------------------------------------------------------------===//

    /// @brief Create a new window with the specified parameters.
    /// @details Allocates a framebuffer, initializes the event queue, and creates
    ///          a native OS window via the platform backend.  The window is
    ///          initially visible and ready for drawing.  Returns NULL on failure
    ///          (e.g., allocation failure, unsupported dimensions).
    /// @param params Configuration for the new window (width, height, title, etc.)
    /// @return Window handle on success, NULL on failure (check vgfx_get_last_error())
    vgfx_window_t vgfx_create_window(const vgfx_window_params_t *params);

    /// @brief Destroy a window and free all associated resources.
    /// @details Closes the native OS window, deallocates the framebuffer and event
    ///          queue, and frees the window structure.  The handle becomes invalid
    ///          and must not be used after this call.  It is safe to pass NULL.
    /// @param window Window handle to destroy (may be NULL)
    void vgfx_destroy_window(vgfx_window_t window);

    /// @brief Update the window display and process pending events.
    /// @details Blits the framebuffer to the native window surface, polls OS
    ///          events (translating them to ViperGFX events), and applies FPS
    ///          limiting if configured.  Must be called regularly in the main
    ///          loop to keep the window responsive.
    /// @param window Window handle
    /// @return 1 on success, 0 on fatal error
    int vgfx_update(vgfx_window_t window);

    /// @brief Get the current window dimensions.
    /// @details Retrieves the framebuffer width and height in pixels.  Dimensions
    ///          may change if the window is resizable and the user resizes it.
    /// @param window Window handle
    /// @param out_width Pointer to receive width (may be NULL)
    /// @param out_height Pointer to receive height (may be NULL)
    /// @return 1 on success, 0 if window is NULL
    int vgfx_get_size(vgfx_window_t window, int32_t *out_width, int32_t *out_height);

    /// @brief Set the target frame rate for the window.
    /// @details Configures FPS limiting for the next vgfx_update() call.  The
    ///          library will sleep to throttle rendering if frames complete faster
    ///          than the target rate.  Pass 0 to disable FPS limiting (unlimited).
    /// @param window Window handle
    /// @param fps Target FPS (< 0: unlimited, 0: unlimited, > 0: limit to fps)
    void vgfx_set_fps(vgfx_window_t window, int32_t fps);

    /// @brief Get the configured target frame rate for the window.
    /// @details Returns the current FPS setting for the window. Negative values
    ///          indicate unlimited, zero should not occur after initialization.
    /// @param window Window handle
    /// @return Current FPS setting, or -1 if window is NULL
    int32_t vgfx_get_fps(vgfx_window_t window);

    /// @brief Set the window title.
    /// @details Changes the window's title bar text at runtime. The title string
    ///          is copied internally, so the caller's string can be freed after
    ///          the call returns.
    /// @param window Window handle
    /// @param title New window title (UTF-8 string; NULL restores default)
    void vgfx_set_title(vgfx_window_t window, const char *title);

    /// @brief Register a callback invoked immediately on window resize.
    /// @details On macOS, the Cocoa live-resize modal loop blocks the main
    ///          thread while the user drags the resize handle.  Registering a
    ///          callback here allows the application to re-render on each resize
    ///          notification, preventing the window from going blank.
    ///          On other platforms the callback is stored but never called
    ///          (resize events arrive via the normal poll loop instead).
    /// @param window   Window handle
    /// @param callback Function called with (userdata, new_width, new_height)
    /// @param userdata Opaque pointer passed back to the callback
    void vgfx_set_resize_callback(vgfx_window_t window,
                                   void (*callback)(void *userdata, int32_t w, int32_t h),
                                   void *userdata);

    /// @brief Set the window to fullscreen or windowed mode.
    /// @details Toggles the window between fullscreen and windowed modes. In
    ///          fullscreen mode, the window covers the entire screen with no
    ///          title bar or borders. The framebuffer is resized to match the
    ///          screen dimensions, and a RESIZE event is generated.
    /// @param window Window handle
    /// @param fullscreen 1 for fullscreen, 0 for windowed mode
    void vgfx_set_fullscreen(vgfx_window_t window, int fullscreen);

    /// @brief Check if the window is in fullscreen mode.
    /// @param window Window handle
    /// @return 1 if fullscreen, 0 if windowed, -1 if window is NULL
    int vgfx_is_fullscreen(vgfx_window_t window);

    /// @brief Minimize (iconify) the window.
    void vgfx_minimize(vgfx_window_t window);

    /// @brief Maximize (zoom) the window.
    void vgfx_maximize(vgfx_window_t window);

    /// @brief Restore the window from minimized or maximized state.
    void vgfx_restore(vgfx_window_t window);

    /// @brief Check if the window is currently minimized.
    /// @return 1 if minimized, 0 otherwise
    int32_t vgfx_is_minimized(vgfx_window_t window);

    /// @brief Check if the window is currently maximized.
    /// @return 1 if maximized, 0 otherwise
    int32_t vgfx_is_maximized(vgfx_window_t window);

    /// @brief Get the window's current screen position.
    /// @param window Window handle
    /// @param out_x Pointer to receive X coordinate (may be NULL)
    /// @param out_y Pointer to receive Y coordinate (may be NULL)
    void vgfx_get_position(vgfx_window_t window, int32_t *out_x, int32_t *out_y);

    /// @brief Move the window to a new screen position.
    /// @param window Window handle
    /// @param x New X coordinate
    /// @param y New Y coordinate
    void vgfx_set_position(vgfx_window_t window, int32_t x, int32_t y);

    /// @brief Bring the window to the front and give it keyboard focus.
    void vgfx_focus(vgfx_window_t window);

    /// @brief Check if the window currently has keyboard focus.
    /// @return 1 if focused, 0 otherwise
    int32_t vgfx_is_focused(vgfx_window_t window);

    /// @brief Control whether clicking the close button closes the window.
    /// @param window Window handle
    /// @param prevent 1 to block close, 0 to allow
    void vgfx_set_prevent_close(vgfx_window_t window, int32_t prevent);

    /// @brief Cursor type constants for vgfx_set_cursor().
    /// DEFAULT=0, POINTER=1, TEXT=2, RESIZE_H=3, RESIZE_V=4, WAIT=5
    typedef enum
    {
        VGFX_CURSOR_DEFAULT  = 0, ///< Standard arrow cursor
        VGFX_CURSOR_POINTER  = 1, ///< Hand/pointer cursor (links, buttons)
        VGFX_CURSOR_TEXT     = 2, ///< I-beam text cursor
        VGFX_CURSOR_RESIZE_H = 3, ///< Horizontal resize cursor
        VGFX_CURSOR_RESIZE_V = 4, ///< Vertical resize cursor
        VGFX_CURSOR_WAIT     = 5  ///< Busy/spinner cursor
    } vgfx_cursor_type_t;

    /// @brief Set the mouse cursor shape.
    /// @param window Window handle
    /// @param cursor_type Cursor type (VGFX_CURSOR_* constant)
    void vgfx_set_cursor(vgfx_window_t window, int32_t cursor_type);

    /// @brief Show or hide the mouse cursor.
    /// @param window Window handle
    /// @param visible 1 to show, 0 to hide
    void vgfx_set_cursor_visible(vgfx_window_t window, int32_t visible);

    /// @brief Get the primary monitor's screen dimensions.
    /// @details Queries the size of the monitor containing the window.  Falls
    ///          back to the primary monitor when the window is NULL.
    /// @param window Window handle (may be NULL to query primary monitor)
    /// @param out_w Pointer to receive monitor width (may be NULL)
    /// @param out_h Pointer to receive monitor height (may be NULL)
    void vgfx_get_monitor_size(vgfx_window_t window, int32_t *out_w, int32_t *out_h);

    /// @brief Resize the native OS window.
    /// @details Changes the window's client area dimensions.  On macOS the
    ///          frame origin is preserved; the window may be clipped by the
    ///          screen bounds.  A RESIZE event is NOT synthesized — the caller
    ///          should update the root widget size after calling this function.
    /// @param window Window handle
    /// @param w New window width in pixels (must be > 0)
    /// @param h New window height in pixels (must be > 0)
    void vgfx_set_window_size(vgfx_window_t window, int32_t w, int32_t h);

    /// @brief Query the HiDPI backing scale factor for a window.
    /// @details Returns the ratio of physical pixels to logical points that was
    ///          applied when the window was created.  On a 2× macOS Retina display
    ///          this returns 2.0; on a standard 96 DPI display it returns 1.0.
    ///          Use this value to scale logical coordinates to physical pixels or
    ///          to adjust font/UI element sizes for crisp rendering.
    /// @param window Window handle (may be NULL → returns 1.0)
    /// @return Scale factor (≥ 1.0)
    float vgfx_window_get_scale(vgfx_window_t window);

    /// @brief Get the physical pixel width of the window framebuffer.
    /// @details Returns win->width, which equals (logical_width × scale_factor)
    ///          after vgfx_create_window().  Use for framebuffer operations.
    /// @param window Window handle
    /// @return Physical width in pixels, or 0 if window is NULL
    int32_t vgfx_window_get_width(vgfx_window_t window);

    /// @brief Get the physical pixel height of the window framebuffer.
    /// @details Returns win->height, which equals (logical_height × scale_factor)
    ///          after vgfx_create_window().  Use for framebuffer operations.
    /// @param window Window handle
    /// @return Physical height in pixels, or 0 if window is NULL
    int32_t vgfx_window_get_height(vgfx_window_t window);

    /// @brief Get direct access to the framebuffer.
    /// @details Returns a descriptor with pointers to the raw RGBA pixel data.
    ///          The framebuffer is always stored in row-major order with 4 bytes
    ///          per pixel (RGBA, 8 bits per channel).  Direct writes are visible
    ///          after the next vgfx_update() call.
    /// @param window Window handle
    /// @param out_fb Pointer to receive framebuffer descriptor
    /// @return 1 on success, 0 if window or out_fb is NULL
    int vgfx_get_framebuffer(vgfx_window_t window, vgfx_framebuffer_t *out_fb);

    //===----------------------------------------------------------------------===//
    // Clipping
    //===----------------------------------------------------------------------===//

    /// @brief Set the clipping rectangle for all drawing operations.
    /// @details When a clipping rectangle is set, all subsequent drawing operations
    ///          are constrained to render only within the specified region. Pixels
    ///          outside the clipping rectangle are not modified. The clipping region
    ///          is intersected with the window bounds.
    /// @param window Window handle
    /// @param x Left edge X coordinate of clip rect
    /// @param y Top edge Y coordinate of clip rect
    /// @param w Width of clip rect (pixels)
    /// @param h Height of clip rect (pixels)
    /// @note The clip rect persists until cleared with vgfx_clear_clip().
    /// @note A zero or negative width/height results in no drawing.
    void vgfx_set_clip(vgfx_window_t window, int32_t x, int32_t y, int32_t w, int32_t h);

    /// @brief Clear the clipping rectangle, restoring full-window drawing.
    /// @details After calling this function, drawing operations can affect any
    ///          pixel within the window bounds. Equivalent to setting the clip
    ///          rectangle to the full window size.
    /// @param window Window handle
    void vgfx_clear_clip(vgfx_window_t window);

    //===----------------------------------------------------------------------===//
    // Drawing Primitives
    //===----------------------------------------------------------------------===//

    /// @brief Clear the entire window to a solid color.
    /// @details Fills every pixel in the framebuffer with the specified color.
    ///          This is faster than drawing a filled rectangle over the entire
    ///          window due to optimized memset-style filling.
    /// @param window Window handle
    /// @param color Fill color (24-bit RGB: 0x00RRGGBB)
    void vgfx_cls(vgfx_window_t window, vgfx_color_t color);

    /// @brief Set a single pixel to a color.
    /// @details Writes directly to the framebuffer at (x, y).  Out-of-bounds
    ///          coordinates are silently ignored (no error).  Coordinates are
    ///          measured from the top-left corner (0, 0).
    /// @param window Window handle
    /// @param x X coordinate (pixels from left edge)
    /// @param y Y coordinate (pixels from top edge)
    /// @param color Pixel color (24-bit RGB)
    void vgfx_pset(vgfx_window_t window, int32_t x, int32_t y, vgfx_color_t color);

    /// @brief Plot a single pixel using source-over alpha blending.
    /// @details Composites src_color (0xAARRGGBB) over the existing framebuffer
    ///          pixel using the Porter-Duff source-over formula:
    ///            dst.rgb = src.rgb * (src.a/255) + dst.rgb * (1 - src.a/255)
    ///          If src_color is fully opaque (alpha == 0xFF), this is identical to
    ///          vgfx_pset. Pixels outside window bounds are silently discarded.
    /// @param window Window handle
    /// @param x      X coordinate (pixels from left edge)
    /// @param y      Y coordinate (pixels from top edge)
    /// @param color  Source color with alpha (0xAARRGGBB)
    void vgfx_pset_alpha(vgfx_window_t window, int32_t x, int32_t y, uint32_t color);

    /// @brief Read the color of a single pixel.
    /// @details Retrieves the current color at (x, y) from the framebuffer.
    ///          Returns 0 (failure) if coordinates are out of bounds or window is
    ///          NULL.  The output color is only written on success.
    /// @param window Window handle
    /// @param x X coordinate
    /// @param y Y coordinate
    /// @param out_color Pointer to receive pixel color
    /// @return 1 on success, 0 if out of bounds or window/out_color is NULL
    int vgfx_point(vgfx_window_t window, int32_t x, int32_t y, vgfx_color_t *out_color);

    /// @brief Draw a line from (x1, y1) to (x2, y2).
    /// @details Uses Bresenham's integer-only line algorithm for deterministic,
    ///          pixel-perfect rendering.  Pixels outside the window bounds are
    ///          clipped per-pixel (no error).  The line includes both endpoints.
    /// @param window Window handle
    /// @param x1 Starting X coordinate
    /// @param y1 Starting Y coordinate
    /// @param x2 Ending X coordinate
    /// @param y2 Ending Y coordinate
    /// @param color Line color
    void vgfx_line(
        vgfx_window_t window, int32_t x1, int32_t y1, int32_t x2, int32_t y2, vgfx_color_t color);

    /// @brief Draw a rectangle outline.
    /// @details Draws the four edges of a rectangle with top-left corner at (x, y)
    ///          and dimensions w × h.  Only the perimeter is drawn; the interior
    ///          is left unchanged.  Negative or zero dimensions are rejected.
    /// @param window Window handle
    /// @param x Left edge X coordinate
    /// @param y Top edge Y coordinate
    /// @param w Rectangle width (must be > 0)
    /// @param h Rectangle height (must be > 0)
    /// @param color Outline color
    void vgfx_rect(
        vgfx_window_t window, int32_t x, int32_t y, int32_t w, int32_t h, vgfx_color_t color);

    /// @brief Draw a filled rectangle.
    /// @details Fills a solid rectangle with top-left corner at (x, y) and
    ///          dimensions w × h.  Uses optimized scanline filling.  The rectangle
    ///          is clipped to window bounds; out-of-bounds regions are ignored.
    /// @param window Window handle
    /// @param x Left edge X coordinate
    /// @param y Top edge Y coordinate
    /// @param w Rectangle width (must be > 0)
    /// @param h Rectangle height (must be > 0)
    /// @param color Fill color
    void vgfx_fill_rect(
        vgfx_window_t window, int32_t x, int32_t y, int32_t w, int32_t h, vgfx_color_t color);

    /// @brief Draw a circle outline.
    /// @details Draws the perimeter of a circle centered at (cx, cy) with the
    ///          specified radius using the midpoint circle algorithm (8-way
    ///          symmetry, integer-only math).  Negative radius is rejected.
    /// @param window Window handle
    /// @param cx Center X coordinate
    /// @param cy Center Y coordinate
    /// @param radius Circle radius in pixels (must be >= 0)
    /// @param color Outline color
    void vgfx_circle(
        vgfx_window_t window, int32_t cx, int32_t cy, int32_t radius, vgfx_color_t color);

    /// @brief Draw a filled circle.
    /// @details Fills a solid circle centered at (cx, cy) with the specified
    ///          radius using scanline-based filling derived from the midpoint
    ///          algorithm.  Negative radius is rejected.
    /// @param window Window handle
    /// @param cx Center X coordinate
    /// @param cy Center Y coordinate
    /// @param radius Circle radius in pixels (must be >= 0)
    /// @param color Fill color
    void vgfx_fill_circle(
        vgfx_window_t window, int32_t cx, int32_t cy, int32_t radius, vgfx_color_t color);

    //===----------------------------------------------------------------------===//
    // Input Polling
    //===----------------------------------------------------------------------===//

    /// @brief Check if a keyboard key is currently pressed.
    /// @details Queries the key state array maintained by the platform backend.
    ///          Key state is updated during vgfx_update() when processing OS
    ///          events.  Returns 0 for unknown keys or NULL window.
    /// @param window Window handle
    /// @param key Key code to query
    /// @return 1 if key is pressed, 0 otherwise
    int vgfx_key_down(vgfx_window_t window, vgfx_key_t key);

    /// @brief Get the current mouse position.
    /// @details Retrieves the last reported mouse coordinates relative to the
    ///          window.  Coordinates are updated during vgfx_update().  Returns 0
    ///          if the mouse is outside the window bounds, but still writes the
    ///          coordinates (which may be negative or exceed window dimensions).
    /// @param window Window handle
    /// @param out_x Pointer to receive X coordinate (may be NULL)
    /// @param out_y Pointer to receive Y coordinate (may be NULL)
    /// @return 1 if mouse is in bounds, 0 if out of bounds or window is NULL
    int vgfx_mouse_pos(vgfx_window_t window, int32_t *out_x, int32_t *out_y);

    /// @brief Check if a mouse button is currently pressed.
    /// @details Queries the button state array maintained by the platform backend.
    ///          Button state is updated during vgfx_update() when processing OS
    ///          events.  Returns 0 for invalid buttons or NULL window.
    /// @param window Window handle
    /// @param button Button identifier (left, right, middle)
    /// @return 1 if button is pressed, 0 otherwise
    int vgfx_mouse_button(vgfx_window_t window, vgfx_mouse_button_t button);

    //===----------------------------------------------------------------------===//
    // Event Queue
    //===----------------------------------------------------------------------===//

    /// @brief Poll the next event from the queue.
    /// @details Retrieves and removes the oldest event from the lock-free SPSC
    ///          ring buffer.  Events are generated by the platform backend during
    ///          vgfx_update() and queued for application consumption.  Returns 0
    ///          if the queue is empty.
    /// @param window Window handle
    /// @param out_event Pointer to receive event data
    /// @return 1 if event was retrieved, 0 if queue is empty or window/out_event is NULL
    int vgfx_poll_event(vgfx_window_t window, vgfx_event_t *out_event);

    /// @brief Peek at the next event without removing it.
    /// @details Returns the oldest event without dequeuing it.  Useful for
    ///          inspecting event types before deciding whether to consume them.
    /// @param window Window handle
    /// @param out_event Pointer to receive event data
    /// @return 1 if event was peeked, 0 if queue is empty or window/out_event is NULL
    int vgfx_peek_event(vgfx_window_t window, vgfx_event_t *out_event);

    /// @brief Clear all events from the queue.
    /// @details Resets the event queue to empty, discarding all pending events.
    ///          Useful for ignoring accumulated input after a menu or dialog.
    /// @param window Window handle
    void vgfx_clear_events(vgfx_window_t window);

    /// @brief Get and reset the event overflow counter.
    /// @details Returns the number of events that were dropped due to queue
    ///          overflow since the last call to this function.  The counter is
    ///          reset to zero after reading.  Overflow occurs when more than
    ///          VGFX_EVENT_QUEUE_SIZE events are generated between updates.
    /// @param window Window handle
    /// @return Number of dropped events (0 if none)
    int32_t vgfx_event_overflow_count(vgfx_window_t window);

    /// @brief Check whether the window close button has been pressed.
    /// @details Returns non-zero if the platform backend received a close
    ///          request (e.g. WM_CLOSE on Win32, windowShouldClose on macOS,
    ///          WM_DELETE_WINDOW on X11).  This flag is sticky — once set it
    ///          remains set for the lifetime of the window.
    /// @param window Window handle
    /// @return Non-zero if close was requested, 0 otherwise
    int32_t vgfx_close_requested(vgfx_window_t window);

    //===----------------------------------------------------------------------===//
    // Color Utilities
    //===----------------------------------------------------------------------===//

    /// @brief Construct a color from RGB components.
    /// @details Packs 8-bit red, green, and blue components into a 24-bit color
    ///          value: 0x00RRGGBB.  Components are clamped to [0, 255].
    /// @param r Red component (0-255)
    /// @param g Green component (0-255)
    /// @param b Blue component (0-255)
    /// @return Packed color value
    static inline vgfx_color_t vgfx_rgb(uint8_t r, uint8_t g, uint8_t b)
    {
        return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
    }

/// @brief Alias for vgfx_rgb() using uppercase macro style.
#define VGFX_RGB(r, g, b) vgfx_rgb((r), (g), (b))

/* Common color constants */
#define VGFX_BLACK 0x000000
#define VGFX_WHITE 0xFFFFFF
#define VGFX_RED 0xFF0000
#define VGFX_GREEN 0x00FF00
#define VGFX_BLUE 0x0000FF
#define VGFX_YELLOW 0xFFFF00
#define VGFX_CYAN 0x00FFFF
#define VGFX_MAGENTA 0xFF00FF
#define VGFX_GRAY 0x808080

    /// @brief Decompose a packed color into RGB components.
    /// @details Writes the red, green, and blue bytes to the provided pointers
    ///          when non-NULL. Useful for debugging and UI integrations.
    /// @param color Packed color value (0x00RRGGBB)
    /// @param r Optional pointer to receive red component
    /// @param g Optional pointer to receive green component
    /// @param b Optional pointer to receive blue component
    void vgfx_color_to_rgb(vgfx_color_t color, uint8_t *r, uint8_t *g, uint8_t *b);

    //===----------------------------------------------------------------------===//
    // Clipboard Operations
    //===----------------------------------------------------------------------===//

    /// @brief Clipboard format types
    typedef enum
    {
        VGFX_CLIPBOARD_TEXT,  ///< Plain text (UTF-8)
        VGFX_CLIPBOARD_HTML,  ///< HTML formatted text
        VGFX_CLIPBOARD_IMAGE, ///< Image data (not yet supported)
        VGFX_CLIPBOARD_FILES  ///< File paths (not yet supported)
    } vgfx_clipboard_format_t;

    /// @brief Check if the clipboard contains data in the specified format.
    /// @param format Clipboard format to check for
    /// @return 1 if data is available, 0 otherwise
    int vgfx_clipboard_has_format(vgfx_clipboard_format_t format);

    /// @brief Get text from the clipboard.
    /// @details Returns a malloc'd UTF-8 string containing the clipboard text.
    ///          The caller is responsible for freeing the returned string.
    /// @return Clipboard text (caller must free), or NULL if not available
    char *vgfx_clipboard_get_text(void);

    /// @brief Set text to the clipboard.
    /// @details Copies the specified UTF-8 string to the system clipboard.
    /// @param text Text to copy (NULL clears text from clipboard)
    void vgfx_clipboard_set_text(const char *text);

    /// @brief Clear all clipboard contents.
    void vgfx_clipboard_clear(void);

#ifdef __cplusplus
}
#endif
