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
 * @brief ViperDOS kernel entry point.
 *
 * @details
 * This translation unit contains the top-level C++ entry point invoked by the
 * early assembly boot stub (`boot.S`). It calls subsystem initialization
 * functions and starts the scheduler.
 */

#include "arch/aarch64/cpu.hpp"
#include "arch/aarch64/timer.hpp"
#include "boot/bootinfo.hpp"
#include "console/gcon.hpp"
#include "console/serial.hpp"
#include "init/init.hpp"
#include "sched/scheduler.hpp"
#include "viper/address_space.hpp"

// C++ runtime support
extern "C" {

/**
 * @brief Handler for "pure virtual function call" runtime errors.
 *
 * @details
 * The C++ runtime calls `__cxa_pure_virtual` if a pure virtual function is
 * invoked. In a kernel environment this is always a fatal programming error.
 */
void __cxa_pure_virtual() {
    serial::puts("PANIC: Pure virtual function called!\n");
    for (;;)
        asm volatile("wfi");
}

} // extern "C"

// =============================================================================
// KERNEL ENTRY POINT
// =============================================================================

/**
 * @brief Kernel main entry point invoked from the assembly boot stub.
 * @param boot_info_ptr Boot environment information pointer (DTB or VBootInfo).
 */
extern "C" void kernel_main(void *boot_info_ptr) {
    // Initialize serial console first
    serial::init();
    init::print_boot_banner();

    // Parse boot information
    boot::init(boot_info_ptr);
    boot::dump();
    serial::puts("\n");

    // Initialize subsystems in order
    init::init_framebuffer();
    init::init_memory_subsystem();
    init::init_interrupts();
    init::init_task_subsystem();
    init::init_virtio_subsystem();
    init::init_network_subsystem();
    init::init_filesystem_subsystem();

    if (gcon::is_available()) {
        gcon::puts("  Devices...OK\n");
        timer::delay_ms(50);
    }

    init::init_viper_subsystem();

    if (gcon::is_available()) {
        gcon::puts("  Kernel...OK\n");
        gcon::puts("\n");
        timer::delay_ms(100);
    }

    serial::puts("\nHello from ViperDOS!\n");
    serial::puts("Kernel initialization complete.\n");

    // Boot secondary CPUs
    cpu::boot_secondaries();
    viper::debug_verify_vinit_tables("after cpu::boot_secondaries");

    serial::puts("Starting scheduler...\n");
    viper::debug_verify_vinit_tables("before scheduler::start");

    if (gcon::is_available()) {
        gcon::puts("  Starting...\n");
        timer::delay_ms(200);
        viper::debug_verify_vinit_tables("after gcon delay");
    }

    // Start scheduler - never returns
    scheduler::start();
}
