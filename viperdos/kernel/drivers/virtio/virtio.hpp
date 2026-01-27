//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: kernel/drivers/virtio/virtio.hpp
// Purpose: Virtio-MMIO core definitions and base device helper.
// Key invariants: MMIO register offsets match QEMU virt machine spec.
// Ownership/Lifetime: Device objects live for kernel lifetime.
// Links: kernel/drivers/virtio/virtio.cpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "../../include/types.hpp"

/**
 * @file virtio.hpp
 * @brief Virtio-MMIO core definitions and base device helper.
 *
 * @details
 * Virtio is a standardized paravirtual device interface commonly used by QEMU.
 * On the QEMU AArch64 `virt` machine, devices are exposed via the virtio-mmio
 * transport: each device occupies a small MMIO register window at a known base
 * address.
 *
 * Virtio-MMIO has two broad modes:
 * - Legacy (version 1): 32-bit feature negotiation and legacy queue registers.
 * - Modern (version 2 / VIRTIO_F_VERSION_1): 64-bit feature negotiation and
 *   separate queue address registers for descriptor/avail/used rings.
 *
 * This header defines common MMIO registers, status bits, and a `virtio::Device`
 * base class that provides register access, feature negotiation, and status
 * management shared by specific device drivers (net, block, input, rng).
 */
namespace virtio {

// MMIO register offsets (shared between legacy v1 and modern v2)
/**
 * @brief Virtio-MMIO register offsets.
 *
 * @details
 * Offsets are relative to the device's MMIO base address. The same offsets are
 * used for both legacy and modern devices, but some registers only apply to
 * one mode (e.g. QUEUE_PFN for legacy, QUEUE_DESC_* for modern).
 */
namespace reg {
constexpr u32 MAGIC = 0x000;
constexpr u32 VERSION = 0x004;
constexpr u32 DEVICE_ID = 0x008;
constexpr u32 VENDOR_ID = 0x00C;
constexpr u32 DEVICE_FEATURES = 0x010;
constexpr u32 DEVICE_FEATURES_SEL = 0x014;
constexpr u32 DRIVER_FEATURES = 0x020;
constexpr u32 DRIVER_FEATURES_SEL = 0x024;
constexpr u32 QUEUE_SEL = 0x030;
constexpr u32 QUEUE_NUM_MAX = 0x034;
constexpr u32 QUEUE_NUM = 0x038;

// Legacy (v1) registers
constexpr u32 GUEST_PAGE_SIZE = 0x028; // Legacy: must set to 4096
constexpr u32 QUEUE_ALIGN = 0x03C;     // Legacy: queue alignment
constexpr u32 QUEUE_PFN = 0x040;       // Legacy: queue page frame number

// Modern (v2) registers
constexpr u32 QUEUE_READY = 0x044;
constexpr u32 QUEUE_NOTIFY = 0x050;
constexpr u32 INTERRUPT_STATUS = 0x060;
constexpr u32 INTERRUPT_ACK = 0x064;
constexpr u32 STATUS = 0x070;
constexpr u32 QUEUE_DESC_LOW = 0x080;
constexpr u32 QUEUE_DESC_HIGH = 0x084;
constexpr u32 QUEUE_AVAIL_LOW = 0x090;
constexpr u32 QUEUE_AVAIL_HIGH = 0x094;
constexpr u32 QUEUE_USED_LOW = 0x0A0;
constexpr u32 QUEUE_USED_HIGH = 0x0A4;
constexpr u32 CONFIG = 0x100;
} // namespace reg

// Device status bits
/**
 * @brief Status bits written to/ read from the `STATUS` register.
 *
 * @details
 * Drivers follow the initialization sequence described by the virtio spec:
 * ACKNOWLEDGE -> DRIVER -> FEATURES_OK -> DRIVER_OK, with FAILED indicating an
 * unrecoverable error.
 */
namespace status {
constexpr u32 ACKNOWLEDGE = 1;
constexpr u32 DRIVER = 2;
constexpr u32 DRIVER_OK = 4;
constexpr u32 FEATURES_OK = 8;
constexpr u32 FAILED = 128;
} // namespace status

// Device types
/**
 * @brief Virtio device IDs as reported by `DEVICE_ID`.
 *
 * @details
 * These numeric IDs identify the device class (net, block, rng, input, etc.).
 */
namespace device_type {
constexpr u32 NET = 1;
constexpr u32 BLK = 2;
constexpr u32 CONSOLE = 3;
constexpr u32 RNG = 4;
constexpr u32 GPU = 16;
constexpr u32 INPUT = 18;
constexpr u32 SOUND = 25;
} // namespace device_type

// Magic value "virt"
/** @brief Expected value of the `MAGIC` register ("virt"). */
constexpr u32 MAGIC_VALUE = 0x74726976;

// Common feature bits (must be negotiated for modern virtio)
/**
 * @brief Common virtio feature bits.
 *
 * @details
 * Modern virtio devices require negotiating `VIRTIO_F_VERSION_1` (bit 32).
 */
namespace features {
constexpr u64 VERSION_1 = 1ULL << 32; // Modern virtio (required for v2)
}

// Base virtio device class
/**
 * @brief Base helper for virtio-mmio devices.
 *
 * @details
 * Provides basic MMIO register access and implements:
 * - Device probing (`init`) which checks magic/version/device ID.
 * - Reset and status bit management.
 * - Configuration space reads.
 * - Feature negotiation for both legacy and modern virtio.
 *
 * Concrete drivers inherit from `Device` and then configure queues and device-
 * specific configuration space.
 */
class Device {
  public:
    /**
     * @brief Initialize this object to represent a virtio-mmio device.
     *
     * @details
     * Sets the MMIO base, verifies the magic value, reads the virtio version,
     * and caches the device ID. Returns `false` if the base address does not
     * contain a valid virtio device.
     *
     * @param base_addr MMIO base address of the virtio device.
     * @return `true` if a valid device is present, otherwise `false`.
     */
    bool init(u64 base_addr);
    /**
     * @brief Reset the device into the initial state.
     *
     * @details
     * Writes 0 to STATUS and waits for the device to acknowledge the reset.
     */
    void reset();

    // Register access
    /** @brief Read a 32-bit MMIO register at the given offset. */
    u32 read32(u32 offset);
    /** @brief Write a 32-bit MMIO register at the given offset. */
    void write32(u32 offset, u32 value);

    // Config space access
    /** @brief Read an 8-bit value from the device configuration space. */
    u8 read_config8(u32 offset);
    /** @brief Read a 16-bit value from the device configuration space. */
    u16 read_config16(u32 offset);
    /** @brief Read a 32-bit value from the device configuration space. */
    u32 read_config32(u32 offset);
    /** @brief Read a 64-bit value from the device configuration space. */
    u64 read_config64(u32 offset);

    // Device info
    /** @brief Virtio device ID (`DEVICE_ID`). */
    u32 device_id() const {
        return device_id_;
    }

    /** @brief MMIO base address of the device. */
    u64 base() const {
        return base_;
    }

    /** @brief Whether the device is legacy mode (version 1). */
    bool is_legacy() const {
        return version_ == 1;
    }

    /** @brief Virtio MMIO version value (1 = legacy, 2 = modern). */
    u32 version() const {
        return version_;
    }

    // Feature negotiation
    /**
     * @brief Negotiate device features.
     *
     * @details
     * For legacy devices, reads the 32-bit feature set and writes accepted
     * features directly.
     *
     * For modern devices, reads the full 64-bit feature set using the selector
     * registers, checks that all `required` features are present, and then
     * writes back only the accepted feature bits. The function sets FEATURES_OK
     * and verifies the device accepted it.
     *
     * @param required Bitmask of features required by the driver.
     * @return `true` if negotiation succeeds, otherwise `false`.
     */
    bool negotiate_features(u64 required);

    // Status management
    /** @brief Overwrite the device status register with `status`. */
    void set_status(u32 status);
    /** @brief Read the current device status register. */
    u32 get_status();
    /** @brief OR the given bits into the device status register. */
    void add_status(u32 bits);

    // Interrupt handling
    /** @brief Read the interrupt status register (ISR). */
    u32 read_isr();
    /** @brief Acknowledge interrupt bits by writing to INTERRUPT_ACK. */
    void ack_interrupt(u32 bits);

    // High-level initialization helper
    /**
     * @brief Perform common early initialization steps.
     *
     * @details
     * Combines the following common initialization steps:
     * 1. init(base_addr) - verify device
     * 2. reset() - reset to initial state
     * 3. For legacy devices: set GUEST_PAGE_SIZE to 4096
     * 4. add_status(ACKNOWLEDGE | DRIVER)
     *
     * After calling this, the driver should negotiate features using
     * negotiate_features(), initialize virtqueues, and finally call
     * add_status(DRIVER_OK).
     *
     * @param base_addr MMIO base address of the device.
     * @return `true` if initialization succeeded, otherwise `false`.
     */
    bool basic_init(u64 base_addr);

  protected:
    volatile u32 *mmio_{nullptr};
    u64 base_{0};
    u32 device_id_{0};
    u32 version_{0};
};

// Maximum devices to probe
/** @brief Maximum number of virtio-mmio devices scanned during init. */
constexpr usize MAX_DEVICES = 8;

// Device registry
/**
 * @brief Record for one discovered virtio-mmio device window.
 *
 * @details
 * `in_use` is set when a driver claims a device so subsequent lookups do not
 * return the same base address twice.
 */
struct DeviceInfo {
    u64 base;
    u32 type;
    bool in_use;
};

// Initialize virtio subsystem and probe devices
/**
 * @brief Scan the QEMU virtio-mmio address range and record discovered devices.
 *
 * @details
 * On the QEMU `virt` machine, virtio-mmio devices are typically located at
 * `0x0a000000` in a series of windows spaced 0x200 bytes apart. This function
 * scans that range and fills an internal device registry used by `find_device`.
 */
void init();

// Get device by type (returns base address, 0 if not found)
/**
 * @brief Find and claim a device of the given type.
 *
 * @details
 * Returns the MMIO base address of the first matching unclaimed device and
 * marks it in-use.
 *
 * @param type Virtio device ID (see @ref device_type).
 * @return MMIO base address, or 0 if not found.
 */
u64 find_device(u32 type);

// Get number of devices found
/** @brief Get the number of devices discovered by @ref init. */
usize device_count();

// Get device info by index
/**
 * @brief Get information about a discovered device.
 *
 * @param index Device index in the discovery list.
 * @return Pointer to device info, or `nullptr` if out of range.
 */
const DeviceInfo *get_device_info(usize index);

} // namespace virtio
