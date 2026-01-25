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
 * @brief ViperDOS kernel entry point and early initialization sequence.
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
#include "mm/pressure.hpp"
#include "mm/slab.hpp"
#include "mm/vmm.hpp"
#include "sched/scheduler.hpp"
#include "sched/task.hpp"
#include "tests/tests.hpp"
#include "tty/tty.hpp"
#include "types.hpp"
#include "viper/address_space.hpp"
#include "viper/viper.hpp"

#if VIPER_KERNEL_ENABLE_NET
#include "drivers/virtio/net.hpp"
#include "net/netstack.hpp"
#endif


// Linker-provided symbols
extern "C" {
extern u8 __kernel_end[];
}

// C++ runtime support - needed for global constructors
extern "C" {
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
void __cxa_pure_virtual() {
    // Pure virtual function called - should never happen
    serial::puts("PANIC: Pure virtual function called!\n");
    for (;;)
        asm volatile("wfi");
}
}

// =============================================================================
// SUBSYSTEM INITIALIZATION HELPERS
// =============================================================================

namespace {

/**
 * @brief Print the boot banner to serial console.
 */
void print_boot_banner() {
    serial::puts("\n");
    serial::puts("=========================================\n");
    serial::puts("  ViperDOS v0.3.1 - AArch64\n");
    serial::puts("  Mode: MONOLITHIC\n");
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
void init_memory_subsystem() {
    serial::puts("\n[kernel] Initializing memory management...\n");

    // Get RAM region from boot info (supports both UEFI and QEMU direct boot)
    u64 ram_base = kc::mem::RAM_BASE;
    u64 ram_size = kc::mem::RAM_SIZE;

    if (boot::get_ram_region(ram_base, ram_size)) {
        serial::puts("[kernel] Using boot info RAM region: ");
        serial::put_hex(ram_base);
        serial::puts(" - ");
        serial::put_hex(ram_base + ram_size);
        serial::puts(" (");
        serial::put_dec(ram_size / (1024 * 1024));
        serial::puts(" MB)\n");
    } else {
        serial::puts("[kernel] Using default RAM region (128 MB)\n");
    }

    // Get framebuffer location from boot info
    u64 fb_base = 0;
    u64 fb_size = 0;

    if (boot::has_uefi_framebuffer()) {
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

    // Initialize memory pressure monitor
    mm::pressure::init();

    // Show high-level progress on display
    if (gcon::is_available()) {
        gcon::puts("  Memory...OK\n");
    }
    // Brief pause for readability
    timer::delay_ms(50);
}

/**
 * @brief Initialize exception handlers, GIC, timer, and enable interrupts.
 */
void init_interrupts() {
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

    // Show high-level progress on display
    if (gcon::is_available()) {
        gcon::puts("  Interrupts...OK\n");
    }
    // Brief pause for readability
    timer::delay_ms(50);
}

/**
 * @brief Initialize task, scheduler, channel, and poll subsystems.
 */
void init_task_subsystem() {
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
void init_virtio_subsystem() {
    serial::puts("\n[kernel] Initializing virtio subsystem...\n");
    virtio::init();

    // Initialize virtio-rng driver (entropy source for TLS)
    if (!virtio::rng::init()) {
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

    // Initialize TTY subsystem (kernel input buffer for text-mode apps)
    tty::init();

    // Initialize console input buffer
    console::init_input();
}

#if VIPER_KERNEL_ENABLE_NET
/**
 * @brief Initialize network stack and run connectivity tests.
 */
void init_network_subsystem() {
    // Initialize virtio-net driver
    virtio::net_init();

    // Initialize network stack
    net::network_init();

    // Give QEMU's network stack time to initialize
    if (virtio::net_device()) {
        u64 start = timer::get_ticks();
        while (timer::get_ticks() - start < 500) {
            net::network_poll();
            asm volatile("wfi");
        }
    }

    // Test ping
    if (virtio::net_device()) {
        serial::puts("[kernel] Testing ping to gateway (10.0.2.2)...\n");
        net::Ipv4Addr gateway = {{10, 0, 2, 2}};
        i32 rtt = net::icmp::ping(gateway, 3000); // 3 second timeout
        if (rtt >= 0) {
            serial::puts("[kernel] Ping successful! RTT: ");
            serial::put_dec(rtt);
            serial::puts(" ms\n");
        } else {
            serial::puts("[kernel] Ping failed (code ");
            serial::put_dec(-rtt);
            serial::puts(")\n");
        }

        // Test DNS resolution
        serial::puts("[kernel] Testing DNS resolution (example.com)...\n");
        net::Ipv4Addr resolved_ip;
        if (net::dns::resolve("example.com", &resolved_ip, 5000)) {
            serial::puts("[kernel] DNS resolved: ");
            serial::put_ipv4(resolved_ip.bytes);
            serial::puts("\n");
        } else {
            serial::puts("[kernel] DNS resolution failed\n");
        }
    }
}
#endif

/**
 * @brief Initialize framebuffer (UEFI GOP or ramfb fallback).
 * @return true if framebuffer was initialized.
 */
bool init_framebuffer() {
    bool fb_initialized = false;

    if (boot::has_uefi_framebuffer()) {
        const auto &fb = boot::get_framebuffer();
        serial::puts("[kernel] UEFI GOP framebuffer: ");
        serial::put_dec(fb.width);
        serial::puts("x");
        serial::put_dec(fb.height);
        serial::puts("\n");

        if (fb.width >= kc::display::DEFAULT_WIDTH && fb.height >= kc::display::DEFAULT_HEIGHT) {
            if (ramfb::init_external(fb.base, fb.width, fb.height, fb.pitch, fb.bpp)) {
                serial::puts("[kernel] Framebuffer initialized (UEFI GOP)\n");
                fb_initialized = true;
            }
        } else {
            serial::puts("[kernel] GOP resolution too small, trying ramfb\n");
        }
    }

    if (!fb_initialized) {
        fwcfg::init();
        if (ramfb::init(kc::display::DEFAULT_WIDTH, kc::display::DEFAULT_HEIGHT)) {
            serial::puts("[kernel] Framebuffer initialized (ramfb)\n");
            fb_initialized = true;
        }
    }

    if (fb_initialized && gcon::init()) {
        serial::puts("[kernel] Graphics console initialized\n");
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
        gcon::puts("  Version: 0.2.0 | AArch64\n");
        gcon::puts("\n");
        gcon::puts("  Booting...\n");
        gcon::puts("\n");
    } else if (!fb_initialized) {
        serial::puts("[kernel] Running in serial-only mode\n");
    }

    return fb_initialized;
}

/**
 * @brief Test block device read/write operations.
 */
void test_block_device() {
    serial::puts("[kernel] Block device ready: ");
    serial::put_dec(virtio::blk_device()->size_bytes() / (1024 * 1024));
    serial::puts(" MB\n");

    u8 sector_buf[512];

    // Test read
    serial::puts("[kernel] Testing block read (sector 0)...\n");
    if (virtio::blk_device()->read_sectors(0, 1, sector_buf) == 0)
        serial::puts("[kernel] Read sector 0 OK!\n");
    else
        serial::puts("[kernel] Read sector 0 FAILED\n");

    // Test write and read back
    serial::puts("[kernel] Testing block write (sector 1)...\n");
    for (int i = 0; i < 512; i++)
        sector_buf[i] = static_cast<u8>(i & 0xFF);
    sector_buf[0] = 'V';
    sector_buf[1] = 'i';
    sector_buf[2] = 'p';
    sector_buf[3] = 'e';
    sector_buf[4] = 'r';

    if (virtio::blk_device()->write_sectors(1, 1, sector_buf) == 0) {
        serial::puts("[kernel] Write sector 1 OK\n");
        u8 read_buf[512] = {};
        if (virtio::blk_device()->read_sectors(1, 1, read_buf) == 0 && read_buf[0] == 'V' &&
            read_buf[1] == 'i') {
            serial::puts("[kernel] Read-back verified: ");
            for (int i = 0; i < 5; i++)
                serial::putc(read_buf[i]);
            serial::puts("\n");
        }
    } else {
        serial::puts("[kernel] Write sector 1 FAILED\n");
    }
}

/**
 * @brief Test block cache operations.
 */
void test_block_cache() {
    serial::puts("[kernel] Initializing block cache...\n");
    fs::cache_init();

    serial::puts("[kernel] Testing block cache...\n");
    fs::CacheBlock *blk0 = fs::cache().get(0);
    if (blk0) {
        serial::puts("[kernel] Cache block 0 OK, first bytes: ");
        for (int i = 0; i < 4; i++) {
            serial::put_hex(blk0->data[i]);
            serial::puts(" ");
        }
        serial::puts("\n");

        fs::CacheBlock *blk0_again = fs::cache().get(0);
        if (blk0_again == blk0)
            serial::puts("[kernel] Cache hit OK (same block returned)\n");
        fs::cache().release(blk0_again);
        fs::cache().release(blk0);

        serial::puts("[kernel] Cache stats: hits=");
        serial::put_dec(fs::cache().hits());
        serial::puts(", misses=");
        serial::put_dec(fs::cache().misses());
        serial::puts("\n");
    }
}

/**
 * @brief Test ViperFS root directory and file operations.
 */
void test_viperfs_root(fs::viperfs::Inode *root) {
    serial::puts("[kernel] Root inode: size=");
    serial::put_dec(root->size);
    serial::puts(", mode=");
    serial::put_hex(root->mode);
    serial::puts("\n");

    serial::puts("[kernel] Directory contents:\n");
    fs::viperfs::viperfs().readdir(
        root,
        0,
        [](const char *name, usize name_len, u64 ino, u8 type, void *) {
            serial::puts("  ");
            for (usize i = 0; i < name_len; i++)
                serial::putc(name[i]);
            serial::puts(" (inode ");
            serial::put_dec(ino);
            serial::puts(", type ");
            serial::put_dec(type);
            serial::puts(")\n");
        },
        nullptr);

    // Look for hello.txt
    u64 hello_ino = fs::viperfs::viperfs().lookup(root, "hello.txt", 9);
    if (hello_ino != 0) {
        serial::puts("[kernel] Found hello.txt at inode ");
        serial::put_dec(hello_ino);
        serial::puts("\n");

        fs::viperfs::Inode *hello = fs::viperfs::viperfs().read_inode(hello_ino);
        if (hello) {
            char buf[256] = {};
            i64 bytes = fs::viperfs::viperfs().read_data(hello, 0, buf, sizeof(buf) - 1);
            if (bytes > 0) {
                serial::puts("[kernel] hello.txt contents: ");
                serial::puts(buf);
            }
            fs::viperfs::viperfs().release_inode(hello);
        }
    } else {
        serial::puts("[kernel] hello.txt not found\n");
    }
}

/**
 * @brief Test file creation and writing on ViperFS.
 */
void test_viperfs_write(fs::viperfs::Inode *root) {
    serial::puts("[kernel] Testing file creation...\n");
    u64 test_ino = fs::viperfs::viperfs().create_file(root, "test.txt", 8);
    if (test_ino == 0)
        return;

    serial::puts("[kernel] Created test.txt at inode ");
    serial::put_dec(test_ino);
    serial::puts("\n");

    fs::viperfs::Inode *test_file = fs::viperfs::viperfs().read_inode(test_ino);
    if (test_file) {
        const char *test_data = "Written by ViperDOS kernel!";
        i64 written = fs::viperfs::viperfs().write_data(test_file, 0, test_data, 27);
        serial::puts("[kernel] Wrote ");
        serial::put_dec(written);
        serial::puts(" bytes\n");

        fs::viperfs::viperfs().write_inode(test_file);

        char verify[64] = {};
        i64 read_back = fs::viperfs::viperfs().read_data(test_file, 0, verify, sizeof(verify) - 1);
        if (read_back > 0) {
            serial::puts("[kernel] Read back: ");
            serial::puts(verify);
            serial::puts("\n");
        }
        fs::viperfs::viperfs().release_inode(test_file);
    }

    serial::puts("[kernel] Updated directory contents:\n");
    fs::viperfs::viperfs().readdir(
        root,
        0,
        [](const char *name, usize name_len, u64 ino, u8, void *) {
            serial::puts("  ");
            for (usize i = 0; i < name_len; i++)
                serial::putc(name[i]);
            serial::puts(" (inode ");
            serial::put_dec(ino);
            serial::puts(")\n");
        },
        nullptr);

    fs::viperfs::viperfs().sync();
    serial::puts("[kernel] Filesystem synced\n");
}

/**
 * @brief Initialize user disk and mount user filesystem.
 */
void init_user_disk() {
    serial::puts("[kernel] Initializing user disk...\n");
    virtio::user_blk_init();

    if (!virtio::user_blk_device()) {
        serial::puts("[kernel] User disk not found\n");
        return;
    }

    serial::puts("[kernel] User disk found: ");
    serial::put_dec(virtio::user_blk_device()->size_bytes() / (1024 * 1024));
    serial::puts(" MB\n");

    fs::user_cache_init();
    if (fs::user_cache_available()) {
        if (fs::viperfs::user_viperfs_init()) {
            serial::puts("[kernel] User filesystem mounted: ");
            serial::puts(fs::viperfs::user_viperfs().label());
            serial::puts("\n");
        } else {
            serial::puts("[kernel] User filesystem mount failed\n");
        }
    } else {
        serial::puts("[kernel] User cache init failed\n");
    }
}

/**
 * @brief Test VFS operations (open, read, write).
 */
void test_vfs_operations() {
    serial::puts("[kernel] Testing VFS operations...\n");

    i32 fd = fs::vfs::open("/c/hello.prg", fs::vfs::flags::O_RDONLY);
    if (fd >= 0) {
        serial::puts("[kernel] Opened /c/hello.prg as fd ");
        serial::put_dec(fd);
        serial::puts("\n");

        char buf[8] = {};
        i64 bytes = fs::vfs::read(fd, buf, 4);
        if (bytes > 0) {
            serial::puts("[kernel] Read ELF header: ");
            for (int i = 0; i < 4; i++) {
                serial::put_hex(static_cast<u8>(buf[i]));
                serial::puts(" ");
            }
            serial::puts("\n");
        }
        fs::vfs::close(fd);
        serial::puts("[kernel] Closed fd\n");
    } else {
        serial::puts("[kernel] VFS open /c/hello.prg failed\n");
    }

    fd = fs::vfs::open("/t/t/vfs_test.txt", fs::vfs::flags::O_RDWR | fs::vfs::flags::O_CREAT);
    if (fd >= 0) {
        serial::puts("[kernel] Created /t/vfs_test.txt as fd ");
        serial::put_dec(fd);
        serial::puts("\n");

        const char *data = "Created via VFS!";
        i64 written = fs::vfs::write(fd, data, 16);
        serial::puts("[kernel] VFS wrote ");
        serial::put_dec(written);
        serial::puts(" bytes\n");

        fs::vfs::lseek(fd, 0, fs::vfs::seek::SET);
        char buf[32] = {};
        i64 rd = fs::vfs::read(fd, buf, sizeof(buf) - 1);
        if (rd > 0) {
            serial::puts("[kernel] VFS read back: ");
            serial::puts(buf);
            serial::puts("\n");
        }
        fs::vfs::close(fd);
    }

    fs::viperfs::viperfs().sync();
}

/**
 * @brief Initialize Assign system and test assign resolution.
 */
void init_assign_system() {
    serial::puts("[kernel] Initializing Assign system...\n");
    viper::assign::init();
    viper::assign::setup_standard_assigns();
    viper::assign::debug_dump();

    serial::puts("[kernel] Testing assign inode resolution...\n");

    u64 sys_inode = viper::assign::get_inode("SYS");
    serial::puts("  SYS -> inode ");
    serial::put_dec(sys_inode);
    serial::puts(sys_inode != 0 ? " OK\n" : " FAIL\n");

    u64 d0_inode = viper::assign::get_inode("D0");
    serial::puts("  D0 -> inode ");
    serial::put_dec(d0_inode);
    serial::puts(d0_inode != 0 ? " OK\n" : " FAIL\n");

    i32 vinit_fd = fs::vfs::open("/sys/vinit.sys", fs::vfs::flags::O_RDONLY);
    serial::puts("  /sys/vinit.sys -> ");
    if (vinit_fd >= 0) {
        serial::puts("fd ");
        serial::put_dec(vinit_fd);
        serial::puts(" OK\n");
        fs::vfs::close(vinit_fd);
    } else {
        serial::puts("FAIL (not found)\n");
    }

    u64 bad_inode = viper::assign::get_inode("NONEXISTENT");
    serial::puts("  NONEXISTENT -> ");
    serial::puts(bad_inode == 0 ? "0 (expected)\n" : "FAIL\n");
}

/**
 * @brief Initialize filesystems and run storage tests.
 */
void init_filesystem_subsystem() {
    if (!virtio::blk_device())
        return;

    test_block_device();
    test_block_cache();

    serial::puts("[kernel] Initializing ViperFS...\n");
    if (!fs::viperfs::viperfs_init()) {
        serial::puts("[kernel] ViperFS mount failed\n");
        return;
    }

    serial::puts("[kernel] ViperFS mounted: ");
    serial::puts(fs::viperfs::viperfs().label());
    serial::puts("\n");

    serial::puts("[kernel] Reading root directory...\n");
    fs::viperfs::Inode *root = fs::viperfs::viperfs().read_inode(fs::viperfs::ROOT_INODE);
    if (root) {
        test_viperfs_root(root);
        test_viperfs_write(root);
        fs::viperfs::viperfs().release_inode(root);

        serial::puts("[kernel] Initializing VFS...\n");
        fs::vfs::init();

        init_user_disk();
        test_vfs_operations();
        init_assign_system();
    }
}

/**
 * @brief Test capability table operations.
 */
void test_cap_table(cap::Table *ct) {
    serial::puts("[kernel] Testing capability table...\n");

    int dummy_object = 42;
    cap::Handle h1 = ct->insert(&dummy_object, cap::Kind::Blob, cap::CAP_RW);
    if (h1 == cap::HANDLE_INVALID)
        return;

    serial::puts("[kernel] Inserted handle ");
    serial::put_hex(h1);
    serial::puts(" (index=");
    serial::put_dec(cap::handle_index(h1));
    serial::puts(", gen=");
    serial::put_dec(cap::handle_gen(h1));
    serial::puts(")\n");

    cap::Entry *e = ct->get(h1);
    if (e && e->object == &dummy_object)
        serial::puts("[kernel] Handle lookup OK\n");

    cap::Handle h2 = ct->derive(h1, cap::CAP_READ);
    if (h2 == cap::HANDLE_INVALID)
        serial::puts("[kernel] Derive failed (expected - no CAP_DERIVE)\n");

    cap::Handle h3 = ct->insert(
        &dummy_object, cap::Kind::Blob, static_cast<cap::Rights>(cap::CAP_RW | cap::CAP_DERIVE));
    cap::Handle h4 = ct->derive(h3, cap::CAP_READ);
    if (h4 != cap::HANDLE_INVALID) {
        serial::puts("[kernel] Derived handle ");
        serial::put_hex(h4);
        serial::puts(" with CAP_READ only\n");
    }

    ct->remove(h1);
    if (!ct->get(h1))
        serial::puts("[kernel] Handle correctly invalidated after remove\n");

    serial::puts("[kernel] Capability table: ");
    serial::put_dec(ct->count());
    serial::puts("/");
    serial::put_dec(ct->capacity());
    serial::puts(" slots used\n");

    serial::puts("[kernel] Testing KObj blob...\n");
    kobj::Blob *blob = kobj::Blob::create(4096);
    if (blob) {
        cap::Handle blob_h = ct->insert(blob, cap::Kind::Blob, cap::CAP_RW);
        if (blob_h != cap::HANDLE_INVALID) {
            serial::puts("[kernel] Blob handle: ");
            serial::put_hex(blob_h);
            serial::puts(", size=");
            serial::put_dec(blob->size());
            serial::puts(", phys=");
            serial::put_hex(blob->phys());
            serial::puts("\n");

            u32 *data = static_cast<u32 *>(blob->data());
            data[0] = 0xDEADBEEF;
            serial::puts("[kernel] Wrote 0xDEADBEEF to blob\n");
        }

        kobj::Channel *kch = kobj::Channel::create();
        if (kch) {
            cap::Handle ch_h = ct->insert(kch, cap::Kind::Channel, cap::CAP_RW);
            serial::puts("[kernel] KObj channel handle: ");
            serial::put_hex(ch_h);
            serial::puts(", channel_id=");
            serial::put_dec(kch->id());
            serial::puts("\n");
        }
    }
}

/**
 * @brief Test sbrk syscall implementation.
 */
void test_sbrk(viper::Viper *vp) {
    serial::puts("[kernel] Testing sbrk...\n");

    u64 initial_break = vp->heap_break;
    serial::puts("[kernel]   Initial heap break: ");
    serial::put_hex(initial_break);
    serial::puts("\n");

    i64 result = viper::do_sbrk(vp, 0);
    if (result == static_cast<i64>(initial_break))
        serial::puts("[kernel]   sbrk(0) returned correct break\n");
    else
        serial::puts("[kernel]   ERROR: sbrk(0) returned wrong value\n");

    result = viper::do_sbrk(vp, 4096);
    if (result == static_cast<i64>(initial_break)) {
        serial::puts("[kernel]   sbrk(4096) returned old break\n");
        serial::puts("[kernel]   New heap break: ");
        serial::put_hex(vp->heap_break);
        serial::puts("\n");

        viper::AddressSpace *as = viper::get_address_space(vp);
        if (as) {
            u64 phys = as->translate(initial_break);
            if (phys != 0) {
                serial::puts("[kernel]   Heap page mapped to phys: ");
                serial::put_hex(phys);
                serial::puts("\n");

                u32 *ptr = static_cast<u32 *>(pmm::phys_to_virt(phys));
                ptr[0] = 0xCAFEBABE;
                if (ptr[0] == 0xCAFEBABE)
                    serial::puts("[kernel]   Heap memory R/W test PASSED\n");
                else
                    serial::puts("[kernel]   ERROR: Heap memory R/W test FAILED\n");
            } else {
                serial::puts("[kernel]   ERROR: Heap page not mapped!\n");
            }
        }
    } else {
        serial::puts("[kernel]   ERROR: sbrk(4096) failed with ");
        serial::put_dec(result);
        serial::puts("\n");
    }

    result = viper::do_sbrk(vp, 8192);
    if (result > 0) {
        serial::puts("[kernel]   sbrk(8192) succeeded, new break: ");
        serial::put_hex(vp->heap_break);
        serial::puts("\n");
    }

    serial::puts("[kernel] sbrk test complete\n");
}

/**
 * @brief Test address space mapping operations.
 */
void test_address_space(viper::Viper *vp) {
    viper::AddressSpace *as = viper::get_address_space(vp);
    if (!as || !as->is_valid())
        return;

    u64 test_vaddr = viper::layout::USER_HEAP_BASE;
    u64 test_page = as->alloc_map(test_vaddr, 4096, viper::prot::RW);
    if (test_page) {
        serial::puts("[kernel] Mapped test page at ");
        serial::put_hex(test_vaddr);
        serial::puts("\n");

        u64 phys = as->translate(test_vaddr);
        serial::puts("[kernel] Translates to physical ");
        serial::put_hex(phys);
        serial::puts("\n");

        as->unmap(test_vaddr, 4096);
        serial::puts("[kernel] Unmapped test page\n");
    }
}

/**
 * @brief Load and start vinit process.
 * @return true if vinit was loaded and enqueued successfully.
 */
bool load_and_start_vinit(viper::Viper *vp) {
    serial::puts("[kernel] Loading vinit from disk...\n");

    loader::LoadResult load_result = loader::load_elf_from_disk(vp, "/sys/vinit.sys");
    if (!load_result.success) {
        serial::puts("[kernel] Failed to load vinit\n");
        return false;
    }

    serial::puts("[kernel] vinit loaded successfully\n");

    viper::AddressSpace *as = viper::get_address_space(vp);
    u64 stack_base = viper::layout::USER_STACK_TOP - viper::layout::USER_STACK_SIZE;
    u64 stack_page = as->alloc_map(stack_base, viper::layout::USER_STACK_SIZE, viper::prot::RW);
    if (!stack_page) {
        serial::puts("[kernel] Failed to map user stack\n");
        return false;
    }

    serial::puts("[kernel] User stack mapped at ");
    serial::put_hex(stack_base);
    serial::puts(" - ");
    serial::put_hex(viper::layout::USER_STACK_TOP);
    serial::puts("\n");

#ifdef VIPERDOS_DIRECT_USER_MODE
    serial::puts("[kernel] DIRECT MODE: Entering user mode without scheduler\n");
    viper::switch_address_space(vp->ttbr0, vp->asid);
    asm volatile("tlbi aside1is, %0" ::"r"(static_cast<u64>(vp->asid) << 48));
    asm volatile("dsb sy");
    asm volatile("isb");
    viper::set_current(vp);
    enter_user_mode(load_result.entry_point, viper::layout::USER_STACK_TOP, 0);
    return true;
#else
    task::Task *vinit_task =
        task::create_user_task("vinit", vp, load_result.entry_point, viper::layout::USER_STACK_TOP);

    if (vinit_task) {
        serial::puts("[kernel] vinit task created, will run under scheduler\n");
        scheduler::enqueue(vinit_task);
        return true;
    } else {
        serial::puts("[kernel] Failed to create vinit task\n");
        return false;
    }
#endif
}

/**
 * @brief Initialize Viper subsystem and create/test vinit process.
 */
void init_viper_subsystem() {
    serial::puts("\n[kernel] Configuring MMU for user space...\n");
    mmu::init();

    serial::puts("\n[kernel] Initializing Viper subsystem...\n");
    viper::init();

    tests::run_storage_tests();

    if (fs::viperfs::viperfs().is_mounted()) {
        fs::viperfs::viperfs().sync();
        serial::puts("[kernel] Filesystem synced after storage tests\n");
    }

    tests::run_viper_tests();
    tests::run_syscall_tests();
    tests::create_ipc_test_tasks();

    serial::puts("[kernel] Testing Viper creation...\n");
    viper::Viper *vp = viper::create(nullptr, "test_viper");
    if (!vp) {
        serial::puts("[kernel] Failed to create test Viper!\n");
        return;
    }

    viper::print_info(vp);
    test_address_space(vp);

    if (vp->cap_table)
        test_cap_table(vp->cap_table);

    test_sbrk(vp);

    if (!load_and_start_vinit(vp))
        viper::destroy(vp);
}

} // anonymous namespace

// =============================================================================
// KERNEL ENTRY POINT
// =============================================================================

/**
 * @brief Kernel main entry point invoked from the assembly boot stub.
 * @param boot_info_ptr Boot environment information pointer (DTB or VBootInfo).
 */
extern "C" void kernel_main(void *boot_info_ptr) {
    serial::init();
    print_boot_banner();
    boot::init(boot_info_ptr);
    boot::dump();
    serial::puts("\n");

    init_framebuffer();
    init_memory_subsystem();
    init_interrupts();
    init_task_subsystem();
    init_virtio_subsystem();

#if VIPER_KERNEL_ENABLE_NET
    init_network_subsystem();
#else
    serial::puts("[kernel] Kernel networking disabled (VIPER_KERNEL_ENABLE_NET=0)\n");
#endif

    init_filesystem_subsystem();

    if (gcon::is_available()) {
        gcon::puts("  Devices...OK\n");
        timer::delay_ms(50);
    }

    init_viper_subsystem();

    if (gcon::is_available()) {
        gcon::puts("  Kernel...OK\n");
        gcon::puts("\n");
        timer::delay_ms(100);
    }

    serial::puts("\nHello from ViperDOS!\n");
    serial::puts("Kernel initialization complete.\n");

    cpu::boot_secondaries();

    serial::puts("Starting scheduler...\n");
    if (gcon::is_available()) {
        gcon::puts("  Starting...\n");
        timer::delay_ms(200);
    }

    scheduler::start();
}
