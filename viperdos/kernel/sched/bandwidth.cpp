//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: kernel/sched/bandwidth.cpp
// Purpose: CPU bandwidth control implementation.
//
//===----------------------------------------------------------------------===//

#include "bandwidth.hpp"
#include "../console/serial.hpp"
#include "scheduler.hpp"

namespace bandwidth {

i32 set_bandwidth(task::Task *t, const BandwidthParams *params) {
    if (!t || !params)
        return -1;

    // Validate parameters
    if (params->runtime != 0) { // 0 means unlimited
        if (params->period < MIN_PERIOD_NS || params->period > MAX_PERIOD_NS) {
            serial::puts("[bandwidth] Invalid period (must be 1ms-1s)\n");
            return -1;
        }
        if (params->runtime > params->period) {
            serial::puts("[bandwidth] Runtime cannot exceed period\n");
            return -1;
        }
    }

    // Set bandwidth parameters
    t->bw_runtime = params->runtime;
    t->bw_period = params->period;
    t->bw_consumed = 0;
    t->bw_period_start = 0; // Will be set on first run
    t->bw_throttled = false;

    if (params->runtime > 0) {
        serial::puts("[bandwidth] Task '");
        serial::puts(t->name);
        serial::puts("' limited to ");
        serial::put_dec(static_cast<u32>(params->runtime / 1000000)); // ms
        serial::puts("ms per ");
        serial::put_dec(static_cast<u32>(params->period / 1000000)); // ms
        serial::puts("ms (");
        serial::put_dec(static_cast<u32>((params->runtime * 100) / params->period));
        serial::puts("% CPU)\n");
    }

    return 0;
}

void clear_bandwidth(task::Task *t) {
    if (!t)
        return;

    t->bw_runtime = 0;
    t->bw_period = 0;
    t->bw_consumed = 0;
    t->bw_period_start = 0;
    t->bw_throttled = false;
}

bool account_runtime(task::Task *t, u32 ticks_used) {
    if (!t)
        return true;

    // No bandwidth control configured
    if (t->bw_runtime == 0)
        return true;

    // Convert ticks to nanoseconds (1 tick = 1ms = 1,000,000ns)
    u64 ns_used = static_cast<u64>(ticks_used) * 1000000ULL;

    // Add to consumed budget
    t->bw_consumed += ns_used;

    // Check if over budget
    if (t->bw_consumed >= t->bw_runtime) {
        t->bw_throttled = true;

        serial::puts("[bandwidth] Task '");
        serial::puts(t->name);
        serial::puts("' throttled (consumed ");
        serial::put_dec(static_cast<u32>(t->bw_consumed / 1000000));
        serial::puts("ms of ");
        serial::put_dec(static_cast<u32>(t->bw_runtime / 1000000));
        serial::puts("ms budget)\n");

        return false; // Task should be removed from run queue
    }

    return true;
}

void replenish_budget(task::Task *t, u64 current_tick) {
    if (!t)
        return;

    // Reset consumed budget
    t->bw_consumed = 0;
    t->bw_period_start = current_tick;

    // Unthrottle if was throttled
    if (t->bw_throttled) {
        t->bw_throttled = false;

        serial::puts("[bandwidth] Task '");
        serial::puts(t->name);
        serial::puts("' unthrottled (new period)\n");

        // Re-enqueue the task if it's still runnable
        if (t->state == task::TaskState::Blocked) {
            t->state = task::TaskState::Ready;
            scheduler::enqueue(t);
        }
    }
}

u32 check_replenish(u64 current_tick) {
    u32 count = 0;

    // Iterate all tasks checking for replenishment
    for (u32 i = 0; i < task::MAX_TASKS; i++) {
        task::Task *t = task::get_by_id(i);
        if (!t)
            continue;

        // Skip tasks without bandwidth control
        if (t->bw_runtime == 0)
            continue;

        // Skip non-throttled tasks
        if (!t->bw_throttled)
            continue;

        // Convert period to ticks (1 tick = 1ms, period is in ns)
        u64 period_ticks = t->bw_period / 1000000ULL;
        if (period_ticks == 0)
            period_ticks = 1;

        // Check if we've entered a new period
        if (current_tick >= t->bw_period_start + period_ticks) {
            replenish_budget(t, current_tick);
            count++;
        }
    }

    return count;
}

} // namespace bandwidth
