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

} // namespace task

// Include shared TaskInfo struct after defining flags (avoids macro conflict)
#include "../../include/viperos/task_info.hpp"

namespace task
{

// Stack sizes
/** @brief Size of each kernel stack in bytes. */
constexpr usize KERNEL_STACK_SIZE = 16 * 1024; // 16KB

// Time slice in timer ticks (10ms at 1000Hz)
/** @brief Default scheduler time slice in timer ticks. */
constexpr u32 TIME_SLICE_DEFAULT = 10;

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

    u32 time_slice; // Remaining time slice ticks
    u32 priority;   // Priority (lower = higher priority)

    Task *next; // Next task in queue (ready/wait queue)
    Task *prev; // Previous task in queue

    void *wait_channel; // What we're waiting on (for debugging)
    i32 exit_code;      // Exit code when task exits

    // User task fields
    struct ViperProcess *viper; // Associated viper (for user tasks) - opaque pointer
    u64 user_entry;             // User-mode entry point
    u64 user_stack;             // User-mode stack pointer
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
