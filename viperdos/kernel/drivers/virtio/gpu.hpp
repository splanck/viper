//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: kernel/drivers/virtio/gpu.hpp
// Purpose: Virtio GPU device driver (virtio-gpu).
// Key invariants: Uses controlq for commands; 2D operations only.
// Ownership/Lifetime: Singleton device; initialized once.
// Links: kernel/drivers/virtio/gpu.cpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "virtio.hpp"
#include "virtqueue.hpp"

/**
 * @file gpu.hpp
 * @brief Virtio GPU device driver (virtio-gpu).
 *
 * @details
 * Virtio-gpu provides a paravirtual 2D/3D graphics interface. This driver
 * implements basic 2D functionality:
 * - Scanout configuration (display resolution)
 * - Framebuffer resource management
 * - 2D transfers and flushes
 *
 * The driver uses two virtqueues:
 * - controlq (queue 0): Command/response for configuration
 * - cursorq (queue 1): Cursor updates (optional)
 */
namespace virtio {

// VirtIO-GPU feature bits
namespace gpu_features {
constexpr u64 VIRGL = 1ULL << 0; // 3D support (virgl)
constexpr u64 EDID = 1ULL << 1;  // EDID support
constexpr u64 RESOURCE_UUID = 1ULL << 2;
constexpr u64 RESOURCE_BLOB = 1ULL << 3;
constexpr u64 CONTEXT_INIT = 1ULL << 4;
} // namespace gpu_features

// VirtIO-GPU command types
namespace gpu_cmd {
// 2D commands
constexpr u32 GET_DISPLAY_INFO = 0x0100;
constexpr u32 RESOURCE_CREATE_2D = 0x0101;
constexpr u32 RESOURCE_UNREF = 0x0102;
constexpr u32 SET_SCANOUT = 0x0103;
constexpr u32 RESOURCE_FLUSH = 0x0104;
constexpr u32 TRANSFER_TO_HOST_2D = 0x0105;
constexpr u32 RESOURCE_ATTACH_BACKING = 0x0106;
constexpr u32 RESOURCE_DETACH_BACKING = 0x0107;
constexpr u32 GET_CAPSET_INFO = 0x0108;
constexpr u32 GET_CAPSET = 0x0109;
constexpr u32 GET_EDID = 0x010A;

// Cursor commands
constexpr u32 UPDATE_CURSOR = 0x0300;
constexpr u32 MOVE_CURSOR = 0x0301;

// Response types
constexpr u32 RESP_OK_NODATA = 0x1100;
constexpr u32 RESP_OK_DISPLAY_INFO = 0x1101;
constexpr u32 RESP_OK_CAPSET_INFO = 0x1102;
constexpr u32 RESP_OK_CAPSET = 0x1103;
constexpr u32 RESP_OK_EDID = 0x1104;

// Error responses
constexpr u32 RESP_ERR_UNSPEC = 0x1200;
constexpr u32 RESP_ERR_OUT_OF_MEMORY = 0x1201;
constexpr u32 RESP_ERR_INVALID_SCANOUT_ID = 0x1202;
constexpr u32 RESP_ERR_INVALID_RESOURCE_ID = 0x1203;
constexpr u32 RESP_ERR_INVALID_CONTEXT_ID = 0x1204;
constexpr u32 RESP_ERR_INVALID_PARAMETER = 0x1205;
} // namespace gpu_cmd

// Pixel formats
namespace gpu_format {
constexpr u32 B8G8R8A8_UNORM = 1;
constexpr u32 B8G8R8X8_UNORM = 2;
constexpr u32 A8R8G8B8_UNORM = 3;
constexpr u32 X8R8G8B8_UNORM = 4;
constexpr u32 R8G8B8A8_UNORM = 67;
constexpr u32 X8B8G8R8_UNORM = 68;
constexpr u32 A8B8G8R8_UNORM = 121;
constexpr u32 R8G8B8X8_UNORM = 134;
} // namespace gpu_format

// Maximum scanouts (displays)
constexpr u32 GPU_MAX_SCANOUTS = 16;

// Control header (all commands start with this)
struct GpuCtrlHdr {
    u32 type;
    u32 flags;
    u64 fence_id;
    u32 ctx_id;
    u32 padding;
} __attribute__((packed));

// Rectangle structure
struct GpuRect {
    u32 x;
    u32 y;
    u32 width;
    u32 height;
} __attribute__((packed));

// Display info for one scanout
struct GpuDisplayOne {
    GpuRect r;
    u32 enabled;
    u32 flags;
} __attribute__((packed));

// GET_DISPLAY_INFO response
struct GpuRespDisplayInfo {
    GpuCtrlHdr hdr;
    GpuDisplayOne pmodes[GPU_MAX_SCANOUTS];
} __attribute__((packed));

// RESOURCE_CREATE_2D command
struct GpuResourceCreate2d {
    GpuCtrlHdr hdr;
    u32 resource_id;
    u32 format;
    u32 width;
    u32 height;
} __attribute__((packed));

// RESOURCE_UNREF command
struct GpuResourceUnref {
    GpuCtrlHdr hdr;
    u32 resource_id;
    u32 padding;
} __attribute__((packed));

// SET_SCANOUT command
struct GpuSetScanout {
    GpuCtrlHdr hdr;
    GpuRect r;
    u32 scanout_id;
    u32 resource_id;
} __attribute__((packed));

// RESOURCE_FLUSH command
struct GpuResourceFlush {
    GpuCtrlHdr hdr;
    GpuRect r;
    u32 resource_id;
    u32 padding;
} __attribute__((packed));

// TRANSFER_TO_HOST_2D command
struct GpuTransferToHost2d {
    GpuCtrlHdr hdr;
    GpuRect r;
    u64 offset;
    u32 resource_id;
    u32 padding;
} __attribute__((packed));

// Memory entry for RESOURCE_ATTACH_BACKING
struct GpuMemEntry {
    u64 addr;
    u32 length;
    u32 padding;
} __attribute__((packed));

// RESOURCE_ATTACH_BACKING command
struct GpuResourceAttachBacking {
    GpuCtrlHdr hdr;
    u32 resource_id;
    u32 nr_entries;
    // Followed by nr_entries GpuMemEntry structures
} __attribute__((packed));

// Cursor position structure
struct GpuCursorPos {
    u32 scanout_id;
    u32 x;
    u32 y;
    u32 padding;
} __attribute__((packed));

// UPDATE_CURSOR / MOVE_CURSOR command
struct GpuUpdateCursor {
    GpuCtrlHdr hdr;
    GpuCursorPos pos;
    u32 resource_id;
    u32 hot_x;
    u32 hot_y;
    u32 padding;
} __attribute__((packed));

// VirtIO-GPU configuration space
struct GpuConfig {
    u32 events_read;
    u32 events_clear;
    u32 num_scanouts;
    u32 num_capsets;
} __attribute__((packed));

/**
 * @brief VirtIO-GPU device driver.
 *
 * @details
 * Provides 2D framebuffer functionality via VirtIO-GPU protocol.
 * Supports:
 * - Display enumeration
 * - Framebuffer resource creation
 * - Scanout configuration
 * - 2D transfers and flushes
 */
class GpuDevice : public Device {
  public:
    /**
     * @brief Initialize the VirtIO-GPU device.
     *
     * @return true on success, false on failure.
     */
    bool init();

    /**
     * @brief Get display information.
     *
     * @param width Output: display width in pixels.
     * @param height Output: display height in pixels.
     * @return true if display is available.
     */
    bool get_display_info(u32 *width, u32 *height);

    /**
     * @brief Create a 2D framebuffer resource.
     *
     * @param resource_id ID to assign to the resource.
     * @param width Width in pixels.
     * @param height Height in pixels.
     * @param format Pixel format (gpu_format::*).
     * @return true on success.
     */
    bool create_resource_2d(u32 resource_id, u32 width, u32 height, u32 format);

    /**
     * @brief Attach backing memory to a resource.
     *
     * @param resource_id Resource to attach to.
     * @param addr Physical address of framebuffer memory.
     * @param size Size in bytes.
     * @return true on success.
     */
    bool attach_backing(u32 resource_id, u64 addr, u32 size);

    /**
     * @brief Set the scanout (display) to show a resource.
     *
     * @param scanout_id Display index (usually 0).
     * @param resource_id Resource to display.
     * @param width Display width.
     * @param height Display height.
     * @return true on success.
     */
    bool set_scanout(u32 scanout_id, u32 resource_id, u32 width, u32 height);

    /**
     * @brief Transfer framebuffer data to the host.
     *
     * @param resource_id Resource containing the data.
     * @param x X offset.
     * @param y Y offset.
     * @param width Width of region.
     * @param height Height of region.
     * @return true on success.
     */
    bool transfer_to_host_2d(u32 resource_id, u32 x, u32 y, u32 width, u32 height);

    /**
     * @brief Flush a region to the display.
     *
     * @param resource_id Resource to flush.
     * @param x X offset.
     * @param y Y offset.
     * @param width Width of region.
     * @param height Height of region.
     * @return true on success.
     */
    bool flush(u32 resource_id, u32 x, u32 y, u32 width, u32 height);

    /**
     * @brief Destroy a resource.
     *
     * @param resource_id Resource to destroy.
     * @return true on success.
     */
    bool unref_resource(u32 resource_id);

    /**
     * @brief Get number of scanouts (displays).
     */
    u32 num_scanouts() const {
        return num_scanouts_;
    }

    /**
     * @brief Check if device is initialized.
     */
    bool is_initialized() const {
        return initialized_;
    }

    /**
     * @brief Set up the hardware cursor image and position.
     *
     * @param pixels BGRA pixel data (width * height * 4 bytes).
     * @param width Cursor image width (max 64).
     * @param height Cursor image height (max 64).
     * @param hot_x Hotspot X offset.
     * @param hot_y Hotspot Y offset.
     * @return true on success.
     */
    bool setup_cursor(const u32 *pixels, u32 width, u32 height, u32 hot_x, u32 hot_y);

    /**
     * @brief Move the hardware cursor to a new position.
     *
     * @param x X position on scanout 0.
     * @param y Y position on scanout 0.
     * @return true on success.
     */
    bool move_cursor(u32 x, u32 y);

    /**
     * @brief Check if hardware cursor is active.
     */
    bool has_cursor() const {
        return cursor_active_;
    }

  private:
    Virtqueue controlq_;
    Virtqueue cursorq_;

    u32 num_scanouts_{0};
    bool initialized_{false};

    // DMA buffers (using helper from virtio.hpp)
    DmaBuffer cmd_dma_;
    DmaBuffer resp_dma_;
    DmaBuffer mem_entries_dma_;
    DmaBuffer cursor_cmd_dma_;
    DmaBuffer cursor_img_dma_;

    // Convenience pointers for existing code
    u8 *cmd_buf_{nullptr};
    u64 cmd_buf_phys_{0};
    u8 *resp_buf_{nullptr};
    u64 resp_buf_phys_{0};
    GpuMemEntry *mem_entries_{nullptr};
    u64 mem_entries_phys_{0};

    static constexpr usize CMD_BUF_SIZE = 4096;
    static constexpr usize RESP_BUF_SIZE = 4096;

    // Cursor state
    u8 *cursor_cmd_buf_{nullptr};
    u64 cursor_cmd_phys_{0};
    u8 *cursor_img_buf_{nullptr};
    u64 cursor_img_phys_{0};
    bool cursor_active_{false};

    static constexpr u32 CURSOR_RES_ID = 100;
    static constexpr u32 MAX_CURSOR_DIM = 64;

    /**
     * @brief Send a command and wait for response.
     *
     * @param cmd_size Size of command data.
     * @param resp_size Expected response size.
     * @return true if command succeeded (response OK).
     */
    bool send_command(usize cmd_size, usize resp_size);

    /**
     * @brief Send a cursor command via the cursor queue (fire-and-forget).
     *
     * @param cmd_size Size of cursor command data.
     * @return true on success.
     */
    bool send_cursor_command(usize cmd_size);
};

// Global GPU device initialization and access
void gpu_init();
GpuDevice *gpu_device();

} // namespace virtio
