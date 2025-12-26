#pragma once

#include "../include/types.hpp"

/**
 * @file input.hpp
 * @brief Kernel input event subsystem (keyboard/mouse).
 *
 * @details
 * The input subsystem collects raw device events (currently via virtio input
 * devices) and exposes them as higher-level events and translated characters.
 *
 * It maintains:
 * - A ring buffer of structured @ref input::Event records (key press/release,
 *   mouse events, etc.).
 * - A separate character ring buffer containing translated ASCII bytes and
 *   escape sequences for special keys (arrow keys, home/end, etc.).
 *
 * The timer interrupt handler calls @ref poll periodically to pull events from
 * devices; consumers can then query for available events/characters without
 * directly interacting with the device drivers.
 */
namespace input
{

// Input event types
/**
 * @brief High-level input event categories.
 */
enum class EventType : u8
{
    None = 0,
    KeyPress = 1,
    KeyRelease = 2,
    MouseMove = 3,
    MouseButton = 4,
};

// Modifier keys
/**
 * @brief Bitmask values representing active keyboard modifiers.
 *
 * @details
 * The modifier mask is updated as modifier key press/release events are
 * processed and is attached to each emitted @ref Event.
 */
namespace modifier
{
constexpr u8 SHIFT = 0x01;
constexpr u8 CTRL = 0x02;
constexpr u8 ALT = 0x04;
constexpr u8 META = 0x08;
constexpr u8 CAPS_LOCK = 0x10;
} // namespace modifier

// Input event structure
/**
 * @brief One input event emitted by the input subsystem.
 *
 * @details
 * The `code` field generally contains a Linux evdev/HID key code for keyboard
 * events (see `keycodes.hpp`). For other devices it may represent button IDs or
 * other device-specific codes.
 */
struct Event
{
    EventType type;
    u8 modifiers; // Current modifier state
    u16 code;     // HID key code or mouse button
    i32 value;    // 1=press, 0=release, or mouse delta
};

// Event queue size
/** @brief Number of events stored in the event ring buffer. */
constexpr usize EVENT_QUEUE_SIZE = 64;

/**
 * @brief Initialize the input subsystem.
 *
 * @details
 * Resets event and character buffers and clears modifier/caps-lock state. Call
 * once during kernel boot before polling devices.
 */
void init();

/**
 * @brief Poll input devices for new events.
 *
 * @details
 * Reads raw events from available input devices (e.g. virtio keyboard/mouse),
 * translates them into @ref Event records and/or characters, and enqueues them
 * in internal ring buffers.
 *
 * This is typically invoked from the periodic timer interrupt handler so input
 * is processed regularly without dedicated threads during bring-up.
 */
void poll();

/**
 * @brief Check if there is at least one pending input event.
 *
 * @return `true` if an event can be retrieved via @ref get_event.
 */
bool has_event();

/**
 * @brief Retrieve the next pending input event.
 *
 * @param event Output pointer for the event.
 * @return `true` if an event was returned, `false` if the queue is empty.
 */
bool get_event(Event *event);

/**
 * @brief Get the current modifier mask.
 *
 * @return Modifier bitmask (see @ref modifier).
 */
u8 get_modifiers();

// Get a character from the keyboard (blocking)
// Returns ASCII character, or 0 for non-printable keys
// Returns -1 if no character available (non-blocking)
/**
 * @brief Retrieve the next translated character from the keyboard buffer.
 *
 * @details
 * Returns the next byte from the character ring buffer. Special keys may be
 * represented as multi-byte escape sequences (e.g. `\"\\033[A\"` for Up).
 *
 * Despite the historical comment, the current implementation is non-blocking:
 * it returns `-1` when no character is available.
 *
 * @return Next character byte (0-255) or `-1` if none is available.
 */
i32 getchar();

/**
 * @brief Check whether a translated character is available.
 *
 * @return `true` if @ref getchar would return a byte without blocking.
 */
bool has_char();

} // namespace input
