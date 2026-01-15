/**
 * @file net.cpp
 * @brief User-space VirtIO network device driver implementation.
 */

#include "../include/net.hpp"
#include "../include/device.hpp"

namespace virtio
{

bool NetDevice::init(u64 mmio_phys, u32 irq)
{
    irq_num_ = irq;

    // Initialize base device (maps MMIO)
    if (!Device::init(mmio_phys))
    {
        return false;
    }

    // Verify device type
    if (device_id() != device_type::NET)
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

    // Read device features to check for MAC
    write32(reg::DEVICE_FEATURES_SEL, 0);
    u32 features_low = read32(reg::DEVICE_FEATURES);
    bool has_mac = (features_low & (1 << 5)) != 0; // MAC feature bit

    // Negotiate features
    if (!negotiate_features(0))
    {
        set_status(status::FAILED);
        return false;
    }

    // Read MAC address from config
    if (has_mac)
    {
        for (int i = 0; i < 6; i++)
        {
            mac_[i] = read_config8(i);
        }
    }
    else
    {
        // Use default MAC
        mac_[0] = 0x52;
        mac_[1] = 0x54;
        mac_[2] = 0x00;
        mac_[3] = 0x12;
        mac_[4] = 0x34;
        mac_[5] = 0x56;
    }

    // Initialize RX virtqueue (queue 0)
    if (!rx_vq_.init(this, 0, 128))
    {
        set_status(status::FAILED);
        return false;
    }

    // Initialize TX virtqueue (queue 1)
    if (!tx_vq_.init(this, 1, 128))
    {
        set_status(status::FAILED);
        return false;
    }

    // Allocate RX buffers
    usize rx_pool_size = RX_BUFFER_COUNT * sizeof(RxBuffer);
    device::DmaBuffer rx_dma;
    if (device::dma_alloc(rx_pool_size, &rx_dma) != 0)
    {
        set_status(status::FAILED);
        return false;
    }
    rx_buffers_ = reinterpret_cast<RxBuffer *>(rx_dma.virt_addr);
    rx_buffers_phys_ = rx_dma.phys_addr;
    rx_buffers_virt_ = rx_dma.virt_addr;

    // Initialize RX buffers
    for (usize i = 0; i < RX_BUFFER_COUNT; i++)
    {
        rx_buffers_[i].in_use = false;
        rx_buffers_[i].desc_idx = 0;
        // Zero the data
        for (usize j = 0; j < RX_BUFFER_SIZE; j++)
        {
            rx_buffers_[i].data[j] = 0;
        }
    }

    // Allocate TX header buffer
    device::DmaBuffer tx_dma;
    if (device::dma_alloc(sizeof(NetHeader), &tx_dma) != 0)
    {
        set_status(status::FAILED);
        return false;
    }
    tx_header_ = reinterpret_cast<NetHeader *>(tx_dma.virt_addr);
    tx_header_phys_ = tx_dma.phys_addr;
    tx_header_virt_ = tx_dma.virt_addr;

    // Initialize received packet queue
    for (usize i = 0; i < RX_QUEUE_SIZE; i++)
    {
        rx_queue_[i].data = nullptr;
        rx_queue_[i].len = 0;
        rx_queue_[i].valid = false;
    }

    // Post RX buffers to device
    refill_rx_buffers();

    // Device is ready
    add_status(status::DRIVER_OK);

    // Register for IRQ
    if (irq_num_ != 0)
    {
        device::irq_register(irq_num_);
    }

    return true;
}

void NetDevice::destroy()
{
    if (irq_num_ != 0)
    {
        device::irq_unregister(irq_num_);
    }

    rx_vq_.destroy();
    tx_vq_.destroy();

    // Free DMA buffers
    if (rx_buffers_virt_ != 0)
    {
        device::dma_free(rx_buffers_virt_);
        rx_buffers_ = nullptr;
        rx_buffers_virt_ = 0;
    }

    if (tx_header_virt_ != 0)
    {
        device::dma_free(tx_header_virt_);
        tx_header_ = nullptr;
        tx_header_virt_ = 0;
    }

    Device::destroy();
}

void NetDevice::get_mac(u8 *mac_out) const
{
    for (int i = 0; i < 6; i++)
    {
        mac_out[i] = mac_[i];
    }
}

void NetDevice::queue_rx_buffer(usize idx)
{
    if (idx >= RX_BUFFER_COUNT)
        return;

    RxBuffer *buf = &rx_buffers_[idx];
    if (buf->in_use)
        return;

    // Calculate physical address of this buffer
    u64 buf_phys = rx_buffers_phys_ + idx * sizeof(RxBuffer);

    // Allocate descriptor
    i32 desc = rx_vq_.alloc_desc();
    if (desc < 0)
        return;

    buf->in_use = true;
    buf->desc_idx = static_cast<u16>(desc);

    // Set up descriptor for device-writable buffer
    rx_vq_.set_desc(desc, buf_phys, RX_BUFFER_SIZE, desc_flags::WRITE);

    // Submit to available ring
    rx_vq_.submit(desc);
}

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

bool NetDevice::transmit(const void *data, usize len)
{
    if (len > 1514)
    { // Max Ethernet frame size
        return false;
    }

    // Allocate DMA buffer for the frame
    device::DmaBuffer frame_dma;
    if (device::dma_alloc(len, &frame_dma) != 0)
    {
        return false;
    }

    // Copy frame data
    u8 *frame_buf = reinterpret_cast<u8 *>(frame_dma.virt_addr);
    const u8 *src = static_cast<const u8 *>(data);
    for (usize i = 0; i < len; i++)
    {
        frame_buf[i] = src[i];
    }

    // Set up virtio header
    tx_header_->flags = 0;
    tx_header_->gso_type = net_gso::NONE;
    tx_header_->hdr_len = 0;
    tx_header_->gso_size = 0;
    tx_header_->csum_start = 0;
    tx_header_->csum_offset = 0;

    // Allocate two descriptors (header + data)
    i32 desc_hdr = tx_vq_.alloc_desc();
    i32 desc_data = tx_vq_.alloc_desc();

    if (desc_hdr < 0 || desc_data < 0)
    {
        if (desc_hdr >= 0)
            tx_vq_.free_desc(desc_hdr);
        if (desc_data >= 0)
            tx_vq_.free_desc(desc_data);
        device::dma_free(frame_dma.virt_addr);
        return false;
    }

    // Set up header descriptor
    tx_vq_.set_desc(desc_hdr, tx_header_phys_, sizeof(NetHeader), desc_flags::NEXT);
    tx_vq_.chain_desc(desc_hdr, desc_data);

    // Set up data descriptor
    tx_vq_.set_desc(desc_data, frame_dma.phys_addr, static_cast<u32>(len), 0);

    // Submit the chain
    tx_vq_.submit(desc_hdr);
    tx_vq_.kick();

    // Poll for completion
    while (tx_vq_.poll_used() < 0)
    {
        // Busy wait - could yield in a real implementation
        asm volatile("yield");
    }

    // Free descriptors
    tx_vq_.free_desc(desc_hdr);
    tx_vq_.free_desc(desc_data);

    // Free frame buffer
    device::dma_free(frame_dma.virt_addr);

    // Update stats
    tx_packets_++;
    tx_bytes_ += len;

    return true;
}

void NetDevice::poll_rx()
{
    while (true)
    {
        i32 desc = rx_vq_.poll_used();
        if (desc < 0)
            break;

        // Get length from used ring
        u32 len = rx_vq_.get_used_len(desc);

        // Find which buffer this descriptor belongs to
        for (usize i = 0; i < RX_BUFFER_COUNT; i++)
        {
            if (rx_buffers_[i].in_use && rx_buffers_[i].desc_idx == static_cast<u16>(desc))
            {
                // Skip virtio header
                if (len > sizeof(NetHeader))
                {
                    u16 frame_len = static_cast<u16>(len - sizeof(NetHeader));
                    u8 *frame_data = rx_buffers_[i].data + sizeof(NetHeader);

                    // Add to received queue if space available
                    usize next_tail = (rx_queue_tail_ + 1) % RX_QUEUE_SIZE;
                    if (next_tail != rx_queue_head_)
                    {
                        rx_queue_[rx_queue_tail_].data = frame_data;
                        rx_queue_[rx_queue_tail_].len = frame_len;
                        rx_queue_[rx_queue_tail_].valid = true;
                        rx_queue_tail_ = next_tail;

                        rx_packets_++;
                        rx_bytes_ += frame_len;
                    }
                }

                // Mark buffer as not in use (will be refilled)
                rx_buffers_[i].in_use = false;
                rx_vq_.free_desc(desc);
                break;
            }
        }
    }

    // Refill RX buffers
    refill_rx_buffers();
}

i32 NetDevice::receive(void *buf, usize max_len)
{
    // Check if we have received packets
    if (rx_queue_head_ == rx_queue_tail_ || !rx_queue_[rx_queue_head_].valid)
    {
        return 0;
    }

    ReceivedPacket *pkt = &rx_queue_[rx_queue_head_];
    u16 copy_len = pkt->len;
    if (copy_len > max_len)
    {
        copy_len = static_cast<u16>(max_len);
    }

    // Copy data
    u8 *dst = static_cast<u8 *>(buf);
    for (u16 i = 0; i < copy_len; i++)
    {
        dst[i] = pkt->data[i];
    }

    // Mark as consumed
    pkt->valid = false;
    rx_queue_head_ = (rx_queue_head_ + 1) % RX_QUEUE_SIZE;

    return copy_len;
}

void NetDevice::handle_interrupt()
{
    // Acknowledge interrupt
    u32 isr = read_isr();
    if (isr & 0x1)
    {
        ack_interrupt(0x1);
        // Process received packets
        poll_rx();
    }
    if (isr & 0x2)
    {
        ack_interrupt(0x2);
    }
}

bool NetDevice::has_rx_data() const
{
    return rx_queue_head_ != rx_queue_tail_ && rx_queue_[rx_queue_head_].valid;
}

bool NetDevice::link_up() const
{
    // Read status from config if STATUS feature was negotiated
    // For simplicity, assume link is always up
    return true;
}

} // namespace virtio
