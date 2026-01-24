/**
 * @file net.hpp
 * @brief Kernel VirtIO network device driver.
 *
 * @details
 * Provides a kernel-space VirtIO-net driver for the monolithic kernel.
 * This driver handles Ethernet frame transmission and reception.
 */
#pragma once

#include "virtio.hpp"
#include "virtqueue.hpp"
#include "../../include/constants.hpp"

namespace kc = kernel::constants;

namespace virtio
{

// virtio-net feature bits
namespace net_features
{
constexpr u64 CSUM = 1ULL << 0;       // Checksum offload
constexpr u64 GUEST_CSUM = 1ULL << 1; // Guest handles checksum
constexpr u64 MAC = 1ULL << 5;        // Device has MAC address
constexpr u64 GSO = 1ULL << 6;        // Generic segmentation offload
constexpr u64 MRG_RXBUF = 1ULL << 15; // Mergeable RX buffers
constexpr u64 STATUS = 1ULL << 16;    // Device status available
constexpr u64 CTRL_VQ = 1ULL << 17;   // Control virtqueue
constexpr u64 MQ = 1ULL << 22;        // Multiple queues
} // namespace net_features

// virtio-net header (prepended to every packet)
struct NetHeader
{
    u8 flags;
    u8 gso_type;
    u16 hdr_len;
    u16 gso_size;
    u16 csum_start;
    u16 csum_offset;
} __attribute__((packed));

// Header flags
namespace net_hdr_flags
{
constexpr u8 NEEDS_CSUM = 1;
constexpr u8 DATA_VALID = 2;
} // namespace net_hdr_flags

// GSO types
namespace net_gso
{
constexpr u8 NONE = 0;
constexpr u8 TCPV4 = 1;
constexpr u8 UDP = 3;
constexpr u8 TCPV6 = 4;
} // namespace net_gso

// virtio-net config space layout
struct NetConfig
{
    u8 mac[6];
    u16 status;
    u16 max_virtqueue_pairs;
    u16 mtu;
} __attribute__((packed));

// Network status bits
namespace net_status
{
constexpr u16 LINK_UP = 1;
constexpr u16 ANNOUNCE = 2;
} // namespace net_status

/**
 * @brief Kernel VirtIO network device driver.
 */
class NetDevice : public Device
{
  public:
    /**
     * @brief Initialize the network device.
     * @return true on success.
     */
    bool init();

    /**
     * @brief Clean up resources.
     */
    void destroy();

    /**
     * @brief Get the device MAC address.
     * @param mac_out Buffer to receive MAC (6 bytes).
     */
    void get_mac(u8 *mac_out) const;

    /**
     * @brief Transmit an Ethernet frame.
     * @param data Frame data.
     * @param len Frame length.
     * @return true on success.
     */
    bool transmit(const void *data, usize len);

    /**
     * @brief Receive an Ethernet frame (non-blocking).
     * @param buf Buffer to receive frame.
     * @param max_len Maximum bytes to receive.
     * @return Bytes received, 0 if none available, negative on error.
     */
    i32 receive(void *buf, usize max_len);

    /**
     * @brief Poll for received packets.
     * Call periodically or after IRQ to process received packets.
     */
    void poll_rx();

    /**
     * @brief Handle device interrupt.
     */
    void handle_interrupt();

    /**
     * @brief Check if packets are available.
     */
    bool has_rx_data() const;

    /**
     * @brief Check if link is up.
     */
    bool link_up() const;

    // Statistics
    u64 tx_packets() const { return tx_packets_; }
    u64 rx_packets() const { return rx_packets_; }
    u64 tx_bytes() const { return tx_bytes_; }
    u64 rx_bytes() const { return rx_bytes_; }

  private:
    Virtqueue rx_vq_;
    Virtqueue tx_vq_;

    // MAC address
    u8 mac_[6];

    // RX buffer pool
    static constexpr usize RX_BUFFER_COUNT = kc::virtio::NET_RX_BUFFER_COUNT;
    static constexpr usize RX_BUFFER_SIZE = kc::virtio::NET_RX_BUFFER_SIZE;

    struct RxBuffer
    {
        u8 data[RX_BUFFER_SIZE];
        bool in_use;
        u16 desc_idx;
    };

    RxBuffer *rx_buffers_{nullptr};
    u64 rx_buffers_phys_{0};

    // TX header buffer
    NetHeader *tx_header_{nullptr};
    u64 tx_header_phys_{0};

    // TX data buffer (for frame data)
    u8 *tx_data_buf_{nullptr};
    u64 tx_data_phys_{0};

    // Received packet queue
    static constexpr usize RX_QUEUE_SIZE = kc::virtio::NET_RX_QUEUE_SIZE;

    struct ReceivedPacket
    {
        u8 *data;
        u16 len;
        bool valid;
    };

    ReceivedPacket rx_queue_[RX_QUEUE_SIZE];
    usize rx_queue_head_{0};
    usize rx_queue_tail_{0};

    // Statistics
    u64 tx_packets_{0};
    u64 rx_packets_{0};
    u64 tx_bytes_{0};
    u64 rx_bytes_{0};

    // IRQ number
    u32 irq_num_{0};
    u32 device_index_{0};

    // Internal methods
    void queue_rx_buffer(usize idx);
    void refill_rx_buffers();
    bool init_mac_address(bool has_mac);
    bool init_virtqueues();
    bool init_rx_buffers();
    bool init_tx_buffers();
};

// Global network device initialization and access
void net_init();
NetDevice *net_device();
bool net_is_available();

} // namespace virtio
