//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
/**
 * @file cpu.hpp
 * @brief Per-CPU data structures and multicore support.
 *
 * @details
 * This file provides the infrastructure for multicore operation:
 * - Per-CPU data structures
 * - CPU identification
 * - PSCI interface for secondary CPU boot
 */
#pragma once

#include <types.hpp>

namespace cpu {

/// Maximum supported CPUs (QEMU virt default is 4)
constexpr u32 MAX_CPUS = 4;

/// Per-CPU stack size (16KB each)
constexpr u64 CPU_STACK_SIZE = 16384;

/**
 * @brief Per-CPU data structure.
 *
 * Each CPU has its own instance of this structure, containing
 * CPU-local state that doesn't need locking.
 */
struct CpuData {
    u32 id;              ///< CPU ID (0 = boot CPU)
    u32 online;          ///< 1 if CPU is online and running
    u64 stack_top;       ///< Top of this CPU's kernel stack
    u64 idle_ticks;      ///< Ticks spent in idle
    void *current_task;  ///< Current running task on this CPU
    void *current_viper; ///< Current viper process on this CPU
};

/**
 * @brief Initialize the boot CPU's data structure.
 *
 * Called early in kernel_main before other CPUs are started.
 */
void init();

/**
 * @brief Get the current CPU ID.
 *
 * Reads MPIDR_EL1 to determine which CPU we're running on.
 *
 * @return CPU ID (0-3 for QEMU virt with 4 CPUs)
 */
u32 current_id();

/**
 * @brief Get the per-CPU data for the current CPU.
 *
 * @return Pointer to the current CPU's CpuData structure.
 */
CpuData *current();

/**
 * @brief Get the per-CPU data for a specific CPU.
 *
 * @param id CPU ID (0 to MAX_CPUS-1)
 * @return Pointer to the CPU's CpuData structure, or nullptr if invalid.
 */
CpuData *get(u32 id);

/**
 * @brief Get the number of online CPUs.
 *
 * @return Count of CPUs that are online and running.
 */
u32 online_count();

/**
 * @brief Boot secondary CPUs using PSCI.
 *
 * Wakes up all secondary CPUs and has them execute the kernel.
 * Each secondary CPU will call secondary_main() after initialization.
 */
void boot_secondaries();

/**
 * @brief Entry point for secondary CPUs.
 *
 * Called from boot.S after a secondary CPU is woken by PSCI.
 * Sets up the CPU's state and enters the scheduler.
 *
 * @param cpu_id The ID of this secondary CPU.
 */
extern "C" void secondary_main(u32 cpu_id);

/**
 * @brief Send an inter-processor interrupt (IPI) to a specific CPU.
 *
 * Uses GIC SGI (Software Generated Interrupt) to signal another CPU.
 *
 * @param target_cpu CPU ID to send the IPI to.
 * @param ipi_type Type of IPI (0 = reschedule, 1 = stop, etc.)
 */
void send_ipi(u32 target_cpu, u32 ipi_type);

/**
 * @brief Broadcast an IPI to all other CPUs.
 *
 * @param ipi_type Type of IPI to send.
 */
void broadcast_ipi(u32 ipi_type);

/// IPI types
namespace ipi {
constexpr u32 RESCHEDULE = 0; ///< Ask CPU to reschedule
constexpr u32 STOP = 1;       ///< Ask CPU to stop (for panic)
constexpr u32 TLB_FLUSH = 2;  ///< Ask CPU to flush TLB
} // namespace ipi

} // namespace cpu
