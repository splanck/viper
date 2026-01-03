//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: kernel/main.cpp
// Purpose: Kernel entry point and early initialization sequence.
// Key invariants: Called once from boot.S; scheduler::start() never returns.
// Ownership/Lifetime: Stateless; subsystems own their internal state.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

/**
 * @file main.cpp
 * @brief ViperOS kernel entry point and early initialization sequence.
 *
 * @details
 * This translation unit contains the top-level C++ entry point invoked by the
 * early assembly boot stub (`boot.S`). Its responsibilities are to bring up the
 * minimum set of kernel subsystems required to run the scheduler:
 *
 * - Establish early console output (serial and optional graphics console).
 * - Initialize firmware-provided devices (e.g. QEMU fw_cfg) and framebuffer.
 * - Bring up physical memory management (PMM), virtual memory scaffolding (VMM),
 *   and the kernel heap allocator.
 * - Install exception vectors and configure interrupt delivery (GIC + timer).
 * - Initialize core kernel services such as IPC, VFS, networking, and the
 *   scheduler/task subsystem.
 *
 * The entry point finishes by starting the scheduler, which is expected to
 * run indefinitely and therefore never returns to the boot stub.
 */

#include "arch/aarch64/cpu.hpp"
#include "arch/aarch64/exceptions.hpp"
#include "arch/aarch64/gic.hpp"
#include "arch/aarch64/mmu.hpp"
#include "arch/aarch64/timer.hpp"
#include "assign/assign.hpp"
#include "boot/bootinfo.hpp"
#include "cap/table.hpp"
#include "console/console.hpp"
#include "console/gcon.hpp"
#include "console/serial.hpp"
#include "drivers/fwcfg.hpp"
#include "drivers/ramfb.hpp"
#include "drivers/virtio/blk.hpp"
#include "drivers/virtio/gpu.hpp"
#include "drivers/virtio/input.hpp"
#include "drivers/virtio/rng.hpp"
#include "drivers/virtio/virtio.hpp"
#include "fs/cache.hpp"
#include "fs/vfs/vfs.hpp"
#include "fs/viperfs/viperfs.hpp"
#include "include/config.hpp"
#include "include/constants.hpp"
#include "include/syscall.hpp"
#include "include/vboot.hpp"
#include "input/input.hpp"
#include "ipc/channel.hpp"
#include "ipc/poll.hpp"
#include "ipc/pollset.hpp"
#include "kobj/blob.hpp"
#include "kobj/channel.hpp"
#include "loader/loader.hpp"
#include "mm/kheap.hpp"
#include "mm/pmm.hpp"
#include "mm/slab.hpp"
#include "mm/vmm.hpp"
#include "sched/scheduler.hpp"
#include "sched/task.hpp"
#include "tests/tests.hpp"
#include "types.hpp"
#include "viper/address_space.hpp"
#include "viper/viper.hpp"


// Linker-provided symbols
extern "C"
{
    extern u8 __kernel_end[];
}

// C++ runtime support - needed for global constructors
extern "C"
{
    // These are called by GCC for global object construction/destruction
    /**
     * @brief Handler for "pure virtual function call" runtime errors.
     *
     * @details
     * The C++ runtime calls `__cxa_pure_virtual` if a pure virtual function is
     * invoked (typically due to calling a virtual method from a constructor or
     * destructor before the most-derived object is fully formed, or due to
     * corruption/use-after-free).
     *
     * In a kernel environment this is always a fatal programming error. The
     * implementation prints a panic message to the serial console and halts the
     * CPU in an infinite low-power wait loop.
     */
    void __cxa_pure_virtual()
    {
        // Pure virtual function called - should never happen
        serial::puts("PANIC: Pure virtual function called!\n");
        for (;;)
            asm volatile("wfi");
    }
}

// =============================================================================
// SUBSYSTEM INITIALIZATION HELPERS
// =============================================================================

namespace
{

/**
 * @brief Print the boot banner to serial console.
 */
void print_boot_banner()
{
    serial::puts("\n");
    serial::puts("=========================================\n");
    serial::puts("  ViperOS v0.2.0 - AArch64\n");
    serial::puts("  Mode: ");
#if VIPER_MICROKERNEL_MODE
    serial::puts("MICROKERNEL (bring-up)\n");
#else
    serial::puts("HYBRID\n");
#endif
    serial::puts("  Kernel services: fs=");
    serial::put_dec(static_cast<u64>(VIPER_KERNEL_ENABLE_FS));
    serial::puts(" net=");
    serial::put_dec(static_cast<u64>(VIPER_KERNEL_ENABLE_NET));
    serial::puts(" tls=");
    serial::put_dec(static_cast<u64>(VIPER_KERNEL_ENABLE_TLS));
    serial::puts("\n");
    serial::puts("=========================================\n");
    serial::puts("\n");
}

/**
 * @brief Initialize memory management subsystems (PMM, VMM, heap, slab).
 */
void init_memory_subsystem()
{
    serial::puts("\n[kernel] Initializing memory management...\n");

    // Get RAM region from boot info (supports both UEFI and QEMU direct boot)
    u64 ram_base = kc::mem::RAM_BASE;
    u64 ram_size = kc::mem::RAM_SIZE;

    if (boot::get_ram_region(ram_base, ram_size))
    {
        serial::puts("[kernel] Using boot info RAM region: ");
        serial::put_hex(ram_base);
        serial::puts(" - ");
        serial::put_hex(ram_base + ram_size);
        serial::puts(" (");
        serial::put_dec(ram_size / (1024 * 1024));
        serial::puts(" MB)\n");
    }
    else
    {
        serial::puts("[kernel] Using default RAM region (128 MB)\n");
    }

    // Get framebuffer location from boot info
    u64 fb_base = 0;
    u64 fb_size = 0;

    if (boot::has_uefi_framebuffer())
    {
        const auto &fb = boot::get_framebuffer();
        fb_base = fb.base;
        // Calculate framebuffer size: pitch * height, rounded up to 8MB for safety
        u64 fb_actual = static_cast<u64>(fb.pitch) * fb.height;
        fb_size = (fb_actual + (8 * 1024 * 1024) - 1) & ~((8 * 1024 * 1024) - 1);
        if (fb_size < 8 * 1024 * 1024)
            fb_size = 8 * 1024 * 1024;

        serial::puts("[kernel] UEFI framebuffer at ");
        serial::put_hex(fb_base);
        serial::puts(", reserving ");
        serial::put_dec(fb_size / (1024 * 1024));
        serial::puts(" MB\n");
    }

    // Initialize physical memory manager
    pmm::init(ram_base, ram_size, reinterpret_cast<u64>(__kernel_end), fb_base, fb_size);

    // Initialize virtual memory manager
    vmm::init();

    // Initialize kernel heap
    kheap::init();

    // Test allocation
    serial::puts("[kernel] Testing heap allocation...\n");
    void *test1 = kheap::kmalloc(1024);
    void *test2 = kheap::kmalloc(4096);
    serial::puts("[kernel] Allocated 1KB at ");
    serial::put_hex(reinterpret_cast<u64>(test1));
    serial::puts("\n");
    serial::puts("[kernel] Allocated 4KB at ");
    serial::put_hex(reinterpret_cast<u64>(test2));
    serial::puts("\n");

    // Initialize slab allocator
    slab::init();
    slab::init_object_caches();

    // Update graphics console with memory info
    if (gcon::is_available())
    {
        gcon::puts("  [OK] Physical memory manager initialized\n");
        gcon::puts("  [OK] Virtual memory manager initialized\n");
        gcon::puts("  [OK] Kernel heap initialized\n");
        gcon::puts("  [OK] Slab allocator initialized\n");
    }
}

/**
 * @brief Initialize exception handlers, GIC, timer, and enable interrupts.
 */
void init_interrupts()
{
    serial::puts("\n[kernel] Initializing exceptions and interrupts...\n");
    exceptions::init();
    gic::init();

    // Initialize timer
    timer::init();

    // Initialize CPU subsystem (per-CPU data structures)
    cpu::init();

    // Enable interrupts
    exceptions::enable_interrupts();
    serial::puts("[kernel] Interrupts enabled\n");

    // Update graphics console
    if (gcon::is_available())
    {
        gcon::puts("  [OK] Exception handlers installed\n");
        gcon::puts("  [OK] GIC initialized\n");
        gcon::puts("  [OK] Timer started (1000 Hz)\n");
        gcon::puts("  [OK] Interrupts enabled\n");
        gcon::puts("\n");

        // Show memory stats
        gcon::set_colors(gcon::colors::VIPER_YELLOW, gcon::colors::VIPER_DARK_BROWN);
        gcon::puts("  Memory: ");
        u64 free_mb = (pmm::get_free_pages() * 4) / 1024;
        if (free_mb >= 100)
            gcon::putc('0' + (free_mb / 100) % 10);
        if (free_mb >= 10)
            gcon::putc('0' + (free_mb / 10) % 10);
        gcon::putc('0' + free_mb % 10);
        gcon::puts(" MB free\n");
    }
}

/**
 * @brief Initialize task, scheduler, channel, and poll subsystems.
 */
void init_task_subsystem()
{
    serial::puts("\n[kernel] Initializing task subsystem...\n");
    task::init();
    scheduler::init();

    serial::puts("\n[kernel] Initializing channel subsystem...\n");
    channel::init();

    serial::puts("\n[kernel] Initializing poll subsystem...\n");
    poll::init();
    pollset::init();

    // Test poll functionality
    poll::test_poll();
    pollset::test_pollset();
}

/**
 * @brief Initialize virtio subsystem and device drivers.
 */
void init_virtio_subsystem()
{
    serial::puts("\n[kernel] Initializing virtio subsystem...\n");
    virtio::init();

    // Initialize virtio-rng driver (entropy source for TLS)
    if (!virtio::rng::init())
    {
        serial::puts("[kernel] WARNING: virtio-rng not available (TCP ISN will use fallback)\n");
    }

    // Initialize virtio-blk driver
    virtio::blk_init();

    // Initialize virtio-gpu driver
    virtio::gpu_init();

    // Initialize virtio-input driver
    virtio::input_init();

    // Initialize input subsystem
    input::init();

    // Initialize console input buffer
    console::init_input();
}

#if VIPER_KERNEL_ENABLE_NET
/**
 * @brief Initialize network stack and run connectivity tests.
 */
void init_network_subsystem()
{
    // Initialize virtio-net driver
    virtio::net_init();

    // Initialize network stack
    net::network_init();

    // Give QEMU's network stack time to initialize
    if (virtio::net_device())
    {
        u64 start = timer::get_ticks();
        while (timer::get_ticks() - start < 500)
        {
            net::network_poll();
            asm volatile("wfi");
        }
    }

    // Test ping
    if (virtio::net_device())
    {
        serial::puts("[kernel] Testing ping to gateway (10.0.2.2)...\n");
        net::Ipv4Addr gateway = {{10, 0, 2, 2}};
        i32 rtt = net::icmp::ping(gateway, 3000); // 3 second timeout
        if (rtt >= 0)
        {
            serial::puts("[kernel] Ping successful! RTT: ");
            serial::put_dec(rtt);
            serial::puts(" ms\n");
        }
        else
        {
            serial::puts("[kernel] Ping failed (code ");
            serial::put_dec(-rtt);
            serial::puts(")\n");
        }

        // Test DNS resolution
        serial::puts("[kernel] Testing DNS resolution (example.com)...\n");
        net::Ipv4Addr resolved_ip;
        if (net::dns::resolve("example.com", &resolved_ip, 5000))
        {
            serial::puts("[kernel] DNS resolved: ");
            serial::put_dec(resolved_ip.bytes[0]);
            serial::putc('.');
            serial::put_dec(resolved_ip.bytes[1]);
            serial::putc('.');
            serial::put_dec(resolved_ip.bytes[2]);
            serial::putc('.');
            serial::put_dec(resolved_ip.bytes[3]);
            serial::puts("\n");

            // Test HTTP fetch
            serial::puts("[kernel] Testing HTTP fetch...\n");
            net::http::fetch("example.com", "/");
        }
        else
        {
            serial::puts("[kernel] DNS resolution failed\n");
        }
    }
}
#endif

} // anonymous namespace

// =============================================================================
// KERNEL ENTRY POINT
// =============================================================================

/**
 * @brief Kernel main entry point invoked from the assembly boot stub.
 *
 * @details
 * This function performs the kernel's early initialization sequence.
 *
 * The `boot_info` pointer is an opaque, boot-environment-provided value. Its
 * exact type depends on how the kernel was started:
 * - When launched directly by QEMU, it may be a device-tree blob (DTB) pointer.
 * - When launched via ViperOS' boot stub, it may be a pointer to a VBootInfo
 *   structure describing boot services and memory layout.
 *
 * The current bring-up code uses the pointer primarily for diagnostics and may
 * ignore it for fixed QEMU `virt` assumptions.
 *
 * On successful initialization the function starts the scheduler, which is
 * expected not to return.
 *
 * @param boot_info Boot environment information pointer (DTB or VBootInfo).
 */
extern "C" void kernel_main(void *boot_info_ptr)
{
    // Initialize serial output first
    serial::init();

    // Print boot banner
    print_boot_banner();

    // Initialize boot info parser
    boot::init(boot_info_ptr);

    // Dump boot info for debugging
    boot::dump();
    serial::puts("\n");

    // Initialize framebuffer
    bool fb_initialized = false;

    if (boot::has_uefi_framebuffer())
    {
        // Use GOP framebuffer from UEFI
        const auto &fb = boot::get_framebuffer();
        serial::puts("[kernel] Using UEFI GOP framebuffer\n");

        // Initialize ramfb module with external framebuffer info
        if (ramfb::init_external(fb.base, fb.width, fb.height, fb.pitch, fb.bpp))
        {
            serial::puts("[kernel] Framebuffer initialized (UEFI GOP)\n");
            fb_initialized = true;
        }
    }

    if (!fb_initialized)
    {
        // Initialize fw_cfg interface for QEMU
        fwcfg::init();

        // Initialize framebuffer via ramfb with default resolution
        if (ramfb::init(kc::display::DEFAULT_WIDTH, kc::display::DEFAULT_HEIGHT))
        {
            serial::puts("[kernel] Framebuffer initialized (ramfb)\n");
            fb_initialized = true;
        }
    }

    if (fb_initialized)
    {
        // Initialize graphics console
        if (gcon::init())
        {
            serial::puts("[kernel] Graphics console initialized\n");

            // Display boot banner on graphics console
            gcon::puts("\n");
            gcon::puts("  =========================================\n");
            gcon::puts("    __     ___                  ___  ____  \n");
            gcon::puts("    \\ \\   / (_)_ __   ___ _ __ / _ \\/ ___| \n");
            gcon::puts("     \\ \\ / /| | '_ \\ / _ \\ '__| | | \\___ \\ \n");
            gcon::puts("      \\ V / | | |_) |  __/ |  | |_| |___) |\n");
            gcon::puts("       \\_/  |_| .__/ \\___|_|   \\___/|____/ \n");
            gcon::puts("              |_|                          \n");
            gcon::puts("  =========================================\n");
            gcon::puts("\n");

            // Print version info
            gcon::set_colors(gcon::colors::VIPER_YELLOW, gcon::colors::VIPER_DARK_BROWN);
            gcon::puts("  Version: 0.1.0\n");
            gcon::puts("  Architecture: AArch64\n");
            gcon::puts("\n");

            // Print main message
            gcon::set_colors(gcon::colors::VIPER_WHITE, gcon::colors::VIPER_DARK_BROWN);
            gcon::puts("  Hello from ViperOS!\n");
            gcon::puts("\n");

            // Print status
            gcon::set_colors(gcon::colors::VIPER_GREEN, gcon::colors::VIPER_DARK_BROWN);
            gcon::puts("  [OK] Serial console initialized\n");
            gcon::puts("  [OK] fw_cfg interface detected\n");
            gcon::puts("  [OK] Framebuffer initialized (1024x768)\n");
            gcon::puts("  [OK] Graphics console initialized\n");
            gcon::puts("\n");

            // Print dimensions
            u32 cols, rows;
            gcon::get_size(cols, rows);
            gcon::puts("  Console size: ");
            // Simple number printing
            if (cols >= 100)
                gcon::putc('0' + (cols / 100) % 10);
            if (cols >= 10)
                gcon::putc('0' + (cols / 10) % 10);
            gcon::putc('0' + cols % 10);
            gcon::puts(" x ");
            if (rows >= 10)
                gcon::putc('0' + (rows / 10) % 10);
            gcon::putc('0' + rows % 10);
            gcon::puts(" characters\n");
            gcon::puts("\n");

            gcon::set_colors(gcon::colors::VIPER_WHITE, gcon::colors::VIPER_DARK_BROWN);
            gcon::puts("  Kernel initialization complete.\n");
            gcon::puts("  System halted.\n");
        }
        else
        {
            serial::puts("[kernel] Failed to initialize graphics console\n");
        }
    }
    else
    {
        serial::puts("[kernel] Running in serial-only mode\n");
    }

    // Initialize core kernel subsystems
    init_memory_subsystem();
    init_interrupts();
    init_task_subsystem();
    init_virtio_subsystem();

#if VIPER_KERNEL_ENABLE_NET
    init_network_subsystem();
#else
    serial::puts("[kernel] Kernel networking disabled (VIPER_KERNEL_ENABLE_NET=0)\n");
#endif

    if (virtio::blk_device())
    {
        serial::puts("[kernel] Block device ready: ");
        serial::put_dec(virtio::blk_device()->size_bytes() / (1024 * 1024));
        serial::puts(" MB\n");

        // Test: Read sector 0
        serial::puts("[kernel] Testing block read (sector 0)...\n");
        u8 sector_buf[512];
        if (virtio::blk_device()->read_sectors(0, 1, sector_buf) == 0)
        {
            serial::puts("[kernel] Read sector 0 OK!\n");
        }
        else
        {
            serial::puts("[kernel] Read sector 0 FAILED\n");
        }

        // Test: Write and read back
        serial::puts("[kernel] Testing block write (sector 1)...\n");
        for (int i = 0; i < 512; i++)
        {
            sector_buf[i] = static_cast<u8>(i & 0xFF);
        }
        sector_buf[0] = 'V';
        sector_buf[1] = 'i';
        sector_buf[2] = 'p';
        sector_buf[3] = 'e';
        sector_buf[4] = 'r';

        if (virtio::blk_device()->write_sectors(1, 1, sector_buf) == 0)
        {
            serial::puts("[kernel] Write sector 1 OK\n");

            // Read it back
            u8 read_buf[512];
            for (int i = 0; i < 512; i++)
                read_buf[i] = 0;
            if (virtio::blk_device()->read_sectors(1, 1, read_buf) == 0)
            {
                if (read_buf[0] == 'V' && read_buf[1] == 'i')
                {
                    serial::puts("[kernel] Read-back verified: ");
                    for (int i = 0; i < 5; i++)
                    {
                        serial::putc(read_buf[i]);
                    }
                    serial::puts("\n");
                }
                else
                {
                    serial::puts("[kernel] Read-back MISMATCH\n");
                }
            }
        }
        else
        {
            serial::puts("[kernel] Write sector 1 FAILED\n");
        }

        // Initialize block cache
        serial::puts("[kernel] Initializing block cache...\n");
        fs::cache_init();

        // Test block cache
        serial::puts("[kernel] Testing block cache...\n");
        fs::CacheBlock *blk0 = fs::cache().get(0);
        if (blk0)
        {
            serial::puts("[kernel] Cache block 0 OK, first bytes: ");
            for (int i = 0; i < 4; i++)
            {
                serial::put_hex(blk0->data[i]);
                serial::puts(" ");
            }
            serial::puts("\n");

            // Get same block again (should be cache hit)
            fs::CacheBlock *blk0_again = fs::cache().get(0);
            if (blk0_again == blk0)
            {
                serial::puts("[kernel] Cache hit OK (same block returned)\n");
            }
            fs::cache().release(blk0_again);
            fs::cache().release(blk0);

            serial::puts("[kernel] Cache stats: hits=");
            serial::put_dec(fs::cache().hits());
            serial::puts(", misses=");
            serial::put_dec(fs::cache().misses());
            serial::puts("\n");
        }

        // Initialize ViperFS
        serial::puts("[kernel] Initializing ViperFS...\n");
        if (fs::viperfs::viperfs_init())
        {
            serial::puts("[kernel] ViperFS mounted: ");
            serial::puts(fs::viperfs::viperfs().label());
            serial::puts("\n");

            // Read root directory
            serial::puts("[kernel] Reading root directory...\n");
            fs::viperfs::Inode *root = fs::viperfs::viperfs().read_inode(fs::viperfs::ROOT_INODE);
            if (root)
            {
                serial::puts("[kernel] Root inode: size=");
                serial::put_dec(root->size);
                serial::puts(", mode=");
                serial::put_hex(root->mode);
                serial::puts("\n");

                // List directory entries
                serial::puts("[kernel] Directory contents:\n");
                fs::viperfs::viperfs().readdir(
                    root,
                    0,
                    [](const char *name, usize name_len, u64 ino, u8 type, void *)
                    {
                        serial::puts("  ");
                        for (usize i = 0; i < name_len; i++)
                        {
                            serial::putc(name[i]);
                        }
                        serial::puts(" (inode ");
                        serial::put_dec(ino);
                        serial::puts(", type ");
                        serial::put_dec(type);
                        serial::puts(")\n");
                    },
                    nullptr);

                // Look for hello.txt
                u64 hello_ino = fs::viperfs::viperfs().lookup(root, "hello.txt", 9);
                if (hello_ino != 0)
                {
                    serial::puts("[kernel] Found hello.txt at inode ");
                    serial::put_dec(hello_ino);
                    serial::puts("\n");

                    // Read file contents
                    fs::viperfs::Inode *hello = fs::viperfs::viperfs().read_inode(hello_ino);
                    if (hello)
                    {
                        char buf[256] = {};
                        i64 bytes =
                            fs::viperfs::viperfs().read_data(hello, 0, buf, sizeof(buf) - 1);
                        if (bytes > 0)
                        {
                            serial::puts("[kernel] hello.txt contents: ");
                            serial::puts(buf);
                        }
                        fs::viperfs::viperfs().release_inode(hello);
                    }
                }
                else
                {
                    serial::puts("[kernel] hello.txt not found\n");
                }

                // Test file creation and writing
                serial::puts("[kernel] Testing file creation...\n");
                u64 test_ino = fs::viperfs::viperfs().create_file(root, "test.txt", 8);
                if (test_ino != 0)
                {
                    serial::puts("[kernel] Created test.txt at inode ");
                    serial::put_dec(test_ino);
                    serial::puts("\n");

                    fs::viperfs::Inode *test_file = fs::viperfs::viperfs().read_inode(test_ino);
                    if (test_file)
                    {
                        const char *test_data = "Written by ViperOS kernel!";
                        i64 written =
                            fs::viperfs::viperfs().write_data(test_file, 0, test_data, 27);
                        serial::puts("[kernel] Wrote ");
                        serial::put_dec(written);
                        serial::puts(" bytes\n");

                        fs::viperfs::viperfs().write_inode(test_file);

                        // Read it back
                        char verify[64] = {};
                        i64 read_back = fs::viperfs::viperfs().read_data(
                            test_file, 0, verify, sizeof(verify) - 1);
                        if (read_back > 0)
                        {
                            serial::puts("[kernel] Read back: ");
                            serial::puts(verify);
                            serial::puts("\n");
                        }

                        fs::viperfs::viperfs().release_inode(test_file);
                    }

                    // List directory again to see new file
                    serial::puts("[kernel] Updated directory contents:\n");
                    fs::viperfs::viperfs().readdir(
                        root,
                        0,
                        [](const char *name, usize name_len, u64 ino, u8, void *)
                        {
                            serial::puts("  ");
                            for (usize i = 0; i < name_len; i++)
                            {
                                serial::putc(name[i]);
                            }
                            serial::puts(" (inode ");
                            serial::put_dec(ino);
                            serial::puts(")\n");
                        },
                        nullptr);

                    // Sync filesystem
                    fs::viperfs::viperfs().sync();
                    serial::puts("[kernel] Filesystem synced\n");
                }

                fs::viperfs::viperfs().release_inode(root);

                // Initialize VFS
                serial::puts("[kernel] Initializing VFS...\n");
                fs::vfs::init();

                // Test VFS operations
                serial::puts("[kernel] Testing VFS operations...\n");
                i32 fd = fs::vfs::open("/hello.txt", fs::vfs::flags::O_RDONLY);
                if (fd >= 0)
                {
                    serial::puts("[kernel] Opened /hello.txt as fd ");
                    serial::put_dec(fd);
                    serial::puts("\n");

                    char buf[64] = {};
                    i64 bytes = fs::vfs::read(fd, buf, sizeof(buf) - 1);
                    if (bytes > 0)
                    {
                        serial::puts("[kernel] Read via VFS: ");
                        serial::puts(buf);
                    }

                    fs::vfs::close(fd);
                    serial::puts("[kernel] Closed fd\n");
                }
                else
                {
                    serial::puts("[kernel] VFS open failed\n");
                }

                // Test creating a file via VFS
                fd = fs::vfs::open("/vfs_test.txt",
                                   fs::vfs::flags::O_RDWR | fs::vfs::flags::O_CREAT);
                if (fd >= 0)
                {
                    serial::puts("[kernel] Created /vfs_test.txt as fd ");
                    serial::put_dec(fd);
                    serial::puts("\n");

                    const char *data = "Created via VFS!";
                    i64 written = fs::vfs::write(fd, data, 16);
                    serial::puts("[kernel] VFS wrote ");
                    serial::put_dec(written);
                    serial::puts(" bytes\n");

                    // Seek back and read
                    fs::vfs::lseek(fd, 0, fs::vfs::seek::SET);
                    char buf[32] = {};
                    i64 rd = fs::vfs::read(fd, buf, sizeof(buf) - 1);
                    if (rd > 0)
                    {
                        serial::puts("[kernel] VFS read back: ");
                        serial::puts(buf);
                        serial::puts("\n");
                    }

                    fs::vfs::close(fd);
                }

                // Sync again
                fs::viperfs::viperfs().sync();

                // Initialize Assign system (v0.2.0)
                serial::puts("[kernel] Initializing Assign system...\n");
                viper::assign::init();

                // Set up standard assigns (C:, S:, L:, T:, CERTS:)
                viper::assign::setup_standard_assigns();

                viper::assign::debug_dump();

                // Test assign inode resolution (using get_inode, not resolve_path
                // since resolve_path requires a Viper context with cap table)
                serial::puts("[kernel] Testing assign inode resolution...\n");

                // Test 1: SYS assign exists and points to root
                u64 sys_inode = viper::assign::get_inode("SYS");
                serial::puts("  SYS -> inode ");
                serial::put_dec(sys_inode);
                serial::puts(sys_inode != 0 ? " OK\n" : " FAIL\n");

                // Test 2: D0 assign exists
                u64 d0_inode = viper::assign::get_inode("D0");
                serial::puts("  D0 -> inode ");
                serial::put_dec(d0_inode);
                serial::puts(d0_inode != 0 ? " OK\n" : " FAIL\n");

                // Test 3: Verify vinit.sys exists via VFS (two-disk: /sys mount)
                i32 vinit_fd = fs::vfs::open("/sys/vinit.sys", fs::vfs::flags::O_RDONLY);
                serial::puts("  /sys/vinit.sys -> ");
                if (vinit_fd >= 0)
                {
                    serial::puts("fd ");
                    serial::put_dec(vinit_fd);
                    serial::puts(" OK\n");
                    fs::vfs::close(vinit_fd);
                }
                else
                {
                    serial::puts("FAIL (not found)\n");
                }

                // Test 4: Nonexistent assign should return 0
                u64 bad_inode = viper::assign::get_inode("NONEXISTENT");
                serial::puts("  NONEXISTENT -> ");
                serial::puts(bad_inode == 0 ? "0 (expected)\n" : "FAIL\n");

                // Note: resolve_path() is tested in user-space where Viper context exists
            }
        }
        else
        {
            serial::puts("[kernel] ViperFS mount failed\n");
        }
    }

    if (gcon::is_available())
    {
        gcon::puts("  [OK] virtio subsystem initialized (");
        u32 count = virtio::device_count();
        if (count >= 10)
            gcon::putc('0' + (count / 10) % 10);
        gcon::putc('0' + count % 10);
        gcon::puts(" devices)\n");
        if (virtio::blk_device())
        {
            gcon::puts("  [OK] virtio-blk driver ready\n");
        }
    }

    // Configure MMU for user space support (Phase 3)
    serial::puts("\n[kernel] Configuring MMU for user space...\n");
    mmu::init();

    // Initialize Viper subsystem (Phase 3)
    serial::puts("\n[kernel] Initializing Viper subsystem...\n");
    viper::init();

    // Run kernel subsystem tests BEFORE loading user processes
    // (Tests must complete before any user tasks are enqueued to avoid
    // scheduler yields during tests switching to user tasks)
    tests::run_storage_tests();

    // Sync filesystem after storage tests to ensure all changes
    // (including unlinked inodes) are persisted to disk
    if (fs::viperfs::viperfs().is_mounted())
    {
        fs::viperfs::viperfs().sync();
        serial::puts("[kernel] Filesystem synced after storage tests\n");
    }

    tests::run_viper_tests();
    tests::run_syscall_tests();
    tests::create_ipc_test_tasks();
    // Note: userfault tests are run via QEMU test infrastructure, not at boot
    // tests::create_userfault_test_task();

    // Test Viper creation
    serial::puts("[kernel] Testing Viper creation...\n");
    viper::Viper *test_viper = viper::create(nullptr, "test_viper");
    if (test_viper)
    {
        viper::print_info(test_viper);

        // Test address space mapping
        viper::AddressSpace *as = viper::get_address_space(test_viper);
        if (as && as->is_valid())
        {
            // Map a test page at heap base
            u64 test_vaddr = viper::layout::USER_HEAP_BASE;
            u64 test_page = as->alloc_map(test_vaddr, 4096, viper::prot::RW);
            if (test_page)
            {
                serial::puts("[kernel] Mapped test page at ");
                serial::put_hex(test_vaddr);
                serial::puts("\n");

                // Verify translation
                u64 phys = as->translate(test_vaddr);
                serial::puts("[kernel] Translates to physical ");
                serial::put_hex(phys);
                serial::puts("\n");

                // Unmap it
                as->unmap(test_vaddr, 4096);
                serial::puts("[kernel] Unmapped test page\n");
            }
        }

        // Test capability table
        cap::Table *ct = test_viper->cap_table;
        if (ct)
        {
            serial::puts("[kernel] Testing capability table...\n");

            // Insert a test capability
            int dummy_object = 42;
            cap::Handle h1 = ct->insert(&dummy_object, cap::Kind::Blob, cap::CAP_RW);
            if (h1 != cap::HANDLE_INVALID)
            {
                serial::puts("[kernel] Inserted handle ");
                serial::put_hex(h1);
                serial::puts(" (index=");
                serial::put_dec(cap::handle_index(h1));
                serial::puts(", gen=");
                serial::put_dec(cap::handle_gen(h1));
                serial::puts(")\n");

                // Look it up
                cap::Entry *e = ct->get(h1);
                if (e && e->object == &dummy_object)
                {
                    serial::puts("[kernel] Handle lookup OK\n");
                }

                // Derive with reduced rights
                cap::Handle h2 = ct->derive(h1, cap::CAP_READ);
                if (h2 == cap::HANDLE_INVALID)
                {
                    serial::puts("[kernel] Derive failed (expected - no CAP_DERIVE)\n");
                }

                // Insert with DERIVE right
                cap::Handle h3 =
                    ct->insert(&dummy_object,
                               cap::Kind::Blob,
                               static_cast<cap::Rights>(cap::CAP_RW | cap::CAP_DERIVE));
                cap::Handle h4 = ct->derive(h3, cap::CAP_READ);
                if (h4 != cap::HANDLE_INVALID)
                {
                    serial::puts("[kernel] Derived handle ");
                    serial::put_hex(h4);
                    serial::puts(" with CAP_READ only\n");
                }

                // Remove and check generation
                ct->remove(h1);
                cap::Entry *e2 = ct->get(h1);
                if (!e2)
                {
                    serial::puts("[kernel] Handle correctly invalidated after remove\n");
                }

                serial::puts("[kernel] Capability table: ");
                serial::put_dec(ct->count());
                serial::puts("/");
                serial::put_dec(ct->capacity());
                serial::puts(" slots used\n");

                // Test blob object
                serial::puts("[kernel] Testing KObj blob...\n");
                kobj::Blob *blob = kobj::Blob::create(4096);
                if (blob)
                {
                    cap::Handle blob_h = ct->insert(blob, cap::Kind::Blob, cap::CAP_RW);
                    if (blob_h != cap::HANDLE_INVALID)
                    {
                        serial::puts("[kernel] Blob handle: ");
                        serial::put_hex(blob_h);
                        serial::puts(", size=");
                        serial::put_dec(blob->size());
                        serial::puts(", phys=");
                        serial::put_hex(blob->phys());
                        serial::puts("\n");

                        // Write something to blob
                        u32 *data = static_cast<u32 *>(blob->data());
                        data[0] = 0xDEADBEEF;
                        serial::puts("[kernel] Wrote 0xDEADBEEF to blob\n");
                    }

                    // Test kobj::Channel
                    kobj::Channel *kch = kobj::Channel::create();
                    if (kch)
                    {
                        cap::Handle ch_h = ct->insert(kch, cap::Kind::Channel, cap::CAP_RW);
                        serial::puts("[kernel] KObj channel handle: ");
                        serial::put_hex(ch_h);
                        serial::puts(", channel_id=");
                        serial::put_dec(kch->id());
                        serial::puts("\n");
                    }
                }
            }
        }

        // Test sbrk syscall
        serial::puts("[kernel] Testing sbrk...\n");
        {
            u64 initial_break = test_viper->heap_break;
            serial::puts("[kernel]   Initial heap break: ");
            serial::put_hex(initial_break);
            serial::puts("\n");

            // Test sbrk(0) - should return current break
            i64 result = viper::do_sbrk(test_viper, 0);
            if (result == static_cast<i64>(initial_break))
            {
                serial::puts("[kernel]   sbrk(0) returned correct break\n");
            }
            else
            {
                serial::puts("[kernel]   ERROR: sbrk(0) returned wrong value\n");
            }

            // Test allocating 4KB (one page)
            result = viper::do_sbrk(test_viper, 4096);
            if (result == static_cast<i64>(initial_break))
            {
                serial::puts("[kernel]   sbrk(4096) returned old break\n");
                serial::puts("[kernel]   New heap break: ");
                serial::put_hex(test_viper->heap_break);
                serial::puts("\n");

                // Verify page was actually mapped by getting address space
                viper::AddressSpace *as = viper::get_address_space(test_viper);
                if (as)
                {
                    u64 phys = as->translate(initial_break);
                    if (phys != 0)
                    {
                        serial::puts("[kernel]   Heap page mapped to phys: ");
                        serial::put_hex(phys);
                        serial::puts("\n");

                        // Write and read to verify it works
                        u32 *ptr = static_cast<u32 *>(pmm::phys_to_virt(phys));
                        ptr[0] = 0xCAFEBABE;
                        if (ptr[0] == 0xCAFEBABE)
                        {
                            serial::puts("[kernel]   Heap memory R/W test PASSED\n");
                        }
                        else
                        {
                            serial::puts("[kernel]   ERROR: Heap memory R/W test FAILED\n");
                        }
                    }
                    else
                    {
                        serial::puts("[kernel]   ERROR: Heap page not mapped!\n");
                    }
                }
            }
            else
            {
                serial::puts("[kernel]   ERROR: sbrk(4096) failed with ");
                serial::put_dec(result);
                serial::puts("\n");
            }

            // Allocate more to cross page boundary
            result = viper::do_sbrk(test_viper, 8192);
            if (result > 0)
            {
                serial::puts("[kernel]   sbrk(8192) succeeded, new break: ");
                serial::put_hex(test_viper->heap_break);
                serial::puts("\n");
            }

            serial::puts("[kernel] sbrk test complete\n");
        }

        // Load vinit from disk (two-disk architecture: system disk at /sys)
        serial::puts("[kernel] Loading vinit from disk...\n");

        loader::LoadResult load_result = loader::load_elf_from_disk(test_viper, "/sys/vinit.sys");
        if (load_result.success)
        {
            serial::puts("[kernel] vinit loaded successfully\n");

            // Map user stack
            u64 stack_base = viper::layout::USER_STACK_TOP - viper::layout::USER_STACK_SIZE;
            u64 stack_page =
                as->alloc_map(stack_base, viper::layout::USER_STACK_SIZE, viper::prot::RW);
            if (stack_page)
            {
                serial::puts("[kernel] User stack mapped at ");
                serial::put_hex(stack_base);
                serial::puts(" - ");
                serial::put_hex(viper::layout::USER_STACK_TOP);
                serial::puts("\n");

#ifdef VIPEROS_DIRECT_USER_MODE
                // Direct mode: Jump to user mode immediately (for debugging)
                // This bypasses the scheduler - not recommended for production
                serial::puts("[kernel] DIRECT MODE: Entering user mode without scheduler\n");
                viper::switch_address_space(test_viper->ttbr0, test_viper->asid);
                asm volatile("tlbi aside1is, %0" ::"r"(static_cast<u64>(test_viper->asid) << 48));
                asm volatile("dsb sy");
                asm volatile("isb");
                viper::set_current(test_viper);
                enter_user_mode(load_result.entry_point, viper::layout::USER_STACK_TOP, 0);
                // Never reaches here
#else
                // Scheduled mode: Create a user task that will enter user mode
                // when scheduled. This is the proper way to run user processes.
                task::Task *vinit_task = task::create_user_task(
                    "vinit", test_viper, load_result.entry_point, viper::layout::USER_STACK_TOP);

                if (vinit_task)
                {
                    serial::puts("[kernel] vinit task created, will run under scheduler\n");
                    scheduler::enqueue(vinit_task);
                }
                else
                {
                    serial::puts("[kernel] Failed to create vinit task\n");
                    viper::destroy(test_viper);
                }
#endif
            }
            else
            {
                serial::puts("[kernel] Failed to map user stack\n");
                viper::destroy(test_viper);
            }
        }
        else
        {
            serial::puts("[kernel] Failed to load vinit\n");
            viper::destroy(test_viper);
        }
    }
    else
    {
        serial::puts("[kernel] Failed to create test Viper!\n");
    }

    if (gcon::is_available())
    {
        gcon::puts("  [OK] Viper subsystem initialized\n");
        gcon::puts("  [OK] Task subsystem initialized\n");
        gcon::puts("  [OK] Scheduler initialized\n");
        gcon::puts("  [OK] Channel subsystem initialized\n");
        gcon::puts("  [OK] Poll/timer subsystem initialized\n");
    }

    // Print success message
    serial::puts("\nHello from ViperOS!\n");
    serial::puts("Kernel initialization complete.\n");

    // Boot secondary CPUs (they will initialize GIC, timer and enter idle loop)
    cpu::boot_secondaries();

    serial::puts("Starting scheduler...\n");

    if (gcon::is_available())
    {
        gcon::puts("\n");
        gcon::set_colors(gcon::colors::VIPER_WHITE, gcon::colors::VIPER_DARK_BROWN);
        gcon::puts("  Starting scheduler...\n");
    }

    // Start the scheduler - this never returns
    scheduler::start();
}
