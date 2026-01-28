//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
#include "task.hpp"
#include "../../include/viperdos/task_info.hpp"
#include "../arch/aarch64/cpu.hpp"
#include "../arch/aarch64/exceptions.hpp"
#include "../console/serial.hpp"
#include "../include/constants.hpp"
#include "../ipc/poll.hpp"
#include "../lib/spinlock.hpp"
#include "../lib/str.hpp"
#include "../mm/vmm.hpp"
#include "../viper/address_space.hpp"
#include "../viper/viper.hpp"
#include "idle.hpp"
#include "scheduler.hpp"
#include "signal.hpp"
#include "wait.hpp"

// External function to enter user mode (from exceptions.S)
extern "C" [[noreturn]] void enter_user_mode(u64 entry, u64 stack, u64 arg);

/**
 * @file task.cpp
 * @brief Task subsystem implementation.
 *
 * @details
 * Tasks are stored in a global fixed-size array. Task creation allocates a
 * kernel stack from a simple fixed stack pool and sets up an initial context
 * that will enter `task_entry_trampoline` when scheduled the first time.
 *
 * The current implementation assumes kernel-mode execution for tasks and uses
 * cooperative scheduling. Task exit marks the task Exited and reschedules.
 */
namespace task {

namespace {
/**
 * @brief Lock protecting task table and stack pool operations.
 *
 * @details
 * This lock must be held when:
 * - Allocating or deallocating task slots
 * - Allocating or freeing kernel stacks
 * - Modifying next_task_id
 * - Modifying the task ID hash table
 *
 * This prevents race conditions on SMP systems where multiple CPUs
 * could otherwise allocate the same task slot or stack.
 */
Spinlock task_lock;

// Task storage
Task tasks[MAX_TASKS];
u32 next_task_id = 1;

// Task ID hash table for O(1) lookup
Task *task_hash_table[TASK_HASH_BUCKETS];

/**
 * @brief Compute hash bucket index for a task ID.
 * @param id Task ID to hash.
 * @return Bucket index (0 to TASK_HASH_BUCKETS-1).
 */
inline u32 task_hash(u32 id) {
    return id & (TASK_HASH_BUCKETS - 1);
}

/**
 * @brief Insert a task into the hash table.
 * @note Caller must hold task_lock.
 * @param t Task to insert.
 */
void hash_insert_locked(Task *t) {
    u32 bucket = task_hash(t->id);
    t->hash_next = task_hash_table[bucket];
    task_hash_table[bucket] = t;
}

/**
 * @brief Remove a task from the hash table.
 * @note Caller must hold task_lock.
 * @param t Task to remove.
 */
void hash_remove_locked(Task *t) {
    u32 bucket = task_hash(t->id);
    Task **pp = &task_hash_table[bucket];
    while (*pp) {
        if (*pp == t) {
            *pp = t->hash_next;
            t->hash_next = nullptr;
            return;
        }
        pp = &(*pp)->hash_next;
    }
}

/**
 * @brief Find a task by ID using the hash table.
 * @note Caller must hold task_lock.
 * @param id Task ID to find.
 * @return Task pointer or nullptr if not found.
 */
Task *hash_find_locked(u32 id) {
    u32 bucket = task_hash(id);
    Task *t = task_hash_table[bucket];
    while (t) {
        if (t->id == id && t->state != TaskState::Invalid) {
            return t;
        }
        t = t->hash_next;
    }
    return nullptr;
}

// Per-CPU current_task is stored in cpu::CpuData::current_task
// Accessor functions current() and set_current() use cpu::current() to get/set it

// Idle task (always task 0)
Task *idle_task = nullptr;

/**
 * @brief Initialize signal state to default values.
 *
 * @details
 * Sets all signal handlers to SIG_DFL (0), clears all handler flags and masks,
 * and resets the blocked and pending signal bitmaps.
 *
 * @param t Pointer to the task whose signal state should be initialized.
 */
void init_signal_state(Task *t) {
    for (int i = 0; i < 32; i++) {
        t->signals.handlers[i] = 0; // SIG_DFL
        t->signals.handler_flags[i] = 0;
        t->signals.handler_mask[i] = 0;
    }
    t->signals.blocked = 0;
    t->signals.pending = 0;
    t->signals.saved_frame = nullptr;
}

// Allocate a task slot
/**
 * @brief Find an unused task slot in the global task table.
 *
 * @note Caller must hold task_lock.
 * @return Pointer to an Invalid task slot, or `nullptr` if table is full.
 */
Task *allocate_task_locked() {
    for (u32 i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].state == TaskState::Invalid) {
            return &tasks[i];
        }
    }
    return nullptr;
}

// Allocate kernel stack (bump allocator with free list for recycling)
u8 *stack_pool = nullptr;
usize stack_pool_offset = 0;
constexpr usize STACK_SLOT_SIZE = KERNEL_STACK_SIZE + kc::limits::GUARD_PAGE_SIZE;
constexpr usize STACK_POOL_SIZE = STACK_SLOT_SIZE * MAX_TASKS;

// Free stack list for recycling exited task stacks
struct FreeStackNode {
    FreeStackNode *next;
};

FreeStackNode *free_stack_list = nullptr;
u32 free_stack_count = 0;

/**
 * @brief Allocate a kernel stack from a fixed pre-reserved pool.
 *
 * @details
 * This allocator uses a free list for recycling and falls back to a bump
 * allocator when the free list is empty. Each stack slot includes a 4KB guard
 * page at the bottom that is unmapped to catch stack overflows.
 *
 * Layout of each stack slot:
 * +-------------------+ <- stack_pool + offset
 * | Guard Page (4KB)  | <- Unmapped, access triggers page fault
 * +-------------------+ <- returned pointer (usable stack base)
 * | Stack (16KB)      | <- Usable stack space
 * +-------------------+ <- stack top (grows down from here)
 *
 * @note Caller must hold task_lock.
 * @return Pointer to the base of the usable stack, or `nullptr` on exhaustion.
 */
u8 *allocate_kernel_stack_locked() {
    // First try the free list
    if (free_stack_list) {
        FreeStackNode *node = free_stack_list;
        free_stack_list = node->next;
        free_stack_count--;

        // Return the stack (node is at the base of usable stack)
        return reinterpret_cast<u8 *>(node);
    }

    // Fall back to bump allocator
    // First call: initialize pool location
    if (stack_pool == nullptr) {
        stack_pool = reinterpret_cast<u8 *>(kc::mem::STACK_POOL_BASE);
        stack_pool_offset = 0;
    }

    if (stack_pool_offset + STACK_SLOT_SIZE > STACK_POOL_SIZE) {
        serial::puts("[task] ERROR: Stack pool exhausted\n");
        return nullptr;
    }

    u8 *slot_base = stack_pool + stack_pool_offset;
    stack_pool_offset += STACK_SLOT_SIZE;

    // Unmap the guard page to catch stack overflows
    // When the stack grows into this page, a page fault will occur
    u64 guard_page_addr = reinterpret_cast<u64>(slot_base);
    vmm::unmap_page(guard_page_addr);

    // Return pointer to usable stack (after guard page)
    u8 *usable_stack = slot_base + kc::limits::GUARD_PAGE_SIZE;

    return usable_stack;
}

/**
 * @brief Free a kernel stack, returning it to the free list for reuse.
 *
 * @note Caller must hold task_lock.
 * @param stack Pointer to the usable stack base (as returned by allocate_kernel_stack).
 */
void free_kernel_stack_locked(u8 *stack) {
    if (!stack)
        return;

    // Add to free list (use the stack memory itself for the node)
    FreeStackNode *node = reinterpret_cast<FreeStackNode *>(stack);
    node->next = free_stack_list;
    free_stack_list = node;
    free_stack_count++;
}

// Idle task function - just loops waiting for interrupts
/**
 * @brief Idle task body.
 *
 * @details
 * Runs when no other task is runnable. It executes `wfi` in a loop to reduce
 * power usage and to wait for interrupts to deliver new work.
 */
void idle_task_fn(void *) {
    while (true) {
        u32 cpu_id = cpu::current_id();
        idle::enter(cpu_id);
        asm volatile("wfi");
        idle::exit(cpu_id);
    }
}
} // namespace

/** @copydoc task::init */
void init() {
    serial::puts("[task] Initializing task subsystem\n");

    // Clear hash table
    for (u32 i = 0; i < TASK_HASH_BUCKETS; i++) {
        task_hash_table[i] = nullptr;
    }

    // Clear all task slots
    for (u32 i = 0; i < MAX_TASKS; i++) {
        tasks[i].state = TaskState::Invalid;
        tasks[i].id = 0;
        tasks[i].hash_next = nullptr;
    }

    // Create idle task (special - uses task slot 0)
    idle_task = &tasks[0];
    idle_task->id = 0;
    lib::strcpy_safe(idle_task->name, "idle", sizeof(idle_task->name));
    idle_task->state = TaskState::Ready;
    idle_task->flags = TASK_FLAG_KERNEL | TASK_FLAG_IDLE;
    idle_task->time_slice = TIME_SLICE_DEFAULT;
    idle_task->priority = PRIORITY_LOWEST; // Lowest priority
    idle_task->original_priority = PRIORITY_LOWEST;
    idle_task->cpu_affinity = CPU_AFFINITY_ALL;
    idle_task->vruntime = 0;
    idle_task->nice = 0;
    idle_task->dl_runtime = 0;
    idle_task->dl_deadline = 0;
    idle_task->dl_period = 0;
    idle_task->dl_abs_deadline = 0;
    idle_task->dl_missed = 0;
    idle_task->dl_flags = 0;
    idle_task->bw_runtime = 0;
    idle_task->bw_period = 0;
    idle_task->bw_consumed = 0;
    idle_task->bw_period_start = 0;
    idle_task->bw_throttled = false;
    idle_task->next = nullptr;
    idle_task->prev = nullptr;
    idle_task->heap_index = static_cast<u32>(-1);
    // Acquire lock for consistency (though init runs single-threaded)
    u64 saved_daif = task_lock.acquire();
    idle_task->kernel_stack = allocate_kernel_stack_locked();
    task_lock.release(saved_daif);
    idle_task->kernel_stack_top = idle_task->kernel_stack + KERNEL_STACK_SIZE;
    idle_task->trap_frame = nullptr;
    idle_task->wait_channel = nullptr;
    idle_task->blocked_mutex = nullptr;
    idle_task->wait_timeout = 0;
    idle_task->exit_code = 0;
    idle_task->cpu_ticks = 0;
    idle_task->switch_count = 0;
    idle_task->parent_id = 0;
    idle_task->viper = nullptr;
    idle_task->user_entry = 0;
    idle_task->user_stack = 0;

    // Initialize CWD to root
    idle_task->cwd[0] = '/';
    idle_task->cwd[1] = '\0';

    // Initialize signal state (idle task doesn't use signals, but initialize anyway)
    init_signal_state(idle_task);

    // Set up idle task context to run idle_task_fn
    u64 *stack_ptr = reinterpret_cast<u64 *>(idle_task->kernel_stack_top);
    stack_ptr -= 2;
    stack_ptr[0] = reinterpret_cast<u64>(idle_task_fn);
    stack_ptr[1] = 0; // arg = nullptr

    idle_task->context.x30 = reinterpret_cast<u64>(task_entry_trampoline);
    idle_task->context.sp = reinterpret_cast<u64>(stack_ptr);
    idle_task->context.x29 = 0;
    idle_task->context.x19 = 0;
    idle_task->context.x20 = 0;
    idle_task->context.x21 = 0;
    idle_task->context.x22 = 0;
    idle_task->context.x23 = 0;
    idle_task->context.x24 = 0;
    idle_task->context.x25 = 0;
    idle_task->context.x26 = 0;
    idle_task->context.x27 = 0;
    idle_task->context.x28 = 0;
    idle_task->hash_next = nullptr;

    // Insert idle task into hash table
    hash_insert_locked(idle_task);

    // Set current task to idle initially
    set_current(idle_task);

    serial::puts("[task] Task subsystem initialized\n");
}

/** @copydoc task::create */
Task *create(const char *name, TaskEntry entry, void *arg, u32 flags) {
    // Acquire task_lock for allocation operations
    u64 saved_daif = task_lock.acquire();

    Task *t = allocate_task_locked();
    if (!t) {
        task_lock.release(saved_daif);
        serial::puts("[task] ERROR: No free task slots\n");
        return nullptr;
    }

    // Allocate kernel stack
    t->kernel_stack = allocate_kernel_stack_locked();
    if (!t->kernel_stack) {
        // Release the task slot we just allocated
        t->state = TaskState::Invalid;
        t->id = 0;
        task_lock.release(saved_daif);
        serial::puts("[task] ERROR: Failed to allocate kernel stack\n");
        return nullptr;
    }
    t->kernel_stack_top = t->kernel_stack + KERNEL_STACK_SIZE;

    // Initialize task fields - get next_task_id while holding lock
    t->id = next_task_id++;
    t->hash_next = nullptr;

    // Insert into hash table before releasing lock
    hash_insert_locked(t);

    // Release lock before the rest of initialization
    task_lock.release(saved_daif);
    lib::strcpy_safe(t->name, name, sizeof(t->name));
    t->state = TaskState::Ready;
    t->flags = flags | TASK_FLAG_KERNEL; // All tasks are kernel tasks for now
    t->time_slice = TIME_SLICE_DEFAULT;
    t->priority = PRIORITY_DEFAULT;
    t->original_priority = PRIORITY_DEFAULT;
    t->policy = SchedPolicy::SCHED_OTHER; // Default to normal scheduling
    t->cpu_affinity = CPU_AFFINITY_ALL;   // Can run on any CPU
    t->vruntime = 0;                      // Start with zero vruntime (CFS)
    t->nice = 0;                          // Default nice value (CFS)
    t->dl_runtime = 0;                    // SCHED_DEADLINE: no deadline by default
    t->dl_deadline = 0;
    t->dl_period = 0;
    t->dl_abs_deadline = 0;
    t->dl_missed = 0;
    t->dl_flags = 0;
    t->bw_runtime = 0; // No bandwidth limit by default
    t->bw_period = 0;
    t->bw_consumed = 0;
    t->bw_period_start = 0;
    t->bw_throttled = false;
    t->next = nullptr;
    t->prev = nullptr;
    t->heap_index = static_cast<u32>(-1); // Not in any heap
    t->wait_channel = nullptr;
    t->blocked_mutex = nullptr;
    t->wait_timeout = 0;
    t->exit_code = 0;
    t->trap_frame = nullptr;
    t->cpu_ticks = 0;
    t->switch_count = 0;
    {
        Task *curr = current();
        t->parent_id = curr ? curr->id : 0;
    }

    // Set up initial context
    // The stack grows downward, so we start at the top
    // We need to set up the stack so that when context_switch loads
    // this context and returns (via x30), it jumps to task_entry_trampoline

    // Reserve space on stack for entry arguments
    // Stack layout (growing down):
    //   [top]
    //   arg (void*)
    //   entry (TaskEntry)
    //   <-- initial SP points here

    u64 *stack_ptr = reinterpret_cast<u64 *>(t->kernel_stack_top);

    // Push entry function and argument onto stack
    // These will be picked up by task_entry_trampoline
    stack_ptr -= 2;
    stack_ptr[0] = reinterpret_cast<u64>(entry);
    stack_ptr[1] = reinterpret_cast<u64>(arg);

    // Initialize context
    // x30 (LR) points to trampoline - when context_switch returns via ret,
    // it will jump to task_entry_trampoline
    t->context.x30 = reinterpret_cast<u64>(task_entry_trampoline);
    t->context.sp = reinterpret_cast<u64>(stack_ptr);
    t->context.x29 = 0; // Frame pointer

    // Clear callee-saved registers
    t->context.x19 = 0;
    t->context.x20 = 0;
    t->context.x21 = 0;
    t->context.x22 = 0;
    t->context.x23 = 0;
    t->context.x24 = 0;
    t->context.x25 = 0;
    t->context.x26 = 0;
    t->context.x27 = 0;
    t->context.x28 = 0;

    // Initialize user task fields to null
    t->viper = nullptr;
    t->user_entry = 0;
    t->user_stack = 0;

    // Initialize CWD - inherit from parent if exists, otherwise root
    {
        Task *curr = current();
        if (curr && curr->cwd[0]) {
            lib::strcpy_safe(t->cwd, curr->cwd, sizeof(t->cwd));
        } else {
            t->cwd[0] = '/';
            t->cwd[1] = '\0';
        }
    }

    // Initialize signal state (kernel tasks don't typically use signals)
    init_signal_state(t);

    return t;
}

/**
 * @brief Entry trampoline for user-mode tasks.
 *
 * @details
 * This function is called when a user task is first scheduled. It switches
 * to the user's address space and enters user mode via eret.
 */
static void user_task_entry_trampoline(void *) {
    Task *t = current();
    if (!t || !t->viper) {
        serial::puts("[task] PANIC: user_task_entry_trampoline with invalid task/viper\n");
        for (;;)
            asm volatile("wfi");
    }

    serial::puts("[task] User task '");
    serial::puts(t->name);
    serial::puts("' entering user mode\n");

    // Cast viper pointer
    ::viper::Viper *v = reinterpret_cast<::viper::Viper *>(t->viper);

    // Switch to the user's address space
    ::viper::switch_address_space(v->ttbr0, v->asid);

    // Flush TLB for the new ASID
    asm volatile("tlbi aside1is, %0" ::"r"(static_cast<u64>(v->asid) << 48));
    asm volatile("dsb sy");
    asm volatile("isb");

    // Set current viper
    ::viper::set_current(v);

    // Set per-thread TLS pointer (TPIDR_EL0) if set
    if (t->thread.tls_base) {
        asm volatile("msr tpidr_el0, %0" ::"r"(t->thread.tls_base));
    }

    // Enter user mode - this won't return
    enter_user_mode(t->user_entry, t->user_stack, 0);

    // Should never reach here
    serial::puts("[task] PANIC: enter_user_mode returned!\n");
    for (;;)
        asm volatile("wfi");
}

/** @copydoc task::create_user_task */
Task *create_user_task(const char *name, void *viper_ptr, u64 entry, u64 stack) {
    // DEBUG: Check TTBR0 before accessing name
    u64 ttbr0_val;
    asm volatile("mrs %0, ttbr0_el1" : "=r"(ttbr0_val));
    serial::puts("[task] create_user_task: name=");
    serial::put_hex(reinterpret_cast<u64>(name));
    serial::puts(", ttbr0=");
    serial::put_hex(ttbr0_val);
    serial::puts("\n");

    // Acquire task_lock for allocation operations
    u64 saved_daif = task_lock.acquire();

    Task *t = allocate_task_locked();
    if (!t) {
        task_lock.release(saved_daif);
        serial::puts("[task] ERROR: No free task slots for user task\n");
        return nullptr;
    }

    // Allocate kernel stack (user tasks still need one for syscalls)
    t->kernel_stack = allocate_kernel_stack_locked();
    if (!t->kernel_stack) {
        // Release the task slot we just allocated
        t->state = TaskState::Invalid;
        t->id = 0;
        task_lock.release(saved_daif);
        serial::puts("[task] ERROR: Failed to allocate kernel stack for user task\n");
        return nullptr;
    }
    t->kernel_stack_top = t->kernel_stack + KERNEL_STACK_SIZE;

    // Initialize task fields - get next_task_id while holding lock
    t->id = next_task_id++;
    t->hash_next = nullptr;

    // Insert into hash table before releasing lock
    hash_insert_locked(t);

    // Release lock before the rest of initialization
    task_lock.release(saved_daif);
    lib::strcpy_safe(t->name, name, sizeof(t->name));
    t->state = TaskState::Ready;
    t->flags = TASK_FLAG_USER; // User task, not kernel
    t->time_slice = TIME_SLICE_DEFAULT;
    t->priority = PRIORITY_DEFAULT;
    t->original_priority = PRIORITY_DEFAULT;
    t->policy = SchedPolicy::SCHED_OTHER; // Default to normal scheduling
    t->cpu_affinity = CPU_AFFINITY_ALL;   // Can run on any CPU
    t->vruntime = 0;                      // Start with zero vruntime (CFS)
    t->nice = 0;                          // Default nice value (CFS)
    t->dl_runtime = 0;                    // SCHED_DEADLINE: no deadline by default
    t->dl_deadline = 0;
    t->dl_period = 0;
    t->dl_abs_deadline = 0;
    t->dl_missed = 0;
    t->dl_flags = 0;
    t->bw_runtime = 0; // No bandwidth limit by default
    t->bw_period = 0;
    t->bw_consumed = 0;
    t->bw_period_start = 0;
    t->bw_throttled = false;
    t->next = nullptr;
    t->prev = nullptr;
    t->heap_index = static_cast<u32>(-1); // Not in any heap
    t->wait_channel = nullptr;
    t->blocked_mutex = nullptr;
    t->wait_timeout = 0;
    t->exit_code = 0;
    t->trap_frame = nullptr;
    t->cpu_ticks = 0;
    t->switch_count = 0;
    {
        Task *curr = current();
        t->parent_id = curr ? curr->id : 0;
    }

    // Set up user task fields
    t->viper = reinterpret_cast<struct ViperProcess *>(viper_ptr);
    t->user_entry = entry;
    t->user_stack = stack;

    // Initialize CWD - inherit from parent if exists, otherwise root
    {
        Task *curr = current();
        if (curr && curr->cwd[0]) {
            lib::strcpy_safe(t->cwd, curr->cwd, sizeof(t->cwd));
        } else {
            t->cwd[0] = '/';
            t->cwd[1] = '\0';
        }
    }

    // Initialize signal state for user task
    init_signal_state(t);

    // Initialize thread state (main task is not a thread)
    t->thread.is_thread = false;
    t->thread.detached = false;
    t->thread.joined = false;
    t->thread.retval = 0;
    t->thread.tls_base = 0;
    t->thread.join_waiters = nullptr;

    // Set up initial context to call user_task_entry_trampoline
    u64 *stack_ptr = reinterpret_cast<u64 *>(t->kernel_stack_top);
    stack_ptr -= 2;
    stack_ptr[0] = reinterpret_cast<u64>(user_task_entry_trampoline);
    stack_ptr[1] = 0; // arg = nullptr (we use current() instead)

    t->context.x30 = reinterpret_cast<u64>(task_entry_trampoline);
    t->context.sp = reinterpret_cast<u64>(stack_ptr);
    t->context.x29 = 0;

    // Clear callee-saved registers
    t->context.x19 = 0;
    t->context.x20 = 0;
    t->context.x21 = 0;
    t->context.x22 = 0;
    t->context.x23 = 0;
    t->context.x24 = 0;
    t->context.x25 = 0;
    t->context.x26 = 0;
    t->context.x27 = 0;
    t->context.x28 = 0;

    serial::puts("[task] Created user task '");
    serial::puts(name);
    serial::puts("' (id=");
    serial::put_dec(t->id);
    serial::puts(", entry=");
    serial::put_hex(entry);
    serial::puts(")\n");

    return t;
}

/** @copydoc task::create_thread */
Task *create_thread(const char *name, void *viper_ptr, u64 entry, u64 stack, u64 tls_base) {
    ::viper::Viper *v = reinterpret_cast<::viper::Viper *>(viper_ptr);

    // Check thread limit
    if (v->task_count >= v->task_limit) {
        serial::puts("[task] ERROR: Thread limit reached for viper\n");
        return nullptr;
    }

    // Acquire task_lock for allocation operations
    u64 saved_daif = task_lock.acquire();

    Task *t = allocate_task_locked();
    if (!t) {
        task_lock.release(saved_daif);
        serial::puts("[task] ERROR: No free task slots for thread\n");
        return nullptr;
    }

    // Allocate kernel stack
    t->kernel_stack = allocate_kernel_stack_locked();
    if (!t->kernel_stack) {
        t->state = TaskState::Invalid;
        t->id = 0;
        task_lock.release(saved_daif);
        serial::puts("[task] ERROR: Failed to allocate kernel stack for thread\n");
        return nullptr;
    }
    t->kernel_stack_top = t->kernel_stack + KERNEL_STACK_SIZE;

    // Initialize task fields
    t->id = next_task_id++;
    t->hash_next = nullptr;
    hash_insert_locked(t);
    task_lock.release(saved_daif);

    lib::strcpy_safe(t->name, name, sizeof(t->name));
    t->state = TaskState::Ready;
    t->flags = TASK_FLAG_USER;
    t->time_slice = TIME_SLICE_DEFAULT;
    t->priority = PRIORITY_DEFAULT;
    t->original_priority = PRIORITY_DEFAULT;
    t->policy = SchedPolicy::SCHED_OTHER;
    t->cpu_affinity = CPU_AFFINITY_ALL;
    t->vruntime = 0;
    t->nice = 0;
    t->dl_runtime = 0;
    t->dl_deadline = 0;
    t->dl_period = 0;
    t->dl_abs_deadline = 0;
    t->dl_missed = 0;
    t->dl_flags = 0;
    t->bw_runtime = 0;
    t->bw_period = 0;
    t->bw_consumed = 0;
    t->bw_period_start = 0;
    t->bw_throttled = false;
    t->next = nullptr;
    t->prev = nullptr;
    t->heap_index = static_cast<u32>(-1);
    t->wait_channel = nullptr;
    t->blocked_mutex = nullptr;
    t->wait_timeout = 0;
    t->exit_code = 0;
    t->trap_frame = nullptr;
    t->cpu_ticks = 0;
    t->switch_count = 0;
    {
        Task *curr = current();
        t->parent_id = curr ? curr->id : 0;
    }

    // Share the same viper as the parent
    t->viper = reinterpret_cast<struct ViperProcess *>(viper_ptr);
    t->user_entry = entry;
    t->user_stack = stack;

    // Inherit CWD from current task
    {
        Task *curr = current();
        if (curr && curr->cwd[0]) {
            lib::strcpy_safe(t->cwd, curr->cwd, sizeof(t->cwd));
        } else {
            t->cwd[0] = '/';
            t->cwd[1] = '\0';
        }
    }

    // Initialize signal state
    init_signal_state(t);

    // Initialize thread state
    t->thread.is_thread = true;
    t->thread.detached = false;
    t->thread.joined = false;
    t->thread.retval = 0;
    t->thread.tls_base = tls_base;

    // Allocate join wait queue
    static sched::WaitQueue thread_wait_queues[MAX_TASKS];
    static u32 next_wq = 0;
    if (next_wq < MAX_TASKS) {
        sched::WaitQueue *wq = &thread_wait_queues[next_wq++];
        sched::wait_init(wq);
        t->thread.join_waiters = wq;
    } else {
        t->thread.join_waiters = nullptr;
    }

    // Increment process thread count
    v->task_count++;

    // Set up initial context to call user_task_entry_trampoline
    u64 *stack_ptr = reinterpret_cast<u64 *>(t->kernel_stack_top);
    stack_ptr -= 2;
    stack_ptr[0] = reinterpret_cast<u64>(user_task_entry_trampoline);
    stack_ptr[1] = 0;

    t->context.x30 = reinterpret_cast<u64>(task_entry_trampoline);
    t->context.sp = reinterpret_cast<u64>(stack_ptr);
    t->context.x29 = 0;
    t->context.x19 = 0;
    t->context.x20 = 0;
    t->context.x21 = 0;
    t->context.x22 = 0;
    t->context.x23 = 0;
    t->context.x24 = 0;
    t->context.x25 = 0;
    t->context.x26 = 0;
    t->context.x27 = 0;
    t->context.x28 = 0;

    serial::puts("[task] Created thread '");
    serial::puts(name);
    serial::puts("' (id=");
    serial::put_dec(t->id);
    serial::puts(", tls=");
    serial::put_hex(tls_base);
    serial::puts(")\n");

    return t;
}

/** @copydoc task::current */
Task *current() {
    return static_cast<Task *>(cpu::current()->current_task);
}

/** @copydoc task::set_current */
void set_current(Task *t) {
    cpu::current()->current_task = t;
}

/** @copydoc task::exit */
void exit(i32 code) {
    Task *t = current();
    if (!t)
        return;

    serial::puts("[task] Task '");
    serial::puts(t->name);
    serial::puts("' exiting with code ");
    serial::put_dec(code);
    serial::puts("\n");

    // Clear any poll/timer waiters referencing this task to prevent use-after-free
    poll::clear_task_waiters(t);

    // If this is a user task with an associated viper process
    if (t->viper) {
        if (t->thread.is_thread) {
            // Thread exit: store return value and wake joiners, don't kill the process
            t->thread.retval = static_cast<u64>(code);
            if (t->thread.join_waiters) {
                sched::wait_wake_all(static_cast<sched::WaitQueue *>(t->thread.join_waiters));
            }
            // Decrement process thread count
            ::viper::Viper *v = reinterpret_cast<::viper::Viper *>(t->viper);
            if (v->task_count > 0)
                v->task_count--;
        } else {
            // Main task exit: exit the whole process
            ::viper::exit(code);
        }
    }

    t->exit_code = code;
    t->state = TaskState::Exited;

    // Schedule next task
    scheduler::schedule();

    // Should never get here
    serial::puts("[task] PANIC: exit() returned after schedule!\n");
    while (true) {
        asm volatile("wfi");
    }
}

/** @copydoc task::yield */
void yield() {
    scheduler::schedule();
}

/** @copydoc task::set_priority */
i32 set_priority(Task *t, u8 priority) {
    if (!t)
        return -1;

    // Don't allow changing idle task priority
    if (t->flags & TASK_FLAG_IDLE)
        return -1;

    t->priority = priority;
    return 0;
}

/** @copydoc task::get_priority */
u8 get_priority(Task *t) {
    if (!t)
        return PRIORITY_LOWEST;
    return t->priority;
}

/** @copydoc task::set_policy */
i32 set_policy(Task *t, SchedPolicy policy) {
    if (!t)
        return -1;

    // Validate policy
    if (policy != SchedPolicy::SCHED_OTHER && policy != SchedPolicy::SCHED_FIFO &&
        policy != SchedPolicy::SCHED_RR) {
        return -1;
    }

    t->policy = policy;

    // Adjust time slice based on policy
    if (policy == SchedPolicy::SCHED_FIFO) {
        // SCHED_FIFO doesn't use time slicing - set max to indicate "run until yield"
        t->time_slice = 0xFFFFFFFF;
    } else if (policy == SchedPolicy::SCHED_RR) {
        // SCHED_RR uses a fixed RT time slice
        t->time_slice = RT_TIME_SLICE_DEFAULT;
    } else {
        // SCHED_OTHER uses priority-based time slicing
        t->time_slice = time_slice_for_priority(t->priority);
    }

    return 0;
}

/** @copydoc task::get_policy */
SchedPolicy get_policy(Task *t) {
    if (!t)
        return SchedPolicy::SCHED_OTHER;
    return t->policy;
}

/** @copydoc task::set_affinity */
i32 set_affinity(Task *t, u32 mask) {
    if (!t)
        return -1;

    // Must have at least one CPU allowed
    if (mask == 0)
        return -1;

    t->cpu_affinity = mask;
    return 0;
}

/** @copydoc task::get_affinity */
u32 get_affinity(Task *t) {
    if (!t)
        return CPU_AFFINITY_ALL;
    return t->cpu_affinity;
}

/** @copydoc task::set_nice */
i32 set_nice(Task *t, i8 nice) {
    if (!t)
        return -1;

    // Clamp to valid range (-20 to +19)
    if (nice < -20)
        nice = -20;
    if (nice > 19)
        nice = 19;

    t->nice = nice;
    return 0;
}

/** @copydoc task::get_nice */
i8 get_nice(Task *t) {
    if (!t)
        return 0;
    return t->nice;
}

/** @copydoc task::get_by_id */
Task *get_by_id(u32 id) {
    // Use hash table for O(1) lookup
    u64 saved_daif = task_lock.acquire();
    Task *t = hash_find_locked(id);
    task_lock.release(saved_daif);
    return t;
}

/** @copydoc task::print_info */
void print_info(Task *t) {
    if (!t) {
        serial::puts("[task] (null task)\n");
        return;
    }

    serial::puts("[task] Task ID ");
    serial::put_dec(t->id);
    serial::puts(" '");
    serial::puts(t->name);
    serial::puts("' state=");

    switch (t->state) {
        case TaskState::Invalid:
            serial::puts("Invalid");
            break;
        case TaskState::Ready:
            serial::puts("Ready");
            break;
        case TaskState::Running:
            serial::puts("Running");
            break;
        case TaskState::Blocked:
            serial::puts("Blocked");
            break;
        case TaskState::Exited:
            serial::puts("Exited");
            break;
    }

    serial::puts(" stack=");
    serial::put_hex(reinterpret_cast<u64>(t->kernel_stack));
    serial::puts("\n");
}

/** @copydoc task::list_tasks */
u32 list_tasks(TaskInfo *buffer, u32 max_count) {
    if (!buffer || max_count == 0) {
        return 0;
    }

    Task *curr = current();
    u32 count = 0;

    // Check if there's a current viper (user process)
    // Only list it separately if the current task isn't a user task
    // (to avoid duplication when using proper scheduled user tasks)
    ::viper::Viper *curr_viper = ::viper::current();
    bool have_user_task = (curr && (curr->flags & TASK_FLAG_USER) && curr->viper);

    if (curr_viper && !have_user_task && count < max_count) {
        // Legacy path: viper running without proper task integration
        TaskInfo &info = buffer[count];
        info.id = static_cast<u32>(curr_viper->id);
        info.state = static_cast<u8>(TaskState::Running);
        info.flags = TASK_FLAG_USER;
        info.priority = 128;
        info._pad0 = 0;

        for (usize j = 0; j < 31 && curr_viper->name[j]; j++) {
            info.name[j] = curr_viper->name[j];
        }
        info.name[31] = '\0';

        // Extended fields (not tracked for legacy vipers)
        info.cpu_ticks = 0;
        info.switch_count = 0;
        info.parent_id = 0;
        info.exit_code = 0;

        count++;
    }

    // Enumerate all tasks
    for (u32 i = 0; i < MAX_TASKS && count < max_count; i++) {
        Task &t = tasks[i];
        if (t.state != TaskState::Invalid) {
            TaskInfo &info = buffer[count];
            info.id = t.id;
            // If this is the current task and no viper is running, report it as Running
            if (&t == curr && !curr_viper) {
                info.state = static_cast<u8>(TaskState::Running);
            } else {
                info.state = static_cast<u8>(t.state);
            }
            info.flags = static_cast<u8>(t.flags);
            info.priority = static_cast<u8>(t.priority);
            info._pad0 = 0;

            // Copy name
            for (usize j = 0; j < 31 && t.name[j]; j++) {
                info.name[j] = t.name[j];
            }
            info.name[31] = '\0';

            // Extended fields
            info.cpu_ticks = t.cpu_ticks;
            info.switch_count = t.switch_count;
            info.parent_id = t.parent_id;
            info.exit_code = t.exit_code;

            count++;
        }
    }

    return count;
}

/**
 * @brief Reap exited tasks and reclaim their resources.
 *
 * @details
 * Scans the task table for Exited tasks and:
 * - Frees their kernel stacks
 * - Marks the task slot as Invalid for reuse
 *
 * This should be called periodically (e.g., from the idle task or timer interrupt)
 * to prevent resource exhaustion from accumulated exited tasks.
 *
 * @return Number of tasks reaped.
 */
u32 reap_exited() {
    u32 reaped = 0;

    for (u32 i = 0; i < MAX_TASKS; i++) {
        Task &t = tasks[i];

        // Don't reap idle task
        if (i == 0)
            continue;

        // Don't reap current task (shouldn't be exited anyway)
        if (&t == current())
            continue;

        if (t.state == TaskState::Exited) {
            serial::puts("[task] Reaping exited task '");
            serial::puts(t.name);
            serial::puts("' (id=");
            serial::put_dec(t.id);
            serial::puts(")\n");

            // Acquire lock for hash table and stack pool operations
            u64 saved_daif = task_lock.acquire();

            // Remove from hash table before clearing ID
            hash_remove_locked(&t);

            // Free kernel stack
            if (t.kernel_stack) {
                free_kernel_stack_locked(t.kernel_stack);
                t.kernel_stack = nullptr;
                t.kernel_stack_top = nullptr;
            }

            task_lock.release(saved_daif);

            // Clear task slot
            t.id = 0;
            t.state = TaskState::Invalid;
            t.name[0] = '\0';
            t.viper = nullptr;
            t.next = nullptr;
            t.prev = nullptr;
            t.hash_next = nullptr;

            reaped++;
        }
    }

    return reaped;
}

/**
 * @brief Destroy a specific task and reclaim its resources.
 *
 * @details
 * Immediately destroys the task regardless of state. Should only be called
 * for tasks that are not currently running or in the ready queue.
 *
 * @param t Task to destroy.
 */
void destroy(Task *t) {
    if (!t)
        return;

    // Can't destroy current task
    if (t == current()) {
        serial::puts("[task] ERROR: Cannot destroy current task\n");
        return;
    }

    // Can't destroy idle task
    if (t->flags & TASK_FLAG_IDLE) {
        serial::puts("[task] ERROR: Cannot destroy idle task\n");
        return;
    }

    serial::puts("[task] Destroying task '");
    serial::puts(t->name);
    serial::puts("' (id=");
    serial::put_dec(t->id);
    serial::puts(")\n");

    // Acquire lock for hash table and stack pool operations
    u64 saved_daif = task_lock.acquire();

    // Remove from hash table before clearing ID
    hash_remove_locked(t);

    // Free kernel stack
    if (t->kernel_stack) {
        free_kernel_stack_locked(t->kernel_stack);
        t->kernel_stack = nullptr;
        t->kernel_stack_top = nullptr;
    }

    task_lock.release(saved_daif);

    // Clear task slot
    t->id = 0;
    t->state = TaskState::Invalid;
    t->name[0] = '\0';
    t->viper = nullptr;
    t->next = nullptr;
    t->prev = nullptr;
    t->hash_next = nullptr;
}

/** @copydoc task::wakeup */
bool wakeup(Task *t) {
    if (!t)
        return false;

    if (t->state != TaskState::Blocked)
        return false;

    // Remove from any wait queue
    if (t->wait_channel) {
        // Try to dequeue from the wait queue
        sched::WaitQueue *wq = reinterpret_cast<sched::WaitQueue *>(t->wait_channel);
        sched::wait_dequeue(wq, t);
    }

    // Mark as ready and enqueue
    t->state = TaskState::Ready;
    scheduler::enqueue(t);

    return true;
}

/** @copydoc task::kill */
i32 kill(u32 pid, i32 signal) {
    Task *t = get_by_id(pid);
    if (!t) {
        return -1; // Task not found
    }

    // Can't kill idle task
    if (t->flags & TASK_FLAG_IDLE) {
        serial::puts("[task] Cannot kill idle task\n");
        return -1;
    }

    // Handle signals
    switch (signal) {
        case signal::sig::SIGKILL:
        case signal::sig::SIGTERM: {
            serial::puts("[task] Killing task '");
            serial::puts(t->name);
            serial::puts("' (id=");
            serial::put_dec(pid);
            serial::puts(") with signal ");
            serial::put_dec(signal);
            serial::puts("\n");

            // If blocked, remove from wait queue but DO NOT enqueue to ready queue.
            // This avoids the race where wakeup() enqueues, then we set state to Exited,
            // leaving an Exited task in the ready queue (RC-016).
            if (t->state == TaskState::Blocked) {
                if (t->wait_channel) {
                    sched::WaitQueue *wq = reinterpret_cast<sched::WaitQueue *>(t->wait_channel);
                    sched::wait_dequeue(wq, t);
                }
            }

            // If this is the current task, call exit
            if (t == current()) {
                exit(-signal); // Exit with negative signal as code
                // exit() never returns
            }

            // Clear poll/timer waiters for this task to prevent use-after-free
            poll::clear_task_waiters(t);

            // If this is a user task with an associated viper process, mark it as zombie
            if (t->viper) {
                ::viper::Viper *v = reinterpret_cast<::viper::Viper *>(t->viper);
                v->exit_code = -signal;
                v->state = ::viper::ViperState::Zombie;

                // Reparent children to init (viper ID 1)
                ::viper::Viper *init = ::viper::find(1);
                ::viper::Viper *child = v->first_child;
                while (child) {
                    ::viper::Viper *next = child->next_sibling;
                    child->parent = init;
                    if (init) {
                        child->next_sibling = init->first_child;
                        init->first_child = child;
                    }
                    child = next;
                }
                v->first_child = nullptr;

                // Wake parent if waiting for children to exit
                if (v->parent) {
                    sched::wait_wake_one(&v->parent->child_waiters);
                }
            }

            // Mark task as exited
            t->exit_code = -signal;
            t->state = TaskState::Exited;

            return 0;
        }

        case signal::sig::SIGSTOP:
        case signal::sig::SIGCONT:
            // Not implemented - return success
            return 0;

        default:
            // Unknown signal - treat as SIGTERM
            return kill(pid, signal::sig::SIGTERM);
    }
}

} // namespace task
