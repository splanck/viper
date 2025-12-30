#pragma once

#include "virtio.hpp"
#include "virtqueue.hpp"

/**
 * @file net.hpp
 * @brief Virtio network device driver (virtio-net).
 *
 * @details
 * Virtio-net provides a paravirtual network interface. Packets are exchanged
 * via two virtqueues:
 * - RX queue: device writes received packets into guest-provided buffers.
 * - TX queue: guest provides packet buffers for the device to transmit.
 *
 * Each packet is preceded by a small virtio-net header. This driver uses a
 * minimal header (no offloads) and supports basic polling-based reception and
 * blocking transmission.
 */
namespace virtio
{

// virtio-net feature bits
/** @brief Virtio-net feature bits (subset). */
namespace net_features
{
constexpr u64 CSUM = 1ULL << 0; // Checksum offload
constexpr u64 GUEST_CSUM = 1ULL << 1;
constexpr u64 MAC = 1ULL << 5; // Device has MAC address
constexpr u64 GSO = 1ULL << 6; // Generic segmentation offload
constexpr u64 GUEST_TSO4 = 1ULL << 7;
constexpr u64 GUEST_TSO6 = 1ULL << 8;
constexpr u64 GUEST_ECN = 1ULL << 9;
constexpr u64 GUEST_UFO = 1ULL << 10;
constexpr u64 HOST_TSO4 = 1ULL << 11;
constexpr u64 HOST_TSO6 = 1ULL << 12;
constexpr u64 HOST_ECN = 1ULL << 13;
constexpr u64 HOST_UFO = 1ULL << 14;
constexpr u64 MRG_RXBUF = 1ULL << 15; // Mergeable RX buffers
constexpr u64 STATUS = 1ULL << 16;    // Device status available
constexpr u64 CTRL_VQ = 1ULL << 17;   // Control virtqueue
constexpr u64 CTRL_RX = 1ULL << 18;
constexpr u64 CTRL_VLAN = 1ULL << 19;
constexpr u64 CTRL_RX_EXTRA = 1ULL << 20;
constexpr u64 GUEST_ANNOUNCE = 1ULL << 21;
constexpr u64 MQ = 1ULL << 22; // Multiple queues
constexpr u64 CTRL_MAC_ADDR = 1ULL << 23;
} // namespace net_features

// virtio-net header (prepended to every packet)
// Without MRG_RXBUF feature, header is 10 bytes
// With MRG_RXBUF feature, header is 12 bytes (adds num_buffers)
/**
 * @brief Virtio-net packet header placed before every frame buffer.
 *
 * @details
 * The driver zeroes this header for basic transmission without offloads.
 */
struct NetHeader
{
    u8 flags;
    u8 gso_type;
    u16 hdr_len;
    u16 gso_size;
    u16 csum_start;
    u16 csum_offset;
    // num_buffers field is NOT present unless MRG_RXBUF is negotiated
} __attribute__((packed));

// Header flags
/** @brief Virtio-net header flags for checksum offload. */
namespace net_hdr_flags
{
constexpr u8 NEEDS_CSUM = 1;     // Device should calculate checksum
constexpr u8 DATA_VALID = 2;    // Checksum is valid (RX)
constexpr u8 RSC_INFO = 4;      // RSC info available
} // namespace net_hdr_flags

// GSO types
/** @brief Virtio-net GSO type values. */
namespace net_gso
{
constexpr u8 NONE = 0;
constexpr u8 TCPV4 = 1;
constexpr u8 UDP = 3;
constexpr u8 TCPV6 = 4;
constexpr u8 ECN = 0x80;
} // namespace net_gso

// virtio-net config space layout
/**
 * @brief Virtio-net configuration structure (partial).
 *
 * @details
 * Contains MAC address and optional link status/MTU fields.
 */
struct NetConfig
{
    u8 mac[6];
    u16 status;
    u16 max_virtqueue_pairs;
    u16 mtu;
} __attribute__((packed));

// Network status bits
/** @brief Link status bits in the config space. */
namespace net_status
{
constexpr u16 LINK_UP = 1;
constexpr u16 ANNOUNCE = 2;
} // namespace net_status

} // namespace virtio

// Forward declaration for task wait list (must be at global scope)
namespace task
{
struct Task;
}

namespace virtio
{

// Network device driver
/**
 * @brief Virtio network device driver instance.
 *
 * @details
 * The driver maintains:
 * - RX/TX virtqueues.
 * - A pool of receive buffers posted to the RX queue.
 * - A small ring of "received packet" pointers into RX buffers for consumers.
 * - A single header buffer used for TX header.
 * - IRQ-based receive notification with wait queue.
 *
 * Reception is interrupt-driven: the device triggers an IRQ when packets
 * arrive, and waiting tasks are woken via @ref rx_waiters_.
 */
class NetDevice : public Device
{
  public:
    // Maximum waiters for RX
    static constexpr usize MAX_RX_WAITERS = 8;

    // Initialize the network device
    /**
     * @brief Initialize the virtio-net device.
     *
     * @details
     * Finds a NET device, resets it, negotiates required features (VERSION_1 for
     * modern), sets up RX/TX virtqueues, allocates buffer pools, posts RX
     * buffers, and marks DRIVER_OK.
     *
     * @return `true` on success, otherwise `false`.
     */
    bool init();

    // Get MAC address
    /**
     * @brief Copy the device MAC address into `mac_out`.
     *
     * @param mac_out Output buffer of at least 6 bytes.
     */
    void get_mac(u8 *mac_out) const;

    // Transmit a packet (blocking)
    // Returns true on success
    /**
     * @brief Transmit an Ethernet frame.
     *
     * @details
     * Builds a two-descriptor chain (virtio header + packet data), submits it to
     * the TX queue, kicks the device, and polls for completion.
     *
     * @param data Pointer to Ethernet frame bytes.
     * @param len Length of frame in bytes.
     * @return `true` on success, otherwise `false`.
     */
    bool transmit(const void *data, usize len);

    /**
     * @brief Transmit an Ethernet frame with checksum offload.
     *
     * @details
     * If checksum offload was negotiated, the device will calculate the checksum.
     * Otherwise falls back to software checksum calculation before transmit.
     *
     * @param data Pointer to Ethernet frame bytes.
     * @param len Length of frame in bytes.
     * @param csum_start Offset from start of packet where checksumming begins.
     * @param csum_offset Offset from csum_start where the checksum should be stored.
     * @return `true` on success, otherwise `false`.
     */
    bool transmit_csum(const void *data, usize len, u16 csum_start, u16 csum_offset);

    // Receive a packet (non-blocking)
    // Returns bytes received, 0 if no packet, negative on error
    /**
     * @brief Receive the next queued Ethernet frame.
     *
     * @details
     * Polls RX queue for completed buffers, enqueues frames into an internal
     * ring, and then copies the next available frame into `buf`.
     *
     * @param buf Destination buffer.
     * @param max_len Maximum bytes to copy.
     * @return Bytes copied, 0 if none available.
     */
    i32 receive(void *buf, usize max_len);

    // Poll for received packets
    /**
     * @brief Poll the RX virtqueue and enqueue any newly received packets.
     *
     * @details
     * Pulls completed RX buffers from the used ring, strips the virtio header,
     * and places pointers into the internal received queue.
     */
    void poll_rx();

    // Check if link is up
    /** @brief Return whether the link is considered up. */
    bool link_up() const;

    // IRQ-based reception

    /**
     * @brief Handle RX interrupt from the device.
     *
     * @details
     * Called from the GIC IRQ handler when the virtio-net device signals
     * an interrupt. Processes received packets and wakes any waiting tasks.
     */
    void rx_irq_handler();

    /**
     * @brief Register a task to wait for RX data.
     *
     * @details
     * Adds the specified task to the RX wait queue. When data arrives
     * (via interrupt), waiting tasks will be woken.
     *
     * @param t Task to register as waiting.
     * @return true if successfully registered, false if wait queue is full.
     */
    bool register_rx_waiter(task::Task *t);

    /**
     * @brief Remove a task from the RX wait queue.
     *
     * @param t Task to remove.
     */
    void unregister_rx_waiter(task::Task *t);

    /**
     * @brief Check if there are received packets available.
     *
     * @return true if data is ready to be read.
     */
    bool has_rx_data() const;

    /**
     * @brief Get the IRQ number for this device.
     *
     * @return IRQ number assigned to this device.
     */
    u32 irq() const
    {
        return irq_;
    }

    // Statistics
    u64 tx_packets() const
    {
        return tx_packets_;
    }

    u64 rx_packets() const
    {
        return rx_packets_;
    }

    u64 tx_bytes() const
    {
        return tx_bytes_;
    }

    u64 rx_bytes() const
    {
        return rx_bytes_;
    }

    u64 tx_dropped() const
    {
        return tx_dropped_;
    }

    u64 rx_dropped() const
    {
        return rx_dropped_;
    }

    /**
     * @brief Check if TX checksum offload is available.
     *
     * @return true if the device supports TX checksum offload.
     */
    bool has_tx_csum() const
    {
        return has_tx_csum_;
    }

    /**
     * @brief Check if RX checksum validation is available.
     *
     * @return true if the device validates checksums on received packets.
     */
    bool has_rx_csum() const
    {
        return has_rx_csum_;
    }

  private:
    Virtqueue rx_vq_; // Queue 0: receive
    Virtqueue tx_vq_; // Queue 1: transmit

    // MAC address
    u8 mac_[6];

    // RX buffer pool
    static constexpr usize RX_BUFFER_COUNT = 32;
    static constexpr usize RX_BUFFER_SIZE = 2048; // Including header

    struct RxBuffer
    {
        u8 data[RX_BUFFER_SIZE];
        bool in_use;
        u16 desc_idx;
    };

    RxBuffer *rx_buffers_{nullptr};
    u64 rx_buffers_phys_{0};

    // TX buffer for header
    NetHeader *tx_header_{nullptr};
    u64 tx_header_phys_{0};

    // Received packet queue (simple ring buffer)
    static constexpr usize RX_QUEUE_SIZE = 16;

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
    u64 tx_dropped_{0};
    u64 rx_dropped_{0};

    // IRQ number for this device
    u32 irq_{0};

    // Checksum offload capabilities
    bool has_tx_csum_{false};  // CSUM feature negotiated
    bool has_rx_csum_{false};  // GUEST_CSUM feature negotiated

    // RX waiters (tasks waiting for data)
    task::Task *rx_waiters_[MAX_RX_WAITERS];
    usize rx_waiter_count_{0};

    // Internal methods
    /** @brief Submit an RX buffer to the device. */
    void queue_rx_buffer(usize idx);
    /** @brief Refill RX queue with any buffers that are no longer in use. */
    void refill_rx_buffers();
    /** @brief Wake all tasks waiting for RX data. */
    void wake_rx_waiters();
};

// Global network device initialization and access
/** @brief Initialize the global virtio-net device instance. */
void net_init();
/** @brief Get the global virtio-net device instance, or nullptr if unavailable. */
NetDevice *net_device();

} // namespace virtio
