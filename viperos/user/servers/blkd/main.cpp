/**
 * @file main.cpp
 * @brief Block device server (blkd) main entry point.
 *
 * @details
 * This server provides block device access to other user-space processes
 * via IPC. It:
 * - Finds and initializes a VirtIO-blk device
 * - Creates a service channel
 * - Registers with the assign system as "BLKD:"
 * - Handles read/write/flush/info requests
 */

#include "../../libvirtio/include/blk.hpp"
#include "../../libvirtio/include/device.hpp"
#include "../../syscall.hpp"
#include "blk_protocol.hpp"

// Debug output helper
static void debug_print(const char *msg)
{
    sys::print(msg);
}

static void debug_print_hex(u64 val)
{
    char buf[17];
    const char *hex = "0123456789abcdef";
    for (int i = 15; i >= 0; i--)
    {
        buf[i] = hex[val & 0xF];
        val >>= 4;
    }
    buf[16] = '\0';
    sys::print(buf);
}

static void debug_print_dec(u64 val)
{
    if (val == 0)
    {
        sys::print("0");
        return;
    }
    char buf[21];
    int i = 20;
    buf[i] = '\0';
    while (val > 0 && i > 0)
    {
        buf[--i] = '0' + (val % 10);
        val /= 10;
    }
    sys::print(&buf[i]);
}

// Global state
static virtio::BlkDevice g_device;
static i32 g_service_channel = -1;

// QEMU virt machine VirtIO IRQ base
constexpr u32 VIRTIO_IRQ_BASE = 48;

static void recv_bootstrap_caps()
{
    // If this process was spawned by vinit, handle 0 is expected to be a
    // bootstrap channel recv endpoint used for initial capability delegation.
    constexpr i32 BOOTSTRAP_RECV = 0;

    u8 dummy[1];
    u32 handles[4];
    u32 handle_count = 4;

    // Wait briefly for vinit to send initial caps; otherwise fall back to the
    // legacy bring-up policy (kernel-side) until strict mode is enabled.
    for (u32 i = 0; i < 2000; i++)
    {
        handle_count = 4;
        i64 n = sys::channel_recv(BOOTSTRAP_RECV, dummy, sizeof(dummy), handles, &handle_count);
        if (n >= 0)
        {
            sys::channel_close(BOOTSTRAP_RECV);
            return;
        }
        if (n == VERR_WOULD_BLOCK)
        {
            sys::yield();
            continue;
        }

        // Invalid handle or other error => no bootstrap channel.
        return;
    }
}

/**
 * @brief Find VirtIO-blk device in the system.
 *
 * @param mmio_phys Output: physical MMIO address.
 * @param irq Output: IRQ number.
 * @return true if found.
 */
static bool find_blk_device(u64 *mmio_phys, u32 *irq)
{
    // Scan VirtIO MMIO range for block device
    constexpr u64 VIRTIO_BASE = 0x0a000000;
    constexpr u64 VIRTIO_END = 0x0a004000;
    constexpr u64 VIRTIO_STRIDE = 0x200;

    for (u64 addr = VIRTIO_BASE; addr < VIRTIO_END; addr += VIRTIO_STRIDE)
    {
        // Map the device temporarily to check type
        u64 virt = device::map_device(addr, VIRTIO_STRIDE);
        if (virt == 0)
        {
            continue;
        }

        volatile u32 *mmio = reinterpret_cast<volatile u32 *>(virt);

        // Check magic
        u32 magic = mmio[0];     // MAGIC at offset 0
        if (magic != 0x74726976) // "virt"
        {
            continue;
        }

        // Check device type (offset 0x008)
        u32 device_id = mmio[2]; // DEVICE_ID at offset 8
        if (device_id == virtio::device_type::BLK)
        {
            // Check if device is already in use (kernel's block driver)
            u32 status = mmio[virtio::reg::STATUS / 4];
            if (status != 0)
            {
                // Device is in use by kernel - skip it and look for another one.
                // The build system creates two virtio-blk devices: one for kernel,
                // one for blkd. We need to find the unused one.
                debug_print("[blkd] Skipping in-use device at ");
                debug_print_hex(addr);
                debug_print("\n");
                continue;
            }

            *mmio_phys = addr;
            *irq = VIRTIO_IRQ_BASE + static_cast<u32>((addr - VIRTIO_BASE) / VIRTIO_STRIDE);
            return true;
        }
    }

    return false;
}

// Simple memory copy for shared memory data transfer
static void memcpy_bytes(void *dst, const void *src, usize n)
{
    u8 *d = static_cast<u8 *>(dst);
    const u8 *s = static_cast<const u8 *>(src);
    while (n--)
        *d++ = *s++;
}

/**
 * @brief Handle BLK_READ request.
 */
static void handle_read(const blk::ReadRequest *req, i32 reply_channel)
{
    blk::ReadReply reply;
    reply.type = blk::BLK_READ_REPLY;
    reply.request_id = req->request_id;

    // Validate request
    if (req->count == 0 || req->count > blk::MAX_SECTORS_PER_REQUEST)
    {
        reply.status = -1;
        reply.bytes_read = 0;
        sys::channel_send(reply_channel, &reply, sizeof(reply), nullptr, 0);
        return;
    }

    u64 size = req->count * blk::SECTOR_SIZE;

    // Create shared memory for the data transfer
    auto shm_result = sys::shm_create(size);
    if (shm_result.error != 0)
    {
        debug_print("[blkd] Failed to create shared memory\n");
        reply.status = -3; // VERR_OUT_OF_MEMORY
        reply.bytes_read = 0;
        sys::channel_send(reply_channel, &reply, sizeof(reply), nullptr, 0);
        return;
    }

    // Allocate DMA buffer for the read (device requires DMA-capable memory)
    device::DmaBuffer dma_buf;
    if (device::dma_alloc(size, &dma_buf) != 0)
    {
        sys::shm_unmap(shm_result.virt_addr);
        sys::shm_close(shm_result.handle);
        reply.status = -3; // VERR_OUT_OF_MEMORY
        reply.bytes_read = 0;
        sys::channel_send(reply_channel, &reply, sizeof(reply), nullptr, 0);
        return;
    }

    // Perform the read into DMA buffer
    i32 result =
        g_device.read_sectors(req->sector, req->count, reinterpret_cast<void *>(dma_buf.virt_addr));

    if (result == 0)
    {
        // Copy data from DMA buffer to shared memory
        memcpy_bytes(reinterpret_cast<void *>(shm_result.virt_addr),
                     reinterpret_cast<void *>(dma_buf.virt_addr),
                     size);

        reply.status = 0;
        reply.bytes_read = static_cast<u32>(size);

        // Done with local mapping; the handle is transferred to the client.
        sys::shm_unmap(shm_result.virt_addr);

        // Send reply with shared memory handle
        u32 handles[1] = {shm_result.handle};
        i64 send_err = sys::channel_send(reply_channel, &reply, sizeof(reply), handles, 1);
        if (send_err != 0)
        {
            // Transfer did not occur; we still own the handle.
            sys::shm_close(shm_result.handle);
        }
    }
    else
    {
        reply.status = -500; // VERR_IO
        reply.bytes_read = 0;
        sys::shm_unmap(shm_result.virt_addr);
        sys::shm_close(shm_result.handle);
        sys::channel_send(reply_channel, &reply, sizeof(reply), nullptr, 0);
    }

    // Free the DMA buffer (shared memory persists until client closes it)
    device::dma_free(dma_buf.virt_addr);
}

/**
 * @brief Handle BLK_WRITE request.
 *
 * @param req Write request.
 * @param reply_channel Channel to send reply.
 * @param shm_handle Shared memory handle containing data to write (0 if none).
 */
static void handle_write(const blk::WriteRequest *req, i32 reply_channel, u32 shm_handle)
{
    blk::WriteReply reply;
    reply.type = blk::BLK_WRITE_REPLY;
    reply.request_id = req->request_id;

    // Validate request
    if (req->count == 0 || req->count > blk::MAX_SECTORS_PER_REQUEST)
    {
        reply.status = -1;
        reply.bytes_written = 0;
        sys::channel_send(reply_channel, &reply, sizeof(reply), nullptr, 0);
        return;
    }

    // Check for shared memory handle
    if (shm_handle == 0)
    {
        debug_print("[blkd] Write request missing shared memory handle\n");
        reply.status = -1; // VERR_INVALID_ARG
        reply.bytes_written = 0;
        sys::channel_send(reply_channel, &reply, sizeof(reply), nullptr, 0);
        return;
    }

    // Map the shared memory
    auto shm_map = sys::shm_map(shm_handle);
    if (shm_map.error != 0)
    {
        debug_print("[blkd] Failed to map shared memory\n");
        reply.status = -1; // VERR_INVALID_HANDLE
        reply.bytes_written = 0;
        sys::channel_send(reply_channel, &reply, sizeof(reply), nullptr, 0);
        return;
    }

    u64 size = req->count * blk::SECTOR_SIZE;
    if (shm_map.size < size)
    {
        debug_print("[blkd] Shared memory too small for write\n");
        sys::shm_unmap(shm_map.virt_addr);
        reply.status = -1; // VERR_INVALID_ARG
        reply.bytes_written = 0;
        sys::channel_send(reply_channel, &reply, sizeof(reply), nullptr, 0);
        return;
    }

    // Allocate DMA buffer for the write
    device::DmaBuffer dma_buf;
    if (device::dma_alloc(size, &dma_buf) != 0)
    {
        sys::shm_unmap(shm_map.virt_addr);
        reply.status = -3; // VERR_OUT_OF_MEMORY
        reply.bytes_written = 0;
        sys::channel_send(reply_channel, &reply, sizeof(reply), nullptr, 0);
        return;
    }

    // Copy data from shared memory to DMA buffer
    memcpy_bytes(reinterpret_cast<void *>(dma_buf.virt_addr),
                 reinterpret_cast<void *>(shm_map.virt_addr),
                 size);

    // Perform the write
    i32 result = g_device.write_sectors(
        req->sector, req->count, reinterpret_cast<void *>(dma_buf.virt_addr));

    if (result == 0)
    {
        reply.status = 0;
        reply.bytes_written = static_cast<u32>(size);
    }
    else
    {
        reply.status = -500; // VERR_IO
        reply.bytes_written = 0;
    }

    // Cleanup
    device::dma_free(dma_buf.virt_addr);
    sys::shm_unmap(shm_map.virt_addr);

    sys::channel_send(reply_channel, &reply, sizeof(reply), nullptr, 0);
}

/**
 * @brief Handle BLK_FLUSH request.
 */
static void handle_flush(const blk::FlushRequest *req, i32 reply_channel)
{
    blk::FlushReply reply;
    reply.type = blk::BLK_FLUSH_REPLY;
    reply.request_id = req->request_id;

    i32 result = g_device.flush();
    reply.status = result;

    sys::channel_send(reply_channel, &reply, sizeof(reply), nullptr, 0);
}

/**
 * @brief Handle BLK_INFO request.
 */
static void handle_info(const blk::InfoRequest *req, i32 reply_channel)
{
    blk::InfoReply reply;
    reply.type = blk::BLK_INFO_REPLY;
    reply.request_id = req->request_id;
    reply.status = 0;
    reply.sector_size = g_device.sector_size();
    reply.total_sectors = g_device.capacity();
    reply.max_request = blk::MAX_SECTORS_PER_REQUEST;
    reply.readonly = g_device.is_readonly() ? 1 : 0;

    sys::channel_send(reply_channel, &reply, sizeof(reply), nullptr, 0);
}

/**
 * @brief Handle incoming request.
 *
 * @param msg Message buffer.
 * @param len Message length.
 * @param reply_channel Channel to send reply.
 * @param data_handle Optional shared memory handle (for writes).
 */
static void handle_request(const u8 *msg, usize len, i32 reply_channel, u32 data_handle)
{
    if (len < 4)
    {
        return;
    }

    u32 type = *reinterpret_cast<const u32 *>(msg);

    switch (type)
    {
        case blk::BLK_READ:
            if (len >= sizeof(blk::ReadRequest))
            {
                handle_read(reinterpret_cast<const blk::ReadRequest *>(msg), reply_channel);
            }
            break;

        case blk::BLK_WRITE:
            if (len >= sizeof(blk::WriteRequest))
            {
                handle_write(
                    reinterpret_cast<const blk::WriteRequest *>(msg), reply_channel, data_handle);
            }
            break;

        case blk::BLK_FLUSH:
            if (len >= sizeof(blk::FlushRequest))
            {
                handle_flush(reinterpret_cast<const blk::FlushRequest *>(msg), reply_channel);
            }
            break;

        case blk::BLK_INFO:
            if (len >= sizeof(blk::InfoRequest))
            {
                handle_info(reinterpret_cast<const blk::InfoRequest *>(msg), reply_channel);
            }
            break;

        default:
            debug_print("[blkd] Unknown request type: ");
            debug_print_dec(type);
            debug_print("\n");
            break;
    }
}

/**
 * @brief Server main loop.
 */
static void server_loop()
{
    debug_print("[blkd] Entering server loop\n");

    while (true)
    {
        // Receive a message
        u8 msg_buf[256];
        u32 handles[4];
        u32 handle_count = 4;

        i64 len =
            sys::channel_recv(g_service_channel, msg_buf, sizeof(msg_buf), handles, &handle_count);
        if (len < 0)
        {
            // Would block or error, yield and retry
            sys::yield();
            continue;
        }

        // First handle should be the reply channel
        if (handle_count < 1)
        {
            debug_print("[blkd] No reply channel in request\n");
            continue;
        }

        i32 reply_channel = static_cast<i32>(handles[0]);

        // Second handle (if present) is data handle for writes
        u32 data_handle = (handle_count >= 2) ? handles[1] : 0;

        // Handle the request
        handle_request(msg_buf, static_cast<usize>(len), reply_channel, data_handle);

        // Close the reply channel
        sys::channel_close(reply_channel);

        // Close any additional transferred handles (e.g., write data SHM).
        for (u32 i = 1; i < handle_count; i++)
        {
            if (handles[i] == 0)
                continue;
            i32 close_err = sys::shm_close(handles[i]);
            if (close_err != 0)
            {
                // Best-effort fallback: at least drop the handle to avoid cap table exhaustion.
                (void)sys::cap_revoke(handles[i]);
            }
        }
    }
}

/**
 * @brief Main entry point.
 */
extern "C" void _start()
{
    debug_print("[blkd] Block device server starting\n");
    recv_bootstrap_caps();

    // Find VirtIO-blk device
    u64 mmio_phys = 0;
    u32 irq = 0;
    if (!find_blk_device(&mmio_phys, &irq))
    {
        debug_print("[blkd] No VirtIO-blk device found\n");
        sys::exit(1);
    }

    debug_print("[blkd] Found device at ");
    debug_print_hex(mmio_phys);
    debug_print(" IRQ ");
    debug_print_dec(irq);
    debug_print("\n");

    // Initialize the device
    if (!g_device.init(mmio_phys, irq))
    {
        debug_print("[blkd] Device init failed\n");
        sys::exit(1);
    }

    debug_print("[blkd] Device initialized: ");
    debug_print_dec(g_device.capacity());
    debug_print(" sectors (");
    debug_print_dec(g_device.size_bytes() / (1024 * 1024));
    debug_print(" MB)\n");

    // Create service channel
    auto result = sys::channel_create();
    if (result.error != 0)
    {
        debug_print("[blkd] Failed to create channel\n");
        sys::exit(1);
    }
    i32 send_ep = static_cast<i32>(result.val0);
    i32 recv_ep = static_cast<i32>(result.val1);
    // Server only needs the receive endpoint.
    sys::channel_close(send_ep);
    g_service_channel = recv_ep;

    debug_print("[blkd] Service channel created: ");
    debug_print_dec(static_cast<u64>(g_service_channel));
    debug_print("\n");

    // Register with assign system
    i32 err = sys::assign_set("BLKD", static_cast<u32>(g_service_channel));
    if (err != 0)
    {
        debug_print("[blkd] Failed to register assign: ");
        debug_print_dec(static_cast<u64>(-err));
        debug_print("\n");
        // Continue anyway - clients can connect directly if they have the handle
    }
    else
    {
        debug_print("[blkd] Registered as BLKD:\n");
    }

    // Enter the server loop
    server_loop();

    // Should never reach here
    sys::exit(0);
}
