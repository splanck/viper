//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
/**
 * @file net.cpp
 * @brief Kernel VirtIO network device driver implementation.
 *
 * @details
 * Implements the VirtIO network device driver for kernel-space networking.
 * The driver manages two virtqueues:
 * - RX queue (0): Receives incoming Ethernet frames
 * - TX queue (1): Transmits outgoing Ethernet frames
 *
 * Packets are processed using the kernel network stack (netstack.cpp) which
 * handles ARP, IP, TCP, UDP, ICMP, and DNS protocols.
 *
 * The driver uses interrupt-driven I/O with the GIC for efficient packet
 * processing without polling.
 */

#include "net.hpp"
#include "../../arch/aarch64/gic.hpp"
#include "../../console/serial.hpp"
#include "../../include/constants.hpp"
#include "../../mm/pmm.hpp"

namespace kc = kernel::constants;

namespace virtio {

// Global network device instance
static NetDevice g_net_device;
static bool g_net_initialized = false;

// Use centralized VirtIO IRQ base
constexpr u32 VIRTIO_IRQ_BASE = kernel::constants::hw::VIRTIO_IRQ_BASE;

/**
 * @brief IRQ handler for virtio-net interrupts.
 */
static void net_irq_handler(u32) {
    if (g_net_initialized) {
        g_net_device.handle_interrupt();
    }
}

NetDevice *net_device() {
    return g_net_initialized ? &g_net_device : nullptr;
}

bool net_is_available() {
    return g_net_initialized;
}

void net_init() {
    if (g_net_initialized)
        return;

    if (g_net_device.init()) {
        g_net_initialized = true;
        serial::puts("[virtio-net] Network device initialized\n");
    } else {
        serial::puts("[virtio-net] No network device found\n");
    }
}

// =============================================================================
// NetDevice Initialization Helpers
// =============================================================================

bool NetDevice::init_mac_address(bool has_mac) {
    if (has_mac) {
        for (int i = 0; i < 6; i++)
            mac_[i] = read_config8(i);
    } else {
        mac_[0] = 0x52;
        mac_[1] = 0x54;
        mac_[2] = 0x00;
        mac_[3] = 0x12;
        mac_[4] = 0x34;
        mac_[5] = 0x56;
    }

    serial::puts("[virtio-net] MAC: ");
    serial::put_mac(mac_);
    serial::putc('\n');
    return true;
}

bool NetDevice::init_virtqueues() {
    if (!rx_vq_.init(this, 0, kc::virtio::NET_VIRTQUEUE_SIZE)) {
        set_status(status::FAILED);
        return false;
    }
    if (!tx_vq_.init(this, 1, kc::virtio::NET_VIRTQUEUE_SIZE)) {
        set_status(status::FAILED);
        return false;
    }
    return true;
}

bool NetDevice::init_rx_buffers() {
    usize rx_pool_size = RX_BUFFER_COUNT * sizeof(RxBuffer);
    rx_buffers_phys_ = pmm::alloc_pages((rx_pool_size + 4095) / 4096);
    if (!rx_buffers_phys_) {
        set_status(status::FAILED);
        return false;
    }
    rx_buffers_ = reinterpret_cast<RxBuffer *>(pmm::phys_to_virt(rx_buffers_phys_));

    for (usize i = 0; i < RX_BUFFER_COUNT; i++) {
        rx_buffers_[i].in_use = false;
        rx_buffers_[i].desc_idx = 0;
        for (usize j = 0; j < RX_BUFFER_SIZE; j++)
            rx_buffers_[i].data[j] = 0;
    }

    for (usize i = 0; i < RX_QUEUE_SIZE; i++) {
        rx_queue_[i].data = nullptr;
        rx_queue_[i].len = 0;
        rx_queue_[i].valid = false;
    }

    return true;
}

bool NetDevice::init_tx_buffers() {
    tx_header_phys_ = pmm::alloc_pages(1);
    if (!tx_header_phys_) {
        set_status(status::FAILED);
        return false;
    }
    tx_header_ = reinterpret_cast<NetHeader *>(pmm::phys_to_virt(tx_header_phys_));

    tx_data_phys_ = pmm::alloc_pages(1);
    if (!tx_data_phys_) {
        set_status(status::FAILED);
        return false;
    }
    tx_data_buf_ = reinterpret_cast<u8 *>(pmm::phys_to_virt(tx_data_phys_));

    return true;
}

// =============================================================================
// NetDevice Main Initialization
// =============================================================================

bool NetDevice::init() {
    u64 base = find_device(device_type::NET);
    if (!base)
        return false;

    if (!basic_init(base)) {
        serial::puts("[virtio-net] Device init failed\n");
        return false;
    }

    device_index_ =
        static_cast<u32>((base - kc::hw::VIRTIO_MMIO_BASE) / kc::hw::VIRTIO_DEVICE_STRIDE);
    irq_num_ = VIRTIO_IRQ_BASE + device_index_;

    serial::puts("[virtio-net] Initializing at ");
    serial::put_hex(base);
    serial::puts(" (IRQ ");
    serial::put_dec(irq_num_);
    serial::puts(")\n");

    // Check for MAC feature
    write32(reg::DEVICE_FEATURES_SEL, 0);
    u32 features_low = read32(reg::DEVICE_FEATURES);
    bool has_mac = (features_low & (1 << 5)) != 0;

    if (!negotiate_features(0)) {
        set_status(status::FAILED);
        return false;
    }

    if (!init_mac_address(has_mac))
        return false;
    if (!init_virtqueues())
        return false;
    if (!init_rx_buffers())
        return false;
    if (!init_tx_buffers())
        return false;

    refill_rx_buffers();
    add_status(status::DRIVER_OK);

    gic::register_handler(irq_num_, net_irq_handler);
    gic::enable_irq(irq_num_);

    return true;
}

void NetDevice::destroy() {
    if (irq_num_ != 0) {
        gic::disable_irq(irq_num_);
    }

    rx_vq_.destroy();
    tx_vq_.destroy();

    // Note: We don't free pmm pages as the kernel doesn't track them dynamically
}

void NetDevice::get_mac(u8 *mac_out) const {
    for (int i = 0; i < 6; i++) {
        mac_out[i] = mac_[i];
    }
}

void NetDevice::queue_rx_buffer(usize idx) {
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

void NetDevice::refill_rx_buffers() {
    for (usize i = 0; i < RX_BUFFER_COUNT; i++) {
        if (!rx_buffers_[i].in_use) {
            queue_rx_buffer(i);
        }
    }
    rx_vq_.kick();
}

bool NetDevice::transmit(const void *data, usize len) {
    if (len > 1514) { // Max Ethernet frame size
        return false;
    }

    // Copy frame data to TX buffer
    const u8 *src = static_cast<const u8 *>(data);
    for (usize i = 0; i < len; i++) {
        tx_data_buf_[i] = src[i];
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

    if (desc_hdr < 0 || desc_data < 0) {
        if (desc_hdr >= 0)
            tx_vq_.free_desc(desc_hdr);
        if (desc_data >= 0)
            tx_vq_.free_desc(desc_data);
        return false;
    }

    // Set up header descriptor
    tx_vq_.set_desc(desc_hdr, tx_header_phys_, sizeof(NetHeader), desc_flags::NEXT);
    tx_vq_.chain_desc(desc_hdr, desc_data);

    // Set up data descriptor
    tx_vq_.set_desc(desc_data, tx_data_phys_, static_cast<u32>(len), 0);

    // Submit the chain
    tx_vq_.submit(desc_hdr);
    tx_vq_.kick();

    // Poll for completion
    for (u32 i = 0; i < kc::virtio::NET_INIT_POLL_ITERATIONS; i++) {
        if (tx_vq_.poll_used() >= 0) {
            break;
        }
        asm volatile("yield");
    }

    // Free descriptors
    tx_vq_.free_desc(desc_hdr);
    tx_vq_.free_desc(desc_data);

    // Update stats
    tx_packets_++;
    tx_bytes_ += len;

    return true;
}

void NetDevice::poll_rx() {
    while (true) {
        i32 desc = rx_vq_.poll_used();
        if (desc < 0)
            break;

        // Get length from used ring
        u32 len = rx_vq_.get_used_len(desc);

        // Find which buffer this descriptor belongs to
        for (usize i = 0; i < RX_BUFFER_COUNT; i++) {
            if (rx_buffers_[i].in_use && rx_buffers_[i].desc_idx == static_cast<u16>(desc)) {
                // Skip virtio header
                if (len > sizeof(NetHeader)) {
                    u16 frame_len = static_cast<u16>(len - sizeof(NetHeader));
                    u8 *frame_data = rx_buffers_[i].data + sizeof(NetHeader);

                    // Add to received queue if space available
                    usize next_tail = (rx_queue_tail_ + 1) % RX_QUEUE_SIZE;
                    if (next_tail != rx_queue_head_) {
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

i32 NetDevice::receive(void *buf, usize max_len) {
    // Check if we have received packets
    if (rx_queue_head_ == rx_queue_tail_ || !rx_queue_[rx_queue_head_].valid) {
        return 0;
    }

    ReceivedPacket *pkt = &rx_queue_[rx_queue_head_];
    u16 copy_len = pkt->len;
    if (copy_len > max_len) {
        copy_len = static_cast<u16>(max_len);
    }

    // Copy data
    u8 *dst = static_cast<u8 *>(buf);
    for (u16 i = 0; i < copy_len; i++) {
        dst[i] = pkt->data[i];
    }

    // Mark as consumed
    pkt->valid = false;
    rx_queue_head_ = (rx_queue_head_ + 1) % RX_QUEUE_SIZE;

    return copy_len;
}

void NetDevice::handle_interrupt() {
    // Acknowledge interrupt
    u32 isr = read_isr();
    if (isr & 0x1) {
        ack_interrupt(0x1);
        // Process received packets
        poll_rx();
    }
    if (isr & 0x2) {
        ack_interrupt(0x2);
    }
}

bool NetDevice::has_rx_data() const {
    return rx_queue_head_ != rx_queue_tail_ && rx_queue_[rx_queue_head_].valid;
}

bool NetDevice::link_up() const {
    // For simplicity, assume link is always up
    return true;
}

} // namespace virtio
