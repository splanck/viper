/**
 * @file virtio.hpp
 * @brief User-space Virtio-MMIO core definitions and base device helper.
 *
 * @details
 * Virtio is a standardized paravirtual device interface commonly used by QEMU.
 * On the QEMU AArch64 `virt` machine, devices are exposed via the virtio-mmio
 * transport: each device occupies a small MMIO register window at a known base
 * address.
 *
 * This user-space library uses the device access syscalls to:
 * - Map device MMIO regions into user address space
 * - Allocate DMA memory for virtqueue rings
 * - Register for and handle device interrupts
 *
 * The API mirrors the kernel VirtIO interface for easy driver porting.
 */
#pragma once

#include "../../syscall.hpp"
#include "device.hpp"

namespace virtio
{

// MMIO register offsets (shared between legacy v1 and modern v2)
/**
 * @brief Virtio-MMIO register offsets.
 */
namespace reg
{
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
constexpr u32 GUEST_PAGE_SIZE = 0x028;
constexpr u32 QUEUE_ALIGN = 0x03C;
constexpr u32 QUEUE_PFN = 0x040;

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
 * @brief Status bits written to/read from the `STATUS` register.
 */
namespace status
{
constexpr u32 ACKNOWLEDGE = 1;
constexpr u32 DRIVER = 2;
constexpr u32 DRIVER_OK = 4;
constexpr u32 FEATURES_OK = 8;
constexpr u32 FAILED = 128;
} // namespace status

// Device types
/**
 * @brief Virtio device IDs as reported by `DEVICE_ID`.
 */
namespace device_type
{
constexpr u32 NET = 1;
constexpr u32 BLK = 2;
constexpr u32 CONSOLE = 3;
constexpr u32 RNG = 4;
constexpr u32 GPU = 16;
constexpr u32 INPUT = 18;
} // namespace device_type

// Magic value "virt"
constexpr u32 MAGIC_VALUE = 0x74726976;

// Common feature bits
namespace features
{
constexpr u64 VERSION_1 = 1ULL << 32;
}

// Page size constant
constexpr u64 PAGE_SIZE = 4096;

/**
 * @brief Base helper for virtio-mmio devices in user-space.
 *
 * @details
 * Provides basic MMIO register access and implements:
 * - Device probing (`init`) which maps the device and checks magic/version.
 * - Reset and status bit management.
 * - Configuration space reads.
 * - Feature negotiation for both legacy and modern virtio.
 *
 * User-space drivers inherit from `Device` and configure queues and device-
 * specific configuration space.
 */
class Device
{
  public:
    /**
     * @brief Initialize this object to represent a virtio-mmio device.
     *
     * @param phys_addr Physical MMIO base address of the virtio device.
     * @return `true` if the device was successfully mapped and validated.
     */
    bool init(u64 phys_addr);

    /**
     * @brief Clean up resources (unmap MMIO region).
     */
    void destroy();

    /**
     * @brief Reset the device into the initial state.
     */
    void reset();

    // Register access
    u32 read32(u32 offset);
    void write32(u32 offset, u32 value);

    // Config space access
    u8 read_config8(u32 offset);
    u16 read_config16(u32 offset);
    u32 read_config32(u32 offset);
    u64 read_config64(u32 offset);

    // Device info
    u32 device_id() const { return device_id_; }
    u64 phys_base() const { return phys_base_; }
    u64 virt_base() const { return virt_base_; }
    bool is_legacy() const { return version_ == 1; }
    u32 version() const { return version_; }
    u32 irq() const { return irq_; }

    // Feature negotiation
    bool negotiate_features(u64 required);

    // Status management
    void set_status(u32 status);
    u32 get_status();
    void add_status(u32 bits);

    // Interrupt handling
    u32 read_isr();
    void ack_interrupt(u32 bits);

    // IRQ registration (for user-space interrupt handling)
    bool register_irq();
    void unregister_irq();
    i64 wait_irq(u64 timeout_ms = 0);
    i64 ack_irq();

  protected:
    volatile u32 *mmio_{nullptr};
    u64 phys_base_{0};
    u64 virt_base_{0};
    u32 device_id_{0};
    u32 version_{0};
    u32 irq_{0};
    bool irq_registered_{false};
};

// Maximum devices to probe
constexpr usize MAX_DEVICES = 8;

// Device registry
struct DeviceInfo
{
    u64 base;
    u32 type;
    bool in_use;
};

/**
 * @brief Scan for virtio devices using the device enumeration syscall.
 */
void init();

/**
 * @brief Find and claim a device of the given type.
 *
 * @param type Virtio device ID (see @ref device_type).
 * @return Physical MMIO base address, or 0 if not found.
 */
u64 find_device(u32 type);

/**
 * @brief Get the number of devices discovered.
 */
usize device_count();

/**
 * @brief Get information about a discovered device.
 */
const DeviceInfo *get_device_info(usize index);

} // namespace virtio
