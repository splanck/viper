//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: kernel/sched/idle.cpp
// Purpose: CPU idle state management implementation.
//
//===----------------------------------------------------------------------===//

#include "idle.hpp"
#include "../arch/aarch64/cpu.hpp"
#include "../console/serial.hpp"

namespace idle
{

namespace
{

// Per-CPU idle statistics (simple counters only)
IdleStats per_cpu_stats[cpu::MAX_CPUS];

} // namespace

void init()
{
    serial::puts("[idle] Initializing idle state tracking\n");

    for (u32 i = 0; i < cpu::MAX_CPUS; i++)
    {
        per_cpu_stats[i].wfi_count = 0;
        per_cpu_stats[i].wakeup_count = 0;
    }

    serial::puts("[idle] Idle state tracking initialized\n");
}

void enter(u32 cpu_id)
{
    if (cpu_id >= cpu::MAX_CPUS)
        return;

    per_cpu_stats[cpu_id].wfi_count++;
}

void exit(u32 cpu_id)
{
    if (cpu_id >= cpu::MAX_CPUS)
        return;

    per_cpu_stats[cpu_id].wakeup_count++;
}

void get_stats(u32 cpu_id, IdleStats *stats)
{
    if (cpu_id >= cpu::MAX_CPUS || !stats)
        return;

    *stats = per_cpu_stats[cpu_id];
}

} // namespace idle
