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
    u32 pending_count_{0};

    // Device info
    char name_[128];
    bool is_keyboard_{false};
    bool is_mouse_{false};
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
