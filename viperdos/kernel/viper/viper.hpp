//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
#pragma once

/**
 * @file viper.hpp
 * @brief Viper process model and management API.
 *
 * @details
 * A "Viper" is ViperDOS' in-kernel representation of a user-space process.
 * Each Viper owns:
 * - An AArch64 EL0 address space (page tables + ASID).
 * - A capability table used to authorize access to kernel objects.
 * - A set of tasks/threads that execute within the process.
 *
 * The viper subsystem currently targets early bring-up and assumes a simple
 * global implementation:
 * - A fixed-size process table is used instead of dynamic allocation.
 * - The "current Viper" pointer is global (not per-CPU).
 * - Many resource limits and lifecycle transitions are tracked but not yet
 *   fully enforced.
 *
 * The API in this header is used by the loader, scheduler and syscall layer to
 * create processes, switch the current process context, and query/debug state.
 */

#include "../cap/table.hpp"
#include "../include/types.hpp"
#include "../mm/vma.hpp"
#include "../sched/wait.hpp"

// Forward declarations
namespace task {
struct Task;
}

namespace fs::vfs {
struct FDTable;
}

namespace viper {

/**
 * @brief Lifecycle state of a Viper process.
 *
 * @details
 * The state machine is intentionally minimal at this stage:
 * - @ref Invalid: unused table slot; not a valid process.
 * - @ref Creating: slot reserved and being initialized.
 * - @ref Running: fully constructed and eligible to run tasks.
 * - @ref Exiting: process is shutting down (future use).
 * - @ref Zombie: exited but still present for parent inspection (future use).
 */
enum class ViperState : u32 {
    Invalid = 0,
    Creating,
    Running,
    Exiting,
    Zombie,
};

/**
 * @brief In-kernel representation of a user-space process.
 *
 * @details
 * A Viper aggregates the process-wide state required to run user-mode code:
 * address-space identity, capability authority, process hierarchy, and basic
 * accounting (heap break and memory usage).
 *
 * Most fields are managed by the viper subsystem and are not intended to be
 * manipulated directly by unrelated subsystems. The structure is stored in a
 * fixed-size table; pointers to a Viper remain valid until @ref destroy marks
 * the slot invalid.
 */
struct Viper {
    // Identity
    u64 id;         /**< Monotonically increasing process identifier. */
    char name[32];  /**< Human-readable name (NUL-terminated, 31 chars max). */
    char args[256]; /**< Command-line arguments (NUL-terminated). */

    // Address space
    u64 ttbr0; /**< Physical address of the user TTBR0 root page table. */
    u16 asid;  /**< Address Space ID (ASID) used for TLB tagging. */

    // Capabilities
    cap::Table *cap_table; /**< Process capability table (owned by this Viper). */

    // File descriptors
    fs::vfs::FDTable *fd_table; /**< Per-process file descriptor table. */

    // Tasks belonging to this Viper
    task::Task *task_list; /**< Linked list of tasks/threads in the process. */
    u32 task_count;        /**< Number of tasks currently associated with this Viper. */

    // Process tree
    Viper *parent;       /**< Parent process, or `nullptr` for the root process. */
    Viper *first_child;  /**< Head of the singly-linked child list. */
    Viper *next_sibling; /**< Next child in the parent's list. */

    // State
    ViperState state; /**< Current lifecycle state. */
    i32 exit_code;    /**< Exit status for zombie collection. */

    // Process groups and sessions (POSIX job control)
    u64 pgid;               /**< Process group ID (0 means use own pid). */
    u64 sid;                /**< Session ID (0 means use own pid). */
    bool is_session_leader; /**< True if this process created its session. */

    // Wait queue for parent waiting on children
    sched::WaitQueue child_waiters; /**< Tasks waiting for this process's children to exit. */

    // Heap tracking
    u64 heap_start; /**< Base virtual address for the user heap region. */
    u64 heap_break; /**< Current program break (end of the heap). */
    u64 heap_max;   /**< Maximum heap address (heap_start + 64MB by default). */

    // mmap region tracking
    u64 mmap_next; /**< Next available virtual address for mmap allocations. */

    // Virtual memory areas for demand paging
    mm::VmaList vma_list; /**< VMA tracking for this process's address space. */

    // Resource limits
    u64 memory_used;  /**< Approximate memory usage accounting (bytes). */
    u64 memory_limit; /**< Configured memory limit for this process (bytes). */
    u32 handle_limit; /**< Maximum number of capability handles. */
    u32 task_limit;   /**< Maximum number of tasks/threads in this process. */

    // Capability bounding set - limits what rights this process can ever acquire
    u32 cap_bounding_set; /**< Bitmask of allowed capability rights. */

    // Global list linkage
    Viper *next_all; /**< Next Viper in the global doubly-linked list. */
    Viper *prev_all; /**< Previous Viper in the global doubly-linked list. */
};

/**
 * @brief User address space layout constants.
 *
 * @details
 * ViperDOS uses a simple fixed virtual layout for user processes (EL0). The
 * lower 2 GiB of virtual space are reserved for kernel identity mappings during
 * bring-up (implemented with large 1 GiB blocks). User space begins at 2 GiB to
 * avoid collisions with those block mappings.
 *
 * The layout values are used by the loader when choosing where to place PIE
 * binaries, heap/stack regions, and when defining default process limits.
 */
namespace layout {
// Code segment at 2GB (outside kernel's 1GB block region)
constexpr u64 USER_CODE_BASE = 0x0000'0000'8000'0000ULL; // 2GB

// Data segment at 3GB
constexpr u64 USER_DATA_BASE = 0x0000'0000'C000'0000ULL; // 3GB

// Heap starts at 4GB
constexpr u64 USER_HEAP_BASE = 0x0000'0001'0000'0000ULL; // 4GB

// mmap region starts at 8GB (grows upward)
constexpr u64 USER_MMAP_BASE = 0x0000'0002'0000'0000ULL; // 8GB

// Stack at top of user space (grows down)
constexpr u64 USER_STACK_TOP = 0x0000'7FFF'FFFF'0000ULL; // ~128TB
constexpr u64 USER_STACK_SIZE = 1 * 1024 * 1024;         // 1MB default stack
} // namespace layout

// Default limits
/** @brief Default per-process memory limit used during bring-up. */
constexpr u64 DEFAULT_MEMORY_LIMIT = 64 * 1024 * 1024; // 64MB
/** @brief Default capability handle limit. */
constexpr u32 DEFAULT_HANDLE_LIMIT = 1024;
/** @brief Default task/thread limit per process. */
constexpr u32 DEFAULT_TASK_LIMIT = 16;
/** @brief Maximum number of concurrently allocated Viper processes. */
constexpr u32 MAX_VIPERS = 64;

/**
 * @brief Resource limit identifiers for get_rlimit/set_rlimit.
 */
enum class ResourceLimit : u32 {
    Memory = 0,  /**< Memory usage in bytes. */
    Handles = 1, /**< Maximum capability handles. */
    Tasks = 2,   /**< Maximum tasks/threads. */
    Count = 3,   /**< Number of resource limit types. */
};

// Viper management functions
/**
 * @brief Initialize the viper subsystem.
 *
 * @details
 * Clears the global process table, resets process IDs, and initializes the ASID
 * allocator used by @ref AddressSpace. This must be called before creating any
 * processes.
 *
 * This routine is intended to run during early kernel initialization before
 * user processes are launched.
 */
void init();

/**
 * @brief Create a new Viper (user-space process).
 *
 * @details
 * Allocates a slot from the fixed-size process table and initializes the
 * process-wide resources:
 * - A fresh user address space (new page tables + ASID).
 * - A capability table used for handle-based access control.
 * - Parent/child linkage in the process hierarchy.
 *
 * The returned pointer is stable until @ref destroy is called on the process.
 *
 * @param parent Parent process, or `nullptr` to create a root process.
 * @param name Human-readable process name for diagnostics.
 * @return Newly created Viper on success, or `nullptr` if the table is full or
 *         resources could not be allocated.
 */
Viper *create(Viper *parent, const char *name);

/**
 * @brief Destroy a Viper and release its process-wide resources.
 *
 * @details
 * Tears down the process address space and capability table, unlinks the
 * process from global and parent lists, and marks the slot as invalid.
 *
 * Task cleanup is not fully implemented yet; callers should ensure no runnable
 * tasks remain for the process before destroying it.
 *
 * @param v Process to destroy.
 */
void destroy(Viper *v);

/**
 * @brief Get the current process.
 *
 * @details
 * Returns the viper subsystem's notion of the "current" process. During early
 * bring-up this is stored in a single global pointer; future versions should
 * store this per-CPU and/or per-task.
 *
 * @return Current Viper pointer, or `nullptr` if none is set.
 */
Viper *current();

/**
 * @brief Set the current process.
 *
 * @details
 * Updates the global "current Viper" pointer. This does not automatically
 * perform an address-space switch; users should call
 * @ref get_address_space and @ref switch_address_space as appropriate.
 *
 * @param v Process to mark as current (may be `nullptr`).
 */
void set_current(Viper *v);

/**
 * @brief Find a process by its numeric ID.
 *
 * @details
 * Searches the global list of active processes. Invalid table slots are not
 * returned.
 *
 * @param id Process identifier.
 * @return Pointer to the matching Viper, or `nullptr` if not found.
 */
Viper *find(u64 id);

/**
 * @brief Exit the current process with an exit code.
 *
 * @details
 * Sets the process state to ZOMBIE, stores the exit code, wakes any waiting
 * parent, and reparents children to init (viper ID 1).
 *
 * @param code Exit status code.
 */
void exit(i32 code);

/**
 * @brief Wait for a child process to exit.
 *
 * @details
 * If child_id is -1, waits for any child. If a matching ZOMBIE child exists,
 * reaps it immediately. Otherwise blocks the caller until a child exits.
 *
 * @param child_id Process ID to wait for, or -1 for any child.
 * @param status Output: exit status of the reaped child.
 * @return Process ID of reaped child on success, negative error on failure.
 */
i64 wait(i64 child_id, i32 *status);

/**
 * @brief Adjust the heap break for a process (sbrk implementation).
 *
 * @details
 * If increment == 0, returns current heap_break.
 * If increment > 0, allocates and maps new pages, extends heap_break.
 * If increment < 0, unmaps pages and shrinks heap_break.
 *
 * @param v Process whose heap to adjust.
 * @param increment Amount to adjust heap break by (can be negative).
 * @return Previous heap break on success, negative error code on failure.
 */
i64 do_sbrk(Viper *v, i64 increment);

/**
 * @brief Reap a zombie child process and free its resources.
 *
 * @param child The zombie child to reap.
 */
void reap(Viper *child);

/**
 * @brief Fork the current process using Copy-on-Write.
 *
 * @details
 * Creates a new child process that shares the parent's address space mappings
 * with copy-on-write semantics. Both processes' pages are marked read-only;
 * writes trigger a fault that copies the page.
 *
 * @return Child process on success, nullptr on failure.
 */
Viper *fork();

// Debug
/**
 * @brief Print a human-readable summary of a Viper to the serial console.
 *
 * @details
 * This routine is intended for diagnostics and debugging. It prints the
 * process name, ID, state, address-space identifiers, and basic accounting
 * information.
 *
 * @param v Process to print, or `nullptr` to print a placeholder.
 */
void print_info(Viper *v);

/**
 * @brief Get the capability table of the current process.
 *
 * @details
 * Convenience wrapper that returns `current()->cap_table`. If no current
 * process is set, returns `nullptr`.
 *
 * @return Pointer to the current process's capability table, or `nullptr`.
 */
cap::Table *current_cap_table();

// Get address space for a Viper
class AddressSpace;

/**
 * @brief Get the AddressSpace object for a process.
 *
 * @details
 * The viper subsystem stores AddressSpace objects in a parallel array indexed
 * by the process slot. This accessor resolves the given Viper pointer back to
 * its table index and returns a pointer to the corresponding AddressSpace.
 *
 * @param v Process whose address space should be returned.
 * @return AddressSpace pointer on success, or `nullptr` if `v` is invalid.
 */
AddressSpace *get_address_space(Viper *v);

// Process group and session management

/**
 * @brief Get the process group ID of a process.
 *
 * @param pid Process ID to query, or 0 for current process.
 * @return Process group ID on success, negative error on failure.
 */
i64 getpgid(u64 pid);

/**
 * @brief Set the process group ID of a process.
 *
 * @param pid Process ID to modify, or 0 for current process.
 * @param pgid New process group ID, or 0 to use the target process's PID.
 * @return 0 on success, negative error on failure.
 */
i64 setpgid(u64 pid, u64 pgid);

/**
 * @brief Get the session ID of a process.
 *
 * @param pid Process ID to query, or 0 for current process.
 * @return Session ID on success, negative error on failure.
 */
i64 getsid(u64 pid);

/**
 * @brief Create a new session with the calling process as leader.
 *
 * @details
 * The calling process becomes the session leader and the process group leader
 * of a new process group. The process must not already be a process group leader.
 *
 * @return New session ID on success, negative error on failure.
 */
i64 setsid();

/**
 * @brief Get the capability bounding set for a process.
 *
 * @param v Process to query.
 * @return Bounding set bitmask.
 */
u32 get_cap_bounding_set(Viper *v);

/**
 * @brief Drop rights from a process's capability bounding set.
 *
 * @details
 * This is an irreversible operation. Once rights are dropped from the bounding
 * set, the process can never acquire capabilities with those rights again, even
 * if offered via IPC.
 *
 * @param v Process to modify.
 * @param rights_to_drop Rights to remove from the bounding set.
 * @return 0 on success.
 */
i64 drop_cap_bounding_set(Viper *v, u32 rights_to_drop);

// Resource limit management

/**
 * @brief Get a resource limit for the current process.
 *
 * @param resource Which resource limit to query.
 * @return Current limit value, or negative error code.
 */
i64 get_rlimit(ResourceLimit resource);

/**
 * @brief Set a resource limit for the current process.
 *
 * @details
 * Limits can only be reduced, not increased (privilege dropping).
 * Setting a limit lower than current usage is allowed but will prevent
 * further resource acquisition.
 *
 * @param resource Which resource limit to modify.
 * @param new_limit New limit value.
 * @return 0 on success, negative error code on failure.
 */
i64 set_rlimit(ResourceLimit resource, u64 new_limit);

/**
 * @brief Get current resource usage for the current process.
 *
 * @param resource Which resource to query usage for.
 * @return Current usage value, or negative error code.
 */
i64 get_rusage(ResourceLimit resource);

/**
 * @brief Check if a resource limit would be exceeded.
 *
 * @details
 * Used internally before allocating resources.
 *
 * @param v Process to check.
 * @param resource Which resource limit to check.
 * @param amount Amount of resource to allocate.
 * @return true if allocation would exceed limit, false otherwise.
 */
bool would_exceed_rlimit(Viper *v, ResourceLimit resource, u64 amount);

} // namespace viper
