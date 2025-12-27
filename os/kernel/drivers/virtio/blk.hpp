#pragma once

#include "virtio.hpp"
#include "virtqueue.hpp"

/**
 * @file blk.hpp
 * @brief Virtio block device driver (virtio-blk).
 *
 * @details
 * The virtio-blk device provides a simple block storage interface backed by a
 * host disk image in QEMU. The driver builds block requests and submits them
 * to the device via a single virtqueue.
 *
 * This header defines:
 * - Request header/status formats used by virtio-blk.
 * - Basic device configuration structure.
 * - `virtio::BlkDevice`, a driver that supports blocking reads/writes and a
 *   flush operation.
 *
 * The driver uses interrupt-driven I/O with polling fallback for robustness.
 */
namespace virtio
{

// Block request types
/**
 * @brief Virtio-blk request type values.
 */
namespace blk_type
{
constexpr u32 IN = 0;    // Read from device
constexpr u32 OUT = 1;   // Write to device
constexpr u32 FLUSH = 4; // Flush buffers
} // namespace blk_type

// Block request status
/**
 * @brief Completion status values written by the device.
 */
namespace blk_status
{
constexpr u8 OK = 0;
constexpr u8 IOERR = 1;
constexpr u8 UNSUPP = 2;
} // namespace blk_status

// Block feature bits
/**
 * @brief Virtio-blk feature bits.
 *
 * @details
 * The driver currently does not rely on most optional features; it primarily
 * checks for read-only capability.
 */
namespace blk_features
{
constexpr u64 SIZE_MAX = 1 << 1;
constexpr u64 SEG_MAX = 1 << 2;
constexpr u64 GEOMETRY = 1 << 4;
constexpr u64 RO = 1 << 5; // Read-only
constexpr u64 BLK_SIZE = 1 << 6;
constexpr u64 FLUSH = 1 << 9;
constexpr u64 TOPOLOGY = 1 << 10;
constexpr u64 CONFIG_WCE = 1 << 11;
constexpr u64 MQ = 1 << 12;
constexpr u64 DISCARD = 1 << 13;
constexpr u64 WRITE_ZEROES = 1 << 14;
} // namespace blk_features

// Block device configuration (at config offset 0x100)
/**
 * @brief Virtio-blk configuration space layout (partial).
 *
 * @details
 * The config space contains capacity and optional properties like block size.
 * Only a subset is represented here.
 */
struct BlkConfig
{
    u64 capacity; // Number of 512-byte sectors
    u32 size_max; // Max size of single segment
    u32 seg_max;  // Max number of segments

    struct
    {
        u16 cylinders;
        u8 heads;
        u8 sectors;
    } geometry;

    u32 blk_size; // Block size (usually 512)
    // ... more fields for topology, etc.
};

// Block request header (sent to device)
/**
 * @brief Virtio-blk request header placed at the start of a request chain.
 */
struct BlkReqHeader
{
    u32 type; // blk_type::IN or blk_type::OUT
    u32 reserved;
    u64 sector; // Starting sector
};

// Block device driver
/**
 * @brief Virtio block device driver.
 *
 * @details
 * The driver uses:
 * - Queue 0 for request submission and completion.
 * - A small fixed array of pending request headers/status bytes stored in a
 *   DMA-accessible buffer.
 *
 * Requests are built as a descriptor chain:
 * 1) Request header (device reads).
 * 2) Data buffer (device reads for write requests, writes for read requests).
 * 3) Status byte (device writes).
 */
class BlkDevice : public Device
{
  public:
    // Initialize the block device
    /**
     * @brief Initialize and configure the virtio-blk device.
     *
     * @details
     * Locates a virtio-blk MMIO device, resets it, negotiates features, sets up
     * the request virtqueue, allocates request bookkeeping memory, and marks the
     * device DRIVER_OK.
     *
     * @return `true` on success, otherwise `false`.
     */
    bool init();

    // Read sectors from disk (blocking)
    // Returns 0 on success, negative on error
    /**
     * @brief Read one or more sectors into a buffer (blocking).
     *
     * @param sector Starting sector index.
     * @param count Number of sectors to read.
     * @param buf Destination buffer (must be large enough for count*sector_size).
     * @return 0 on success, -1 on error.
     */
    i32 read_sectors(u64 sector, u32 count, void *buf);

    // Write sectors to disk (blocking)
    // Returns 0 on success, negative on error
    /**
     * @brief Write one or more sectors from a buffer (blocking).
     *
     * @param sector Starting sector index.
     * @param count Number of sectors to write.
     * @param buf Source buffer.
     * @return 0 on success, -1 on error.
     */
    i32 write_sectors(u64 sector, u32 count, const void *buf);

    // Flush write cache
    /**
     * @brief Flush the device write cache (if supported).
     *
     * @return 0 on success, -1 on error.
     */
    i32 flush();

    // Device info
    /** @brief Total number of sectors on the device. */
    u64 capacity() const
    {
        return capacity_;
    }

    /** @brief Sector size in bytes (defaults to 512). */
    u32 sector_size() const
    {
        return sector_size_;
    }

    /** @brief Total device size in bytes. */
    u64 size_bytes() const
    {
        return capacity_ * sector_size_;
    }

    /** @brief Whether the device is read-only. */
    bool is_readonly() const
    {
        return readonly_;
    }

    /**
     * @brief Handle block device interrupt.
     *
     * @details
     * Called from the IRQ handler. Acknowledges the interrupt and
     * signals completion to any waiting requests.
     */
    void handle_interrupt();

    /**
     * @brief Get the device index for IRQ calculation.
     *
     * @return Device index in the virtio MMIO range.
     */
    u32 device_index() const
    {
        return device_index_;
    }

  private:
    Virtqueue vq_;
    u64 capacity_{0};
    u32 sector_size_{512};
    bool readonly_{false};
    u32 device_index_{0}; // Index for IRQ calculation

    // Interrupt-driven I/O state
    volatile bool io_complete_{false};    // Set by IRQ handler
    volatile i32 completed_desc_{-1};     // Descriptor completed by IRQ
    u32 irq_num_{0};                      // Assigned IRQ number

    // Pre-allocated request buffer
    static constexpr usize MAX_PENDING = 8;

    struct PendingRequest
    {
        BlkReqHeader header;
        u8 status;
        bool in_use;
    } __attribute__((packed));

    PendingRequest *requests_{nullptr};
    u64 requests_phys_{0};

    // Perform a request
    /**
     * @brief Build and submit a virtio-blk request.
     *
     * @details
     * Builds a descriptor chain consisting of a request header, a data buffer,
     * and a status byte. Submits the chain and polls for completion.
     *
     * @param type Request type (IN/OUT/FLUSH).
     * @param sector Starting sector.
     * @param count Number of sectors.
     * @param buf Data buffer (may be nullptr for FLUSH).
     * @return 0 on success, -1 on error.
     */
    i32 do_request(u32 type, u64 sector, u32 count, void *buf);
};

// Global block device initialization and access
/** @brief Initialize the global virtio-blk device instance. */
void blk_init();
/** @brief Get the global virtio-blk device instance, or nullptr if unavailable. */
BlkDevice *blk_device();

} // namespace virtio
