#include "virtio.hpp"
#include "../../console/serial.hpp"
#include "../../mm/pmm.hpp"

/**
 * @file virtio.cpp
 * @brief Virtio-MMIO core implementation and device scanning.
 *
 * @details
 * Implements the `virtio::Device` helper used by all virtio drivers and the
 * discovery mechanism that scans the QEMU `virt` MMIO range to enumerate
 * devices.
 *
 * The discovery list is used by device-specific drivers via `find_device()`
 * which returns and claims a device base address for initialization.
 */
namespace virtio
{

// Device registry
static DeviceInfo devices[MAX_DEVICES];
static usize num_devices = 0;

/** @copydoc virtio::Device::init */
bool Device::init(u64 base_addr)
{
    base_ = base_addr;
    mmio_ = reinterpret_cast<volatile u32 *>(base_addr);

    // Check magic
    u32 magic = read32(reg::MAGIC);
    if (magic != MAGIC_VALUE)
    {
        return false;
    }

    // Check version (1 = legacy, 2 = modern)
    version_ = read32(reg::VERSION);
    if (version_ != 1 && version_ != 2)
    {
        serial::puts("[virtio] Unsupported device version ");
        serial::put_dec(version_);
        serial::puts("\n");
        return false;
    }

    device_id_ = read32(reg::DEVICE_ID);
    if (device_id_ == 0)
    {
        return false; // Invalid device
    }

    return true;
}

/** @copydoc virtio::Device::reset */
void Device::reset()
{
    write32(reg::STATUS, 0);
    // Wait for reset to complete
    while (read32(reg::STATUS) != 0)
    {
        asm volatile("yield");
    }
}

/** @copydoc virtio::Device::read32 */
u32 Device::read32(u32 offset)
{
    return mmio_[offset / 4];
}

/** @copydoc virtio::Device::write32 */
void Device::write32(u32 offset, u32 value)
{
    mmio_[offset / 4] = value;
}

/** @copydoc virtio::Device::read_config8 */
u8 Device::read_config8(u32 offset)
{
    volatile u8 *config =
        reinterpret_cast<volatile u8 *>(reinterpret_cast<u64>(mmio_) + reg::CONFIG + offset);
    return *config;
}

/** @copydoc virtio::Device::read_config16 */
u16 Device::read_config16(u32 offset)
{
    volatile u16 *config =
        reinterpret_cast<volatile u16 *>(reinterpret_cast<u64>(mmio_) + reg::CONFIG + offset);
    return *config;
}

/** @copydoc virtio::Device::read_config32 */
u32 Device::read_config32(u32 offset)
{
    return read32(reg::CONFIG + offset);
}

/** @copydoc virtio::Device::read_config64 */
u64 Device::read_config64(u32 offset)
{
    u32 lo = read32(reg::CONFIG + offset);
    u32 hi = read32(reg::CONFIG + offset + 4);
    return (static_cast<u64>(hi) << 32) | lo;
}

/** @copydoc virtio::Device::negotiate_features */
bool Device::negotiate_features(u64 required)
{
    if (is_legacy())
    {
        // Legacy: simpler feature negotiation
        u32 device_features = read32(reg::DEVICE_FEATURES);

        if ((device_features & static_cast<u32>(required)) != static_cast<u32>(required))
        {
            serial::puts("[virtio] Missing required features\n");
            return false;
        }

        write32(reg::DRIVER_FEATURES, required & 0xFFFFFFFF);
        return true;
    }

    // Modern: full 64-bit feature negotiation
    // Read device features (low 32 bits)
    write32(reg::DEVICE_FEATURES_SEL, 0);
    u32 features_lo = read32(reg::DEVICE_FEATURES);

    // Read device features (high 32 bits)
    write32(reg::DEVICE_FEATURES_SEL, 1);
    u32 features_hi = read32(reg::DEVICE_FEATURES);

    u64 device_features = (static_cast<u64>(features_hi) << 32) | features_lo;

    // Check required features are available
    if ((device_features & required) != required)
    {
        serial::puts("[virtio] Missing required features\n");
        return false;
    }

    // Accept only required features
    write32(reg::DRIVER_FEATURES_SEL, 0);
    write32(reg::DRIVER_FEATURES, required & 0xFFFFFFFF);
    write32(reg::DRIVER_FEATURES_SEL, 1);
    write32(reg::DRIVER_FEATURES, (required >> 32) & 0xFFFFFFFF);

    // Set FEATURES_OK
    add_status(status::FEATURES_OK);

    // Verify FEATURES_OK is still set
    if (!(get_status() & status::FEATURES_OK))
    {
        serial::puts("[virtio] Device rejected features\n");
        return false;
    }

    return true;
}

/** @copydoc virtio::Device::set_status */
void Device::set_status(u32 s)
{
    write32(reg::STATUS, s);
}

/** @copydoc virtio::Device::get_status */
u32 Device::get_status()
{
    return read32(reg::STATUS);
}

/** @copydoc virtio::Device::add_status */
void Device::add_status(u32 bits)
{
    write32(reg::STATUS, get_status() | bits);
}

/** @copydoc virtio::Device::read_isr */
u32 Device::read_isr()
{
    return read32(reg::INTERRUPT_STATUS);
}

/** @copydoc virtio::Device::ack_interrupt */
void Device::ack_interrupt(u32 bits)
{
    write32(reg::INTERRUPT_ACK, bits);
}

// Probe for virtio devices
/** @copydoc virtio::init */
void init()
{
    serial::puts("[virtio] Probing for devices...\n");

    num_devices = 0;

    // QEMU virt machine: virtio MMIO at 0x0a000000-0x0a004000
    // Each device is 0x200 bytes apart
    for (u64 addr = 0x0a000000; addr < 0x0a004000 && num_devices < MAX_DEVICES; addr += 0x200)
    {
        volatile u32 *mmio = reinterpret_cast<volatile u32 *>(addr);

        // Check magic
        u32 magic = mmio[reg::MAGIC / 4];
        if (magic != MAGIC_VALUE)
        {
            continue;
        }

        // Check device ID
        u32 dev_id = mmio[reg::DEVICE_ID / 4];
        if (dev_id == 0)
        {
            continue; // No device
        }

        // Found a device
        devices[num_devices].base = addr;
        devices[num_devices].type = dev_id;
        devices[num_devices].in_use = false;

        const char *type_name = "unknown";
        switch (dev_id)
        {
            case device_type::NET:
                type_name = "network";
                break;
            case device_type::BLK:
                type_name = "block";
                break;
            case device_type::CONSOLE:
                type_name = "console";
                break;
            case device_type::RNG:
                type_name = "rng";
                break;
            case device_type::GPU:
                type_name = "gpu";
                break;
            case device_type::INPUT:
                type_name = "input";
                break;
        }

        serial::puts("[virtio] Found ");
        serial::puts(type_name);
        serial::puts(" device at ");
        serial::put_hex(addr);
        serial::puts("\n");

        num_devices++;
    }

    serial::puts("[virtio] Found ");
    serial::put_dec(num_devices);
    serial::puts(" device(s)\n");
}

/** @copydoc virtio::find_device */
u64 find_device(u32 type)
{
    for (usize i = 0; i < num_devices; i++)
    {
        if (devices[i].type == type && !devices[i].in_use)
        {
            devices[i].in_use = true;
            return devices[i].base;
        }
    }
    return 0;
}

/** @copydoc virtio::device_count */
usize device_count()
{
    return num_devices;
}

/** @copydoc virtio::get_device_info */
const DeviceInfo *get_device_info(usize index)
{
    if (index >= num_devices)
        return nullptr;
    return &devices[index];
}

} // namespace virtio
