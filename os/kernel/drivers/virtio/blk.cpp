#include "blk.hpp"
#include "../../console/serial.hpp"
#include "../../mm/pmm.hpp"

/**
 * @file blk.cpp
 * @brief Virtio block device driver implementation.
 *
 * @details
 * Implements a simple polling-based virtio-blk driver suitable for QEMU
 * bring-up. Requests are submitted via a single virtqueue and the driver polls
 * the used ring for completion.
 *
 * The driver assumes buffers are identity-mapped so it can compute physical
 * addresses directly using `pmm::virt_to_phys`.
 */

// Freestanding offsetof
#define OFFSETOF(type, member) __builtin_offsetof(type, member)

namespace virtio
{

// Global block device instance
static BlkDevice g_blk_device;
static bool g_blk_initialized = false;

/** @copydoc virtio::blk_device */
BlkDevice *blk_device()
{
    return g_blk_initialized ? &g_blk_device : nullptr;
}

/** @copydoc virtio::BlkDevice::init */
bool BlkDevice::init()
{
    // Find virtio-blk device
    u64 base = find_device(device_type::BLK);
    if (!base)
    {
        serial::puts("[virtio-blk] No block device found\n");
        return false;
    }

    // Initialize base device
    if (!Device::init(base))
    {
        serial::puts("[virtio-blk] Device init failed\n");
        return false;
    }

    serial::puts("[virtio-blk] Initializing block device at ");
    serial::put_hex(base);
    serial::puts("\n");

    // Reset device
    reset();

    // For legacy mode, set guest page size
    if (is_legacy())
    {
        write32(reg::GUEST_PAGE_SIZE, 4096);
    }

    // Acknowledge device
    add_status(status::ACKNOWLEDGE);
    add_status(status::DRIVER);

    // Read configuration
    capacity_ = read_config64(0); // offset 0: capacity

    // Try to read block size from config
    // This is optional - default is 512
    sector_size_ = 512;

    // Check for read-only
    write32(reg::DEVICE_FEATURES_SEL, 0);
    u32 features = read32(reg::DEVICE_FEATURES);
    readonly_ = (features & blk_features::RO) != 0;

    serial::puts("[virtio-blk] Capacity: ");
    serial::put_dec(capacity_);
    serial::puts(" sectors (");
    serial::put_dec((capacity_ * sector_size_) / (1024 * 1024));
    serial::puts(" MB)\n");

    if (readonly_)
    {
        serial::puts("[virtio-blk] Device is read-only\n");
    }

    // Negotiate features - we just need basic read/write
    if (!negotiate_features(0))
    {
        serial::puts("[virtio-blk] Feature negotiation failed\n");
        set_status(status::FAILED);
        return false;
    }

    // Initialize virtqueue
    if (!vq_.init(this, 0, 128))
    {
        serial::puts("[virtio-blk] Virtqueue init failed\n");
        set_status(status::FAILED);
        return false;
    }

    // Allocate request buffer (page for pending requests)
    requests_phys_ = pmm::alloc_page();
    if (!requests_phys_)
    {
        serial::puts("[virtio-blk] Failed to allocate request buffer\n");
        set_status(status::FAILED);
        return false;
    }
    requests_ = reinterpret_cast<PendingRequest *>(pmm::phys_to_virt(requests_phys_));

    // Zero request buffer
    for (usize i = 0; i < pmm::PAGE_SIZE / sizeof(u64); i++)
    {
        reinterpret_cast<u64 *>(requests_)[i] = 0;
    }

    // Device is ready
    add_status(status::DRIVER_OK);

    serial::puts("[virtio-blk] Driver initialized\n");
    return true;
}

/** @copydoc virtio::BlkDevice::do_request */
i32 BlkDevice::do_request(u32 type, u64 sector, u32 count, void *buf)
{
    if (type == blk_type::OUT && readonly_)
    {
        serial::puts("[virtio-blk] Write to read-only device\n");
        return -1;
    }

    // Find a free request slot
    int req_idx = -1;
    for (usize i = 0; i < MAX_PENDING; i++)
    {
        if (!requests_[i].in_use)
        {
            req_idx = i;
            break;
        }
    }
    if (req_idx < 0)
    {
        serial::puts("[virtio-blk] No free request slots\n");
        return -1;
    }

    PendingRequest &req = requests_[req_idx];
    req.in_use = true;
    req.header.type = type;
    req.header.reserved = 0;
    req.header.sector = sector;
    req.status = 0xFF; // Invalid/pending

    // Calculate physical addresses
    u64 header_phys = requests_phys_ + req_idx * sizeof(PendingRequest);
    u64 status_phys = header_phys + OFFSETOF(PendingRequest, status);

    // Get physical address of data buffer
    // Assuming buf is in kernel identity-mapped region
    u64 buf_phys = pmm::virt_to_phys(buf);
    u32 buf_len = count * sector_size_;

    // Allocate 3 descriptors for the request chain
    i32 desc0 = vq_.alloc_desc();
    i32 desc1 = vq_.alloc_desc();
    i32 desc2 = vq_.alloc_desc();

    if (desc0 < 0 || desc1 < 0 || desc2 < 0)
    {
        if (desc0 >= 0)
            vq_.free_desc(desc0);
        if (desc1 >= 0)
            vq_.free_desc(desc1);
        if (desc2 >= 0)
            vq_.free_desc(desc2);
        req.in_use = false;
        serial::puts("[virtio-blk] No free descriptors\n");
        return -1;
    }

    // Descriptor 0: Request header (device reads)
    vq_.set_desc(desc0, header_phys, sizeof(BlkReqHeader), desc_flags::NEXT);
    vq_.chain_desc(desc0, desc1);

    // Descriptor 1: Data buffer
    u16 data_flags = desc_flags::NEXT;
    if (type == blk_type::IN)
    {
        data_flags |= desc_flags::WRITE; // Device writes to this buffer
    }
    vq_.set_desc(desc1, buf_phys, buf_len, data_flags);
    vq_.chain_desc(desc1, desc2);

    // Descriptor 2: Status (device writes)
    vq_.set_desc(desc2, status_phys, 1, desc_flags::WRITE);

    // Memory barrier before submitting
    asm volatile("dsb sy" ::: "memory");

    // Submit and notify
    vq_.submit(desc0);
    vq_.kick();

    // Wait for completion (polling with timeout)
    bool got_completion = false;
    for (u32 i = 0; i < 10000000; i++)
    {
        i32 completed = vq_.poll_used();
        if (completed == desc0)
        {
            got_completion = true;
            break;
        }
        // Yield CPU while waiting
        asm volatile("yield" ::: "memory");
    }

    if (!got_completion)
    {
        serial::puts("[virtio-blk] Request timed out!\n");
        vq_.free_desc(desc0);
        vq_.free_desc(desc1);
        vq_.free_desc(desc2);
        req.in_use = false;
        return -1;
    }

    // Free descriptors
    vq_.free_desc(desc0);
    vq_.free_desc(desc1);
    vq_.free_desc(desc2);

    // Check status
    u8 status = req.status;
    req.in_use = false;

    if (status != blk_status::OK)
    {
        serial::puts("[virtio-blk] Request failed, status=");
        serial::put_dec(status);
        serial::puts("\n");
        return -1;
    }

    return 0;
}

/** @copydoc virtio::BlkDevice::read_sectors */
i32 BlkDevice::read_sectors(u64 sector, u32 count, void *buf)
{
    if (!buf || count == 0)
        return -1;
    if (sector + count > capacity_)
    {
        serial::puts("[virtio-blk] Read past end of disk\n");
        return -1;
    }

    return do_request(blk_type::IN, sector, count, buf);
}

/** @copydoc virtio::BlkDevice::write_sectors */
i32 BlkDevice::write_sectors(u64 sector, u32 count, const void *buf)
{
    if (!buf || count == 0)
        return -1;
    if (sector + count > capacity_)
    {
        serial::puts("[virtio-blk] Write past end of disk\n");
        return -1;
    }

    return do_request(blk_type::OUT, sector, count, const_cast<void *>(buf));
}

/** @copydoc virtio::BlkDevice::flush */
i32 BlkDevice::flush()
{
    // Find a free request slot
    int req_idx = -1;
    for (usize i = 0; i < MAX_PENDING; i++)
    {
        if (!requests_[i].in_use)
        {
            req_idx = i;
            break;
        }
    }
    if (req_idx < 0)
        return -1;

    PendingRequest &req = requests_[req_idx];
    req.in_use = true;
    req.header.type = blk_type::FLUSH;
    req.header.reserved = 0;
    req.header.sector = 0;
    req.status = 0xFF;

    u64 header_phys = requests_phys_ + req_idx * sizeof(PendingRequest);
    u64 status_phys = header_phys + OFFSETOF(PendingRequest, status);

    // Flush only needs header and status
    i32 desc0 = vq_.alloc_desc();
    i32 desc1 = vq_.alloc_desc();

    if (desc0 < 0 || desc1 < 0)
    {
        if (desc0 >= 0)
            vq_.free_desc(desc0);
        if (desc1 >= 0)
            vq_.free_desc(desc1);
        req.in_use = false;
        return -1;
    }

    vq_.set_desc(desc0, header_phys, sizeof(BlkReqHeader), desc_flags::NEXT);
    vq_.chain_desc(desc0, desc1);
    vq_.set_desc(desc1, status_phys, 1, desc_flags::WRITE);

    vq_.submit(desc0);
    vq_.kick();

    while (true)
    {
        i32 completed = vq_.poll_used();
        if (completed == desc0)
            break;
        asm volatile("yield" ::: "memory");
    }

    vq_.free_desc(desc0);
    vq_.free_desc(desc1);

    u8 status = req.status;
    req.in_use = false;

    return (status == blk_status::OK) ? 0 : -1;
}

/** @copydoc virtio::blk_init */
void blk_init()
{
    if (g_blk_device.init())
    {
        g_blk_initialized = true;
    }
}

} // namespace virtio
