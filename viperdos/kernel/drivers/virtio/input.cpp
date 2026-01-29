//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
#include "input.hpp"
#include "../../console/serial.hpp"
#include "../../mm/pmm.hpp"

/**
 * @file input.cpp
 * @brief Virtio-input driver implementation.
 *
 * @details
 * Initializes virtio input devices (keyboard/mouse) and exposes non-blocking
 * polling APIs for higher-level input processing.
 *
 * Note: The kernel input subsystem (`kernel/input/input.cpp`) is responsible for
 * consuming events and translating them into characters; this driver only
 * retrieves raw virtio input events.
 */
namespace virtio {

// Global input device pointers
InputDevice *keyboard = nullptr;
InputDevice *mouse = nullptr;

// =============================================================================
// InputDevice Initialization Helpers
// =============================================================================

void InputDevice::read_device_name() {
    volatile u8 *config = reinterpret_cast<volatile u8 *>(base() + reg::CONFIG);

    config[0] = input_config::ID_NAME;
    config[1] = 0;
    asm volatile("dsb sy" ::: "memory");

    u8 name_size = config[2];
    if (name_size > 127)
        name_size = 127;

    for (u8 i = 0; i < name_size; i++) {
        name_[i] = static_cast<char>(config[8 + i]);
    }
    name_[name_size] = '\0';

    serial::puts("[virtio-input] Device name: ");
    serial::puts(name_);
    serial::puts("\n");
}

void InputDevice::detect_device_type() {
    volatile u8 *config = reinterpret_cast<volatile u8 *>(base() + reg::CONFIG);

    // EV_BITS query for EV_REL (mouse movement - definitive for mouse)
    config[0] = input_config::EV_BITS;
    config[1] = ev_type::REL;
    asm volatile("dsb sy" ::: "memory");
    u8 ev_rel_size = config[2];
    is_mouse_ = (ev_rel_size > 0);

    // EV_BITS query for EV_KEY
    config[0] = input_config::EV_BITS;
    config[1] = ev_type::KEY;
    asm volatile("dsb sy" ::: "memory");
    u8 ev_key_size = config[2];
    is_keyboard_ = (ev_key_size > 0 && !is_mouse_);

    // EV_BITS query for EV_LED (LED support)
    config[0] = input_config::EV_BITS;
    config[1] = ev_type::LED;
    asm volatile("dsb sy" ::: "memory");
    u8 ev_led_size = config[2];
    has_led_ = (ev_led_size > 0);

    if (is_keyboard_)
        serial::puts("[virtio-input] Device is a keyboard\n");
    if (is_mouse_)
        serial::puts("[virtio-input] Device is a mouse\n");
    if (has_led_)
        serial::puts("[virtio-input] Device supports LED control\n");
}

bool InputDevice::negotiate_features() {
    if (is_legacy())
        return true;

    write32(reg::DEVICE_FEATURES_SEL, 1);
    u32 features_hi = read32(reg::DEVICE_FEATURES);

    serial::puts("[virtio-input] Device features_hi: ");
    serial::put_hex(features_hi);
    serial::puts("\n");

    write32(reg::DRIVER_FEATURES_SEL, 0);
    write32(reg::DRIVER_FEATURES, 0);
    write32(reg::DRIVER_FEATURES_SEL, 1);
    write32(reg::DRIVER_FEATURES, features::VERSION_1 >> 32);

    add_status(status::FEATURES_OK);
    if (!(get_status() & status::FEATURES_OK)) {
        serial::puts("[virtio-input] Failed to set FEATURES_OK\n");
        return false;
    }
    return true;
}

bool InputDevice::setup_event_queue() {
    write32(reg::QUEUE_SEL, 0);
    u32 max_queue_size = read32(reg::QUEUE_NUM_MAX);
    if (max_queue_size == 0) {
        serial::puts("[virtio-input] Invalid queue size\n");
        return false;
    }

    u32 queue_size = max_queue_size;
    if (queue_size > INPUT_EVENT_BUFFERS)
        queue_size = INPUT_EVENT_BUFFERS;

    if (!eventq_.init(this, 0, queue_size)) {
        serial::puts("[virtio-input] Failed to init eventq\n");
        return false;
    }
    return true;
}

bool InputDevice::setup_status_queue() {
    if (!has_led_)
        return true;

    write32(reg::QUEUE_SEL, 1);
    u32 status_queue_size = read32(reg::QUEUE_NUM_MAX);
    if (status_queue_size == 0) {
        serial::puts("[virtio-input] No status queue available\n");
        has_led_ = false;
        return true;
    }

    u32 sq_size = status_queue_size > 8 ? 8 : status_queue_size;
    if (!statusq_.init(this, 1, sq_size)) {
        serial::puts("[virtio-input] Failed to init statusq (LED control disabled)\n");
        has_led_ = false;
        return true;
    }

    // Allocate status buffer using DMA helper (Issue #36-38)
    status_dma_ = alloc_dma_buffer(1);
    if (!status_dma_.is_valid()) {
        serial::puts("[virtio-input] Failed to allocate status buffer\n");
        has_led_ = false;
        return true;
    }

    status_event_phys_ = status_dma_.phys;
    status_event_ = reinterpret_cast<InputEvent *>(status_dma_.virt);
    serial::puts("[virtio-input] Status queue initialized for LED control\n");
    return true;
}

bool InputDevice::allocate_event_buffers() {
    usize events_size = sizeof(InputEvent) * INPUT_EVENT_BUFFERS;
    usize pages_needed = (events_size + pmm::PAGE_SIZE - 1) / pmm::PAGE_SIZE;

    // Allocate event buffers using DMA helper (Issue #36-38)
    events_dma_ = alloc_dma_buffer(pages_needed);
    if (!events_dma_.is_valid()) {
        serial::puts("[virtio-input] Failed to allocate event buffers\n");
        return false;
    }

    events_phys_ = events_dma_.phys;
    InputEvent *virt_events = reinterpret_cast<InputEvent *>(events_dma_.virt);
    for (usize i = 0; i < INPUT_EVENT_BUFFERS; i++) {
        events_[i] = virt_events[i];
    }
    return true;
}

// =============================================================================
// InputDevice Main Initialization
// =============================================================================

/** @copydoc virtio::InputDevice::init */
bool InputDevice::init(u64 base_addr) {
    if (!Device::init(base_addr))
        return false;

    if (device_id() != device_type::INPUT) {
        serial::puts("[virtio-input] Not an input device\n");
        return false;
    }

    serial::puts("[virtio-input] Initializing input device at ");
    serial::put_hex(base_addr);
    serial::puts(" version=");
    serial::put_dec(version());
    serial::puts(is_legacy() ? " (legacy)\n" : " (modern)\n");

    reset();
    serial::puts("[virtio-input] After reset, status=");
    serial::put_hex(get_status());
    serial::puts("\n");

    add_status(status::ACKNOWLEDGE);
    add_status(status::DRIVER);

    read_device_name();
    detect_device_type();

    if (!negotiate_features())
        return false;
    if (!setup_event_queue())
        return false;
    if (!setup_status_queue())
        return false;
    if (!allocate_event_buffers())
        return false;

    add_status(status::DRIVER_OK);
    refill_eventq();

    serial::puts("[virtio-input] Final status=");
    serial::put_hex(get_status());
    serial::puts(" queue_size=");
    serial::put_dec(eventq_.size());
    serial::puts(" avail_idx=");
    serial::put_dec(eventq_.avail_idx());
    serial::puts("\n");

    serial::puts("[virtio-input] Driver initialized\n");
    return true;
}

/** @copydoc virtio::InputDevice::refill_eventq */
void InputDevice::refill_eventq() {
    // Add as many buffers as we can to the eventq
    while (eventq_.num_free() > 0) {
        i32 desc_idx = eventq_.alloc_desc();
        if (desc_idx < 0)
            break;

        // Point descriptor at an event buffer
        u64 buf_addr =
            events_phys_ + (static_cast<u32>(desc_idx) % INPUT_EVENT_BUFFERS) * sizeof(InputEvent);
        eventq_.set_desc(
            static_cast<u32>(desc_idx), buf_addr, sizeof(InputEvent), desc_flags::WRITE);
        eventq_.submit(static_cast<u32>(desc_idx));
    }
    eventq_.kick();
}

/** @copydoc virtio::InputDevice::has_event */
bool InputDevice::has_event() {
    return eventq_.poll_used() >= 0;
}

/** @copydoc virtio::InputDevice::get_event */
bool InputDevice::get_event(InputEvent *event) {
    // Debug disabled - was flooding output
    // static u32 poll_count = 0;
    // if (++poll_count % 10000 == 0) { ... }

    i32 used_idx = eventq_.poll_used();
    if (used_idx < 0) {
        return false;
    }

    // Copy event data from the buffer (convert physical to virtual address)
    u32 desc_idx = static_cast<u32>(used_idx);
    u64 buf_phys = events_phys_ + (desc_idx % INPUT_EVENT_BUFFERS) * sizeof(InputEvent);
    InputEvent *src = reinterpret_cast<InputEvent *>(pmm::phys_to_virt(buf_phys));

    event->type = src->type;
    event->code = src->code;
    event->value = src->value;

    // Free the descriptor and refill
    eventq_.free_desc(desc_idx);
    refill_eventq();

    return true;
}

/** @copydoc virtio::input_init */
void input_init() {
    serial::puts("[virtio-input] Scanning for input devices...\n");
    serial::puts("[virtio-input] Total virtio devices: ");
    serial::put_dec(device_count());
    serial::puts("\n");

    // Look for keyboard and mouse devices
    for (usize i = 0; i < device_count(); i++) {
        const DeviceInfo *info = get_device_info(i);
        if (!info)
            continue;

        serial::puts("[virtio-input] Device ");
        serial::put_dec(i);
        serial::puts(": type=");
        serial::put_dec(info->type);
        serial::puts(" (INPUT=");
        serial::put_dec(device_type::INPUT);
        serial::puts(")\n");

        if (info->type != device_type::INPUT || info->in_use) {
            continue;
        }

        serial::puts("[virtio-input] Found INPUT device, initializing...\n");

        // Try to initialize as input device
        InputDevice *dev = new InputDevice();
        if (!dev->init(info->base)) {
            serial::puts("[virtio-input] Init failed!\n");
            delete dev;
            continue;
        }

        serial::puts("[virtio-input] Device name: ");
        serial::puts(dev->name());
        serial::puts(", is_keyboard=");
        serial::put_dec(dev->is_keyboard() ? 1 : 0);
        serial::puts(", is_mouse=");
        serial::put_dec(dev->is_mouse() ? 1 : 0);
        serial::puts("\n");

        // Assign to keyboard or mouse based on capabilities
        if (dev->is_keyboard() && !keyboard) {
            keyboard = dev;
            serial::puts("[virtio-input] *** KEYBOARD ASSIGNED ***\n");
        } else if (dev->is_mouse() && !mouse) {
            mouse = dev;
            serial::puts("[virtio-input] *** MOUSE ASSIGNED ***\n");
        } else {
            serial::puts("[virtio-input] Device not assigned (duplicate or unknown)\n");
            delete dev;
        }
    }

    if (!keyboard && !mouse) {
        serial::puts("[virtio-input] WARNING: No input devices found!\n");
    }
}

// Note: Keyboard/mouse event processing is handled by input::poll() in kernel/input/input.cpp
// which is called from the timer interrupt handler. Do NOT consume events here.

/** @copydoc virtio::InputDevice::set_led */
bool InputDevice::set_led(u16 led, bool on) {
    if (!has_led_ || !status_event_) {
        return false;
    }

    if (led > led_code::MAX) {
        return false;
    }

    // Prepare the LED event
    status_event_->type = ev_type::LED;
    status_event_->code = led;
    status_event_->value = on ? 1 : 0;

    // Memory barrier before submitting
    asm volatile("dsb sy" ::: "memory");

    // Allocate a descriptor
    i32 desc = statusq_.alloc_desc();
    if (desc < 0) {
        serial::puts("[virtio-input] No free status descriptors\n");
        return false;
    }

    // Set up descriptor - device reads this buffer
    statusq_.set_desc(desc, status_event_phys_, sizeof(InputEvent), 0);

    // Submit and kick
    statusq_.submit(desc);
    statusq_.kick();

    // Wait for completion (blocking with timeout)
    bool completed = false;
    for (u32 i = 0; i < 100000; i++) {
        i32 used = statusq_.poll_used();
        if (used == desc) {
            completed = true;
            break;
        }
        asm volatile("yield" ::: "memory");
    }

    // Free the descriptor
    statusq_.free_desc(desc);

    if (!completed) {
        serial::puts("[virtio-input] LED set timed out\n");
        return false;
    }

    return true;
}

} // namespace virtio
