//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: include/viperos/task_info.hpp
// Purpose: Task metadata structures for SYS_TASK_LIST syscall.
// Key invariants: ABI-stable; state values mirror kernel task states.
// Ownership/Lifetime: Shared; included by kernel and user-space.
// Links: kernel/sched/task.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

/**
 * @file task_info.hpp
 * @brief Shared task enumeration structures for `SYS_TASK_LIST`.
 *
 * @details
 * This header defines the user/kernel ABI used to return a summary of running
 * tasks/processes to user-space. The kernel writes an array of @ref TaskInfo
 * entries into a caller-provided buffer.
 *
 * The intent is to expose a small, stable subset of scheduler/task metadata
 * suitable for diagnostic tools (e.g., a shell `Status` command). The fields
 * are intentionally simple and fixed-size so the structure can be consumed by
 * freestanding user-space without depending on libc or C++ standard library
 * types.
 *
 * The numeric constants in this file are shared with the kernel. When building
 * user-space (`__VIPEROS_USERSPACE__`), this header provides `TASK_FLAG_*`
 * macros for convenience. When building the kernel, equivalent values are
 * typically provided as `constexpr` values in the kernel task subsystem.
 */

/** @name Task State Values
 *  @brief Values stored in @ref TaskInfo::state.
 *
 *  @details
 *  These values mirror the kernel's internal task state machine. User-space
 *  should treat them as informational rather than as a synchronization
 *  primitive; the state can change at any time between reading the task list
 *  and displaying it.
 *  @{
 */
#define TASK_STATE_INVALID 0 /**< Entry is unused/invalid. */
#define TASK_STATE_READY 1   /**< Runnable and eligible for scheduling. */
#define TASK_STATE_RUNNING 2 /**< Currently executing on a CPU. */
#define TASK_STATE_BLOCKED 3 /**< Sleeping or waiting on an event. */
#define TASK_STATE_EXITED 4  /**< Task has terminated (may still be in table). */
/** @} */

/** @name Task Flags
 *  @brief Bitmask values stored in @ref TaskInfo::flags.
 *
 *  @details
 *  Flags provide a coarse classification of tasks. They are not permission
 *  bits; they are intended for display and debugging output.
 *
 *  The kernel provides its own definitions when building kernel code. The
 *  `__VIPEROS_USERSPACE__` guard avoids macro pollution and lets the kernel
 *  keep these as strongly typed constants.
 *  @{
 */
#ifdef __VIPEROS_USERSPACE__
#define TASK_FLAG_KERNEL (1 << 0) /**< Kernel task (runs in privileged mode). */
#define TASK_FLAG_IDLE (1 << 1)   /**< Idle task (runs when no other work). */
#define TASK_FLAG_USER (1 << 2)   /**< User task/process. */
#endif
/** @} */

/**
 * @brief Per-task metadata returned by `SYS_TASK_LIST`.
 *
 * @details
 * A caller typically allocates an array of @ref TaskInfo structures and asks
 * the kernel to populate it. Each entry describes one task:
 * - `id` is the stable numeric identifier used internally by the scheduler.
 * - `state` is one of `TASK_STATE_*` values.
 * - `flags` is a bitmask of `TASK_FLAG_*` values.
 * - `priority` is the scheduler priority (lower values represent higher
 *   priority).
 * - `name` is a fixed-size, NUL-terminated string for display.
 *
 * Because this is a snapshot, fields may change immediately after the syscall
 * returns. User-space should not assume the list is consistent with other
 * observations (e.g., a task might exit between listing and querying it).
 */
struct TaskInfo
{
    unsigned int id;         /**< Kernel task identifier. */
    unsigned char state;     /**< Task state (`TASK_STATE_*`). */
    unsigned char flags;     /**< Task flags (`TASK_FLAG_*`). */
    unsigned char priority;  /**< Scheduler priority (0–255, lower is higher). */
    unsigned char _pad0;     /**< Padding for alignment. */
    char name[32];           /**< NUL-terminated task name for display. */

    /* Extended fields (v2) */
    unsigned long long cpu_ticks;    /**< Total CPU ticks consumed. */
    unsigned long long switch_count; /**< Number of times scheduled. */
    unsigned int parent_id;          /**< Parent task ID (0 for root tasks). */
    int exit_code;                   /**< Exit code (valid if state == EXITED). */
};
