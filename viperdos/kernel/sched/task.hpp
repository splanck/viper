//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: kernel/sched/task.hpp
// Purpose: Task structures and task management interface.
// Key invariants: Task IDs unique; context layout matches context.S.
// Ownership/Lifetime: Fixed task table; slots reused after reap.
// Links: kernel/sched/task.cpp, kernel/sched/context.S
//
//===----------------------------------------------------------------------===//

#pragma once

#include "../include/types.hpp"

/**
 * @file task.hpp
 * @brief Task structures and task management interface.
 *
 * @details
 * The task subsystem provides the kernel's notion of an executable unit of work
 * ("task"). Tasks are scheduled by the scheduler module and can be in various
 * lifecycle states (Ready, Running, Blocked, Exited).
 *
 * This header defines:
 * - Lightweight task state/flag constants.
 * - The saved CPU context format used by the context switch routine.
 * - A trap frame format for exceptions/interrupts (used for user-mode support).
 * - The `task::Task` structure and basic task management APIs.
 *
 * The current implementation focuses on kernel-mode tasks and cooperative
 * scheduling during bring-up; user-mode trap frames are present for future
 * expansion.
 */
namespace task
{

// Task states
/**
 * @brief Lifecycle state of a task.
 *
 * @details
 * - `Ready`: runnable and eligible to be scheduled.
 * - `Running`: currently executing on the CPU.
 * - `Blocked`: waiting on an event (IPC, timer, etc.).
 * - `Exited`: terminated; resources may be reclaimed.
 */
enum class TaskState : u8
{
    Invalid = 0,
    Ready,
    Running,
    Blocked,
    Exited
};

// Task flags
/** @brief Task runs in kernel privilege level (bring-up default). */
constexpr u32 TASK_FLAG_KERNEL = 1 << 0;
/** @brief Task is the idle task that runs when no other task is runnable. */
constexpr u32 TASK_FLAG_IDLE = 1 << 1;
/** @brief Task runs in user mode (EL0). */
constexpr u32 TASK_FLAG_USER = 1 << 2;

// Priority constants
/** @brief Highest priority (most urgent). */
constexpr u8 PRIORITY_HIGHEST = 0;
/** @brief Default priority for normal tasks. */
constexpr u8 PRIORITY_DEFAULT = 128;
/** @brief Lowest priority (idle task). */
constexpr u8 PRIORITY_LOWEST = 255;
/** @brief Number of priority queues in the scheduler. */
constexpr u8 NUM_PRIORITY_QUEUES = 8;
/** @brief Tasks per queue (256 priority levels / 8 queues). */
constexpr u8 PRIORITIES_PER_QUEUE = 32;

/**
 * @brief Scheduling policy for a task.
 *
 * @details
 * - SCHED_OTHER: Normal time-sharing scheduling (default)
 * - SCHED_FIFO: Real-time FIFO scheduling (no time slicing)
 * - SCHED_RR: Real-time round-robin scheduling (time sliced)
 *
 * Real-time tasks (SCHED_FIFO/SCHED_RR) always have priority over
 * SCHED_OTHER tasks regardless of priority level.
 */
enum class SchedPolicy : u8
{
    SCHED_OTHER = 0,    ///< Normal time-sharing (default)
    SCHED_FIFO = 1,     ///< Real-time FIFO (run until yield/block)
    SCHED_RR = 2,       ///< Real-time round-robin (time sliced)
    SCHED_DEADLINE = 3  ///< Deadline scheduler (EDF)
};

/** @brief Default real-time time slice in ticks (100ms for SCHED_RR). */
constexpr u32 RT_TIME_SLICE_DEFAULT = 100;

/** @brief Default CPU affinity mask (all CPUs allowed). */
constexpr u32 CPU_AFFINITY_ALL = 0xFFFFFFFF;

} // namespace task

// Include shared TaskInfo struct after defining flags (avoids macro conflict)
#include "../../include/viperdos/task_info.hpp"

namespace task
{

// Stack sizes
/** @brief Size of each kernel stack in bytes. */
constexpr usize KERNEL_STACK_SIZE = 16 * 1024; // 16KB

// Time slices in timer ticks (at 1000Hz = 1ms per tick)
// Higher priority tasks get larger slices for better throughput
// Lower priority tasks get smaller slices for better responsiveness

/** @brief Default scheduler time slice in timer ticks (10ms). */
constexpr u32 TIME_SLICE_DEFAULT = 10;

/** @brief Time slice per priority queue (ms per tick at 1000Hz).
 * Queue 0 (highest): 20ms
 * Queue 1: 18ms
 * Queue 2: 15ms
 * Queue 3: 12ms
 * Queue 4 (default): 10ms
 * Queue 5: 8ms
 * Queue 6: 5ms
 * Queue 7 (idle): 5ms
 */
constexpr u32 TIME_SLICE_BY_QUEUE[NUM_PRIORITY_QUEUES] = {
    20, // Queue 0 - highest priority (system tasks)
    18, // Queue 1
    15, // Queue 2
    12, // Queue 3
    10, // Queue 4 - default priority
    8,  // Queue 5
    5,  // Queue 6
    5,  // Queue 7 - lowest priority (idle)
};

/**
 * @brief Get time slice for a given priority level.
 * @param priority Task priority (0-255).
 * @return Time slice in timer ticks.
 */
inline u32 time_slice_for_priority(u8 priority)
{
    u8 queue = priority / PRIORITIES_PER_QUEUE;
    if (queue >= NUM_PRIORITY_QUEUES)
        queue = NUM_PRIORITY_QUEUES - 1;
    return TIME_SLICE_BY_QUEUE[queue];
}

// Maximum tasks
/** @brief Maximum number of tasks supported by the fixed task table. */
constexpr u32 MAX_TASKS = 64;

// Saved context for context switch (callee-saved registers)
// These are the registers that must be preserved across function calls
/**
 * @brief Minimal CPU context saved/restored during a context switch.
 *
 * @details
 * On AArch64, registers x19-x29 and x30 (LR) are callee-saved per the ABI.
 * The context switch routine saves these along with the stack pointer so that
 * tasks can resume exactly where they yielded/preempted.
 *
 * This structure's layout must match the offsets used in `context.S`.
 */
struct TaskContext
{
    u64 x19;
    u64 x20;
    u64 x21;
    u64 x22;
    u64 x23;
    u64 x24;
    u64 x25;
    u64 x26;
    u64 x27;
    u64 x28;
    u64 x29; // Frame pointer
    u64 x30; // Link register (return address)
    u64 sp;  // Stack pointer
};

// Trap frame saved on exception/interrupt entry
// This saves the complete CPU state for returning to user mode
/**
 * @brief Full CPU register frame for exception/interrupt returns.
 *
 * @details
 * Trap frames are used when tasks execute in user mode and need to return from
 * syscalls or handle faults/interrupts. The current kernel primarily uses
 * kernel-mode tasks, but the structure is defined here to support future EL0
 * execution and syscall return paths.
 */
struct TrapFrame
{
    u64 x[31]; // x0-x30
    u64 sp;    // Stack pointer (SP_EL0 for user tasks)
    u64 elr;   // Exception Link Register (return address)
    u64 spsr;  // Saved Program Status Register
};

// Forward declarations
struct Task;

// Task entry function type
/**
 * @brief Task entry point function signature.
 *
 * @details
 * Tasks created by @ref create begin execution at a trampoline that invokes the
 * entry function with the provided argument pointer.
 */
using TaskEntry = void (*)(void *arg);

// Task structure
/**
 * @brief Kernel task control block (TCB).
 *
 * @details
 * This structure holds scheduling and execution context for a task, including:
 * - A unique ID and human-readable name (for diagnostics).
 * - Scheduling state, flags, priority, and time slice accounting.
 * - Saved context for context switching.
 * - Pointers to kernel stack memory.
 * - Optional trap frame pointer used by syscall/interrupt paths.
 *
 * The task subsystem stores tasks in a fixed-size array; pointers remain stable
 * for the lifetime of the task slot.
 */
struct Task
{
    u32 id;          // Unique task ID
    char name[32];   // Task name for debugging
    TaskState state; // Current state
    u32 flags;       // Task flags

    TaskContext context;   // Saved context for context switch
    TrapFrame *trap_frame; // Trap frame pointer (for syscalls/interrupts)

    u8 *kernel_stack;     // Kernel stack base
    u8 *kernel_stack_top; // Kernel stack top (initial SP)

    u32 time_slice;     // Remaining time slice ticks
    u8 priority;        // Priority (0=highest, 255=lowest, default 128)
    SchedPolicy policy; // Scheduling policy (SCHED_OTHER, SCHED_FIFO, SCHED_RR)
    u32 cpu_affinity;   // CPU affinity mask (bit N = can run on CPU N)

    // CFS (Completely Fair Scheduler) fields
    u64 vruntime;  // Virtual runtime for CFS (nanoseconds, scaled by weight)
    i8 nice;       // Nice value (-20 to +19, default 0)

    // SCHED_DEADLINE fields (EDF - Earliest Deadline First)
    u64 dl_runtime;   // Maximum runtime per period (nanoseconds)
    u64 dl_deadline;  // Relative deadline (nanoseconds)
    u64 dl_period;    // Period length (nanoseconds)
    u64 dl_abs_deadline; // Absolute deadline (timer tick when deadline expires)

    Task *next; // Next task in queue (ready/wait queue)
    Task *prev; // Previous task in queue

    void *wait_channel; // What we're waiting on (for debugging)
    i32 exit_code;      // Exit code when task exits

    // Statistics
    u64 cpu_ticks;    // Total CPU ticks consumed
    u64 switch_count; // Number of times scheduled
    u32 parent_id;    // Parent task ID (0 for root tasks)

    // User task fields
    struct ViperProcess *viper; // Associated viper (for user tasks) - opaque pointer
    u64 user_entry;             // User-mode entry point
    u64 user_stack;             // User-mode stack pointer

    // Current working directory (256 bytes max path)
    char cwd[256];

    // Signal state (for user tasks with signal handlers)
    struct
    {
        u64 handlers[32];       ///< Signal handler addresses (0=SIG_DFL, 1=SIG_IGN)
        u32 handler_flags[32];  ///< Flags for each handler (SA_*)
        u32 handler_mask[32];   ///< Mask for each handler
        u32 blocked;            ///< Blocked signal mask
        u32 pending;            ///< Pending signals bitmap
        TrapFrame *saved_frame; ///< Saved trap frame for sigreturn
    } signals;
};

/**
 * @brief Initialize the task subsystem.
 *
 * @details
 * Resets the global task table and creates the idle task (task ID 0). The idle
 * task runs when no other task is ready and typically executes a low-power wait
 * loop.
 */
void init();

/**
 * @brief Create a new task.
 *
 * @details
 * Allocates a task slot and a kernel stack, initializes the task control block,
 * and prepares an initial @ref TaskContext that will jump to the assembly
 * `task_entry_trampoline` when first scheduled.
 *
 * The trampoline reads the entry function pointer and argument from the new
 * task's stack and calls the entry function. If the entry function returns, the
 * trampoline terminates the task via `task::exit(0)`.
 *
 * @param name Human-readable task name (for debugging).
 * @param entry Entry function pointer.
 * @param arg Argument passed to `entry`.
 * @param flags Task flags (kernel/idle/etc.).
 * @return Pointer to the created task, or `nullptr` on failure.
 */
Task *create(const char *name, TaskEntry entry, void *arg, u32 flags = 0);

/**
 * @brief Get the currently running task.
 *
 * @return Pointer to the current task, or `nullptr` if none is set.
 */
Task *current();

/**
 * @brief Set the current running task pointer.
 *
 * @details
 * Used by the scheduler when switching tasks. Most kernel code should use
 * @ref current rather than setting the pointer directly.
 *
 * @param t New current task.
 */
void set_current(Task *t);

/**
 * @brief Terminate the current task.
 *
 * @details
 * Marks the task exited and invokes the scheduler to select a new runnable task.
 * In the current design this is expected not to return to the exiting task.
 *
 * @param code Exit status code.
 */
void exit(i32 code);

/**
 * @brief Yield the CPU to the scheduler.
 *
 * @details
 * Requests a reschedule so another task may run. This is used both explicitly
 * by tasks (cooperative yielding) and implicitly by subsystems that block
 * waiting for events.
 */
void yield();

/**
 * @brief Set the priority of a task.
 *
 * @details
 * Updates the task's priority. If the task is currently in a ready queue,
 * it will be moved to the appropriate priority queue on the next schedule.
 * Priority 0 is highest, 255 is lowest. Default is 128.
 *
 * @param t Task to modify.
 * @param priority New priority value (0-255).
 * @return 0 on success, -1 on error.
 */
i32 set_priority(Task *t, u8 priority);

/**
 * @brief Get the priority of a task.
 *
 * @param t Task to query.
 * @return Priority value (0-255), or 255 if task is null.
 */
u8 get_priority(Task *t);

/**
 * @brief Set the scheduling policy of a task.
 *
 * @details
 * Changes the scheduling policy for a task. SCHED_FIFO and SCHED_RR
 * are real-time policies that always run before SCHED_OTHER tasks.
 *
 * @param t Task to modify.
 * @param policy New scheduling policy.
 * @return 0 on success, -1 on error.
 */
i32 set_policy(Task *t, SchedPolicy policy);

/**
 * @brief Get the scheduling policy of a task.
 *
 * @param t Task to query.
 * @return Scheduling policy, or SCHED_OTHER if task is null.
 */
SchedPolicy get_policy(Task *t);

/**
 * @brief Set the CPU affinity mask for a task.
 *
 * @param t Task to modify.
 * @param mask Bitmask of allowed CPUs (bit N = CPU N allowed).
 * @return 0 on success, -1 on error.
 */
i32 set_affinity(Task *t, u32 mask);

/**
 * @brief Get the CPU affinity mask for a task.
 *
 * @param t Task to query.
 * @return CPU affinity mask, or CPU_AFFINITY_ALL if task is null.
 */
u32 get_affinity(Task *t);

/**
 * @brief Set the nice value for a task.
 *
 * @details
 * Nice values range from -20 (highest priority) to +19 (lowest priority).
 * Default is 0. Used by CFS for weight-based scheduling.
 *
 * @param t Task to modify.
 * @param nice New nice value (-20 to +19).
 * @return 0 on success, -1 on error.
 */
i32 set_nice(Task *t, i8 nice);

/**
 * @brief Get the nice value for a task.
 *
 * @param t Task to query.
 * @return Nice value (-20 to +19), or 0 if task is null.
 */
i8 get_nice(Task *t);

/**
 * @brief Look up a task by its numeric ID.
 *
 * @param id Task ID.
 * @return Pointer to the task if found, otherwise `nullptr`.
 */
Task *get_by_id(u32 id);

/**
 * @brief Print human-readable information about a task to the serial console.
 *
 * @param t Task to print (may be `nullptr`).
 */
void print_info(Task *t);

/**
 * @brief Create a user-mode task.
 *
 * @details
 * Creates a task that will execute in EL0 (user mode). The task is associated
 * with a Viper process and will enter user mode when first scheduled.
 *
 * @param name Human-readable task name.
 * @param viper_ptr The viper (user process) this task belongs to (as void* to avoid circular deps).
 * @param entry User-mode entry point address.
 * @param stack User-mode stack pointer.
 * @return Pointer to the created task, or `nullptr` on failure.
 */
Task *create_user_task(const char *name, void *viper_ptr, u64 entry, u64 stack);

/**
 * @brief Enumerate active tasks into a user-provided buffer.
 *
 * @details
 * Iterates the task table and copies information about non-Invalid tasks into
 * the provided TaskInfo array. Used by the SYS_TASK_LIST syscall.
 *
 * @param buffer Output array to receive task information.
 * @param max_count Maximum number of entries the buffer can hold.
 * @return Number of tasks written to the buffer.
 */
u32 list_tasks(TaskInfo *buffer, u32 max_count);

/**
 * @brief Reap exited tasks and reclaim their resources.
 *
 * @details
 * Scans the task table for Exited tasks and frees their kernel stacks,
 * marking the task slots as Invalid for reuse. This should be called
 * periodically to prevent resource exhaustion.
 *
 * @return Number of tasks reaped.
 */
u32 reap_exited();

/**
 * @brief Destroy a specific task and reclaim its resources.
 *
 * @details
 * Immediately destroys the task regardless of state. Should only be called
 * for tasks that are not currently running or in the ready queue.
 *
 * @param t Task to destroy.
 */
void destroy(Task *t);

// Signal numbers are defined in signal.hpp (namespace sig)
// Use sig::SIGKILL, sig::SIGTERM, etc.

/**
 * @brief Send a signal to a task.
 *
 * @details
 * Sends the specified signal to the target task. Currently supported signals:
 * - SIGKILL (9): Immediately terminates the task
 * - SIGTERM (15): Terminates the task (same as SIGKILL for now)
 * - SIGSTOP (19): Not implemented (returns success)
 * - SIGCONT (18): Not implemented (returns success)
 *
 * @param pid Task ID to signal.
 * @param signal Signal number.
 * @return 0 on success, -1 if task not found.
 */
i32 kill(u32 pid, i32 signal);

/**
 * @brief Wake a blocked task.
 *
 * @details
 * If the task is Blocked, sets it to Ready and enqueues it on the scheduler.
 * This is used when killing blocked tasks or for external wakeup.
 *
 * @param t Task to wake.
 * @return true if task was woken, false if not blocked.
 */
bool wakeup(Task *t);

} // namespace task

// Assembly functions (extern "C" linkage)
extern "C"
{
    // Context switch: saves old context, loads new context
    /**
     * @brief Save the current task context and restore the next task context.
     *
     * @details
     * Implemented in `context.S`. Saves callee-saved registers and SP into
     * `old_ctx` and restores them from `new_ctx`, returning into the new task's
     * continuation address stored in x30.
     *
     * @param old_ctx Destination for the outgoing context.
     * @param new_ctx Source for the incoming context.
     */
    void context_switch(task::TaskContext *old_ctx, task::TaskContext *new_ctx);

    // Task entry trampoline
    /**
     * @brief Assembly trampoline that starts newly created tasks.
     *
     * @details
     * Implemented in `context.S`. Loads the entry function pointer and argument
     * from the new task's stack, calls it, and terminates the task if it returns.
     */
    void task_entry_trampoline();
}
