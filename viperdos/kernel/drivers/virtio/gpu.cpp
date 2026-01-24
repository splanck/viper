#include "gpu.hpp"
#include "../../console/serial.hpp"
#include "../../mm/pmm.hpp"

/**
 * @file gpu.cpp
 * @brief VirtIO-GPU driver implementation.
 *
 * @details
 * Implements a basic 2D VirtIO-GPU driver for framebuffer display.
 * The driver supports resource creation, backing memory attachment,
 * scanout configuration, and 2D transfers/flushes.
 */
namespace virtio {

// Global GPU device instance
static GpuDevice g_gpu_device;
static bool g_gpu_initialized = false;

GpuDevice *gpu_device() {
    return g_gpu_initialized ? &g_gpu_device : nullptr;
}

bool GpuDevice::init() {
    // Find virtio-gpu device
    u64 base = find_device(device_type::GPU);
    if (!base) {
        serial::puts("[virtio-gpu] No GPU device found\n");
        return false;
    }

    // Use common init sequence (init, reset, legacy page size, acknowledge, driver)
    if (!basic_init(base)) {
        serial::puts("[virtio-gpu] Device init failed\n");
        return false;
    }

    serial::puts("[virtio-gpu] Initializing GPU device at 0x");
    serial::put_hex(base);
    serial::puts(" version=");
    serial::put_dec(version());
    serial::puts(is_legacy() ? " (legacy)\n" : " (modern)\n");

    // Read number of scanouts from config
    num_scanouts_ = read_config32(8); // offset 8 in GpuConfig
    serial::puts("[virtio-gpu] Number of scanouts: ");
    serial::put_dec(num_scanouts_);
    serial::puts("\n");

    // Negotiate features - we only need basic 2D
    u64 required = 0;
    if (!is_legacy()) {
        required |= features::VERSION_1;
    }

    if (!negotiate_features(required)) {
        serial::puts("[virtio-gpu] Feature negotiation failed\n");
        set_status(status::FAILED);
        return false;
    }

    // Initialize control virtqueue (queue 0)
    if (!controlq_.init(this, 0, 64)) {
        serial::puts("[virtio-gpu] Failed to init controlq\n");
        set_status(status::FAILED);
        return false;
    }

    // Initialize cursor virtqueue (queue 1) - optional
    write32(reg::QUEUE_SEL, 1);
    u32 cursor_queue_size = read32(reg::QUEUE_NUM_MAX);
    if (cursor_queue_size > 0) {
        if (!cursorq_.init(this, 1, cursor_queue_size > 16 ? 16 : cursor_queue_size)) {
            serial::puts("[virtio-gpu] Warning: cursor queue init failed\n");
            // Not fatal - cursor is optional
        }
    }

    // Allocate command buffer
    cmd_buf_phys_ = pmm::alloc_page();
    if (!cmd_buf_phys_) {
        serial::puts("[virtio-gpu] Failed to allocate command buffer\n");
        set_status(status::FAILED);
        return false;
    }
    cmd_buf_ = reinterpret_cast<u8 *>(pmm::phys_to_virt(cmd_buf_phys_));

    // Allocate response buffer
    resp_buf_phys_ = pmm::alloc_page();
    if (!resp_buf_phys_) {
        serial::puts("[virtio-gpu] Failed to allocate response buffer\n");
        pmm::free_page(cmd_buf_phys_);
        set_status(status::FAILED);
        return false;
    }
    resp_buf_ = reinterpret_cast<u8 *>(pmm::phys_to_virt(resp_buf_phys_));

    // Allocate memory entries buffer
    mem_entries_phys_ = pmm::alloc_page();
    if (!mem_entries_phys_) {
        serial::puts("[virtio-gpu] Failed to allocate mem entries buffer\n");
        pmm::free_page(cmd_buf_phys_);
        pmm::free_page(resp_buf_phys_);
        set_status(status::FAILED);
        return false;
    }
    mem_entries_ = reinterpret_cast<GpuMemEntry *>(pmm::phys_to_virt(mem_entries_phys_));

    // Zero buffers
    for (usize i = 0; i < pmm::PAGE_SIZE; i++) {
        cmd_buf_[i] = 0;
        resp_buf_[i] = 0;
    }

    // Device is ready
    add_status(status::DRIVER_OK);

    initialized_ = true;
    serial::puts("[virtio-gpu] Driver initialized\n");
    return true;
}

bool GpuDevice::send_command(usize cmd_size, usize resp_size) {
    // Allocate descriptors
    i32 cmd_desc = controlq_.alloc_desc();
    i32 resp_desc = controlq_.alloc_desc();

    if (cmd_desc < 0 || resp_desc < 0) {
        if (cmd_desc >= 0)
            controlq_.free_desc(cmd_desc);
        if (resp_desc >= 0)
            controlq_.free_desc(resp_desc);
        serial::puts("[virtio-gpu] No free descriptors\n");
        return false;
    }

    // Memory barrier
    asm volatile("dsb sy" ::: "memory");

    // Set up command descriptor (device reads)
    controlq_.set_desc(cmd_desc, cmd_buf_phys_, cmd_size, desc_flags::NEXT);
    controlq_.chain_desc(cmd_desc, resp_desc);

    // Set up response descriptor (device writes)
    controlq_.set_desc(resp_desc, resp_buf_phys_, resp_size, desc_flags::WRITE);

    // Submit and kick
    controlq_.submit(cmd_desc);
    controlq_.kick();

    // Wait for completion (polling)
    bool completed = false;
    for (u32 i = 0; i < 1000000; i++) {
        i32 used = controlq_.poll_used();
        if (used == cmd_desc) {
            completed = true;
            break;
        }
        asm volatile("yield" ::: "memory");
    }

    // Free descriptors
    controlq_.free_desc(cmd_desc);
    controlq_.free_desc(resp_desc);

    if (!completed) {
        serial::puts("[virtio-gpu] Command timeout\n");
        return false;
    }

    // Check response type
    GpuCtrlHdr *resp = reinterpret_cast<GpuCtrlHdr *>(resp_buf_);
    if (resp->type >= gpu_cmd::RESP_ERR_UNSPEC) {
        serial::puts("[virtio-gpu] Command error: 0x");
        serial::put_hex(resp->type);
        serial::puts("\n");
        return false;
    }

    return true;
}

bool GpuDevice::get_display_info(u32 *width, u32 *height) {
    if (!initialized_)
        return false;

    // Build GET_DISPLAY_INFO command
    GpuCtrlHdr *cmd = reinterpret_cast<GpuCtrlHdr *>(cmd_buf_);
    cmd->type = gpu_cmd::GET_DISPLAY_INFO;
    cmd->flags = 0;
    cmd->fence_id = 0;
    cmd->ctx_id = 0;
    cmd->padding = 0;

    if (!send_command(sizeof(GpuCtrlHdr), sizeof(GpuRespDisplayInfo))) {
        return false;
    }

    // Parse response
    GpuRespDisplayInfo *resp = reinterpret_cast<GpuRespDisplayInfo *>(resp_buf_);
    if (resp->hdr.type != gpu_cmd::RESP_OK_DISPLAY_INFO) {
        serial::puts("[virtio-gpu] Unexpected response type\n");
        return false;
    }

    // Find first enabled display
    for (u32 i = 0; i < GPU_MAX_SCANOUTS && i < num_scanouts_; i++) {
        if (resp->pmodes[i].enabled) {
            *width = resp->pmodes[i].r.width;
            *height = resp->pmodes[i].r.height;
            serial::puts("[virtio-gpu] Display ");
            serial::put_dec(i);
            serial::puts(": ");
            serial::put_dec(*width);
            serial::puts("x");
            serial::put_dec(*height);
            serial::puts("\n");
            return true;
        }
    }

    serial::puts("[virtio-gpu] No enabled displays found\n");
    return false;
}

bool GpuDevice::create_resource_2d(u32 resource_id, u32 width, u32 height, u32 format) {
    if (!initialized_)
        return false;

    GpuResourceCreate2d *cmd = reinterpret_cast<GpuResourceCreate2d *>(cmd_buf_);
    cmd->hdr.type = gpu_cmd::RESOURCE_CREATE_2D;
    cmd->hdr.flags = 0;
    cmd->hdr.fence_id = 0;
    cmd->hdr.ctx_id = 0;
    cmd->hdr.padding = 0;
    cmd->resource_id = resource_id;
    cmd->format = format;
    cmd->width = width;
    cmd->height = height;

    if (!send_command(sizeof(GpuResourceCreate2d), sizeof(GpuCtrlHdr))) {
        serial::puts("[virtio-gpu] Failed to create resource\n");
        return false;
    }

    serial::puts("[virtio-gpu] Created resource ");
    serial::put_dec(resource_id);
    serial::puts(" (");
    serial::put_dec(width);
    serial::puts("x");
    serial::put_dec(height);
    serial::puts(")\n");
    return true;
}

bool GpuDevice::attach_backing(u32 resource_id, u64 addr, u32 size) {
    if (!initialized_)
        return false;

    // Set up memory entry
    mem_entries_[0].addr = addr;
    mem_entries_[0].length = size;
    mem_entries_[0].padding = 0;

    // Build command
    GpuResourceAttachBacking *cmd = reinterpret_cast<GpuResourceAttachBacking *>(cmd_buf_);
    cmd->hdr.type = gpu_cmd::RESOURCE_ATTACH_BACKING;
    cmd->hdr.flags = 0;
    cmd->hdr.fence_id = 0;
    cmd->hdr.ctx_id = 0;
    cmd->hdr.padding = 0;
    cmd->resource_id = resource_id;
    cmd->nr_entries = 1;

    // Copy memory entry after the command header
    GpuMemEntry *entry =
        reinterpret_cast<GpuMemEntry *>(cmd_buf_ + sizeof(GpuResourceAttachBacking));
    entry->addr = addr;
    entry->length = size;
    entry->padding = 0;

    usize cmd_size = sizeof(GpuResourceAttachBacking) + sizeof(GpuMemEntry);
    if (!send_command(cmd_size, sizeof(GpuCtrlHdr))) {
        serial::puts("[virtio-gpu] Failed to attach backing\n");
        return false;
    }

    serial::puts("[virtio-gpu] Attached backing memory to resource ");
    serial::put_dec(resource_id);
    serial::puts("\n");
    return true;
}

bool GpuDevice::set_scanout(u32 scanout_id, u32 resource_id, u32 width, u32 height) {
    if (!initialized_)
        return false;

    GpuSetScanout *cmd = reinterpret_cast<GpuSetScanout *>(cmd_buf_);
    cmd->hdr.type = gpu_cmd::SET_SCANOUT;
    cmd->hdr.flags = 0;
    cmd->hdr.fence_id = 0;
    cmd->hdr.ctx_id = 0;
    cmd->hdr.padding = 0;
    cmd->r.x = 0;
    cmd->r.y = 0;
    cmd->r.width = width;
    cmd->r.height = height;
    cmd->scanout_id = scanout_id;
    cmd->resource_id = resource_id;

    if (!send_command(sizeof(GpuSetScanout), sizeof(GpuCtrlHdr))) {
        serial::puts("[virtio-gpu] Failed to set scanout\n");
        return false;
    }

    serial::puts("[virtio-gpu] Set scanout ");
    serial::put_dec(scanout_id);
    serial::puts(" to resource ");
    serial::put_dec(resource_id);
    serial::puts("\n");
    return true;
}

bool GpuDevice::transfer_to_host_2d(u32 resource_id, u32 x, u32 y, u32 width, u32 height) {
    if (!initialized_)
        return false;

    GpuTransferToHost2d *cmd = reinterpret_cast<GpuTransferToHost2d *>(cmd_buf_);
    cmd->hdr.type = gpu_cmd::TRANSFER_TO_HOST_2D;
    cmd->hdr.flags = 0;
    cmd->hdr.fence_id = 0;
    cmd->hdr.ctx_id = 0;
    cmd->hdr.padding = 0;
    cmd->r.x = x;
    cmd->r.y = y;
    cmd->r.width = width;
    cmd->r.height = height;
    cmd->offset = 0;
    cmd->resource_id = resource_id;
    cmd->padding = 0;

    return send_command(sizeof(GpuTransferToHost2d), sizeof(GpuCtrlHdr));
}

bool GpuDevice::flush(u32 resource_id, u32 x, u32 y, u32 width, u32 height) {
    if (!initialized_)
        return false;

    GpuResourceFlush *cmd = reinterpret_cast<GpuResourceFlush *>(cmd_buf_);
    cmd->hdr.type = gpu_cmd::RESOURCE_FLUSH;
    cmd->hdr.flags = 0;
    cmd->hdr.fence_id = 0;
    cmd->hdr.ctx_id = 0;
    cmd->hdr.padding = 0;
    cmd->r.x = x;
    cmd->r.y = y;
    cmd->r.width = width;
    cmd->r.height = height;
    cmd->resource_id = resource_id;
    cmd->padding = 0;

    return send_command(sizeof(GpuResourceFlush), sizeof(GpuCtrlHdr));
}

bool GpuDevice::unref_resource(u32 resource_id) {
    if (!initialized_)
        return false;

    GpuResourceUnref *cmd = reinterpret_cast<GpuResourceUnref *>(cmd_buf_);
    cmd->hdr.type = gpu_cmd::RESOURCE_UNREF;
    cmd->hdr.flags = 0;
    cmd->hdr.fence_id = 0;
    cmd->hdr.ctx_id = 0;
    cmd->hdr.padding = 0;
    cmd->resource_id = resource_id;
    cmd->padding = 0;

    return send_command(sizeof(GpuResourceUnref), sizeof(GpuCtrlHdr));
}

void gpu_init() {
    serial::puts("[virtio-gpu] Starting gpu_init()...\n");
    if (g_gpu_device.init()) {
        g_gpu_initialized = true;
        serial::puts("[virtio-gpu] GPU device ready\n");
    } else {
        serial::puts("[virtio-gpu] GPU device initialization failed or not present\n");
    }
}

} // namespace virtio
