/**
 * @file blk.hpp
 * @brief User-space VirtIO block device driver.
 *
 * @details
 * Provides a user-space VirtIO-blk driver that uses the device access syscalls
 * for MMIO mapping, DMA allocation, and interrupt handling.
 */
#pragma once

#include "virtio.hpp"
#include "virtqueue.hpp"

namespace virtio
{

// Block request types
namespace blk_type
{
constexpr u32 IN = 0;    // Read from device
constexpr u32 OUT = 1;   // Write to device
constexpr u32 FLUSH = 4; // Flush buffers
} // namespace blk_type

// Block request status
namespace blk_status
{
constexpr u8 OK = 0;
constexpr u8 IOERR = 1;
constexpr u8 UNSUPP = 2;
} // namespace blk_status

// Block feature bits
namespace blk_features
{
constexpr u64 SIZE_MAX = 1 << 1;
constexpr u64 SEG_MAX = 1 << 2;
constexpr u64 GEOMETRY = 1 << 4;
constexpr u64 RO = 1 << 5;
constexpr u64 BLK_SIZE = 1 << 6;
constexpr u64 FLUSH = 1 << 9;
constexpr u64 TOPOLOGY = 1 << 10;
constexpr u64 CONFIG_WCE = 1 << 11;
constexpr u64 MQ = 1 << 12;
} // namespace blk_features

// Block request header
struct BlkReqHeader
{
    u32 type;
    u32 reserved;
    u64 sector;
};

/**
 * @brief User-space VirtIO block device driver.
 */
class BlkDevice : public Device
{
  public:
    /**
     * @brief Initialize the block device.
     *
     * @param mmio_phys Physical MMIO address.
     * @param irq IRQ number for the device.
     * @return true on success.
     */
    bool init(u64 mmio_phys, u32 irq);

    /**
     * @brief Clean up resources.
     */
    void destroy();

    /**
     * @brief Read sectors from disk (blocking).
     *
     * @param sector Starting sector index.
     * @param count Number of sectors to read.
     * @param buf Destination buffer.
     * @return 0 on success, negative on error.
     */
    i32 read_sectors(u64 sector, u32 count, void *buf);

    /**
     * @brief Write sectors to disk (blocking).
     *
     * @param sector Starting sector index.
     * @param count Number of sectors to write.
     * @param buf Source buffer.
     * @return 0 on success, negative on error.
     */
    i32 write_sectors(u64 sector, u32 count, const void *buf);

    /**
     * @brief Flush write cache.
     *
     * @return 0 on success, negative on error.
     */
    i32 flush();

    /**
     * @brief Handle device interrupt.
     *
     * Call this when IRQ fires.
     */
    void handle_interrupt();

    // Device info
    u64 capacity() const
    {
        return capacity_;
    }

    u32 sector_size() const
    {
        return sector_size_;
    }

    u64 size_bytes() const
    {
        return capacity_ * sector_size_;
    }

    bool is_readonly() const
    {
        return readonly_;
    }

  private:
    Virtqueue vq_;
    u64 capacity_{0};
    u32 sector_size_{512};
    bool readonly_{false};
    u32 irq_num_{0};

    // Completion state
    volatile bool io_complete_{false};
    volatile i32 completed_desc_{-1};

    // Pre-allocated request buffer (DMA-accessible)
    static constexpr usize MAX_PENDING = 8;

    struct PendingRequest
    {
        BlkReqHeader header;
        u8 status;
        u8 _pad[3];
    } __attribute__((packed));

    struct RequestSlot
    {
        bool in_use{false};
        i32 desc_head{-1};
        i32 desc_data{-1};
        i32 desc_status{-1};
    };

    PendingRequest *requests_{nullptr};
    u64 requests_phys_{0};
    u64 requests_virt_{0};
    RequestSlot slots_[MAX_PENDING]{};

    i32 do_request(u32 type, u64 sector, u32 count, void *buf);
};

} // namespace virtio
