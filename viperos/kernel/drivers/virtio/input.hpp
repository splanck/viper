//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: kernel/drivers/virtio/input.hpp
// Purpose: Virtio input device driver (virtio-input).
// Key invariants: Event queue with receive buffers; non-blocking poll.
// Ownership/Lifetime: Singleton device; initialized once.
// Links: kernel/drivers/virtio/input.cpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "../../include/types.hpp"
#include "virtio.hpp"
#include "virtqueue.hpp"

/**
 * @file input.hpp
 * @brief Virtio input device driver (virtio-input).
 *
 * @details
 * Virtio-input provides generic input events (keyboard, mouse, touchscreen)
 * using a Linux-like `input_event` format delivered via virtqueues.
 *
 * This driver:
 * - Probes a virtio input device and reads basic identification data.
 * - Determines whether the device behaves like a keyboard or mouse by querying
 *   supported event types.
 * - Sets up an event virtqueue with a pool of receive buffers.
 * - Provides non-blocking polling APIs to retrieve events.
 *
 * Higher-level translation (keycodes to ASCII, escape sequences, etc.) is
 * handled by `kernel/input/input.cpp`.
 */
namespace virtio
{

// virtio-input config select values
/** @brief Config-space selector values used by virtio-input. */
namespace input_config
{
constexpr u8 UNSET = 0x00;
constexpr u8 ID_NAME = 0x01;
constexpr u8 ID_SERIAL = 0x02;
constexpr u8 ID_DEVIDS = 0x03;
constexpr u8 PROP_BITS = 0x10;
constexpr u8 EV_BITS = 0x11;
constexpr u8 ABS_INFO = 0x12;
} // namespace input_config

// Linux input event types
/**
 * @brief Linux input event type constants.
 *
 * @details
 * These values match Linux `EV_*` types and are used by virtio-input devices.
 */
namespace ev_type
{
constexpr u16 SYN = 0x00; // Synchronization
constexpr u16 KEY = 0x01; // Key/button
constexpr u16 REL = 0x02; // Relative axis (mouse movement)
constexpr u16 ABS = 0x03; // Absolute axis (touchscreen)
constexpr u16 MSC = 0x04; // Misc
constexpr u16 LED = 0x11; // LED
constexpr u16 REP = 0x14; // Repeat
} // namespace ev_type

// Linux LED codes
/**
 * @brief Linux LED code constants.
 *
 * @details
 * These values match Linux `LED_*` codes and are used to control keyboard LEDs.
 */
namespace led_code
{
constexpr u16 NUML = 0x00;    // Num Lock LED
constexpr u16 CAPSL = 0x01;   // Caps Lock LED
constexpr u16 SCROLLL = 0x02; // Scroll Lock LED
constexpr u16 COMPOSE = 0x03; // Compose LED
constexpr u16 KANA = 0x04;    // Kana LED
constexpr u16 MAX = 0x0F;     // Maximum LED code
} // namespace led_code

// virtio-input event structure (matches Linux input_event)
/**
 * @brief One input event as delivered by virtio-input.
 *
 * @details
 * This is compatible with the Linux `struct input_event` payload used by
 * virtio-input.
 */
struct InputEvent
{
    u16 type;  // Event type (EV_KEY, EV_REL, etc.)
    u16 code;  // Event code (key code, axis, etc.)
    u32 value; // Event value (1=press, 0=release, movement delta)
};

// virtio-input device config
/**
 * @brief Virtio-input configuration structure at CONFIG space.
 *
 * @details
 * The guest writes `select`/`subsel` to choose what data is exposed, then reads
 * `size` and the union payload.
 */
struct InputConfig
{
    u8 select;
    u8 subsel;
    u8 size;
    u8 reserved[5];

    union
    {
        char string[128];
        u8 bitmap[128];

        struct
        {
            u16 bustype;
            u16 vendor;
            u16 product;
            u16 version;
        } ids;
    } u;
};

// Event buffer for receiving input events
/** @brief Number of event buffers kept in the receive pool. */
constexpr usize INPUT_EVENT_BUFFERS = 64;

// Input device class
/**
 * @brief Virtio-input device driver instance.
 *
 * @details
 * Uses:
 * - Queue 0 (eventq) for delivering input events into guest-provided buffers.
 * - Optional status queue (present in the spec; not fully used here).
 */
class InputDevice : public Device
{
  public:
    /**
     * @brief Initialize the device at the given MMIO base.
     *
     * @param base_addr MMIO base address.
     * @return `true` on success, otherwise `false`.
     */
    bool init(u64 base_addr);

    // Check for pending events (non-blocking)
    /**
     * @brief Check whether a completed event buffer is available.
     *
     * @details
     * Polls the used ring; does not consume the event.
     *
     * @return `true` if an event is available.
     */
    bool has_event();

    // Get next event (returns false if no event)
    /**
     * @brief Retrieve the next input event from the device.
     *
     * @details
     * Polls the used ring for a completed buffer, copies the event payload out,
     * returns the descriptor to the free list, and refills the queue with a new
     * receive buffer.
     *
     * @param event Output pointer for the event.
     * @return `true` if an event was returned, otherwise `false`.
     */
    bool get_event(InputEvent *event);

    // Get device name
    /** @brief Human-readable device name from config space. */
    const char *name() const
    {
        return name_;
    }

    // Is this a keyboard?
    /** @brief Whether the device appears to be a keyboard. */
    bool is_keyboard() const
    {
        return is_keyboard_;
    }

    // Is this a mouse?
    /** @brief Whether the device appears to be a mouse. */
    bool is_mouse() const
    {
        return is_mouse_;
    }

    /**
     * @brief Set the state of a keyboard LED.
     *
     * @details
     * Sends an LED event to the device via the status queue. This is used to
     * control Num Lock, Caps Lock, and Scroll Lock LEDs.
     *
     * @param led LED code from led_code namespace.
     * @param on true to turn on, false to turn off.
     * @return true on success, false if LED control not supported or failed.
     */
    bool set_led(u16 led, bool on);

    /**
     * @brief Check if the device supports LED control.
     *
     * @return true if LEDs can be controlled.
     */
    bool has_led_support() const
    {
        return has_led_;
    }

  private:
    // Refill the event queue with buffers
    /**
     * @brief Submit receive buffers to the event virtqueue.
     *
     * @details
     * Allocates descriptors and points them at DMA buffers so the device can
     * write incoming events.
     */
    void refill_eventq();

    Virtqueue eventq_;
    Virtqueue statusq_;

    // Event buffers
    InputEvent events_[INPUT_EVENT_BUFFERS];
    u64 events_phys_{0};
    [[maybe_unused]] u32 pending_count_{0};

    // Device info
    char name_[128];
    bool is_keyboard_{false};
    bool is_mouse_{false};
    bool has_led_{false};

    // Status buffer for LED control (single event)
    InputEvent *status_event_{nullptr};
    u64 status_event_phys_{0};
};

// Global input device pointers
extern InputDevice *keyboard;
extern InputDevice *mouse;

// Initialize input subsystem
/**
 * @brief Probe and initialize virtio input devices.
 *
 * @details
 * Iterates over discovered virtio devices, initializes those of INPUT type, and
 * assigns the first keyboard and mouse devices to the global pointers.
 */
void input_init();

} // namespace virtio
