//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
/**
 * @file display_protocol.hpp
 * @brief IPC protocol definitions for the display server (displayd).
 *
 * @details
 * Defines message types and structures for communication between clients
 * and the display server. Clients can create surfaces, present content,
 * and receive input events.
 */

#pragma once

#include <stdint.h>

namespace display_protocol {

// Message types (requests)
enum MsgType : uint32_t {
    // Requests from clients
    DISP_GET_INFO = 1,          // Query display resolution
    DISP_CREATE_SURFACE = 2,    // Create pixel buffer
    DISP_DESTROY_SURFACE = 3,   // Release surface
    DISP_PRESENT = 4,           // Composite to screen
    DISP_SET_GEOMETRY = 5,      // Move/resize surface
    DISP_SET_VISIBLE = 6,       // Show/hide surface
    DISP_SET_TITLE = 7,         // Set window title
    DISP_SUBSCRIBE_EVENTS = 10, // Get event channel
    DISP_POLL_EVENT = 11,       // Poll for pending events
    DISP_LIST_WINDOWS = 12,     // List all windows (for taskbar)
    DISP_RESTORE_WINDOW = 13,   // Restore/focus a window
    DISP_SET_SCROLLBAR = 14,    // Configure scrollbar
    DISP_SET_MENU = 15,         // Set/update menu bar for surface (Amiga/Mac style)

    // Replies
    DISP_INFO_REPLY = 0x81,
    DISP_POLL_EVENT_REPLY = 0x84,
    DISP_CREATE_SURFACE_REPLY = 0x82,
    DISP_GENERIC_REPLY = 0x83,
    DISP_LIST_WINDOWS_REPLY = 0x85,

    // Events (server -> client)
    DISP_EVENT_KEY = 0x90,
    DISP_EVENT_MOUSE = 0x91,
    DISP_EVENT_FOCUS = 0x92,
    DISP_EVENT_CLOSE = 0x93,
    DISP_EVENT_RESIZE = 0x94, // Window resized
    DISP_EVENT_SCROLL = 0x95, // Scrollbar position changed
    DISP_EVENT_MENU = 0x96,   // Menu item selected (Amiga/Mac style global menu)

    // Clipboard (via kernel syscalls SYS_CLIPBOARD_SET/GET/HAS)
    DISP_EVENT_CLIPBOARD = 0x97, // Clipboard content changed notification

    // Drag and drop (stub - reserved for future use)
    DISP_EVENT_DROP = 0x98, // Data dropped onto surface
};

// Request: Get display info
struct GetInfoRequest {
    uint32_t type; // DISP_GET_INFO
    uint32_t request_id;
};

// Reply: Display info
struct GetInfoReply {
    uint32_t type; // DISP_INFO_REPLY
    uint32_t request_id;
    int32_t status;
    uint32_t width;
    uint32_t height;
    uint32_t format; // Pixel format (XRGB8888 = 0x34325258)
};

// Request: Create surface
struct CreateSurfaceRequest {
    uint32_t type; // DISP_CREATE_SURFACE
    uint32_t request_id;
    uint32_t width;
    uint32_t height;
    uint32_t flags;
    char title[64];
};

// Reply: Create surface
struct CreateSurfaceReply {
    uint32_t type; // DISP_CREATE_SURFACE_REPLY
    uint32_t request_id;
    int32_t status; // 0 = success
    uint32_t surface_id;
    uint32_t stride; // Bytes per row
    // handle[0] = shared memory handle for pixel buffer
};

// Request: Destroy surface
struct DestroySurfaceRequest {
    uint32_t type; // DISP_DESTROY_SURFACE
    uint32_t request_id;
    uint32_t surface_id;
};

// Request: Present surface
struct PresentRequest {
    uint32_t type; // DISP_PRESENT
    uint32_t request_id;
    uint32_t surface_id;
    // Damage region (0,0,0,0 = full surface)
    uint32_t damage_x;
    uint32_t damage_y;
    uint32_t damage_w;
    uint32_t damage_h;
};

// Request: Set surface geometry
struct SetGeometryRequest {
    uint32_t type; // DISP_SET_GEOMETRY
    uint32_t request_id;
    uint32_t surface_id;
    int32_t x;
    int32_t y;
};

// Request: Set surface visibility
struct SetVisibleRequest {
    uint32_t type; // DISP_SET_VISIBLE
    uint32_t request_id;
    uint32_t surface_id;
    uint32_t visible; // 0 = hidden, 1 = visible
};

// Request: Set window title
struct SetTitleRequest {
    uint32_t type; // DISP_SET_TITLE
    uint32_t request_id;
    uint32_t surface_id;
    char title[64];
};

// Generic reply (for requests that don't need specific data)
struct GenericReply {
    uint32_t type; // DISP_GENERIC_REPLY
    uint32_t request_id;
    int32_t status;
};

// Event: Key press/release
struct KeyEvent {
    uint32_t type; // DISP_EVENT_KEY
    uint32_t surface_id;
    uint16_t keycode;  // Linux evdev code
    uint8_t modifiers; // Shift, Ctrl, Alt, etc.
    uint8_t pressed;   // 1 = down, 0 = up
};

// Event: Mouse
struct MouseEvent {
    uint32_t type; // DISP_EVENT_MOUSE
    uint32_t surface_id;
    int32_t x; // Position relative to surface
    int32_t y;
    int32_t dx; // Movement delta
    int32_t dy;
    uint8_t buttons;    // Button state bitmask
    uint8_t event_type; // 0=move, 1=button_down, 2=button_up
    uint8_t button;     // Which button changed (0=left, 1=right, 2=middle)
    uint8_t _pad;
};

// Event: Focus change
struct FocusEvent {
    uint32_t type; // DISP_EVENT_FOCUS
    uint32_t surface_id;
    uint8_t gained; // 1 = gained focus, 0 = lost
    uint8_t _pad[3];
};

// Event: Close request
struct CloseEvent {
    uint32_t type; // DISP_EVENT_CLOSE
    uint32_t surface_id;
};

// Event: Resize notification (sent when window resize completes)
struct ResizeEvent {
    uint32_t type; // DISP_EVENT_RESIZE
    uint32_t surface_id;
    uint32_t new_width;
    uint32_t new_height;
    uint32_t new_stride;
    // handle[0] = new shared memory handle for resized buffer
};

// Event: Scroll notification (sent when scrollbar is dragged)
struct ScrollEvent {
    uint32_t type; // DISP_EVENT_SCROLL
    uint32_t surface_id;
    int32_t new_position; // New scroll position in pixels
    uint8_t vertical;     // 1 = vertical, 0 = horizontal
    uint8_t _pad[3];
};

// Event: Menu item selected (Amiga/Mac style global menu bar)
struct MenuEvent {
    uint32_t type; // DISP_EVENT_MENU
    uint32_t surface_id;
    uint8_t menu_index; // Which menu (0 = first menu)
    uint8_t item_index; // Which item in that menu
    uint8_t action;     // Action code from MenuItem
    uint8_t _pad;
};

// Event: Clipboard content changed
struct ClipboardEvent {
    uint32_t type; // DISP_EVENT_CLIPBOARD
    uint32_t surface_id;
    uint32_t data_length; // Length of clipboard data (use SYS_CLIPBOARD_GET to retrieve)
};

// Event: Data dropped onto surface (stub - reserved for future use)
struct DropEvent {
    uint32_t type; // DISP_EVENT_DROP
    uint32_t surface_id;
    int32_t x; // Drop position relative to surface
    int32_t y;
    uint32_t data_length; // Length of dropped data
    uint8_t data_type;    // 0=text, 1=file_path
    uint8_t _pad[3];
};

// Request: Configure scrollbar
struct SetScrollbarRequest {
    uint32_t type; // DISP_SET_SCROLLBAR
    uint32_t request_id;
    uint32_t surface_id;
    uint8_t vertical; // 1 = vertical, 0 = horizontal
    uint8_t enabled;  // 1 = show scrollbar, 0 = hide
    uint8_t _pad[2];
    int32_t content_size;  // Total content size in pixels
    int32_t viewport_size; // Visible area size in pixels
    int32_t scroll_pos;    // Current scroll position
};

// Request: Subscribe to events (sets up event channel)
struct SubscribeEventsRequest {
    uint32_t type; // DISP_SUBSCRIBE_EVENTS
    uint32_t request_id;
    uint32_t surface_id;
    // handle[0] = event channel (write endpoint) for displayd to push events
};

// Reply: Subscribe events
struct SubscribeEventsReply {
    uint32_t type; // DISP_GENERIC_REPLY
    uint32_t request_id;
    int32_t status; // 0 = success
};

// Request: Poll for events
struct PollEventRequest {
    uint32_t type; // DISP_POLL_EVENT
    uint32_t request_id;
    uint32_t surface_id;
};

// Reply: Poll event result
struct PollEventReply {
    uint32_t type; // DISP_POLL_EVENT_REPLY
    uint32_t request_id;
    int32_t has_event; // 1 = event available, 0 = no event
    // Event data (if has_event == 1)
    uint32_t event_type; // MsgType (DISP_EVENT_KEY, DISP_EVENT_MOUSE, etc.)

    union {
        KeyEvent key;
        MouseEvent mouse;
        FocusEvent focus;
        CloseEvent close;
        ResizeEvent resize;
        ScrollEvent scroll;
        MenuEvent menu;
        ClipboardEvent clipboard;
        DropEvent drop;
    };
};

// Surface flags for create
enum SurfaceFlags : uint32_t {
    SURFACE_FLAG_NONE = 0,
    SURFACE_FLAG_SYSTEM = 1,         // System surface (taskbar, etc.) - not in window list
    SURFACE_FLAG_NO_DECORATIONS = 2, // No title bar or borders
};

// Window info for list response
struct WindowInfo {
    uint32_t surface_id;
    uint32_t flags; // SurfaceFlags
    uint8_t minimized;
    uint8_t maximized;
    uint8_t focused;
    uint8_t _pad;
    char title[64];
};

// Request: List windows (for taskbar)
struct ListWindowsRequest {
    uint32_t type; // DISP_LIST_WINDOWS
    uint32_t request_id;
};

// Reply: List windows
struct ListWindowsReply {
    uint32_t type; // DISP_LIST_WINDOWS_REPLY
    uint32_t request_id;
    int32_t status;
    uint32_t window_count;
    WindowInfo windows[16]; // Max 16 windows in response
};

// Request: Restore/focus a window
struct RestoreWindowRequest {
    uint32_t type; // DISP_RESTORE_WINDOW
    uint32_t request_id;
    uint32_t surface_id;
};

//===----------------------------------------------------------------------===//
// Global Menu Bar Protocol (Amiga/Mac style)
//===----------------------------------------------------------------------===//

/// Maximum menus per application (File, Edit, View, etc.)
constexpr uint32_t MAX_MENUS = 8;

/// Maximum items per menu
constexpr uint32_t MAX_MENU_ITEMS = 16;

/// Menu item definition
struct MenuItem {
    char label[32];    ///< Display text (empty string = separator)
    char shortcut[16]; ///< Keyboard shortcut text (e.g., "Ctrl+S")
    uint8_t action;    ///< Action code returned in MenuEvent (0 = disabled/separator)
    uint8_t enabled;   ///< 1 = enabled, 0 = disabled (grayed out)
    uint8_t checked;   ///< 1 = show checkmark, 0 = no checkmark
    uint8_t _pad;
};

/// Menu definition (one pulldown menu like "File" or "Edit")
struct MenuDef {
    char title[24];     ///< Menu title shown in menu bar
    uint8_t item_count; ///< Number of items in this menu
    uint8_t _pad[3];
    MenuItem items[MAX_MENU_ITEMS]; ///< Menu items
};

/// Request: Set menu bar for a surface
/// When this surface has focus, these menus appear in the global menu bar
struct SetMenuRequest {
    uint32_t type; ///< DISP_SET_MENU
    uint32_t request_id;
    uint32_t surface_id;
    uint8_t menu_count; ///< Number of menus (0 = clear menus)
    uint8_t _pad[3];
    MenuDef menus[MAX_MENUS];
};

// Maximum message payload size
// SetMenuRequest is ~6900 bytes due to menu item data, so we need a larger buffer
constexpr size_t MAX_PAYLOAD = 8192;

} // namespace display_protocol
