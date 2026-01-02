/**
 * @file input_protocol.hpp
 * @brief IPC protocol definitions for the input server (inputd).
 *
 * @details
 * Defines message types and structures for communication between clients
 * and the input server. Clients can subscribe to input events, query
 * modifier state, and poll for keyboard/mouse input.
 */

#pragma once

#include <stdint.h>

namespace input_protocol
{

// Message types
enum MsgType : uint32_t
{
    // Requests from clients
    INP_SUBSCRIBE = 1,      // Subscribe to input events
    INP_UNSUBSCRIBE = 2,    // Unsubscribe from events
    INP_GET_CHAR = 10,      // Get translated character (non-blocking)
    INP_GET_EVENT = 11,     // Get raw input event (non-blocking)
    INP_GET_MODIFIERS = 12, // Query current modifier state
    INP_HAS_INPUT = 13,     // Check if input is available

    // Async notifications (server -> client)
    INP_EVENT_NOTIFY = 0x80, // Async event notification

    // Replies
    INP_SUBSCRIBE_REPLY = 0x81,
    INP_GET_CHAR_REPLY = 0x8A,
    INP_GET_EVENT_REPLY = 0x8B,
    INP_GET_MODIFIERS_REPLY = 0x8C,
    INP_HAS_INPUT_REPLY = 0x8D,
};

// Input event types
enum EventType : uint8_t
{
    EVENT_NONE = 0,
    EVENT_KEY_PRESS = 1,
    EVENT_KEY_RELEASE = 2,
    EVENT_MOUSE_MOVE = 3,
    EVENT_MOUSE_BUTTON = 4,
};

// Modifier key bits
namespace modifier
{
constexpr uint8_t SHIFT = 0x01;
constexpr uint8_t CTRL = 0x02;
constexpr uint8_t ALT = 0x04;
constexpr uint8_t META = 0x08;
constexpr uint8_t CAPS_LOCK = 0x10;
} // namespace modifier

// Input event structure
struct InputEvent
{
    EventType type;
    uint8_t modifiers;
    uint16_t code; // Linux evdev keycode
    int32_t value; // 1=press, 0=release, or mouse delta
};

// Maximum message payload size
constexpr size_t MAX_PAYLOAD = 256;

// Request: Subscribe to input events
struct SubscribeRequest
{
    uint32_t type; // INP_SUBSCRIBE
    uint32_t request_id;
    uint32_t event_mask; // Which events to receive (bitmask of EventType)
};

// Reply: Subscribe result
struct SubscribeReply
{
    uint32_t type; // INP_SUBSCRIBE_REPLY
    uint32_t request_id;
    int32_t status;         // 0 = success, negative = error
    uint32_t event_channel; // Channel handle for async events (if status == 0)
};

// Request: Get translated character
struct GetCharRequest
{
    uint32_t type; // INP_GET_CHAR
    uint32_t request_id;
};

// Reply: Character result
struct GetCharReply
{
    uint32_t type; // INP_GET_CHAR_REPLY
    uint32_t request_id;
    int32_t result; // Character (0-255) or -1 if none available
};

// Request: Get raw input event
struct GetEventRequest
{
    uint32_t type; // INP_GET_EVENT
    uint32_t request_id;
};

// Reply: Event result
struct GetEventReply
{
    uint32_t type; // INP_GET_EVENT_REPLY
    uint32_t request_id;
    int32_t status;   // 0 = event available, -1 = no event
    InputEvent event; // Valid if status == 0
};

// Request: Get modifier state
struct GetModifiersRequest
{
    uint32_t type; // INP_GET_MODIFIERS
    uint32_t request_id;
};

// Reply: Modifier state
struct GetModifiersReply
{
    uint32_t type; // INP_GET_MODIFIERS_REPLY
    uint32_t request_id;
    uint8_t modifiers; // Current modifier bitmask
    uint8_t _pad[3];
};

// Request: Check if input available
struct HasInputRequest
{
    uint32_t type; // INP_HAS_INPUT
    uint32_t request_id;
};

// Reply: Input availability
struct HasInputReply
{
    uint32_t type; // INP_HAS_INPUT_REPLY
    uint32_t request_id;
    int32_t has_char;  // 1 if character available, 0 if not
    int32_t has_event; // 1 if event available, 0 if not
};

// Async notification: Input event
struct EventNotify
{
    uint32_t type; // INP_EVENT_NOTIFY
    InputEvent event;
};

} // namespace input_protocol
