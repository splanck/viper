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

namespace display_protocol
{

// Message types (requests)
enum MsgType : uint32_t
{
    // Requests from clients
    DISP_GET_INFO = 1,         // Query display resolution
    DISP_CREATE_SURFACE = 2,   // Create pixel buffer
    DISP_DESTROY_SURFACE = 3,  // Release surface
    DISP_PRESENT = 4,          // Composite to screen
    DISP_SET_GEOMETRY = 5,     // Move/resize surface
    DISP_SET_VISIBLE = 6,      // Show/hide surface
    DISP_SET_TITLE = 7,        // Set window title
    DISP_SUBSCRIBE_EVENTS = 10, // Get event channel

    // Replies
    DISP_INFO_REPLY = 0x81,
    DISP_CREATE_SURFACE_REPLY = 0x82,
    DISP_GENERIC_REPLY = 0x83,

    // Events (server -> client)
    DISP_EVENT_KEY = 0x90,
    DISP_EVENT_MOUSE = 0x91,
    DISP_EVENT_FOCUS = 0x92,
    DISP_EVENT_CLOSE = 0x93,
};

// Request: Get display info
struct GetInfoRequest
{
    uint32_t type; // DISP_GET_INFO
    uint32_t request_id;
};

// Reply: Display info
struct GetInfoReply
{
    uint32_t type; // DISP_INFO_REPLY
    uint32_t request_id;
    int32_t status;
    uint32_t width;
    uint32_t height;
    uint32_t format; // Pixel format (XRGB8888 = 0x34325258)
};

// Request: Create surface
struct CreateSurfaceRequest
{
    uint32_t type; // DISP_CREATE_SURFACE
    uint32_t request_id;
    uint32_t width;
    uint32_t height;
    uint32_t flags;
    char title[64];
};

// Reply: Create surface
struct CreateSurfaceReply
{
    uint32_t type; // DISP_CREATE_SURFACE_REPLY
    uint32_t request_id;
    int32_t status;      // 0 = success
    uint32_t surface_id;
    uint32_t stride;     // Bytes per row
    // handle[0] = shared memory handle for pixel buffer
};

// Request: Destroy surface
struct DestroySurfaceRequest
{
    uint32_t type; // DISP_DESTROY_SURFACE
    uint32_t request_id;
    uint32_t surface_id;
};

// Request: Present surface
struct PresentRequest
{
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
struct SetGeometryRequest
{
    uint32_t type; // DISP_SET_GEOMETRY
    uint32_t request_id;
    uint32_t surface_id;
    int32_t x;
    int32_t y;
};

// Request: Set surface visibility
struct SetVisibleRequest
{
    uint32_t type; // DISP_SET_VISIBLE
    uint32_t request_id;
    uint32_t surface_id;
    uint32_t visible; // 0 = hidden, 1 = visible
};

// Request: Set window title
struct SetTitleRequest
{
    uint32_t type; // DISP_SET_TITLE
    uint32_t request_id;
    uint32_t surface_id;
    char title[64];
};

// Generic reply (for requests that don't need specific data)
struct GenericReply
{
    uint32_t type; // DISP_GENERIC_REPLY
    uint32_t request_id;
    int32_t status;
};

// Event: Key press/release
struct KeyEvent
{
    uint32_t type; // DISP_EVENT_KEY
    uint32_t surface_id;
    uint16_t keycode;   // Linux evdev code
    uint8_t modifiers;  // Shift, Ctrl, Alt, etc.
    uint8_t pressed;    // 1 = down, 0 = up
};

// Event: Mouse
struct MouseEvent
{
    uint32_t type; // DISP_EVENT_MOUSE
    uint32_t surface_id;
    int32_t x;          // Position relative to surface
    int32_t y;
    int32_t dx;         // Movement delta
    int32_t dy;
    uint8_t buttons;    // Button state bitmask
    uint8_t event_type; // 0=move, 1=button_down, 2=button_up
    uint8_t button;     // Which button changed (0=left, 1=right, 2=middle)
    uint8_t _pad;
};

// Event: Focus change
struct FocusEvent
{
    uint32_t type; // DISP_EVENT_FOCUS
    uint32_t surface_id;
    uint8_t gained; // 1 = gained focus, 0 = lost
    uint8_t _pad[3];
};

// Event: Close request
struct CloseEvent
{
    uint32_t type; // DISP_EVENT_CLOSE
    uint32_t surface_id;
};

// Maximum message payload size
constexpr size_t MAX_PAYLOAD = 256;

} // namespace display_protocol
