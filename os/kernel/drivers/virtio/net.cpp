#include "net.hpp"
#include "../../arch/aarch64/gic.hpp"
#include "../../console/serial.hpp"
#include "../../mm/pmm.hpp"
#include "../../sched/scheduler.hpp"
#include "../../sched/task.hpp"

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

/**
 * @brief Calculate the IRQ number for a virtio-mmio device.
 *
 * @details
 * On the QEMU virt machine, virtio-mmio devices are assigned SPIs starting
 * at IRQ 48. Each device slot (0x200 bytes apart) gets the next IRQ.
 * Base address 0x0a000000 -> IRQ 48, 0x0a000200 -> IRQ 49, etc.
 *
 * @param base MMIO base address of the device.
 * @return IRQ number for the device.
 */
static u32 calculate_virtio_irq(u64 base)
{
    // QEMU virt: virtio-mmio at 0x0a000000 uses IRQ 48 (SPI 16)
    // Each device is 0x200 apart, each gets the next IRQ
    constexpr u64 VIRTIO_BASE = 0x0a000000;
    constexpr u32 VIRTIO_IRQ_BASE = 48; // SPI 16 = 32 + 16 = 48
    return VIRTIO_IRQ_BASE + static_cast<u32>((base - VIRTIO_BASE) / 0x200);
}

/**
 * @brief Global IRQ handler for the virtio-net device.
 *
 * @details
 * This function is registered with the GIC and called when the virtio-net
 * device triggers an interrupt. It delegates to the device's rx_irq_handler.
 */
static void net_irq_handler(u32)
{
    if (g_net_initialized)
    {
        g_net_device.rx_irq_handler();
    }
}

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
    serial::puts(" (");
    serial::puts(is_legacy() ? "legacy" : "modern");
    serial::puts(" mode)\n");

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

    // Calculate and store IRQ number
    irq_ = calculate_virtio_irq(base);
    serial::puts("[virtio-net] Using IRQ ");
    serial::put_dec(irq_);
    serial::puts("\n");

    // Initialize RX waiters
    for (usize i = 0; i < MAX_RX_WAITERS; i++)
    {
        rx_waiters_[i] = nullptr;
    }
    rx_waiter_count_ = 0;

    // Negotiate features
    // For modern virtio, we must negotiate VERSION_1
    // Also try to negotiate checksum offload features
    u64 required = 0;
    if (!is_legacy())
    {
        required |= features::VERSION_1;
    }

    // Check what features the device offers
    write32(reg::DEVICE_FEATURES_SEL, 0);
    u64 device_features = read32(reg::DEVICE_FEATURES);

    // Try to negotiate checksum offload
    u64 desired = required;
    if (device_features & net_features::CSUM)
    {
        desired |= net_features::CSUM;
    }
    if (device_features & net_features::GUEST_CSUM)
    {
        desired |= net_features::GUEST_CSUM;
    }

    if (!negotiate_features(desired))
    {
        // Try without optional checksum features
        if (!negotiate_features(required))
        {
            serial::puts("[virtio-net] Feature negotiation failed\n");
            set_status(status::FAILED);
            return false;
        }
    }
    else
    {
        // Check which checksum features were successfully negotiated
        write32(reg::DRIVER_FEATURES_SEL, 0);
        u64 negotiated = read32(reg::DRIVER_FEATURES);
        has_tx_csum_ = (negotiated & net_features::CSUM) != 0;
        has_rx_csum_ = (negotiated & net_features::GUEST_CSUM) != 0;

        if (has_tx_csum_)
        {
            serial::puts("[virtio-net] TX checksum offload enabled\n");
        }
        if (has_rx_csum_)
        {
            serial::puts("[virtio-net] RX checksum validation enabled\n");
        }
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

    // Queue RX buffers (submit to available ring, but don't kick yet)
    for (usize i = 0; i < RX_BUFFER_COUNT; i++)
    {
        rx_buffers_[i].in_use = false;
        queue_rx_buffer(i);
    }

    // Device is ready - MUST be set before kicking queue
    add_status(status::DRIVER_OK);

    // Now kick RX queue to notify device we're ready to receive
    rx_vq_.kick();

    // Register IRQ handler with GIC
    gic::register_handler(irq_, net_irq_handler);
    gic::set_priority(irq_, 0x80); // Medium priority
    gic::enable_irq(irq_);

    serial::puts("[virtio-net] Driver initialized with interrupt support\n");
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

    bool got_packet = false;

    while (true)
    {
        i32 desc = rx_vq_.poll_used();
        if (desc < 0)
        {
            break;
        }

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

        // Skip virtio-net header (size depends on legacy vs modern mode)
        usize hdr_size = header_size();
        if (len <= hdr_size)
        {
            continue;
        }

        u8 *data = rx_buffers_[buf_idx].data + hdr_size;
        u16 pkt_len = len - hdr_size;

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
    tx_header_->num_buffers = 0; // Required for VERSION_1

    // Memory barrier
    asm volatile("dsb sy" ::: "memory");

    // Get physical address of data
    // Assuming data is in kernel identity-mapped region
    u64 data_phys = pmm::virt_to_phys(const_cast<void *>(data));

    // Set up descriptor chain
    // Header descriptor (device reads) - size depends on legacy vs modern mode
    tx_vq_.set_desc(hdr_desc, tx_header_phys_, header_size(), desc_flags::NEXT);
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

/** @copydoc virtio::NetDevice::transmit_csum */
bool NetDevice::transmit_csum(const void *data, usize len, u16 csum_start, u16 csum_offset)
{
    if (!data || len == 0 || len > 1514)
    { // Max Ethernet frame
        return false;
    }

    // Validate checksum offsets
    if (csum_start >= len || csum_start + csum_offset + 2 > len)
    {
        serial::puts("[virtio-net] Invalid checksum offsets\n");
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

    // Common header fields
    tx_header_->gso_type = net_gso::NONE;
    tx_header_->hdr_len = 0;
    tx_header_->gso_size = 0;
    tx_header_->num_buffers = 0; // Required for VERSION_1

    // Prepare virtio-net header with checksum offload request
    if (has_tx_csum_)
    {
        // Hardware checksum offload available
        tx_header_->flags = net_hdr_flags::NEEDS_CSUM;
        tx_header_->csum_start = csum_start;
        tx_header_->csum_offset = csum_offset;
    }
    else
    {
        // No hardware offload - need software checksum
        // Calculate checksum in software before transmit
        tx_header_->flags = 0;
        tx_header_->csum_start = 0;
        tx_header_->csum_offset = 0;

        // Calculate and insert checksum
        const u8 *pkt = reinterpret_cast<const u8 *>(data);
        u32 sum = 0;

        // Sum 16-bit words from csum_start to end of packet
        for (usize i = csum_start; i + 1 < len; i += 2)
        {
            sum += (static_cast<u32>(pkt[i]) << 8) | pkt[i + 1];
        }
        // Handle odd byte
        if ((len - csum_start) & 1)
        {
            sum += static_cast<u32>(pkt[len - 1]) << 8;
        }

        // Fold 32-bit sum to 16 bits
        while (sum >> 16)
        {
            sum = (sum & 0xFFFF) + (sum >> 16);
        }

        // One's complement
        u16 csum = ~static_cast<u16>(sum);

        // Write checksum into packet (need mutable access)
        u8 *pkt_mut = const_cast<u8 *>(pkt);
        pkt_mut[csum_start + csum_offset] = static_cast<u8>(csum >> 8);
        pkt_mut[csum_start + csum_offset + 1] = static_cast<u8>(csum & 0xFF);
    }

    tx_header_->gso_type = net_gso::NONE;
    tx_header_->hdr_len = 0;
    tx_header_->gso_size = 0;

    // Memory barrier
    asm volatile("dsb sy" ::: "memory");

    // Get physical address of data
    u64 data_phys = pmm::virt_to_phys(const_cast<void *>(data));

    // Set up descriptor chain - header size depends on legacy vs modern mode
    tx_vq_.set_desc(hdr_desc, tx_header_phys_, header_size(), desc_flags::NEXT);
    tx_vq_.chain_desc(hdr_desc, data_desc);
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

/** @copydoc virtio::NetDevice::rx_irq_handler */
void NetDevice::rx_irq_handler()
{
    // Acknowledge the virtio interrupt
    u32 isr = read_isr();
    serial::puts("[virtio-net] IRQ: isr=");
    serial::put_hex(isr);
    serial::puts(" waiters=");
    serial::put_dec(rx_waiter_count_);
    serial::puts("\n");

    if (isr)
    {
        ack_interrupt(isr);
    }

    // Check if this is a queue interrupt (bit 0)
    if (!(isr & 0x1))
    {
        return; // Not a used buffer interrupt
    }

    // Process received packets
    poll_rx();

    // Always wake waiting tasks after processing packets
    // Data may have been added to TCP socket buffers even if raw queue is empty
    wake_rx_waiters();
}

/** @copydoc virtio::NetDevice::register_rx_waiter */
bool NetDevice::register_rx_waiter(task::Task *t)
{
    if (!t)
        return false;

    // Check if already registered
    for (usize i = 0; i < rx_waiter_count_; i++)
    {
        if (rx_waiters_[i] == t)
        {
            return true; // Already registered
        }
    }

    // Add to wait list
    if (rx_waiter_count_ < MAX_RX_WAITERS)
    {
        rx_waiters_[rx_waiter_count_++] = t;
        return true;
    }

    return false; // Wait queue full
}

/** @copydoc virtio::NetDevice::unregister_rx_waiter */
void NetDevice::unregister_rx_waiter(task::Task *t)
{
    if (!t)
        return;

    for (usize i = 0; i < rx_waiter_count_; i++)
    {
        if (rx_waiters_[i] == t)
        {
            // Remove by shifting remaining entries
            for (usize j = i; j + 1 < rx_waiter_count_; j++)
            {
                rx_waiters_[j] = rx_waiters_[j + 1];
            }
            rx_waiter_count_--;
            rx_waiters_[rx_waiter_count_] = nullptr;
            return;
        }
    }
}

/** @copydoc virtio::NetDevice::has_rx_data */
bool NetDevice::has_rx_data() const
{
    return rx_queue_head_ != rx_queue_tail_;
}

/**
 * @brief Wake all tasks waiting for RX data.
 *
 * @details
 * Called from the IRQ handler when data arrives. Moves all waiting
 * tasks from Blocked to Ready state and enqueues them for scheduling.
 */
void NetDevice::wake_rx_waiters()
{
    for (usize i = 0; i < rx_waiter_count_; i++)
    {
        task::Task *t = rx_waiters_[i];
        if (t && t->state == task::TaskState::Blocked)
        {
            t->state = task::TaskState::Ready;
            scheduler::enqueue(t);
        }
        rx_waiters_[i] = nullptr;
    }
    rx_waiter_count_ = 0;
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
