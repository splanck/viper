//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: kernel/sched/cfs.hpp
// Purpose: CFS (Completely Fair Scheduler) support utilities.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "../include/types.hpp"

namespace cfs {

/**
 * @brief CFS weight table indexed by nice value.
 *
 * @details
 * Maps nice values (-20 to +19) to weights. Each nice level represents
 * approximately a 10% CPU time difference. Nice 0 = weight 1024.
 *
 * Index 0 corresponds to nice -20, index 39 corresponds to nice +19.
 */
constexpr u32 NICE_TO_WEIGHT[40] = {
    /* -20 */ 88761, 71755, 56483, 46273, 36291,
    /* -15 */ 29154, 23254, 18705, 14949, 11916,
    /* -10 */ 9548,  7620,  6100,  4904,  3906,
    /*  -5 */ 3121,  2501,  1991,  1586,  1277,
    /*   0 */ 1024,  820,   655,   526,   423,
    /*   5 */ 335,   272,   215,   172,   137,
    /*  10 */ 110,   87,    70,    56,    45,
    /*  15 */ 36,    29,    23,    18,    15,
};

/**
 * @brief Inverse weight multipliers for fast vruntime calculation.
 *
 * @details
 * inverse_weight[i] = 2^32 / weight[i], used to compute:
 * vruntime += (delta * inverse_weight) >> 32
 */
constexpr u32 NICE_TO_INVERSE_WEIGHT[40] = {
    /* -20 */ 48388,     59856,     76040,     92818,     118348,
    /* -15 */ 147320,    184698,    229616,    287308,    360437,
    /* -10 */ 449829,    563644,    704093,    875809,    1099582,
    /*  -5 */ 1376151,   1717300,   2157191,   2708050,   3363326,
    /*   0 */ 4194304,   5237765,   6557202,   8165337,   10153587,
    /*   5 */ 12820798,  15790321,  19976592,  24970740,  31350126,
    /*  10 */ 39045157,  49367440,  61356676,  76695844,  95443717,
    /*  15 */ 119304647, 148102320, 186737708, 238609294, 286331153,
};

/**
 * @brief Default weight (nice 0).
 */
constexpr u32 WEIGHT_DEFAULT = 1024;

/**
 * @brief Minimum granularity for scheduling (microseconds).
 */
constexpr u64 MIN_GRANULARITY_US = 750;

/**
 * @brief Target latency for CFS scheduling (microseconds).
 */
constexpr u64 TARGET_LATENCY_US = 6000;

/**
 * @brief Get weight for a nice value.
 *
 * @param nice Nice value (-20 to +19).
 * @return Weight for scheduling.
 */
inline u32 nice_to_weight(i8 nice) {
    // Convert nice (-20 to +19) to index (0 to 39)
    i32 idx = nice + 20;
    if (idx < 0)
        idx = 0;
    if (idx > 39)
        idx = 39;
    return NICE_TO_WEIGHT[idx];
}

/**
 * @brief Get inverse weight for a nice value.
 *
 * @param nice Nice value (-20 to +19).
 * @return Inverse weight for vruntime calculation.
 */
inline u32 nice_to_inverse_weight(i8 nice) {
    i32 idx = nice + 20;
    if (idx < 0)
        idx = 0;
    if (idx > 39)
        idx = 39;
    return NICE_TO_INVERSE_WEIGHT[idx];
}

/**
 * @brief Calculate vruntime delta from wall-clock delta.
 *
 * @details
 * vruntime_delta = wall_delta * (WEIGHT_DEFAULT / weight)
 *                = wall_delta * inverse_weight >> 32
 *
 * @param wall_delta_ns Wall clock time in nanoseconds.
 * @param nice Nice value of the task.
 * @return Virtual runtime delta.
 */
inline u64 calc_vruntime_delta(u64 wall_delta_ns, i8 nice) {
    u64 inv_weight = nice_to_inverse_weight(nice);
    return (wall_delta_ns * inv_weight) >> 22; // Scale down from 2^32
}

} // namespace cfs
