#include "rng.hpp"
#include "../../console/serial.hpp"
#include "../../mm/pmm.hpp"

/**
 * @file rng.cpp
 * @brief Virtio-rng driver implementation.
 *
 * @details
 * Implements a simple polling-based virtio RNG driver:
 * - Initializes a virtqueue and a DMA buffer.
 * - Submits a writable descriptor pointing at the buffer.
 * - Polls for completion and copies returned bytes to the caller buffer.
 */
namespace virtio
{
namespace rng
{

// RNG device state
static Device device;
static Virtqueue vq;
static bool initialized = false;

// Buffer for RNG requests (page-aligned for DMA)
static u8 *rng_buffer = nullptr;
static u64 rng_buffer_phys = 0;
constexpr usize RNG_BUFFER_SIZE = 256;

/** @copydoc virtio::rng::init */
bool init()
{
    serial::puts("[virtio-rng] Scanning for RNG device...\n");

    // Find RNG device
    u64 base = find_device(device_type::RNG);
    if (base == 0)
    {
        serial::puts("[virtio-rng] No RNG device found\n");
        return false;
    }

    serial::puts("[virtio-rng] Found RNG device at ");
    serial::put_hex(base);
    serial::puts("\n");

    // Use common init sequence (init, reset, legacy page size, acknowledge, driver)
    if (!device.basic_init(base))
    {
        serial::puts("[virtio-rng] Failed to initialize device\n");
        return false;
    }

    // For RNG, no special features needed - just accept what device offers
    // Legacy devices don't require FEATURES_OK
    if (!device.is_legacy())
    {
        device.add_status(status::FEATURES_OK);
        if (!(device.get_status() & status::FEATURES_OK))
        {
            serial::puts("[virtio-rng] Device rejected features\n");
            device.add_status(status::FAILED);
            return false;
        }
    }

    // Initialize virtqueue 0
    if (!vq.init(&device, 0, 0))
    {
        serial::puts("[virtio-rng] Failed to initialize virtqueue\n");
        device.add_status(status::FAILED);
        return false;
    }

    // Allocate DMA buffer for RNG data
    u64 page = pmm::alloc_page();
    if (page == 0)
    {
        serial::puts("[virtio-rng] Failed to allocate RNG buffer\n");
        device.add_status(status::FAILED);
        return false;
    }
    rng_buffer = reinterpret_cast<u8 *>(page);
    rng_buffer_phys = page;

    // Mark device as ready
    device.add_status(status::DRIVER_OK);

    initialized = true;
    serial::puts("[virtio-rng] RNG device initialized (entropy source available)\n");

    return true;
}

/** @copydoc virtio::rng::is_available */
bool is_available()
{
    return initialized;
}

/** @copydoc virtio::rng::get_bytes */
usize get_bytes(u8 *buffer, usize len)
{
    if (!initialized || !buffer || len == 0)
    {
        return 0;
    }

    usize total = 0;

    while (total < len)
    {
        // Request up to RNG_BUFFER_SIZE bytes at a time
        usize request_len = len - total;
        if (request_len > RNG_BUFFER_SIZE)
        {
            request_len = RNG_BUFFER_SIZE;
        }

        // Allocate descriptor
        i32 desc_idx = vq.alloc_desc();
        if (desc_idx < 0)
        {
            // No descriptors available, return what we have
            break;
        }

        // Set up descriptor: device writes to our buffer
        vq.set_desc(static_cast<u32>(desc_idx),
                    rng_buffer_phys,
                    static_cast<u32>(request_len),
                    desc_flags::WRITE);

        // Submit to available ring
        vq.submit(static_cast<u32>(desc_idx));

        // Notify device
        vq.kick();

        // Poll for completion (with timeout)
        i32 used_desc = -1;
        for (int tries = 0; tries < 100000; tries++)
        {
            used_desc = vq.poll_used();
            if (used_desc >= 0)
            {
                break;
            }
            // Memory barrier
            asm volatile("dmb sy" ::: "memory");
        }

        if (used_desc < 0)
        {
            // Timeout - free descriptor and bail
            vq.free_desc(static_cast<u32>(desc_idx));
            break;
        }

        // Get actual bytes returned
        u32 returned = vq.get_used_len(static_cast<u32>(used_desc));
        if (returned > request_len)
        {
            returned = static_cast<u32>(request_len);
        }

        // Copy to user buffer
        for (u32 i = 0; i < returned; i++)
        {
            buffer[total + i] = rng_buffer[i];
        }
        total += returned;

        // Free descriptor
        vq.free_chain(static_cast<u32>(used_desc));
    }

    return total;
}

} // namespace rng
} // namespace virtio
