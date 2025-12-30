/**
 * @file blk.cpp
 * @brief User-space VirtIO block device driver implementation.
 */
#include "../include/blk.hpp"

// Freestanding offsetof
#define OFFSETOF(type, member) __builtin_offsetof(type, member)

namespace virtio
{

bool BlkDevice::init(u64 mmio_phys, u32 irq)
{
    irq_num_ = irq;

    // Initialize base device (maps MMIO)
    if (!Device::init(mmio_phys))
    {
        return false;
    }

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
    capacity_ = read_config64(0);
    sector_size_ = 512;

    // Check for read-only
    write32(reg::DEVICE_FEATURES_SEL, 0);
    u32 features = read32(reg::DEVICE_FEATURES);
    readonly_ = (features & blk_features::RO) != 0;

    // Negotiate features
    if (!negotiate_features(0))
    {
        set_status(status::FAILED);
        return false;
    }

    // Initialize virtqueue
    if (!vq_.init(this, 0, 128))
    {
        set_status(status::FAILED);
        return false;
    }

    // Allocate DMA buffer for pending requests
    device::DmaBuffer req_buf;
    if (device::dma_alloc(PAGE_SIZE, &req_buf) != 0)
    {
        set_status(status::FAILED);
        return false;
    }
    requests_phys_ = req_buf.phys_addr;
    requests_virt_ = req_buf.virt_addr;
    requests_ = reinterpret_cast<PendingRequest *>(requests_virt_);

    // Zero request buffer
    u8 *ptr = reinterpret_cast<u8 *>(requests_virt_);
    for (usize i = 0; i < PAGE_SIZE; i++)
    {
        ptr[i] = 0;
    }

    // Device is ready
    add_status(status::DRIVER_OK);

    // Register for IRQ
    if (irq_num_ != 0)
    {
        device::irq_register(irq_num_);
    }

    return true;
}

void BlkDevice::destroy()
{
    if (irq_num_ != 0)
    {
        device::irq_unregister(irq_num_);
    }

    vq_.destroy();

    if (requests_virt_ != 0)
    {
        device::dma_free(requests_virt_);
    }

    Device::destroy();
}

void BlkDevice::handle_interrupt()
{
    u32 isr = read_isr();
    if (isr & 0x1)
    {
        ack_interrupt(0x1);

        i32 completed = vq_.poll_used();
        if (completed >= 0)
        {
            completed_desc_ = completed;
            io_complete_ = true;
        }
    }
    if (isr & 0x2)
    {
        ack_interrupt(0x2);
    }
}

i32 BlkDevice::do_request(u32 type, u64 sector, u32 count, void *buf)
{
    if (type == blk_type::OUT && readonly_)
    {
        return -1;
    }

    // Find a free request slot
    int req_idx = -1;
    for (usize i = 0; i < MAX_PENDING; i++)
    {
        if (!slots_[i].in_use)
        {
            req_idx = i;
            break;
        }
    }
    if (req_idx < 0)
    {
        return -1;
    }

    PendingRequest &req = requests_[req_idx];
    RequestSlot &slot = slots_[req_idx];
    slot.in_use = true;
    req.header.type = type;
    req.header.reserved = 0;
    req.header.sector = sector;
    req.status = 0xFF;

    // Calculate physical addresses
    u64 header_phys = requests_phys_ + req_idx * sizeof(PendingRequest);
    u64 status_phys = header_phys + OFFSETOF(PendingRequest, status);

    // Get physical address of data buffer
    u64 buf_phys = device::virt_to_phys(reinterpret_cast<u64>(buf));
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
        slot.in_use = false;
        return -1;
    }

    slot.desc_head = desc0;
    slot.desc_data = desc1;
    slot.desc_status = desc2;

    // Descriptor 0: Request header (device reads)
    vq_.set_desc(desc0, header_phys, sizeof(BlkReqHeader), desc_flags::NEXT);
    vq_.chain_desc(desc0, desc1);

    // Descriptor 1: Data buffer
    u16 data_flags = desc_flags::NEXT;
    if (type == blk_type::IN)
    {
        data_flags |= desc_flags::WRITE;
    }
    vq_.set_desc(desc1, buf_phys, buf_len, data_flags);
    vq_.chain_desc(desc1, desc2);

    // Descriptor 2: Status (device writes)
    vq_.set_desc(desc2, status_phys, 1, desc_flags::WRITE);

    // Clear completion state
    io_complete_ = false;
    completed_desc_ = -1;

    // Memory barrier before submitting
    asm volatile("dsb sy" ::: "memory");

    // Submit and notify
    vq_.submit(desc0);
    vq_.kick();

    // Wait for completion using IRQ
    bool got_completion = false;
    constexpr u32 IRQ_TIMEOUT_ITERS = 100;
    constexpr u32 POLL_TIMEOUT_ITERS = 10000000;

    // Try IRQ-based waiting first
    for (u32 i = 0; i < IRQ_TIMEOUT_ITERS; i++)
    {
        // Wait for IRQ
        i64 err = device::irq_wait(irq_num_, 100); // 100ms timeout
        if (err == 0)
        {
            handle_interrupt();
            device::irq_ack(irq_num_);

            if (io_complete_ && completed_desc_ == desc0)
            {
                got_completion = true;
                break;
            }
        }
    }

    // Fallback to polling if IRQ failed
    if (!got_completion)
    {
        for (u32 i = 0; i < POLL_TIMEOUT_ITERS; i++)
        {
            i32 completed = vq_.poll_used();
            if (completed == desc0)
            {
                got_completion = true;
                break;
            }
            asm volatile("yield" ::: "memory");
        }
    }

    if (!got_completion)
    {
        vq_.free_desc(desc0);
        vq_.free_desc(desc1);
        vq_.free_desc(desc2);
        slot.in_use = false;
        return -1;
    }

    // Free descriptors
    vq_.free_desc(desc0);
    vq_.free_desc(desc1);
    vq_.free_desc(desc2);

    u8 result_status = req.status;
    slot.in_use = false;

    return (result_status == blk_status::OK) ? 0 : -1;
}

i32 BlkDevice::read_sectors(u64 sector, u32 count, void *buf)
{
    if (!buf || count == 0)
        return -1;
    if (sector + count > capacity_)
        return -1;

    return do_request(blk_type::IN, sector, count, buf);
}

i32 BlkDevice::write_sectors(u64 sector, u32 count, const void *buf)
{
    if (!buf || count == 0)
        return -1;
    if (sector + count > capacity_)
        return -1;

    return do_request(blk_type::OUT, sector, count, const_cast<void *>(buf));
}

i32 BlkDevice::flush()
{
    int req_idx = -1;
    for (usize i = 0; i < MAX_PENDING; i++)
    {
        if (!slots_[i].in_use)
        {
            req_idx = i;
            break;
        }
    }
    if (req_idx < 0)
        return -1;

    PendingRequest &req = requests_[req_idx];
    RequestSlot &slot = slots_[req_idx];
    slot.in_use = true;
    req.header.type = blk_type::FLUSH;
    req.header.reserved = 0;
    req.header.sector = 0;
    req.status = 0xFF;

    u64 header_phys = requests_phys_ + req_idx * sizeof(PendingRequest);
    u64 status_phys = header_phys + OFFSETOF(PendingRequest, status);

    i32 desc0 = vq_.alloc_desc();
    i32 desc1 = vq_.alloc_desc();

    if (desc0 < 0 || desc1 < 0)
    {
        if (desc0 >= 0)
            vq_.free_desc(desc0);
        if (desc1 >= 0)
            vq_.free_desc(desc1);
        slot.in_use = false;
        return -1;
    }

    slot.desc_head = desc0;
    slot.desc_data = -1;
    slot.desc_status = desc1;

    vq_.set_desc(desc0, header_phys, sizeof(BlkReqHeader), desc_flags::NEXT);
    vq_.chain_desc(desc0, desc1);
    vq_.set_desc(desc1, status_phys, 1, desc_flags::WRITE);

    io_complete_ = false;
    completed_desc_ = -1;

    vq_.submit(desc0);
    vq_.kick();

    // Wait for completion
    bool got_completion = false;
    for (u32 i = 0; i < 100; i++)
    {
        i64 err = device::irq_wait(irq_num_, 100);
        if (err == 0)
        {
            handle_interrupt();
            device::irq_ack(irq_num_);

            if (io_complete_ && completed_desc_ == desc0)
            {
                got_completion = true;
                break;
            }
        }
    }

    // Fallback to polling
    if (!got_completion)
    {
        while (true)
        {
            i32 completed = vq_.poll_used();
            if (completed == desc0)
            {
                got_completion = true;
                break;
            }
            asm volatile("yield" ::: "memory");
        }
    }

    vq_.free_desc(desc0);
    vq_.free_desc(desc1);

    u8 result_status = req.status;
    slot.in_use = false;

    return (result_status == blk_status::OK) ? 0 : -1;
}

} // namespace virtio
