# Viper Processes and Address Spaces

In ViperOS terminology, a “Viper” is a process-like container:

- owns an address space (user mappings + kernel mappings for exception handling)
- owns a capability table (the process’ authority)
- owns one or more tasks/threads (still evolving)

This page explains the relationship between tasks, Vipers, and address spaces, and how user-mode execution is entered.

## The big picture

Think of the layers as:

- **Task** (`kernel/sched/task.*`): “something the scheduler can run”
- **Viper** (`kernel/viper/viper.*`): “a process container with identity/ownership”
- **AddressSpace** (`kernel/viper/address_space.*`): “the page tables and ASID that define what virtual addresses mean
  for this Viper”
- **Capabilities** (`kernel/cap/*`): “the handles and rights that define what this Viper can do”

## Viper subsystem: a fixed-size process table (for now)

The Viper subsystem in `kernel/viper/viper.cpp` uses a fixed-size table of `Viper` structs plus parallel arrays:

- `vipers[MAX_VIPERS]`
- `address_spaces[MAX_VIPERS]`
- `cap_tables[MAX_VIPERS]`

This is a common bring-up pattern: it avoids dynamic allocation complexity and makes process identity stable.

### Creating a Viper

`viper::create(parent, name)`:

1. Allocates a free `Viper` slot.
2. Initializes a fresh `AddressSpace` (`AddressSpace::init()`).
3. Initializes a fresh capability table (`cap::Table::init()`).
4. Links parent/child relationships (tree).
5. Marks the Viper running and inserts it into a global list for iteration/debugging.

The intent is that later: tasks become “threads inside a Viper”, and Viper teardown reaps those tasks.

Key files:

- `kernel/viper/viper.hpp`
- `kernel/viper/viper.cpp`

## Address spaces: ASIDs + translation tables

`kernel/viper/address_space.cpp` implements a minimal AArch64 user address space:

- allocates an **ASID** from a small bitmap (8-bit ASIDs, 0 reserved for kernel)
- allocates a root L0 table from PMM
- installs an L1 table that preserves kernel mappings for exception entry

### Why copy kernel mappings?

When user space triggers an exception (syscall, page fault, IRQ), the CPU must execute kernel code reliably.

The bring-up approach here is:

- the kernel builds a kernel mapping (via `mmu::init()` and TTBR0)
- each user address space includes those kernel mappings in its own tables so exceptions can run without switching
  translation regimes first

This makes “first user mode” easier to get right, at the cost of more page table complexity later.

### Mapping user memory

The `AddressSpace` API provides helpers such as:

- `alloc_map(virt, size, prot)` to allocate physical pages and map them into user VA space
- `translate(virt)` to translate user VA to physical (useful for copying during ELF load)
- `map(virt, phys, size, prot)` for explicit mappings
- `unmap(virt, size)` to remove mappings

These are the primitives the ELF loader uses.

Key files:

- `kernel/viper/address_space.hpp`
- `kernel/viper/address_space.cpp`
- `kernel/arch/aarch64/mmu.*` (kernel mapping base)

## Entering user mode: from kernel task to EL0

ViperOS reaches user mode through a controlled transition:

1. The kernel loads an ELF into a Viper’s address space (see `kernel/loader/loader.cpp`).
2. It maps a user stack near `viper::layout::USER_STACK_TOP`.
3. It creates a user task (`task::create_user_task(...)`) that remembers:
    - which Viper to run under
    - entry point
    - user stack pointer
4. When that task runs for the first time, it switches to the Viper’s address space and uses
   `enter_user_mode(entry, sp, arg)` to perform the `eret` transition to EL0.

During bring-up there is also an optional “direct user mode” path (bypassing the scheduler), useful for debugging early
user-mode transitions.

Key files:

- `kernel/sched/task.cpp` (`create_user_task`, trampoline)
- `kernel/arch/aarch64/exceptions.S` (mode switch helpers)
- `kernel/arch/aarch64/exceptions.cpp` (EL0 exception handlers)

## How process state is found “right now”

Many subsystems need “current process” state, like “which capability table should this syscall use?”.

The current pattern is:

- find the current task (`task::current()`)
- if it has an associated `Task::viper`, use it
- otherwise fall back to a global “current viper” pointer

This is pragmatic for bring-up and kernel test tasks. Over time, you’d typically make the association mandatory for user
tasks and restrict kernel tasks to explicit “kernel authority”.

## Current limitations and next steps

- Address space destruction is incomplete (page table walking/freeing is TODO).
- ASID allocation is a simple bitmap with no locking (SMP and preemption concerns later).
- Task ownership/reaping for Vipers is not complete (“process exit” is still evolving).

