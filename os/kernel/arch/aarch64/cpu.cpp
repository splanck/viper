/**
 * @file cpu.cpp
 * @brief Per-CPU data structures and multicore boot implementation.
 *
 * @details
 * Implements multicore support including:
 * - Per-CPU data structures
 * - PSCI-based secondary CPU boot
 * - IPI (Inter-Processor Interrupt) support via GIC SGIs
 */
#include "cpu.hpp"
#include "../../console/serial.hpp"
#include "gic.hpp"

// Suppress warnings for PSCI constants that document the full API
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-const-variable"

namespace cpu
{

namespace
{

/// Per-CPU data array (one per CPU)
CpuData cpu_data[MAX_CPUS];

/// Per-CPU kernel stacks (allocated statically)
alignas(16) u8 cpu_stacks[MAX_CPUS][CPU_STACK_SIZE];

/// Number of CPUs detected/online
u32 num_cpus_online = 0;

/// PSCI function IDs (SMCCC compliant)
namespace psci
{
constexpr u64 CPU_ON_64 = 0xC4000003; ///< CPU_ON for 64-bit
constexpr u64 CPU_OFF = 0x84000002;
constexpr u64 SYSTEM_OFF = 0x84000008;
constexpr u64 SYSTEM_RESET = 0x84000009;
constexpr u64 PSCI_VERSION = 0x84000000;

/// Return codes
constexpr i64 SUCCESS = 0;
constexpr i64 NOT_SUPPORTED = -1;
constexpr i64 INVALID_PARAMS = -2;
constexpr i64 DENIED = -3;
constexpr i64 ALREADY_ON = -4;
constexpr i64 ON_PENDING = -5;
constexpr i64 INTERNAL_FAILURE = -6;

/**
 * @brief Invoke a PSCI function via SMC.
 *
 * @param fn PSCI function ID
 * @param arg0-arg2 Arguments
 * @return PSCI return code
 */
inline i64 call(u64 fn, u64 arg0 = 0, u64 arg1 = 0, u64 arg2 = 0)
{
    register u64 x0 asm("x0") = fn;
    register u64 x1 asm("x1") = arg0;
    register u64 x2 asm("x2") = arg1;
    register u64 x3 asm("x3") = arg2;

    asm volatile("smc #0"
                 : "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3)
                 :
                 : "x4", "x5", "x6", "x7", "x8", "x9", "x10", "x11", "x12",
                   "x13", "x14", "x15", "x16", "x17", "memory");

    return static_cast<i64>(x0);
}
} // namespace psci

/**
 * @brief Get CPU ID from MPIDR_EL1.
 *
 * On QEMU virt, the Aff0 field contains the CPU number.
 */
inline u32 read_cpu_id()
{
    u64 mpidr;
    asm volatile("mrs %0, mpidr_el1" : "=r"(mpidr));
    return mpidr & 0xFF; // Aff0 field
}

} // namespace

void init()
{
    serial::puts("[cpu] Initializing CPU subsystem\n");

    // Initialize boot CPU (CPU 0)
    u32 id = read_cpu_id();
    serial::puts("[cpu] Boot CPU ID: ");
    serial::put_dec(id);
    serial::puts("\n");

    // Set up boot CPU's data
    cpu_data[0].id = id;
    cpu_data[0].online = 1;
    cpu_data[0].stack_top = reinterpret_cast<u64>(&cpu_stacks[0][CPU_STACK_SIZE]);
    cpu_data[0].idle_ticks = 0;
    cpu_data[0].current_task = nullptr;

    // Initialize other CPU data as offline
    for (u32 i = 1; i < MAX_CPUS; i++)
    {
        cpu_data[i].id = i;
        cpu_data[i].online = 0;
        cpu_data[i].stack_top = reinterpret_cast<u64>(&cpu_stacks[i][CPU_STACK_SIZE]);
        cpu_data[i].idle_ticks = 0;
        cpu_data[i].current_task = nullptr;
    }

    num_cpus_online = 1;

    // Check PSCI version
    i64 version = psci::call(psci::PSCI_VERSION);
    if (version >= 0)
    {
        serial::puts("[cpu] PSCI version: ");
        serial::put_dec((version >> 16) & 0xFFFF);
        serial::puts(".");
        serial::put_dec(version & 0xFFFF);
        serial::puts("\n");
    }
    else
    {
        serial::puts("[cpu] PSCI not available (single CPU mode)\n");
    }
}

u32 current_id()
{
    return read_cpu_id();
}

CpuData *current()
{
    u32 id = read_cpu_id();
    if (id < MAX_CPUS)
    {
        return &cpu_data[id];
    }
    return &cpu_data[0]; // Fallback to boot CPU
}

CpuData *get(u32 id)
{
    if (id < MAX_CPUS)
    {
        return &cpu_data[id];
    }
    return nullptr;
}

u32 online_count()
{
    return num_cpus_online;
}

// External symbol from boot.S for secondary CPU entry
extern "C" void secondary_entry();

void boot_secondaries()
{
    serial::puts("[cpu] Booting secondary CPUs...\n");

    u64 entry_point = reinterpret_cast<u64>(&secondary_entry);

    // Try to boot CPUs 1, 2, 3
    for (u32 i = 1; i < MAX_CPUS; i++)
    {
        // MPIDR for CPU i on QEMU virt is simply i in Aff0
        u64 mpidr = i;

        serial::puts("[cpu] Starting CPU ");
        serial::put_dec(i);
        serial::puts(" (MPIDR=");
        serial::put_hex(mpidr);
        serial::puts(")...\n");

        // CPU_ON(target_cpu, entry_point, context_id)
        // context_id is passed to secondary_entry in x0
        i64 result = psci::call(psci::CPU_ON_64, mpidr, entry_point, i);

        if (result == psci::SUCCESS)
        {
            serial::puts("[cpu] CPU ");
            serial::put_dec(i);
            serial::puts(" started successfully\n");
        }
        else if (result == psci::ALREADY_ON)
        {
            serial::puts("[cpu] CPU ");
            serial::put_dec(i);
            serial::puts(" already running\n");
        }
        else
        {
            serial::puts("[cpu] CPU ");
            serial::put_dec(i);
            serial::puts(" start failed: ");
            serial::put_hex(static_cast<u64>(result));
            serial::puts("\n");
        }
    }
}

extern "C" void secondary_main(u32 cpu_id)
{
    // Set up this CPU's data
    if (cpu_id < MAX_CPUS)
    {
        cpu_data[cpu_id].online = 1;
        // Simple increment with memory barrier (no SMP contention for this counter)
        asm volatile("dmb sy" ::: "memory");
        num_cpus_online++;
        asm volatile("dmb sy" ::: "memory");
    }

    serial::puts("[cpu] Secondary CPU ");
    serial::put_dec(cpu_id);
    serial::puts(" online\n");

    // Initialize per-CPU timer (each CPU has its own timer)
    // For now, secondary CPUs just enter idle loop
    // In a full implementation, they would:
    // 1. Set up their own timer
    // 2. Enable interrupts
    // 3. Enter the scheduler's idle loop

    serial::puts("[cpu] CPU ");
    serial::put_dec(cpu_id);
    serial::puts(" entering idle\n");

    // Idle loop - wait for IPIs
    for (;;)
    {
        asm volatile("wfi");
    }
}

void send_ipi(u32 target_cpu, u32 ipi_type)
{
    if (target_cpu >= MAX_CPUS)
        return;

    // SGI (Software Generated Interrupt) via GIC
    // GICD_SGIR format: [25:24] target list filter, [23:16] CPU target list, [3:0] SGI ID
    // For target list filter = 0b00, we specify target CPU in bits 23:16
    volatile u32 *gicd_sgir = reinterpret_cast<volatile u32 *>(0x08000000 + 0xF00);

    u32 target_mask = 1 << target_cpu;
    u32 sgi_value = (target_mask << 16) | (ipi_type & 0xF);

    *gicd_sgir = sgi_value;
}

void broadcast_ipi(u32 ipi_type)
{
    // Broadcast to all other CPUs (target list filter = 0b01)
    volatile u32 *gicd_sgir = reinterpret_cast<volatile u32 *>(0x08000000 + 0xF00);

    u32 sgi_value = (1 << 24) | (ipi_type & 0xF); // Filter = 0b01 = all except self

    *gicd_sgir = sgi_value;
}

} // namespace cpu

#pragma GCC diagnostic pop
