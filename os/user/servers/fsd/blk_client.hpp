/**
 * @file blk_client.hpp
 * @brief Block device client for communicating with blkd server.
 *
 * @details
 * Provides a simple interface for reading/writing blocks via IPC
 * to the block device server. Uses shared memory for data transfer.
 */
#pragma once

#include "../../syscall.hpp"
#include "../blkd/blk_protocol.hpp"
#include "format.hpp"

namespace viperfs
{

// Simple memory copy
static inline void memcpy_shm(void *dst, const void *src, usize n)
{
    u8 *d = static_cast<u8 *>(dst);
    const u8 *s = static_cast<const u8 *>(src);
    while (n--)
        *d++ = *s++;
}

/**
 * @brief Block device client using IPC.
 *
 * Communicates with blkd to read/write disk blocks via shared memory.
 */
class BlkClient
{
  public:
    BlkClient() : blkd_channel_(-1), next_request_id_(1) {}

    /**
     * @brief Connect to the block device server.
     *
     * @return true if connection successful.
     */
    bool connect()
    {
        // Look up blkd service via assign
        u32 handle;
        if (sys::assign_get("BLKD", &handle) != 0)
        {
            return false;
        }
        blkd_channel_ = static_cast<i32>(handle);
        return true;
    }

    /**
     * @brief Read a block from disk.
     *
     * @param block_num Block number to read.
     * @param buf Buffer to receive data (must be BLOCK_SIZE bytes).
     * @return 0 on success, negative error on failure.
     */
    i32 read_block(u64 block_num, void *buf)
    {
        // Convert block to sectors (BLOCK_SIZE / 512 = 8 sectors per block)
        u64 sector = block_num * (BLOCK_SIZE / 512);
        u32 count = BLOCK_SIZE / 512;

        blk::ReadRequest req;
        req.type = blk::BLK_READ;
        req.request_id = next_request_id_++;
        req.sector = sector;
        req.count = count;
        req._pad = 0;

        // Send request
        i64 err = sys::channel_send(blkd_channel_, &req, sizeof(req), nullptr, 0);
        if (err != 0)
        {
            return static_cast<i32>(err);
        }

        // Wait for reply
        blk::ReadReply reply;
        u32 handles[4];
        u32 handle_count = 4;
        i64 len = sys::channel_recv(blkd_channel_, &reply, sizeof(reply),
                                     handles, &handle_count);
        if (len < 0)
        {
            return static_cast<i32>(len);
        }

        if (reply.status != 0)
        {
            return reply.status;
        }

        // Check if we received a shared memory handle
        if (handle_count >= 1 && handles[0] != 0)
        {
            // Map the shared memory
            auto shm_map = sys::shm_map(handles[0]);
            if (shm_map.error == 0)
            {
                // Copy data from shared memory to user buffer
                memcpy_shm(buf, reinterpret_cast<void *>(shm_map.virt_addr), BLOCK_SIZE);

                // Unmap shared memory
                sys::shm_unmap(shm_map.virt_addr);
            }
        }

        return 0;
    }

    /**
     * @brief Write a block to disk.
     *
     * @param block_num Block number to write.
     * @param buf Buffer with data to write (must be BLOCK_SIZE bytes).
     * @return 0 on success, negative error on failure.
     */
    i32 write_block(u64 block_num, const void *buf)
    {
        // Create shared memory for the write data
        auto shm_result = sys::shm_create(BLOCK_SIZE);
        if (shm_result.error != 0)
        {
            return static_cast<i32>(shm_result.error);
        }

        // Copy data to shared memory
        memcpy_shm(reinterpret_cast<void *>(shm_result.virt_addr), buf, BLOCK_SIZE);

        // Convert block to sectors
        u64 sector = block_num * (BLOCK_SIZE / 512);
        u32 count = BLOCK_SIZE / 512;

        blk::WriteRequest req;
        req.type = blk::BLK_WRITE;
        req.request_id = next_request_id_++;
        req.sector = sector;
        req.count = count;
        req._pad = 0;

        // Send request with shared memory handle
        u32 send_handles[1] = {shm_result.handle};
        i64 err = sys::channel_send(blkd_channel_, &req, sizeof(req), send_handles, 1);
        if (err != 0)
        {
            sys::shm_unmap(shm_result.virt_addr);
            return static_cast<i32>(err);
        }

        // Wait for reply
        blk::WriteReply reply;
        u32 handles[4];
        u32 handle_count = 4;
        i64 len = sys::channel_recv(blkd_channel_, &reply, sizeof(reply),
                                     handles, &handle_count);

        // Cleanup shared memory
        sys::shm_unmap(shm_result.virt_addr);

        if (len < 0)
        {
            return static_cast<i32>(len);
        }

        return reply.status;
    }

    /**
     * @brief Flush pending writes to disk.
     *
     * @return 0 on success, negative error on failure.
     */
    i32 flush()
    {
        blk::FlushRequest req;
        req.type = blk::BLK_FLUSH;
        req.request_id = next_request_id_++;

        i64 err = sys::channel_send(blkd_channel_, &req, sizeof(req), nullptr, 0);
        if (err != 0)
        {
            return static_cast<i32>(err);
        }

        blk::FlushReply reply;
        u32 handles[4];
        u32 handle_count = 4;
        i64 len = sys::channel_recv(blkd_channel_, &reply, sizeof(reply),
                                     handles, &handle_count);
        if (len < 0)
        {
            return static_cast<i32>(len);
        }

        return reply.status;
    }

    /**
     * @brief Get block device info.
     *
     * @param total_sectors Output: total sectors on device.
     * @param sector_size Output: bytes per sector.
     * @return 0 on success, negative error on failure.
     */
    i32 get_info(u64 *total_sectors, u32 *sector_size)
    {
        blk::InfoRequest req;
        req.type = blk::BLK_INFO;
        req.request_id = next_request_id_++;

        i64 err = sys::channel_send(blkd_channel_, &req, sizeof(req), nullptr, 0);
        if (err != 0)
        {
            return static_cast<i32>(err);
        }

        blk::InfoReply reply;
        u32 handles[4];
        u32 handle_count = 4;
        i64 len = sys::channel_recv(blkd_channel_, &reply, sizeof(reply),
                                     handles, &handle_count);
        if (len < 0)
        {
            return static_cast<i32>(len);
        }

        if (reply.status == 0)
        {
            if (total_sectors)
                *total_sectors = reply.total_sectors;
            if (sector_size)
                *sector_size = reply.sector_size;
        }

        return reply.status;
    }

  private:
    i32 blkd_channel_;
    u32 next_request_id_;
};

} // namespace viperfs
