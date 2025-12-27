/**
 * @file viper.cpp
 * @brief Viper process subsystem implementation.
 *
 * @details
 * Implements the process table and helper routines declared in `viper.hpp`.
 * The current design is deliberately simple for bring-up:
 * - A fixed-size table stores all Viper structures.
 * - Parallel arrays store per-process AddressSpace and capability tables.
 * - A global doubly-linked list enables iteration/debugging.
 *
 * The implementation is not yet fully concurrent and does not currently
 * integrate with per-task ownership or process reaping; those pieces will be
 * layered on as multitasking and user-space mature.
 */

#include "viper.hpp"
#include "../console/serial.hpp"
#include "../fs/vfs/vfs.hpp"
#include "../include/error.hpp"
#include "../mm/pmm.hpp"
#include "../sched/scheduler.hpp"
#include "../sched/task.hpp"
#include "address_space.hpp"

namespace viper
{

// Viper table
static Viper vipers[MAX_VIPERS];
static u64 next_viper_id = 1;
static Viper *all_vipers_head = nullptr;
static Viper *current_viper = nullptr;

// Per-Viper address spaces (stored separately since AddressSpace has methods)
static AddressSpace address_spaces[MAX_VIPERS];

// Per-Viper capability tables
static cap::Table cap_tables[MAX_VIPERS];

// Per-Viper file descriptor tables
static fs::vfs::FDTable fd_tables[MAX_VIPERS];

/** @copydoc viper::init */
void init()
{
    serial::puts("[viper] Initializing Viper subsystem\n");

    // Initialize ASID allocator
    asid_init();

    // Clear all Viper slots
    for (u32 i = 0; i < MAX_VIPERS; i++)
    {
        vipers[i].id = 0;
        vipers[i].state = ViperState::Invalid;
        vipers[i].name[0] = '\0';
        vipers[i].ttbr0 = 0;
        vipers[i].asid = 0;
        vipers[i].cap_table = nullptr;
        vipers[i].fd_table = nullptr;
        vipers[i].task_list = nullptr;
        vipers[i].task_count = 0;
        vipers[i].parent = nullptr;
        vipers[i].first_child = nullptr;
        vipers[i].next_sibling = nullptr;
        vipers[i].exit_code = 0;
        vipers[i].wait_queue = nullptr;
        vipers[i].heap_start = layout::USER_HEAP_BASE;
        vipers[i].heap_break = layout::USER_HEAP_BASE;
        vipers[i].memory_used = 0;
        vipers[i].memory_limit = DEFAULT_MEMORY_LIMIT;
        vipers[i].next_all = nullptr;
        vipers[i].prev_all = nullptr;
    }

    all_vipers_head = nullptr;
    current_viper = nullptr;
    next_viper_id = 1;

    serial::puts("[viper] Viper subsystem initialized\n");
}

/**
 * @brief Allocate a free Viper slot from the global table.
 *
 * @details
 * Scans the fixed-size Viper array for an entry marked @ref ViperState::Invalid.
 * The returned slot is not initialized; callers must transition it through
 * @ref ViperState::Creating and finish initialization before exposing it.
 *
 * @return Pointer to a free Viper slot, or `nullptr` if the table is full.
 */
static Viper *alloc_viper()
{
    for (u32 i = 0; i < MAX_VIPERS; i++)
    {
        if (vipers[i].state == ViperState::Invalid)
        {
            return &vipers[i];
        }
    }
    return nullptr;
}

/**
 * @brief Convert a Viper pointer into its index within the Viper table.
 *
 * @details
 * The viper subsystem stores related resources (address spaces and capability
 * tables) in parallel arrays indexed the same way as the `vipers` table. This
 * helper computes the index by subtracting the base address of the table.
 *
 * The computation assumes that `v` points into the `vipers` array. If `v` does
 * not, the computed index is meaningless; callers treat negative values as an
 * error.
 *
 * @param v Pointer to a Viper (expected to be within the `vipers` array).
 * @return Zero-based index on success, or -1 if `v` is `nullptr`.
 */
static int viper_index(Viper *v)
{
    if (!v)
        return -1;
    uintptr offset = reinterpret_cast<uintptr>(v) - reinterpret_cast<uintptr>(&vipers[0]);
    return static_cast<int>(offset / sizeof(Viper));
}

/** @copydoc viper::create */
Viper *create(Viper *parent, const char *name)
{
    Viper *v = alloc_viper();
    if (!v)
    {
        serial::puts("[viper] ERROR: No free Viper slots!\n");
        return nullptr;
    }

    int idx = viper_index(v);
    if (idx < 0)
    {
        return nullptr;
    }

    // Mark as creating
    v->state = ViperState::Creating;
    v->id = next_viper_id++;

    // Copy name
    int i = 0;
    while (name[i] && i < 31)
    {
        v->name[i] = name[i];
        i++;
    }
    v->name[i] = '\0';

    // Initialize address space
    AddressSpace &as = address_spaces[idx];
    if (!as.init())
    {
        serial::puts("[viper] ERROR: Failed to create address space!\n");
        v->state = ViperState::Invalid;
        v->id = 0;
        return nullptr;
    }

    v->ttbr0 = as.root();
    v->asid = as.asid();

    // Set up parent relationship
    v->parent = parent;
    v->first_child = nullptr;
    v->next_sibling = nullptr;

    if (parent)
    {
        // Add to parent's child list
        v->next_sibling = parent->first_child;
        parent->first_child = v;
    }

    // Initialize heap
    v->heap_start = layout::USER_HEAP_BASE;
    v->heap_break = layout::USER_HEAP_BASE;

    // Initialize resource tracking
    v->memory_used = 0;
    v->memory_limit = DEFAULT_MEMORY_LIMIT;

    // No tasks yet
    v->task_list = nullptr;
    v->task_count = 0;

    // Initialize capability table
    cap::Table &ct = cap_tables[idx];
    if (!ct.init())
    {
        serial::puts("[viper] ERROR: Failed to create capability table!\n");
        as.destroy();
        v->state = ViperState::Invalid;
        v->id = 0;
        return nullptr;
    }
    v->cap_table = &ct;

    // Initialize file descriptor table
    fs::vfs::FDTable &fdt = fd_tables[idx];
    fdt.init();
    v->fd_table = &fdt;

    // Add to global list
    v->next_all = all_vipers_head;
    v->prev_all = nullptr;
    if (all_vipers_head)
    {
        all_vipers_head->prev_all = v;
    }
    all_vipers_head = v;

    // Mark as running
    v->state = ViperState::Running;

    serial::puts("[viper] Created Viper '");
    serial::puts(v->name);
    serial::puts("' ID=");
    serial::put_dec(v->id);
    serial::puts(", ASID=");
    serial::put_dec(v->asid);
    serial::puts(", TTBR0=");
    serial::put_hex(v->ttbr0);
    serial::puts("\n");

    return v;
}

/** @copydoc viper::destroy */
void destroy(Viper *v)
{
    if (!v || v->state == ViperState::Invalid)
        return;

    serial::puts("[viper] Destroying Viper '");
    serial::puts(v->name);
    serial::puts("' ID=");
    serial::put_dec(v->id);
    serial::puts("\n");

    int idx = viper_index(v);
    if (idx >= 0)
    {
        // Close all open file descriptors
        fs::vfs::close_all_fds(&fd_tables[idx]);
        v->fd_table = nullptr;

        // Destroy address space
        address_spaces[idx].destroy();

        // Destroy capability table
        cap_tables[idx].destroy();
    }

    // Remove from global list
    if (v->prev_all)
    {
        v->prev_all->next_all = v->next_all;
    }
    else
    {
        all_vipers_head = v->next_all;
    }
    if (v->next_all)
    {
        v->next_all->prev_all = v->prev_all;
    }

    // Remove from parent's child list
    if (v->parent)
    {
        Viper **pp = &v->parent->first_child;
        while (*pp && *pp != v)
        {
            pp = &(*pp)->next_sibling;
        }
        if (*pp == v)
        {
            *pp = v->next_sibling;
        }
    }

    // TODO: Clean up tasks

    // Mark as invalid
    v->state = ViperState::Invalid;
    v->id = 0;
    v->name[0] = '\0';
}

/** @copydoc viper::current */
Viper *current()
{
    // First check if the current task has an associated viper
    task::Task *t = task::current();
    if (t && t->viper)
    {
        return reinterpret_cast<Viper *>(t->viper);
    }
    // Fall back to global current_viper for compatibility
    return current_viper;
}

/** @copydoc viper::set_current */
void set_current(Viper *v)
{
    current_viper = v;
}

/** @copydoc viper::find */
Viper *find(u64 id)
{
    for (Viper *v = all_vipers_head; v; v = v->next_all)
    {
        if (v->id == id && v->state != ViperState::Invalid)
        {
            return v;
        }
    }
    return nullptr;
}

/** @copydoc viper::print_info */
void print_info(Viper *v)
{
    if (!v)
    {
        serial::puts("[viper] (null)\n");
        return;
    }

    serial::puts("[viper] Viper '");
    serial::puts(v->name);
    serial::puts("':\n");
    serial::puts("  ID: ");
    serial::put_dec(v->id);
    serial::puts("\n");
    serial::puts("  State: ");
    switch (v->state)
    {
        case ViperState::Invalid:
            serial::puts("Invalid");
            break;
        case ViperState::Creating:
            serial::puts("Creating");
            break;
        case ViperState::Running:
            serial::puts("Running");
            break;
        case ViperState::Exiting:
            serial::puts("Exiting");
            break;
        case ViperState::Zombie:
            serial::puts("Zombie");
            break;
    }
    serial::puts("\n");
    serial::puts("  ASID: ");
    serial::put_dec(v->asid);
    serial::puts("\n");
    serial::puts("  TTBR0: ");
    serial::put_hex(v->ttbr0);
    serial::puts("\n");
    serial::puts("  Heap: ");
    serial::put_hex(v->heap_start);
    serial::puts(" - ");
    serial::put_hex(v->heap_break);
    serial::puts("\n");
    serial::puts("  Tasks: ");
    serial::put_dec(v->task_count);
    serial::puts("\n");
}

/** @copydoc viper::current_cap_table */
cap::Table *current_cap_table()
{
    Viper *v = current();
    return v ? v->cap_table : nullptr;
}

// Get address space for a Viper
/** @copydoc viper::get_address_space */
AddressSpace *get_address_space(Viper *v)
{
    if (!v)
        return nullptr;
    int idx = viper_index(v);
    if (idx < 0 || idx >= static_cast<int>(MAX_VIPERS))
        return nullptr;
    return &address_spaces[idx];
}

/** @copydoc viper::exit */
void exit(i32 code)
{
    Viper *v = current();
    if (!v)
        return;

    serial::puts("[viper] Process '");
    serial::puts(v->name);
    serial::puts("' exiting with code ");
    serial::put_dec(code);
    serial::puts("\n");

    // Store exit code and transition to ZOMBIE
    v->exit_code = code;
    v->state = ViperState::Zombie;

    // Reparent children to init (viper ID 1)
    Viper *init = find(1);
    Viper *child = v->first_child;
    while (child)
    {
        Viper *next = child->next_sibling;
        child->parent = init;
        if (init)
        {
            child->next_sibling = init->first_child;
            init->first_child = child;
        }
        child = next;
    }
    v->first_child = nullptr;

    // Wake parent if waiting
    if (v->parent && v->parent->wait_queue)
    {
        task::Task *waiter = v->parent->wait_queue;
        v->parent->wait_queue = nullptr;
        waiter->state = task::TaskState::Ready;
        scheduler::enqueue(waiter);
    }

    // Mark all tasks in this process as exited
    // The current task will be cleaned up by scheduler
}

/** @copydoc viper::wait */
i64 wait(i64 child_id, i32 *status)
{
    Viper *v = current();
    if (!v)
        return error::VERR_NOT_SUPPORTED;

    while (true)
    {
        // Look for a matching zombie child
        for (Viper *child = v->first_child; child; child = child->next_sibling)
        {
            if (child->state == ViperState::Zombie)
            {
                if (child_id == -1 || static_cast<u64>(child_id) == child->id)
                {
                    // Found a zombie to reap
                    i64 pid = static_cast<i64>(child->id);
                    if (status)
                        *status = child->exit_code;
                    reap(child);
                    return pid;
                }
            }
        }

        // Check if we have any children at all
        if (!v->first_child)
        {
            return error::VERR_NOT_FOUND;
        }

        // No zombie found - block and wait
        task::Task *t = task::current();
        if (!t)
            return error::VERR_NOT_SUPPORTED;

        t->state = task::TaskState::Blocked;
        v->wait_queue = t;
        task::yield();

        // When woken, loop to check again
    }
}

/** @copydoc viper::reap */
void reap(Viper *child)
{
    if (!child || child->state != ViperState::Zombie)
        return;

    serial::puts("[viper] Reaping zombie '");
    serial::puts(child->name);
    serial::puts("'\n");

    // Remove from parent's child list
    if (child->parent)
    {
        Viper **pp = &child->parent->first_child;
        while (*pp && *pp != child)
        {
            pp = &(*pp)->next_sibling;
        }
        if (*pp == child)
        {
            *pp = child->next_sibling;
        }
    }

    // Now fully destroy the process
    destroy(child);
}

} // namespace viper
