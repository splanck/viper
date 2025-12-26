#include "net.hpp"
#include "../../console/serial.hpp"
#include "../../mm/pmm.hpp"

/**
 * @file net.cpp
 * @brief Virtio-net driver implementation.
 *
 * @details
 * Implements a minimal virtio-net driver for QEMU bring-up.
 *
 * RX path:
 * - Pre-post a pool of buffers to the RX virtqueue (device writes into them).
 * - Poll the used ring for completions and enqueue packet pointers.
 * - Copy packet data out to consumer buffers on demand.
 *
 * TX path:
 * - Submit a header+data descriptor chain to the TX queue.
 * - Poll for completion and free descriptors.
 */
namespace virtio
{

// Global network device instance
static NetDevice g_net_device;
static bool g_net_initialized = false;

/** @copydoc virtio::net_device */
NetDevice *net_device()
{
    return g_net_initialized ? &g_net_device : nullptr;
}

/** @copydoc virtio::NetDevice::init */
bool NetDevice::init()
{
    // Find virtio-net device
    u64 base = find_device(device_type::NET);
    if (!base)
    {
        serial::puts("[virtio-net] No network device found\n");
        return false;
    }

    // Initialize base device
    if (!Device::init(base))
    {
        serial::puts("[virtio-net] Device init failed\n");
        return false;
    }

    serial::puts("[virtio-net] Initializing network device at 0x");
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

    // Read MAC address from config space
    for (int i = 0; i < 6; i++)
    {
        mac_[i] = read_config8(i);
    }

    serial::puts("[virtio-net] MAC: ");
    for (int i = 0; i < 6; i++)
    {
        if (i > 0)
            serial::putc(':');
        // Print hex byte
        u8 b = mac_[i];
        const char hex[] = "0123456789abcdef";
        serial::putc(hex[(b >> 4) & 0xF]);
        serial::putc(hex[b & 0xF]);
    }
    serial::puts("\n");


    // Negotiate features
    // For modern virtio, we must negotiate VERSION_1
    u64 required = 0;
    if (!is_legacy())
    {
        required |= features::VERSION_1;
    }

    if (!negotiate_features(required))
    {
        serial::puts("[virtio-net] Feature negotiation failed\n");
        set_status(status::FAILED);
        return false;
    }

    // Initialize virtqueues
    // Queue 0: RX, Queue 1: TX
    if (!rx_vq_.init(this, 0, 64))
    {
        serial::puts("[virtio-net] RX virtqueue init failed\n");
        set_status(status::FAILED);
        return false;
    }

    if (!tx_vq_.init(this, 1, 64))
    {
        serial::puts("[virtio-net] TX virtqueue init failed\n");
        set_status(status::FAILED);
        return false;
    }

    // Allocate RX buffer pool (multiple pages)
    usize rx_pages = (sizeof(RxBuffer) * RX_BUFFER_COUNT + pmm::PAGE_SIZE - 1) / pmm::PAGE_SIZE;
    rx_buffers_phys_ = pmm::alloc_pages(rx_pages);
    if (!rx_buffers_phys_)
    {
        serial::puts("[virtio-net] Failed to allocate RX buffers\n");
        set_status(status::FAILED);
        return false;
    }
    rx_buffers_ = reinterpret_cast<RxBuffer *>(pmm::phys_to_virt(rx_buffers_phys_));

    // Zero RX buffers
    u8 *p = reinterpret_cast<u8 *>(rx_buffers_);
    for (usize i = 0; i < rx_pages * pmm::PAGE_SIZE; i++)
    {
        p[i] = 0;
    }

    // Allocate TX header buffer
    tx_header_phys_ = pmm::alloc_page();
    if (!tx_header_phys_)
    {
        serial::puts("[virtio-net] Failed to allocate TX header\n");
        set_status(status::FAILED);
        return false;
    }
    tx_header_ = reinterpret_cast<NetHeader *>(pmm::phys_to_virt(tx_header_phys_));

    // Zero TX header
    p = reinterpret_cast<u8 *>(tx_header_);
    for (usize i = 0; i < pmm::PAGE_SIZE; i++)
    {
        p[i] = 0;
    }

    // Initialize RX queue
    for (usize i = 0; i < RX_QUEUE_SIZE; i++)
    {
        rx_queue_[i].valid = false;
    }

    // Queue RX buffers
    for (usize i = 0; i < RX_BUFFER_COUNT; i++)
    {
        rx_buffers_[i].in_use = false;
        queue_rx_buffer(i);
    }

    // Kick RX queue to notify device we're ready to receive
    rx_vq_.kick();

    // Device is ready
    add_status(status::DRIVER_OK);

    serial::puts("[virtio-net] Driver initialized\n");
    return true;
}

/** @copydoc virtio::NetDevice::get_mac */
void NetDevice::get_mac(u8 *mac_out) const
{
    for (int i = 0; i < 6; i++)
    {
        mac_out[i] = mac_[i];
    }
}

/** @copydoc virtio::NetDevice::queue_rx_buffer */
void NetDevice::queue_rx_buffer(usize idx)
{
    if (idx >= RX_BUFFER_COUNT)
        return;
    if (rx_buffers_[idx].in_use)
        return;

    // Allocate descriptor
    i32 desc = rx_vq_.alloc_desc();
    if (desc < 0)
        return;

    // Calculate physical address of this buffer
    u64 buf_phys = rx_buffers_phys_ + idx * sizeof(RxBuffer);

    // Set up descriptor - device writes to this buffer
    rx_vq_.set_desc(desc, buf_phys, RX_BUFFER_SIZE, desc_flags::WRITE);

    rx_buffers_[idx].in_use = true;
    rx_buffers_[idx].desc_idx = desc;

    // Submit to available ring
    rx_vq_.submit(desc);
}

/** @copydoc virtio::NetDevice::refill_rx_buffers */
void NetDevice::refill_rx_buffers()
{
    for (usize i = 0; i < RX_BUFFER_COUNT; i++)
    {
        if (!rx_buffers_[i].in_use)
        {
            queue_rx_buffer(i);
        }
    }
    rx_vq_.kick();
}

/** @copydoc virtio::NetDevice::poll_rx */
void NetDevice::poll_rx()
{
    // Check and acknowledge interrupts
    u32 isr = read_isr();
    if (isr)
    {
        ack_interrupt(isr);
    }

    static u32 poll_count = 0;
    poll_count++;

    bool got_packet = false;

    while (true)
    {
        i32 desc = rx_vq_.poll_used();
        if (desc < 0)
            break;

        got_packet = true;

        // Find which buffer this is
        usize buf_idx = 0xFFFFFFFF;
        for (usize i = 0; i < RX_BUFFER_COUNT; i++)
        {
            if (rx_buffers_[i].in_use && rx_buffers_[i].desc_idx == static_cast<u16>(desc))
            {
                buf_idx = i;
                break;
            }
        }

        if (buf_idx >= RX_BUFFER_COUNT)
        {
            // Unknown buffer, free descriptor
            rx_vq_.free_desc(desc);
            continue;
        }

        // Get the received length
        u32 len = rx_vq_.get_used_len(desc);

        rx_vq_.free_desc(desc);
        rx_buffers_[buf_idx].in_use = false;

        // Skip virtio-net header
        if (len <= sizeof(NetHeader))
        {
            continue;
        }

        u8 *data = rx_buffers_[buf_idx].data + sizeof(NetHeader);
        u16 pkt_len = len - sizeof(NetHeader);

        // Add to received queue if space available
        usize next_tail = (rx_queue_tail_ + 1) % RX_QUEUE_SIZE;
        if (next_tail != rx_queue_head_)
        {
            // Allocate buffer for the packet
            // For now, we just point to the rx buffer data
            // This is safe because we'll copy it out before reusing
            rx_queue_[rx_queue_tail_].data = data;
            rx_queue_[rx_queue_tail_].len = pkt_len;
            rx_queue_[rx_queue_tail_].valid = true;
            rx_queue_tail_ = next_tail;

            rx_packets_++;
            rx_bytes_ += pkt_len;
        }
        else
        {
            // Queue full, drop packet
            rx_dropped_++;
        }
    }

    if (got_packet)
    {
        refill_rx_buffers();
    }
}

/** @copydoc virtio::NetDevice::receive */
i32 NetDevice::receive(void *buf, usize max_len)
{
    // Poll for new packets
    poll_rx();

    // Check if we have a packet in the queue
    if (rx_queue_head_ == rx_queue_tail_)
    {
        return 0; // No packet
    }

    if (!rx_queue_[rx_queue_head_].valid)
    {
        return 0; // No valid packet
    }

    // Copy packet data
    u16 pkt_len = rx_queue_[rx_queue_head_].len;
    u16 copy_len = pkt_len < max_len ? pkt_len : max_len;

    u8 *src = rx_queue_[rx_queue_head_].data;
    u8 *dst = reinterpret_cast<u8 *>(buf);
    for (usize i = 0; i < copy_len; i++)
    {
        dst[i] = src[i];
    }

    // Mark as consumed
    rx_queue_[rx_queue_head_].valid = false;
    rx_queue_head_ = (rx_queue_head_ + 1) % RX_QUEUE_SIZE;

    return copy_len;
}

/** @copydoc virtio::NetDevice::transmit */
bool NetDevice::transmit(const void *data, usize len)
{
    if (!data || len == 0 || len > 1514)
    { // Max Ethernet frame
        return false;
    }

    // Allocate descriptors - one for header, one for data
    i32 hdr_desc = tx_vq_.alloc_desc();
    i32 data_desc = tx_vq_.alloc_desc();

    if (hdr_desc < 0 || data_desc < 0)
    {
        if (hdr_desc >= 0)
            tx_vq_.free_desc(hdr_desc);
        if (data_desc >= 0)
            tx_vq_.free_desc(data_desc);
        tx_dropped_++;
        return false;
    }

    // Prepare virtio-net header (all zeros for simple transmit)
    tx_header_->flags = 0;
    tx_header_->gso_type = net_gso::NONE;
    tx_header_->hdr_len = 0;
    tx_header_->gso_size = 0;
    tx_header_->csum_start = 0;
    tx_header_->csum_offset = 0;

    // Memory barrier
    asm volatile("dsb sy" ::: "memory");

    // Get physical address of data
    // Assuming data is in kernel identity-mapped region
    u64 data_phys = pmm::virt_to_phys(const_cast<void *>(data));

    // Set up descriptor chain
    // Header descriptor (device reads)
    tx_vq_.set_desc(hdr_desc, tx_header_phys_, sizeof(NetHeader), desc_flags::NEXT);
    tx_vq_.chain_desc(hdr_desc, data_desc);

    // Data descriptor (device reads)
    tx_vq_.set_desc(data_desc, data_phys, len, 0);

    // Submit and kick
    tx_vq_.submit(hdr_desc);
    tx_vq_.kick();

    // Wait for completion (blocking)
    bool completed = false;
    for (u32 i = 0; i < 1000000; i++)
    {
        i32 used = tx_vq_.poll_used();
        if (used == hdr_desc)
        {
            completed = true;
            break;
        }
        asm volatile("yield" ::: "memory");
    }

    // Free descriptors
    tx_vq_.free_desc(hdr_desc);
    tx_vq_.free_desc(data_desc);

    if (!completed)
    {
        tx_dropped_++;
        return false;
    }

    tx_packets_++;
    tx_bytes_ += len;

    return true;
}

/** @copydoc virtio::NetDevice::link_up */
bool NetDevice::link_up() const
{
    // Read status from config if available
    // For QEMU user networking, link is always up
    return true;
}

/** @copydoc virtio::net_init */
void net_init()
{
    serial::puts("[virtio-net] Starting net_init()...\n");
    if (g_net_device.init())
    {
        g_net_initialized = true;
        serial::puts("[virtio-net] Network device ready\n");
    }
    else
    {
        serial::puts("[virtio-net] Network device initialization failed\n");
    }
}

} // namespace virtio
