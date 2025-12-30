#include "task.hpp"
#include "../../include/viperos/task_info.hpp"
#include "../arch/aarch64/exceptions.hpp"
#include "../console/serial.hpp"
#include "../mm/vmm.hpp"
#include "../viper/address_space.hpp"
#include "../viper/viper.hpp"
#include "scheduler.hpp"
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
namespace task
{

namespace
{
// Task storage
Task tasks[MAX_TASKS];
u32 next_task_id = 1;

// Current running task
Task *current_task = nullptr;

// Idle task (always task 0)
Task *idle_task = nullptr;

// Simple string copy
/**
 * @brief Copy a string into a fixed-size buffer with NUL-termination.
 *
 * @details
 * Ensures the destination is always NUL-terminated and never writes more
 * than `max` bytes.
 *
 * @param dst Destination buffer.
 * @param src Source NUL-terminated string.
 * @param max Size of destination buffer in bytes.
 */
void strcpy_safe(char *dst, const char *src, usize max)
{
    usize i = 0;
    while (src[i] && i < max - 1)
    {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

// Allocate a task slot
/**
 * @brief Find an unused task slot in the global task table.
 *
 * @return Pointer to an Invalid task slot, or `nullptr` if table is full.
 */
Task *allocate_task()
{
    for (u32 i = 0; i < MAX_TASKS; i++)
    {
        if (tasks[i].state == TaskState::Invalid)
        {
            return &tasks[i];
        }
    }
    return nullptr;
}

// Allocate kernel stack (bump allocator with free list for recycling)
u8 *stack_pool = nullptr;
usize stack_pool_offset = 0;
constexpr usize PAGE_SIZE = 4096;
constexpr usize GUARD_PAGE_SIZE = PAGE_SIZE; // One 4KB guard page per stack
constexpr usize STACK_SLOT_SIZE = KERNEL_STACK_SIZE + GUARD_PAGE_SIZE;
constexpr usize STACK_POOL_SIZE = STACK_SLOT_SIZE * MAX_TASKS;

// Stack pool base address (64MB into RAM, after kernel and framebuffer)
// QEMU virt machine: RAM starts at 0x40000000, kernel at start, FB at +16MB
constexpr u64 STACK_POOL_BASE = 0x44000000;

// Free stack list for recycling exited task stacks
struct FreeStackNode
{
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
 * @return Pointer to the base of the usable stack, or `nullptr` on exhaustion.
 */
u8 *allocate_kernel_stack()
{
    // First try the free list
    if (free_stack_list)
    {
        FreeStackNode *node = free_stack_list;
        free_stack_list = node->next;
        free_stack_count--;

        // Return the stack (node is at the base of usable stack)
        return reinterpret_cast<u8 *>(node);
    }

    // Fall back to bump allocator
    // First call: initialize pool location
    if (stack_pool == nullptr)
    {
        stack_pool = reinterpret_cast<u8 *>(STACK_POOL_BASE);
        stack_pool_offset = 0;
    }

    if (stack_pool_offset + STACK_SLOT_SIZE > STACK_POOL_SIZE)
    {
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
    u8 *usable_stack = slot_base + GUARD_PAGE_SIZE;

    return usable_stack;
}

/**
 * @brief Free a kernel stack, returning it to the free list for reuse.
 *
 * @param stack Pointer to the usable stack base (as returned by allocate_kernel_stack).
 */
void free_kernel_stack(u8 *stack)
{
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
void idle_task_fn(void *)
{
    while (true)
    {
        asm volatile("wfi");
    }
}
} // namespace

/** @copydoc task::init */
void init()
{
    serial::puts("[task] Initializing task subsystem\n");

    // Clear all task slots
    for (u32 i = 0; i < MAX_TASKS; i++)
    {
        tasks[i].state = TaskState::Invalid;
        tasks[i].id = 0;
    }

    // Create idle task (special - uses task slot 0)
    idle_task = &tasks[0];
    idle_task->id = 0;
    strcpy_safe(idle_task->name, "idle", sizeof(idle_task->name));
    idle_task->state = TaskState::Ready;
    idle_task->flags = TASK_FLAG_KERNEL | TASK_FLAG_IDLE;
    idle_task->time_slice = TIME_SLICE_DEFAULT;
    idle_task->priority = PRIORITY_LOWEST; // Lowest priority
    idle_task->next = nullptr;
    idle_task->prev = nullptr;
    idle_task->kernel_stack = allocate_kernel_stack();
    idle_task->kernel_stack_top = idle_task->kernel_stack + KERNEL_STACK_SIZE;
    idle_task->trap_frame = nullptr;
    idle_task->wait_channel = nullptr;
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
    for (int i = 0; i < 32; i++)
    {
        idle_task->signals.handlers[i] = 0; // SIG_DFL
        idle_task->signals.handler_flags[i] = 0;
        idle_task->signals.handler_mask[i] = 0;
    }
    idle_task->signals.blocked = 0;
    idle_task->signals.pending = 0;
    idle_task->signals.saved_frame = nullptr;

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

    // Set current task to idle initially
    current_task = idle_task;

    serial::puts("[task] Task subsystem initialized\n");
}

/** @copydoc task::create */
Task *create(const char *name, TaskEntry entry, void *arg, u32 flags)
{
    Task *t = allocate_task();
    if (!t)
    {
        serial::puts("[task] ERROR: No free task slots\n");
        return nullptr;
    }

    // Allocate kernel stack
    t->kernel_stack = allocate_kernel_stack();
    if (!t->kernel_stack)
    {
        return nullptr;
    }
    t->kernel_stack_top = t->kernel_stack + KERNEL_STACK_SIZE;

    // Initialize task fields
    t->id = next_task_id++;
    strcpy_safe(t->name, name, sizeof(t->name));
    t->state = TaskState::Ready;
    t->flags = flags | TASK_FLAG_KERNEL; // All tasks are kernel tasks for now
    t->time_slice = TIME_SLICE_DEFAULT;
    t->priority = PRIORITY_DEFAULT;
    t->policy = SchedPolicy::SCHED_OTHER; // Default to normal scheduling
    t->next = nullptr;
    t->prev = nullptr;
    t->wait_channel = nullptr;
    t->exit_code = 0;
    t->trap_frame = nullptr;
    t->cpu_ticks = 0;
    t->switch_count = 0;
    t->parent_id = current_task ? current_task->id : 0;

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
    if (current_task && current_task->cwd[0])
    {
        strcpy_safe(t->cwd, current_task->cwd, sizeof(t->cwd));
    }
    else
    {
        t->cwd[0] = '/';
        t->cwd[1] = '\0';
    }

    // Initialize signal state (kernel tasks don't typically use signals)
    for (int i = 0; i < 32; i++)
    {
        t->signals.handlers[i] = 0; // SIG_DFL
        t->signals.handler_flags[i] = 0;
        t->signals.handler_mask[i] = 0;
    }
    t->signals.blocked = 0;
    t->signals.pending = 0;
    t->signals.saved_frame = nullptr;

    return t;
}

/**
 * @brief Entry trampoline for user-mode tasks.
 *
 * @details
 * This function is called when a user task is first scheduled. It switches
 * to the user's address space and enters user mode via eret.
 */
static void user_task_entry_trampoline(void *)
{
    Task *t = current_task;
    if (!t || !t->viper)
    {
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

    // Enter user mode - this won't return
    enter_user_mode(t->user_entry, t->user_stack, 0);

    // Should never reach here
    serial::puts("[task] PANIC: enter_user_mode returned!\n");
    for (;;)
        asm volatile("wfi");
}

/** @copydoc task::create_user_task */
Task *create_user_task(const char *name, void *viper_ptr, u64 entry, u64 stack)
{
    Task *t = allocate_task();
    if (!t)
    {
        serial::puts("[task] ERROR: No free task slots for user task\n");
        return nullptr;
    }

    // Allocate kernel stack (user tasks still need one for syscalls)
    t->kernel_stack = allocate_kernel_stack();
    if (!t->kernel_stack)
    {
        return nullptr;
    }
    t->kernel_stack_top = t->kernel_stack + KERNEL_STACK_SIZE;

    // Initialize task fields
    t->id = next_task_id++;
    strcpy_safe(t->name, name, sizeof(t->name));
    t->state = TaskState::Ready;
    t->flags = TASK_FLAG_USER; // User task, not kernel
    t->time_slice = TIME_SLICE_DEFAULT;
    t->priority = PRIORITY_DEFAULT;
    t->policy = SchedPolicy::SCHED_OTHER; // Default to normal scheduling
    t->next = nullptr;
    t->prev = nullptr;
    t->wait_channel = nullptr;
    t->exit_code = 0;
    t->trap_frame = nullptr;
    t->cpu_ticks = 0;
    t->switch_count = 0;
    t->parent_id = current_task ? current_task->id : 0;

    // Set up user task fields
    t->viper = reinterpret_cast<struct ViperProcess *>(viper_ptr);
    t->user_entry = entry;
    t->user_stack = stack;

    // Initialize CWD - inherit from parent if exists, otherwise root
    if (current_task && current_task->cwd[0])
    {
        strcpy_safe(t->cwd, current_task->cwd, sizeof(t->cwd));
    }
    else
    {
        t->cwd[0] = '/';
        t->cwd[1] = '\0';
    }

    // Initialize signal state for user task
    for (int i = 0; i < 32; i++)
    {
        t->signals.handlers[i] = 0; // SIG_DFL
        t->signals.handler_flags[i] = 0;
        t->signals.handler_mask[i] = 0;
    }
    t->signals.blocked = 0;
    t->signals.pending = 0;
    t->signals.saved_frame = nullptr;

    // Set up initial context to call user_task_entry_trampoline
    u64 *stack_ptr = reinterpret_cast<u64 *>(t->kernel_stack_top);
    stack_ptr -= 2;
    stack_ptr[0] = reinterpret_cast<u64>(user_task_entry_trampoline);
    stack_ptr[1] = 0; // arg = nullptr (we use current_task instead)

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

/** @copydoc task::current */
Task *current()
{
    return current_task;
}

/** @copydoc task::set_current */
void set_current(Task *t)
{
    current_task = t;
}

/** @copydoc task::exit */
void exit(i32 code)
{
    Task *t = current_task;
    if (!t)
        return;

    serial::puts("[task] Task '");
    serial::puts(t->name);
    serial::puts("' exiting with code ");
    serial::put_dec(code);
    serial::puts("\n");

    // If this is a user task with an associated viper process, exit the process
    // This marks it as a zombie and wakes any waiting parent
    if (t->viper)
    {
        ::viper::exit(code);
    }

    t->exit_code = code;
    t->state = TaskState::Exited;

    // Schedule next task
    scheduler::schedule();

    // Should never get here
    serial::puts("[task] PANIC: exit() returned after schedule!\n");
    while (true)
    {
        asm volatile("wfi");
    }
}

/** @copydoc task::yield */
void yield()
{
    scheduler::schedule();
}

/** @copydoc task::set_priority */
i32 set_priority(Task *t, u8 priority)
{
    if (!t)
        return -1;

    // Don't allow changing idle task priority
    if (t->flags & TASK_FLAG_IDLE)
        return -1;

    t->priority = priority;
    return 0;
}

/** @copydoc task::get_priority */
u8 get_priority(Task *t)
{
    if (!t)
        return PRIORITY_LOWEST;
    return t->priority;
}

/** @copydoc task::set_policy */
i32 set_policy(Task *t, SchedPolicy policy)
{
    if (!t)
        return -1;

    // Validate policy
    if (policy != SchedPolicy::SCHED_OTHER && policy != SchedPolicy::SCHED_FIFO &&
        policy != SchedPolicy::SCHED_RR)
    {
        return -1;
    }

    t->policy = policy;

    // Adjust time slice based on policy
    if (policy == SchedPolicy::SCHED_FIFO)
    {
        // SCHED_FIFO doesn't use time slicing - set max to indicate "run until yield"
        t->time_slice = 0xFFFFFFFF;
    }
    else if (policy == SchedPolicy::SCHED_RR)
    {
        // SCHED_RR uses a fixed RT time slice
        t->time_slice = RT_TIME_SLICE_DEFAULT;
    }
    else
    {
        // SCHED_OTHER uses priority-based time slicing
        t->time_slice = time_slice_for_priority(t->priority);
    }

    return 0;
}

/** @copydoc task::get_policy */
SchedPolicy get_policy(Task *t)
{
    if (!t)
        return SchedPolicy::SCHED_OTHER;
    return t->policy;
}

/** @copydoc task::get_by_id */
Task *get_by_id(u32 id)
{
    for (u32 i = 0; i < MAX_TASKS; i++)
    {
        if (tasks[i].id == id && tasks[i].state != TaskState::Invalid)
        {
            return &tasks[i];
        }
    }
    return nullptr;
}

/** @copydoc task::print_info */
void print_info(Task *t)
{
    if (!t)
    {
        serial::puts("[task] (null task)\n");
        return;
    }

    serial::puts("[task] Task ID ");
    serial::put_dec(t->id);
    serial::puts(" '");
    serial::puts(t->name);
    serial::puts("' state=");

    switch (t->state)
    {
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
u32 list_tasks(TaskInfo *buffer, u32 max_count)
{
    if (!buffer || max_count == 0)
    {
        return 0;
    }

    Task *curr = current_task;
    u32 count = 0;

    // Check if there's a current viper (user process)
    // Only list it separately if the current task isn't a user task
    // (to avoid duplication when using proper scheduled user tasks)
    ::viper::Viper *curr_viper = ::viper::current();
    bool have_user_task = (curr && (curr->flags & TASK_FLAG_USER) && curr->viper);

    if (curr_viper && !have_user_task && count < max_count)
    {
        // Legacy path: viper running without proper task integration
        TaskInfo &info = buffer[count];
        info.id = static_cast<u32>(curr_viper->id);
        info.state = static_cast<u8>(TaskState::Running);
        info.flags = TASK_FLAG_USER;
        info.priority = 128;
        info._pad0 = 0;

        for (usize j = 0; j < 31 && curr_viper->name[j]; j++)
        {
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
    for (u32 i = 0; i < MAX_TASKS && count < max_count; i++)
    {
        Task &t = tasks[i];
        if (t.state != TaskState::Invalid)
        {
            TaskInfo &info = buffer[count];
            info.id = t.id;
            // If this is the current task and no viper is running, report it as Running
            if (&t == curr && !curr_viper)
            {
                info.state = static_cast<u8>(TaskState::Running);
            }
            else
            {
                info.state = static_cast<u8>(t.state);
            }
            info.flags = static_cast<u8>(t.flags);
            info.priority = static_cast<u8>(t.priority);
            info._pad0 = 0;

            // Copy name
            for (usize j = 0; j < 31 && t.name[j]; j++)
            {
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
u32 reap_exited()
{
    u32 reaped = 0;

    for (u32 i = 0; i < MAX_TASKS; i++)
    {
        Task &t = tasks[i];

        // Don't reap idle task
        if (i == 0)
            continue;

        // Don't reap current task (shouldn't be exited anyway)
        if (&t == current_task)
            continue;

        if (t.state == TaskState::Exited)
        {
            serial::puts("[task] Reaping exited task '");
            serial::puts(t.name);
            serial::puts("' (id=");
            serial::put_dec(t.id);
            serial::puts(")\n");

            // Free kernel stack
            if (t.kernel_stack)
            {
                free_kernel_stack(t.kernel_stack);
                t.kernel_stack = nullptr;
                t.kernel_stack_top = nullptr;
            }

            // Clear task slot
            t.id = 0;
            t.state = TaskState::Invalid;
            t.name[0] = '\0';
            t.viper = nullptr;
            t.next = nullptr;
            t.prev = nullptr;

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
void destroy(Task *t)
{
    if (!t)
        return;

    // Can't destroy current task
    if (t == current_task)
    {
        serial::puts("[task] ERROR: Cannot destroy current task\n");
        return;
    }

    // Can't destroy idle task
    if (t->flags & TASK_FLAG_IDLE)
    {
        serial::puts("[task] ERROR: Cannot destroy idle task\n");
        return;
    }

    serial::puts("[task] Destroying task '");
    serial::puts(t->name);
    serial::puts("' (id=");
    serial::put_dec(t->id);
    serial::puts(")\n");

    // Free kernel stack
    if (t->kernel_stack)
    {
        free_kernel_stack(t->kernel_stack);
        t->kernel_stack = nullptr;
        t->kernel_stack_top = nullptr;
    }

    // Clear task slot
    t->id = 0;
    t->state = TaskState::Invalid;
    t->name[0] = '\0';
    t->viper = nullptr;
    t->next = nullptr;
    t->prev = nullptr;
}

/** @copydoc task::wakeup */
bool wakeup(Task *t)
{
    if (!t)
        return false;

    if (t->state != TaskState::Blocked)
        return false;

    // Remove from any wait queue
    if (t->wait_channel)
    {
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
i32 kill(u32 pid, i32 signal)
{
    Task *t = get_by_id(pid);
    if (!t)
    {
        return -1; // Task not found
    }

    // Can't kill idle task
    if (t->flags & TASK_FLAG_IDLE)
    {
        serial::puts("[task] Cannot kill idle task\n");
        return -1;
    }

    // Handle signals
    switch (signal)
    {
        case SIGKILL:
        case SIGTERM:
        {
            serial::puts("[task] Killing task '");
            serial::puts(t->name);
            serial::puts("' (id=");
            serial::put_dec(pid);
            serial::puts(") with signal ");
            serial::put_dec(signal);
            serial::puts("\n");

            // If blocked, wake it up first
            if (t->state == TaskState::Blocked)
            {
                wakeup(t);
            }

            // If this is the current task, call exit
            if (t == current_task)
            {
                exit(-signal); // Exit with negative signal as code
                // exit() never returns
            }

            // Otherwise, mark as exited
            t->exit_code = -signal;
            t->state = TaskState::Exited;

            return 0;
        }

        case SIGSTOP:
        case SIGCONT:
            // Not implemented - return success
            return 0;

        default:
            // Unknown signal - treat as SIGTERM
            return kill(pid, SIGTERM);
    }
}

} // namespace task
