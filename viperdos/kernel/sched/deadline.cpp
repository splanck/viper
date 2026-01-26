//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: kernel/sched/deadline.cpp
// Purpose: SCHED_DEADLINE support implementation (EDF scheduling).
//
//===----------------------------------------------------------------------===//

#include "deadline.hpp"
#include "../console/serial.hpp"

namespace deadline {

// Total bandwidth currently reserved
u64 total_bandwidth = 0;

i32 set_deadline(task::Task *t, const DeadlineParams *params) {
    if (!t || !params)
        return -1;

    // Validate parameters
    if (!validate_params(params)) {
        serial::puts("[deadline] Invalid deadline parameters\n");
        return -1;
    }

    // Calculate bandwidth
    u64 new_bandwidth = calc_bandwidth(params);

    // If task already has deadline params, subtract old bandwidth
    u64 old_bandwidth = 0;
    if (t->dl_period > 0) {
        DeadlineParams old_params = {t->dl_runtime, t->dl_deadline, t->dl_period};
        old_bandwidth = calc_bandwidth(&old_params);
    }

    // Check admission control
    if (!can_admit(new_bandwidth - old_bandwidth)) {
        serial::puts("[deadline] Admission control failed: bandwidth limit exceeded\n");
        return -1;
    }

    // Update bandwidth tracking
    total_bandwidth = total_bandwidth - old_bandwidth + new_bandwidth;

    // Set task parameters
    t->dl_runtime = params->runtime;
    t->dl_deadline = params->deadline;
    t->dl_period = params->period;
    t->policy = task::SchedPolicy::SCHED_DEADLINE;

    return 0;
}

void clear_deadline(task::Task *t) {
    if (!t)
        return;

    // Remove bandwidth reservation
    if (t->dl_period > 0) {
        DeadlineParams params = {t->dl_runtime, t->dl_deadline, t->dl_period};
        u64 bandwidth = calc_bandwidth(&params);
        if (total_bandwidth >= bandwidth) {
            total_bandwidth -= bandwidth;
        }
    }

    // Clear deadline parameters
    t->dl_runtime = 0;
    t->dl_deadline = 0;
    t->dl_period = 0;
    t->dl_abs_deadline = 0;
    t->policy = task::SchedPolicy::SCHED_OTHER;
}

void replenish(task::Task *t, u64 current_time) {
    if (!t || t->dl_period == 0)
        return;

    // Set absolute deadline to current time + relative deadline
    t->dl_abs_deadline = current_time + t->dl_deadline;
}

bool check_deadline_miss(task::Task *t, u64 current_time) {
    if (!t)
        return false;

    // Only check deadline tasks with a valid absolute deadline
    if (t->policy != task::SchedPolicy::SCHED_DEADLINE)
        return false;
    if (t->dl_abs_deadline == 0)
        return false;

    // Deadline is missed if current time exceeds the absolute deadline
    return current_time > t->dl_abs_deadline;
}

void handle_deadline_miss(task::Task *t, u64 current_time) {
    if (!t)
        return;

    // Increment missed deadline counter
    t->dl_missed++;

    serial::puts("[deadline] Task '");
    serial::puts(t->name);
    serial::puts("' missed deadline (count: ");
    serial::put_dec(t->dl_missed);
    serial::puts(")\n");

    // Handle based on flags
    if (t->dl_flags & DL_FLAG_DEMOTE_ON_MISS) {
        if (t->dl_missed >= DL_MISS_THRESHOLD) {
            serial::puts("[deadline] Demoting task '");
            serial::puts(t->name);
            serial::puts("' to SCHED_OTHER after ");
            serial::put_dec(DL_MISS_THRESHOLD);
            serial::puts(" misses\n");

            // Clear deadline (releases bandwidth) and demote to SCHED_OTHER
            clear_deadline(t);
            return;
        }
    }

    if (t->dl_flags & DL_FLAG_THROTTLE_ON_MISS) {
        // Skip to next period - replenish deadline
        serial::puts("[deadline] Throttling task '");
        serial::puts(t->name);
        serial::puts("' to next period\n");

        // Advance to next period boundary
        u64 periods_elapsed = 1;
        if (t->dl_period > 0 && current_time > t->dl_abs_deadline) {
            // Calculate how many periods we've missed
            u64 overshoot = current_time - t->dl_abs_deadline;
            periods_elapsed = 1 + (overshoot / t->dl_period);
        }
        t->dl_abs_deadline += periods_elapsed * t->dl_period;
    } else {
        // Default: just replenish for next period
        replenish(t, current_time);
    }
}

} // namespace deadline
