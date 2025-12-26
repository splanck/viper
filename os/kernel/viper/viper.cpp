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
#include "../mm/pmm.hpp"
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
        vipers[i].task_list = nullptr;
        vipers[i].task_count = 0;
        vipers[i].parent = nullptr;
        vipers[i].first_child = nullptr;
        vipers[i].next_sibling = nullptr;
        vipers[i].exit_code = 0;
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

} // namespace viper
