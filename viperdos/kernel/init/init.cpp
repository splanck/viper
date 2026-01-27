//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//

/**
 * @file init.cpp
 * @brief Kernel subsystem initialization functions.
 */

#include "init.hpp"
#include "../arch/aarch64/cpu.hpp"
#include "../arch/aarch64/exceptions.hpp"
#include "../arch/aarch64/gic.hpp"
#include "../arch/aarch64/mmu.hpp"
#include "../arch/aarch64/timer.hpp"
#include "../assign/assign.hpp"
#include "../boot/bootinfo.hpp"
#include "../console/console.hpp"
#include "../console/gcon.hpp"
#include "../console/serial.hpp"
#include "../drivers/fwcfg.hpp"
#include "../drivers/pl031.hpp"
#include "../drivers/ramfb.hpp"
#include "../drivers/virtio/blk.hpp"
#include "../drivers/virtio/gpu.hpp"
#include "../drivers/virtio/input.hpp"
#include "../drivers/virtio/rng.hpp"
#include "../drivers/virtio/virtio.hpp"
#include "../fs/cache.hpp"
#include "../fs/vfs/vfs.hpp"
#include "../fs/viperfs/viperfs.hpp"
#include "../include/config.hpp"
#include "../include/constants.hpp"
#include "../input/input.hpp"
#include "../ipc/channel.hpp"
#include "../ipc/poll.hpp"
#include "../ipc/pollset.hpp"
#include "../loader/loader.hpp"
#include "../mm/kheap.hpp"
#include "../mm/pmm.hpp"
#include "../mm/pressure.hpp"
#include "../mm/slab.hpp"
#include "../mm/swap.hpp"
#include "../mm/vmm.hpp"
#include "../sched/scheduler.hpp"
#include "../sched/task.hpp"
#include "../tests/boot_diagnostics.hpp"
#include "../tests/tests.hpp"
#include "../tty/tty.hpp"
#include "../viper/address_space.hpp"
#include "../viper/viper.hpp"
#include "../../version.h"

#if VIPER_KERNEL_ENABLE_NET
#include "../drivers/virtio/net.hpp"
#include "../net/netstack.hpp"
#endif

// Linker-provided symbols
extern "C" {
extern u8 __kernel_end[];
}

namespace init {

void print_boot_banner() {
    serial::puts("\n");
    serial::puts("=========================================\n");
    serial::puts("  " VIPERDOS_VERSION_FULL " - AArch64\n");
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
        gcon::puts("  Version: " VIPERDOS_VERSION_STRING " | AArch64\n");
        gcon::puts("\n");
        gcon::puts("  Booting...\n");
        gcon::puts("\n");
    } else if (!fb_initialized) {
        serial::puts("[kernel] Running in serial-only mode\n");
    }

    return fb_initialized;
}

void init_memory_subsystem() {
    serial::puts("\n[kernel] Initializing memory management...\n");

    // Get RAM region from boot info
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

    // Initialize subsystems
    pmm::init(ram_base, ram_size, reinterpret_cast<u64>(__kernel_end), fb_base, fb_size);
    vmm::init();
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

    slab::init();
    slab::init_object_caches();
    mm::pressure::init();

    if (gcon::is_available()) {
        gcon::puts("  Memory...OK\n");
    }
    timer::delay_ms(50);
}

void init_interrupts() {
    serial::puts("\n[kernel] Initializing exceptions and interrupts...\n");
    exceptions::init();
    gic::init();
    timer::init();
    cpu::init();
    exceptions::enable_interrupts();
    serial::puts("[kernel] Interrupts enabled\n");

    // Initialize PL031 RTC for wall-clock time
    if (!pl031::init()) {
        serial::puts("[kernel] WARNING: PL031 RTC not available (time() will use uptime)\n");
    }

    if (gcon::is_available()) {
        gcon::puts("  Interrupts...OK\n");
    }
    timer::delay_ms(50);
}

void init_task_subsystem() {
    serial::puts("\n[kernel] Initializing task subsystem...\n");
    task::init();
    scheduler::init();

    serial::puts("\n[kernel] Initializing channel subsystem...\n");
    channel::init();

    serial::puts("\n[kernel] Initializing poll subsystem...\n");
    poll::init();
    pollset::init();

    poll::test_poll();
    pollset::test_pollset();
}

void init_virtio_subsystem() {
    serial::puts("\n[kernel] Initializing virtio subsystem...\n");
    virtio::init();

    if (!virtio::rng::init()) {
        serial::puts("[kernel] WARNING: virtio-rng not available (TCP ISN will use fallback)\n");
    }

    virtio::blk_init();
    virtio::gpu_init();
    virtio::input_init();
    input::init();
    tty::init();
    console::init_input();
}

#if VIPER_KERNEL_ENABLE_NET
void init_network_subsystem() {
    virtio::net_init();
    net::network_init();

    if (virtio::net_device()) {
        u64 start = timer::get_ticks();
        while (timer::get_ticks() - start < 500) {
            net::network_poll();
            asm volatile("wfi");
        }
    }

    if (virtio::net_device()) {
        serial::puts("[kernel] Testing ping to gateway (10.0.2.2)...\n");
        net::Ipv4Addr gateway = {{10, 0, 2, 2}};
        i32 rtt = net::icmp::ping(gateway, 3000);
        if (rtt >= 0) {
            serial::puts("[kernel] Ping successful! RTT: ");
            serial::put_dec(rtt);
            serial::puts(" ms\n");
        } else {
            serial::puts("[kernel] Ping failed (code ");
            serial::put_dec(-rtt);
            serial::puts(")\n");
        }

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
#else
void init_network_subsystem() {
    serial::puts("[kernel] Kernel networking disabled (VIPER_KERNEL_ENABLE_NET=0)\n");
}
#endif

// Helper: Initialize user disk
static void init_user_disk() {
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

    if (mm::swap::init()) {
        serial::puts("[kernel] Swap enabled\n");
    } else {
        serial::puts("[kernel] Swap not available\n");
    }
}

// Helper: Initialize assign system
static void init_assign_system() {
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

void init_filesystem_subsystem() {
    if (!virtio::blk_device())
        return;

    boot_diag::test_block_device();
    boot_diag::test_block_cache();

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
        boot_diag::test_viperfs_root(root);
        boot_diag::test_viperfs_write(root);
        fs::viperfs::viperfs().release_inode(root);

        serial::puts("[kernel] Initializing VFS...\n");
        fs::vfs::init();

        init_user_disk();
        boot_diag::test_vfs_operations();
        init_assign_system();
    }
}

// Helper: Load and start vinit
static bool load_and_start_vinit(viper::Viper *vp) {
    serial::puts("[kernel] Loading vinit from disk...\n");

    loader::LoadResult load_result = loader::load_elf_from_disk(vp, "/sys/vinit.sys");
    if (!load_result.success) {
        serial::puts("[kernel] Failed to load vinit\n");
        return false;
    }

    serial::puts("[kernel] vinit loaded successfully\n");

    viper::AddressSpace *as = viper::get_address_space(vp);
    serial::puts("[kernel] DEBUG: got address space, root=");
    serial::put_hex(as->root());
    serial::puts("\n");

    // Register vinit's page tables for corruption tracking
    {
        u64 l0_phys = as->root();
        serial::puts("[kernel] DEBUG: l0_phys=");
        serial::put_hex(l0_phys);
        serial::puts("\n");
        u64 *l0 = reinterpret_cast<u64 *>(pmm::phys_to_virt(l0_phys));
        serial::puts("[kernel] DEBUG: l0[0]=");
        serial::put_hex(l0[0]);
        serial::puts("\n");
        u64 l1_phys = l0[0] & ~0xFFFULL;
        u64 *l1 = reinterpret_cast<u64 *>(pmm::phys_to_virt(l1_phys));
        serial::puts("[kernel] DEBUG: l1[2]=");
        serial::put_hex(l1[2]);
        serial::puts("\n");
        u64 l2_phys = (l1[2] & ~0xFFFULL);
        viper::debug_set_vinit_tables(l0_phys, l1_phys, l2_phys);
    }

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
        viper::debug_verify_vinit_tables("after vinit enqueue");
        return true;
    } else {
        serial::puts("[kernel] Failed to create vinit task\n");
        return false;
    }
#endif
}

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
    boot_diag::test_address_space(vp);

    if (vp->cap_table)
        boot_diag::test_cap_table(vp->cap_table);

    boot_diag::test_sbrk(vp);

    if (!load_and_start_vinit(vp))
        viper::destroy(vp);
}

} // namespace init
