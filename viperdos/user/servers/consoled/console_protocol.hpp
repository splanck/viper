//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
/**
 * @file console_protocol.hpp
 * @brief IPC protocol definitions for the console server.
 *
 * @details
 * This header defines the message formats used for communication between
 * clients and the consoled server. The console server provides:
 * - Text output to the graphics console (ramfb)
 * - Cursor position control
 * - Color settings
 * - Screen clearing
 */

#pragma once

#include <stdint.h>

namespace console_protocol {

// Maximum payload size for IPC messages
constexpr size_t MAX_PAYLOAD = 4096;

// Message types (requests)
constexpr uint32_t CON_WRITE = 0x1001;       // Write text to console
constexpr uint32_t CON_CLEAR = 0x1002;       // Clear the screen
constexpr uint32_t CON_SET_CURSOR = 0x1003;  // Set cursor position
constexpr uint32_t CON_GET_CURSOR = 0x1004;  // Get cursor position
constexpr uint32_t CON_SET_COLORS = 0x1005;  // Set foreground/background
constexpr uint32_t CON_GET_SIZE = 0x1006;    // Get console dimensions
constexpr uint32_t CON_SHOW_CURSOR = 0x1007; // Show text cursor
constexpr uint32_t CON_HIDE_CURSOR = 0x1008; // Hide text cursor
constexpr uint32_t CON_CONNECT = 0x1009;     // Client connects with input channel

// Events (consoled -> client)
constexpr uint32_t CON_INPUT = 0x3001; // Keyboard input event

// Reply types
constexpr uint32_t CON_WRITE_REPLY = 0x2001;
constexpr uint32_t CON_CLEAR_REPLY = 0x2002;
constexpr uint32_t CON_SET_CURSOR_REPLY = 0x2003;
constexpr uint32_t CON_GET_CURSOR_REPLY = 0x2004;
constexpr uint32_t CON_SET_COLORS_REPLY = 0x2005;
constexpr uint32_t CON_GET_SIZE_REPLY = 0x2006;
constexpr uint32_t CON_SHOW_CURSOR_REPLY = 0x2007;
constexpr uint32_t CON_HIDE_CURSOR_REPLY = 0x2008;
constexpr uint32_t CON_CONNECT_REPLY = 0x2009;

/**
 * @brief Write text request.
 */
struct WriteRequest {
    uint32_t type; // CON_WRITE
    uint32_t request_id;
    uint32_t length; // Length of text data
    uint32_t reserved;
    // Followed by text data (up to MAX_PAYLOAD - 16 bytes)
};

struct WriteReply {
    uint32_t type; // CON_WRITE_REPLY
    uint32_t request_id;
    int32_t status;   // 0 = success, < 0 = error
    uint32_t written; // Number of characters written
};

/**
 * @brief Clear screen request.
 */
struct ClearRequest {
    uint32_t type; // CON_CLEAR
    uint32_t request_id;
};

struct ClearReply {
    uint32_t type; // CON_CLEAR_REPLY
    uint32_t request_id;
    int32_t status; // 0 = success
    uint32_t reserved;
};

/**
 * @brief Set cursor position request.
 */
struct SetCursorRequest {
    uint32_t type; // CON_SET_CURSOR
    uint32_t request_id;
    uint32_t x; // Column
    uint32_t y; // Row
};

struct SetCursorReply {
    uint32_t type; // CON_SET_CURSOR_REPLY
    uint32_t request_id;
    int32_t status; // 0 = success
    uint32_t reserved;
};

/**
 * @brief Get cursor position request.
 */
struct GetCursorRequest {
    uint32_t type; // CON_GET_CURSOR
    uint32_t request_id;
};

struct GetCursorReply {
    uint32_t type; // CON_GET_CURSOR_REPLY
    uint32_t request_id;
    uint32_t x; // Current column
    uint32_t y; // Current row
};

/**
 * @brief Set colors request.
 */
struct SetColorsRequest {
    uint32_t type; // CON_SET_COLORS
    uint32_t request_id;
    uint32_t foreground; // 32-bit ARGB foreground color
    uint32_t background; // 32-bit ARGB background color
};

struct SetColorsReply {
    uint32_t type; // CON_SET_COLORS_REPLY
    uint32_t request_id;
    int32_t status; // 0 = success
    uint32_t reserved;
};

/**
 * @brief Get console size request.
 */
struct GetSizeRequest {
    uint32_t type; // CON_GET_SIZE
    uint32_t request_id;
};

struct GetSizeReply {
    uint32_t type; // CON_GET_SIZE_REPLY
    uint32_t request_id;
    uint32_t cols; // Number of columns
    uint32_t rows; // Number of rows
};

/**
 * @brief Show/hide cursor requests.
 */
struct ShowCursorRequest {
    uint32_t type; // CON_SHOW_CURSOR
    uint32_t request_id;
};

struct ShowCursorReply {
    uint32_t type; // CON_SHOW_CURSOR_REPLY
    uint32_t request_id;
    int32_t status; // 0 = success
    uint32_t reserved;
};

struct HideCursorRequest {
    uint32_t type; // CON_HIDE_CURSOR
    uint32_t request_id;
};

struct HideCursorReply {
    uint32_t type; // CON_HIDE_CURSOR_REPLY
    uint32_t request_id;
    int32_t status; // 0 = success
    uint32_t reserved;
};

/**
 * @brief Connect request - establishes bidirectional channel.
 *
 * Client sends this with a channel handle for receiving input events.
 * The handle should be a send endpoint that consoled can use to send
 * keyboard input back to the client.
 */
struct ConnectRequest {
    uint32_t type; // CON_CONNECT
    uint32_t request_id;
    // handle[0] = send endpoint for input events (client keeps recv)
};

struct ConnectReply {
    uint32_t type; // CON_CONNECT_REPLY
    uint32_t request_id;
    int32_t status; // 0 = success
    uint32_t cols;  // Console columns
    uint32_t rows;  // Console rows
};

/**
 * @brief Input event - keyboard input from consoled to client.
 */
struct InputEvent {
    uint32_t type;     // CON_INPUT
    char ch;           // ASCII character (0 if special key)
    uint8_t pressed;   // 1 = key down, 0 = key up
    uint16_t keycode;  // Raw evdev keycode
    uint8_t modifiers; // Shift=1, Ctrl=2, Alt=4
    uint8_t _pad[3];
};

} // namespace console_protocol
