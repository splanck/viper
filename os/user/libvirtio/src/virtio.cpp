/**
 * @file virtio.cpp
 * @brief User-space Virtio-MMIO implementation.
 *
 * @details
 * Implements the user-space `virtio::Device` helper using device access
 * syscalls for MMIO mapping and interrupt handling.
 */
#include "../include/virtio.hpp"

namespace virtio
{

// Device registry (populated by init())
static DeviceInfo devices[MAX_DEVICES];
static usize num_devices = 0;

// Size of virtio MMIO region to map
constexpr u64 MMIO_SIZE = 0x200;

bool Device::init(u64 phys_addr)
{
    phys_base_ = phys_addr;

    // Map the device MMIO region into our address space
    virt_base_ = device::map_device(phys_addr, MMIO_SIZE);
    if (virt_base_ == 0)
    {
        return false;
    }

    mmio_ = reinterpret_cast<volatile u32 *>(virt_base_);

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
        return false;
    }

    device_id_ = read32(reg::DEVICE_ID);
    if (device_id_ == 0)
    {
        return false;
    }

    return true;
}

void Device::destroy()
{
    if (irq_registered_)
    {
        unregister_irq();
    }
    // Note: MMIO mapping is cleaned up on process exit
    mmio_ = nullptr;
    virt_base_ = 0;
}

void Device::reset()
{
    write32(reg::STATUS, 0);
    // Wait for reset to complete
    while (read32(reg::STATUS) != 0)
    {
        asm volatile("yield");
    }
}

u32 Device::read32(u32 offset)
{
    return mmio_[offset / 4];
}

void Device::write32(u32 offset, u32 value)
{
    mmio_[offset / 4] = value;
}

u8 Device::read_config8(u32 offset)
{
    volatile u8 *config =
        reinterpret_cast<volatile u8 *>(reinterpret_cast<u64>(mmio_) + reg::CONFIG + offset);
    return *config;
}

u16 Device::read_config16(u32 offset)
{
    volatile u16 *config =
        reinterpret_cast<volatile u16 *>(reinterpret_cast<u64>(mmio_) + reg::CONFIG + offset);
    return *config;
}

u32 Device::read_config32(u32 offset)
{
    return read32(reg::CONFIG + offset);
}

u64 Device::read_config64(u32 offset)
{
    u32 lo = read32(reg::CONFIG + offset);
    u32 hi = read32(reg::CONFIG + offset + 4);
    return (static_cast<u64>(hi) << 32) | lo;
}

bool Device::negotiate_features(u64 required)
{
    if (is_legacy())
    {
        // Legacy: simpler feature negotiation
        u32 device_features = read32(reg::DEVICE_FEATURES);

        if ((device_features & static_cast<u32>(required)) != static_cast<u32>(required))
        {
            return false;
        }

        write32(reg::DRIVER_FEATURES, required & 0xFFFFFFFF);
        return true;
    }

    // Modern: full 64-bit feature negotiation
    write32(reg::DEVICE_FEATURES_SEL, 0);
    u32 features_lo = read32(reg::DEVICE_FEATURES);

    write32(reg::DEVICE_FEATURES_SEL, 1);
    u32 features_hi = read32(reg::DEVICE_FEATURES);

    u64 device_features = (static_cast<u64>(features_hi) << 32) | features_lo;

    if ((device_features & required) != required)
    {
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
        return false;
    }

    return true;
}

void Device::set_status(u32 s)
{
    write32(reg::STATUS, s);
}

u32 Device::get_status()
{
    return read32(reg::STATUS);
}

void Device::add_status(u32 bits)
{
    write32(reg::STATUS, get_status() | bits);
}

u32 Device::read_isr()
{
    return read32(reg::INTERRUPT_STATUS);
}

void Device::ack_interrupt(u32 bits)
{
    write32(reg::INTERRUPT_ACK, bits);
}

bool Device::register_irq()
{
    if (irq_ == 0)
    {
        return false;
    }

    i64 err = device::irq_register(irq_);
    if (err != 0)
    {
        return false;
    }

    irq_registered_ = true;
    return true;
}

void Device::unregister_irq()
{
    if (irq_registered_)
    {
        device::irq_unregister(irq_);
        irq_registered_ = false;
    }
}

i64 Device::wait_irq(u64 timeout_ms)
{
    if (!irq_registered_)
    {
        return -1;
    }
    return device::irq_wait(irq_, timeout_ms);
}

i64 Device::ack_irq()
{
    if (!irq_registered_)
    {
        return -1;
    }
    return device::irq_ack(irq_);
}

// Device scanning
void init()
{
    num_devices = 0;

    // Use device enumeration syscall
    device::DeviceInfo dev_infos[MAX_DEVICES];
    i64 count = device::enumerate(dev_infos, MAX_DEVICES);

    if (count <= 0)
    {
        // Fall back to scanning known virtio addresses
        // QEMU virt machine: virtio MMIO at 0x0a000000-0x0a004000
        for (u64 addr = 0x0a000000; addr < 0x0a004000 && num_devices < MAX_DEVICES; addr += 0x200)
        {
            // Try to map and probe the device
            u64 virt = device::map_device(addr, MMIO_SIZE);
            if (virt == 0)
            {
                continue;
            }

            volatile u32 *mmio = reinterpret_cast<volatile u32 *>(virt);

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
                continue;
            }

            devices[num_devices].base = addr;
            devices[num_devices].type = dev_id;
            devices[num_devices].in_use = false;
            num_devices++;
        }
        return;
    }

    // Process enumerated devices
    for (i64 i = 0; i < count && num_devices < MAX_DEVICES; i++)
    {
        // Check if this is a virtio device (in the virtio MMIO range)
        u64 addr = dev_infos[i].mmio_base;
        if (addr >= 0x0a000000 && addr < 0x0a004000)
        {
            // Try to probe it
            u64 virt = device::map_device(addr, MMIO_SIZE);
            if (virt == 0)
            {
                continue;
            }

            volatile u32 *mmio = reinterpret_cast<volatile u32 *>(virt);

            u32 magic = mmio[reg::MAGIC / 4];
            if (magic != MAGIC_VALUE)
            {
                continue;
            }

            u32 dev_id = mmio[reg::DEVICE_ID / 4];
            if (dev_id == 0)
            {
                continue;
            }

            devices[num_devices].base = addr;
            devices[num_devices].type = dev_id;
            devices[num_devices].in_use = false;
            num_devices++;
        }
    }
}

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

usize device_count()
{
    return num_devices;
}

const DeviceInfo *get_device_info(usize index)
{
    if (index >= num_devices)
    {
        return nullptr;
    }
    return &devices[index];
}

} // namespace virtio
