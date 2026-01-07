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

namespace deadline
{

// Total bandwidth currently reserved
u64 total_bandwidth = 0;

i32 set_deadline(task::Task *t, const DeadlineParams *params)
{
    if (!t || !params)
        return -1;

    // Validate parameters
    if (!validate_params(params))
    {
        serial::puts("[deadline] Invalid deadline parameters\n");
        return -1;
    }

    // Calculate bandwidth
    u64 new_bandwidth = calc_bandwidth(params);

    // If task already has deadline params, subtract old bandwidth
    u64 old_bandwidth = 0;
    if (t->dl_period > 0)
    {
        DeadlineParams old_params = {t->dl_runtime, t->dl_deadline, t->dl_period};
        old_bandwidth = calc_bandwidth(&old_params);
    }

    // Check admission control
    if (!can_admit(new_bandwidth - old_bandwidth))
    {
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

void clear_deadline(task::Task *t)
{
    if (!t)
        return;

    // Remove bandwidth reservation
    if (t->dl_period > 0)
    {
        DeadlineParams params = {t->dl_runtime, t->dl_deadline, t->dl_period};
        u64 bandwidth = calc_bandwidth(&params);
        if (total_bandwidth >= bandwidth)
        {
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

void replenish(task::Task *t, u64 current_time)
{
    if (!t || t->dl_period == 0)
        return;

    // Set absolute deadline to current time + relative deadline
    t->dl_abs_deadline = current_time + t->dl_deadline;
}

} // namespace deadline
