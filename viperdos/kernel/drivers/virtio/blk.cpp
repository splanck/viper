#include "blk.hpp"
#include "../../arch/aarch64/gic.hpp"
#include "../../console/serial.hpp"
#include "../../include/constants.hpp"
#include "../../mm/pmm.hpp"

namespace kc = kernel::constants;

/**
 * @file blk.cpp
 * @brief Virtio block device driver implementation.
 *
 * @details
 * Implements an interrupt-driven virtio-blk driver with polling fallback.
 * Requests are submitted via a single virtqueue. The driver waits for
 * completion via interrupt, falling back to polling on timeout.
 *
 * The driver assumes buffers are identity-mapped so it can compute physical
 * addresses directly using `pmm::virt_to_phys`.
 *
 * Two-disk architecture: The kernel uses the SYSTEM disk (2MB) for /sys,
 * while the larger USER disk (8MB) is handled by userspace blkd.
 */

// Freestanding offsetof
#define OFFSETOF(type, member) __builtin_offsetof(type, member)

// Expected capacity for system disk (2MB = 4096 sectors)
constexpr u64 SYSTEM_DISK_SECTORS = 4096;

namespace virtio
{

/**
 * @brief Probe a block device's capacity without claiming it.
 *
 * @param base MMIO base address of the device.
 * @return Capacity in sectors, or 0 if not a valid block device.
 */
static u64 probe_blk_capacity(u64 base)
{
    volatile u32 *mmio = reinterpret_cast<volatile u32 *>(base);

    // Check magic
    u32 magic = mmio[reg::MAGIC / 4];
    if (magic != MAGIC_VALUE)
        return 0;

    // Check it's a block device
    u32 dev_id = mmio[reg::DEVICE_ID / 4];
    if (dev_id != device_type::BLK)
        return 0;

    // Read capacity from config space (offset 0x100)
    volatile u32 *config = reinterpret_cast<volatile u32 *>(base + reg::CONFIG);
    u32 cap_lo = config[0];
    u32 cap_hi = config[1];
    return (static_cast<u64>(cap_hi) << 32) | cap_lo;
}

/**
 * @brief Find a block device with specific capacity.
 *
 * @param expected_sectors Expected capacity in sectors.
 * @return MMIO base address of matching device, or 0 if not found.
 */
static u64 find_blk_by_capacity(u64 expected_sectors)
{
    // Scan virtio MMIO range for block devices
    for (u64 addr = 0x0a000000; addr < 0x0a004000; addr += 0x200)
    {
        u64 capacity = probe_blk_capacity(addr);
        if (capacity == expected_sectors)
        {
            return addr;
        }
    }
    return 0;
}

// Global block device instance
static BlkDevice g_blk_device;
static bool g_blk_initialized = false;

// Use centralized VirtIO IRQ base
constexpr u32 VIRTIO_IRQ_BASE = kernel::constants::hw::VIRTIO_IRQ_BASE;

/**
 * @brief IRQ handler for virtio-blk interrupts.
 *
 * @details
 * Called by the GIC when a virtio-blk interrupt fires. Delegates to the
 * device's handle_interrupt() method.
 */
static void blk_irq_handler(u32)
{
    if (g_blk_initialized)
    {
        g_blk_device.handle_interrupt();
    }
}

/** @copydoc virtio::blk_device */
BlkDevice *blk_device()
{
    return g_blk_initialized ? &g_blk_device : nullptr;
}

/** @copydoc virtio::BlkDevice::init */
bool BlkDevice::init()
{
    // Two-disk architecture: find the SYSTEM disk (2MB = 4096 sectors)
    // The larger USER disk (8MB) is handled by userspace blkd
    u64 base = find_blk_by_capacity(SYSTEM_DISK_SECTORS);
    if (!base)
    {
        // Fallback: try any block device (for single-disk setups)
        serial::puts("[virtio-blk] System disk (2MB) not found, trying first available\n");
        base = find_device(device_type::BLK);
    }
    if (!base)
    {
        serial::puts("[virtio-blk] No block device found\n");
        return false;
    }

    // Use common init sequence (init, reset, legacy page size, acknowledge, driver)
    if (!basic_init(base))
    {
        serial::puts("[virtio-blk] Device init failed\n");
        return false;
    }

    // Calculate device index for IRQ (device base - VIRTIO_BASE) / VIRTIO_STRIDE
    device_index_ = static_cast<u32>((base - kernel::constants::hw::VIRTIO_MMIO_BASE) / kernel::constants::hw::VIRTIO_DEVICE_STRIDE);
    irq_num_ = VIRTIO_IRQ_BASE + device_index_;

    serial::puts("[virtio-blk] Initializing block device at ");
    serial::put_hex(base);
    serial::puts(" (IRQ ");
    serial::put_dec(irq_num_);
    serial::puts(")\n");

    // Read configuration
    capacity_ = read_config64(0); // offset 0: capacity

    // Try to read block size from config
    // This is optional - default is 512
    sector_size_ = 512;

    // Check for read-only
    write32(reg::DEVICE_FEATURES_SEL, 0);
    u32 features = read32(reg::DEVICE_FEATURES);
    readonly_ = (features & blk_features::RO) != 0;

    serial::puts("[virtio-blk] Capacity: ");
    serial::put_dec(capacity_);
    serial::puts(" sectors (");
    serial::put_dec((capacity_ * sector_size_) / (1024 * 1024));
    serial::puts(" MB)\n");

    if (readonly_)
    {
        serial::puts("[virtio-blk] Device is read-only\n");
    }

    // Negotiate features - we just need basic read/write
    if (!negotiate_features(0))
    {
        serial::puts("[virtio-blk] Feature negotiation failed\n");
        set_status(status::FAILED);
        return false;
    }

    // Initialize virtqueue
    if (!vq_.init(this, 0, 128))
    {
        serial::puts("[virtio-blk] Virtqueue init failed\n");
        set_status(status::FAILED);
        return false;
    }

    // Allocate request buffer (page for pending requests)
    requests_phys_ = pmm::alloc_page();
    if (!requests_phys_)
    {
        serial::puts("[virtio-blk] Failed to allocate request buffer\n");
        set_status(status::FAILED);
        return false;
    }
    requests_ = reinterpret_cast<PendingRequest *>(pmm::phys_to_virt(requests_phys_));

    // Zero request buffer
    for (usize i = 0; i < pmm::PAGE_SIZE / sizeof(u64); i++)
    {
        reinterpret_cast<u64 *>(requests_)[i] = 0;
    }

    // Device is ready
    add_status(status::DRIVER_OK);

    // Register IRQ handler
    gic::register_handler(irq_num_, blk_irq_handler);
    gic::enable_irq(irq_num_);

    serial::puts("[virtio-blk] Driver initialized (interrupt-driven)\n");
    return true;
}

/**
 * @brief Handle virtio-blk interrupt.
 *
 * @details
 * Acknowledges the interrupt, checks the used ring for completions,
 * and signals the waiting request.
 */
void BlkDevice::handle_interrupt()
{
    // Read and acknowledge interrupt status
    u32 isr = read_isr();
    if (isr & 0x1) // Used buffer notification
    {
        ack_interrupt(0x1);

        // Check for completed requests
        i32 completed = vq_.poll_used();
        if (completed >= 0)
        {
            completed_desc_ = completed;
            io_complete_ = true;
        }
    }
    if (isr & 0x2) // Configuration change
    {
        ack_interrupt(0x2);
    }
}

/** @copydoc virtio::BlkDevice::do_request */
i32 BlkDevice::do_request(u32 type, u64 sector, u32 count, void *buf)
{
    if (type == blk_type::OUT && readonly_)
    {
        serial::puts("[virtio-blk] Write to read-only device\n");
        return -1;
    }

    // Find a free request slot
    int req_idx = -1;
    for (usize i = 0; i < MAX_PENDING; i++)
    {
        if (!async_requests_[i].in_use)
        {
            req_idx = i;
            break;
        }
    }
    if (req_idx < 0)
    {
        serial::puts("[virtio-blk] No free request slots\n");
        return -1;
    }

    PendingRequest &req = requests_[req_idx];
    AsyncRequest &async = async_requests_[req_idx];
    async.in_use = true;
    async.completed = false;
    async.result = 0;
    async.callback = nullptr;
    async.user_data = nullptr;
    req.header.type = type;
    req.header.reserved = 0;
    req.header.sector = sector;
    req.status = 0xFF; // Invalid/pending

    // Calculate physical addresses
    u64 header_phys = requests_phys_ + req_idx * sizeof(PendingRequest);
    u64 status_phys = header_phys + OFFSETOF(PendingRequest, status);

    // Get physical address of data buffer
    // Assuming buf is in kernel identity-mapped region
    u64 buf_phys = pmm::virt_to_phys(buf);
    u32 buf_len = count * sector_size_;

    // Allocate 3 descriptors for the request chain
    i32 desc0 = vq_.alloc_desc();
    i32 desc1 = vq_.alloc_desc();
    i32 desc2 = vq_.alloc_desc();

    if (desc0 < 0 || desc1 < 0 || desc2 < 0)
    {
        if (desc0 >= 0)
            vq_.free_desc(desc0);
        if (desc1 >= 0)
            vq_.free_desc(desc1);
        if (desc2 >= 0)
            vq_.free_desc(desc2);
        async.in_use = false;
        serial::puts("[virtio-blk] No free descriptors\n");
        return -1;
    }

    // Store descriptor indices for later cleanup
    async.desc_head = desc0;
    async.desc_data = desc1;
    async.desc_status = desc2;

    // Descriptor 0: Request header (device reads)
    vq_.set_desc(desc0, header_phys, sizeof(BlkReqHeader), desc_flags::NEXT);
    vq_.chain_desc(desc0, desc1);

    // Descriptor 1: Data buffer
    u16 data_flags = desc_flags::NEXT;
    if (type == blk_type::IN)
    {
        data_flags |= desc_flags::WRITE; // Device writes to this buffer
    }
    vq_.set_desc(desc1, buf_phys, buf_len, data_flags);
    vq_.chain_desc(desc1, desc2);

    // Descriptor 2: Status (device writes)
    vq_.set_desc(desc2, status_phys, 1, desc_flags::WRITE);

    // Clear completion state before submitting
    io_complete_ = false;
    completed_desc_ = -1;

    // Memory barrier before submitting
    asm volatile("dsb sy" ::: "memory");

    // Submit and notify
    vq_.submit(desc0);
    vq_.kick();

    // Wait for completion: interrupt-driven with polling fallback
    bool got_completion = false;
    constexpr u32 INTERRUPT_TIMEOUT = 100000; // Iterations to wait for interrupt
    constexpr u32 POLL_TIMEOUT = 10000000;    // Total polling iterations (fallback)

    // First, try to wait for interrupt
    for (u32 i = 0; i < INTERRUPT_TIMEOUT; i++)
    {
        // Check if interrupt signaled completion
        if (io_complete_ && completed_desc_ == desc0)
        {
            got_completion = true;
            break;
        }

        // Wait for interrupt (low power)
        asm volatile("wfi" ::: "memory");
    }

    // Fallback to polling if interrupt didn't fire
    if (!got_completion)
    {
        for (u32 i = 0; i < POLL_TIMEOUT; i++)
        {
            i32 completed = vq_.poll_used();
            if (completed == desc0)
            {
                got_completion = true;
                break;
            }
            // Yield CPU while polling
            asm volatile("yield" ::: "memory");
        }
    }

    if (!got_completion)
    {
        serial::puts("[virtio-blk] Request timed out!\n");
        vq_.free_desc(desc0);
        vq_.free_desc(desc1);
        vq_.free_desc(desc2);
        async.in_use = false;
        return -1;
    }

    // Free descriptors
    vq_.free_desc(desc0);
    vq_.free_desc(desc1);
    vq_.free_desc(desc2);

    // Check status
    u8 status = req.status;
    async.in_use = false;

    if (status != blk_status::OK)
    {
        serial::puts("[virtio-blk] Request failed, status=");
        serial::put_dec(status);
        serial::puts("\n");
        return -1;
    }

    return 0;
}

/** @copydoc virtio::BlkDevice::read_sectors */
i32 BlkDevice::read_sectors(u64 sector, u32 count, void *buf)
{
    if (!buf || count == 0)
        return -1;
    if (sector + count > capacity_)
    {
        serial::puts("[virtio-blk] Read past end of disk\n");
        return -1;
    }

    return do_request(blk_type::IN, sector, count, buf);
}

/** @copydoc virtio::BlkDevice::write_sectors */
i32 BlkDevice::write_sectors(u64 sector, u32 count, const void *buf)
{
    if (!buf || count == 0)
        return -1;
    if (sector + count > capacity_)
    {
        serial::puts("[virtio-blk] Write past end of disk\n");
        return -1;
    }

    return do_request(blk_type::OUT, sector, count, const_cast<void *>(buf));
}

/** @copydoc virtio::BlkDevice::flush */
i32 BlkDevice::flush()
{
    // Find a free request slot
    int req_idx = -1;
    for (usize i = 0; i < MAX_PENDING; i++)
    {
        if (!async_requests_[i].in_use)
        {
            req_idx = i;
            break;
        }
    }
    if (req_idx < 0)
        return -1;

    PendingRequest &req = requests_[req_idx];
    AsyncRequest &async = async_requests_[req_idx];
    async.in_use = true;
    async.completed = false;
    async.callback = nullptr;
    req.header.type = blk_type::FLUSH;
    req.header.reserved = 0;
    req.header.sector = 0;
    req.status = 0xFF;

    u64 header_phys = requests_phys_ + req_idx * sizeof(PendingRequest);
    u64 status_phys = header_phys + OFFSETOF(PendingRequest, status);

    // Flush only needs header and status
    i32 desc0 = vq_.alloc_desc();
    i32 desc1 = vq_.alloc_desc();

    if (desc0 < 0 || desc1 < 0)
    {
        if (desc0 >= 0)
            vq_.free_desc(desc0);
        if (desc1 >= 0)
            vq_.free_desc(desc1);
        async.in_use = false;
        return -1;
    }

    // Track descriptors
    async.desc_head = desc0;
    async.desc_data = -1; // Flush has no data descriptor
    async.desc_status = desc1;

    vq_.set_desc(desc0, header_phys, sizeof(BlkReqHeader), desc_flags::NEXT);
    vq_.chain_desc(desc0, desc1);
    vq_.set_desc(desc1, status_phys, 1, desc_flags::WRITE);

    // Clear completion state before submitting
    io_complete_ = false;
    completed_desc_ = -1;

    vq_.submit(desc0);
    vq_.kick();

    // Wait for completion: interrupt-driven with polling fallback
    bool got_completion = false;
    constexpr u32 INTERRUPT_TIMEOUT = 100000;

    // First, try to wait for interrupt
    for (u32 i = 0; i < INTERRUPT_TIMEOUT; i++)
    {
        if (io_complete_ && completed_desc_ == desc0)
        {
            got_completion = true;
            break;
        }
        asm volatile("wfi" ::: "memory");
    }

    // Fallback to polling with timeout
    if (!got_completion)
    {
        constexpr u32 POLLING_TIMEOUT = 10000000; // ~10 million iterations
        for (u32 poll_count = 0; poll_count < POLLING_TIMEOUT; poll_count++)
        {
            i32 completed = vq_.poll_used();
            if (completed == desc0)
            {
                got_completion = true;
                break;
            }
            asm volatile("yield" ::: "memory");
        }
        if (!got_completion)
        {
            serial::puts("[virtio-blk] Flush timeout in polling fallback\n");
            vq_.free_desc(desc0);
            vq_.free_desc(desc1);
            async.in_use = false;
            return -1;
        }
    }

    vq_.free_desc(desc0);
    vq_.free_desc(desc1);

    u8 status = req.status;
    async.in_use = false;

    return (status == blk_status::OK) ? 0 : -1;
}

// =========================================================================
// Async I/O Implementation
// =========================================================================

/** @copydoc virtio::BlkDevice::submit_async */
BlkDevice::RequestHandle BlkDevice::submit_async(
    u32 type, u64 sector, u32 count, void *buf, CompletionCallback callback, void *user_data)
{
    if (type == blk_type::OUT && readonly_)
    {
        serial::puts("[virtio-blk] Write to read-only device\n");
        return INVALID_HANDLE;
    }

    // Find a free request slot
    int req_idx = -1;
    for (usize i = 0; i < MAX_PENDING; i++)
    {
        if (!async_requests_[i].in_use)
        {
            req_idx = i;
            break;
        }
    }
    if (req_idx < 0)
    {
        serial::puts("[virtio-blk] No free request slots\n");
        return INVALID_HANDLE;
    }

    PendingRequest &req = requests_[req_idx];
    AsyncRequest &async = async_requests_[req_idx];
    async.in_use = true;
    async.completed = false;
    async.result = 0;
    async.callback = callback;
    async.user_data = user_data;
    req.header.type = type;
    req.header.reserved = 0;
    req.header.sector = sector;
    req.status = 0xFF; // Invalid/pending

    // Calculate physical addresses
    u64 header_phys = requests_phys_ + req_idx * sizeof(PendingRequest);
    u64 status_phys = header_phys + OFFSETOF(PendingRequest, status);

    // Get physical address of data buffer
    u64 buf_phys = pmm::virt_to_phys(buf);
    u32 buf_len = count * sector_size_;

    // Allocate 3 descriptors for the request chain
    i32 desc0 = vq_.alloc_desc();
    i32 desc1 = vq_.alloc_desc();
    i32 desc2 = vq_.alloc_desc();

    if (desc0 < 0 || desc1 < 0 || desc2 < 0)
    {
        if (desc0 >= 0)
            vq_.free_desc(desc0);
        if (desc1 >= 0)
            vq_.free_desc(desc1);
        if (desc2 >= 0)
            vq_.free_desc(desc2);
        async.in_use = false;
        serial::puts("[virtio-blk] No free descriptors\n");
        return INVALID_HANDLE;
    }

    // Store descriptor indices for later cleanup
    async.desc_head = desc0;
    async.desc_data = desc1;
    async.desc_status = desc2;

    // Descriptor 0: Request header (device reads)
    vq_.set_desc(desc0, header_phys, sizeof(BlkReqHeader), desc_flags::NEXT);
    vq_.chain_desc(desc0, desc1);

    // Descriptor 1: Data buffer
    u16 data_flags = desc_flags::NEXT;
    if (type == blk_type::IN)
    {
        data_flags |= desc_flags::WRITE; // Device writes to this buffer
    }
    vq_.set_desc(desc1, buf_phys, buf_len, data_flags);
    vq_.chain_desc(desc1, desc2);

    // Descriptor 2: Status (device writes)
    vq_.set_desc(desc2, status_phys, 1, desc_flags::WRITE);

    // Memory barrier before submitting
    asm volatile("dsb sy" ::: "memory");

    // Submit and notify (non-blocking)
    vq_.submit(desc0);
    vq_.kick();

    return static_cast<RequestHandle>(req_idx);
}

/** @copydoc virtio::BlkDevice::read_async */
BlkDevice::RequestHandle BlkDevice::read_async(
    u64 sector, u32 count, void *buf, CompletionCallback callback, void *user_data)
{
    if (!buf || count == 0)
        return INVALID_HANDLE;
    if (sector + count > capacity_)
    {
        serial::puts("[virtio-blk] Read past end of disk\n");
        return INVALID_HANDLE;
    }

    return submit_async(blk_type::IN, sector, count, buf, callback, user_data);
}

/** @copydoc virtio::BlkDevice::write_async */
BlkDevice::RequestHandle BlkDevice::write_async(
    u64 sector, u32 count, const void *buf, CompletionCallback callback, void *user_data)
{
    if (!buf || count == 0)
        return INVALID_HANDLE;
    if (sector + count > capacity_)
    {
        serial::puts("[virtio-blk] Write past end of disk\n");
        return INVALID_HANDLE;
    }

    return submit_async(blk_type::OUT, sector, count, const_cast<void *>(buf), callback, user_data);
}

/** @copydoc virtio::BlkDevice::is_complete */
bool BlkDevice::is_complete(RequestHandle handle)
{
    if (handle < 0 || handle >= static_cast<i32>(MAX_PENDING))
        return false;
    if (!async_requests_[handle].in_use)
        return false;

    return async_requests_[handle].completed;
}

/** @copydoc virtio::BlkDevice::get_result */
i32 BlkDevice::get_result(RequestHandle handle)
{
    if (handle < 0 || handle >= static_cast<i32>(MAX_PENDING))
        return -1;
    if (!async_requests_[handle].in_use)
        return -1;
    if (!async_requests_[handle].completed)
        return -1; // Still pending

    return async_requests_[handle].result;
}

/** @copydoc virtio::BlkDevice::wait_complete */
i32 BlkDevice::wait_complete(RequestHandle handle)
{
    if (handle < 0 || handle >= static_cast<i32>(MAX_PENDING))
        return -1;
    if (!async_requests_[handle].in_use)
        return -1;

    AsyncRequest &async = async_requests_[handle];
    PendingRequest &req = requests_[handle];

    // Wait for completion: interrupt-driven with polling fallback
    constexpr u32 INTERRUPT_TIMEOUT = 100000;
    constexpr u32 POLL_TIMEOUT = 10000000;

    // First, try to wait for interrupt
    for (u32 i = 0; i < INTERRUPT_TIMEOUT && !async.completed; i++)
    {
        // Check if interrupt handler marked it complete
        if (io_complete_ && completed_desc_ == async.desc_head)
        {
            // Mark this specific request as completed
            async.completed = true;
            async.result = (req.status == blk_status::OK) ? 0 : -1;
            break;
        }
        asm volatile("wfi" ::: "memory");
    }

    // Fallback to polling if not yet completed
    if (!async.completed)
    {
        for (u32 i = 0; i < POLL_TIMEOUT && !async.completed; i++)
        {
            i32 completed = vq_.poll_used();
            if (completed == async.desc_head)
            {
                async.completed = true;
                async.result = (req.status == blk_status::OK) ? 0 : -1;
                break;
            }
            asm volatile("yield" ::: "memory");
        }
    }

    if (!async.completed)
    {
        serial::puts("[virtio-blk] Async request timed out\n");
        // Free descriptors
        vq_.free_desc(async.desc_head);
        vq_.free_desc(async.desc_data);
        vq_.free_desc(async.desc_status);
        async.in_use = false;
        return -1;
    }

    // Free descriptors
    vq_.free_desc(async.desc_head);
    vq_.free_desc(async.desc_data);
    vq_.free_desc(async.desc_status);

    i32 result = async.result;
    async.in_use = false;

    return result;
}

/** @copydoc virtio::BlkDevice::process_completions */
u32 BlkDevice::process_completions()
{
    u32 processed = 0;

    // Poll the used ring for completions
    while (true)
    {
        i32 completed_desc = vq_.poll_used();
        if (completed_desc < 0)
            break;

        // Find which async request this belongs to
        for (usize i = 0; i < MAX_PENDING; i++)
        {
            AsyncRequest &async = async_requests_[i];
            if (async.in_use && !async.completed && async.desc_head == completed_desc)
            {
                PendingRequest &req = requests_[i];

                // Mark as completed
                async.completed = true;
                async.result = (req.status == blk_status::OK) ? 0 : -1;

                // Invoke callback if registered
                if (async.callback)
                {
                    async.callback(static_cast<RequestHandle>(i), async.result, async.user_data);
                }

                // Free descriptors
                vq_.free_desc(async.desc_head);
                vq_.free_desc(async.desc_data);
                vq_.free_desc(async.desc_status);

                // Mark slot as free for reuse
                async.in_use = false;

                processed++;
                break;
            }
        }
    }

    return processed;
}

/** @copydoc virtio::blk_init */
void blk_init()
{
    if (g_blk_device.init())
    {
        g_blk_initialized = true;
    }
}

// =============================================================================
// User Disk Block Device (8MB = 16384 sectors)
// =============================================================================

// Expected capacity for user disk (8MB = 16384 sectors)
constexpr u64 USER_DISK_SECTORS = 16384;

// User disk block device instance
static BlkDevice g_user_blk_device;
static bool g_user_blk_initialized = false;

/**
 * @brief IRQ handler for user disk virtio-blk interrupts.
 */
static void user_blk_irq_handler(u32)
{
    if (g_user_blk_initialized)
    {
        g_user_blk_device.handle_interrupt();
    }
}

/** @copydoc virtio::user_blk_device */
BlkDevice *user_blk_device()
{
    return g_user_blk_initialized ? &g_user_blk_device : nullptr;
}

/**
 * @brief Initialize the user disk block device.
 *
 * @details
 * Finds and initializes the 8MB user disk (16384 sectors).
 * This is separate from the system disk to support the two-disk architecture.
 */
bool BlkDevice::init_user_disk()
{
    // Find the USER disk (8MB = 16384 sectors)
    u64 base = find_blk_by_capacity(USER_DISK_SECTORS);
    if (!base)
    {
        serial::puts("[virtio-blk] User disk (8MB) not found\n");
        return false;
    }

    // Use common init sequence
    if (!basic_init(base))
    {
        serial::puts("[virtio-blk] User disk init failed\n");
        return false;
    }

    // Calculate device index for IRQ
    device_index_ = static_cast<u32>((base - kernel::constants::hw::VIRTIO_MMIO_BASE) /
                                      kernel::constants::hw::VIRTIO_DEVICE_STRIDE);
    irq_num_ = VIRTIO_IRQ_BASE + device_index_;

    serial::puts("[virtio-blk] Initializing user disk at ");
    serial::put_hex(base);
    serial::puts(" (IRQ ");
    serial::put_dec(irq_num_);
    serial::puts(")\n");

    // Read configuration
    capacity_ = read_config64(0);
    sector_size_ = 512;

    // Check for read-only
    write32(reg::DEVICE_FEATURES_SEL, 0);
    u32 features = read32(reg::DEVICE_FEATURES);
    readonly_ = (features & blk_features::RO) != 0;

    serial::puts("[virtio-blk] User disk capacity: ");
    serial::put_dec(capacity_);
    serial::puts(" sectors (");
    serial::put_dec((capacity_ * sector_size_) / (1024 * 1024));
    serial::puts(" MB)\n");

    // Negotiate features
    if (!negotiate_features(0))
    {
        serial::puts("[virtio-blk] User disk feature negotiation failed\n");
        set_status(status::FAILED);
        return false;
    }

    // Initialize virtqueue
    if (!vq_.init(this, 0, 128))
    {
        serial::puts("[virtio-blk] User disk virtqueue init failed\n");
        set_status(status::FAILED);
        return false;
    }

    // Allocate request buffer
    requests_phys_ = pmm::alloc_page();
    if (!requests_phys_)
    {
        serial::puts("[virtio-blk] Failed to allocate user disk request buffer\n");
        set_status(status::FAILED);
        return false;
    }
    requests_ = reinterpret_cast<PendingRequest *>(pmm::phys_to_virt(requests_phys_));

    // Zero request buffer
    for (usize i = 0; i < pmm::PAGE_SIZE / sizeof(u64); i++)
    {
        reinterpret_cast<u64 *>(requests_)[i] = 0;
    }

    // Device is ready
    add_status(status::DRIVER_OK);

    // Register IRQ handler
    gic::register_handler(irq_num_, user_blk_irq_handler);
    gic::enable_irq(irq_num_);

    serial::puts("[virtio-blk] User disk driver initialized\n");
    return true;
}

/** @copydoc virtio::user_blk_init */
void user_blk_init()
{
    if (g_user_blk_device.init_user_disk())
    {
        g_user_blk_initialized = true;
    }
}

} // namespace virtio
